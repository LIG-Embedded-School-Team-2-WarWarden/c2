#include "c2/protocol_validation.hpp"

#include <cmath>

namespace c2 {
namespace {
void append(ValidationResult& destination, ValidationResult source) {
    destination.errors.insert(destination.errors.end(), source.errors.begin(), source.errors.end());
}
bool finite(const float value) noexcept { return std::isfinite(value); }
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
}  // namespace c2
