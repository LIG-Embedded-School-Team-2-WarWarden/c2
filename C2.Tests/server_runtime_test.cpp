#include "pch.h"

#include "c2/asset_assignment.hpp"
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

c2::AssetPose pose(
    const c2::AssetRegistration& asset, std::uint32_t sequence,
    float x_m, float y_m = 0, float z_m = 0) {
    return {{c2::protocol_version, sequence, 10 + sequence,
             asset.header.source_id, c2::ComponentId::command_and_control,
             asset.header.asset_id, asset.header.session_id},
            c2::CoordinateFrame::project_frame, x_m, y_m, z_m, 0};
}

c2::EffectorStatus effector_status(
    const c2::AssetRegistration& asset, std::uint32_t sequence,
    c2::EffectorState state = c2::EffectorState::ready) {
    return {{c2::protocol_version, sequence, 10 + sequence,
             c2::ComponentId::effector_asset,
             c2::ComponentId::command_and_control,
             asset.header.asset_id, asset.header.session_id},
            state, 0, 0, 0, 0, true, false, false, 0, 10 + sequence};
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

TEST(ServerRuntimeTest, AppliesConfiguredErrorHistoryLimit) {
    auto runtime_config = config();
    runtime_config.maximum_error_history = 2;
    c2::ServerRuntime server(runtime_config, [](auto, auto) {});
    for (std::uint32_t sequence = 1; sequence <= 3; ++sequence) {
        c2::ErrorReport error{
            header(c2::ComponentId::observation_asset, sequence, 10 + sequence),
            sequence, c2::ErrorSeverity::warning, 0, 10 + sequence, "test"};
        ASSERT_EQ(server.ingest(bytes(error), 100 + sequence),
                  c2::InboundResult::accepted);
    }
    const auto errors = server.errors();
    ASSERT_EQ(errors.size(), 2U);
    EXPECT_EQ(errors.front().error_code, 2U);
    EXPECT_EQ(errors.back().error_code, 3U);
}

TEST(ServerRuntimeTest, ExposesAssignmentAndCommandOutcomeSnapshots) {
    auto runtime_config = config();
    runtime_config.commands = {10, 50, 1, 8, 4, 16};
    c2::ServerRuntime server(runtime_config, [](auto, auto) {});
    const c2::Endpoint observer_source{"10.0.0.1", 40'101};
    const c2::Endpoint effector_source{"10.0.0.2", 40'201};
    const auto observer = registration(101, 1, c2::AssetRole::observation, 51'101);
    const auto effector = registration(201, 1, c2::AssetRole::effector, 60'201);
    ASSERT_EQ(server.ingest(bytes(observer), observer_source, 100),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(effector), effector_source, 100),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(heartbeat(observer, 2)), observer_source, 101),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(heartbeat(effector, 2)), effector_source, 101),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(pose(effector, 3, 0)), effector_source, 102),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(effector_status(effector, 4)), effector_source, 103),
              c2::InboundResult::accepted);
    c2::TargetCoordinate target{
        {c2::protocol_version, 3, 104, c2::ComponentId::observation_asset,
         c2::ComponentId::command_and_control, 101, 1},
        7, 104, c2::CoordinateFrame::project_frame, 10, 0, 0, 0.9F};
    ASSERT_EQ(server.ingest(bytes(target), observer_source, 104),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.assign(1, 105).result, c2::AssignmentResult::assigned);
    ASSERT_TRUE(std::holds_alternative<c2::EffectorTurretCommand>(
        server.point_effector(1, 106)));

    const auto assignments = server.assignments();
    ASSERT_EQ(assignments.size(), 1U);
    EXPECT_EQ(assignments.front().track_id, 1U);
    EXPECT_EQ(assignments.front().effector_asset_id, 201U);
    EXPECT_EQ(server.retry_unacknowledged(116).exhausted, 1U);
    const auto outcomes = server.command_outcomes();
    ASSERT_EQ(outcomes.size(), 1U);
    EXPECT_EQ(outcomes.front().key.asset_id, 201U);
    EXPECT_EQ(outcomes.front().state,
              c2::CommandTerminalState::delivery_exhausted);
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
    auto runtime_config = config();
    runtime_config.maximum_inbound_rejections = 1;
    c2::ServerRuntime server(runtime_config, [](auto, auto) {});
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

    const auto rejections = server.inbound_rejections();
    ASSERT_EQ(rejections.size(), 1U);
    EXPECT_EQ(rejections.front().asset_id, 101U);
    EXPECT_EQ(rejections.front().session_id, 10U);
    EXPECT_EQ(rejections.front().message_kind, c2::MessageKind::heartbeat);
    EXPECT_EQ(rejections.front().reason,
              c2::AssetRegistryResult::endpoint_mismatch);
    EXPECT_EQ(rejections.front().occurred_at_us, 102U);
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

TEST(ServerRuntimeRegistrationTest, AssignsGlobalTrackIdsAcrossObservationAssets) {
    auto runtime_config = config();
    runtime_config.tracks = {1'000, 8, 4, 100};
    c2::ServerRuntime server(runtime_config, [](auto, auto) {});
    const c2::Endpoint first_source{"10.10.0.7", 40'001};
    const c2::Endpoint second_source{"10.10.0.8", 40'002};
    const auto first = registration(101, 1, c2::AssetRole::observation, 51'101);
    const auto second = registration(102, 1, c2::AssetRole::observation, 51'102);
    ASSERT_EQ(server.ingest(bytes(first), first_source, 100),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(second), second_source, 100),
              c2::InboundResult::accepted);

    c2::TargetCoordinate first_target{
        {c2::protocol_version, 2, 101, c2::ComponentId::observation_asset,
         c2::ComponentId::command_and_control, 101, 1},
        7, 101, c2::CoordinateFrame::project_frame, 10, 20, 5, 0.9F};
    auto second_target = first_target;
    second_target.header.asset_id = 102;
    ASSERT_EQ(server.ingest(bytes(first_target), first_source, 101),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(second_target), second_source, 101),
              c2::InboundResult::accepted);

    const auto tracks = server.tracks(101);
    ASSERT_EQ(tracks.size(), 2U);
    EXPECT_EQ(tracks[0].track_id, 100U);
    EXPECT_EQ(tracks[0].observation_asset_id, 101U);
    EXPECT_EQ(tracks[1].track_id, 101U);
    EXPECT_EQ(tracks[1].observation_asset_id, 102U);
    EXPECT_EQ(tracks[0].detection_id, tracks[1].detection_id);
}

TEST(ServerRuntimeRegistrationTest, RoutesObservationCommandToRegisteredSessionAndTracksFinalAck) {
    struct Sent { std::vector<std::byte> data; c2::Endpoint endpoint; };
    std::vector<Sent> sent;
    c2::ServerRuntime server(config(), [&](auto data, const auto& endpoint) {
        sent.push_back({{data.begin(), data.end()}, endpoint});
    });
    const c2::Endpoint source{"10.10.0.7", 40'001};
    const auto asset = registration(101, 10, c2::AssetRole::observation, 51'101);
    ASSERT_EQ(server.ingest(bytes(asset), source, 100), c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(heartbeat(asset, 2)), source, 101),
              c2::InboundResult::accepted);
    EXPECT_EQ(std::get<c2::DispatchError>(server.command_observation(
                  101, c2::ObservationTurretCommandType::scan, 10, 5, 102)),
              c2::DispatchError::pose_resynchronization_required);

    c2::AssetPose synchronized_pose{
        {c2::protocol_version, 3, 102, c2::ComponentId::observation_asset,
         c2::ComponentId::command_and_control, 101, 10},
        c2::CoordinateFrame::project_frame, 0, 0, 1, 0};
    ASSERT_EQ(server.ingest(bytes(synchronized_pose), source, 102),
              c2::InboundResult::accepted);
    const auto dispatched = server.command_observation(
        101, c2::ObservationTurretCommandType::scan, 10, 5, 103);
    ASSERT_TRUE(std::holds_alternative<c2::ObservationTurretCommand>(dispatched));
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent.front().endpoint.address, "10.10.0.7");
    EXPECT_EQ(sent.front().endpoint.port, 51'101);
    const auto& command = std::get<c2::ObservationTurretCommand>(dispatched);
    EXPECT_EQ(command.header.asset_id, 101U);
    EXPECT_EQ(command.header.session_id, 10U);
    EXPECT_EQ(server.pending_command_count(), 1U);

    c2::CommandAck progress{
        {c2::protocol_version, 4, 104, c2::ComponentId::observation_asset,
         c2::ComponentId::command_and_control, 101, 10},
        command.command_id, c2::CommandResult::received, 0, 104};
    ASSERT_EQ(server.ingest(bytes(progress), source, 104),
              c2::InboundResult::accepted);
    EXPECT_EQ(server.pending_command_count(), 1U);
    progress.header.sequence = 5;
    progress.header.timestamp_us = 105;
    progress.timestamp_us = 105;
    progress.result = c2::CommandResult::completed;
    ASSERT_EQ(server.ingest(bytes(progress), source, 105),
              c2::InboundResult::accepted);
    EXPECT_EQ(server.pending_command_count(), 0U);
}

TEST(ServerRuntimeRegistrationTest, NewSessionEndsPreviousSessionPendingCommands) {
    c2::ServerRuntime server(config(), [](auto, auto) {});
    const c2::Endpoint source{"10.10.0.7", 40'001};
    const auto old_session = registration(101, 10, c2::AssetRole::observation, 51'101);
    ASSERT_EQ(server.ingest(bytes(old_session), source, 100),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(heartbeat(old_session, 2)), source, 101),
              c2::InboundResult::accepted);
    c2::AssetPose synchronized_pose{
        {c2::protocol_version, 3, 102, c2::ComponentId::observation_asset,
         c2::ComponentId::command_and_control, 101, 10},
        c2::CoordinateFrame::project_frame, 0, 0, 1, 0};
    ASSERT_EQ(server.ingest(bytes(synchronized_pose), source, 102),
              c2::InboundResult::accepted);
    ASSERT_TRUE(std::holds_alternative<c2::ObservationTurretCommand>(
        server.command_observation(
            101, c2::ObservationTurretCommandType::home, 0, 0, 103)));
    ASSERT_EQ(server.pending_command_count(), 1U);

    const auto new_session = registration(
        101, 11, c2::AssetRole::observation, 51'102);
    ASSERT_EQ(server.ingest(bytes(new_session), {"10.10.0.8", 40'002}, 104),
              c2::InboundResult::accepted);
    EXPECT_EQ(server.pending_command_count(), 0U);
}

TEST(ServerRuntimeRegistrationTest, SendsHeartbeatToEveryRegisteredAssetEndpoint) {
    struct Sent { std::vector<std::byte> data; c2::Endpoint endpoint; };
    std::vector<Sent> sent;
    c2::ServerRuntime server(config(), [&](auto data, const auto& endpoint) {
        sent.push_back({{data.begin(), data.end()}, endpoint});
    });
    const auto observation = registration(
        101, 7, c2::AssetRole::observation, 51'101);
    const auto effector = registration(
        201, 9, c2::AssetRole::effector, 60'201);
    ASSERT_EQ(server.ingest(bytes(observation), {"10.0.0.1", 40'001}, 100),
              c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(effector), {"10.0.0.2", 40'002}, 100),
              c2::InboundResult::accepted);

    server.send_heartbeats(101, 5);

    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(sent[0].endpoint.port, 51'101);
    EXPECT_EQ(sent[1].endpoint.port, 60'201);
    for (std::size_t index = 0; index < sent.size(); ++index) {
        const auto decoded = c2::protobuf::decode(sent[index].data);
        ASSERT_TRUE(std::holds_alternative<c2::Envelope>(decoded));
        const auto& message = std::get<c2::Heartbeat>(
            std::get<c2::Envelope>(decoded).payload);
        EXPECT_EQ(message.header.asset_id, index == 0 ? 101U : 201U);
        EXPECT_EQ(message.header.session_id, index == 0 ? 7U : 9U);
    }
}

TEST(ServerRuntimeAssignmentTest, AssignsNearestEffectorAndReassignsAfterHeartbeatTimeout) {
    struct Sent { std::vector<std::byte> data; c2::Endpoint endpoint; };
    std::vector<Sent> sent;
    auto runtime_config = config();
    runtime_config.registry = {8, 1'000, 10'000, 1'000};
    runtime_config.assignments.required_capabilities =
        c2::capability::effector_point | c2::capability::effector_attack;
    c2::ServerRuntime server(runtime_config, [&](auto data, const auto& endpoint) {
        sent.push_back({{data.begin(), data.end()}, endpoint});
    });
    const c2::Endpoint observer_source{"10.10.0.1", 40'001};
    const c2::Endpoint near_source{"10.10.0.2", 40'002};
    const c2::Endpoint far_source{"10.10.0.3", 40'003};
    const auto observer = registration(101, 1, c2::AssetRole::observation, 51'101);
    const auto near_effector = registration(201, 1, c2::AssetRole::effector, 60'201);
    const auto far_effector = registration(202, 1, c2::AssetRole::effector, 60'202);
    ASSERT_EQ(server.ingest(bytes(observer), observer_source, 100), c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(near_effector), near_source, 100), c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(far_effector), far_source, 100), c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(heartbeat(near_effector, 2)), near_source, 101), c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(heartbeat(far_effector, 2)), far_source, 101), c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(pose(near_effector, 3, 0)), near_source, 102), c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(pose(far_effector, 3, 100)), far_source, 102), c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(effector_status(near_effector, 4)), near_source, 103), c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(effector_status(far_effector, 4)), far_source, 103), c2::InboundResult::accepted);
    c2::TargetCoordinate target{
        {c2::protocol_version, 2, 104, c2::ComponentId::observation_asset,
         c2::ComponentId::command_and_control, 101, 1},
        7, 104, c2::CoordinateFrame::project_frame, 10, 0, 0, 0.9F};
    ASSERT_EQ(server.ingest(bytes(target), observer_source, 104), c2::InboundResult::accepted);

    const auto point = server.point_effector(1, 105);
    ASSERT_TRUE(std::holds_alternative<c2::EffectorTurretCommand>(point));
    ASSERT_TRUE(server.assignment(1).has_value());
    EXPECT_EQ(server.assignment(1)->effector_asset_id, 201U);
    EXPECT_FALSE(server.assignment(1)->manually_selected);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent.front().endpoint.address, near_source.address);
    EXPECT_EQ(sent.front().endpoint.port, 60'201);
    const auto decoded = c2::protobuf::decode(sent.front().data);
    ASSERT_TRUE(std::holds_alternative<c2::Envelope>(decoded));
    const auto& point_command = std::get<c2::EffectorTurretCommand>(
        std::get<c2::Envelope>(decoded).payload);
    EXPECT_EQ(point_command.header.asset_id, 201U);
    EXPECT_EQ(point_command.header.session_id, 1U);
    EXPECT_EQ(point_command.target_id, 1U);

    ASSERT_EQ(server.ingest(bytes(heartbeat(far_effector, 5)), far_source, 1'101),
              c2::InboundResult::accepted);
    const auto reassigned = server.assign(1, 1'102);
    ASSERT_EQ(reassigned.result, c2::AssignmentResult::assigned);
    ASSERT_TRUE(reassigned.assignment.has_value());
    EXPECT_EQ(reassigned.assignment->effector_asset_id, 202U);
}

TEST(ServerRuntimeAssignmentTest, RejectsUnsafeManualChoiceAndMarksUnregisteredAssignmentLost) {
    auto runtime_config = config();
    runtime_config.registry = {8, 1'000, 10'000, 1'000};
    c2::ServerRuntime server(runtime_config, [](auto, auto) {});
    const c2::Endpoint observer_source{"10.10.0.1", 40'001};
    const c2::Endpoint healthy_source{"10.10.0.2", 40'002};
    const c2::Endpoint faulted_source{"10.10.0.3", 40'003};
    const auto observer = registration(101, 1, c2::AssetRole::observation, 51'101);
    const auto healthy = registration(201, 1, c2::AssetRole::effector, 60'201);
    const auto faulted = registration(202, 1, c2::AssetRole::effector, 60'202);
    for (const auto& item : std::vector<std::pair<c2::AssetRegistration, c2::Endpoint>>{
             {observer, observer_source}, {healthy, healthy_source}, {faulted, faulted_source}})
        ASSERT_EQ(server.ingest(bytes(item.first), item.second, 100), c2::InboundResult::accepted);
    for (const auto& item : std::vector<std::pair<c2::AssetRegistration, c2::Endpoint>>{
             {healthy, healthy_source}, {faulted, faulted_source}}) {
        ASSERT_EQ(server.ingest(bytes(heartbeat(item.first, 2)), item.second, 101), c2::InboundResult::accepted);
        ASSERT_EQ(server.ingest(bytes(pose(item.first, 3, 0)), item.second, 102), c2::InboundResult::accepted);
    }
    ASSERT_EQ(server.ingest(bytes(effector_status(healthy, 4)), healthy_source, 103), c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(effector_status(faulted, 4, c2::EffectorState::fault)), faulted_source, 103), c2::InboundResult::accepted);
    c2::TargetCoordinate target{
        {c2::protocol_version, 2, 104, c2::ComponentId::observation_asset,
         c2::ComponentId::command_and_control, 101, 1},
        7, 104, c2::CoordinateFrame::project_frame, 10, 0, 0, 0.9F};
    ASSERT_EQ(server.ingest(bytes(target), observer_source, 104), c2::InboundResult::accepted);

    EXPECT_EQ(server.assign(1, 202, 105).result,
              c2::AssignmentResult::temporarily_unavailable);
    ASSERT_EQ(server.assign(1, 201, 105).result, c2::AssignmentResult::assigned);
    c2::AssetUnregister unregister{
        {c2::protocol_version, 5, 106, c2::ComponentId::effector_asset,
         c2::ComponentId::command_and_control, 201, 1},
        "operator shutdown"};
    ASSERT_EQ(server.ingest(bytes(unregister), healthy_source, 106),
              c2::InboundResult::accepted);
    ASSERT_TRUE(server.assignment(1).has_value());
    EXPECT_EQ(server.assignment(1)->state, c2::AssignmentResult::assignment_lost);
}

TEST(ServerRuntimeAssignmentTest, EnforcesAssignedAttackSafetyAndStopsDisconnectedEffector) {
    struct Sent { std::vector<std::byte> data; c2::Endpoint endpoint; };
    std::vector<Sent> sent;
    auto runtime_config = config();
    runtime_config.registry = {8, 1'000, 10'000, 1'000};
    c2::ServerRuntime server(runtime_config, [&](auto data, const auto& endpoint) {
        sent.push_back({{data.begin(), data.end()}, endpoint});
    });
    const c2::Endpoint observer_source{"10.10.0.1", 40'001};
    const c2::Endpoint effector_source{"10.10.0.2", 40'002};
    const auto observer = registration(101, 1, c2::AssetRole::observation, 51'101);
    const auto effector = registration(201, 9, c2::AssetRole::effector, 60'201);
    ASSERT_EQ(server.ingest(bytes(observer), observer_source, 100), c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(effector), effector_source, 100), c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(heartbeat(effector, 2)), effector_source, 101), c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(pose(effector, 3, 0)), effector_source, 102), c2::InboundResult::accepted);
    ASSERT_EQ(server.ingest(bytes(effector_status(effector, 4)), effector_source, 103), c2::InboundResult::accepted);
    c2::TargetCoordinate target{
        {c2::protocol_version, 2, 104, c2::ComponentId::observation_asset,
         c2::ComponentId::command_and_control, 101, 1},
        7, 104, c2::CoordinateFrame::project_frame, 10, 0, 0, 0.9F};
    ASSERT_EQ(server.ingest(bytes(target), observer_source, 104), c2::InboundResult::accepted);
    ASSERT_EQ(server.assign(1, 105).result, c2::AssignmentResult::assigned);

    EXPECT_EQ(std::get<c2::DispatchError>(
                  server.attack(c2::AttackAction::arm, 1, 0, 106)),
              c2::DispatchError::command_rejected);
    ASSERT_TRUE(std::holds_alternative<c2::EffectorTurretCommand>(
        server.point_effector(1, 107)));
    const auto arm = server.attack(c2::AttackAction::arm, 1, 0, 108);
    ASSERT_TRUE(std::holds_alternative<c2::AttackCommand>(arm));
    EXPECT_EQ(std::get<c2::AttackCommand>(arm).header.asset_id, 201U);
    EXPECT_EQ(std::get<c2::AttackCommand>(arm).header.session_id, 9U);

    auto armed = effector_status(effector, 5);
    armed.attack_armed = true;
    ASSERT_EQ(server.ingest(bytes(armed), effector_source, 109), c2::InboundResult::accepted);
    const auto start = server.attack(c2::AttackAction::start, 1, 500, 110);
    ASSERT_TRUE(std::holds_alternative<c2::AttackCommand>(start));
    EXPECT_TRUE(server.assignment(1)->attack_started);
    EXPECT_EQ(server.unassign(1), c2::AssignmentResult::operator_action_required);

    c2::AssetUnregister unregister{
        {c2::protocol_version, 6, 111, c2::ComponentId::effector_asset,
         c2::ComponentId::command_and_control, 201, 9}, "link lost"};
    ASSERT_EQ(server.ingest(bytes(unregister), effector_source, 111),
              c2::InboundResult::accepted);
    EXPECT_EQ(server.assignment(1)->state,
              c2::AssignmentResult::operator_action_required);
    const auto stop = server.stop_effector(201, 112);
    ASSERT_TRUE(std::holds_alternative<c2::AttackCommand>(stop));
    EXPECT_EQ(sent.back().endpoint.port, 60'201);
    EXPECT_EQ(std::get<c2::AttackCommand>(stop).action, c2::AttackAction::stop);

    const auto before_targeted_estop = sent.size();
    const auto targeted_estop = server.emergency_stop_effector(201, 113);
    ASSERT_TRUE(std::holds_alternative<c2::AttackCommand>(targeted_estop));
    ASSERT_EQ(sent.size(), before_targeted_estop +
                               runtime_config.emergency_stop_repetitions);
    std::uint32_t targeted_command_id{};
    for (std::size_t index = before_targeted_estop; index < sent.size(); ++index) {
        const auto decoded = c2::protobuf::decode(sent[index].data);
        ASSERT_TRUE(std::holds_alternative<c2::Envelope>(decoded));
        const auto& command = std::get<c2::AttackCommand>(
            std::get<c2::Envelope>(decoded).payload);
        EXPECT_EQ(command.action, c2::AttackAction::emergency_stop);
        if (targeted_command_id == 0) targeted_command_id = command.command_id;
        EXPECT_EQ(command.command_id, targeted_command_id);
    }

    const auto before_estop = sent.size();
    const auto estop = server.emergency_stop_all(114);
    EXPECT_EQ(estop.assets, 1U);
    EXPECT_EQ(estop.datagrams, runtime_config.emergency_stop_repetitions);
    ASSERT_EQ(sent.size(), before_estop + runtime_config.emergency_stop_repetitions);
    std::uint32_t repeated_command_id{};
    for (std::size_t index = before_estop; index < sent.size(); ++index) {
        const auto decoded = c2::protobuf::decode(sent[index].data);
        ASSERT_TRUE(std::holds_alternative<c2::Envelope>(decoded));
        const auto& command = std::get<c2::AttackCommand>(
            std::get<c2::Envelope>(decoded).payload);
        EXPECT_EQ(command.action, c2::AttackAction::emergency_stop);
        EXPECT_EQ(command.header.asset_id, 201U);
        EXPECT_EQ(command.header.session_id, 9U);
        EXPECT_EQ(sent[index].endpoint.port, 60'201);
        if (repeated_command_id == 0) repeated_command_id = command.command_id;
        EXPECT_EQ(command.command_id, repeated_command_id);
    }
}
}  // namespace
