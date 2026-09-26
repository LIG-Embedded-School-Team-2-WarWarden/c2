#include "pch.h"

#include "c2/protobuf_codec.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
c2::MessageHeader header(const c2::ComponentId source, const c2::ComponentId destination) {
    return {c2::protocol_version, 1, 1, source, destination};
}

std::uint8_t nibble(const char value) {
    if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
    if (value >= 'A' && value <= 'F') return static_cast<std::uint8_t>(value - 'A' + 10);
    if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
    throw std::invalid_argument("invalid hexadecimal digit");
}

std::vector<std::byte> bytes(const std::string_view hex) {
    if (hex.size() % 2 != 0) throw std::invalid_argument("hexadecimal text must have even length");
    std::vector<std::byte> result;
    result.reserve(hex.size() / 2);
    for (std::size_t index = 0; index < hex.size(); index += 2)
        result.push_back(static_cast<std::byte>((nibble(hex[index]) << 4) | nibble(hex[index + 1])));
    return result;
}

std::vector<std::pair<c2::Envelope, std::vector<std::byte>>> golden_packets() {
    return {
        {c2::Envelope{c2::AssetPose{header(c2::ComponentId::effector_asset,
                                          c2::ComponentId::command_and_control),
                                   c2::CoordinateFrame::project_frame, 0, 0, 0, 0}},
         bytes("0A0E0A0A080110011801200328021001")},
        {c2::Envelope{c2::TargetCoordinate{
             header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control),
             1, 1, c2::CoordinateFrame::project_frame, 0, 0, 0, 0}},
         bytes("12120A0A08011001180120012802100118012001")},
        {c2::Envelope{c2::ObservationStatus{
             header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control),
             c2::ObservationState::off, 0, 0, false, false, 0, 1}},
         bytes("1A100A0A0801100118012001280210014001")},
        {c2::Envelope{c2::ObservationTurretCommand{
             header(c2::ComponentId::command_and_control, c2::ComponentId::observation_asset),
             1, c2::ObservationTurretCommandType::home, 0, 0, 2}},
         bytes("22120A0A08011001180120022801100118013002")},
        {c2::Envelope{c2::EffectorTurretCommand{
             header(c2::ComponentId::command_and_control, c2::ComponentId::effector_asset),
             1, 1, 0, 0, 2}},
         bytes("2A120A0A08011001180120022803100118013002")},
        {c2::Envelope{c2::AttackCommand{
             header(c2::ComponentId::command_and_control, c2::ComponentId::effector_asset),
             1, 1, c2::AttackAction::arm, 0, 2}},
         bytes("32140A0A080110011801200228031001180120013002")},
        {c2::Envelope{c2::EffectorStatus{
             header(c2::ComponentId::effector_asset, c2::ComponentId::command_and_control),
             c2::EffectorState::off, 0, 0, 0, 0, false, false, false, 0, 1}},
         bytes("3A100A0A0801100118012003280210015801")},
        {c2::Envelope{c2::CommandAck{
             header(c2::ComponentId::effector_asset, c2::ComponentId::command_and_control),
             1, c2::CommandResult::received, 0, 1}},
         bytes("42120A0A08011001180120032802100118012801")},
        {c2::Envelope{c2::Heartbeat{
             header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control),
             c2::AssetOperatingState::off, 0, 1}},
         bytes("4A100A0A0801100118012001280210012001")},
        {c2::Envelope{c2::ErrorReport{
             header(c2::ComponentId::effector_asset, c2::ComponentId::command_and_control),
             1, c2::ErrorSeverity::info, 0, 1, {}}},
         bytes("52120A0A08011001180120032802100118012801")},
    };
}

void expect_decode_error(
    const std::span<const std::byte> encoded, const c2::protobuf::DecodeError expected) {
    const auto result = c2::protobuf::decode(encoded);
    ASSERT_TRUE(std::holds_alternative<c2::protobuf::DecodeError>(result));
    EXPECT_EQ(std::get<c2::protobuf::DecodeError>(result), expected);
}

TEST(ProtobufCodecTest, MatchesGoldenPacketAndRoundTripsEveryMessage) {
    for (const auto& [envelope, expected] : golden_packets()) {
        SCOPED_TRACE(static_cast<std::uint32_t>(c2::message_kind(envelope)));
        EXPECT_EQ(c2::protobuf::encode(envelope), expected);

        const auto decoded = c2::protobuf::decode(expected);
        ASSERT_TRUE(std::holds_alternative<c2::Envelope>(decoded));
        const auto& decoded_envelope = std::get<c2::Envelope>(decoded);
        EXPECT_EQ(c2::message_kind(decoded_envelope), c2::message_kind(envelope));
        EXPECT_EQ(c2::protobuf::encode(decoded_envelope), expected);
    }
}

TEST(ProtobufCodecTest, RejectsMissingMalformedAndWrongWirePayloads) {
    expect_decode_error({}, c2::protobuf::DecodeError::missing_payload);
    expect_decode_error(bytes("80"), c2::protobuf::DecodeError::malformed);
    expect_decode_error(bytes("0A0208"), c2::protobuf::DecodeError::malformed);
    expect_decode_error(bytes("0801"), c2::protobuf::DecodeError::malformed);
}

TEST(ProtobufCodecTest, RejectsSemanticallyInvalidDecodedMessageWithoutThrowing) {
    auto invalid_state = bytes("1A100A0A0801100118012001280210634001");
    expect_decode_error(invalid_state, c2::protobuf::DecodeError::invalid_message);
}

TEST(ProtobufCodecTest, RefusesToEncodeInvalidEnvelope) {
    EXPECT_THROW((void)c2::protobuf::encode(c2::Envelope{}), std::invalid_argument);

    auto command = std::get<c2::AttackCommand>(golden_packets()[5].first.payload);
    command.valid_until_us = command.header.timestamp_us;
    EXPECT_THROW((void)c2::protobuf::encode(c2::Envelope{command}), std::invalid_argument);
}

TEST(ProtobufCodecTest, UsesLastKnownPayloadForProtobufOneofCompatibility) {
    const auto packets = golden_packets();
    auto combined = packets[0].second;
    const auto& heartbeat = packets[8].second;
    combined.insert(combined.end(), heartbeat.begin(), heartbeat.end());

    const auto decoded = c2::protobuf::decode(combined);
    ASSERT_TRUE(std::holds_alternative<c2::Envelope>(decoded));
    EXPECT_EQ(c2::message_kind(std::get<c2::Envelope>(decoded)), c2::MessageKind::heartbeat);
}

TEST(ProtobufCodecTest, RoundTripsNonDefaultFloatsBooleansStringsAndMultibyteVarints) {
    c2::EffectorStatus status{
        {c2::protocol_version, 300, 70'000, c2::ComponentId::effector_asset,
         c2::ComponentId::command_and_control},
        c2::EffectorState::active,
        12.5F,
        -3.25F,
        13.0F,
        -2.75F,
        true,
        true,
        true,
        0x3001,
        69'999};
    const c2::Envelope original{status};
    const auto encoded = c2::protobuf::encode(original);
    const auto decoded = c2::protobuf::decode(encoded);
    ASSERT_TRUE(std::holds_alternative<c2::Envelope>(decoded));
    const auto& decoded_status =
        std::get<c2::EffectorStatus>(std::get<c2::Envelope>(decoded).payload);
    EXPECT_FLOAT_EQ(decoded_status.current_pan_deg, status.current_pan_deg);
    EXPECT_FLOAT_EQ(decoded_status.current_tilt_deg, status.current_tilt_deg);
    EXPECT_TRUE(decoded_status.aligned);
    EXPECT_TRUE(decoded_status.attack_armed);
    EXPECT_TRUE(decoded_status.attack_active);
    EXPECT_EQ(decoded_status.error_code, status.error_code);
    EXPECT_EQ(c2::protobuf::encode(std::get<c2::Envelope>(decoded)), encoded);

    c2::ErrorReport report{
        header(c2::ComponentId::observation_asset, c2::ComponentId::command_and_control),
        0x2001,
        c2::ErrorSeverity::warning,
        300,
        1,
        "LiDAR warning"};
    const auto report_encoded = c2::protobuf::encode(c2::Envelope{report});
    const auto report_decoded = c2::protobuf::decode(report_encoded);
    ASSERT_TRUE(std::holds_alternative<c2::Envelope>(report_decoded));
    EXPECT_EQ(
        std::get<c2::ErrorReport>(std::get<c2::Envelope>(report_decoded).payload).detail,
        report.detail);
}

TEST(ProtobufCodecTest, SkipsUnknownFieldsAndRejectsWrongWireTypeOnKnownField) {
    auto with_unknown = golden_packets()[8].second;
    const auto unknown_field = bytes("7A01FF");
    with_unknown.insert(with_unknown.end(), unknown_field.begin(), unknown_field.end());
    const auto decoded = c2::protobuf::decode(with_unknown);
    ASSERT_TRUE(std::holds_alternative<c2::Envelope>(decoded));
    EXPECT_EQ(c2::message_kind(std::get<c2::Envelope>(decoded)), c2::MessageKind::heartbeat);

    expect_decode_error(
        bytes("1A130A0A0801100118012001280215000000004001"),
        c2::protobuf::DecodeError::malformed);
}

TEST(ProtobufCodecTest, RejectsEveryTruncatedGoldenPacketWithoutThrowing) {
    for (const auto& [envelope, expected] : golden_packets()) {
        SCOPED_TRACE(static_cast<std::uint32_t>(c2::message_kind(envelope)));
        for (std::size_t size = 0; size < expected.size(); ++size) {
            const auto result = c2::protobuf::decode({expected.data(), size});
            EXPECT_TRUE(std::holds_alternative<c2::protobuf::DecodeError>(result));
        }
    }

    expect_decode_error(
        bytes("8080808080808080808002"), c2::protobuf::DecodeError::malformed);
    expect_decode_error(bytes("0A00"), c2::protobuf::DecodeError::invalid_message);
}
}  // namespace
