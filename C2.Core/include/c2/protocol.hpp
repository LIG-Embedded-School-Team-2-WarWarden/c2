#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace c2 {
inline constexpr std::uint32_t protocol_version = 3;

enum class ComponentId : std::uint32_t {
    unspecified = 0,
    observation_asset = 1,
    command_and_control = 2,
    effector_asset = 3,
};

enum class CoordinateFrame : std::uint32_t { unspecified = 0, project_frame = 1 };

enum class ObservationTurretCommandType : std::uint32_t {
    unspecified = 0,
    home = 1,
    stop = 2,
    absolute_angle = 3,
    scan = 4,
};

enum class ObservationState : std::uint32_t {
    unspecified = 0,
    off = 1,
    initializing = 2,
    standby = 3,
    operating = 4,
    fault = 5,
};

enum class EffectorState : std::uint32_t {
    unspecified = 0,
    off = 1,
    initializing = 2,
    standby = 3,
    slewing = 4,
    ready = 5,
    active = 6,
    fault = 7,
};

enum class AttackAction : std::uint32_t {
    unspecified = 0,
    arm = 1,
    start = 2,
    stop = 3,
    emergency_stop = 4,
};

enum class CommandResult : std::uint32_t {
    unspecified = 0,
    received = 1,
    accepted = 2,
    in_progress = 3,
    completed = 4,
    rejected = 5,
    failed = 6,
};

enum class AssetOperatingState : std::uint32_t {
    unspecified = 0,
    off = 1,
    initializing = 2,
    standby = 3,
    operating = 4,
    slewing = 5,
    ready = 6,
    active = 7,
    fault = 8,
};

enum class ErrorSeverity : std::uint32_t {
    unspecified = 0,
    info = 1,
    warning = 2,
    error = 3,
    critical = 4,
};

enum class AssetRole : std::uint32_t {
    unspecified = 0,
    observation = 1,
    effector = 2,
};

enum class TrackingStopReason : std::uint32_t {
    none = 0,
    operator_stop = 1,
    emergency_stop = 2,
    target_expired = 3,
    prediction_timeout = 4,
    outside_turret_limits = 5,
    pose_unavailable = 6,
    invalid_target = 7,
    track_mismatch = 8,
    target_lost = 9,
    actuator_fault = 10,
    communication_timeout = 11,
    following_error = 12,
    session_replaced = 13,
};

namespace capability {
inline constexpr std::uint64_t observation_scan = 1ULL << 0U;
inline constexpr std::uint64_t effector_point = 1ULL << 1U;
inline constexpr std::uint64_t effector_attack = 1ULL << 2U;
}  // namespace capability

enum class MessageKind : std::uint32_t {
    unspecified = 0,
    asset_pose = 1,
    target_coordinate = 2,
    observation_status = 3,
    observation_turret_command = 4,
    effector_turret_command = 5,
    attack_command = 6,
    effector_status = 7,
    command_ack = 8,
    heartbeat = 9,
    error_report = 10,
    asset_registration = 11,
    asset_unregister = 12,
    target_track_update = 13,
};

struct MessageHeader {
    std::uint32_t protocol_version_value{protocol_version};
    std::uint32_t sequence{};
    std::uint64_t timestamp_us{};
    ComponentId source_id{ComponentId::unspecified};
    ComponentId destination_id{ComponentId::unspecified};
    std::uint64_t asset_id{1};
    std::uint64_t session_id{1};
};

struct PanTiltLimits {
    float minimum_pan_deg{};
    float maximum_pan_deg{};
    float minimum_tilt_deg{};
    float maximum_tilt_deg{};
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
    float vx_mps{};
    float vy_mps{};
    float vz_mps{};
    bool velocity_valid{};
};

// C2 forwards a validated global track only to its assigned effector.  This is
// a state-stream update, not an attack command and therefore has no CommandAck.
struct TargetTrackUpdate {
    MessageHeader header;
    std::uint64_t track_id{};
    CoordinateFrame coordinate_frame{CoordinateFrame::project_frame};
    float x_m{};
    float y_m{};
    float z_m{};
    float vx_mps{};
    float vy_mps{};
    float vz_mps{};
    bool velocity_valid{};
    std::uint64_t measurement_time_us{};
    std::uint64_t valid_until_us{};
    float confidence{};
    std::uint64_t observation_asset_id{};
    std::uint64_t observation_session_id{};
};

struct ObservationStatus {
    MessageHeader header;
    ObservationState state{ObservationState::unspecified};
    float current_pan_deg{};
    float current_tilt_deg{};
    bool lidar_active{};
    bool turret_active{};
    std::uint32_t error_code{};
    std::uint64_t timestamp_us{};
};

[[nodiscard]] constexpr bool is_scanning(const ObservationStatus& status) noexcept {
    return status.state == ObservationState::operating && status.lidar_active;
}

struct ObservationTurretCommand {
    MessageHeader header;
    std::uint32_t command_id{};
    ObservationTurretCommandType command_type{ObservationTurretCommandType::unspecified};
    float target_pan_deg{};
    float target_tilt_deg{};
    std::uint64_t valid_until_us{};
};

struct EffectorTurretCommand {
    MessageHeader header;
    std::uint32_t command_id{};
    std::uint64_t target_id{};
    float target_pan_deg{};
    float target_tilt_deg{};
    std::uint64_t valid_until_us{};
};

struct AttackCommand {
    MessageHeader header;
    std::uint32_t command_id{};
    std::uint64_t target_id{};
    AttackAction action{AttackAction::unspecified};
    std::uint32_t duration_ms{};
    std::uint64_t valid_until_us{};
};

struct EffectorStatus {
    MessageHeader header;
    EffectorState state{EffectorState::unspecified};
    float current_pan_deg{};
    float current_tilt_deg{};
    float target_pan_deg{};
    float target_tilt_deg{};
    bool aligned{};
    bool attack_armed{};
    bool attack_active{};
    std::uint32_t error_code{};
    std::uint64_t timestamp_us{};
    std::uint64_t tracking_track_id{};
    bool automatic_tracking_active{};
    std::uint64_t last_target_measurement_time_us{};
    std::uint64_t target_freshness_us{};
    TrackingStopReason tracking_stop_reason{TrackingStopReason::none};
    float predicted_x_m{};
    float predicted_y_m{};
    float predicted_z_m{};
};

struct CommandAck {
    MessageHeader header;
    std::uint32_t command_id{};
    CommandResult result{CommandResult::unspecified};
    std::uint32_t error_code{};
    std::uint64_t timestamp_us{};
};

struct Heartbeat {
    MessageHeader header;
    AssetOperatingState state{AssetOperatingState::unspecified};
    std::uint64_t uptime_ms{};
    std::uint64_t timestamp_us{};
};

struct ErrorReport {
    MessageHeader header;
    std::uint32_t error_code{};
    ErrorSeverity severity{ErrorSeverity::unspecified};
    std::uint32_t related_command_id{};
    std::uint64_t timestamp_us{};
    std::string detail;
};

struct AssetRegistration {
    MessageHeader header;
    AssetRole role{AssetRole::unspecified};
    std::uint32_t command_port{};
    std::uint64_t capabilities{};
    std::string software_version;
    std::string hardware_version;
    PanTiltLimits turret_limits;
    bool concurrent_tasks{};
    std::uint64_t lease_duration_ms{};
};

struct AssetUnregister {
    MessageHeader header;
    std::string reason;
};

using MessagePayload = std::variant<
    std::monostate,
    AssetPose,
    TargetCoordinate,
    ObservationStatus,
    ObservationTurretCommand,
    EffectorTurretCommand,
    AttackCommand,
    EffectorStatus,
    CommandAck,
    Heartbeat,
    ErrorReport,
    AssetRegistration,
    AssetUnregister,
    TargetTrackUpdate>;

struct Envelope {
    MessagePayload payload;
};

[[nodiscard]] constexpr MessageKind message_kind(const Envelope& envelope) noexcept {
    return static_cast<MessageKind>(envelope.payload.index());
}
}  // namespace c2
