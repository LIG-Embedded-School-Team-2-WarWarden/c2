#include "pch.h"

#include "c2/dummy_assets.hpp"
#include "c2/protobuf_codec.hpp"
#include "c2/server_runtime.hpp"
#include "c2/server_udp_ingress.hpp"
#include "c2/udp_transport.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <variant>
#include <vector>
#include <thread>

namespace {
std::vector<std::byte> encode(c2::MessagePayload payload) {
    return c2::protobuf::encode(c2::Envelope{std::move(payload)});
}

c2::AssetPose initial_pose(c2::ComponentId source, float x) {
    return {{c2::protocol_version, 1, 1, source,
             c2::ComponentId::command_and_control},
            c2::CoordinateFrame::project_frame, x, 0, 0, 0};
}

TEST(SystemIntegrationTest, RunsObservationToPointingAndAttackFlow) {
    c2::DummyObservationAsset observation(
        initial_pose(c2::ComponentId::observation_asset, 0),
        {-180, 180, -90, 90});
    c2::DummyEffectorAsset effector(
        initial_pose(c2::ComponentId::effector_asset, 10));
    std::unique_ptr<c2::ServerRuntime> server;
    auto ingest = [&](const auto& message, std::uint64_t received) {
        EXPECT_EQ(server->ingest(encode(message), received),
                  c2::InboundResult::accepted);
    };
    c2::ServerRuntimeConfig config{
        {10'000, 8}, {1'000},
        {{-180, 180, -90, 90}, 500, 1, 1},
        {{-180, 180, -90, 90}, 500, 1, 1},
        {500, 1, 1}, {"127.0.0.1", 5101}, {"127.0.0.1", 6001}};
    server = std::make_unique<c2::ServerRuntime>(
        config, [&](std::span<const std::byte> bytes, const c2::Endpoint&) {
            const auto decoded = c2::protobuf::decode(bytes);
            ASSERT_TRUE(std::holds_alternative<c2::Envelope>(decoded));
            const auto& payload = std::get<c2::Envelope>(decoded).payload;
            if (const auto* command = std::get_if<c2::ObservationTurretCommand>(&payload)) {
                const auto result = observation.handle(*command, command->header.timestamp_us);
                ingest(result.acknowledgement, command->header.timestamp_us);
                ingest(observation.status(command->header.timestamp_us), command->header.timestamp_us);
            } else if (const auto* point = std::get_if<c2::EffectorTurretCommand>(&payload)) {
                const auto result = effector.handle(*point, point->header.timestamp_us);
                ingest(result.acknowledgement, point->header.timestamp_us);
                ingest(effector.status(point->header.timestamp_us), point->header.timestamp_us);
            } else if (const auto* attack = std::get_if<c2::AttackCommand>(&payload)) {
                const auto result = effector.handle(*attack, attack->header.timestamp_us);
                ingest(result.acknowledgement, attack->header.timestamp_us);
                ingest(effector.status(attack->header.timestamp_us), attack->header.timestamp_us);
            }
        });

    ingest(observation.heartbeat(100, 1), 100);
    ingest(observation.asset_pose(101), 101);
    ingest(observation.target(7, 100, 0, 0, 1, 102), 102);
    ingest(effector.heartbeat(100, 1), 100);
    ingest(effector.asset_pose(101), 101);
    ingest(effector.status(102), 102);

    const auto scan = server->command_observation(
        c2::ObservationTurretCommandType::scan, 30, 5, 110);
    ASSERT_TRUE(std::holds_alternative<c2::ObservationTurretCommand>(scan));
    EXPECT_TRUE(c2::is_scanning(observation.status(111)));

    const auto point = server->point_effector(7, 120);
    ASSERT_TRUE(std::holds_alternative<c2::EffectorTurretCommand>(point));
    EXPECT_TRUE(effector.status(121).aligned);
    // Absolute-angle movement does not designate a target for output.
    const auto arm = server->attack(c2::AttackAction::arm, 7, 0, 130);
    EXPECT_TRUE(std::holds_alternative<c2::DispatchError>(arm));
    EXPECT_FALSE(effector.status(131).attack_armed);

}

TEST(SystemIntegrationTest, RegistersAssetThroughUdpUsingActualSourceEndpoint) {
    c2::ServerRuntimeConfig config{
        {10'000, 8}, {1'000},
        {{-180, 180, -90, 90}, 500, 1, 1},
        {{-180, 180, -90, 90}, 500, 1, 1},
        {500, 1, 1}, {"127.0.0.1", 5101}, {"127.0.0.1", 6001}};
    config.registry = {8, 1'000, 10'000, 1'000};
    c2::ServerRuntime server(config, [](auto, auto) {});
    c2::ServerUdpIngress ingress(
        server, {"127.0.0.1", 0}, [] { return std::uint64_t{100}; });
    c2::UdpTransport asset({"127.0.0.1", 0}, [](auto, auto) {});
    ingress.start();
    asset.start();
    const auto asset_source = asset.local_endpoint();
    c2::AssetRegistration registration{
        {c2::protocol_version, 1, 10, c2::ComponentId::observation_asset,
         c2::ComponentId::command_and_control, 101, 7},
        c2::AssetRole::observation, 51'101,
        c2::capability::observation_scan, "udp-test/2.0", {},
        {-180, 180, -90, 90}, false, 5'000};
    asset.send(encode(registration), ingress.local_endpoint());

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(1);
    while (server.assets(100).empty() &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto assets = server.assets(100);
    ASSERT_EQ(assets.size(), 1U);
    EXPECT_EQ(assets.front().source_endpoint.address, "127.0.0.1");
    EXPECT_EQ(assets.front().source_endpoint.port, asset_source.port);
    EXPECT_EQ(assets.front().command_endpoint.port, 51'101);
    asset.stop();
    ingress.stop();
}
}  // namespace
