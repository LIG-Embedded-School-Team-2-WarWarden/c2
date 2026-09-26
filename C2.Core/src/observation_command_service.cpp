#include "c2/observation_command_service.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace c2 {
namespace {
bool valid_limits(const ObservationTurretLimits& limits) noexcept {
    return std::isfinite(limits.minimum_pan_deg) &&
           std::isfinite(limits.maximum_pan_deg) &&
           std::isfinite(limits.minimum_tilt_deg) &&
           std::isfinite(limits.maximum_tilt_deg) &&
           limits.minimum_pan_deg <= limits.maximum_pan_deg &&
           limits.minimum_tilt_deg <= limits.maximum_tilt_deg;
}

bool uses_target_angles(const ObservationTurretCommandType type) noexcept {
    return type == ObservationTurretCommandType::absolute_angle ||
           type == ObservationTurretCommandType::scan;
}

bool supported(const ObservationTurretCommandType type) noexcept {
    return type == ObservationTurretCommandType::home ||
           type == ObservationTurretCommandType::stop || uses_target_angles(type);
}

std::uint32_t increment_non_zero(const std::uint32_t value) noexcept {
    return value == std::numeric_limits<std::uint32_t>::max() ? 1U : value + 1U;
}
}  // namespace

ObservationCommandService::ObservationCommandService(ObservationCommandConfig config)
    : config_(config),
      next_command_id_(config.first_command_id),
      next_sequence_(config.first_sequence) {
    if (!valid_limits(config_.limits))
        throw std::invalid_argument("observation turret limits are invalid");
    if (config_.command_validity_us == 0)
        throw std::invalid_argument("command validity must be non-zero");
    if (next_command_id_ == 0)
        throw std::invalid_argument("first command id must be non-zero");
    if (next_sequence_ == 0)
        throw std::invalid_argument("first sequence must be non-zero");
}

ObservationTurretCommand ObservationCommandService::create(
    const ObservationTurretCommandType type,
    float target_pan_deg,
    float target_tilt_deg,
    const std::uint64_t now_us) {
    if (!supported(type)) throw std::invalid_argument("unsupported observation command type");
    if (now_us == 0) throw std::invalid_argument("command time must be non-zero");
    if (now_us > std::numeric_limits<std::uint64_t>::max() - config_.command_validity_us)
        throw std::overflow_error("command validity deadline overflows");

    if (uses_target_angles(type)) {
        if (!std::isfinite(target_pan_deg) || !std::isfinite(target_tilt_deg) ||
            target_pan_deg < config_.limits.minimum_pan_deg ||
            target_pan_deg > config_.limits.maximum_pan_deg ||
            target_tilt_deg < config_.limits.minimum_tilt_deg ||
            target_tilt_deg > config_.limits.maximum_tilt_deg)
            throw std::out_of_range("target angles exceed observation turret limits");
    } else {
        target_pan_deg = 0.0F;
        target_tilt_deg = 0.0F;
    }

    std::lock_guard lock(mutex_);
    ObservationTurretCommand command{
        {protocol_version, next_sequence_, now_us, ComponentId::command_and_control,
         ComponentId::observation_asset},
        next_command_id_, type, target_pan_deg, target_tilt_deg,
        now_us + config_.command_validity_us};
    if (!validate(command, config_.limits).valid())
        throw std::logic_error("generated observation command is invalid");

    next_command_id_ = increment_non_zero(next_command_id_);
    next_sequence_ = increment_non_zero(next_sequence_);
    return command;
}
}  // namespace c2
