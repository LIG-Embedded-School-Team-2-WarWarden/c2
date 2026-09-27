#include "c2/dummy_assets.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace c2 {
namespace {
std::uint32_t next(const std::uint32_t value) noexcept {
    return value == std::numeric_limits<std::uint32_t>::max() ? 1U : value + 1U;
}
bool valid_limits(const ObservationTurretLimits& limits) noexcept {
    return std::isfinite(limits.minimum_pan_deg) && std::isfinite(limits.maximum_pan_deg) &&
           std::isfinite(limits.minimum_tilt_deg) && std::isfinite(limits.maximum_tilt_deg) &&
           limits.minimum_pan_deg <= limits.maximum_pan_deg &&
           limits.minimum_tilt_deg <= limits.maximum_tilt_deg;
}
bool in_limits(const float pan, const float tilt, const ObservationTurretLimits& limits) noexcept {
    return pan >= limits.minimum_pan_deg && pan <= limits.maximum_pan_deg &&
           tilt >= limits.minimum_tilt_deg && tilt <= limits.maximum_tilt_deg;
}
bool newer_command(const std::uint32_t candidate, const std::uint32_t current) noexcept {
    return static_cast<std::int32_t>(candidate - current) > 0;
}
}  // namespace

DummyObservationAsset::DummyObservationAsset(
    AssetPose pose, const ObservationTurretLimits limits,
    const std::size_t maximum_cached_results)
    : pose_(std::move(pose)), limits_(limits),
      maximum_cached_results_(maximum_cached_results) {
    if (!validate(pose_).valid() || pose_.header.source_id != ComponentId::observation_asset)
        throw std::invalid_argument("invalid observation pose");
    if (!valid_limits(limits_)) throw std::invalid_argument("invalid observation limits");
    if (maximum_cached_results_ == 0)
        throw std::invalid_argument("maximum cached results must be non-zero");
    status_.state = ObservationState::standby;
}

ProcessedCommand DummyObservationAsset::handle(
    const ObservationTurretCommand& command, const std::uint64_t now_us) {
    std::lock_guard lock(mutex_);
    if (const auto found = results_.find(command.command_id); found != results_.end())
        return {found->second, true, std::nullopt};
    if (latest_command_id_ && !newer_command(command.command_id, *latest_command_id_)) {
        auto response = ack(command.command_id, CommandResult::rejected,
                            dummy_error::duplicate_command, now_us);
        return {response, true,
                error(dummy_error::duplicate_command, ErrorSeverity::warning,
                      command.command_id, "stale or duplicate observation command", now_us)};
    }
    CommandResult result = CommandResult::completed;
    std::uint32_t error_code{};
    std::string detail;
    if (!validate(command, limits_).valid()) {
        result = CommandResult::rejected;
        error_code = dummy_error::invalid_command;
        detail = "invalid observation command";
    } else if (now_us >= command.valid_until_us) {
        result = CommandResult::rejected;
        error_code = dummy_error::expired_command;
        detail = "expired observation command";
    } else if (command.command_type == ObservationTurretCommandType::scan) {
        status_.current_pan_deg = command.target_pan_deg;
        status_.current_tilt_deg = command.target_tilt_deg;
        status_.state = ObservationState::operating;
        status_.lidar_active = true;
        status_.turret_active = false;
    } else if (command.command_type == ObservationTurretCommandType::absolute_angle) {
        status_.current_pan_deg = command.target_pan_deg;
        status_.current_tilt_deg = command.target_tilt_deg;
        status_.state = ObservationState::standby;
        status_.lidar_active = false;
        status_.turret_active = false;
    } else if (command.command_type == ObservationTurretCommandType::home) {
        status_.current_pan_deg = 0;
        status_.current_tilt_deg = 0;
        status_.state = ObservationState::standby;
        status_.lidar_active = false;
        status_.turret_active = false;
    } else {
        status_.state = ObservationState::standby;
        status_.lidar_active = false;
        status_.turret_active = false;
    }
    const auto response = ack(command.command_id, result, error_code, now_us);
    remember(command.command_id, response);
    std::optional<ErrorReport> report;
    if (error_code != 0)
        report = error(error_code, ErrorSeverity::warning, command.command_id,
                       std::move(detail), now_us);
    return {response, false, std::move(report)};
}

MessageHeader DummyObservationAsset::header(const std::uint64_t now_us) {
    auto value = MessageHeader{protocol_version, sequence_, now_us,
                               ComponentId::observation_asset,
                               ComponentId::command_and_control};
    sequence_ = next(sequence_);
    return value;
}
CommandAck DummyObservationAsset::ack(
    const std::uint32_t id, const CommandResult result,
    const std::uint32_t error_code, const std::uint64_t now_us) {
    return {header(now_us), id, result, error_code, now_us};
}
ErrorReport DummyObservationAsset::error(
    const std::uint32_t error_code, const ErrorSeverity severity,
    const std::uint32_t command_id, std::string detail, const std::uint64_t now_us) {
    return {header(now_us), error_code, severity, command_id, now_us, std::move(detail)};
}
void DummyObservationAsset::remember(
    const std::uint32_t command_id, const CommandAck& result) {
    results_.emplace(command_id, result);
    result_order_.push_back(command_id);
    latest_command_id_ = command_id;
    while (result_order_.size() > maximum_cached_results_) {
        results_.erase(result_order_.front());
        result_order_.pop_front();
    }
}
AssetPose DummyObservationAsset::asset_pose(const std::uint64_t now_us) {
    std::lock_guard lock(mutex_);
    auto value = pose_;
    value.header = header(now_us);
    return value;
}
ObservationStatus DummyObservationAsset::status(const std::uint64_t now_us) {
    std::lock_guard lock(mutex_);
    auto value = status_;
    value.header = header(now_us);
    value.timestamp_us = now_us;
    return value;
}
Heartbeat DummyObservationAsset::heartbeat(
    const std::uint64_t now_us, const std::uint64_t uptime_ms) {
    std::lock_guard lock(mutex_);
    const auto state = status_.state == ObservationState::fault
                           ? AssetOperatingState::fault : AssetOperatingState::operating;
    return {header(now_us), state, uptime_ms, now_us};
}
TargetCoordinate DummyObservationAsset::target(
    const std::uint32_t id, const float x, const float y, const float z,
    const float confidence, const std::uint64_t now_us) {
    std::lock_guard lock(mutex_);
    return {header(now_us), id, now_us, CoordinateFrame::project_frame,
            x, y, z, confidence};
}
bool DummyObservationAsset::observe_control_heartbeat(
    const Heartbeat& value, const std::uint64_t received_at_us) {
    std::lock_guard lock(mutex_);
    if (!validate(value).valid() || value.header.source_id != ComponentId::command_and_control ||
        value.header.destination_id != ComponentId::observation_asset || received_at_us == 0)
        return false;
    last_control_heartbeat_us_ = received_at_us;
    watchdog_tripped_ = false;
    return true;
}
std::optional<ErrorReport> DummyObservationAsset::check_watchdog(
    const std::uint64_t now_us, const std::uint64_t timeout_us) {
    std::lock_guard lock(mutex_);
    if (timeout_us == 0 || last_control_heartbeat_us_ == 0 ||
        now_us <= last_control_heartbeat_us_ ||
        now_us - last_control_heartbeat_us_ <= timeout_us || watchdog_tripped_)
        return std::nullopt;
    status_.state = ObservationState::standby;
    status_.lidar_active = false;
    status_.turret_active = false;
    watchdog_tripped_ = true;
    return error(dummy_error::communication_timeout, ErrorSeverity::warning, 0,
                 "command-and-control heartbeat timed out", now_us);
}

DummyEffectorAsset::DummyEffectorAsset(
    AssetPose pose, const ObservationTurretLimits limits,
    const std::size_t maximum_cached_results)
    : pose_(std::move(pose)), limits_(limits),
      maximum_cached_results_(maximum_cached_results) {
    if (!validate(pose_).valid() || pose_.header.source_id != ComponentId::effector_asset)
        throw std::invalid_argument("invalid effector pose");
    if (!valid_limits(limits_)) throw std::invalid_argument("invalid effector limits");
    if (maximum_cached_results_ == 0)
        throw std::invalid_argument("maximum cached results must be non-zero");
    status_.state = EffectorState::standby;
}

ProcessedCommand DummyEffectorAsset::handle(
    const EffectorTurretCommand& command, const std::uint64_t now_us) {
    std::lock_guard lock(mutex_);
    if (const auto found = results_.find(command.command_id); found != results_.end())
        return {found->second, true, std::nullopt};
    if (latest_command_id_ && !newer_command(command.command_id, *latest_command_id_)) {
        auto response = ack(command.command_id, CommandResult::rejected,
                            dummy_error::duplicate_command, now_us);
        return {response, true,
                error(dummy_error::duplicate_command, ErrorSeverity::warning,
                      command.command_id, "stale or duplicate effector command", now_us)};
    }
    CommandResult result = CommandResult::completed;
    std::uint32_t error_code{};
    std::string detail;
    if (!validate(command).valid()) {
        result = CommandResult::rejected;
        error_code = dummy_error::invalid_command;
        detail = "invalid effector pointing command";
    } else if (!in_limits(command.target_pan_deg, command.target_tilt_deg, limits_)) {
        result = CommandResult::rejected;
        error_code = dummy_error::out_of_range;
        detail = "effector pointing command exceeds turret limits";
    } else if (now_us >= command.valid_until_us) {
        result = CommandResult::rejected;
        error_code = dummy_error::expired_command;
        detail = "expired effector pointing command";
    } else {
        current_target_id_ = command.target_id;
        status_.target_pan_deg = command.target_pan_deg;
        status_.target_tilt_deg = command.target_tilt_deg;
        status_.current_pan_deg = command.target_pan_deg;
        status_.current_tilt_deg = command.target_tilt_deg;
        status_.aligned = true;
        status_.state = EffectorState::ready;
    }
    const auto response = ack(command.command_id, result, error_code, now_us);
    remember(command.command_id, response);
    std::optional<ErrorReport> report;
    if (error_code != 0)
        report = error(error_code, ErrorSeverity::warning, command.command_id,
                       std::move(detail), now_us);
    return {response, false, std::move(report)};
}

ProcessedCommand DummyEffectorAsset::handle(
    const AttackCommand& command, const std::uint64_t now_us) {
    std::lock_guard lock(mutex_);
    if (const auto found = results_.find(command.command_id); found != results_.end())
        return {found->second, true, std::nullopt};
    if (latest_command_id_ && !newer_command(command.command_id, *latest_command_id_)) {
        auto response = ack(command.command_id, CommandResult::rejected,
                            dummy_error::duplicate_command, now_us);
        return {response, true,
                error(dummy_error::duplicate_command, ErrorSeverity::warning,
                      command.command_id, "stale or duplicate attack command", now_us)};
    }
    advance(now_us);
    CommandResult result = CommandResult::completed;
    std::uint32_t error_code{};
    std::string detail;
    if (!validate(command).valid()) {
        result = CommandResult::rejected;
        error_code = dummy_error::invalid_command;
        detail = "invalid attack command";
    } else if (now_us >= command.valid_until_us) {
        result = CommandResult::rejected;
        error_code = dummy_error::expired_command;
        detail = "expired attack command";
    } else if (command.action == AttackAction::emergency_stop) {
        status_.attack_active = false;
        status_.attack_armed = false;
        status_.aligned = false;
        status_.state = EffectorState::standby;
        attack_end_us_ = 0;
    } else if (command.action == AttackAction::stop) {
        status_.attack_active = false;
        status_.attack_armed = false;
        status_.state = status_.aligned ? EffectorState::ready : EffectorState::standby;
        attack_end_us_ = 0;
    } else if (status_.state != EffectorState::ready) {
        result = CommandResult::rejected;
        error_code = dummy_error::invalid_state;
        detail = "effector is not ready";
    } else if (!status_.aligned || command.target_id != current_target_id_) {
        result = CommandResult::rejected;
        error_code = dummy_error::not_aligned;
        detail = "effector is not aligned with the requested target";
    } else if (command.action == AttackAction::arm) {
        status_.attack_armed = true;
    } else if (!status_.attack_armed) {
        result = CommandResult::rejected;
        error_code = dummy_error::not_armed;
        detail = "effector is not armed";
    } else {
        status_.attack_active = true;
        status_.state = EffectorState::active;
        const auto duration_us = static_cast<std::uint64_t>(command.duration_ms) * 1000U;
        attack_end_us_ = now_us > std::numeric_limits<std::uint64_t>::max() - duration_us
                             ? std::numeric_limits<std::uint64_t>::max()
                             : now_us + duration_us;
    }
    const auto response = ack(command.command_id, result, error_code, now_us);
    remember(command.command_id, response);
    std::optional<ErrorReport> report;
    if (error_code != 0)
        report = error(error_code, ErrorSeverity::error, command.command_id,
                       std::move(detail), now_us);
    return {response, false, std::move(report)};
}

void DummyEffectorAsset::advance(const std::uint64_t now_us) {
    if (status_.attack_active && now_us >= attack_end_us_) {
        status_.attack_active = false;
        status_.attack_armed = false;
        status_.state = EffectorState::ready;
    }
}
MessageHeader DummyEffectorAsset::header(const std::uint64_t now_us) {
    auto value = MessageHeader{protocol_version, sequence_, now_us,
                               ComponentId::effector_asset,
                               ComponentId::command_and_control};
    sequence_ = next(sequence_);
    return value;
}
CommandAck DummyEffectorAsset::ack(
    const std::uint32_t id, const CommandResult result,
    const std::uint32_t error_code, const std::uint64_t now_us) {
    return {header(now_us), id, result, error_code, now_us};
}
ErrorReport DummyEffectorAsset::error(
    const std::uint32_t error_code, const ErrorSeverity severity,
    const std::uint32_t command_id, std::string detail, const std::uint64_t now_us) {
    return {header(now_us), error_code, severity, command_id, now_us, std::move(detail)};
}
void DummyEffectorAsset::remember(
    const std::uint32_t command_id, const CommandAck& result) {
    results_.emplace(command_id, result);
    result_order_.push_back(command_id);
    latest_command_id_ = command_id;
    while (result_order_.size() > maximum_cached_results_) {
        results_.erase(result_order_.front());
        result_order_.pop_front();
    }
}
AssetPose DummyEffectorAsset::asset_pose(const std::uint64_t now_us) {
    std::lock_guard lock(mutex_);
    auto value = pose_;
    value.header = header(now_us);
    return value;
}
EffectorStatus DummyEffectorAsset::status(const std::uint64_t now_us) {
    std::lock_guard lock(mutex_);
    advance(now_us);
    auto value = status_;
    value.header = header(now_us);
    value.timestamp_us = now_us;
    return value;
}
Heartbeat DummyEffectorAsset::heartbeat(
    const std::uint64_t now_us, const std::uint64_t uptime_ms) {
    std::lock_guard lock(mutex_);
    advance(now_us);
    auto state = AssetOperatingState::standby;
    if (status_.state == EffectorState::ready) state = AssetOperatingState::ready;
    else if (status_.state == EffectorState::active) state = AssetOperatingState::active;
    else if (status_.state == EffectorState::fault) state = AssetOperatingState::fault;
    return {header(now_us), state, uptime_ms, now_us};
}
bool DummyEffectorAsset::observe_control_heartbeat(
    const Heartbeat& value, const std::uint64_t received_at_us) {
    std::lock_guard lock(mutex_);
    if (!validate(value).valid() || value.header.source_id != ComponentId::command_and_control ||
        value.header.destination_id != ComponentId::effector_asset || received_at_us == 0)
        return false;
    last_control_heartbeat_us_ = received_at_us;
    watchdog_tripped_ = false;
    return true;
}
std::optional<ErrorReport> DummyEffectorAsset::check_watchdog(
    const std::uint64_t now_us, const std::uint64_t timeout_us) {
    std::lock_guard lock(mutex_);
    if (timeout_us == 0 || last_control_heartbeat_us_ == 0 ||
        now_us <= last_control_heartbeat_us_ ||
        now_us - last_control_heartbeat_us_ <= timeout_us || watchdog_tripped_)
        return std::nullopt;
    status_.attack_active = false;
    status_.attack_armed = false;
    status_.state = status_.aligned ? EffectorState::ready : EffectorState::standby;
    attack_end_us_ = 0;
    watchdog_tripped_ = true;
    return error(dummy_error::communication_timeout, ErrorSeverity::critical, 0,
                 "command-and-control heartbeat timed out", now_us);
}
}  // namespace c2
