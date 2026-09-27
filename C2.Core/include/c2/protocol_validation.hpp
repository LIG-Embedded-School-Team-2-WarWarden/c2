#pragma once

#include "c2/protocol.hpp"

#include <string>
#include <vector>

namespace c2 {
struct ValidationResult {
    std::vector<std::string> errors;
    [[nodiscard]] bool valid() const noexcept { return errors.empty(); }
};

struct ObservationTurretLimits {
    float minimum_pan_deg{};
    float maximum_pan_deg{};
    float minimum_tilt_deg{};
    float maximum_tilt_deg{};
};

[[nodiscard]] ValidationResult validate_header(
    const MessageHeader& header, ComponentId expected_source, ComponentId expected_destination);
[[nodiscard]] ValidationResult validate(const AssetPose& pose);
[[nodiscard]] ValidationResult validate(const TargetCoordinate& target);
[[nodiscard]] ValidationResult validate(const ObservationStatus& status);
[[nodiscard]] ValidationResult validate(const ObservationTurretCommand& command);
[[nodiscard]] ValidationResult validate(
    const ObservationTurretCommand& command, const ObservationTurretLimits& limits);
[[nodiscard]] ValidationResult validate(const EffectorTurretCommand& command);
[[nodiscard]] ValidationResult validate(const AttackCommand& command);
[[nodiscard]] ValidationResult validate(const EffectorStatus& status);
[[nodiscard]] ValidationResult validate(const CommandAck& acknowledgement);
[[nodiscard]] ValidationResult validate(const Heartbeat& heartbeat);
[[nodiscard]] ValidationResult validate(const ErrorReport& report);
[[nodiscard]] ValidationResult validate(const AssetRegistration& registration);
[[nodiscard]] ValidationResult validate(const AssetUnregister& unregister_message);
[[nodiscard]] ValidationResult validate(const Envelope& envelope);
}  // namespace c2
