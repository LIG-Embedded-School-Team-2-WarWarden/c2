#include "pch.h"
#include "c2/command_identity.hpp"
#include "c2/inbound_rejection_log.hpp"
#include "c2/legacy_command_tracker.hpp"
#include "c2/track_update_publisher.hpp"
#include <algorithm>
#include <limits>
#include <thread>

TEST(CommandIdentityTest, ConcurrentCommandKindsShareUniqueIdsAndSequences) {
    c2::CommandIdentity identity(1, 1);
    std::vector<std::uint32_t> ids(200);
    auto assign = [&](std::size_t begin) {
        for (std::size_t i = begin; i < begin + 100; ++i) {
            c2::AttackCommand command;
            identity.assign(command);
            ids[i] = command.command_id;
            EXPECT_EQ(command.command_id, command.header.sequence);
        }
    };
    std::thread first(assign, 0), second(assign, 100);
    first.join(); second.join();
    std::sort(ids.begin(), ids.end());
    for (std::size_t i = 0; i < ids.size(); ++i) EXPECT_EQ(ids[i], i + 1);
}

TEST(CommandIdentityTest, WrapSkipsZeroAcrossCommandTypes) {
    c2::CommandIdentity identity(std::numeric_limits<std::uint32_t>::max(),
                                 std::numeric_limits<std::uint32_t>::max());
    c2::EffectorTurretCommand point;
    c2::AttackCommand attack;
    identity.assign(point); identity.assign(attack);
    EXPECT_EQ(point.command_id, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(attack.command_id, 1);
    EXPECT_EQ(attack.header.sequence, 1);
}

TEST(InboundRejectionLogTest, BoundsHistoryAndReturnsIndependentSnapshots) {
    c2::InboundRejectionLog log(2);
    auto record = [&] { log.record(c2::InboundRejectionCategory::invalid_packet,
        c2::MessageKind::unspecified, nullptr, c2::AssetRegistryResult::invalid, 100); };
    record(); record();
    auto snapshot = log.snapshot();
    record();
    ASSERT_EQ(snapshot.size(), 2);
    EXPECT_EQ(snapshot.front().event_id, 1);
    const auto current = log.snapshot();
    ASSERT_EQ(current.size(), 2);
    EXPECT_EQ(current.front().event_id, 2);
    EXPECT_EQ(current.back().event_id, 3);
    EXPECT_THROW(c2::InboundRejectionLog(0), std::invalid_argument);
}

TEST(LegacyCommandTrackerTest, SeparatesRoleIdsAndHandlesClockRegressionAndRetryLimit) {
    c2::LegacyCommandTracker tracker(50, 2);
    const auto observer = c2::ComponentId::observation_asset;
    const auto effector = c2::ComponentId::effector_asset;
    tracker.track(observer, 1, {}, {"127.0.0.1", 5101}, 100);
    tracker.track(effector, 1, {}, {"127.0.0.1", 6001}, 100);
    EXPECT_TRUE(tracker.poll(90).transmissions.empty());
    EXPECT_TRUE(tracker.poll(149).transmissions.empty());
    EXPECT_EQ(tracker.poll(150).transmissions.size(), 2);
    c2::CommandAck ack;
    ack.header.source_id = observer; ack.command_id = 1;
    tracker.acknowledge(ack);
    EXPECT_EQ(tracker.pending_count(), 1);
    const auto result = tracker.poll(200);
    EXPECT_EQ(result.exhausted, 1);
    EXPECT_TRUE(result.transmissions.empty());
    EXPECT_EQ(tracker.pending_count(), 0);
}

TEST(TrackUpdatePublisherTest, ConcurrentPublicationsUseUniqueSequencesAndPreserveIdentity) {
    std::mutex received_mutex;
    std::vector<std::uint32_t> sequences;
    c2::TrackUpdatePublisher publisher([&](auto bytes, const auto&) {
        const auto decoded = c2::protobuf::decode(bytes);
        const auto& update = std::get<c2::TargetTrackUpdate>(std::get<c2::Envelope>(decoded).payload);
        EXPECT_EQ(update.header.asset_id, 201);
        EXPECT_EQ(update.header.session_id, 7);
        EXPECT_EQ(update.track_id, 42);
        std::lock_guard lock(received_mutex);
        sequences.push_back(update.header.sequence);
    });
    c2::AssetSnapshot asset;
    asset.asset_id = 201; asset.session_id = 7; asset.role = c2::AssetRole::effector;
    asset.connection_state = c2::AssetConnectionState::connected;
    c2::TrackSnapshot track;
    track.track_id = 42; track.observation_asset_id = 101; track.observation_session_id = 5;
    track.expires_at_us = 1000;
    track.measurement.x_m = 10; track.measurement.confidence = 0.9F;
    track.measurement.velocity_valid = true; track.measurement.measurement_time_us = 100;
    auto publish = [&] { for (int i = 0; i < 100; ++i) publisher.publish(track, asset, 200); };
    std::thread first(publish), second(publish);
    first.join(); second.join();
    std::sort(sequences.begin(), sequences.end());
    ASSERT_EQ(sequences.size(), 200);
    for (std::size_t i = 0; i < sequences.size(); ++i) EXPECT_EQ(sequences[i], i + 1);
    track.measurement.velocity_valid = false;
    publisher.publish(track, asset, 200);
    EXPECT_EQ(sequences.size(), 200);
}
