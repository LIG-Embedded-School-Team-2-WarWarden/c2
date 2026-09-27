#include "c2/attack_command_service.hpp"

#include "c2/protocol_validation.hpp"

#include <limits>
#include <stdexcept>

namespace c2 {
namespace {
std::uint32_t increment_non_zero(const std::uint32_t value) noexcept {
    return value == std::numeric_limits<std::uint32_t>::max() ? 1U : value + 1U;
}

bool supported(const AttackAction action) noexcept {
    return action == AttackAction::arm || action == AttackAction::start ||
           action == AttackAction::stop || action == AttackAction::emergency_stop;
}

bool safety_override(const AttackAction action) noexcept {
    return action == AttackAction::stop || action == AttackAction::emergency_stop;
}
}  // namespace

AttackCommandService::AttackCommandService(const AttackCommandConfig config)
    : config_(config),
      next_command_id_(config.first_command_id),
      next_sequence_(config.first_sequence) {
    if (config_.command_validity_us == 0)
        throw std::invalid_argument("command validity must be non-zero");
    if (next_command_id_ == 0)
        throw std::invalid_argument("first command id must be non-zero");
    if (next_sequence_ == 0)
        throw std::invalid_argument("first sequence must be non-zero");
}

AttackStatusUpdateResult AttackCommandService::update_status(
    const EffectorStatus& status) {
    if (!validate(status).valid()) return AttackStatusUpdateResult::invalid;
    std::lock_guard lock(mutex_);
    if (status_) {
        if (status.header.sequence == status_->header.sequence &&
            status.timestamp_us == status_->timestamp_us)
            return AttackStatusUpdateResult::duplicate;
        if (status.timestamp_us <= status_->timestamp_us)
            return AttackStatusUpdateResult::out_of_order;
    }
    status_ = status;
    return AttackStatusUpdateResult::stored;
}

bool AttackCommandService::record_pointing_command(
    const EffectorTurretCommand& command) {
    if (!validate(command).valid()) return false;
    std::lock_guard lock(mutex_);
    if (pointing_command_ &&
        command.header.timestamp_us <= pointing_command_->header.timestamp_us)
        return false;
    pointing_command_ = command;
    return true;
}

AttackCommandResult AttackCommandService::create(
    const AttackAction action, std::uint64_t target_id,
    std::uint32_t duration_ms, const std::uint64_t now_us) {
    if (!supported(action)) return AttackCommandError::unsupported_action;
    if (now_us == 0) return AttackCommandError::invalid_time;
    if (now_us > std::numeric_limits<std::uint64_t>::max() - config_.command_validity_us)
        return AttackCommandError::deadline_overflow;

    std::lock_guard lock(mutex_);
    if (!safety_override(action)) {
        if (!status_) return AttackCommandError::status_unavailable;
        if (!pointing_command_) return AttackCommandError::target_unavailable;
        if (now_us > pointing_command_->valid_until_us)
            return AttackCommandError::target_unavailable;
        if (target_id == 0 || target_id != pointing_command_->target_id)
            return AttackCommandError::target_mismatch;
        if (status_->state != EffectorState::ready)
            return AttackCommandError::invalid_state;
        if (!status_->aligned) return AttackCommandError::not_aligned;
        if (action == AttackAction::start && !status_->attack_armed)
            return AttackCommandError::not_armed;
        if (action == AttackAction::start && duration_ms == 0)
            return AttackCommandError::invalid_duration;
    } else {
        target_id = 0;
        duration_ms = 0;
    }

    AttackCommand command{
        {protocol_version, next_sequence_, now_us, ComponentId::command_and_control,
         ComponentId::effector_asset},
        next_command_id_, target_id, action, duration_ms,
        now_us + config_.command_validity_us};
    if (!validate(command).valid())
        throw std::logic_error("generated attack command is invalid");
    next_command_id_ = increment_non_zero(next_command_id_);
    next_sequence_ = increment_non_zero(next_sequence_);
    return command;
}
}  // namespace c2
