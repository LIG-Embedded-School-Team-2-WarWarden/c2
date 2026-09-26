#include "pch.h"

#include "c2/connection_monitor.hpp"

#include <cstdint>
#include <stdexcept>

namespace {
c2::Heartbeat heartbeat(
    const c2::ComponentId source, const std::uint32_t sequence,
    const std::uint64_t timestamp) {
    return {{c2::protocol_version, sequence, timestamp, source,
             c2::ComponentId::command_and_control},
            c2::AssetOperatingState::operating, 1'000, timestamp};
}

TEST(ConnectionMonitorTest, TracksAssetsIndependentlyUsingReceiveTime) {
    c2::ConnectionMonitor monitor({3'000});
    EXPECT_EQ(monitor.state(c2::ComponentId::observation_asset, 100),
              c2::ConnectionState::never_seen);
    EXPECT_EQ(monitor.observe(heartbeat(c2::ComponentId::observation_asset, 1, 10), 100),
              c2::HeartbeatUpdateResult::stored);
    EXPECT_EQ(monitor.state(c2::ComponentId::observation_asset, 3'100),
              c2::ConnectionState::connected);
    EXPECT_EQ(monitor.state(c2::ComponentId::observation_asset, 3'101),
              c2::ConnectionState::disconnected);
    EXPECT_EQ(monitor.state(c2::ComponentId::effector_asset, 3'101),
              c2::ConnectionState::never_seen);
}

TEST(ConnectionMonitorTest, RequiresPoseSynchronizationOnInitialConnectAndReconnect) {
    c2::ConnectionMonitor monitor({100});
    ASSERT_EQ(monitor.observe(heartbeat(c2::ComponentId::effector_asset, 1, 10), 100),
              c2::HeartbeatUpdateResult::stored);
    EXPECT_TRUE(monitor.pose_resynchronization_required(c2::ComponentId::effector_asset));
    EXPECT_TRUE(monitor.mark_pose_synchronized(c2::ComponentId::effector_asset, 100));
    EXPECT_FALSE(monitor.pose_resynchronization_required(c2::ComponentId::effector_asset));

    EXPECT_EQ(monitor.state(c2::ComponentId::effector_asset, 201),
              c2::ConnectionState::disconnected);
    EXPECT_EQ(monitor.observe(heartbeat(c2::ComponentId::effector_asset, 1, 1), 202),
              c2::HeartbeatUpdateResult::stored);
    EXPECT_EQ(monitor.state(c2::ComponentId::effector_asset, 202),
              c2::ConnectionState::connected);
    EXPECT_TRUE(monitor.pose_resynchronization_required(c2::ComponentId::effector_asset));
}

TEST(ConnectionMonitorTest, RejectsInvalidDuplicateAndOutOfOrderHeartbeatWhileConnected) {
    c2::ConnectionMonitor monitor({1'000});
    auto invalid = heartbeat(c2::ComponentId::observation_asset, 1, 10);
    invalid.state = c2::AssetOperatingState::unspecified;
    EXPECT_EQ(monitor.observe(invalid, 100), c2::HeartbeatUpdateResult::invalid);
    EXPECT_EQ(monitor.observe(heartbeat(c2::ComponentId::observation_asset, 1, 10), 100),
              c2::HeartbeatUpdateResult::stored);
    EXPECT_EQ(monitor.observe(heartbeat(c2::ComponentId::observation_asset, 1, 10), 101),
              c2::HeartbeatUpdateResult::duplicate);
    EXPECT_EQ(monitor.observe(heartbeat(c2::ComponentId::observation_asset, 2, 9), 102),
              c2::HeartbeatUpdateResult::out_of_order);
    EXPECT_EQ(monitor.observe(heartbeat(c2::ComponentId::observation_asset, 2, 11), 99),
              c2::HeartbeatUpdateResult::out_of_order);
}

TEST(ConnectionMonitorTest, RejectsUnsupportedSourceAndInvalidReceiveTime) {
    c2::ConnectionMonitor monitor({1'000});
    EXPECT_EQ(monitor.observe(heartbeat(c2::ComponentId::command_and_control, 1, 10), 100),
              c2::HeartbeatUpdateResult::invalid);
    EXPECT_EQ(monitor.observe(heartbeat(c2::ComponentId::effector_asset, 1, 10), 0),
              c2::HeartbeatUpdateResult::invalid);
    EXPECT_EQ(monitor.state(c2::ComponentId::command_and_control, 100),
              c2::ConnectionState::unsupported_source);
    EXPECT_FALSE(monitor.mark_pose_synchronized(c2::ComponentId::effector_asset, 100));
}

TEST(ConnectionMonitorTest, RejectsInvalidConfiguration) {
    EXPECT_THROW((void)c2::ConnectionMonitor({0}), std::invalid_argument);
}
}  // namespace
