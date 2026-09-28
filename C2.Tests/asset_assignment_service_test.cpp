#include "pch.h"

#include "c2/asset_assignment.hpp"

#include <atomic>
#include <cstdint>
#include <latch>
#include <thread>
#include <vector>

namespace {
c2::TrackSnapshot assignment_track(std::uint64_t track_id, float x = 100) {
    c2::TargetCoordinate measurement{
        {c2::protocol_version, 1, 100, c2::ComponentId::observation_asset,
         c2::ComponentId::command_and_control, 900, 1},
        static_cast<std::uint32_t>(track_id), 100,
        c2::CoordinateFrame::project_frame, x, 0, 0, 0.9F};
    return {track_id, 900, 1, static_cast<std::uint32_t>(track_id),
            measurement, 100, 1'000};
}

c2::EffectorCandidate assignment_candidate(
    std::uint64_t asset_id, float x = 0, bool concurrent = false) {
    c2::AssetSnapshot asset;
    asset.asset_id = asset_id;
    asset.session_id = 100 + asset_id;
    asset.role = c2::AssetRole::effector;
    asset.capabilities = c2::capability::effector_point |
                         c2::capability::effector_attack;
    asset.turret_limits = {-180, 180, -90, 90};
    asset.concurrent_tasks = concurrent;
    asset.connection_state = c2::AssetConnectionState::connected;
    asset.pose_synchronized = true;
    asset.pose = c2::AssetPose{
        {c2::protocol_version, 1, 100, c2::ComponentId::effector_asset,
         c2::ComponentId::command_and_control, asset_id, 100 + asset_id},
        c2::CoordinateFrame::project_frame, x, 0, 0, 0};
    c2::EffectorStatus status{
        {c2::protocol_version, 2, 100, c2::ComponentId::effector_asset,
         c2::ComponentId::command_and_control, asset_id, 100 + asset_id},
        c2::EffectorState::ready, 0, 0, 0, 0,
        false, false, false, 0, 100};
    return {asset, status, true, false, false, 0, 0};
}

c2::AssetAssignmentConfig assignment_config() {
    return {{1, 1, 100, 500, 25},
            c2::capability::effector_attack, 32};
}
}

TEST(AssetAssignmentServiceTest, AssignsBestCandidateAndPreventsExclusiveDoubleBooking) {
    c2::AssetAssignmentService service(assignment_config());
    const std::vector candidates{assignment_candidate(10)};
    const auto first = service.assign(assignment_track(1), candidates);
    EXPECT_EQ(first.result, c2::AssignmentResult::assigned);
    ASSERT_TRUE(first.assignment.has_value());
    EXPECT_EQ(first.assignment->effector_asset_id, 10U);

    const auto second = service.assign(assignment_track(2), candidates);
    EXPECT_EQ(second.result, c2::AssignmentResult::temporarily_unavailable);
    EXPECT_FALSE(second.assignment.has_value());
}

TEST(AssetAssignmentServiceTest, MakesConcurrentAssignmentRequestsAtomic) {
    c2::AssetAssignmentService service(assignment_config());
    const std::vector candidates{assignment_candidate(10)};
    std::latch ready(2);
    std::latch start(1);
    std::atomic<int> assigned{};
    std::atomic<int> unavailable{};
    std::jthread first([&] {
        ready.count_down();
        start.wait();
        const auto result = service.assign(assignment_track(1), candidates).result;
        if (result == c2::AssignmentResult::assigned) ++assigned;
        if (result == c2::AssignmentResult::temporarily_unavailable) ++unavailable;
    });
    std::jthread second([&] {
        ready.count_down();
        start.wait();
        const auto result = service.assign(assignment_track(2), candidates).result;
        if (result == c2::AssignmentResult::assigned) ++assigned;
        if (result == c2::AssignmentResult::temporarily_unavailable) ++unavailable;
    });
    ready.wait();
    start.count_down();
    first.join();
    second.join();
    EXPECT_EQ(assigned.load(), 1);
    EXPECT_EQ(unavailable.load(), 1);
}

TEST(AssetAssignmentServiceTest, ManualSelectionCannotBypassSafetyChecks) {
    c2::AssetAssignmentService service(assignment_config());
    auto unsafe = assignment_candidate(10);
    unsafe.asset.pose_synchronized = false;
    const auto rejected = service.assign(
        assignment_track(1), {unsafe, assignment_candidate(20)}, 10);
    EXPECT_EQ(rejected.result, c2::AssignmentResult::temporarily_unavailable);
    EXPECT_FALSE(rejected.assignment.has_value());

    const auto selected = service.assign(
        assignment_track(1), {unsafe, assignment_candidate(20)}, 20);
    ASSERT_EQ(selected.result, c2::AssignmentResult::assigned);
    ASSERT_TRUE(selected.assignment.has_value());
    EXPECT_TRUE(selected.assignment->manually_selected);
    EXPECT_EQ(selected.assignment->effector_asset_id, 20U);
}

TEST(AssetAssignmentServiceTest, ReassignsPreAttackLossButNotActiveAttackLoss) {
    c2::AssetAssignmentService service(assignment_config());
    const auto first = assignment_candidate(10, 90);
    const auto backup = assignment_candidate(20, 0);
    ASSERT_EQ(service.assign(assignment_track(1), {first, backup}).result,
              c2::AssignmentResult::assigned);
    ASSERT_EQ(service.mark_unavailable(10, 110, 110), 1U);
    ASSERT_TRUE(service.assignment(1).has_value());
    EXPECT_EQ(service.assignment(1)->state, c2::AssignmentResult::assignment_lost);
    auto unavailable_first = first;
    unavailable_first.asset.connection_state = c2::AssetConnectionState::disconnected;
    const auto reassigned = service.assign(
        assignment_track(1), {unavailable_first, backup});
    ASSERT_EQ(reassigned.result, c2::AssignmentResult::assigned);
    EXPECT_EQ(reassigned.assignment->effector_asset_id, 20U);

    ASSERT_TRUE(service.mark_attack_started(1));
    ASSERT_EQ(service.mark_unavailable(20, 120, 120), 1U);
    EXPECT_EQ(service.assignment(1)->state,
              c2::AssignmentResult::operator_action_required);
    const auto prohibited = service.assign(
        assignment_track(1), {assignment_candidate(30)});
    EXPECT_EQ(prohibited.result,
              c2::AssignmentResult::operator_action_required);
    EXPECT_EQ(service.assignment(1)->effector_asset_id, 20U);
}

TEST(AssetAssignmentServiceTest, RejectsUnassignDuringAttackAndRecordsCompletion) {
    c2::AssetAssignmentService service(assignment_config());
    ASSERT_EQ(service.assign(
                  assignment_track(1), {assignment_candidate(10)}).result,
              c2::AssignmentResult::assigned);
    ASSERT_TRUE(service.mark_attack_started(1));
    EXPECT_EQ(service.unassign(1), c2::AssignmentResult::operator_action_required);
    EXPECT_EQ(service.complete(1, true), c2::AssignmentResult::completed);
    EXPECT_EQ(service.assignment(1)->state, c2::AssignmentResult::completed);
}

TEST(AssetAssignmentServiceTest, DistinguishesNoCandidateFromTemporaryUnavailability) {
    c2::AssetAssignmentService service(assignment_config());
    auto no_capability = assignment_candidate(10);
    no_capability.asset.capabilities = c2::capability::effector_point;
    EXPECT_EQ(service.assign(assignment_track(1), {no_capability}).result,
              c2::AssignmentResult::no_candidate);

    auto disconnected = assignment_candidate(20);
    disconnected.asset.connection_state = c2::AssetConnectionState::disconnected;
    EXPECT_EQ(service.assign(assignment_track(1), {disconnected}).result,
              c2::AssignmentResult::temporarily_unavailable);
}

TEST(AssetAssignmentServiceTest, DefaultPolicyRequiresPointAndAttackCapabilities) {
    c2::AssetAssignmentService service({});
    auto point_only = assignment_candidate(10);
    point_only.asset.capabilities = c2::capability::effector_point;

    EXPECT_EQ(service.assign(assignment_track(1), {point_only}).result,
              c2::AssignmentResult::no_candidate);
}

TEST(AssetAssignmentServiceTest, ExposesDeterministicAssignmentSnapshot) {
    c2::AssetAssignmentService service(assignment_config());
    ASSERT_EQ(service.assign(
                  assignment_track(20), {assignment_candidate(20, 90, true)}).result,
              c2::AssignmentResult::assigned);
    ASSERT_EQ(service.assign(
                  assignment_track(10), {assignment_candidate(10, 90, true)}).result,
              c2::AssignmentResult::assigned);

    const auto assignments = service.assignments();
    ASSERT_EQ(assignments.size(), 2U);
    EXPECT_EQ(assignments[0].track_id, 10U);
    EXPECT_EQ(assignments[1].track_id, 20U);
}
