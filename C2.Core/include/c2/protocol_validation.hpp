#pragma once

#include "c2/protocol.hpp"

#include <string>
#include <vector>

namespace c2 {
struct ValidationResult {
    std::vector<std::string> errors;
    [[nodiscard]] bool valid() const noexcept { return errors.empty(); }
};

[[nodiscard]] ValidationResult validate_header(
    const MessageHeader& header, ComponentId expected_source, ComponentId expected_destination);
[[nodiscard]] ValidationResult validate(const AssetPose& pose);
[[nodiscard]] ValidationResult validate(const TargetCoordinate& target);
[[nodiscard]] ValidationResult validate(const EffectorTurretCommand& command);
}  // namespace c2
