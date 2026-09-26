#include "pch.h"

#include "c2/protocol_validation.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace {
c2::MessageHeader header(const c2::ComponentId source, const c2::ComponentId destination) {
    return {c2::protocol_version, 7, 1'000'000, source, destination};
}

bool has_error(const c2::ValidationResult& result, const std::string_view expected) {
    return std::find(result.errors.begin(), result.errors.end(), expected) != result.errors.end();
}

c2::ObservationStatus valid_observation_status() {
    return {header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control),
            c2::ObservationState::operating,
            15.0F,
            -3.0F,
            true,
            false,
            0,
            999'900};
}

c2::EffectorStatus valid_effector_status() {
    return {header(c2::ComponentId::effector_asset, c2::ComponentId::command_and_control),
            c2::EffectorState::ready,
            15.0F,
            -3.0F,
            15.0F,
            -3.0F,
            true,
            true,
            false,
            0,
            999'900};
}

c2::AttackCommand valid_attack_command(
    const c2::AttackAction action = c2::AttackAction::start) {
    return {header(c2::ComponentId::command_and_control, c2::ComponentId::effector_asset),
            31,
            42,
            action,
            action == c2::AttackAction::start ? 500U : 0U,
            1'000'001};
}

c2::CommandAck valid_ack(const c2::ComponentId source = c2::ComponentId::effector_asset) {
    return {header(source, c2::ComponentId::command_and_control),
            31,
            c2::CommandResult::accepted,
            0,
            999'950};
}

c2::Heartbeat valid_heartbeat(const c2::ComponentId source = c2::ComponentId::observation_asset) {
    return {header(source, c2::ComponentId::command_and_control),
            c2::AssetOperatingState::standby,
            5'000,
            999'950};
}

c2::ErrorReport valid_error_report(const c2::ComponentId source = c2::ComponentId::effector_asset) {
    return {header(source, c2::ComponentId::command_and_control),
            0x3001,
            c2::ErrorSeverity::error,
            31,
            999'950,
            "PAN_FAULT"};
}

TEST(ObservationStatusContractTest, DerivesScanningFromOperatingAndLidarState) {
    auto status = valid_observation_status();
    EXPECT_TRUE(c2::is_scanning(status));

    status.lidar_active = false;
    EXPECT_FALSE(c2::is_scanning(status));
    status.lidar_active = true;
    status.state = c2::ObservationState::standby;
    EXPECT_FALSE(c2::is_scanning(status));
}

TEST(ObservationStatusContractTest, AcceptsEveryDefinedState) {
    for (const auto state : {c2::ObservationState::off,
                             c2::ObservationState::initializing,
                             c2::ObservationState::standby,
                             c2::ObservationState::operating,
                             c2::ObservationState::fault}) {
        auto status = valid_observation_status();
        status.state = state;
        EXPECT_TRUE(c2::validate(status).valid());
    }
}

TEST(ObservationStatusContractTest, RejectsInvalidRouteStateAnglesAndTimestamp) {
    auto status = valid_observation_status();
    status.header.source_id = c2::ComponentId::effector_asset;
    EXPECT_TRUE(has_error(c2::validate(status), "unexpected source_id"));

    status = valid_observation_status();
    status.state = static_cast<c2::ObservationState>(99);
    EXPECT_TRUE(has_error(c2::validate(status), "unsupported observation state"));

    status = valid_observation_status();
    status.current_pan_deg = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(has_error(c2::validate(status), "current angles must be finite"));

    status = valid_observation_status();
    status.timestamp_us = 0;
    EXPECT_TRUE(has_error(c2::validate(status), "status timestamp_us must be non-zero"));
}

TEST(EffectorStatusContractTest, AcceptsEveryDefinedState) {
    for (const auto state : {c2::EffectorState::off,
                             c2::EffectorState::initializing,
                             c2::EffectorState::standby,
                             c2::EffectorState::slewing,
                             c2::EffectorState::ready,
                             c2::EffectorState::active,
                             c2::EffectorState::fault}) {
        auto status = valid_effector_status();
        status.state = state;
        EXPECT_TRUE(c2::validate(status).valid());
    }
}

TEST(EffectorStatusContractTest, RejectsInvalidRouteStateAnglesAndTimestamp) {
    auto status = valid_effector_status();
    status.header.destination_id = c2::ComponentId::observation_asset;
    EXPECT_TRUE(has_error(c2::validate(status), "unexpected destination_id"));

    status = valid_effector_status();
    status.state = c2::EffectorState::unspecified;
    EXPECT_TRUE(has_error(c2::validate(status), "unsupported effector state"));

    status = valid_effector_status();
    status.target_tilt_deg = std::numeric_limits<float>::infinity();
    EXPECT_TRUE(has_error(c2::validate(status), "effector angles must be finite"));

    status = valid_effector_status();
    status.timestamp_us = 0;
    EXPECT_TRUE(has_error(c2::validate(status), "status timestamp_us must be non-zero"));
}

TEST(AttackCommandContractTest, AcceptsEveryActionWithApplicableFields) {
    for (const auto action : {c2::AttackAction::arm,
                              c2::AttackAction::start,
                              c2::AttackAction::stop,
                              c2::AttackAction::emergency_stop}) {
        auto command = valid_attack_command(action);
        if (action == c2::AttackAction::stop || action == c2::AttackAction::emergency_stop)
            command.target_id = 0;
        EXPECT_TRUE(c2::validate(command).valid());
    }
}

TEST(AttackCommandContractTest, EnforcesIdentityTargetAndDurationRules) {
    auto command = valid_attack_command();
    command.command_id = 0;
    EXPECT_TRUE(has_error(c2::validate(command), "command_id must be non-zero"));

    command = valid_attack_command(c2::AttackAction::arm);
    command.target_id = 0;
    EXPECT_TRUE(has_error(c2::validate(command), "ARM and START require target_id"));

    command = valid_attack_command();
    command.duration_ms = 0;
    EXPECT_TRUE(has_error(c2::validate(command), "START requires non-zero duration_ms"));
}

TEST(AttackCommandContractTest, RejectsInvalidRouteAndAction) {
    auto command = valid_attack_command();
    command.header.destination_id = c2::ComponentId::observation_asset;
    EXPECT_TRUE(has_error(c2::validate(command), "unexpected destination_id"));

    command = valid_attack_command();
    command.action = static_cast<c2::AttackAction>(99);
    EXPECT_TRUE(has_error(c2::validate(command), "unsupported attack action"));

    command = valid_attack_command();
    command.valid_until_us = command.header.timestamp_us;
    EXPECT_TRUE(has_error(
        c2::validate(command), "valid_until_us must be later than timestamp_us"));

    command.valid_until_us = command.header.timestamp_us - 1;
    EXPECT_TRUE(has_error(
        c2::validate(command), "valid_until_us must be later than timestamp_us"));
}

TEST(CommandAckContractTest, AcceptsBothAssetSourcesAndEveryResult) {
    for (const auto source : {c2::ComponentId::observation_asset, c2::ComponentId::effector_asset}) {
        for (const auto result : {c2::CommandResult::received,
                                  c2::CommandResult::accepted,
                                  c2::CommandResult::in_progress,
                                  c2::CommandResult::completed,
                                  c2::CommandResult::rejected,
                                  c2::CommandResult::failed}) {
            auto ack = valid_ack(source);
            ack.result = result;
            EXPECT_TRUE(c2::validate(ack).valid());
        }
    }
}

TEST(CommandAckContractTest, RejectsInvalidSourceIdentityResultAndTimestamp) {
    auto ack = valid_ack(c2::ComponentId::command_and_control);
    EXPECT_TRUE(has_error(c2::validate(ack), "CommandAck source must be an asset"));

    ack = valid_ack();
    ack.command_id = 0;
    EXPECT_TRUE(has_error(c2::validate(ack), "command_id must be non-zero"));

    ack = valid_ack();
    ack.result = c2::CommandResult::unspecified;
    EXPECT_TRUE(has_error(c2::validate(ack), "unsupported command result"));

    ack = valid_ack();
    ack.timestamp_us = 0;
    EXPECT_TRUE(has_error(c2::validate(ack), "ack timestamp_us must be non-zero"));
}

TEST(HeartbeatContractTest, AcceptsBothAssetsAndEveryOperatingState) {
    for (const auto source : {c2::ComponentId::observation_asset, c2::ComponentId::effector_asset}) {
        for (const auto state : {c2::AssetOperatingState::off,
                                 c2::AssetOperatingState::initializing,
                                 c2::AssetOperatingState::standby,
                                 c2::AssetOperatingState::operating,
                                 c2::AssetOperatingState::slewing,
                                 c2::AssetOperatingState::ready,
                                 c2::AssetOperatingState::active,
                                 c2::AssetOperatingState::fault}) {
            auto heartbeat = valid_heartbeat(source);
            heartbeat.state = state;
            EXPECT_TRUE(c2::validate(heartbeat).valid());
        }
    }
}

TEST(HeartbeatContractTest, AcceptsCommandAndControlToBothAssets) {
    for (const auto destination : {c2::ComponentId::observation_asset,
                                   c2::ComponentId::effector_asset}) {
        c2::Heartbeat heartbeat{
            {c2::protocol_version, 1, 10,
             c2::ComponentId::command_and_control, destination},
            c2::AssetOperatingState::operating, 100, 10};
        EXPECT_TRUE(c2::validate(heartbeat).valid());
    }
}

TEST(HeartbeatContractTest, RejectsInvalidSourceStateAndTimestamp) {
    auto heartbeat = valid_heartbeat(c2::ComponentId::unspecified);
    EXPECT_TRUE(has_error(c2::validate(heartbeat), "Heartbeat route is unsupported"));

    heartbeat = valid_heartbeat();
    heartbeat.state = c2::AssetOperatingState::unspecified;
    EXPECT_TRUE(has_error(c2::validate(heartbeat), "unsupported operating state"));

    heartbeat = valid_heartbeat();
    heartbeat.timestamp_us = 0;
    EXPECT_TRUE(has_error(c2::validate(heartbeat), "heartbeat timestamp_us must be non-zero"));
}

TEST(ErrorReportContractTest, AcceptsBothAssetsAndEverySeverity) {
    for (const auto source : {c2::ComponentId::observation_asset, c2::ComponentId::effector_asset}) {
        for (const auto severity : {c2::ErrorSeverity::info,
                                    c2::ErrorSeverity::warning,
                                    c2::ErrorSeverity::error,
                                    c2::ErrorSeverity::critical}) {
            auto report = valid_error_report(source);
            report.severity = severity;
            EXPECT_TRUE(c2::validate(report).valid());
        }
    }
}

TEST(ErrorReportContractTest, RejectsInvalidSourceCodeSeverityAndTimestamp) {
    auto report = valid_error_report(c2::ComponentId::command_and_control);
    EXPECT_TRUE(has_error(c2::validate(report), "ErrorReport source must be an asset"));

    report = valid_error_report();
    report.error_code = 0;
    EXPECT_TRUE(has_error(c2::validate(report), "error_code must be non-zero"));

    report = valid_error_report();
    report.severity = c2::ErrorSeverity::unspecified;
    EXPECT_TRUE(has_error(c2::validate(report), "unsupported error severity"));

    report = valid_error_report();
    report.timestamp_us = 0;
    EXPECT_TRUE(has_error(c2::validate(report), "error timestamp_us must be non-zero"));
}

TEST(EnvelopeContractTest, IdentifiesAndValidatesEveryPayloadType) {
    const std::array envelopes{
        c2::Envelope{c2::AssetPose{header(c2::ComponentId::effector_asset,
                                         c2::ComponentId::command_and_control),
                                  c2::CoordinateFrame::project_frame, 0, 0, 0, 0}},
        c2::Envelope{c2::TargetCoordinate{header(c2::ComponentId::observation_asset,
                                                c2::ComponentId::command_and_control),
                                         1, 1, c2::CoordinateFrame::project_frame, 1, 2, 3, 1}},
        c2::Envelope{valid_observation_status()},
        c2::Envelope{c2::ObservationTurretCommand{
            header(c2::ComponentId::command_and_control, c2::ComponentId::observation_asset),
            1, c2::ObservationTurretCommandType::home, 0, 0, 1'000'001}},
        c2::Envelope{c2::EffectorTurretCommand{
            header(c2::ComponentId::command_and_control, c2::ComponentId::effector_asset),
            1, 1, 0, 0, 1'000'001}},
        c2::Envelope{valid_attack_command()},
        c2::Envelope{valid_effector_status()},
        c2::Envelope{valid_ack()},
        c2::Envelope{valid_heartbeat()},
        c2::Envelope{valid_error_report()},
    };
    constexpr std::array expected{
        c2::MessageKind::asset_pose,
        c2::MessageKind::target_coordinate,
        c2::MessageKind::observation_status,
        c2::MessageKind::observation_turret_command,
        c2::MessageKind::effector_turret_command,
        c2::MessageKind::attack_command,
        c2::MessageKind::effector_status,
        c2::MessageKind::command_ack,
        c2::MessageKind::heartbeat,
        c2::MessageKind::error_report,
    };

    for (std::size_t index = 0; index < envelopes.size(); ++index) {
        EXPECT_EQ(c2::message_kind(envelopes[index]), expected[index]);
        EXPECT_TRUE(c2::validate(envelopes[index]).valid());
    }
}

TEST(EnvelopeContractTest, RejectsMissingPayload) {
    const c2::Envelope envelope;
    EXPECT_EQ(c2::message_kind(envelope), c2::MessageKind::unspecified);
    EXPECT_TRUE(has_error(c2::validate(envelope), "envelope payload must be set"));
}
}  // namespace
