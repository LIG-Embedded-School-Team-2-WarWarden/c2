#include "pch.h"

#include "c2/protobuf_codec.hpp"
#include "c2/server_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace {
c2::ServerRuntimeConfig config() {
    return {{1'000, 8}, {100},
            {{-90, 90, -30, 45}, 50, 1, 1},
            {{-180, 180, -90, 90}, 50, 1, 1},
            {50, 1, 1},
            {"127.0.0.1", 5101}, {"127.0.0.1", 6001}};
}

c2::MessageHeader header(c2::ComponentId source, std::uint32_t sequence, std::uint64_t time) {
    return {c2::protocol_version, sequence, time, source,
            c2::ComponentId::command_and_control};
}

std::vector<std::byte> bytes(c2::MessagePayload payload) {
    return c2::protobuf::encode(c2::Envelope{std::move(payload)});
}

c2::Heartbeat heartbeat(c2::ComponentId source, std::uint64_t time = 10) {
    return {header(source, 1, time), c2::AssetOperatingState::operating, 100, time};
}

c2::AssetPose pose(c2::ComponentId source, std::uint64_t time = 11) {
    return {header(source, 2, time), c2::CoordinateFrame::project_frame, 0, 0, 0, 0};
}

c2::AssetRegistration registration(
    std::uint64_t asset_id, std::uint64_t session_id, c2::AssetRole role,
    std::uint16_t command_port) {
    const auto source = role == c2::AssetRole::observation
        ? c2::ComponentId::observation_asset
        : c2::ComponentId::effector_asset;
    const auto capabilities = role == c2::AssetRole::observation
        ? c2::capability::observation_scan
        : c2::capability::effector_point | c2::capability::effector_attack;
    return {{c2::protocol_version, 1, 10, source,
             c2::ComponentId::command_and_control, asset_id, session_id},
            role, command_port, capabilities, "runtime-test/2.0", {},
            {-180, 180, -90, 90}, false, 5'000};
}

c2::Heartbeat heartbeat(const c2::AssetRegistration& asset, std::uint32_t sequence) {
    return {{c2::protocol_version, sequence, 10 + sequence,
             asset.header.source_id, c2::ComponentId::command_and_control,
             asset.header.asset_id, asset.header.session_id},
            c2::AssetOperatingState::operating, sequence, 10 + sequence};
}

TEST(ServerRuntimeTest, IngestsHeartbeatPoseTargetAndEffectorStatus) {
    std::vector<c2::Endpoint> destinations;
    c2::ServerRuntime server(config(), [&](auto, const auto& endpoint) { destinations.push_back(endpoint); });

    EXPECT_EQ(server.ingest(bytes(heartbeat(c2::ComponentId::effector_asset)), 100),
              c2::InboundResult::accepted);
    EXPECT_TRUE(server.pose_resynchronization_required(c2::ComponentId::effector_asset));
    EXPECT_EQ(server.ingest(bytes(pose(c2::ComponentId::effector_asset)), 101),
              c2::InboundResult::accepted);
    EXPECT_FALSE(server.pose_resynchronization_required(c2::ComponentId::effector_asset));

    c2::TargetCoordinate target{header(c2::ComponentId::observation_asset, 1, 12),
                                7, 12, c2::CoordinateFrame::project_frame,
                                10, 0, 0, 0.9F};
    EXPECT_EQ(server.ingest(bytes(target), 102), c2::InboundResult::accepted);
    c2::EffectorStatus status{header(c2::ComponentId::effector_asset, 3, 13),
                              c2::EffectorState::ready, 0, 0, 0, 0,
                              true, false, false, 0, 13};
    EXPECT_EQ(server.ingest(bytes(status), 103), c2::InboundResult::accepted);
    EXPECT_TRUE(destinations.empty());
}

TEST(ServerRuntimeTest, RejectsMalformedAndOutboundOnlyInboundMessages) {
    c2::ServerRuntime server(config(), [](auto, auto) {});
    const std::vector malformed{std::byte{0xFF}};
    EXPECT_EQ(server.ingest(malformed, 100), c2::InboundResult::invalid_packet);
    c2::ObservationTurretCommand command{
        {c2::protocol_version, 1, 10, c2::ComponentId::command_and_control,
         c2::ComponentId::observation_asset},
        1, c2::ObservationTurretCommandType::home, 0, 0, 20};
    EXPECT_EQ(server.ingest(bytes(command), 100), c2::InboundResult::unsupported_message);
}

TEST(ServerRuntimeTest, DispatchesObservationAndEffectorCommandsAfterSynchronization) {
    struct Sent { std::vector<std::byte> data; c2::Endpoint endpoint; };
    std::vector<Sent> sent;
    c2::ServerRuntime server(config(), [&](auto data, const auto& endpoint) {
        sent.push_back({{data.begin(), data.end()}, endpoint});
    });
    EXPECT_EQ(std::get<c2::DispatchError>(server.command_observation(
                  c2::ObservationTurretCommandType::scan, 10, 5, 100)),
              c2::DispatchError::connection_unavailable);

    ASSERT_EQ(server.ingest(bytes(heartbeat(c2::ComponentId::observation_asset)), 100),
              c2::InboundResult::accepted);
    EXPECT_EQ(std::get<c2::DispatchError>(server.command_observation(
                  c2::ObservationTurretCommandType::scan, 10, 5, 100)),
              c2::DispatchError::pose_resynchronization_required);
    ASSERT_EQ(server.ingest(bytes(pose(c2::ComponentId::observation_asset)), 101),
              c2::InboundResult::accepted);
    const auto observation = server.command_observation(
        c2::ObservationTurretCommandType::scan, 10, 5, 102);
    ASSERT_TRUE(std::holds_alternative<c2::ObservationTurretCommand>(observation));
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent.back().endpoint.port, 5101);

    ASSERT_EQ(server.ingest(bytes(heartbeat(c2::ComponentId::effector_asset)), 100),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(pose(c2::ComponentId::effector_asset)), 101),
              c2::InboundResult::accepted);
    c2::TargetCoordinate target{header(c2::ComponentId::observation_asset, 3, 12),
                                7, 12, c2::CoordinateFrame::project_frame,
                                10, 0, 0, 1};
    ASSERT_EQ(server.ingest(bytes(target), 102), c2::InboundResult::accepted);
    const auto point = server.point_effector(7, 103);
    ASSERT_TRUE(std::holds_alternative<c2::EffectorTurretCommand>(point));
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(sent.back().endpoint.port, 6001);
}

TEST(ServerRuntimeTest, EnforcesConnectionForAttackButAlwaysDispatchesEmergencyStop) {
    std::vector<std::vector<std::byte>> sent;
    c2::ServerRuntime server(config(), [&](auto data, auto) { sent.emplace_back(data.begin(), data.end()); });
    EXPECT_EQ(std::get<c2::DispatchError>(server.attack(c2::AttackAction::arm, 7, 0, 100)),
              c2::DispatchError::connection_unavailable);
    const auto emergency = server.attack(c2::AttackAction::emergency_stop, 0, 0, 100);
    ASSERT_TRUE(std::holds_alternative<c2::AttackCommand>(emergency));
    ASSERT_EQ(sent.size(), 3U);
    std::uint32_t command_id{};
    for (const auto& packet : sent) {
        const auto decoded = c2::protobuf::decode(packet);
        ASSERT_TRUE(std::holds_alternative<c2::Envelope>(decoded));
        const auto& command = std::get<c2::AttackCommand>(
            std::get<c2::Envelope>(decoded).payload);
        EXPECT_EQ(command.action, c2::AttackAction::emergency_stop);
        if (command_id == 0) command_id = command.command_id;
        EXPECT_EQ(command.command_id, command_id);
    }
}

TEST(ServerRuntimeTest, UsesOneIdentitySequenceForAllEffectorCommands) {
    std::vector<std::vector<std::byte>> sent;
    c2::ServerRuntime server(config(), [&](auto data, auto) {
        sent.emplace_back(data.begin(), data.end());
    });

    ASSERT_EQ(server.ingest(bytes(heartbeat(c2::ComponentId::effector_asset)), 100),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(pose(c2::ComponentId::effector_asset)), 101),
              c2::InboundResult::accepted);
    c2::TargetCoordinate target{header(c2::ComponentId::observation_asset, 3, 12),
                                7, 12, c2::CoordinateFrame::project_frame,
                                10, 0, 0, 1};
    ASSERT_EQ(server.ingest(bytes(target), 102), c2::InboundResult::accepted);
    c2::EffectorStatus status{header(c2::ComponentId::effector_asset, 3, 13),
                              c2::EffectorState::ready, 0, 0, 0, 0,
                              true, false, false, 0, 13};
    ASSERT_EQ(server.ingest(bytes(status), 103), c2::InboundResult::accepted);

    const auto point = server.point_effector(7, 104);
    const auto arm = server.attack(c2::AttackAction::arm, 7, 0, 105);
    ASSERT_TRUE(std::holds_alternative<c2::EffectorTurretCommand>(point));
    ASSERT_TRUE(std::holds_alternative<c2::AttackCommand>(arm));
    const auto& point_command = std::get<c2::EffectorTurretCommand>(point);
    const auto& arm_command = std::get<c2::AttackCommand>(arm);
    EXPECT_EQ(point_command.command_id, 1U);
    EXPECT_EQ(arm_command.command_id, 2U);
    EXPECT_EQ(point_command.header.sequence, 1U);
    EXPECT_EQ(arm_command.header.sequence, 2U);
}

TEST(ServerRuntimeTest, SendsHeartbeatToBothAssets) {
    struct Sent { std::vector<std::byte> data; c2::Endpoint endpoint; };
    std::vector<Sent> sent;
    c2::ServerRuntime server(config(), [&](auto data, const auto& endpoint) {
        sent.push_back({{data.begin(), data.end()}, endpoint});
    });

    server.send_heartbeats(100, 25);

    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(sent[0].endpoint.port, 5101);
    EXPECT_EQ(sent[1].endpoint.port, 6001);
    for (std::size_t index = 0; index < sent.size(); ++index) {
        const auto decoded = c2::protobuf::decode(sent[index].data);
        ASSERT_TRUE(std::holds_alternative<c2::Envelope>(decoded));
        const auto& heartbeat = std::get<c2::Heartbeat>(
            std::get<c2::Envelope>(decoded).payload);
        EXPECT_EQ(heartbeat.header.source_id,
                  c2::ComponentId::command_and_control);
        EXPECT_EQ(heartbeat.header.destination_id,
                  index == 0 ? c2::ComponentId::observation_asset
                             : c2::ComponentId::effector_asset);
        EXPECT_EQ(heartbeat.uptime_ms, 25U);
    }
}

TEST(ServerRuntimeTest, ExposesConnectionsAndCurrentTargetsForOperatorDisplay) {
    c2::ServerRuntime server(config(), [](auto, auto) {});
    EXPECT_EQ(server.connection_state(c2::ComponentId::observation_asset, 100),
              c2::ConnectionState::never_seen);
    ASSERT_EQ(server.ingest(bytes(heartbeat(c2::ComponentId::observation_asset)), 100),
              c2::InboundResult::accepted);
    EXPECT_EQ(server.connection_state(c2::ComponentId::observation_asset, 100),
              c2::ConnectionState::connected);
    c2::TargetCoordinate target{header(c2::ComponentId::observation_asset, 3, 12),
                                7, 12, c2::CoordinateFrame::project_frame,
                                10, 20, 30, 0.8F};
    ASSERT_EQ(server.ingest(bytes(target), 102), c2::InboundResult::accepted);
    const auto targets = server.targets(103);
    ASSERT_EQ(targets.size(), 1U);
    EXPECT_EQ(targets.front().detection_id, 7U);
}

TEST(ServerRuntimeTest, RetriesCommandsUntilAcknowledged) {
    auto runtime_config = config();
    runtime_config.command_ack_timeout_us = 10;
    runtime_config.command_max_attempts = 3;
    std::vector<std::vector<std::byte>> sent;
    c2::ServerRuntime server(runtime_config, [&](auto data, auto) {
        sent.emplace_back(data.begin(), data.end());
    });
    ASSERT_EQ(server.ingest(bytes(heartbeat(c2::ComponentId::observation_asset)), 100),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(pose(c2::ComponentId::observation_asset)), 101),
              c2::InboundResult::accepted);
    ASSERT_TRUE(std::holds_alternative<c2::ObservationTurretCommand>(
        server.command_observation(c2::ObservationTurretCommandType::scan, 10, 5, 102)));
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(server.pending_command_count(), 1U);
    EXPECT_EQ(server.retry_unacknowledged(111).resent, 0U);
    EXPECT_EQ(server.retry_unacknowledged(112).resent, 1U);
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(sent[0], sent[1]);

    c2::CommandAck acknowledgement{
        header(c2::ComponentId::observation_asset, 3, 113),
        1, c2::CommandResult::completed, 0, 113};
    EXPECT_EQ(server.ingest(bytes(acknowledgement), 113), c2::InboundResult::accepted);
    EXPECT_EQ(server.pending_command_count(), 0U);
    EXPECT_EQ(server.retry_unacknowledged(200).resent, 0U);
}

TEST(ServerRuntimeTest, StopsRetryingAfterConfiguredAttemptLimit) {
    auto runtime_config = config();
    runtime_config.command_ack_timeout_us = 10;
    runtime_config.command_max_attempts = 3;
    std::vector<std::vector<std::byte>> sent;
    c2::ServerRuntime server(runtime_config, [&](auto data, auto) {
        sent.emplace_back(data.begin(), data.end());
    });
    ASSERT_EQ(server.ingest(bytes(heartbeat(c2::ComponentId::observation_asset)), 100),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(pose(c2::ComponentId::observation_asset)), 101),
              c2::InboundResult::accepted);
    ASSERT_TRUE(std::holds_alternative<c2::ObservationTurretCommand>(
        server.command_observation(c2::ObservationTurretCommandType::home, 0, 0, 102)));

    EXPECT_EQ(server.retry_unacknowledged(112).resent, 1U);
    EXPECT_EQ(server.retry_unacknowledged(122).resent, 1U);
    const auto exhausted = server.retry_unacknowledged(132);
    EXPECT_EQ(exhausted.resent, 0U);
    EXPECT_EQ(exhausted.exhausted, 1U);
    EXPECT_EQ(server.pending_command_count(), 0U);
    EXPECT_EQ(sent.size(), 3U);
}

TEST(ServerRuntimeRegistrationTest, RegistersAssetsFromActualSourceAndAdvertisedCommandPort) {
    auto runtime_config = config();
    runtime_config.registry = {8, 1'000, 10'000};
    c2::ServerRuntime server(runtime_config, [](auto, auto) {});
    const c2::Endpoint first_source{"10.10.0.7", 40'001};
    const c2::Endpoint second_source{"10.10.0.7", 40'002};

    EXPECT_EQ(server.ingest(bytes(registration(
                  101, 1, c2::AssetRole::observation, 51'101)), first_source, 100),
              c2::InboundResult::accepted);
    EXPECT_EQ(server.ingest(bytes(registration(
                  202, 1, c2::AssetRole::effector, 60'202)), second_source, 101),
              c2::InboundResult::accepted);

    const auto assets = server.assets(101);
    ASSERT_EQ(assets.size(), 2U);
    EXPECT_EQ(assets[0].asset_id, 101U);
    EXPECT_EQ(assets[0].source_endpoint.port, 40'001);
    EXPECT_EQ(assets[0].command_endpoint.address, "10.10.0.7");
    EXPECT_EQ(assets[0].command_endpoint.port, 51'101);
    EXPECT_EQ(assets[1].asset_id, 202U);
    EXPECT_EQ(assets[1].command_endpoint.port, 60'202);
}

TEST(ServerRuntimeRegistrationTest, RejectsUnregisteredAndEndpointMismatchPackets) {
    c2::ServerRuntime server(config(), [](auto, auto) {});
    const auto asset = registration(101, 10, c2::AssetRole::observation, 51'101);
    const c2::Endpoint source{"10.10.0.7", 40'001};

    EXPECT_EQ(server.ingest(bytes(heartbeat(asset, 2)), source, 100),
              c2::InboundResult::rejected);
    ASSERT_EQ(server.ingest(bytes(asset), source, 101),
              c2::InboundResult::accepted);
    EXPECT_EQ(server.ingest(bytes(heartbeat(asset, 2)),
                            {"10.10.0.7", 40'099}, 102),
              c2::InboundResult::rejected);
    EXPECT_EQ(server.ingest(bytes(heartbeat(asset, 2)), source, 103),
              c2::InboundResult::accepted);
}

TEST(ServerRuntimeRegistrationTest, ReplacesSessionAndRejectsOldSessionTraffic) {
    c2::ServerRuntime server(config(), [](auto, auto) {});
    const c2::Endpoint old_source{"10.10.0.7", 40'001};
    const c2::Endpoint new_source{"10.10.0.8", 40'002};
    const auto old_session = registration(
        101, 10, c2::AssetRole::observation, 51'101);
    const auto new_session = registration(
        101, 11, c2::AssetRole::observation, 51'102);

    ASSERT_EQ(server.ingest(bytes(old_session), old_source, 100),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(heartbeat(old_session, 2)), old_source, 101),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(new_session), new_source, 102),
              c2::InboundResult::accepted);
    EXPECT_EQ(server.ingest(bytes(heartbeat(old_session, 3)), old_source, 103),
              c2::InboundResult::rejected);
    EXPECT_EQ(server.ingest(bytes(heartbeat(new_session, 2)), new_source, 104),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.assets(104).size(), 1U);
    EXPECT_EQ(server.assets(104).front().session_id, 11U);
    EXPECT_FALSE(server.assets(104).front().pose_synchronized);
}
}  // namespace
