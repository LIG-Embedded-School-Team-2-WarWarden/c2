#pragma once

#include "c2/asset_registry.hpp"
#include "c2/attack_command_service.hpp"
#include "c2/connection_monitor.hpp"
#include "c2/effector_command_service.hpp"
#include "c2/observation_command_service.hpp"
#include "c2/state_store.hpp"
#include "c2/telemetry_store.hpp"
#include "c2/track_store.hpp"
#include "c2/udp_transport.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <variant>
#include <vector>
#include <unordered_map>

namespace c2 {
struct ServerRuntimeConfig {
    StateStoreConfig state;
    ConnectionMonitorConfig connections;
    ObservationCommandConfig observation_commands;
    EffectorCommandConfig effector_commands;
    AttackCommandConfig attack_commands;
    Endpoint observation_endpoint;
    Endpoint effector_endpoint;
    std::uint32_t emergency_stop_repetitions{3};
    std::uint64_t command_ack_timeout_us{200'000};
    std::uint32_t command_max_attempts{3};
    AssetRegistryConfig registry;
    TrackStoreConfig tracks;
};

struct CommandRetryResult {
    std::size_t resent{};
    std::size_t exhausted{};
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
    [[nodiscard]] InboundResult ingest(
        std::span<const std::byte> datagram, const Endpoint& source,
        std::uint64_t received_at_us);
    [[nodiscard]] ObservationDispatchResult command_observation(
        ObservationTurretCommandType type, float pan_deg, float tilt_deg,
        std::uint64_t now_us);
    [[nodiscard]] EffectorDispatchResult point_effector(
        std::uint32_t target_id, std::uint64_t now_us);
    [[nodiscard]] AttackDispatchResult attack(
        AttackAction action, std::uint32_t target_id,
        std::uint32_t duration_ms, std::uint64_t now_us);
    void send_heartbeats(std::uint64_t now_us, std::uint64_t uptime_ms);
    [[nodiscard]] CommandRetryResult retry_unacknowledged(std::uint64_t now_us);
    [[nodiscard]] std::size_t pending_command_count() const;
    [[nodiscard]] bool pose_resynchronization_required(ComponentId source) const;
    [[nodiscard]] ConnectionState connection_state(
        ComponentId source, std::uint64_t now_us);
    [[nodiscard]] std::vector<TargetCoordinate> targets(std::uint64_t now_us) const;
    [[nodiscard]] std::vector<AssetSnapshot> assets(std::uint64_t now_us);
    [[nodiscard]] std::vector<TrackSnapshot> tracks(std::uint64_t now_us);
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
    template <typename Message>
    void dispatch_tracked(
        const Message& message, const Endpoint& endpoint,
        ComponentId acknowledgement_source, std::uint64_t now_us);
    void acknowledge_delivery(const CommandAck& acknowledgement);
    void assign_effector_identity(EffectorTurretCommand& command);
    void assign_effector_identity(AttackCommand& command);

    ServerRuntimeConfig config_;
    DatagramSender sender_;
    AssetRegistry registry_;
    TrackStore tracks_;
    StateStore state_;
    TelemetryStore telemetry_;
    ConnectionMonitor connections_;
    ObservationCommandService observation_commands_;
    EffectorCommandService effector_commands_;
    AttackCommandService attack_commands_;
    std::mutex effector_identity_mutex_;
    std::uint32_t next_effector_command_id_;
    std::uint32_t next_effector_sequence_;
    std::mutex heartbeat_mutex_;
    std::uint32_t next_observation_heartbeat_sequence_{1};
    std::uint32_t next_effector_heartbeat_sequence_{1};
    struct PendingCommand {
        std::vector<std::byte> datagram;
        Endpoint endpoint;
        std::uint64_t last_sent_us{};
        std::uint32_t attempts{};
    };
    static std::uint64_t pending_key(ComponentId source, std::uint32_t command_id) noexcept;
    mutable std::mutex pending_mutex_;
    std::unordered_map<std::uint64_t, PendingCommand> pending_commands_;
};
}  // namespace c2
