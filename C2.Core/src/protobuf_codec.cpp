#include "c2/protobuf_codec.hpp"

#include "c2/protocol_validation.hpp"

#include <bit>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace c2::protobuf {
namespace {
constexpr std::uint8_t wire_varint = 0;
constexpr std::uint8_t wire_fixed64 = 1;
constexpr std::uint8_t wire_length_delimited = 2;
constexpr std::uint8_t wire_fixed32 = 5;

class Reader final {
public:
    explicit Reader(const std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool done() const noexcept { return offset_ == bytes_.size(); }

    bool fail() noexcept {
        failed_ = true;
        return false;
    }

    bool key(std::uint32_t& field, std::uint8_t& wire_type) {
        std::uint64_t value{};
        if (!varint(value) || value == 0) return fail();
        field = static_cast<std::uint32_t>(value >> 3);
        wire_type = static_cast<std::uint8_t>(value & 7U);
        return field != 0 || fail();
    }

    bool varint(std::uint64_t& value) {
        value = 0;
        for (unsigned index = 0; index < 10; ++index) {
            if (offset_ >= bytes_.size()) return fail();
            const auto octet = std::to_integer<std::uint8_t>(bytes_[offset_++]);
            if (index == 9 && octet > 1) return fail();
            value |= static_cast<std::uint64_t>(octet & 0x7FU) << (index * 7);
            if ((octet & 0x80U) == 0) return true;
        }
        return fail();
    }

    bool fixed32(float& value) {
        if (bytes_.size() - offset_ < sizeof(std::uint32_t)) return fail();
        std::uint32_t bits{};
        for (unsigned index = 0; index < sizeof(bits); ++index)
            bits |= static_cast<std::uint32_t>(
                        std::to_integer<std::uint8_t>(bytes_[offset_++]))
                    << (index * 8);
        value = std::bit_cast<float>(bits);
        return true;
    }

    bool message(std::span<const std::byte>& value) {
        std::uint64_t size{};
        if (!varint(size)) return false;
        if (size > bytes_.size() - offset_) return fail();
        value = bytes_.subspan(offset_, static_cast<std::size_t>(size));
        offset_ += static_cast<std::size_t>(size);
        return true;
    }

    bool string(std::string& value) {
        std::span<const std::byte> encoded;
        if (!message(encoded)) return false;
        value.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        return true;
    }

    bool skip(const std::uint8_t wire_type) {
        if (failed_) return false;
        std::uint64_t ignored{};
        std::span<const std::byte> message_bytes;
        switch (wire_type) {
            case wire_varint:
                return varint(ignored);
            case wire_fixed64:
                if (bytes_.size() - offset_ < 8) return fail();
                offset_ += 8;
                return true;
            case wire_length_delimited:
                return message(message_bytes);
            case wire_fixed32:
                if (bytes_.size() - offset_ < 4) return fail();
                offset_ += 4;
                return true;
            default:
                return fail();
        }
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
    bool failed_{};
};

void put_varint(std::vector<std::byte>& output, std::uint64_t value) {
    while (value >= 0x80U) {
        output.push_back(static_cast<std::byte>((value & 0x7FU) | 0x80U));
        value >>= 7;
    }
    output.push_back(static_cast<std::byte>(value));
}

void put_key(
    std::vector<std::byte>& output, const std::uint32_t field, const std::uint8_t wire_type) {
    put_varint(output, (static_cast<std::uint64_t>(field) << 3) | wire_type);
}

void put_uint(
    std::vector<std::byte>& output, const std::uint32_t field, const std::uint64_t value) {
    if (value == 0) return;
    put_key(output, field, wire_varint);
    put_varint(output, value);
}

template <typename Enum>
void put_enum(std::vector<std::byte>& output, const std::uint32_t field, const Enum value) {
    put_uint(output, field, static_cast<std::underlying_type_t<Enum>>(value));
}

void put_bool(std::vector<std::byte>& output, const std::uint32_t field, const bool value) {
    if (value) put_uint(output, field, 1);
}

void put_float(std::vector<std::byte>& output, const std::uint32_t field, const float value) {
    if (value == 0.0F) return;
    put_key(output, field, wire_fixed32);
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned index = 0; index < sizeof(bits); ++index)
        output.push_back(static_cast<std::byte>((bits >> (index * 8)) & 0xFFU));
}

void put_string(
    std::vector<std::byte>& output, const std::uint32_t field, const std::string& value) {
    if (value.empty()) return;
    put_key(output, field, wire_length_delimited);
    put_varint(output, value.size());
    for (const auto character : value)
        output.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
}

void put_message(
    std::vector<std::byte>& output,
    const std::uint32_t field,
    const std::vector<std::byte>& value) {
    put_key(output, field, wire_length_delimited);
    put_varint(output, value.size());
    output.insert(output.end(), value.begin(), value.end());
}

std::vector<std::byte> encode_header(const MessageHeader& header) {
    std::vector<std::byte> output;
    put_uint(output, 1, header.protocol_version_value);
    put_uint(output, 2, header.sequence);
    put_uint(output, 3, header.timestamp_us);
    put_enum(output, 4, header.source_id);
    put_enum(output, 5, header.destination_id);
    put_uint(output, 6, header.asset_id);
    put_uint(output, 7, header.session_id);
    return output;
}

void reset_header(MessageHeader& header) {
    header = {};
    header.protocol_version_value = 0;
    header.asset_id = 0;
    header.session_id = 0;
}

bool decode_header(const std::span<const std::byte> bytes, MessageHeader& header) {
    reset_header(header);
    Reader reader(bytes);
    while (!reader.done()) {
        std::uint32_t field{};
        std::uint8_t type{};
        if (!reader.key(field, type)) return false;
        std::uint64_t value{};
        if (field >= 1 && field <= 7) {
            if (type != wire_varint || !reader.varint(value)) return false;
            switch (field) {
                case 1:
                    header.protocol_version_value = static_cast<std::uint32_t>(value);
                    break;
                case 2:
                    header.sequence = static_cast<std::uint32_t>(value);
                    break;
                case 3:
                    header.timestamp_us = value;
                    break;
                case 4:
                    header.source_id = static_cast<ComponentId>(value);
                    break;
                case 5:
                    header.destination_id = static_cast<ComponentId>(value);
                    break;
                case 6:
                    header.asset_id = value;
                    break;
                case 7:
                    header.session_id = value;
                    break;
                default:
                    break;
            }
        } else if (!reader.skip(type)) {
            return false;
        }
    }
    return true;
}

template <typename Message>
void put_header(std::vector<std::byte>& output, const Message& message) {
    put_message(output, 1, encode_header(message.header));
}

template <typename Message>
bool read_header_field(
    Reader& reader, const std::uint8_t type, Message& message) {
    if (type != wire_length_delimited) return reader.fail();
    std::span<const std::byte> nested;
    return reader.message(nested) && decode_header(nested, message.header);
}

std::vector<std::byte> encode_message(const AssetPose& message) {
    std::vector<std::byte> output;
    put_header(output, message);
    put_enum(output, 2, message.coordinate_frame);
    put_float(output, 3, message.x_m);
    put_float(output, 4, message.y_m);
    put_float(output, 5, message.z_m);
    put_float(output, 6, message.azimuth_deg);
    return output;
}

std::vector<std::byte> encode_message(const TargetCoordinate& message) {
    std::vector<std::byte> output;
    put_header(output, message);
    put_uint(output, 2, message.detection_id);
    put_uint(output, 3, message.measurement_time_us);
    put_enum(output, 4, message.coordinate_frame);
    put_float(output, 5, message.x_m);
    put_float(output, 6, message.y_m);
    put_float(output, 7, message.z_m);
    put_float(output, 8, message.confidence);
    return output;
}

std::vector<std::byte> encode_message(const ObservationStatus& message) {
    std::vector<std::byte> output;
    put_header(output, message);
    put_enum(output, 2, message.state);
    put_float(output, 3, message.current_pan_deg);
    put_float(output, 4, message.current_tilt_deg);
    put_bool(output, 5, message.lidar_active);
    put_bool(output, 6, message.turret_active);
    put_uint(output, 7, message.error_code);
    put_uint(output, 8, message.timestamp_us);
    return output;
}

std::vector<std::byte> encode_message(const ObservationTurretCommand& message) {
    std::vector<std::byte> output;
    put_header(output, message);
    put_uint(output, 2, message.command_id);
    put_enum(output, 3, message.command_type);
    put_float(output, 4, message.target_pan_deg);
    put_float(output, 5, message.target_tilt_deg);
    put_uint(output, 6, message.valid_until_us);
    return output;
}

std::vector<std::byte> encode_message(const EffectorTurretCommand& message) {
    std::vector<std::byte> output;
    put_header(output, message);
    put_uint(output, 2, message.command_id);
    put_uint(output, 3, message.target_id);
    put_float(output, 4, message.target_pan_deg);
    put_float(output, 5, message.target_tilt_deg);
    put_uint(output, 6, message.valid_until_us);
    return output;
}

std::vector<std::byte> encode_message(const AttackCommand& message) {
    std::vector<std::byte> output;
    put_header(output, message);
    put_uint(output, 2, message.command_id);
    put_uint(output, 3, message.target_id);
    put_enum(output, 4, message.action);
    put_uint(output, 5, message.duration_ms);
    put_uint(output, 6, message.valid_until_us);
    return output;
}

std::vector<std::byte> encode_message(const EffectorStatus& message) {
    std::vector<std::byte> output;
    put_header(output, message);
    put_enum(output, 2, message.state);
    put_float(output, 3, message.current_pan_deg);
    put_float(output, 4, message.current_tilt_deg);
    put_float(output, 5, message.target_pan_deg);
    put_float(output, 6, message.target_tilt_deg);
    put_bool(output, 7, message.aligned);
    put_bool(output, 8, message.attack_armed);
    put_bool(output, 9, message.attack_active);
    put_uint(output, 10, message.error_code);
    put_uint(output, 11, message.timestamp_us);
    return output;
}

std::vector<std::byte> encode_message(const CommandAck& message) {
    std::vector<std::byte> output;
    put_header(output, message);
    put_uint(output, 2, message.command_id);
    put_enum(output, 3, message.result);
    put_uint(output, 4, message.error_code);
    put_uint(output, 5, message.timestamp_us);
    return output;
}

std::vector<std::byte> encode_message(const Heartbeat& message) {
    std::vector<std::byte> output;
    put_header(output, message);
    put_enum(output, 2, message.state);
    put_uint(output, 3, message.uptime_ms);
    put_uint(output, 4, message.timestamp_us);
    return output;
}

std::vector<std::byte> encode_message(const ErrorReport& message) {
    std::vector<std::byte> output;
    put_header(output, message);
    put_uint(output, 2, message.error_code);
    put_enum(output, 3, message.severity);
    put_uint(output, 4, message.related_command_id);
    put_uint(output, 5, message.timestamp_us);
    put_string(output, 6, message.detail);
    return output;
}

std::vector<std::byte> encode_limits(const PanTiltLimits& limits) {
    std::vector<std::byte> output;
    put_float(output, 1, limits.minimum_pan_deg);
    put_float(output, 2, limits.maximum_pan_deg);
    put_float(output, 3, limits.minimum_tilt_deg);
    put_float(output, 4, limits.maximum_tilt_deg);
    return output;
}

std::vector<std::byte> encode_message(const AssetRegistration& message) {
    std::vector<std::byte> output;
    put_header(output, message);
    put_enum(output, 2, message.role);
    put_uint(output, 3, message.command_port);
    put_uint(output, 4, message.capabilities);
    put_string(output, 5, message.software_version);
    put_string(output, 6, message.hardware_version);
    put_message(output, 7, encode_limits(message.turret_limits));
    put_bool(output, 8, message.concurrent_tasks);
    put_uint(output, 9, message.lease_duration_ms);
    return output;
}

std::vector<std::byte> encode_message(const AssetUnregister& message) {
    std::vector<std::byte> output;
    put_header(output, message);
    put_string(output, 2, message.reason);
    return output;
}

template <typename Message, typename Handler>
bool decode_fields(const std::span<const std::byte> bytes, Message& message, Handler&& handler) {
    reset_header(message.header);
    Reader reader(bytes);
    while (!reader.done()) {
        std::uint32_t field{};
        std::uint8_t type{};
        if (!reader.key(field, type)) return false;
        if (field == 1) {
            if (!read_header_field(reader, type, message)) return false;
        } else if (!handler(reader, field, type, message) && !reader.skip(type)) {
            return false;
        }
    }
    return true;
}

bool read_uint(Reader& reader, const std::uint8_t type, std::uint64_t& value) {
    if (type != wire_varint) return reader.fail();
    return reader.varint(value);
}

bool read_float(Reader& reader, const std::uint8_t type, float& value) {
    if (type != wire_fixed32) return reader.fail();
    return reader.fixed32(value);
}

bool read_string(Reader& reader, const std::uint8_t type, std::string& value) {
    if (type != wire_length_delimited) return reader.fail();
    return reader.string(value);
}

bool decode_limits(const std::span<const std::byte> bytes, PanTiltLimits& limits) {
    Reader reader(bytes);
    while (!reader.done()) {
        std::uint32_t field{};
        std::uint8_t type{};
        if (!reader.key(field, type)) return false;
        switch (field) {
            case 1:
                if (!read_float(reader, type, limits.minimum_pan_deg)) return false;
                break;
            case 2:
                if (!read_float(reader, type, limits.maximum_pan_deg)) return false;
                break;
            case 3:
                if (!read_float(reader, type, limits.minimum_tilt_deg)) return false;
                break;
            case 4:
                if (!read_float(reader, type, limits.maximum_tilt_deg)) return false;
                break;
            default:
                if (!reader.skip(type)) return false;
                break;
        }
    }
    return true;
}

bool decode_message(const std::span<const std::byte> bytes, AssetPose& message) {
    message.coordinate_frame = CoordinateFrame::unspecified;
    return decode_fields(bytes, message, [](Reader& reader, const std::uint32_t field,
                                            const std::uint8_t type, AssetPose& value) {
        std::uint64_t integer{};
        switch (field) {
            case 2:
                if (!read_uint(reader, type, integer)) return false;
                value.coordinate_frame = static_cast<CoordinateFrame>(integer);
                return true;
            case 3:
                return read_float(reader, type, value.x_m);
            case 4:
                return read_float(reader, type, value.y_m);
            case 5:
                return read_float(reader, type, value.z_m);
            case 6:
                return read_float(reader, type, value.azimuth_deg);
            default:
                return false;
        }
    });
}

bool decode_message(const std::span<const std::byte> bytes, TargetCoordinate& message) {
    message.coordinate_frame = CoordinateFrame::unspecified;
    return decode_fields(bytes, message, [](Reader& reader, const std::uint32_t field,
                                            const std::uint8_t type, TargetCoordinate& value) {
        std::uint64_t integer{};
        switch (field) {
            case 2:
                if (!read_uint(reader, type, integer)) return false;
                value.detection_id = static_cast<std::uint32_t>(integer);
                return true;
            case 3:
                return read_uint(reader, type, value.measurement_time_us);
            case 4:
                if (!read_uint(reader, type, integer)) return false;
                value.coordinate_frame = static_cast<CoordinateFrame>(integer);
                return true;
            case 5:
                return read_float(reader, type, value.x_m);
            case 6:
                return read_float(reader, type, value.y_m);
            case 7:
                return read_float(reader, type, value.z_m);
            case 8:
                return read_float(reader, type, value.confidence);
            default:
                return false;
        }
    });
}

bool decode_message(const std::span<const std::byte> bytes, ObservationStatus& message) {
    return decode_fields(bytes, message, [](Reader& reader, const std::uint32_t field,
                                            const std::uint8_t type, ObservationStatus& value) {
        std::uint64_t integer{};
        switch (field) {
            case 2:
                if (!read_uint(reader, type, integer)) return false;
                value.state = static_cast<ObservationState>(integer);
                return true;
            case 3:
                return read_float(reader, type, value.current_pan_deg);
            case 4:
                return read_float(reader, type, value.current_tilt_deg);
            case 5:
                if (!read_uint(reader, type, integer)) return false;
                value.lidar_active = integer != 0;
                return true;
            case 6:
                if (!read_uint(reader, type, integer)) return false;
                value.turret_active = integer != 0;
                return true;
            case 7:
                if (!read_uint(reader, type, integer)) return false;
                value.error_code = static_cast<std::uint32_t>(integer);
                return true;
            case 8:
                return read_uint(reader, type, value.timestamp_us);
            default:
                return false;
        }
    });
}

bool decode_message(
    const std::span<const std::byte> bytes, ObservationTurretCommand& message) {
    return decode_fields(bytes, message, [](Reader& reader, const std::uint32_t field,
                                            const std::uint8_t type,
                                            ObservationTurretCommand& value) {
        std::uint64_t integer{};
        switch (field) {
            case 2:
                if (!read_uint(reader, type, integer)) return false;
                value.command_id = static_cast<std::uint32_t>(integer);
                return true;
            case 3:
                if (!read_uint(reader, type, integer)) return false;
                value.command_type = static_cast<ObservationTurretCommandType>(integer);
                return true;
            case 4:
                return read_float(reader, type, value.target_pan_deg);
            case 5:
                return read_float(reader, type, value.target_tilt_deg);
            case 6:
                return read_uint(reader, type, value.valid_until_us);
            default:
                return false;
        }
    });
}

bool decode_message(const std::span<const std::byte> bytes, EffectorTurretCommand& message) {
    return decode_fields(bytes, message, [](Reader& reader, const std::uint32_t field,
                                            const std::uint8_t type,
                                            EffectorTurretCommand& value) {
        std::uint64_t integer{};
        switch (field) {
            case 2:
                if (!read_uint(reader, type, integer)) return false;
                value.command_id = static_cast<std::uint32_t>(integer);
                return true;
            case 3:
                if (!read_uint(reader, type, integer)) return false;
                value.target_id = static_cast<std::uint32_t>(integer);
                return true;
            case 4:
                return read_float(reader, type, value.target_pan_deg);
            case 5:
                return read_float(reader, type, value.target_tilt_deg);
            case 6:
                return read_uint(reader, type, value.valid_until_us);
            default:
                return false;
        }
    });
}

bool decode_message(const std::span<const std::byte> bytes, AttackCommand& message) {
    return decode_fields(bytes, message, [](Reader& reader, const std::uint32_t field,
                                            const std::uint8_t type, AttackCommand& value) {
        std::uint64_t integer{};
        switch (field) {
            case 2:
                if (!read_uint(reader, type, integer)) return false;
                value.command_id = static_cast<std::uint32_t>(integer);
                return true;
            case 3:
                if (!read_uint(reader, type, integer)) return false;
                value.target_id = static_cast<std::uint32_t>(integer);
                return true;
            case 4:
                if (!read_uint(reader, type, integer)) return false;
                value.action = static_cast<AttackAction>(integer);
                return true;
            case 5:
                if (!read_uint(reader, type, integer)) return false;
                value.duration_ms = static_cast<std::uint32_t>(integer);
                return true;
            case 6:
                return read_uint(reader, type, value.valid_until_us);
            default:
                return false;
        }
    });
}

bool decode_message(const std::span<const std::byte> bytes, EffectorStatus& message) {
    return decode_fields(bytes, message, [](Reader& reader, const std::uint32_t field,
                                            const std::uint8_t type, EffectorStatus& value) {
        std::uint64_t integer{};
        switch (field) {
            case 2:
                if (!read_uint(reader, type, integer)) return false;
                value.state = static_cast<EffectorState>(integer);
                return true;
            case 3:
                return read_float(reader, type, value.current_pan_deg);
            case 4:
                return read_float(reader, type, value.current_tilt_deg);
            case 5:
                return read_float(reader, type, value.target_pan_deg);
            case 6:
                return read_float(reader, type, value.target_tilt_deg);
            case 7:
                if (!read_uint(reader, type, integer)) return false;
                value.aligned = integer != 0;
                return true;
            case 8:
                if (!read_uint(reader, type, integer)) return false;
                value.attack_armed = integer != 0;
                return true;
            case 9:
                if (!read_uint(reader, type, integer)) return false;
                value.attack_active = integer != 0;
                return true;
            case 10:
                if (!read_uint(reader, type, integer)) return false;
                value.error_code = static_cast<std::uint32_t>(integer);
                return true;
            case 11:
                return read_uint(reader, type, value.timestamp_us);
            default:
                return false;
        }
    });
}

bool decode_message(const std::span<const std::byte> bytes, CommandAck& message) {
    return decode_fields(bytes, message, [](Reader& reader, const std::uint32_t field,
                                            const std::uint8_t type, CommandAck& value) {
        std::uint64_t integer{};
        switch (field) {
            case 2:
                if (!read_uint(reader, type, integer)) return false;
                value.command_id = static_cast<std::uint32_t>(integer);
                return true;
            case 3:
                if (!read_uint(reader, type, integer)) return false;
                value.result = static_cast<CommandResult>(integer);
                return true;
            case 4:
                if (!read_uint(reader, type, integer)) return false;
                value.error_code = static_cast<std::uint32_t>(integer);
                return true;
            case 5:
                return read_uint(reader, type, value.timestamp_us);
            default:
                return false;
        }
    });
}

bool decode_message(const std::span<const std::byte> bytes, Heartbeat& message) {
    return decode_fields(bytes, message, [](Reader& reader, const std::uint32_t field,
                                            const std::uint8_t type, Heartbeat& value) {
        std::uint64_t integer{};
        switch (field) {
            case 2:
                if (!read_uint(reader, type, integer)) return false;
                value.state = static_cast<AssetOperatingState>(integer);
                return true;
            case 3:
                return read_uint(reader, type, value.uptime_ms);
            case 4:
                return read_uint(reader, type, value.timestamp_us);
            default:
                return false;
        }
    });
}

bool decode_message(const std::span<const std::byte> bytes, ErrorReport& message) {
    return decode_fields(bytes, message, [](Reader& reader, const std::uint32_t field,
                                            const std::uint8_t type, ErrorReport& value) {
        std::uint64_t integer{};
        switch (field) {
            case 2:
                if (!read_uint(reader, type, integer)) return false;
                value.error_code = static_cast<std::uint32_t>(integer);
                return true;
            case 3:
                if (!read_uint(reader, type, integer)) return false;
                value.severity = static_cast<ErrorSeverity>(integer);
                return true;
            case 4:
                if (!read_uint(reader, type, integer)) return false;
                value.related_command_id = static_cast<std::uint32_t>(integer);
                return true;
            case 5:
                return read_uint(reader, type, value.timestamp_us);
            case 6:
                return read_string(reader, type, value.detail);
            default:
                return false;
        }
    });
}

bool decode_message(const std::span<const std::byte> bytes, AssetRegistration& message) {
    message.role = AssetRole::unspecified;
    return decode_fields(bytes, message, [](Reader& reader, const std::uint32_t field,
                                            const std::uint8_t type,
                                            AssetRegistration& value) {
        std::uint64_t integer{};
        std::span<const std::byte> nested;
        switch (field) {
            case 2:
                if (!read_uint(reader, type, integer)) return false;
                value.role = static_cast<AssetRole>(integer);
                return true;
            case 3:
                if (!read_uint(reader, type, integer)) return false;
                value.command_port = static_cast<std::uint32_t>(integer);
                return true;
            case 4:
                return read_uint(reader, type, value.capabilities);
            case 5:
                return read_string(reader, type, value.software_version);
            case 6:
                return read_string(reader, type, value.hardware_version);
            case 7:
                if (type != wire_length_delimited || !reader.message(nested)) return false;
                return decode_limits(nested, value.turret_limits);
            case 8:
                if (!read_uint(reader, type, integer)) return false;
                value.concurrent_tasks = integer != 0;
                return true;
            case 9:
                return read_uint(reader, type, value.lease_duration_ms);
            default:
                return false;
        }
    });
}

bool decode_message(const std::span<const std::byte> bytes, AssetUnregister& message) {
    return decode_fields(bytes, message, [](Reader& reader, const std::uint32_t field,
                                            const std::uint8_t type,
                                            AssetUnregister& value) {
        if (field == 2) return read_string(reader, type, value.reason);
        return false;
    });
}

template <typename Message>
bool set_payload(const std::span<const std::byte> bytes, Envelope& envelope) {
    Message message;
    if (!decode_message(bytes, message)) return false;
    envelope.payload = std::move(message);
    return true;
}

bool decode_payload(
    const std::uint32_t field,
    const std::span<const std::byte> bytes,
    Envelope& envelope) {
    switch (static_cast<MessageKind>(field)) {
        case MessageKind::asset_pose:
            return set_payload<AssetPose>(bytes, envelope);
        case MessageKind::target_coordinate:
            return set_payload<TargetCoordinate>(bytes, envelope);
        case MessageKind::observation_status:
            return set_payload<ObservationStatus>(bytes, envelope);
        case MessageKind::observation_turret_command:
            return set_payload<ObservationTurretCommand>(bytes, envelope);
        case MessageKind::effector_turret_command:
            return set_payload<EffectorTurretCommand>(bytes, envelope);
        case MessageKind::attack_command:
            return set_payload<AttackCommand>(bytes, envelope);
        case MessageKind::effector_status:
            return set_payload<EffectorStatus>(bytes, envelope);
        case MessageKind::command_ack:
            return set_payload<CommandAck>(bytes, envelope);
        case MessageKind::heartbeat:
            return set_payload<Heartbeat>(bytes, envelope);
        case MessageKind::error_report:
            return set_payload<ErrorReport>(bytes, envelope);
        case MessageKind::asset_registration:
            return set_payload<AssetRegistration>(bytes, envelope);
        case MessageKind::asset_unregister:
            return set_payload<AssetUnregister>(bytes, envelope);
        case MessageKind::unspecified:
        default:
            return false;
    }
}
}  // namespace

std::vector<std::byte> encode(const Envelope& envelope) {
    if (!validate(envelope).valid()) throw std::invalid_argument("cannot encode invalid envelope");

    std::vector<std::byte> output;
    std::visit(
        [&](const auto& payload) {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (!std::is_same_v<Payload, std::monostate>)
                put_message(
                    output, static_cast<std::uint32_t>(message_kind(envelope)),
                    encode_message(payload));
        },
        envelope.payload);
    return output;
}

DecodeResult decode(const std::span<const std::byte> bytes) {
    Envelope envelope;
    Reader reader(bytes);
    while (!reader.done()) {
        std::uint32_t field{};
        std::uint8_t type{};
        if (!reader.key(field, type)) return DecodeError::malformed;
        if (field >= static_cast<std::uint32_t>(MessageKind::asset_pose) &&
            field <= static_cast<std::uint32_t>(MessageKind::asset_unregister)) {
            std::span<const std::byte> nested;
            if (type != wire_length_delimited || !reader.message(nested) ||
                !decode_payload(field, nested, envelope))
                return DecodeError::malformed;
        } else if (!reader.skip(type)) {
            return DecodeError::malformed;
        }
    }

    if (message_kind(envelope) == MessageKind::unspecified)
        return DecodeError::missing_payload;
    if (!validate(envelope).valid()) return DecodeError::invalid_message;
    return envelope;
}

std::string_view to_string(const DecodeError error) noexcept {
    switch (error) {
        case DecodeError::malformed:
            return "malformed protobuf payload";
        case DecodeError::missing_payload:
            return "envelope payload is missing";
        case DecodeError::invalid_message:
            return "decoded message violates the protocol contract";
    }
    return "unknown protobuf decode error";
}
}  // namespace c2::protobuf
