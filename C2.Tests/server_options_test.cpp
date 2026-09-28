#include "pch.h"

#include "c2/server_options.hpp"

#include <string_view>
#include <vector>

TEST(ServerOptionsTest, ParsesAndPropagatesDynamicRuntimeLimits) {
    const std::vector<std::string_view> arguments{
        "--asset-port", "5500",
        "--target-validity-ms", "3000",
        "--max-targets", "120",
        "--max-tracks-per-observer", "30",
        "--max-assets", "40",
        "--heartbeat-timeout-ms", "4500",
        "--retired-retention-ms", "70000",
        "--max-registration-lease-ms", "10000",
        "--status-timeout-ms", "1500",
        "--ack-timeout-ms", "250",
        "--completion-timeout-ms", "5000",
        "--command-attempts", "5",
        "--max-pending-commands", "200",
        "--max-pending-per-asset", "20",
        "--max-command-outcomes", "400",
        "--max-assignments", "100",
        "--max-error-history", "50",
        "--max-inbound-rejections", "75",
        "--distance-weight", "2.5",
        "--rotation-weight", "3",
        "--assignment-weight", "125",
        "--degraded-penalty", "750",
        "--failure-weight", "40",
        "--auto-reassign", "false"};

    const auto options = c2::parse_server_options(arguments);
    const auto runtime = c2::make_server_runtime_config(options);
    EXPECT_EQ(options.asset_port, 5500U);
    EXPECT_EQ(runtime.state.target_validity_us, 3'000'000U);
    EXPECT_EQ(runtime.tracks.validity_us, 3'000'000U);
    EXPECT_EQ(runtime.state.maximum_targets, 120U);
    EXPECT_EQ(runtime.tracks.maximum_tracks, 120U);
    EXPECT_EQ(runtime.tracks.maximum_tracks_per_observer, 30U);
    EXPECT_EQ(runtime.registry.maximum_assets, 40U);
    EXPECT_EQ(runtime.connections.heartbeat_timeout_us, 4'500'000U);
    EXPECT_EQ(runtime.registry.heartbeat_timeout_us, 4'500'000U);
    EXPECT_EQ(runtime.registry.retired_retention_us, 70'000'000U);
    EXPECT_EQ(runtime.registry.maximum_lease_duration_ms, 10'000U);
    EXPECT_EQ(runtime.registry.status_timeout_us, 1'500'000U);
    EXPECT_EQ(runtime.command_ack_timeout_us, 250'000U);
    EXPECT_EQ(runtime.commands.delivery_ack_timeout_us, 250'000U);
    EXPECT_EQ(runtime.commands.completion_timeout_us, 5'000'000U);
    EXPECT_EQ(runtime.command_max_attempts, 5U);
    EXPECT_EQ(runtime.commands.maximum_attempts, 5U);
    EXPECT_EQ(runtime.commands.maximum_pending, 200U);
    EXPECT_EQ(runtime.commands.maximum_pending_per_asset, 20U);
    EXPECT_EQ(runtime.commands.maximum_outcomes, 400U);
    EXPECT_EQ(runtime.assignments.maximum_assignments, 100U);
    EXPECT_EQ(runtime.maximum_error_history, 50U);
    EXPECT_EQ(runtime.maximum_inbound_rejections, 75U);
    EXPECT_DOUBLE_EQ(runtime.assignments.weights.distance_weight, 2.5);
    EXPECT_DOUBLE_EQ(runtime.assignments.weights.rotation_weight, 3.0);
    EXPECT_DOUBLE_EQ(runtime.assignments.weights.assignment_weight, 125.0);
    EXPECT_DOUBLE_EQ(runtime.assignments.weights.degraded_penalty, 750.0);
    EXPECT_DOUBLE_EQ(runtime.assignments.weights.failure_weight, 40.0);
    EXPECT_FALSE(runtime.assignments.auto_reassignment_enabled);
}

TEST(ServerOptionsTest, KeepsDocumentedDefaultsConsistentAcrossLegacyAndDynamicPaths) {
    const auto options = c2::parse_server_options({});
    const auto runtime = c2::make_server_runtime_config(options);
    EXPECT_EQ(runtime.state.target_validity_us, runtime.tracks.validity_us);
    EXPECT_EQ(runtime.state.maximum_targets, runtime.tracks.maximum_tracks);
    EXPECT_EQ(runtime.connections.heartbeat_timeout_us,
              runtime.registry.heartbeat_timeout_us);
    EXPECT_EQ(runtime.command_ack_timeout_us,
              runtime.commands.delivery_ack_timeout_us);
    EXPECT_EQ(runtime.command_max_attempts,
              runtime.commands.maximum_attempts);
}

TEST(ServerOptionsTest, RejectsUnknownMissingZeroOverflowAndContradictoryValues) {
    for (const std::vector<std::string_view> arguments : {
             std::vector<std::string_view>{"--unknown", "1"},
             {"--asset-port"},
             {"--asset-port", "0"},
             {"--target-validity-ms", "18446744073709552"},
             {"--distance-weight", "-1"},
             {"--rotation-weight", "nan"},
             {"--auto-reassign", "sometimes"},
             {"--max-targets", "10", "--max-tracks-per-observer", "11"},
             {"--max-pending-commands", "10", "--max-pending-per-asset", "11"}}) {
        EXPECT_THROW((void)c2::parse_server_options(arguments),
                     std::invalid_argument);
    }
}
