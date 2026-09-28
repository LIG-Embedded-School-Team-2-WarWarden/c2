#include "pch.h"

#include "c2/server_console.hpp"

#include <variant>

namespace {
const c2::ConsoleCommand& parsed(const std::string_view text) {
    static c2::ConsoleParseResult result;
    result = c2::parse_console_command(text);
    EXPECT_TRUE(std::holds_alternative<c2::ConsoleCommand>(result));
    return std::get<c2::ConsoleCommand>(result);
}

TEST(ServerConsoleTest, ParsesAssetTrackAndStatusQueries) {
    EXPECT_EQ(parsed("assets").kind, c2::ConsoleCommandKind::assets);
    EXPECT_EQ(parsed("targets").kind, c2::ConsoleCommandKind::targets);
    EXPECT_EQ(parsed("status").kind, c2::ConsoleCommandKind::status);
    EXPECT_EQ(parsed("errors").kind, c2::ConsoleCommandKind::errors);
    EXPECT_EQ(parsed("quit").kind, c2::ConsoleCommandKind::quit);
}

TEST(ServerConsoleTest, ParsesObservationCommandsWithAssetIdentity) {
    const auto scan = parsed("scan 101 12.5 -3");
    EXPECT_EQ(scan.kind, c2::ConsoleCommandKind::scan);
    EXPECT_EQ(scan.asset_id, 101U);
    EXPECT_FLOAT_EQ(scan.pan_deg, 12.5F);
    EXPECT_FLOAT_EQ(scan.tilt_deg, -3.0F);
    EXPECT_EQ(parsed("observe 102 4 5").kind,
              c2::ConsoleCommandKind::observe);
    EXPECT_EQ(parsed("obs-stop 103").kind,
              c2::ConsoleCommandKind::observation_stop);
    EXPECT_EQ(parsed("obs-home 104").kind,
              c2::ConsoleCommandKind::observation_home);
}

TEST(ServerConsoleTest, ParsesAssignmentAndEffectorCommands) {
    const auto automatic = parsed("assign 9000000000");
    EXPECT_EQ(automatic.kind, c2::ConsoleCommandKind::assign);
    EXPECT_EQ(automatic.track_id, 9'000'000'000ULL);
    EXPECT_FALSE(automatic.effector_asset_id.has_value());
    const auto manual = parsed("assign 7 202");
    ASSERT_TRUE(manual.effector_asset_id.has_value());
    EXPECT_EQ(*manual.effector_asset_id, 202U);
    EXPECT_EQ(parsed("unassign 7").kind, c2::ConsoleCommandKind::unassign);
    EXPECT_EQ(parsed("point 7").kind, c2::ConsoleCommandKind::point);
    EXPECT_EQ(parsed("arm 7").kind, c2::ConsoleCommandKind::arm);
    const auto start = parsed("start 7 500");
    EXPECT_EQ(start.kind, c2::ConsoleCommandKind::start);
    EXPECT_EQ(start.duration_ms, 500U);
    EXPECT_EQ(parsed("stop 201").kind, c2::ConsoleCommandKind::stop);
    EXPECT_EQ(parsed("estop 201").kind, c2::ConsoleCommandKind::emergency_stop);
    EXPECT_EQ(parsed("estop-all").kind,
              c2::ConsoleCommandKind::emergency_stop_all);
}

TEST(ServerConsoleTest, PreservesLegacySingleAssetSyntaxDuringMigration) {
    const auto scan = parsed("scan 10 5");
    EXPECT_EQ(scan.kind, c2::ConsoleCommandKind::scan);
    EXPECT_EQ(scan.asset_id, 0U);
    EXPECT_FLOAT_EQ(scan.pan_deg, 10.0F);
    EXPECT_FLOAT_EQ(scan.tilt_deg, 5.0F);
    EXPECT_EQ(parsed("obs-stop").asset_id, 0U);
    EXPECT_EQ(parsed("obs-home").asset_id, 0U);
    EXPECT_EQ(parsed("stop").asset_id, 0U);
    EXPECT_EQ(parsed("estop").asset_id, 0U);
}

TEST(ServerConsoleTest, RejectsMissingExtraMalformedAndZeroArguments) {
    for (const auto text : {"", "unknown", "assets extra", "scan 1 2",
                            "scan 0 1 2", "assign 0", "assign 1 0",
                            "start 1 0", "stop", "estop-all 1"}) {
        SCOPED_TRACE(text);
        EXPECT_TRUE(std::holds_alternative<c2::ConsoleParseError>(
            c2::parse_console_command(text)));
    }
}
}  // namespace
