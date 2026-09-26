#include "c2/effector_command_service.hpp"

#include "c2/protocol_validation.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace c2 {
namespace {
bool valid_limits(const PointingLimits& limits) noexcept {
    return std::isfinite(limits.minimum_pan_deg) &&
           std::isfinite(limits.maximum_pan_deg) &&
           std::isfinite(limits.minimum_tilt_deg) &&
           std::isfinite(limits.maximum_tilt_deg) &&
           limits.minimum_pan_deg <= limits.maximum_pan_deg &&
           limits.minimum_tilt_deg <= limits.maximum_tilt_deg;
}

std::uint32_t increment_non_zero(const std::uint32_t value) noexcept {
    return value == std::numeric_limits<std::uint32_t>::max() ? 1U : value + 1U;
}

EffectorCommandError command_error(const PointingError error) noexcept {
    switch (error) {
        case PointingError::invalid_target:
            return EffectorCommandError::invalid_target;
        case PointingError::invalid_effector_pose:
            return EffectorCommandError::invalid_effector_pose;
        case PointingError::coincident_target:
            return EffectorCommandError::coincident_target;
        case PointingError::outside_turret_limits:
            return EffectorCommandError::outside_turret_limits;
    }
    return EffectorCommandError::invalid_target;
}
}  // namespace

EffectorCommandService::EffectorCommandService(
    StateStore& state, const EffectorCommandConfig config)
    : state_(state),
      config_(config),
      next_command_id_(config.first_command_id),
      next_sequence_(config.first_sequence) {
    if (!valid_limits(config_.limits))
        throw std::invalid_argument("effector pointing limits are invalid");
    if (config_.command_validity_us == 0)
        throw std::invalid_argument("command validity must be non-zero");
    if (next_command_id_ == 0)
        throw std::invalid_argument("first command id must be non-zero");
    if (next_sequence_ == 0)
        throw std::invalid_argument("first sequence must be non-zero");
}

EffectorCommandResult EffectorCommandService::create_for_target(
    const std::uint32_t target_id, const std::uint64_t now_us) {
    if (now_us == 0) return EffectorCommandError::invalid_time;
    if (now_us > std::numeric_limits<std::uint64_t>::max() - config_.command_validity_us)
        return EffectorCommandError::deadline_overflow;

    const auto target = state_.target(target_id, now_us);
    if (!target) return EffectorCommandError::target_unavailable;
    const auto effector_pose = state_.asset_pose(ComponentId::effector_asset);
    if (!effector_pose) return EffectorCommandError::effector_pose_unavailable;

    const auto pointing = calculate_effector_pointing(*target, *effector_pose, config_.limits);
    if (const auto* error = std::get_if<PointingError>(&pointing))
        return command_error(*error);
    const auto& solution = std::get<PointingSolution>(pointing);

    std::lock_guard lock(mutex_);
    EffectorTurretCommand command{
        {protocol_version, next_sequence_, now_us, ComponentId::command_and_control,
         ComponentId::effector_asset},
        next_command_id_, target_id, solution.pan_deg, solution.tilt_deg,
        now_us + config_.command_validity_us};
    if (!validate(command).valid())
        throw std::logic_error("generated effector command is invalid");

    next_command_id_ = increment_non_zero(next_command_id_);
    next_sequence_ = increment_non_zero(next_sequence_);
    return command;
}
}  // namespace c2
