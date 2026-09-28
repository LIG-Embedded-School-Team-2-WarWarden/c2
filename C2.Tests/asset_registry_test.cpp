#include "pch.h"

#include "c2/asset_registry.hpp"

#include <atomic>
#include <cstdint>
#include <latch>
#include <thread>
#include <vector>

namespace {
c2::AssetRegistration registration(
    const std::uint64_t asset_id,
    const std::uint64_t session_id,
    const c2::AssetRole role,
    const std::uint16_t command_port,
    const std::uint64_t lease_ms = 5) {
    const auto source = role == c2::AssetRole::observation
        ? c2::ComponentId::observation_asset
        : c2::ComponentId::effector_asset;
    const auto capabilities = role == c2::AssetRole::observation
        ? c2::capability::observation_scan
        : c2::capability::effector_point | c2::capability::effector_attack;
    return {{c2::protocol_version, 1, 100, source,
             c2::ComponentId::command_and_control, asset_id, session_id},
            role, command_port, capabilities, "test/2.0", {},
            {-180, 180, -90, 90}, false, lease_ms};
}

c2::Heartbeat heartbeat(const c2::AssetRegistration& value, const std::uint32_t sequence) {
    return {{c2::protocol_version, sequence, 100 + sequence,
             value.header.source_id, c2::ComponentId::command_and_control,
             value.header.asset_id, value.header.session_id},
            c2::AssetOperatingState::standby, 1, 100 + sequence};
}

c2::AssetPose pose(const c2::AssetRegistration& value, const float x) {
    return {{c2::protocol_version, 2, 102, value.header.source_id,
             c2::ComponentId::command_and_control, value.header.asset_id,
             value.header.session_id},
            c2::CoordinateFrame::project_frame, x, 0, 0, 0};
}

c2::EffectorStatus effector_status(
    const c2::AssetRegistration& value, const std::uint32_t sequence,
    const std::uint64_t timestamp, const c2::EffectorState state = c2::EffectorState::ready) {
    return {{c2::protocol_version, sequence, timestamp,
             c2::ComponentId::effector_asset,
             c2::ComponentId::command_and_control,
             value.header.asset_id, value.header.session_id},
            state, 0, 0, 0, 0, false, false, false, 0, timestamp};
}
}  // namespace

TEST(AssetRegistryTest, RegistersMultipleRolesAndReturnsDeterministicLists) {
    c2::AssetRegistry registry({8, 3'000, 10'000});
    const auto observation_two = registration(20, 1, c2::AssetRole::observation, 51'020);
    const auto observation_one = registration(10, 1, c2::AssetRole::observation, 51'010);
    const auto effector = registration(30, 1, c2::AssetRole::effector, 60'030);

    EXPECT_EQ(registry.register_asset(observation_two, {"10.0.0.1", 51'020}, 100),
              c2::AssetRegistryResult::registered);
    EXPECT_EQ(registry.register_asset(effector, {"10.0.0.2", 60'030}, 101),
              c2::AssetRegistryResult::registered);
    EXPECT_EQ(registry.register_asset(observation_one, {"10.0.0.1", 51'010}, 102),
              c2::AssetRegistryResult::registered);

    const auto observations = registry.assets(c2::AssetRole::observation, 102);
    ASSERT_EQ(observations.size(), 2U);
    EXPECT_EQ(observations[0].asset_id, 10U);
    EXPECT_EQ(observations[1].asset_id, 20U);
    EXPECT_EQ(observations[0].command_endpoint.address, "10.0.0.1");
    EXPECT_EQ(observations[0].command_endpoint.port, 51'010);
    EXPECT_EQ(registry.assets(c2::AssetRole::effector, 102).front().asset_id, 30U);
}

TEST(AssetRegistryTest, NewSessionInvalidatesPoseAndRejectsOldSessionPackets) {
    c2::AssetRegistry registry({4, 3'000, 10'000});
    const auto first = registration(30, 11, c2::AssetRole::effector, 60'030);
    const c2::Endpoint endpoint{"10.0.0.2", 60'030};
    ASSERT_EQ(registry.register_asset(first, endpoint, 100),
              c2::AssetRegistryResult::registered);
    ASSERT_EQ(registry.observe_heartbeat(heartbeat(first, 1), endpoint, 101),
              c2::AssetRegistryResult::stored);
    ASSERT_EQ(registry.update_pose(pose(first, 10), endpoint, 102),
              c2::AssetRegistryResult::stored);
    ASSERT_TRUE(registry.asset(30, 102)->pose_synchronized);

    const auto second = registration(30, 12, c2::AssetRole::effector, 60'031);
    ASSERT_EQ(registry.register_asset(second, {"10.0.0.2", 60'031}, 103),
              c2::AssetRegistryResult::session_replaced);
    const auto snapshot = registry.asset(30, 103);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->session_id, 12U);
    EXPECT_FALSE(snapshot->pose_synchronized);
    EXPECT_FALSE(snapshot->pose.has_value());
    EXPECT_EQ(snapshot->connection_state, c2::AssetConnectionState::awaiting_heartbeat);
    EXPECT_EQ(registry.observe_heartbeat(heartbeat(first, 2), endpoint, 104),
              c2::AssetRegistryResult::session_mismatch);
    EXPECT_EQ(registry.update_pose(pose(first, 99), endpoint, 105),
              c2::AssetRegistryResult::session_mismatch);
}

TEST(AssetRegistryTest, RejectsUnregisteredAndEndpointMismatchMessages) {
    c2::AssetRegistry registry({4, 3'000, 10'000});
    const auto asset = registration(10, 1, c2::AssetRole::observation, 51'010);
    EXPECT_EQ(registry.observe_heartbeat(heartbeat(asset, 1), {"10.0.0.1", 51'010}, 100),
              c2::AssetRegistryResult::not_registered);
    ASSERT_EQ(registry.register_asset(asset, {"10.0.0.1", 51'010}, 100),
              c2::AssetRegistryResult::registered);
    EXPECT_EQ(registry.observe_heartbeat(heartbeat(asset, 1), {"10.0.0.9", 51'010}, 101),
              c2::AssetRegistryResult::endpoint_mismatch);
    EXPECT_EQ(registry.observe_heartbeat(heartbeat(asset, 1), {"10.0.0.1", 51'011}, 101),
              c2::AssetRegistryResult::endpoint_mismatch);
}

TEST(AssetRegistryTest, AppliesCapacityLeaseUnregisterAndRetentionPolicies) {
    c2::AssetRegistry registry({2, 100, 500});
    const auto first = registration(1, 1, c2::AssetRole::observation, 51'001, 1);
    const auto second = registration(2, 1, c2::AssetRole::effector, 60'002, 1);
    const auto third = registration(3, 1, c2::AssetRole::effector, 60'003, 1);
    ASSERT_EQ(registry.register_asset(first, {"127.0.0.1", 51'001}, 100),
              c2::AssetRegistryResult::registered);
    ASSERT_EQ(registry.register_asset(second, {"127.0.0.1", 60'002}, 100),
              c2::AssetRegistryResult::registered);
    EXPECT_EQ(registry.register_asset(third, {"127.0.0.1", 60'003}, 100),
              c2::AssetRegistryResult::capacity_exceeded);

    ASSERT_EQ(registry.expire(1'101), 2U);
    EXPECT_EQ(registry.asset(1, 1'101)->connection_state,
              c2::AssetConnectionState::lease_expired);
    EXPECT_TRUE(registry.assets(c2::AssetRole::observation, 1'101).empty());
    EXPECT_EQ(registry.prune(1'600), 0U);
    EXPECT_EQ(registry.prune(1'602), 2U);
    EXPECT_FALSE(registry.asset(1, 1'602).has_value());

    ASSERT_EQ(registry.register_asset(third, {"127.0.0.1", 60'003}, 1'603),
              c2::AssetRegistryResult::registered);
    c2::AssetUnregister unregister{third.header, "normal shutdown"};
    EXPECT_EQ(registry.unregister_asset(unregister, {"127.0.0.1", 60'003}, 1'604),
              c2::AssetRegistryResult::unregistered);
    EXPECT_EQ(registry.asset(3, 1'604)->connection_state,
              c2::AssetConnectionState::unregistered);
}

TEST(AssetRegistryTest, RejectsInvalidConfiguration) {
    EXPECT_THROW((void)c2::AssetRegistry({0, 1, 1}), std::invalid_argument);
    EXPECT_THROW((void)c2::AssetRegistry({1, 0, 1}), std::invalid_argument);
    EXPECT_THROW((void)c2::AssetRegistry({1, 1, 0}), std::invalid_argument);
    EXPECT_THROW((void)c2::AssetRegistry({1, 1, 1, 1, 0}),
                 std::invalid_argument);
}

TEST(AssetRegistryTest, RejectsLeaseBeyondServerPolicy) {
    c2::AssetRegistry registry({2, 1'000, 10'000, 100, 5'000});
    auto asset = registration(
        10, 1, c2::AssetRole::observation, 51'010, 5'001);
    EXPECT_EQ(registry.register_asset(asset, {"10.0.0.1", 40'010}, 100),
              c2::AssetRegistryResult::invalid);
    asset.lease_duration_ms = 5'000;
    EXPECT_EQ(registry.register_asset(asset, {"10.0.0.1", 40'010}, 100),
              c2::AssetRegistryResult::registered);
}

TEST(AssetRegistryTest, TracksHeartbeatTimeoutIndependentlyPerAsset) {
    c2::AssetRegistry registry({4, 100, 10'000});
    const auto first = registration(10, 1, c2::AssetRole::observation, 51'010, 10);
    const auto second = registration(20, 1, c2::AssetRole::observation, 51'020, 10);
    const c2::Endpoint first_endpoint{"10.0.0.1", 51'010};
    const c2::Endpoint second_endpoint{"10.0.0.2", 51'020};
    ASSERT_EQ(registry.register_asset(first, first_endpoint, 100),
              c2::AssetRegistryResult::registered);
    ASSERT_EQ(registry.register_asset(second, second_endpoint, 100),
              c2::AssetRegistryResult::registered);
    ASSERT_EQ(registry.observe_heartbeat(heartbeat(first, 1), first_endpoint, 110),
              c2::AssetRegistryResult::stored);
    ASSERT_EQ(registry.observe_heartbeat(heartbeat(second, 1), second_endpoint, 190),
              c2::AssetRegistryResult::stored);

    EXPECT_EQ(registry.connection_state(10, 211),
              c2::AssetConnectionState::disconnected);
    EXPECT_EQ(registry.connection_state(20, 211),
              c2::AssetConnectionState::connected);
    EXPECT_EQ(registry.connection_state(999, 211), std::nullopt);
}

TEST(AssetRegistryTest, RejectsDuplicateAndOutOfOrderHeartbeatAndPose) {
    c2::AssetRegistry registry({2, 1'000, 10'000});
    const auto asset = registration(10, 1, c2::AssetRole::observation, 51'010, 10);
    const c2::Endpoint endpoint{"10.0.0.1", 51'010};
    ASSERT_EQ(registry.register_asset(asset, endpoint, 100),
              c2::AssetRegistryResult::registered);
    const auto first_heartbeat = heartbeat(asset, 2);
    ASSERT_EQ(registry.observe_heartbeat(first_heartbeat, endpoint, 110),
              c2::AssetRegistryResult::stored);
    EXPECT_EQ(registry.observe_heartbeat(first_heartbeat, endpoint, 111),
              c2::AssetRegistryResult::duplicate);
    auto stale_heartbeat = heartbeat(asset, 3);
    stale_heartbeat.timestamp_us = first_heartbeat.timestamp_us - 1;
    EXPECT_EQ(registry.observe_heartbeat(stale_heartbeat, endpoint, 112),
              c2::AssetRegistryResult::stale);

    const auto first_pose = pose(asset, 1);
    ASSERT_EQ(registry.update_pose(first_pose, endpoint, 120),
              c2::AssetRegistryResult::stored);
    EXPECT_EQ(registry.update_pose(first_pose, endpoint, 121),
              c2::AssetRegistryResult::duplicate);
    auto stale_pose = first_pose;
    stale_pose.header.sequence++;
    stale_pose.header.timestamp_us--;
    EXPECT_EQ(registry.update_pose(stale_pose, endpoint, 122),
              c2::AssetRegistryResult::stale);
}

TEST(AssetRegistryTest, SerializesConcurrentRegistrationsWithoutLosingAssets) {
    constexpr std::size_t count = 8;
    c2::AssetRegistry registry({count, 1'000, 10'000});
    std::latch ready(count);
    std::latch start(1);
    std::atomic_size_t registered{};
    std::vector<std::jthread> workers;
    workers.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        workers.emplace_back([&, index] {
            ready.count_down();
            start.wait();
            const auto id = static_cast<std::uint64_t>(count - index);
            const auto port = static_cast<std::uint16_t>(51'000 + id);
            const auto value = registration(
                id, 1, c2::AssetRole::observation, port, 10);
            if (registry.register_asset(value, {"127.0.0.1", port}, 100) ==
                c2::AssetRegistryResult::registered)
                ++registered;
        });
    }
    ready.wait();
    start.count_down();
    workers.clear();

    EXPECT_EQ(registered.load(), count);
    const auto snapshots = registry.assets(c2::AssetRole::observation, 101);
    ASSERT_EQ(snapshots.size(), count);
    for (std::size_t index = 0; index < count; ++index)
        EXPECT_EQ(snapshots[index].asset_id, index + 1);
}

TEST(AssetRegistryTest, StoresEffectorStatusIndependentlyAndTracksFreshness) {
    c2::AssetRegistry registry({4, 1'000, 10'000, 100});
    const auto first = registration(10, 1, c2::AssetRole::effector, 60'010, 5'000);
    const auto second = registration(20, 1, c2::AssetRole::effector, 60'020, 5'000);
    const c2::Endpoint first_source{"10.0.0.1", 40'010};
    const c2::Endpoint second_source{"10.0.0.2", 40'020};
    ASSERT_EQ(registry.register_asset(first, first_source, 100),
              c2::AssetRegistryResult::registered);
    ASSERT_EQ(registry.register_asset(second, second_source, 100),
              c2::AssetRegistryResult::registered);

    EXPECT_EQ(registry.update_effector_status(
                  effector_status(first, 2, 101), first_source, 101),
              c2::AssetRegistryResult::stored);
    EXPECT_EQ(registry.update_effector_status(
                  effector_status(second, 2, 102, c2::EffectorState::standby),
                  second_source, 102),
              c2::AssetRegistryResult::stored);
    EXPECT_EQ(registry.update_effector_status(
                  effector_status(first, 2, 101), first_source, 103),
              c2::AssetRegistryResult::duplicate);
    EXPECT_EQ(registry.update_effector_status(
                  effector_status(first, 3, 99), first_source, 104),
              c2::AssetRegistryResult::stale);

    const auto assets = registry.assets(c2::AssetRole::effector, 150);
    ASSERT_EQ(assets.size(), 2U);
    ASSERT_TRUE(assets[0].effector_status.has_value());
    ASSERT_TRUE(assets[1].effector_status.has_value());
    EXPECT_EQ(assets[0].effector_status->state, c2::EffectorState::ready);
    EXPECT_EQ(assets[1].effector_status->state, c2::EffectorState::standby);
    EXPECT_TRUE(assets[0].status_current);
    EXPECT_TRUE(assets[1].status_current);
    EXPECT_FALSE(registry.asset(10, 202)->status_current);
    EXPECT_TRUE(registry.asset(20, 202)->status_current);
}

TEST(AssetRegistryTest, SessionReplacementClearsEffectorStatus) {
    c2::AssetRegistry registry({2, 1'000, 10'000, 100});
    const auto old_session = registration(10, 1, c2::AssetRole::effector, 60'010, 5'000);
    const auto new_session = registration(10, 2, c2::AssetRole::effector, 60'011, 5'000);
    const c2::Endpoint old_source{"10.0.0.1", 40'010};
    const c2::Endpoint new_source{"10.0.0.1", 40'011};
    ASSERT_EQ(registry.register_asset(old_session, old_source, 100),
              c2::AssetRegistryResult::registered);
    ASSERT_EQ(registry.update_effector_status(
                  effector_status(old_session, 2, 101), old_source, 101),
              c2::AssetRegistryResult::stored);
    ASSERT_EQ(registry.register_asset(new_session, new_source, 102),
              c2::AssetRegistryResult::session_replaced);
    ASSERT_TRUE(registry.asset(10, 102).has_value());
    EXPECT_FALSE(registry.asset(10, 102)->effector_status.has_value());
    EXPECT_FALSE(registry.asset(10, 102)->status_current);
    EXPECT_EQ(registry.update_effector_status(
                  effector_status(old_session, 3, 103), old_source, 103),
              c2::AssetRegistryResult::session_mismatch);
}

TEST(AssetRegistryTest, KeepsRetiredEffectorsDiscoverableOnlyDuringRetention) {
    c2::AssetRegistry registry({4, 1'000, 100, 100});
    const c2::Endpoint source{"10.0.0.9", 40'009};
    const auto asset = registration(90, 7, c2::AssetRole::effector, 60'090);
    ASSERT_EQ(registry.register_asset(asset, source, 10),
              c2::AssetRegistryResult::registered);
    c2::AssetUnregister unregister{
        {c2::protocol_version, 2, 20, c2::ComponentId::effector_asset,
         c2::ComponentId::command_and_control, 90, 7}, "shutdown"};
    ASSERT_EQ(registry.unregister_asset(unregister, source, 20),
              c2::AssetRegistryResult::unregistered);

    const auto retained = registry.known_assets(c2::AssetRole::effector, 120);
    ASSERT_EQ(retained.size(), 1U);
    EXPECT_EQ(retained.front().asset_id, 90U);
    EXPECT_EQ(retained.front().connection_state,
              c2::AssetConnectionState::unregistered);
    EXPECT_TRUE(registry.known_assets(c2::AssetRole::effector, 121).empty());
}
