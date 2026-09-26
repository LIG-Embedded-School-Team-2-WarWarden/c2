#include "pch.h"

#include "c2/dummy_assets.hpp"
#include "c2/protobuf_codec.hpp"
#include "c2/server_runtime.hpp"

#include <cstddef>
#include <memory>
#include <variant>
#include <vector>

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
    const auto arm = server->attack(c2::AttackAction::arm, 7, 0, 130);
    ASSERT_TRUE(std::holds_alternative<c2::AttackCommand>(arm));
    EXPECT_TRUE(effector.status(131).attack_armed);
    const auto start = server->attack(c2::AttackAction::start, 7, 100, 140);
    ASSERT_TRUE(std::holds_alternative<c2::AttackCommand>(start));
    EXPECT_TRUE(effector.status(141).attack_active);
    EXPECT_FALSE(effector.status(100'140).attack_active);
}
}  // namespace
