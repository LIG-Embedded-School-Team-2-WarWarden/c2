#include "pch.h"

#include "c2/protocol_validation.hpp"

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
}  // namespace
