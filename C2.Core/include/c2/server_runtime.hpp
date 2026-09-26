#pragma once

#include "c2/attack_command_service.hpp"
#include "c2/connection_monitor.hpp"
#include "c2/effector_command_service.hpp"
#include "c2/observation_command_service.hpp"
#include "c2/state_store.hpp"
#include "c2/telemetry_store.hpp"
#include "c2/udp_transport.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace c2 {
struct ServerRuntimeConfig {
    StateStoreConfig state;
    ConnectionMonitorConfig connections;
    ObservationCommandConfig observation_commands;
    EffectorCommandConfig effector_commands;
    AttackCommandConfig attack_commands;
    Endpoint observation_endpoint;
    Endpoint effector_endpoint;
};

enum class InboundResult { accepted, invalid_packet, unsupported_message, rejected };
enum class DispatchError {
    connection_unavailable,
    pose_resynchronization_required,
    command_rejected,
};

using ObservationDispatchResult =
    std::variant<ObservationTurretCommand, DispatchError>;
using EffectorDispatchResult = std::variant<EffectorTurretCommand, DispatchError>;
using AttackDispatchResult = std::variant<AttackCommand, DispatchError>;

class ServerRuntime final {
public:
    using DatagramSender =
        std::function<void(std::span<const std::byte>, const Endpoint&)>;

    ServerRuntime(ServerRuntimeConfig config, DatagramSender sender);

    [[nodiscard]] InboundResult ingest(
        std::span<const std::byte> datagram, std::uint64_t received_at_us);
    [[nodiscard]] ObservationDispatchResult command_observation(
        ObservationTurretCommandType type, float pan_deg, float tilt_deg,
        std::uint64_t now_us);
    [[nodiscard]] EffectorDispatchResult point_effector(
        std::uint32_t target_id, std::uint64_t now_us);
    [[nodiscard]] AttackDispatchResult attack(
        AttackAction action, std::uint32_t target_id,
        std::uint32_t duration_ms, std::uint64_t now_us);
    [[nodiscard]] bool pose_resynchronization_required(ComponentId source) const;
    [[nodiscard]] std::optional<ObservationStatus> observation_status() const;
    [[nodiscard]] std::optional<EffectorStatus> effector_status() const;
    [[nodiscard]] std::optional<CommandAck> acknowledgement(
        ComponentId source, std::uint32_t command_id) const;
    [[nodiscard]] std::vector<ErrorReport> errors() const;

private:
    [[nodiscard]] std::optional<DispatchError> connection_error(
        ComponentId source, std::uint64_t now_us);
    template <typename Message>
    void dispatch(const Message& message, const Endpoint& endpoint);
    void assign_effector_identity(EffectorTurretCommand& command);
    void assign_effector_identity(AttackCommand& command);

    ServerRuntimeConfig config_;
    DatagramSender sender_;
    StateStore state_;
    TelemetryStore telemetry_;
    ConnectionMonitor connections_;
    ObservationCommandService observation_commands_;
    EffectorCommandService effector_commands_;
    AttackCommandService attack_commands_;
    std::mutex effector_identity_mutex_;
    std::uint32_t next_effector_command_id_;
    std::uint32_t next_effector_sequence_;
};
}  // namespace c2
