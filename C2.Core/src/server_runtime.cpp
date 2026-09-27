#include "c2/server_runtime.hpp"

#include "c2/protobuf_codec.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <limits>

namespace c2 {
namespace {
std::uint32_t next_non_zero(std::uint32_t value) noexcept {
    return value == std::numeric_limits<std::uint32_t>::max() ? 1U : value + 1U;
}
}

ServerRuntime::ServerRuntime(ServerRuntimeConfig config, DatagramSender sender)
    : config_(std::move(config)),
      sender_(std::move(sender)),
      registry_(config_.registry),
      tracks_(config_.tracks),
      command_tracker_(config_.commands),
      assignments_(config_.assignments),
      state_(config_.state),
      telemetry_(),
      connections_(config_.connections),
      observation_commands_(config_.observation_commands),
      effector_commands_(state_, config_.effector_commands),
      attack_commands_(config_.attack_commands),
      next_effector_command_id_(config_.effector_commands.first_command_id),
      next_effector_sequence_(config_.effector_commands.first_sequence) {
    if (!sender_) throw std::invalid_argument("datagram sender must be set");
    if (config_.emergency_stop_repetitions == 0)
        throw std::invalid_argument("emergency stop repetitions must be non-zero");
    if (config_.command_ack_timeout_us == 0 || config_.command_max_attempts == 0)
        throw std::invalid_argument("command acknowledgement retry configuration is invalid");
}

InboundResult ServerRuntime::ingest(
    const std::span<const std::byte> datagram, const Endpoint& source,
    const std::uint64_t received_at_us) {
    const auto decoded = protobuf::decode(datagram);
    if (!std::holds_alternative<Envelope>(decoded))
        return InboundResult::invalid_packet;
    const auto& payload = std::get<Envelope>(decoded).payload;
    return std::visit(
        [&](const auto& message) -> InboundResult {
            using T = std::decay_t<decltype(message)>;
            if constexpr (std::is_same_v<T, AssetRegistration>) {
                const auto previous = registry_.asset(
                    message.header.asset_id, received_at_us);
                const auto result = registry_.register_asset(
                    message, source, received_at_us);
                if (result == AssetRegistryResult::session_replaced && previous &&
                    previous->session_id != message.header.session_id) {
                    (void)command_tracker_.end_session(
                        previous->asset_id, previous->session_id, received_at_us);
                    if (previous->role == AssetRole::effector)
                        (void)assignments_.mark_unavailable(
                            previous->asset_id, previous->session_id,
                            received_at_us);
                }
                return result == AssetRegistryResult::registered ||
                               result == AssetRegistryResult::refreshed ||
                               result == AssetRegistryResult::session_replaced
                           ? InboundResult::accepted
                           : InboundResult::rejected;
            } else if constexpr (std::is_same_v<T, AssetUnregister>) {
                const auto result = registry_.unregister_asset(
                    message, source, received_at_us);
                if (result != AssetRegistryResult::unregistered)
                    return InboundResult::rejected;
                (void)command_tracker_.end_session(
                    message.header.asset_id, message.header.session_id,
                    received_at_us);
                (void)assignments_.mark_unavailable(
                    message.header.asset_id, message.header.session_id,
                    received_at_us);
                return InboundResult::accepted;
            } else if constexpr (std::is_same_v<T, Heartbeat>) {
                const auto result = registry_.observe_heartbeat(
                    message, source, received_at_us);
                if (result != AssetRegistryResult::stored &&
                    result != AssetRegistryResult::duplicate)
                    return InboundResult::rejected;
            } else if constexpr (std::is_same_v<T, AssetPose>) {
                const auto result = registry_.update_pose(
                    message, source, received_at_us);
                if (result != AssetRegistryResult::stored &&
                    result != AssetRegistryResult::duplicate)
                    return InboundResult::rejected;
            } else if constexpr (std::is_same_v<T, TargetCoordinate>) {
                if (registry_.authenticate(
                        message.header, source, received_at_us) !=
                    AssetRegistryResult::stored)
                    return InboundResult::rejected;
                const auto result = tracks_.update(message, received_at_us);
                return result.result == TrackUpdateResult::stored ||
                               result.result == TrackUpdateResult::duplicate
                           ? InboundResult::accepted
                           : InboundResult::rejected;
            } else if constexpr (std::is_same_v<T, CommandAck>) {
                if (registry_.authenticate(
                        message.header, source, received_at_us) !=
                    AssetRegistryResult::stored)
                    return InboundResult::rejected;
                const auto result = command_tracker_.observe(
                    message, received_at_us);
                return result == AckUpdateResult::progress ||
                               result == AckUpdateResult::terminal ||
                               result == AckUpdateResult::duplicate
                           ? InboundResult::accepted
                           : InboundResult::rejected;
            } else if constexpr (std::is_same_v<T, EffectorStatus>) {
                const auto result = registry_.update_effector_status(
                    message, source, received_at_us);
                return result == AssetRegistryResult::stored ||
                               result == AssetRegistryResult::duplicate
                           ? InboundResult::accepted
                           : InboundResult::rejected;
            } else if constexpr (std::is_same_v<T, ObservationStatus> ||
                                 std::is_same_v<T, ErrorReport>) {
                if (registry_.authenticate(
                        message.header, source, received_at_us) !=
                    AssetRegistryResult::stored)
                    return InboundResult::rejected;
            } else {
                return InboundResult::unsupported_message;
            }
            return ingest(datagram, received_at_us);
        },
        payload);
}

ObservationDispatchResult ServerRuntime::command_observation(
    const std::uint64_t asset_id, const ObservationTurretCommandType type,
    const float pan_deg, const float tilt_deg, const std::uint64_t now_us) {
    const auto asset = registry_.asset(asset_id, now_us);
    if (!asset || asset->role != AssetRole::observation ||
        asset->connection_state != AssetConnectionState::connected)
        return DispatchError::connection_unavailable;
    if (!asset->pose_synchronized)
        return DispatchError::pose_resynchronization_required;
    try {
        auto command = observation_commands_.create(
            type, pan_deg, tilt_deg, now_us);
        command.header.asset_id = asset->asset_id;
        command.header.session_id = asset->session_id;
        auto encoded = protobuf::encode(Envelope{command});
        const auto tracked = command_tracker_.track(
            c2::PendingCommand{asset->asset_id, asset->session_id,
                               command.command_id, encoded,
                               asset->command_endpoint, now_us,
                               command.valid_until_us});
        if (tracked != CommandTrackResult::tracked)
            return DispatchError::command_rejected;
        sender_(encoded, asset->command_endpoint);
        return command;
    } catch (const std::exception&) {
        return DispatchError::command_rejected;
    }
}

InboundResult ServerRuntime::ingest(
    const std::span<const std::byte> datagram, const std::uint64_t received_at_us) {
    const auto decoded = protobuf::decode(datagram);
    if (!std::holds_alternative<Envelope>(decoded)) return InboundResult::invalid_packet;
    const auto& payload = std::get<Envelope>(decoded).payload;
    return std::visit(
        [&](const auto& message) -> InboundResult {
            using T = std::decay_t<decltype(message)>;
            if constexpr (std::is_same_v<T, AssetPose>) {
                const auto result = state_.update(message);
                if (result == StoreUpdateResult::stored) {
                    (void)connections_.mark_pose_synchronized(
                        message.header.source_id, received_at_us);
                    return InboundResult::accepted;
                }
                return result == StoreUpdateResult::duplicate
                           ? InboundResult::accepted
                           : InboundResult::rejected;
            } else if constexpr (std::is_same_v<T, TargetCoordinate>) {
                const auto result = state_.update(message, received_at_us);
                return result == StoreUpdateResult::stored ||
                               result == StoreUpdateResult::duplicate
                           ? InboundResult::accepted
                           : InboundResult::rejected;
            } else if constexpr (std::is_same_v<T, Heartbeat>) {
                const auto result = connections_.observe(message, received_at_us);
                return result == HeartbeatUpdateResult::stored ||
                               result == HeartbeatUpdateResult::duplicate
                           ? InboundResult::accepted
                           : InboundResult::rejected;
            } else if constexpr (std::is_same_v<T, EffectorStatus>) {
                const auto telemetry_result = telemetry_.update(message);
                const auto safety_result = attack_commands_.update_status(message);
                return (telemetry_result == TelemetryUpdateResult::stored || telemetry_result == TelemetryUpdateResult::duplicate) &&
                               (safety_result == AttackStatusUpdateResult::stored || safety_result == AttackStatusUpdateResult::duplicate)
                           ? InboundResult::accepted
                           : InboundResult::rejected;
            } else if constexpr (std::is_same_v<T, CommandAck>) {
                const auto result = telemetry_.update(message);
                if (result == TelemetryUpdateResult::stored ||
                    result == TelemetryUpdateResult::duplicate) {
                    acknowledge_delivery(message);
                    return InboundResult::accepted;
                }
                return InboundResult::rejected;
            } else if constexpr (std::is_same_v<T, ObservationStatus> ||
                                 std::is_same_v<T, ErrorReport>) {
                const auto result = telemetry_.update(message);
                return result == TelemetryUpdateResult::stored ||
                               result == TelemetryUpdateResult::duplicate
                           ? InboundResult::accepted
                           : InboundResult::rejected;
            } else {
                return InboundResult::unsupported_message;
            }
        },
        payload);
}

ObservationDispatchResult ServerRuntime::command_observation(
    const ObservationTurretCommandType type, const float pan_deg,
    const float tilt_deg, const std::uint64_t now_us) {
    if (const auto error = connection_error(ComponentId::observation_asset, now_us))
        return *error;
    try {
        auto command = observation_commands_.create(type, pan_deg, tilt_deg, now_us);
        dispatch_tracked(command, config_.observation_endpoint,
                         ComponentId::observation_asset, now_us);
        return command;
    } catch (const std::exception&) {
        return DispatchError::command_rejected;
    }
}

EffectorDispatchResult ServerRuntime::point_effector(
    const std::uint32_t target_id, const std::uint64_t now_us) {
    if (const auto error = connection_error(ComponentId::effector_asset, now_us))
        return *error;
    const auto result = effector_commands_.create_for_target(target_id, now_us);
    if (!std::holds_alternative<EffectorTurretCommand>(result))
        return DispatchError::command_rejected;
    auto command = std::get<EffectorTurretCommand>(result);
    assign_effector_identity(command);
    if (!attack_commands_.record_pointing_command(command))
        return DispatchError::command_rejected;
    dispatch_tracked(command, config_.effector_endpoint,
                     ComponentId::effector_asset, now_us);
    return command;
}

AttackDispatchResult ServerRuntime::attack(
    const AttackAction action, const std::uint32_t target_id,
    const std::uint32_t duration_ms, const std::uint64_t now_us) {
    if (action != AttackAction::stop && action != AttackAction::emergency_stop) {
        if (const auto error = connection_error(ComponentId::effector_asset, now_us))
            return *error;
    }
    const auto result = attack_commands_.create(action, target_id, duration_ms, now_us);
    if (!std::holds_alternative<AttackCommand>(result))
        return DispatchError::command_rejected;
    auto command = std::get<AttackCommand>(result);
    assign_effector_identity(command);
    const auto repetitions = action == AttackAction::emergency_stop
                                 ? config_.emergency_stop_repetitions
                                 : 1U;
    if (action == AttackAction::emergency_stop) {
        for (std::uint32_t attempt = 0; attempt < repetitions; ++attempt)
            dispatch(command, config_.effector_endpoint);
    } else {
        dispatch_tracked(command, config_.effector_endpoint,
                         ComponentId::effector_asset, now_us);
    }
    return command;
}

CommandRetryResult ServerRuntime::retry_unacknowledged(const std::uint64_t now_us) {
    std::vector<PendingCommand> retries;
    CommandRetryResult result;
    {
        std::lock_guard lock(pending_mutex_);
        for (auto iterator = pending_commands_.begin(); iterator != pending_commands_.end();) {
            auto& pending = iterator->second;
            const auto due = now_us >= pending.last_sent_us &&
                             now_us - pending.last_sent_us >= config_.command_ack_timeout_us;
            if (!due) {
                ++iterator;
            } else if (pending.attempts >= config_.command_max_attempts) {
                iterator = pending_commands_.erase(iterator);
                ++result.exhausted;
            } else {
                pending.last_sent_us = now_us;
                ++pending.attempts;
                retries.push_back(pending);
                ++iterator;
            }
        }
    }
    for (const auto& retry : retries) {
        sender_(retry.datagram, retry.endpoint);
        ++result.resent;
    }
    const auto dynamic = command_tracker_.poll(now_us);
    for (const auto& retry : dynamic.transmissions) {
        sender_(retry.datagram, retry.endpoint);
        ++result.resent;
    }
    for (const auto& outcome : dynamic.finalized) {
        if (outcome.state == CommandTerminalState::delivery_exhausted)
            ++result.exhausted;
    }
    return result;
}

std::size_t ServerRuntime::pending_command_count() const {
    std::lock_guard lock(pending_mutex_);
    return pending_commands_.size() + command_tracker_.pending_count();
}

void ServerRuntime::send_heartbeats(
    const std::uint64_t now_us, const std::uint64_t uptime_ms) {
    std::lock_guard lock(heartbeat_mutex_);
    Heartbeat observation{
        {protocol_version, next_observation_heartbeat_sequence_, now_us,
         ComponentId::command_and_control, ComponentId::observation_asset},
        AssetOperatingState::operating, uptime_ms, now_us};
    Heartbeat effector{
        {protocol_version, next_effector_heartbeat_sequence_, now_us,
         ComponentId::command_and_control, ComponentId::effector_asset},
        AssetOperatingState::operating, uptime_ms, now_us};
    dispatch(observation, config_.observation_endpoint);
    dispatch(effector, config_.effector_endpoint);
    next_observation_heartbeat_sequence_ =
        next_non_zero(next_observation_heartbeat_sequence_);
    next_effector_heartbeat_sequence_ =
        next_non_zero(next_effector_heartbeat_sequence_);
}

bool ServerRuntime::pose_resynchronization_required(const ComponentId source) const {
    return connections_.pose_resynchronization_required(source);
}

ConnectionState ServerRuntime::connection_state(
    const ComponentId source, const std::uint64_t now_us) {
    return connections_.state(source, now_us);
}

std::vector<TargetCoordinate> ServerRuntime::targets(const std::uint64_t now_us) const {
    return state_.targets(now_us);
}

std::vector<AssetSnapshot> ServerRuntime::assets(const std::uint64_t now_us) {
    return registry_.assets(now_us);
}

std::vector<TrackSnapshot> ServerRuntime::tracks(const std::uint64_t now_us) {
    return tracks_.tracks(now_us);
}

AssignmentDecision ServerRuntime::assign(
    const std::uint64_t track_id, const std::uint64_t now_us) {
    const auto track = tracks_.track(track_id, now_us);
    if (!track) return {AssignmentResult::invalid_track, std::nullopt};
    auto candidates = effector_candidates(now_us);
    invalidate_unsafe_assignment(*track, candidates, now_us);
    return assignments_.assign(*track, candidates);
}

AssignmentDecision ServerRuntime::assign(
    const std::uint64_t track_id, const std::uint64_t effector_asset_id,
    const std::uint64_t now_us) {
    const auto track = tracks_.track(track_id, now_us);
    if (!track) return {AssignmentResult::invalid_track, std::nullopt};
    auto candidates = effector_candidates(now_us);
    invalidate_unsafe_assignment(*track, candidates, now_us);
    return assignments_.assign(*track, candidates, effector_asset_id);
}

std::optional<AssetAssignment> ServerRuntime::assignment(
    const std::uint64_t track_id) const {
    return assignments_.assignment(track_id);
}

std::vector<EffectorCandidate> ServerRuntime::effector_candidates(
    const std::uint64_t now_us) {
    std::vector<EffectorCandidate> candidates;
    for (auto& asset : registry_.assets(AssetRole::effector, now_us)) {
        candidates.push_back({asset, asset.effector_status, asset.status_current,
                              false, false, 0, 0});
    }
    return candidates;
}

void ServerRuntime::invalidate_unsafe_assignment(
    const TrackSnapshot& track,
    const std::vector<EffectorCandidate>& candidates,
    const std::uint64_t now_us) {
    const auto existing = assignments_.assignment(track.track_id);
    if (!existing || existing->state != AssignmentResult::assigned) return;
    std::vector<EffectorCandidate> assigned_candidate;
    for (const auto& candidate : candidates) {
        if (candidate.asset.asset_id == existing->effector_asset_id &&
            candidate.asset.session_id == existing->effector_session_id) {
            assigned_candidate.push_back(candidate);
            break;
        }
    }
    if (!select_effector_candidate(
            track, assigned_candidate, config_.assignments.required_capabilities,
            config_.assignments.weights))
        (void)assignments_.mark_unavailable(
            existing->effector_asset_id, existing->effector_session_id, now_us);
}

std::optional<ObservationStatus> ServerRuntime::observation_status() const { return telemetry_.observation_status(); }
std::optional<EffectorStatus> ServerRuntime::effector_status() const { return telemetry_.effector_status(); }
std::optional<CommandAck> ServerRuntime::acknowledgement(ComponentId source, std::uint32_t command_id) const {
    return telemetry_.acknowledgement(source, command_id);
}
std::vector<ErrorReport> ServerRuntime::errors() const { return telemetry_.errors(); }

std::optional<DispatchError> ServerRuntime::connection_error(
    const ComponentId source, const std::uint64_t now_us) {
    if (connections_.state(source, now_us) != ConnectionState::connected)
        return DispatchError::connection_unavailable;
    if (connections_.pose_resynchronization_required(source))
        return DispatchError::pose_resynchronization_required;
    return std::nullopt;
}

template <typename Message>
void ServerRuntime::dispatch(const Message& message, const Endpoint& endpoint) {
    const auto encoded = protobuf::encode(Envelope{message});
    sender_(encoded, endpoint);
}

template <typename Message>
void ServerRuntime::dispatch_tracked(
    const Message& message, const Endpoint& endpoint,
    const ComponentId acknowledgement_source, const std::uint64_t now_us) {
    auto encoded = protobuf::encode(Envelope{message});
    {
        std::lock_guard lock(pending_mutex_);
        pending_commands_.insert_or_assign(
            pending_key(acknowledgement_source, message.command_id),
            PendingCommand{encoded, endpoint, now_us, 1});
    }
    sender_(encoded, endpoint);
}

void ServerRuntime::acknowledge_delivery(const CommandAck& acknowledgement) {
    std::lock_guard lock(pending_mutex_);
    pending_commands_.erase(pending_key(
        acknowledgement.header.source_id, acknowledgement.command_id));
}

std::uint64_t ServerRuntime::pending_key(
    const ComponentId source, const std::uint32_t command_id) noexcept {
    return (static_cast<std::uint64_t>(source) << 32U) | command_id;
}

void ServerRuntime::assign_effector_identity(EffectorTurretCommand& command) {
    std::lock_guard lock(effector_identity_mutex_);
    command.command_id = next_effector_command_id_;
    command.header.sequence = next_effector_sequence_;
    next_effector_command_id_ = next_non_zero(next_effector_command_id_);
    next_effector_sequence_ = next_non_zero(next_effector_sequence_);
}

void ServerRuntime::assign_effector_identity(AttackCommand& command) {
    std::lock_guard lock(effector_identity_mutex_);
    command.command_id = next_effector_command_id_;
    command.header.sequence = next_effector_sequence_;
    next_effector_command_id_ = next_non_zero(next_effector_command_id_);
    next_effector_sequence_ = next_non_zero(next_effector_sequence_);
}
}  // namespace c2
