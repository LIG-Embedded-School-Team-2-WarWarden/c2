#include "pch.h"

#include "c2/protocol_validation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>

namespace {
c2::MessageHeader header(const c2::ComponentId source, const c2::ComponentId destination) {
    return {c2::protocol_version, 1, 1'000'000, source, destination};
}

c2::AssetPose valid_pose(const c2::ComponentId source = c2::ComponentId::effector_asset) {
    return {header(source, c2::ComponentId::command_and_control),
            c2::CoordinateFrame::project_frame,
            10.0F,
            5.0F,
            2.0F,
            90.0F};
}

c2::TargetCoordinate valid_target() {
    return {header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control),
            42,
            999'000,
            c2::CoordinateFrame::project_frame,
            10.0F,
            20.0F,
            3.0F,
            0.9F};
}

c2::EffectorTurretCommand valid_command() {
    return {header(c2::ComponentId::command_and_control, c2::ComponentId::effector_asset),
            10,
            42,
            30.0F,
            5.0F,
            1'000'001};
}

c2::ObservationTurretCommand valid_observation_command(
    const c2::ObservationTurretCommandType type = c2::ObservationTurretCommandType::scan) {
    return {header(c2::ComponentId::command_and_control, c2::ComponentId::observation_asset),
            20,
            type,
            30.0F,
            -5.0F,
            1'000'001};
}

bool has_error(const c2::ValidationResult& result, const std::string_view expected) {
    return std::find(result.errors.begin(), result.errors.end(), expected) != result.errors.end();
}

TEST(ProtocolHeaderTest, AcceptsSupportedHeader) {
    EXPECT_TRUE(c2::validate_header(
                    header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control),
                    c2::ComponentId::observation_asset,
                    c2::ComponentId::command_and_control)
                    .valid());
}

TEST(ProtocolHeaderTest, RejectsEachInvalidHeaderField) {
    const auto validate = [](const c2::MessageHeader& candidate) {
        return c2::validate_header(candidate,
                                   c2::ComponentId::observation_asset,
                                   c2::ComponentId::command_and_control);
    };

    auto candidate = header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control);
    candidate.protocol_version_value = c2::protocol_version + 1;
    EXPECT_TRUE(has_error(validate(candidate), "unsupported protocol_version"));

    candidate = header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control);
    candidate.sequence = 0;
    EXPECT_TRUE(has_error(validate(candidate), "sequence must be non-zero"));

    candidate = header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control);
    candidate.timestamp_us = 0;
    EXPECT_TRUE(has_error(validate(candidate), "timestamp_us must be non-zero"));

    candidate = header(c2::ComponentId::effector_asset, c2::ComponentId::command_and_control);
    EXPECT_TRUE(has_error(validate(candidate), "unexpected source_id"));

    candidate = header(c2::ComponentId::observation_asset, c2::ComponentId::effector_asset);
    EXPECT_TRUE(has_error(validate(candidate), "unexpected destination_id"));
}

TEST(AssetPoseContractTest, AcceptsProjectFramePoseFromEitherAsset) {
    EXPECT_TRUE(c2::validate(valid_pose(c2::ComponentId::observation_asset)).valid());
    EXPECT_TRUE(c2::validate(valid_pose(c2::ComponentId::effector_asset)).valid());
}

TEST(AssetPoseContractTest, AcceptsAzimuthBoundaryValues) {
    auto pose = valid_pose();
    pose.azimuth_deg = 0.0F;
    EXPECT_TRUE(c2::validate(pose).valid());

    pose.azimuth_deg = std::nextafter(360.0F, 0.0F);
    EXPECT_TRUE(c2::validate(pose).valid());
}

TEST(AssetPoseContractTest, RejectsUnsupportedSourceAndCoordinateFrame) {
    auto pose = valid_pose(c2::ComponentId::command_and_control);
    auto result = c2::validate(pose);
    EXPECT_TRUE(has_error(result, "AssetPose source must be an observation or effector asset"));

    pose = valid_pose();
    pose.coordinate_frame = c2::CoordinateFrame::unspecified;
    result = c2::validate(pose);
    EXPECT_TRUE(has_error(result, "AssetPose must use PROJECT_FRAME"));
}

TEST(AssetPoseContractTest, RejectsNonFinitePositionOnEveryAxis) {
    const auto non_finite = std::numeric_limits<float>::infinity();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        SCOPED_TRACE(axis);
        auto pose = valid_pose();
        std::array<float*, 3> coordinates{&pose.x_m, &pose.y_m, &pose.z_m};
        *coordinates[axis] = non_finite;
        EXPECT_TRUE(has_error(c2::validate(pose), "AssetPose position must be finite"));
    }
}

TEST(AssetPoseContractTest, RejectsAzimuthOutsideHalfOpenRangeOrNonFinite) {
    for (const auto azimuth : {-0.001F,
                               360.0F,
                               std::numeric_limits<float>::infinity(),
                               std::numeric_limits<float>::quiet_NaN()}) {
        SCOPED_TRACE(azimuth);
        auto pose = valid_pose();
        pose.azimuth_deg = azimuth;
        EXPECT_TRUE(has_error(c2::validate(pose), "azimuth_deg must be in [0, 360)"));
    }
}

TEST(TargetCoordinateContractTest, AcceptsProjectFrameAndConfidenceBoundaries) {
    auto target = valid_target();
    target.confidence = 0.0F;
    EXPECT_TRUE(c2::validate(target).valid());

    target.confidence = 1.0F;
    EXPECT_TRUE(c2::validate(target).valid());
}

TEST(TargetCoordinateContractTest, RejectsMissingIdentityAndMeasurementTime) {
    auto target = valid_target();
    target.detection_id = 0;
    EXPECT_TRUE(has_error(c2::validate(target), "detection_id must be non-zero"));

    target = valid_target();
    target.measurement_time_us = 0;
    EXPECT_TRUE(has_error(c2::validate(target), "measurement_time_us must be non-zero"));
}

TEST(TargetCoordinateContractTest, RejectsTargetWithoutExplicitProjectFrame) {
    auto target = valid_target();
    target.coordinate_frame = c2::CoordinateFrame::unspecified;
    EXPECT_TRUE(has_error(
        c2::validate(target), "TargetCoordinate must use PROJECT_FRAME world coordinates"));
}

TEST(TargetCoordinateContractTest, RejectsNonFiniteWorldCoordinateOnEveryAxis) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        SCOPED_TRACE(axis);
        auto target = valid_target();
        std::array<float*, 3> coordinates{&target.x_m, &target.y_m, &target.z_m};
        *coordinates[axis] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_TRUE(
            has_error(c2::validate(target), "TargetCoordinate position must be finite"));
    }
}

TEST(TargetCoordinateContractTest, RejectsConfidenceOutsideClosedRangeOrNonFinite) {
    for (const auto confidence : {-0.001F,
                                  1.001F,
                                  std::numeric_limits<float>::infinity(),
                                  std::numeric_limits<float>::quiet_NaN()}) {
        SCOPED_TRACE(confidence);
        auto target = valid_target();
        target.confidence = confidence;
        EXPECT_TRUE(has_error(c2::validate(target), "confidence must be in [0, 1]"));
    }
}

TEST(EffectorTurretCommandContractTest, AcceptsFiniteCommandWithFutureExpiry) {
    EXPECT_TRUE(c2::validate(valid_command()).valid());
}

TEST(EffectorTurretCommandContractTest, RejectsMissingCommandOrTargetIdentity) {
    auto command = valid_command();
    command.command_id = 0;
    EXPECT_TRUE(has_error(c2::validate(command), "command_id must be non-zero"));

    command = valid_command();
    command.target_id = 0;
    EXPECT_TRUE(has_error(c2::validate(command), "target_id must be non-zero"));
}

TEST(EffectorTurretCommandContractTest, RejectsNonFinitePanOrTilt) {
    auto command = valid_command();
    command.target_pan_deg = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(has_error(c2::validate(command), "target angles must be finite"));

    command = valid_command();
    command.target_tilt_deg = std::numeric_limits<float>::infinity();
    EXPECT_TRUE(has_error(c2::validate(command), "target angles must be finite"));
}

TEST(EffectorTurretCommandContractTest, RejectsExpiryAtOrBeforeCommandTimestamp) {
    auto command = valid_command();
    command.valid_until_us = command.header.timestamp_us;
    EXPECT_TRUE(has_error(
        c2::validate(command), "valid_until_us must be later than timestamp_us"));

    command.valid_until_us = command.header.timestamp_us - 1;
    EXPECT_TRUE(has_error(
        c2::validate(command), "valid_until_us must be later than timestamp_us"));
}

TEST(ObservationTurretCommandContractTest, AcceptsEverySupportedCommandType) {
    for (const auto type : {c2::ObservationTurretCommandType::home,
                            c2::ObservationTurretCommandType::stop,
                            c2::ObservationTurretCommandType::absolute_angle,
                            c2::ObservationTurretCommandType::scan}) {
        SCOPED_TRACE(static_cast<std::uint32_t>(type));
        EXPECT_TRUE(c2::validate(valid_observation_command(type)).valid());
    }
}

TEST(ObservationTurretCommandContractTest, RejectsUnspecifiedCommandType) {
    for (const auto type : {c2::ObservationTurretCommandType::unspecified,
                            static_cast<c2::ObservationTurretCommandType>(99)}) {
        const auto result = c2::validate(valid_observation_command(type));
        EXPECT_TRUE(
            has_error(result, "command_type must be HOME, STOP, ABSOLUTE_ANGLE, or SCAN"));
    }
}

TEST(ObservationTurretCommandContractTest, RejectsInvalidRouteIdentityAndExpiry) {
    auto command = valid_observation_command();
    command.header.destination_id = c2::ComponentId::effector_asset;
    EXPECT_TRUE(has_error(c2::validate(command), "unexpected destination_id"));

    command = valid_observation_command();
    command.command_id = 0;
    EXPECT_TRUE(has_error(c2::validate(command), "command_id must be non-zero"));

    command = valid_observation_command();
    command.valid_until_us = command.header.timestamp_us;
    EXPECT_TRUE(has_error(
        c2::validate(command), "valid_until_us must be later than timestamp_us"));

    command.valid_until_us = command.header.timestamp_us - 1;
    EXPECT_TRUE(has_error(
        c2::validate(command), "valid_until_us must be later than timestamp_us"));
}

TEST(ObservationTurretCommandContractTest, RejectsNonFiniteTargetAngles) {
    auto command = valid_observation_command();
    command.target_pan_deg = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(has_error(c2::validate(command), "target angles must be finite"));

    command = valid_observation_command(c2::ObservationTurretCommandType::absolute_angle);
    command.target_tilt_deg = std::numeric_limits<float>::infinity();
    EXPECT_TRUE(has_error(c2::validate(command), "target angles must be finite"));
}

TEST(ObservationTurretCommandContractTest, AcceptsTargetAnglesOnConfiguredLimits) {
    const c2::ObservationTurretLimits limits{-90.0F, 90.0F, -30.0F, 45.0F};
    auto command = valid_observation_command();
    command.target_pan_deg = limits.minimum_pan_deg;
    command.target_tilt_deg = limits.minimum_tilt_deg;
    EXPECT_TRUE(c2::validate(command, limits).valid());

    command.target_pan_deg = limits.maximum_pan_deg;
    command.target_tilt_deg = limits.maximum_tilt_deg;
    EXPECT_TRUE(c2::validate(command, limits).valid());
}

TEST(ObservationTurretCommandContractTest, RejectsEachTargetAngleOutsideConfiguredLimits) {
    const c2::ObservationTurretLimits limits{-90.0F, 90.0F, -30.0F, 45.0F};
    struct Case {
        float pan;
        float tilt;
    };
    constexpr std::array cases{
        Case{-90.001F, 0.0F},
        Case{90.001F, 0.0F},
        Case{0.0F, -30.001F},
        Case{0.0F, 45.001F},
    };

    for (const auto& test_case : cases) {
        SCOPED_TRACE(test_case.pan);
        SCOPED_TRACE(test_case.tilt);
        auto command = valid_observation_command();
        command.target_pan_deg = test_case.pan;
        command.target_tilt_deg = test_case.tilt;
        EXPECT_TRUE(has_error(
            c2::validate(command, limits), "target angles exceed observation turret limits"));
    }
}

TEST(ObservationTurretCommandContractTest, HomeAndStopIgnoreTargetAngleLimits) {
    const c2::ObservationTurretLimits limits{-90.0F, 90.0F, -30.0F, 45.0F};
    for (const auto type : {c2::ObservationTurretCommandType::home,
                            c2::ObservationTurretCommandType::stop}) {
        auto command = valid_observation_command(type);
        command.target_pan_deg = 999.0F;
        command.target_tilt_deg = -999.0F;
        EXPECT_TRUE(c2::validate(command, limits).valid());
    }
}

TEST(ObservationTurretCommandContractTest, RejectsInvalidLimitConfiguration) {
    auto result = c2::validate(
        valid_observation_command(), c2::ObservationTurretLimits{90.0F, -90.0F, 45.0F, -30.0F});
    EXPECT_TRUE(has_error(result, "observation turret limits are invalid"));

    result = c2::validate(
        valid_observation_command(),
        c2::ObservationTurretLimits{
            -90.0F, 90.0F, -30.0F, std::numeric_limits<float>::quiet_NaN()});
    EXPECT_TRUE(has_error(result, "observation turret limits are invalid"));
}
}  // namespace
