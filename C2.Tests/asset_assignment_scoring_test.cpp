#include "pch.h"

#include "c2/asset_assignment.hpp"

#include <cstdint>
#include <vector>

namespace {
c2::TrackSnapshot track(float x, float y, float z = 0) {
    c2::TargetCoordinate measurement{
        {c2::protocol_version, 1, 100, c2::ComponentId::observation_asset,
         c2::ComponentId::command_and_control, 900, 1},
        7, 100, c2::CoordinateFrame::project_frame, x, y, z, 0.9F};
    return {50, 900, 1, 7, measurement, 100, 1'000};
}

c2::EffectorCandidate candidate(
    std::uint64_t asset_id, float x, float y, float pan = 0,
    c2::PanTiltLimits limits = {-180, 180, -90, 90}) {
    c2::AssetSnapshot asset;
    asset.asset_id = asset_id;
    asset.session_id = 10 + asset_id;
    asset.role = c2::AssetRole::effector;
    asset.capabilities = c2::capability::effector_point |
                         c2::capability::effector_attack;
    asset.turret_limits = limits;
    asset.connection_state = c2::AssetConnectionState::connected;
    asset.pose_synchronized = true;
    asset.pose = c2::AssetPose{
        {c2::protocol_version, 1, 100, c2::ComponentId::effector_asset,
         c2::ComponentId::command_and_control, asset_id, 10 + asset_id},
        c2::CoordinateFrame::project_frame, x, y, 0, 0};
    c2::EffectorStatus status{
        {c2::protocol_version, 2, 100, c2::ComponentId::effector_asset,
         c2::ComponentId::command_and_control, asset_id, 10 + asset_id},
        c2::EffectorState::ready, pan, 0, 0, 0,
        false, false, false, 0, 100};
    return {asset, status, true, false, false, 0, 0};
}

c2::AssignmentScoreWeights weights() {
    return {1.0, 1.0, 100.0, 500.0, 25.0};
}
}

TEST(AssetAssignmentScoringTest, FiltersEveryUnsafeOrUnavailableCandidate) {
    auto valid = candidate(1, 0, 0);
    auto wrong_role = candidate(2, 0, 0);
    wrong_role.asset.role = c2::AssetRole::observation;
    auto disconnected = candidate(3, 0, 0);
    disconnected.asset.connection_state = c2::AssetConnectionState::disconnected;
    auto unsynchronized = candidate(4, 0, 0);
    unsynchronized.asset.pose_synchronized = false;
    auto missing_status = candidate(5, 0, 0);
    missing_status.status.reset();
    auto stale_status = candidate(6, 0, 0);
    stale_status.status_current = false;
    auto faulted = candidate(7, 0, 0);
    faulted.faulted = true;
    auto no_capability = candidate(8, 0, 0);
    no_capability.asset.capabilities = c2::capability::effector_point;
    auto attacking = candidate(9, 0, 0);
    attacking.status->attack_active = true;
    auto exclusively_busy = candidate(10, 0, 0);
    exclusively_busy.active_assignments = 1;
    exclusively_busy.asset.concurrent_tasks = false;
    auto outside_limits = candidate(11, 0, 0, 0, {-10, 10, -5, 5});

    const std::vector candidates{
        wrong_role, disconnected, unsynchronized, missing_status, stale_status,
        faulted, no_capability, attacking, exclusively_busy, outside_limits, valid};
    const auto ranked = c2::rank_effector_candidates(
        track(0, 100, 50), candidates,
        c2::capability::effector_attack, weights());

    ASSERT_EQ(ranked.size(), 1U);
    EXPECT_EQ(ranked.front().asset_id, 1U);
}

TEST(AssetAssignmentScoringTest, SelectsByDistanceWhenDistanceWeightDominates) {
    auto score_weights = weights();
    score_weights.rotation_weight = 0;
    const auto selected = c2::select_effector_candidate(
        track(100, 0), {candidate(20, 90, 0), candidate(10, 0, 0)},
        c2::capability::effector_point, score_weights);
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->asset_id, 20U);
    EXPECT_NEAR(selected->distance_m, 10.0, 0.001);
}

TEST(AssetAssignmentScoringTest, SelectsByShortestRotationAndRespondsToWeights) {
    auto near_wrong_direction = candidate(10, 90, 0, 170);
    auto far_aligned = candidate(20, 0, 0, 0);
    auto rotation_weights = weights();
    rotation_weights.distance_weight = 0;
    rotation_weights.rotation_weight = 10;
    auto selected = c2::select_effector_candidate(
        track(100, 0), {near_wrong_direction, far_aligned},
        c2::capability::effector_point, rotation_weights);
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->asset_id, 20U);

    auto distance_weights = rotation_weights;
    distance_weights.distance_weight = 100;
    distance_weights.rotation_weight = 0;
    selected = c2::select_effector_candidate(
        track(100, 0), {near_wrong_direction, far_aligned},
        c2::capability::effector_point, distance_weights);
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->asset_id, 10U);
}

TEST(AssetAssignmentScoringTest, AppliesWorkloadDegradedAndFailurePenalties) {
    auto penalized = candidate(10, 0, 0);
    penalized.asset.concurrent_tasks = true;
    penalized.active_assignments = 1;
    penalized.degraded = true;
    penalized.recent_failures = 2;
    auto idle = candidate(20, 0, 0);

    const auto selected = c2::select_effector_candidate(
        track(100, 0), {penalized, idle},
        c2::capability::effector_point, weights());
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->asset_id, 20U);
}

TEST(AssetAssignmentScoringTest, BreaksEqualScoresByLowestAssetId) {
    const auto selected = c2::select_effector_candidate(
        track(100, 0), {candidate(20, 0, 0), candidate(10, 0, 0)},
        c2::capability::effector_point, weights());
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->asset_id, 10U);
}

TEST(AssetAssignmentScoringTest, RejectsInvalidWeightsAndReturnsNoCandidate) {
    auto invalid = weights();
    invalid.distance_weight = -1;
    EXPECT_THROW((void)c2::rank_effector_candidates(
                     track(1, 0), {}, c2::capability::effector_point, invalid),
                 std::invalid_argument);
    EXPECT_FALSE(c2::select_effector_candidate(
        track(1, 0), {}, c2::capability::effector_point, weights()).has_value());
}
