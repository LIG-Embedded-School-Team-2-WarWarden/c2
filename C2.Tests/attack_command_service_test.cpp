#include "pch.h"

#include "c2/attack_command_service.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <variant>

namespace {
c2::EffectorStatus status(
    const c2::EffectorState state = c2::EffectorState::ready,
    const bool aligned = true, const bool armed = false,
    const std::uint64_t timestamp = 100, const std::uint32_t sequence = 1,
    const std::uint64_t track_id = 7) {
    return {{c2::protocol_version, sequence, timestamp,
             c2::ComponentId::effector_asset,
             c2::ComponentId::command_and_control},
            state, 0, 0, 0, 0, aligned, armed, false, 0, timestamp, track_id};
}

c2::AttackCommandService service(
    const std::uint32_t first_id = 10, const std::uint32_t first_sequence = 20) {
    return c2::AttackCommandService({500, first_id, first_sequence});
}

TEST(AttackCommandServiceTest, ArmRequiresReadyAlignedStatusAndMatchingTrackedTarget) {
    auto commands = service();
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(c2::AttackAction::arm, 7, 0, 100)),
              c2::AttackCommandError::status_unavailable);
    ASSERT_EQ(commands.update_status(status(c2::EffectorState::ready, true, false, 98, 1, 0)), c2::AttackStatusUpdateResult::stored);
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(c2::AttackAction::arm, 7, 0, 100)), c2::AttackCommandError::target_unavailable);
    ASSERT_EQ(commands.update_status(status(c2::EffectorState::ready, true, false, 99, 2, 8)), c2::AttackStatusUpdateResult::stored);
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(c2::AttackAction::arm, 7, 0, 100)), c2::AttackCommandError::target_mismatch);
    ASSERT_EQ(commands.update_status(status(c2::EffectorState::ready, true, false, 100, 3, 7)), c2::AttackStatusUpdateResult::stored);
    const auto result = commands.create(c2::AttackAction::arm, 7, 0, 100);
    ASSERT_TRUE(std::holds_alternative<c2::AttackCommand>(result));
    const auto& command = std::get<c2::AttackCommand>(result);
    EXPECT_EQ(command.command_id, 10U);
    EXPECT_EQ(command.action, c2::AttackAction::arm);
    EXPECT_EQ(command.duration_ms, 0U);
    EXPECT_EQ(command.valid_until_us, 600U);
}

TEST(AttackCommandServiceTest, ArmRejectsUnsafeEffectorStateAndAlignment) {
    auto commands = service();
    ASSERT_EQ(commands.update_status(status(c2::EffectorState::standby, true)),
              c2::AttackStatusUpdateResult::stored);
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(c2::AttackAction::arm, 7, 0, 100)),
              c2::AttackCommandError::invalid_state);
    ASSERT_EQ(commands.update_status(status(c2::EffectorState::ready, false, false, 101, 2)),
              c2::AttackStatusUpdateResult::stored);
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(c2::AttackAction::arm, 7, 0, 101)),
              c2::AttackCommandError::not_aligned);
}

TEST(AttackCommandServiceTest, StartRequiresArmedReadyAlignedAndDuration) {
    auto commands = service();
    ASSERT_EQ(commands.update_status(status()), c2::AttackStatusUpdateResult::stored);
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(c2::AttackAction::start, 7, 1000, 100)),
              c2::AttackCommandError::not_armed);
    ASSERT_EQ(commands.update_status(status(c2::EffectorState::ready, true, true, 101, 2)),
              c2::AttackStatusUpdateResult::stored);
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(c2::AttackAction::start, 7, 0, 101)),
              c2::AttackCommandError::invalid_duration);

    const auto result = commands.create(c2::AttackAction::start, 7, 1000, 101);
    ASSERT_TRUE(std::holds_alternative<c2::AttackCommand>(result));
    EXPECT_EQ(std::get<c2::AttackCommand>(result).duration_ms, 1000U);
}

TEST(AttackCommandServiceTest, StopAndEmergencyStopBypassOperationalPreconditions) {
    auto commands = service();
    const auto stop = commands.create(c2::AttackAction::stop, 99, 999, 100);
    const auto emergency = commands.create(c2::AttackAction::emergency_stop, 99, 999, 101);
    ASSERT_TRUE(std::holds_alternative<c2::AttackCommand>(stop));
    ASSERT_TRUE(std::holds_alternative<c2::AttackCommand>(emergency));
    EXPECT_EQ(std::get<c2::AttackCommand>(stop).target_id, 0U);
    EXPECT_EQ(std::get<c2::AttackCommand>(stop).duration_ms, 0U);
    EXPECT_EQ(std::get<c2::AttackCommand>(emergency).target_id, 0U);
    EXPECT_EQ(std::get<c2::AttackCommand>(emergency).duration_ms, 0U);
}

TEST(AttackCommandServiceTest, StatusUpdatesRejectInvalidDuplicateAndOutOfOrderData) {
    auto commands = service();
    auto invalid = status();
    invalid.header.source_id = c2::ComponentId::observation_asset;
    EXPECT_EQ(commands.update_status(invalid), c2::AttackStatusUpdateResult::invalid);
    EXPECT_EQ(commands.update_status(status()), c2::AttackStatusUpdateResult::stored);
    EXPECT_EQ(commands.update_status(status()), c2::AttackStatusUpdateResult::duplicate);
    EXPECT_EQ(commands.update_status(status(c2::EffectorState::ready, true, false, 99, 2)),
              c2::AttackStatusUpdateResult::out_of_order);
}

TEST(AttackCommandServiceTest, ArmRejectsStaleTrackedStatus) {
    auto commands = service();
    ASSERT_EQ(commands.update_status(status()), c2::AttackStatusUpdateResult::stored);
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(c2::AttackAction::arm, 7, 0, 600)), c2::AttackCommandError::target_unavailable);
}

TEST(AttackCommandServiceTest, InvalidRequestsDoNotConsumeIdentifiersAndWrapSkipsZero) {
    auto commands = service(std::numeric_limits<std::uint32_t>::max(),
                            std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(c2::AttackAction::unspecified, 0, 0, 100)),
              c2::AttackCommandError::unsupported_action);
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(c2::AttackAction::stop, 0, 0, 0)),
              c2::AttackCommandError::invalid_time);
    const auto last = std::get<c2::AttackCommand>(commands.create(c2::AttackAction::stop, 0, 0, 100));
    const auto wrapped = std::get<c2::AttackCommand>(commands.create(c2::AttackAction::emergency_stop, 0, 0, 101));
    EXPECT_EQ(last.command_id, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(last.header.sequence, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(wrapped.command_id, 1U);
    EXPECT_EQ(wrapped.header.sequence, 1U);
}

TEST(AttackCommandServiceTest, RejectsDeadlineOverflowAndInvalidConfiguration) {
    auto commands = service();
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(
                  c2::AttackAction::emergency_stop, 0, 0,
                  std::numeric_limits<std::uint64_t>::max() - 499)),
              c2::AttackCommandError::deadline_overflow);
    EXPECT_THROW((void)c2::AttackCommandService({0, 1, 1}), std::invalid_argument);
    EXPECT_THROW((void)c2::AttackCommandService({1, 0, 1}), std::invalid_argument);
    EXPECT_THROW((void)c2::AttackCommandService({1, 1, 0}), std::invalid_argument);
}
}  // namespace

TEST(AttackCommandServiceTest, ExplicitContextIsIsolatedFromLegacyStatus) {
    auto commands = service();
    ASSERT_EQ(commands.update_status(status(c2::EffectorState::fault)),
              c2::AttackStatusUpdateResult::stored);
    const c2::AttackCommandContext context{status(c2::EffectorState::ready, true, true), true, 42};
    const auto result = commands.create(c2::AttackAction::start, 42, 100, 300, context);
    ASSERT_TRUE(std::holds_alternative<c2::AttackCommand>(result));
    EXPECT_EQ(std::get<c2::AttackCommand>(result).target_id, 42);
    EXPECT_EQ(std::get<c2::AttackCommand>(result).valid_until_us, 800);
    EXPECT_EQ(std::get<c2::AttackCommandError>(
        commands.create(c2::AttackAction::arm, 7, 0, 150)),
        c2::AttackCommandError::invalid_state);
}

TEST(AttackCommandServiceTest, ExplicitContextEnforcesAttackSafetyPreconditions) {
    auto commands = service();
    c2::AttackCommandContext context{status(), true, 42};
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(
        c2::AttackAction::start, 42, 100, 150, context)), c2::AttackCommandError::not_armed);
    context.status = status(c2::EffectorState::ready, false, true);
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(
        c2::AttackAction::start, 42, 100, 150, context)), c2::AttackCommandError::not_aligned);
    context.status = status(c2::EffectorState::ready, true, true);
    context.target_available = false;
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(
        c2::AttackAction::start, 42, 100, 150, context)), c2::AttackCommandError::target_unavailable);
    context.target_available = true;
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(
        c2::AttackAction::start, 43, 100, 150, context)), c2::AttackCommandError::target_mismatch);
    EXPECT_EQ(std::get<c2::AttackCommandError>(commands.create(
        c2::AttackAction::start, 42, 0, 150, context)), c2::AttackCommandError::invalid_duration);
}
