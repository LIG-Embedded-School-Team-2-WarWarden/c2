#include "pch.h"

#include "c2/protocol_validation.hpp"
#include "c2/pointing.hpp"

#include <limits>

namespace {
c2::MessageHeader header(const c2::ComponentId source, const c2::ComponentId destination) {
    return {c2::protocol_version, 1, 1'000'000, source, destination};
}

TEST(ProtocolContractTest, AcceptsProjectFrameTargetCoordinate) {
    const c2::TargetCoordinate target{
        header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control),
        42, 999'000, c2::CoordinateFrame::project_frame, 10.0F, 20.0F, 3.0F, 0.9F};
    EXPECT_TRUE(c2::validate(target).valid());
}

TEST(ProtocolContractTest, RejectsTargetWithoutExplicitProjectFrame) {
    const c2::TargetCoordinate target{
        header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control),
        42, 999'000, c2::CoordinateFrame::unspecified, 10.0F, 20.0F, 3.0F, 0.9F};
    EXPECT_FALSE(c2::validate(target).valid());
}

TEST(ProtocolContractTest, RejectsInvalidWorldCoordinateAndConfidence) {
    const c2::TargetCoordinate target{
        header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control),
        42, 999'000, c2::CoordinateFrame::project_frame,
        std::numeric_limits<float>::infinity(), 20.0F, 3.0F, 1.1F};
    EXPECT_FALSE(c2::validate(target).valid());
}

TEST(ProtocolContractTest, AcceptsAssetPoseFromEitherAsset) {
    const c2::AssetPose observer{
        header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control),
        c2::CoordinateFrame::project_frame, 0.0F, 0.0F, 0.0F, 0.0F};
    const c2::AssetPose effector{
        header(c2::ComponentId::effector_asset, c2::ComponentId::command_and_control),
        c2::CoordinateFrame::project_frame, 10.0F, 5.0F, 0.0F, 90.0F};
    EXPECT_TRUE(c2::validate(observer).valid());
    EXPECT_TRUE(c2::validate(effector).valid());
}

TEST(ProtocolContractTest, RejectsExpiredTurretCommandContract) {
    const c2::EffectorTurretCommand command{
        header(c2::ComponentId::command_and_control, c2::ComponentId::effector_asset),
        10, 42, 30.0F, 5.0F, 1'000'000};
    EXPECT_FALSE(c2::validate(command).valid());
}

TEST(EffectorPointingTest, CalculatesPanAndTiltFromProjectFrame) {
    const c2::TargetCoordinate target{
        header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control),
        42, 999'000, c2::CoordinateFrame::project_frame, 10.0F, 10.0F, 10.0F, 0.9F};
    const c2::AssetPose effector{
        header(c2::ComponentId::effector_asset, c2::ComponentId::command_and_control),
        c2::CoordinateFrame::project_frame, 0.0F, 0.0F, 0.0F, 0.0F};

    const auto calculated = c2::calculate_effector_pointing(target, effector);
    ASSERT_TRUE(std::holds_alternative<c2::PointingSolution>(calculated));
    const auto& solution = std::get<c2::PointingSolution>(calculated);
    EXPECT_NEAR(solution.pan_deg, 45.0F, 1.0e-4F);
    EXPECT_NEAR(solution.tilt_deg, 35.26439F, 1.0e-4F);
}

TEST(EffectorPointingTest, AppliesEffectorInstallationPositionAndAzimuth) {
    const c2::TargetCoordinate target{
        header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control),
        42, 999'000, c2::CoordinateFrame::project_frame, 10.0F, 10.0F, 0.0F, 0.9F};
    const c2::AssetPose effector{
        header(c2::ComponentId::effector_asset, c2::ComponentId::command_and_control),
        c2::CoordinateFrame::project_frame, 10.0F, 0.0F, 0.0F, 90.0F};

    const auto calculated = c2::calculate_effector_pointing(target, effector);
    ASSERT_TRUE(std::holds_alternative<c2::PointingSolution>(calculated));
    const auto& solution = std::get<c2::PointingSolution>(calculated);
    EXPECT_NEAR(solution.relative_x_m, 10.0F, 1.0e-4F);
    EXPECT_NEAR(solution.relative_y_m, 0.0F, 1.0e-4F);
    EXPECT_NEAR(solution.pan_deg, 0.0F, 1.0e-4F);
}

TEST(EffectorPointingTest, RejectsCommandOutsideConfiguredLimits) {
    const c2::TargetCoordinate target{
        header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control),
        42, 999'000, c2::CoordinateFrame::project_frame, 0.0F, 10.0F, 0.0F, 0.9F};
    const c2::AssetPose effector{
        header(c2::ComponentId::effector_asset, c2::ComponentId::command_and_control),
        c2::CoordinateFrame::project_frame, 0.0F, 0.0F, 0.0F, 0.0F};

    const auto calculated = c2::calculate_effector_pointing(
        target, effector, c2::PointingLimits{-45.0F, 45.0F, -20.0F, 20.0F});
    ASSERT_TRUE(std::holds_alternative<c2::PointingError>(calculated));
    EXPECT_EQ(std::get<c2::PointingError>(calculated), c2::PointingError::outside_turret_limits);
}
}  // namespace
