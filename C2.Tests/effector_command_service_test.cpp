#include "pch.h"

#include "c2/effector_command_service.hpp"
#include "c2/protocol_validation.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <variant>

namespace {
c2::MessageHeader header(
    const c2::ComponentId source, const std::uint32_t sequence,
    const std::uint64_t timestamp_us) {
    return {c2::protocol_version, sequence, timestamp_us, source,
            c2::ComponentId::command_and_control};
}

c2::AssetPose effector_pose(
    const float x = 0.0F, const float y = 0.0F, const float z = 0.0F,
    const float azimuth = 0.0F, const std::uint64_t timestamp_us = 100) {
    return {header(c2::ComponentId::effector_asset, 1, timestamp_us),
            c2::CoordinateFrame::project_frame, x, y, z, azimuth};
}

c2::AssetPose observation_pose() {
    return {header(c2::ComponentId::observation_asset, 1, 100),
            c2::CoordinateFrame::project_frame, 100.0F, 100.0F, 0.0F, 0.0F};
}

c2::TargetCoordinate target(
    const std::uint32_t id, const float x, const float y, const float z,
    const std::uint64_t measurement_time_us = 100,
    const std::uint32_t sequence = 1) {
    return {header(c2::ComponentId::observation_asset, sequence,
                   measurement_time_us + 1),
            id, measurement_time_us, c2::CoordinateFrame::project_frame,
            x, y, z, 0.9F};
}

c2::EffectorCommandConfig config(
    const std::uint32_t first_command_id = 10,
    const std::uint32_t first_sequence = 20) {
    return {{-90.0F, 90.0F, -30.0F, 45.0F}, 500, first_command_id,
            first_sequence};
}

TEST(EffectorCommandServiceTest, RequiresCurrentTargetAndEffectorPose) {
    c2::StateStore store({1'000, 4});
    c2::EffectorCommandService commands(store, config());

    EXPECT_EQ(
        std::get<c2::EffectorCommandError>(commands.create_for_target(7, 200)),
        c2::EffectorCommandError::target_unavailable);
    ASSERT_EQ(store.update(target(7, 10.0F, 0.0F, 0.0F), 200),
              c2::StoreUpdateResult::stored);
    EXPECT_EQ(
        std::get<c2::EffectorCommandError>(commands.create_for_target(7, 200)),
        c2::EffectorCommandError::effector_pose_unavailable);
}

TEST(EffectorCommandServiceTest, CreatesPointingCommandFromProjectTargetAndEffectorPose) {
    c2::StateStore store({1'000, 4});
    ASSERT_EQ(store.update(observation_pose()), c2::StoreUpdateResult::stored);
    ASSERT_EQ(store.update(effector_pose(10.0F, 20.0F, 2.0F, 90.0F)),
              c2::StoreUpdateResult::stored);
    ASSERT_EQ(store.update(target(7, 20.0F, 20.0F, 12.0F), 200),
              c2::StoreUpdateResult::stored);
    c2::EffectorCommandService commands(store, config());

    const auto result = commands.create_for_target(7, 200);
    ASSERT_TRUE(std::holds_alternative<c2::EffectorTurretCommand>(result));
    const auto& command = std::get<c2::EffectorTurretCommand>(result);
    EXPECT_EQ(command.header.sequence, 20U);
    EXPECT_EQ(command.header.timestamp_us, 200U);
    EXPECT_EQ(command.header.source_id, c2::ComponentId::command_and_control);
    EXPECT_EQ(command.header.destination_id, c2::ComponentId::effector_asset);
    EXPECT_EQ(command.command_id, 10U);
    EXPECT_EQ(command.target_id, 7U);
    EXPECT_NEAR(command.target_pan_deg, -90.0F, 0.001F);
    EXPECT_NEAR(command.target_tilt_deg, 45.0F, 0.001F);
    EXPECT_EQ(command.valid_until_us, 700U);
    EXPECT_TRUE(c2::validate(command).valid());
}

TEST(EffectorCommandServiceTest, UsesLatestTargetAndPoseWithoutApplyingObservationPose) {
    c2::StateStore store({1'000, 4});
    ASSERT_EQ(store.update(observation_pose()), c2::StoreUpdateResult::stored);
    ASSERT_EQ(store.update(effector_pose()), c2::StoreUpdateResult::stored);
    ASSERT_EQ(store.update(target(7, 10.0F, 0.0F, 0.0F, 100, 1), 200),
              c2::StoreUpdateResult::stored);
    ASSERT_EQ(store.update(target(7, 0.0F, 10.0F, 0.0F, 101, 2), 200),
              c2::StoreUpdateResult::stored);
    ASSERT_EQ(store.update(effector_pose(0.0F, 0.0F, 0.0F, 90.0F, 101)),
              c2::StoreUpdateResult::stored);
    c2::EffectorCommandService commands(store, config());

    const auto result = commands.create_for_target(7, 200);
    ASSERT_TRUE(std::holds_alternative<c2::EffectorTurretCommand>(result));
    EXPECT_NEAR(std::get<c2::EffectorTurretCommand>(result).target_pan_deg, 0.0F, 0.001F);
}

TEST(EffectorCommandServiceTest, ReportsExpiredOutOfRangeAndCoincidentTargets) {
    c2::StateStore expired_store({100, 4});
    ASSERT_EQ(expired_store.update(effector_pose()), c2::StoreUpdateResult::stored);
    ASSERT_EQ(expired_store.update(target(1, 10.0F, 0.0F, 0.0F), 100),
              c2::StoreUpdateResult::stored);
    c2::EffectorCommandService expired_commands(expired_store, config());
    EXPECT_EQ(
        std::get<c2::EffectorCommandError>(expired_commands.create_for_target(1, 201)),
        c2::EffectorCommandError::target_unavailable);

    c2::StateStore store({1'000, 4});
    ASSERT_EQ(store.update(effector_pose()), c2::StoreUpdateResult::stored);
    ASSERT_EQ(store.update(target(2, -10.0F, 0.0F, 0.0F), 100),
              c2::StoreUpdateResult::stored);
    ASSERT_EQ(store.update(target(3, 0.0F, 0.0F, 0.0F), 100),
              c2::StoreUpdateResult::stored);
    c2::EffectorCommandService commands(store, config());
    EXPECT_EQ(
        std::get<c2::EffectorCommandError>(commands.create_for_target(2, 100)),
        c2::EffectorCommandError::outside_turret_limits);
    EXPECT_EQ(
        std::get<c2::EffectorCommandError>(commands.create_for_target(3, 100)),
        c2::EffectorCommandError::coincident_target);
}

TEST(EffectorCommandServiceTest, FailedRequestsDoNotConsumeIdentifiers) {
    c2::StateStore store({1'000, 4});
    ASSERT_EQ(store.update(effector_pose()), c2::StoreUpdateResult::stored);
    ASSERT_EQ(store.update(target(1, 10.0F, 0.0F, 0.0F), 100),
              c2::StoreUpdateResult::stored);
    c2::EffectorCommandService commands(store, config());

    EXPECT_EQ(
        std::get<c2::EffectorCommandError>(commands.create_for_target(99, 100)),
        c2::EffectorCommandError::target_unavailable);
    EXPECT_EQ(
        std::get<c2::EffectorCommandError>(commands.create_for_target(1, 0)),
        c2::EffectorCommandError::invalid_time);
    const auto result = commands.create_for_target(1, 100);
    ASSERT_TRUE(std::holds_alternative<c2::EffectorTurretCommand>(result));
    EXPECT_EQ(std::get<c2::EffectorTurretCommand>(result).command_id, 10U);
    EXPECT_EQ(std::get<c2::EffectorTurretCommand>(result).header.sequence, 20U);
}

TEST(EffectorCommandServiceTest, HandlesDeadlineAndIdentifierBoundaries) {
    c2::StateStore store({1'000, 4});
    const auto near_max = std::numeric_limits<std::uint64_t>::max() - 100;
    ASSERT_EQ(store.update(effector_pose(0, 0, 0, 0, near_max)),
              c2::StoreUpdateResult::stored);
    ASSERT_EQ(store.update(target(1, 10, 0, 0, near_max), near_max),
              c2::StoreUpdateResult::stored);
    c2::EffectorCommandService overflow_commands(store, config());
    EXPECT_EQ(
        std::get<c2::EffectorCommandError>(overflow_commands.create_for_target(1, near_max)),
        c2::EffectorCommandError::deadline_overflow);

    c2::EffectorCommandService commands(
        store, config(std::numeric_limits<std::uint32_t>::max(),
                      std::numeric_limits<std::uint32_t>::max()));
    const auto last = std::get<c2::EffectorTurretCommand>(commands.create_for_target(1, 100));
    const auto wrapped = std::get<c2::EffectorTurretCommand>(commands.create_for_target(1, 101));
    EXPECT_EQ(last.command_id, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(last.header.sequence, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(wrapped.command_id, 1U);
    EXPECT_EQ(wrapped.header.sequence, 1U);
}

TEST(EffectorCommandServiceTest, RejectsInvalidConfiguration) {
    c2::StateStore store({1'000, 4});
    EXPECT_THROW((void)c2::EffectorCommandService(store, {{1, -1, -1, 1}, 1, 1, 1}),
                 std::invalid_argument);
    EXPECT_THROW((void)c2::EffectorCommandService(store, {{-1, 1, -1, 1}, 0, 1, 1}),
                 std::invalid_argument);
    EXPECT_THROW((void)c2::EffectorCommandService(store, {{-1, 1, -1, 1}, 1, 0, 1}),
                 std::invalid_argument);
    EXPECT_THROW((void)c2::EffectorCommandService(store, {{-1, 1, -1, 1}, 1, 1, 0}),
                 std::invalid_argument);
}
}  // namespace
