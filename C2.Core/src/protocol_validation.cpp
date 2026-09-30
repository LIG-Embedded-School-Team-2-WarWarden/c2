#include "c2/protocol_validation.hpp"

#include <cmath>
#include <type_traits>
#include <variant>

namespace c2 {
namespace {
void append(ValidationResult& destination, ValidationResult source) {
    destination.errors.insert(destination.errors.end(), source.errors.begin(), source.errors.end());
}
bool finite(const float value) noexcept { return std::isfinite(value); }

bool supported(const ObservationTurretCommandType type) noexcept {
    switch (type) {
        case ObservationTurretCommandType::home:
        case ObservationTurretCommandType::stop:
        case ObservationTurretCommandType::absolute_angle:
        case ObservationTurretCommandType::scan:
            return true;
        case ObservationTurretCommandType::unspecified:
        default:
            return false;
    }
}

bool uses_target_angles(const ObservationTurretCommandType type) noexcept {
    return type == ObservationTurretCommandType::absolute_angle ||
           type == ObservationTurretCommandType::scan;
}

bool supported(const ObservationState state) noexcept {
    switch (state) {
        case ObservationState::off:
        case ObservationState::initializing:
        case ObservationState::standby:
        case ObservationState::operating:
        case ObservationState::fault:
            return true;
        case ObservationState::unspecified:
        default:
            return false;
    }
}

bool supported(const EffectorState state) noexcept {
    switch (state) {
        case EffectorState::off:
        case EffectorState::initializing:
        case EffectorState::standby:
        case EffectorState::slewing:
        case EffectorState::ready:
        case EffectorState::active:
        case EffectorState::fault:
            return true;
        case EffectorState::unspecified:
        default:
            return false;
    }
}

bool supported(const AttackAction action) noexcept {
    switch (action) {
        case AttackAction::arm:
        case AttackAction::start:
        case AttackAction::stop:
        case AttackAction::emergency_stop:
            return true;
        case AttackAction::unspecified:
        default:
            return false;
    }
}

bool supported(const CommandResult result) noexcept {
    switch (result) {
        case CommandResult::received:
        case CommandResult::accepted:
        case CommandResult::in_progress:
        case CommandResult::completed:
        case CommandResult::rejected:
        case CommandResult::failed:
            return true;
        case CommandResult::unspecified:
        default:
            return false;
    }
}

bool supported(const AssetOperatingState state) noexcept {
    switch (state) {
        case AssetOperatingState::off:
        case AssetOperatingState::initializing:
        case AssetOperatingState::standby:
        case AssetOperatingState::operating:
        case AssetOperatingState::slewing:
        case AssetOperatingState::ready:
        case AssetOperatingState::active:
        case AssetOperatingState::fault:
            return true;
        case AssetOperatingState::unspecified:
        default:
            return false;
    }
}

bool supported(const ErrorSeverity severity) noexcept {
    switch (severity) {
        case ErrorSeverity::info:
        case ErrorSeverity::warning:
        case ErrorSeverity::error:
        case ErrorSeverity::critical:
            return true;
        case ErrorSeverity::unspecified:
        default:
            return false;
    }
}

bool supported(const AssetRole role) noexcept {
    return role == AssetRole::observation || role == AssetRole::effector;
}

ValidationResult validate_asset_message_header(
    const MessageHeader& header, const char* message_name) {
    ValidationResult result;
    if (header.source_id != ComponentId::observation_asset &&
        header.source_id != ComponentId::effector_asset) {
        result.errors.emplace_back(std::string(message_name) + " source must be an asset");
        return result;
    }
    append(result, validate_header(
                       header, header.source_id, ComponentId::command_and_control));
    return result;
}
}  // namespace

ValidationResult validate_header(
    const MessageHeader& header,
    const ComponentId expected_source,
    const ComponentId expected_destination) {
    ValidationResult result;
    if (header.protocol_version_value != protocol_version) result.errors.emplace_back("unsupported protocol_version");
    if (header.sequence == 0) result.errors.emplace_back("sequence must be non-zero");
    if (header.timestamp_us == 0) result.errors.emplace_back("timestamp_us must be non-zero");
    if (header.source_id != expected_source) result.errors.emplace_back("unexpected source_id");
    if (header.destination_id != expected_destination) result.errors.emplace_back("unexpected destination_id");
    if (header.asset_id == 0) result.errors.emplace_back("asset_id must be non-zero");
    if (header.session_id == 0) result.errors.emplace_back("session_id must be non-zero");
    return result;
}

ValidationResult validate(const AssetPose& pose) {
    const auto source = pose.header.source_id;
    ValidationResult result;
    if (source != ComponentId::observation_asset && source != ComponentId::effector_asset)
        result.errors.emplace_back("AssetPose source must be an observation or effector asset");
    else
        append(result, validate_header(pose.header, source, ComponentId::command_and_control));
    if (pose.coordinate_frame != CoordinateFrame::project_frame)
        result.errors.emplace_back("AssetPose must use PROJECT_FRAME");
    if (!finite(pose.x_m) || !finite(pose.y_m) || !finite(pose.z_m))
        result.errors.emplace_back("AssetPose position must be finite");
    if (!finite(pose.azimuth_deg) || pose.azimuth_deg < 0.0F || pose.azimuth_deg >= 360.0F)
        result.errors.emplace_back("azimuth_deg must be in [0, 360)");
    return result;
}

ValidationResult validate(const TargetCoordinate& target) {
    ValidationResult result = validate_header(
        target.header, ComponentId::observation_asset, ComponentId::command_and_control);
    if (target.detection_id == 0) result.errors.emplace_back("detection_id must be non-zero");
    if (target.measurement_time_us == 0) result.errors.emplace_back("measurement_time_us must be non-zero");
    if (target.coordinate_frame != CoordinateFrame::project_frame)
        result.errors.emplace_back("TargetCoordinate must use PROJECT_FRAME world coordinates");
    if (!finite(target.x_m) || !finite(target.y_m) || !finite(target.z_m))
        result.errors.emplace_back("TargetCoordinate position must be finite");
    if (!finite(target.confidence) || target.confidence < 0.0F || target.confidence > 1.0F)
        result.errors.emplace_back("confidence must be in [0, 1]");
    if (!finite(target.vx_mps) || !finite(target.vy_mps) || !finite(target.vz_mps))
        result.errors.emplace_back("TargetCoordinate velocity must be finite");
    return result;
}

bool supported(const TrackingStopReason reason) noexcept {
    switch (reason) {
        case TrackingStopReason::none:
        case TrackingStopReason::operator_stop:
        case TrackingStopReason::emergency_stop:
        case TrackingStopReason::target_expired:
        case TrackingStopReason::prediction_timeout:
        case TrackingStopReason::outside_turret_limits:
        case TrackingStopReason::pose_unavailable:
        case TrackingStopReason::invalid_target:
        case TrackingStopReason::track_mismatch:
        case TrackingStopReason::target_lost:
        case TrackingStopReason::actuator_fault:
        case TrackingStopReason::communication_timeout:
        case TrackingStopReason::following_error:
        case TrackingStopReason::session_replaced:
            return true;
        default:
            return false;
    }
}

ValidationResult validate(const TargetTrackUpdate& target) {
    ValidationResult result = validate_header(
        target.header, ComponentId::command_and_control, ComponentId::effector_asset);
    if (target.track_id == 0) result.errors.emplace_back("track_id must be non-zero");
    if (target.coordinate_frame != CoordinateFrame::project_frame)
        result.errors.emplace_back("TargetTrackUpdate must use PROJECT_FRAME");
    if (!finite(target.x_m) || !finite(target.y_m) || !finite(target.z_m))
        result.errors.emplace_back("TargetTrackUpdate position must be finite");
    if (!target.velocity_valid)
        result.errors.emplace_back("TargetTrackUpdate requires valid velocity");
    if (!finite(target.vx_mps) || !finite(target.vy_mps) || !finite(target.vz_mps))
        result.errors.emplace_back("TargetTrackUpdate velocity must be finite");
    if (target.measurement_time_us == 0)
        result.errors.emplace_back("measurement_time_us must be non-zero");
    if (target.valid_until_us <= target.measurement_time_us)
        result.errors.emplace_back("valid_until_us must follow measurement_time_us");
    if (!finite(target.confidence) || target.confidence < 0.0F || target.confidence > 1.0F)
        result.errors.emplace_back("confidence must be in [0, 1]");
    if (target.observation_asset_id == 0 || target.observation_session_id == 0)
        result.errors.emplace_back("observation identity must be non-zero");
    return result;
}

ValidationResult validate(const ObservationStatus& status) {
    ValidationResult result = validate_header(
        status.header, ComponentId::observation_asset, ComponentId::command_and_control);
    if (!supported(status.state)) result.errors.emplace_back("unsupported observation state");
    if (!finite(status.current_pan_deg) || !finite(status.current_tilt_deg))
        result.errors.emplace_back("current angles must be finite");
    if (status.timestamp_us == 0)
        result.errors.emplace_back("status timestamp_us must be non-zero");
    return result;
}

ValidationResult validate(const ObservationTurretCommand& command) {
    ValidationResult result = validate_header(
        command.header, ComponentId::command_and_control, ComponentId::observation_asset);
    if (command.command_id == 0) result.errors.emplace_back("command_id must be non-zero");
    if (!supported(command.command_type))
        result.errors.emplace_back("command_type must be HOME, STOP, ABSOLUTE_ANGLE, or SCAN");
    if (!finite(command.target_pan_deg) || !finite(command.target_tilt_deg))
        result.errors.emplace_back("target angles must be finite");
    if (command.valid_until_us <= command.header.timestamp_us)
        result.errors.emplace_back("valid_until_us must be later than timestamp_us");
    return result;
}

ValidationResult validate(
    const ObservationTurretCommand& command, const ObservationTurretLimits& limits) {
    ValidationResult result = validate(command);
    const auto valid_limits = finite(limits.minimum_pan_deg) && finite(limits.maximum_pan_deg) &&
                              finite(limits.minimum_tilt_deg) &&
                              finite(limits.maximum_tilt_deg) &&
                              limits.minimum_pan_deg <= limits.maximum_pan_deg &&
                              limits.minimum_tilt_deg <= limits.maximum_tilt_deg;
    if (!valid_limits) {
        result.errors.emplace_back("observation turret limits are invalid");
        return result;
    }

    if (uses_target_angles(command.command_type) && finite(command.target_pan_deg) &&
        finite(command.target_tilt_deg) &&
        (command.target_pan_deg < limits.minimum_pan_deg ||
         command.target_pan_deg > limits.maximum_pan_deg ||
         command.target_tilt_deg < limits.minimum_tilt_deg ||
         command.target_tilt_deg > limits.maximum_tilt_deg))
        result.errors.emplace_back("target angles exceed observation turret limits");
    return result;
}

ValidationResult validate(const EffectorTurretCommand& command) {
    ValidationResult result = validate_header(
        command.header, ComponentId::command_and_control, ComponentId::effector_asset);
    if (command.command_id == 0) result.errors.emplace_back("command_id must be non-zero");
    if (command.target_id == 0) result.errors.emplace_back("target_id must be non-zero");
    if (!finite(command.target_pan_deg) || !finite(command.target_tilt_deg))
        result.errors.emplace_back("target angles must be finite");
    if (command.valid_until_us <= command.header.timestamp_us)
        result.errors.emplace_back("valid_until_us must be later than timestamp_us");
    return result;
}

ValidationResult validate(const AttackCommand& command) {
    ValidationResult result = validate_header(
        command.header, ComponentId::command_and_control, ComponentId::effector_asset);
    if (command.command_id == 0) result.errors.emplace_back("command_id must be non-zero");
    if (!supported(command.action)) result.errors.emplace_back("unsupported attack action");
    if ((command.action == AttackAction::arm || command.action == AttackAction::start) &&
        command.target_id == 0)
        result.errors.emplace_back("ARM and START require target_id");
    if (command.action == AttackAction::start && command.duration_ms == 0)
        result.errors.emplace_back("START requires non-zero duration_ms");
    if (command.valid_until_us <= command.header.timestamp_us)
        result.errors.emplace_back("valid_until_us must be later than timestamp_us");
    return result;
}

ValidationResult validate(const EffectorStatus& status) {
    ValidationResult result = validate_header(
        status.header, ComponentId::effector_asset, ComponentId::command_and_control);
    if (!supported(status.state)) result.errors.emplace_back("unsupported effector state");
    if (!supported(status.tracking_stop_reason))
        result.errors.emplace_back("unsupported tracking stop reason");
    if (!finite(status.current_pan_deg) || !finite(status.current_tilt_deg) ||
        !finite(status.target_pan_deg) || !finite(status.target_tilt_deg) ||
        !finite(status.predicted_x_m) || !finite(status.predicted_y_m) ||
        !finite(status.predicted_z_m))
        result.errors.emplace_back("effector angles must be finite");
    if (status.timestamp_us == 0)
        result.errors.emplace_back("status timestamp_us must be non-zero");
    return result;
}

ValidationResult validate(const CommandAck& acknowledgement) {
    ValidationResult result =
        validate_asset_message_header(acknowledgement.header, "CommandAck");
    if (acknowledgement.command_id == 0)
        result.errors.emplace_back("command_id must be non-zero");
    if (!supported(acknowledgement.result))
        result.errors.emplace_back("unsupported command result");
    if (acknowledgement.timestamp_us == 0)
        result.errors.emplace_back("ack timestamp_us must be non-zero");
    return result;
}

ValidationResult validate(const Heartbeat& heartbeat) {
    ValidationResult result;
    if (heartbeat.header.source_id == ComponentId::command_and_control &&
        (heartbeat.header.destination_id == ComponentId::observation_asset ||
         heartbeat.header.destination_id == ComponentId::effector_asset)) {
        append(result, validate_header(
                           heartbeat.header, ComponentId::command_and_control,
                           heartbeat.header.destination_id));
    } else if (heartbeat.header.source_id == ComponentId::observation_asset ||
               heartbeat.header.source_id == ComponentId::effector_asset) {
        append(result, validate_header(
                           heartbeat.header, heartbeat.header.source_id,
                           ComponentId::command_and_control));
    } else {
        result.errors.emplace_back("Heartbeat route is unsupported");
    }
    if (!supported(heartbeat.state))
        result.errors.emplace_back("unsupported operating state");
    if (heartbeat.timestamp_us == 0)
        result.errors.emplace_back("heartbeat timestamp_us must be non-zero");
    return result;
}

ValidationResult validate(const ErrorReport& report) {
    ValidationResult result = validate_asset_message_header(report.header, "ErrorReport");
    if (report.error_code == 0) result.errors.emplace_back("error_code must be non-zero");
    if (!supported(report.severity)) result.errors.emplace_back("unsupported error severity");
    if (report.timestamp_us == 0)
        result.errors.emplace_back("error timestamp_us must be non-zero");
    return result;
}

ValidationResult validate(const AssetRegistration& registration) {
    ValidationResult result = validate_asset_message_header(
        registration.header, "AssetRegistration");
    if (!supported(registration.role))
        result.errors.emplace_back("unsupported asset role");
    const auto expected_source = registration.role == AssetRole::observation
        ? ComponentId::observation_asset
        : ComponentId::effector_asset;
    if (supported(registration.role) && registration.header.source_id != expected_source)
        result.errors.emplace_back("asset role does not match source_id");
    if (registration.command_port == 0 || registration.command_port > 65'535)
        result.errors.emplace_back("command_port must be in [1, 65535]");
    if (registration.capabilities == 0)
        result.errors.emplace_back("capabilities must be non-zero");
    if (registration.software_version.empty() && registration.hardware_version.empty())
        result.errors.emplace_back("software or hardware version must be provided");
    const auto& limits = registration.turret_limits;
    if (!finite(limits.minimum_pan_deg) || !finite(limits.maximum_pan_deg) ||
        !finite(limits.minimum_tilt_deg) || !finite(limits.maximum_tilt_deg) ||
        limits.minimum_pan_deg > limits.maximum_pan_deg ||
        limits.minimum_tilt_deg > limits.maximum_tilt_deg)
        result.errors.emplace_back("turret limits are invalid");
    if (registration.lease_duration_ms == 0)
        result.errors.emplace_back("lease_duration_ms must be non-zero");
    return result;
}

ValidationResult validate(const AssetUnregister& unregister_message) {
    return validate_asset_message_header(
        unregister_message.header, "AssetUnregister");
}

ValidationResult validate(const Envelope& envelope) {
    return std::visit(
        [](const auto& payload) -> ValidationResult {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, std::monostate>)
                return ValidationResult{{"envelope payload must be set"}};
            else
                return validate(payload);
        },
        envelope.payload);
}
}  // namespace c2
