#pragma once

#include <cstdint>

namespace c2 {
inline constexpr std::uint32_t protocol_version = 1;

enum class ComponentId : std::uint32_t {
    unspecified = 0,
    observation_asset = 1,
    command_and_control = 2,
    effector_asset = 3,
};

enum class CoordinateFrame : std::uint32_t { unspecified = 0, project_frame = 1 };

struct MessageHeader {
    std::uint32_t protocol_version_value{protocol_version};
    std::uint32_t sequence{};
    std::uint64_t timestamp_us{};
    ComponentId source_id{ComponentId::unspecified};
    ComponentId destination_id{ComponentId::unspecified};
};

struct AssetPose {
    MessageHeader header;
    CoordinateFrame coordinate_frame{CoordinateFrame::project_frame};
    float x_m{};
    float y_m{};
    float z_m{};
    float azimuth_deg{};
};

struct TargetCoordinate {
    MessageHeader header;
    std::uint32_t detection_id{};
    std::uint64_t measurement_time_us{};
    CoordinateFrame coordinate_frame{CoordinateFrame::project_frame};
    float x_m{};
    float y_m{};
    float z_m{};
    float confidence{};
};

struct EffectorTurretCommand {
    MessageHeader header;
    std::uint32_t command_id{};
    std::uint32_t target_id{};
    float target_pan_deg{};
    float target_tilt_deg{};
    std::uint64_t valid_until_us{};
};
}  // namespace c2
