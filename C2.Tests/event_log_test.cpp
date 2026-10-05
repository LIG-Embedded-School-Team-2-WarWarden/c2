#include "pch.h"
#include <stdexcept>
#include "c2/event_log.hpp"
#include <filesystem>
#include <chrono>
#include <sstream>

namespace {
struct LogDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("c2-event-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    LogDirectory() { std::filesystem::create_directory(path); }
    ~LogDirectory() { std::filesystem::remove_all(path); }
};
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream data; data << input.rdbuf(); return data.str();
}
}
TEST(EventLogTest, EscapesDetailsAppendsAcrossRestartsAndBoundsRotation) {
    LogDirectory directory;
    const auto path = directory.path / "events.jsonl";
    const c2::RuntimeEvent event{"send_uncertain", 100, 201, 9, 42, "quote\" slash\\ newline\n"};
    {
        c2::EventLog log({path.string(), 1024, 2});
        log.append(event);
        EXPECT_EQ(log.stats().written, 1);
    }
    const auto first = read(path);
    EXPECT_NE(first.find("\\u000a"), std::string::npos);
    EXPECT_NE(first.find("\"asset_id\":201"), std::string::npos);
    {
        c2::EventLog log({path.string(), 1024, 2});
        log.append(event);
        EXPECT_EQ(read(path).substr(0, first.size()), first);
        for (int i = 0; i < 32; ++i) log.append(event);
        EXPECT_GT(log.stats().rotations, 0);
        EXPECT_EQ(log.stats().write_errors, 0);
    }
    for (const auto& file : std::filesystem::directory_iterator(directory.path))
        EXPECT_LE(std::filesystem::file_size(file), 1024);
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".3"));
}
TEST(EventLogTest, RejectsInvalidConfigurationAndCountsOversizedRecords) {
    EXPECT_THROW((void)c2::EventLog({"", 1, 1}), std::invalid_argument);
    EXPECT_THROW((void)c2::EventLog({"", 1024, 0}), std::invalid_argument);
    LogDirectory directory;
    c2::EventLog log({(directory.path / "events.jsonl").string(), 1024, 1});
    EXPECT_NO_THROW(log.append({"oversized", 1, 0, 0, 0, std::string(4096, 'x')}));
    EXPECT_EQ(log.stats().write_errors, 1);
    EXPECT_EQ(log.stats().written, 0);
}
