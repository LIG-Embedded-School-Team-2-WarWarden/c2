#include "pch.h"

#include "c2/state_store.hpp"

#include <cstdint>
#include <stdexcept>

namespace {
c2::MessageHeader header(
    const c2::ComponentId source, const std::uint32_t sequence,
    const std::uint64_t timestamp_us) {
    return {c2::protocol_version, sequence, timestamp_us, source,
            c2::ComponentId::command_and_control};
}

c2::AssetPose pose(
    const c2::ComponentId source, const std::uint32_t sequence,
    const std::uint64_t timestamp_us, const float x_m) {
    return {header(source, sequence, timestamp_us), c2::CoordinateFrame::project_frame,
            x_m, 2.0F, 3.0F, 10.0F};
}

c2::TargetCoordinate target(
    const std::uint32_t detection_id, const std::uint32_t sequence,
    const std::uint64_t measurement_time_us, const float x_m = 1.0F) {
    return {header(c2::ComponentId::observation_asset, sequence,
                   measurement_time_us + 10),
            detection_id, measurement_time_us, c2::CoordinateFrame::project_frame,
            x_m, 2.0F, 3.0F, 0.9F};
}

TEST(StateStoreTest, StoresAssetPosesIndependentlyBySource) {
    c2::StateStore store({1'000, 4});

    EXPECT_EQ(store.update(pose(c2::ComponentId::observation_asset, 1, 100, 10.0F)),
              c2::StoreUpdateResult::stored);
    EXPECT_EQ(store.update(pose(c2::ComponentId::effector_asset, 1, 100, 20.0F)),
              c2::StoreUpdateResult::stored);

    ASSERT_TRUE(store.asset_pose(c2::ComponentId::observation_asset).has_value());
    ASSERT_TRUE(store.asset_pose(c2::ComponentId::effector_asset).has_value());
    EXPECT_FLOAT_EQ(store.asset_pose(c2::ComponentId::observation_asset)->x_m, 10.0F);
    EXPECT_FLOAT_EQ(store.asset_pose(c2::ComponentId::effector_asset)->x_m, 20.0F);
    EXPECT_FALSE(store.asset_pose(c2::ComponentId::command_and_control).has_value());
}

TEST(StateStoreTest, RejectsInvalidDuplicateAndOutOfOrderAssetPoseWithoutMutation) {
    c2::StateStore store({1'000, 4});
    const auto original = pose(c2::ComponentId::effector_asset, 7, 200, 10.0F);
    ASSERT_EQ(store.update(original), c2::StoreUpdateResult::stored);

    auto invalid = pose(c2::ComponentId::effector_asset, 8, 300, 99.0F);
    invalid.coordinate_frame = c2::CoordinateFrame::unspecified;
    EXPECT_EQ(store.update(invalid), c2::StoreUpdateResult::invalid);
    EXPECT_EQ(store.update(original), c2::StoreUpdateResult::duplicate);
    EXPECT_EQ(store.update(pose(c2::ComponentId::effector_asset, 6, 199, 88.0F)),
              c2::StoreUpdateResult::out_of_order);
    EXPECT_EQ(store.update(pose(c2::ComponentId::effector_asset, 8, 200, 77.0F)),
              c2::StoreUpdateResult::out_of_order);
    EXPECT_FLOAT_EQ(store.asset_pose(c2::ComponentId::effector_asset)->x_m, 10.0F);
}

TEST(StateStoreTest, AcceptsNewerAssetPoseEvenWhenSequenceRestarts) {
    c2::StateStore store({1'000, 4});
    ASSERT_EQ(store.update(pose(c2::ComponentId::effector_asset, 100, 200, 10.0F)),
              c2::StoreUpdateResult::stored);
    EXPECT_EQ(store.update(pose(c2::ComponentId::effector_asset, 1, 300, 20.0F)),
              c2::StoreUpdateResult::stored);
    EXPECT_FLOAT_EQ(store.asset_pose(c2::ComponentId::effector_asset)->x_m, 20.0F);
}

TEST(StateStoreTest, StoresLatestTargetPerDetectionAndRejectsReplay) {
    c2::StateStore store({1'000, 4});
    const auto original = target(10, 3, 100, 10.0F);
    ASSERT_EQ(store.update(original, 500), c2::StoreUpdateResult::stored);
    EXPECT_EQ(store.update(original, 500), c2::StoreUpdateResult::duplicate);
    EXPECT_EQ(store.update(target(10, 2, 99, 20.0F), 500),
              c2::StoreUpdateResult::out_of_order);
    EXPECT_EQ(store.update(target(10, 4, 100, 30.0F), 500),
              c2::StoreUpdateResult::out_of_order);
    EXPECT_EQ(store.update(target(10, 1, 101, 40.0F), 500),
              c2::StoreUpdateResult::stored);

    ASSERT_TRUE(store.target(10, 500).has_value());
    EXPECT_FLOAT_EQ(store.target(10, 500)->x_m, 40.0F);
}

TEST(StateStoreTest, RejectsInvalidAndExpiredTargetAtPreciseBoundary) {
    c2::StateStore store({1'000, 4});
    auto invalid = target(1, 1, 100);
    invalid.detection_id = 0;
    EXPECT_EQ(store.update(invalid, 100), c2::StoreUpdateResult::invalid);

    EXPECT_EQ(store.update(target(1, 1, 100), 1'100),
              c2::StoreUpdateResult::stored);
    EXPECT_TRUE(store.target(1, 1'100).has_value());
    EXPECT_FALSE(store.target(1, 1'101).has_value());
    EXPECT_EQ(store.update(target(2, 2, 100), 1'101),
              c2::StoreUpdateResult::expired);
}

TEST(StateStoreTest, HandlesValidityDeadlineOverflowSafely) {
    constexpr auto near_max = UINT64_MAX - 5;
    c2::StateStore store({10, 4});
    auto candidate = target(1, 1, near_max);
    candidate.header.timestamp_us = near_max;

    EXPECT_EQ(store.update(candidate, UINT64_MAX), c2::StoreUpdateResult::stored);
    EXPECT_TRUE(store.target(1, UINT64_MAX).has_value());
}

TEST(StateStoreTest, EnforcesCapacityAndPrunesExpiredTargets) {
    c2::StateStore store({100, 2});
    ASSERT_EQ(store.update(target(1, 1, 100), 100), c2::StoreUpdateResult::stored);
    ASSERT_EQ(store.update(target(2, 2, 150), 150), c2::StoreUpdateResult::stored);
    EXPECT_EQ(store.update(target(3, 3, 160), 160),
              c2::StoreUpdateResult::capacity_exceeded);
    EXPECT_EQ(store.target_count(160), 2U);

    EXPECT_EQ(store.prune_expired(201), 1U);
    EXPECT_EQ(store.update(target(3, 3, 201), 201), c2::StoreUpdateResult::stored);
    EXPECT_EQ(store.target_count(201), 2U);
    EXPECT_FALSE(store.target(1, 201).has_value());
    EXPECT_TRUE(store.target(2, 201).has_value());
    EXPECT_TRUE(store.target(3, 201).has_value());
}

TEST(StateStoreTest, RejectsInvalidConfiguration) {
    EXPECT_THROW((void)c2::StateStore({0, 1}), std::invalid_argument);
    EXPECT_THROW((void)c2::StateStore({1, 0}), std::invalid_argument);
}

TEST(StateStoreTest, ReturnsCurrentTargetsInStableIdentityOrder) {
    c2::StateStore store({100, 4});
    auto second = target(2, 10, 10);
    auto first = target(1, 11, 11);
    ASSERT_EQ(store.update(second, 10), c2::StoreUpdateResult::stored);
    ASSERT_EQ(store.update(first, 11), c2::StoreUpdateResult::stored);

    const auto targets = store.targets(12);

    ASSERT_EQ(targets.size(), 2U);
    EXPECT_EQ(targets[0].detection_id, 1U);
    EXPECT_EQ(targets[1].detection_id, 2U);
    EXPECT_EQ(store.targets(111).size(), 1U);
    EXPECT_TRUE(store.targets(112).empty());
}
}  // namespace
