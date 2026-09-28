#include "c2/server_options.hpp"

#include <charconv>
#include <chrono>
#include <limits>
#include <stdexcept>

namespace c2 {
namespace {
template <typename Value>
Value positive_number(const std::string_view text, const char* name) {
    Value value{};
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        value == 0)
        throw std::invalid_argument(std::string{"invalid "} + name);
    return value;
}

std::uint16_t port(const std::string_view text) {
    const auto value = positive_number<unsigned>(text, "UDP port");
    if (value > std::numeric_limits<std::uint16_t>::max())
        throw std::invalid_argument("invalid UDP port");
    return static_cast<std::uint16_t>(value);
}

std::uint64_t interval_ms(const std::string_view text, const char* name) {
    const auto value = positive_number<std::uint64_t>(text, name);
    if (value > static_cast<std::uint64_t>(
                    std::numeric_limits<std::chrono::milliseconds::rep>::max()))
        throw std::invalid_argument(std::string{name} + " is too large");
    return value;
}

std::uint64_t microseconds(
    const std::uint64_t milliseconds, const char* name) {
    if (milliseconds > std::numeric_limits<std::uint64_t>::max() / 1000U)
        throw std::invalid_argument(std::string{name} + " is too large");
    return milliseconds * 1000U;
}
}  // namespace

ServerOptions parse_server_options(
    const std::span<const std::string_view> arguments) {
    ServerOptions options;
    for (std::size_t index = 0; index < arguments.size(); index += 2) {
        if (index + 1 >= arguments.size())
            throw std::invalid_argument("option value is missing");
        const auto name = arguments[index];
        const auto value = arguments[index + 1];
        if (name == "--bind") options.bind_address = value;
        else if (name == "--observation-ip") options.observation_address = value;
        else if (name == "--effector-ip") options.effector_address = value;
        else if (name == "--observation-status-port") options.observation_status_port = port(value);
        else if (name == "--target-port") options.target_port = port(value);
        else if (name == "--observation-command-port") options.observation_command_port = port(value);
        else if (name == "--effector-command-port") options.effector_command_port = port(value);
        else if (name == "--effector-status-port") options.effector_status_port = port(value);
        else if (name == "--asset-port") options.asset_port = port(value);
        else if (name == "--target-validity-ms") options.target_validity_ms = positive_number<std::uint64_t>(value, "target validity");
        else if (name == "--max-targets") options.maximum_targets = positive_number<std::size_t>(value, "maximum target count");
        else if (name == "--max-tracks-per-observer") options.maximum_tracks_per_observer = positive_number<std::size_t>(value, "per-observer track count");
        else if (name == "--max-assets") options.maximum_assets = positive_number<std::size_t>(value, "maximum asset count");
        else if (name == "--heartbeat-interval-ms") options.heartbeat_interval_ms = interval_ms(value, "heartbeat interval");
        else if (name == "--heartbeat-timeout-ms") options.heartbeat_timeout_ms = positive_number<std::uint64_t>(value, "heartbeat timeout");
        else if (name == "--retired-retention-ms") options.retired_retention_ms = positive_number<std::uint64_t>(value, "retired retention");
        else if (name == "--status-timeout-ms") options.status_timeout_ms = positive_number<std::uint64_t>(value, "status timeout");
        else if (name == "--command-validity-ms") options.command_validity_ms = positive_number<std::uint64_t>(value, "command validity");
        else if (name == "--ack-timeout-ms") options.acknowledgement_timeout_ms = positive_number<std::uint64_t>(value, "acknowledgement timeout");
        else if (name == "--completion-timeout-ms") options.completion_timeout_ms = positive_number<std::uint64_t>(value, "completion timeout");
        else if (name == "--command-attempts") options.command_attempts = positive_number<std::uint32_t>(value, "command attempts");
        else if (name == "--max-pending-commands") options.maximum_pending_commands = positive_number<std::size_t>(value, "maximum pending commands");
        else if (name == "--max-pending-per-asset") options.maximum_pending_per_asset = positive_number<std::size_t>(value, "per-asset pending commands");
        else if (name == "--max-command-outcomes") options.maximum_command_outcomes = positive_number<std::size_t>(value, "maximum command outcomes");
        else if (name == "--max-assignments") options.maximum_assignments = positive_number<std::size_t>(value, "maximum assignments");
        else if (name == "--emergency-stop-repetitions") options.emergency_stop_repetitions = positive_number<std::uint32_t>(value, "emergency stop repetitions");
        else throw std::invalid_argument("unknown option: " + std::string{name});
    }
    if (options.maximum_tracks_per_observer > options.maximum_targets)
        throw std::invalid_argument("per-observer track limit exceeds total track limit");
    if (options.maximum_pending_per_asset > options.maximum_pending_commands)
        throw std::invalid_argument("per-asset command limit exceeds total command limit");
    (void)microseconds(options.target_validity_ms, "target validity");
    (void)microseconds(options.heartbeat_timeout_ms, "heartbeat timeout");
    (void)microseconds(options.retired_retention_ms, "retired retention");
    (void)microseconds(options.status_timeout_ms, "status timeout");
    (void)microseconds(options.command_validity_ms, "command validity");
    (void)microseconds(options.acknowledgement_timeout_ms, "acknowledgement timeout");
    (void)microseconds(options.completion_timeout_ms, "completion timeout");
    return options;
}

ServerRuntimeConfig make_server_runtime_config(const ServerOptions& options) {
    const auto target_validity = microseconds(
        options.target_validity_ms, "target validity");
    const auto heartbeat_timeout = microseconds(
        options.heartbeat_timeout_ms, "heartbeat timeout");
    const auto command_validity = microseconds(
        options.command_validity_ms, "command validity");
    const auto acknowledgement_timeout = microseconds(
        options.acknowledgement_timeout_ms, "acknowledgement timeout");
    ServerRuntimeConfig config;
    config.state = {target_validity, options.maximum_targets};
    config.connections = {heartbeat_timeout};
    config.observation_commands = {
        {-180, 180, -90, 90}, command_validity, 1, 1};
    config.effector_commands = {
        {-180, 180, -90, 90}, command_validity, 1, 1};
    config.attack_commands = {command_validity, 1, 1};
    config.observation_endpoint = {
        options.observation_address, options.observation_command_port};
    config.effector_endpoint = {
        options.effector_address, options.effector_command_port};
    config.emergency_stop_repetitions = options.emergency_stop_repetitions;
    config.command_ack_timeout_us = acknowledgement_timeout;
    config.command_max_attempts = options.command_attempts;
    config.registry = {
        options.maximum_assets, heartbeat_timeout,
        microseconds(options.retired_retention_ms, "retired retention"),
        microseconds(options.status_timeout_ms, "status timeout")};
    config.tracks = {target_validity, options.maximum_targets,
                     options.maximum_tracks_per_observer, 1};
    config.commands = {
        acknowledgement_timeout,
        microseconds(options.completion_timeout_ms, "completion timeout"),
        options.command_attempts, options.maximum_pending_commands,
        options.maximum_pending_per_asset, options.maximum_command_outcomes};
    config.assignments.maximum_assignments = options.maximum_assignments;
    return config;
}
}  // namespace c2
