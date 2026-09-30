#include "pch.h"

#include "c2/dummy_assets.hpp"
#include "c2/protobuf_codec.hpp"
#include "c2/protocol_validation.hpp"
#include "c2/pointing.hpp"

#include <cmath>
#include <limits>

namespace {
c2::DummyEffectorAsset effector() {
    return c2::DummyEffectorAsset({
        {c2::protocol_version, 1, 1, c2::ComponentId::effector_asset,
         c2::ComponentId::command_and_control, 201, 301},
        c2::CoordinateFrame::project_frame, 0, 0, 0, 0});
}

c2::TargetTrackUpdate update(
    const std::uint32_t sequence = 1, const std::uint64_t track_id = 42,
    const bool velocity_valid = true) {
    return {{c2::protocol_version, sequence, 1'000'000,
             c2::ComponentId::command_and_control,
             c2::ComponentId::effector_asset, 201, 301},
            track_id, c2::CoordinateFrame::project_frame,
            100, 0, 0, 0, 10, 0, velocity_valid,
            1'000'000, 3'000'000, 0.9F, 101, 501};
}

c2::AttackCommand attack(
    const std::uint32_t id, const c2::AttackAction action,
    const std::uint64_t now, const std::uint32_t duration = 100) {
    return {{c2::protocol_version, id, now,
             c2::ComponentId::command_and_control,
             c2::ComponentId::effector_asset, 201, 301},
            id, action == c2::AttackAction::stop ||
                        action == c2::AttackAction::emergency_stop ? 0U : 42U,
            action, duration, now + 500'000};
}
}

TEST(TargetVelocityContractTest, DistinguishesStationaryFromUnavailableAndRejectsNonFinite) {
    c2::DummyObservationAsset observation({
        {c2::protocol_version, 1, 1, c2::ComponentId::observation_asset,
         c2::ComponentId::command_and_control, 101, 501},
        c2::CoordinateFrame::project_frame, 0, 0, 0, 0},
        {-180, 180, -90, 90});
    const auto stationary = observation.target(
        1, 1, 2, 3, 0, 0, 0, true, 1.0F, 1'000'000);
    EXPECT_TRUE(c2::validate(stationary).valid());
    EXPECT_TRUE(stationary.velocity_valid);
    EXPECT_EQ(stationary.measurement_time_us, stationary.header.timestamp_us);
    const auto unavailable = observation.target(
        2, 1, 2, 3, 0, 0, 0, false, 1.0F, 1'000'001);
    EXPECT_TRUE(c2::validate(unavailable).valid());
    EXPECT_FALSE(unavailable.velocity_valid);
    auto invalid = stationary;
    invalid.vx_mps = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(c2::validate(invalid).valid());
    invalid = stationary;
    invalid.z_m = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(c2::validate(invalid).valid());
}

TEST(TargetVelocityContractTest, RotatesSensorVelocityWithoutApplyingTranslation) {
    const auto rotated = c2::rotate_velocity_to_project({2, 0, -1}, 90);
    EXPECT_NEAR(rotated.x_mps, 0.0F, 0.0001F);
    EXPECT_NEAR(rotated.y_mps, 2.0F, 0.0001F);
    EXPECT_FLOAT_EQ(rotated.z_mps, -1.0F);
}

TEST(ContinuousTrackingTest, DeadReckonsBetweenMeasurementsAndRoundTripsTrackStream) {
    auto asset = effector();
    const auto track = update();
    EXPECT_TRUE(asset.handle(track, 1'000'000));
    EXPECT_FALSE(asset.control_step(1'500'000).has_value());
    const auto status = asset.status(1'500'000);
    EXPECT_NEAR(status.predicted_y_m, 5.0F, 0.001F);
    EXPECT_NEAR(status.target_pan_deg, 2.8624F, 0.01F);
    EXPECT_EQ(status.tracking_track_id, 42U);

    const auto bytes = c2::protobuf::encode(c2::Envelope{track});
    const auto decoded = c2::protobuf::decode(bytes);
    ASSERT_TRUE(std::holds_alternative<c2::Envelope>(decoded));
    const auto& copy = std::get<c2::TargetTrackUpdate>(
        std::get<c2::Envelope>(decoded).payload);
    EXPECT_EQ(copy.track_id, track.track_id);
    EXPECT_FLOAT_EQ(copy.vy_mps, track.vy_mps);
    EXPECT_EQ(c2::protobuf::encode(std::get<c2::Envelope>(decoded)), bytes);
}

TEST(ContinuousTrackingTest, StartPersistsAfterOutputDurationUntilStop) {
    auto asset = effector();
    ASSERT_TRUE(asset.handle(update(), 1'000'000));
    ASSERT_FALSE(asset.control_step(1'000'000).has_value());
    EXPECT_EQ(asset.handle(attack(1, c2::AttackAction::arm, 1'000'010),
                           1'000'010).acknowledgement.result,
              c2::CommandResult::completed);
    EXPECT_EQ(asset.handle(attack(2, c2::AttackAction::start, 1'000'020),
                           1'000'020).acknowledgement.result,
              c2::CommandResult::completed);
    ASSERT_FALSE(asset.control_step(1'200'000).has_value());
    auto status = asset.status(1'200'000);
    EXPECT_FALSE(status.attack_active);
    EXPECT_TRUE(status.automatic_tracking_active);
    EXPECT_TRUE(status.aligned);

    EXPECT_EQ(asset.handle(attack(3, c2::AttackAction::stop, 1'200'010, 0),
                           1'200'010).acknowledgement.result,
              c2::CommandResult::completed);
    status = asset.status(1'200'010);
    EXPECT_FALSE(status.automatic_tracking_active);
    EXPECT_EQ(status.tracking_stop_reason, c2::TrackingStopReason::operator_stop);
}

TEST(ContinuousTrackingTest, RejectsReplayOtherTrackAndFailsSafeOnExpiry) {
    auto asset = effector();
    ASSERT_TRUE(asset.handle(update(), 1'000'000));
    EXPECT_FALSE(asset.handle(update(), 1'000'001));
    EXPECT_FALSE(asset.handle(update(2, 43), 1'000'002));
    ASSERT_FALSE(asset.control_step(1'000'000).has_value());
    ASSERT_TRUE(asset.control_step(3'000'000).has_value());
    const auto status = asset.status(3'000'000);
    EXPECT_EQ(status.state, c2::EffectorState::fault);
    EXPECT_FALSE(status.attack_active);
    EXPECT_FALSE(status.attack_armed);
    EXPECT_FALSE(status.automatic_tracking_active);
    EXPECT_EQ(status.tracking_stop_reason, c2::TrackingStopReason::target_expired);
}

TEST(ContinuousTrackingTest, FailsSafeWhenLocalPoseBecomesStale) {
    auto asset = effector();
    auto track = update();
    track.valid_until_us = 10'000'000;
    ASSERT_TRUE(asset.handle(track, 1'000'000));
    ASSERT_TRUE(asset.control_step(3'000'002, 10'000'000).has_value());
    const auto status = asset.status(3'000'002);
    EXPECT_EQ(status.tracking_stop_reason, c2::TrackingStopReason::pose_unavailable);
    EXPECT_FALSE(status.attack_active);
    EXPECT_FALSE(status.attack_armed);
}

TEST(ContinuousTrackingTest, AcceptsAReplacementTrackOnlyAfterTrackingStops) {
    auto asset = effector();
    ASSERT_TRUE(asset.handle(update(), 1'000'000));
    ASSERT_FALSE(asset.control_step(1'000'000).has_value());
    ASSERT_EQ(asset.handle(attack(1, c2::AttackAction::arm, 1'000'010),
                           1'000'010).acknowledgement.result,
              c2::CommandResult::completed);
    ASSERT_EQ(asset.handle(attack(2, c2::AttackAction::start, 1'000'020),
                           1'000'020).acknowledgement.result,
              c2::CommandResult::completed);
    EXPECT_FALSE(asset.handle(update(2, 43), 1'000'030));
    ASSERT_EQ(asset.handle(attack(3, c2::AttackAction::stop, 1'000'040, 0),
                           1'000'040).acknowledgement.result,
              c2::CommandResult::completed);
    EXPECT_TRUE(asset.handle(update(2, 43), 1'000'050));
    EXPECT_EQ(asset.status(1'000'050).tracking_track_id, 43U);
}

TEST(ContinuousTrackingTest, NewMeasurementRebasesPredictionState) {
    auto asset = effector();
    ASSERT_TRUE(asset.handle(update(), 1'000'000));
    ASSERT_FALSE(asset.control_step(1'500'000).has_value());
    EXPECT_NEAR(asset.status(1'500'000).predicted_y_m, 5.0F, 0.001F);
    auto corrected = update(2);
    corrected.header.timestamp_us = 1'500'000;
    corrected.measurement_time_us = 1'500'000;
    corrected.valid_until_us = 3'500'000;
    corrected.x_m = 80;
    corrected.y_m = 20;
    corrected.vy_mps = -4;
    ASSERT_TRUE(asset.handle(corrected, 1'500'000));
    ASSERT_FALSE(asset.control_step(1'750'000).has_value());
    const auto status = asset.status(1'750'000);
    EXPECT_NEAR(status.predicted_x_m, 80.0F, 0.001F);
    EXPECT_NEAR(status.predicted_y_m, 19.0F, 0.001F);
    EXPECT_EQ(status.last_target_measurement_time_us, 1'500'000U);
}

TEST(ContinuousTrackingTest, TurretLimitViolationFailsSafe) {
    c2::DummyEffectorAsset asset({
        {c2::protocol_version, 1, 1, c2::ComponentId::effector_asset,
         c2::ComponentId::command_and_control, 201, 301},
        c2::CoordinateFrame::project_frame, 0, 0, 0, 0},
        {-45, 45, -20, 20});
    auto track = update();
    track.x_m = 0;
    track.y_m = 100;
    track.vy_mps = 0;
    ASSERT_TRUE(asset.handle(track, 1'000'000));
    ASSERT_TRUE(asset.control_step(1'000'000).has_value());
    const auto status = asset.status(1'000'000);
    EXPECT_EQ(status.state, c2::EffectorState::fault);
    EXPECT_FALSE(status.attack_armed);
    EXPECT_FALSE(status.attack_active);
    EXPECT_FALSE(status.automatic_tracking_active);
    EXPECT_EQ(status.tracking_stop_reason,
              c2::TrackingStopReason::outside_turret_limits);
}
