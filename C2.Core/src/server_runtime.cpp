#include "c2/server_runtime.hpp"

#include "c2/protobuf_codec.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace c2 {
ServerRuntime::ServerRuntime(ServerRuntimeConfig config, DatagramSender sender)
    : config_(std::move(config)),
      sender_(std::move(sender)),
      state_(config_.state),
      connections_(config_.connections),
      observation_commands_(config_.observation_commands),
      effector_commands_(state_, config_.effector_commands),
      attack_commands_(config_.attack_commands) {
    if (!sender_) throw std::invalid_argument("datagram sender must be set");
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
                const auto result = attack_commands_.update_status(message);
                return result == AttackStatusUpdateResult::stored ||
                               result == AttackStatusUpdateResult::duplicate
                           ? InboundResult::accepted
                           : InboundResult::rejected;
            } else if constexpr (
                std::is_same_v<T, ObservationStatus> ||
                std::is_same_v<T, CommandAck> ||
                std::is_same_v<T, ErrorReport>) {
                return InboundResult::accepted;
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
    dispatch(command, config_.effector_endpoint);
    return command;
}

bool ServerRuntime::pose_resynchronization_required(const ComponentId source) const {
    return connections_.pose_resynchronization_required(source);
}

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
}  // namespace c2
