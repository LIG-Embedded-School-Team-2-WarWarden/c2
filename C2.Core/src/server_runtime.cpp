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
            } else if constexpr (std::is_same_v<T, ObservationStatus> ||
                                 std::is_same_v<T, CommandAck> ||
                                 std::is_same_v<T, ErrorReport>) {
                const auto result = telemetry_.update(message);
                return result == TelemetryUpdateResult::stored || result == TelemetryUpdateResult::duplicate
                           ? InboundResult::accepted : InboundResult::rejected;
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
        dispatch(command, config_.observation_endpoint);
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
    dispatch(command, config_.effector_endpoint);
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
    for (std::uint32_t attempt = 0; attempt < repetitions; ++attempt)
        dispatch(command, config_.effector_endpoint);
    return command;
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
