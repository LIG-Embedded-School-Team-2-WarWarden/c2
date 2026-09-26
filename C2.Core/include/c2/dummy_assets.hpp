#pragma once
#include "c2/protocol.hpp"
#include "c2/protocol_validation.hpp"
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace c2 {
struct ProcessedCommand { CommandAck acknowledgement; bool duplicate{}; };

class DummyObservationAsset final {
public:
    DummyObservationAsset(AssetPose pose, ObservationTurretLimits limits);
    [[nodiscard]] ProcessedCommand handle(const ObservationTurretCommand& command, std::uint64_t now_us);
    [[nodiscard]] AssetPose asset_pose(std::uint64_t now_us);
    [[nodiscard]] ObservationStatus status(std::uint64_t now_us);
    [[nodiscard]] Heartbeat heartbeat(std::uint64_t now_us, std::uint64_t uptime_ms);
    [[nodiscard]] TargetCoordinate target(std::uint32_t detection_id, float x_m, float y_m, float z_m, float confidence, std::uint64_t now_us);
    [[nodiscard]] bool observe_control_heartbeat(
        const Heartbeat& heartbeat, std::uint64_t received_at_us);
    [[nodiscard]] bool check_watchdog(
        std::uint64_t now_us, std::uint64_t timeout_us);
private:
    MessageHeader header(std::uint64_t now_us);
    CommandAck ack(std::uint32_t command_id, CommandResult result, std::uint32_t error, std::uint64_t now_us);
    AssetPose pose_;
    ObservationTurretLimits limits_;
    ObservationStatus status_;
    std::uint32_t sequence_{1};
    std::unordered_map<std::uint32_t, CommandAck> results_;
    std::uint64_t last_control_heartbeat_us_{};
    bool watchdog_tripped_{};
    mutable std::mutex mutex_;
};

class DummyEffectorAsset final {
public:
    explicit DummyEffectorAsset(AssetPose pose);
    [[nodiscard]] ProcessedCommand handle(const EffectorTurretCommand& command, std::uint64_t now_us);
    [[nodiscard]] ProcessedCommand handle(const AttackCommand& command, std::uint64_t now_us);
    [[nodiscard]] AssetPose asset_pose(std::uint64_t now_us);
    [[nodiscard]] EffectorStatus status(std::uint64_t now_us);
    [[nodiscard]] Heartbeat heartbeat(std::uint64_t now_us, std::uint64_t uptime_ms);
    [[nodiscard]] bool observe_control_heartbeat(
        const Heartbeat& heartbeat, std::uint64_t received_at_us);
    [[nodiscard]] bool check_watchdog(
        std::uint64_t now_us, std::uint64_t timeout_us);
private:
    MessageHeader header(std::uint64_t now_us);
    CommandAck ack(std::uint32_t command_id, CommandResult result, std::uint32_t error, std::uint64_t now_us);
    void advance(std::uint64_t now_us);
    AssetPose pose_;
    EffectorStatus status_;
    std::uint32_t current_target_id_{};
    std::uint64_t attack_end_us_{};
    std::uint32_t sequence_{1};
    std::unordered_map<std::uint32_t, CommandAck> results_;
    std::uint64_t last_control_heartbeat_us_{};
    bool watchdog_tripped_{};
    mutable std::mutex mutex_;
};
}  // namespace c2
