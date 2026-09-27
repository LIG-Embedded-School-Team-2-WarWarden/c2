#include "pch.h"

#include "c2/track_store.hpp"

#include <cstdint>
#include <limits>

namespace {
c2::TargetCoordinate target(
    std::uint64_t asset_id, std::uint64_t session_id,
    std::uint32_t detection_id, std::uint64_t measured_at,
    std::uint32_t sequence = 1, float x = 10.0F) {
    return {{c2::protocol_version, sequence, measured_at,
             c2::ComponentId::observation_asset,
             c2::ComponentId::command_and_control, asset_id, session_id},
            detection_id, measured_at, c2::CoordinateFrame::project_frame,
            x, 20, 5, 0.9F};
}
}

TEST(TrackStoreTest, SeparatesEqualDetectionIdsAndReturnsTrackIdOrder) {
    c2::TrackStore store({1'000, 8, 4, 1});

    const auto first = store.update(target(20, 1, 7, 100), 100);
    const auto second = store.update(target(10, 9, 7, 101), 101);

    ASSERT_EQ(first.result, c2::TrackUpdateResult::stored);
    ASSERT_EQ(second.result, c2::TrackUpdateResult::stored);
    EXPECT_EQ(first.track_id, 1U);
    EXPECT_EQ(second.track_id, 2U);
    const auto tracks = store.tracks(101);
    ASSERT_EQ(tracks.size(), 2U);
    EXPECT_EQ(tracks[0].track_id, 1U);
    EXPECT_EQ(tracks[0].observation_asset_id, 20U);
    EXPECT_EQ(tracks[0].observation_session_id, 1U);
    EXPECT_EQ(tracks[0].detection_id, 7U);
    EXPECT_EQ(tracks[1].track_id, 2U);
    EXPECT_EQ(tracks[1].observation_asset_id, 10U);
}

TEST(TrackStoreTest, PreservesTrackIdAndRejectsDuplicateAndOutOfOrderMeasurements) {
    c2::TrackStore store({1'000, 8, 4, 10});
    ASSERT_EQ(store.update(target(20, 1, 7, 100), 100).track_id, 10U);

    const auto duplicate = store.update(target(20, 1, 7, 100), 101);
    EXPECT_EQ(duplicate.result, c2::TrackUpdateResult::duplicate);
    EXPECT_EQ(duplicate.track_id, 10U);
    EXPECT_EQ(store.update(target(20, 1, 7, 99, 2), 102).result,
              c2::TrackUpdateResult::stale);
    const auto updated = store.update(target(20, 1, 7, 110, 3, 42), 110);
    EXPECT_EQ(updated.result, c2::TrackUpdateResult::stored);
    EXPECT_EQ(updated.track_id, 10U);
    ASSERT_TRUE(store.track(10, 110).has_value());
    EXPECT_FLOAT_EQ(store.track(10, 110)->measurement.x_m, 42);
}

TEST(TrackStoreTest, ExpiresTracksAndEnforcesGlobalAndPerObserverBounds) {
    c2::TrackStore store({10, 3, 2, 1});
    EXPECT_EQ(store.update(target(10, 1, 1, 100), 100).result,
              c2::TrackUpdateResult::stored);
    EXPECT_EQ(store.update(target(10, 1, 2, 101), 101).result,
              c2::TrackUpdateResult::stored);
    EXPECT_EQ(store.update(target(10, 1, 3, 102), 102).result,
              c2::TrackUpdateResult::observer_capacity_exceeded);
    EXPECT_EQ(store.update(target(20, 1, 1, 103), 103).result,
              c2::TrackUpdateResult::stored);
    EXPECT_EQ(store.update(target(30, 1, 1, 104), 104).result,
              c2::TrackUpdateResult::capacity_exceeded);

    EXPECT_EQ(store.tracks(109).size(), 3U);
    EXPECT_TRUE(store.tracks(113).empty());
    EXPECT_EQ(store.update(target(30, 1, 1, 114), 114).result,
              c2::TrackUpdateResult::stored);
}

TEST(TrackStoreTest, WrapsTrackIdsWithoutUsingZeroOrCollidingWithActiveTracks) {
    c2::TrackStore store({1'000, 4, 4,
                          std::numeric_limits<std::uint64_t>::max()});
    const auto maximum = store.update(target(1, 1, 1, 100), 100);
    const auto wrapped = store.update(target(2, 1, 1, 101), 101);
    EXPECT_EQ(maximum.track_id, std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(wrapped.track_id, 1U);
}

TEST(TrackStoreTest, RejectsInvalidConfigurationAndInvalidOrExpiredMeasurement) {
    EXPECT_THROW((void)c2::TrackStore({0, 1, 1, 1}), std::invalid_argument);
    EXPECT_THROW((void)c2::TrackStore({1, 0, 1, 1}), std::invalid_argument);
    EXPECT_THROW((void)c2::TrackStore({1, 1, 0, 1}), std::invalid_argument);
    EXPECT_THROW((void)c2::TrackStore({1, 1, 1, 0}), std::invalid_argument);

    c2::TrackStore store({10, 2, 2, 1});
    auto invalid = target(1, 1, 1, 100);
    invalid.header.asset_id = 0;
    EXPECT_EQ(store.update(invalid, 100).result,
              c2::TrackUpdateResult::invalid);
    EXPECT_EQ(store.update(target(1, 1, 1, 100), 111).result,
              c2::TrackUpdateResult::expired);
}
