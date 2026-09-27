#include "pch.h"

#include "c2/asset_registry.hpp"

#include <cstdint>

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
}
