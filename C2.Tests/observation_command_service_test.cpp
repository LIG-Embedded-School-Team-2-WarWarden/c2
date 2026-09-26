#include "pch.h"

#include "c2/observation_command_service.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {
c2::ObservationCommandService service(
    const std::uint32_t first_command_id = 10,
    const std::uint32_t first_sequence = 20) {
    return c2::ObservationCommandService(
        {{-90.0F, 90.0F, -20.0F, 45.0F}, 500, first_command_id,
         first_sequence});
}

TEST(ObservationCommandServiceTest, CreatesScanCommandWithRouteAnglesAndDeadline) {
    auto commands = service();
    const auto command = commands.create(
        c2::ObservationTurretCommandType::scan, 30.0F, 15.0F, 1'000);

    EXPECT_EQ(command.header.protocol_version_value, c2::protocol_version);
    EXPECT_EQ(command.header.sequence, 20U);
    EXPECT_EQ(command.header.timestamp_us, 1'000U);
    EXPECT_EQ(command.header.source_id, c2::ComponentId::command_and_control);
    EXPECT_EQ(command.header.destination_id, c2::ComponentId::observation_asset);
    EXPECT_EQ(command.command_id, 10U);
    EXPECT_EQ(command.command_type, c2::ObservationTurretCommandType::scan);
    EXPECT_FLOAT_EQ(command.target_pan_deg, 30.0F);
    EXPECT_FLOAT_EQ(command.target_tilt_deg, 15.0F);
    EXPECT_EQ(command.valid_until_us, 1'500U);
    EXPECT_TRUE(c2::validate(command, {-90.0F, 90.0F, -20.0F, 45.0F}).valid());
}

TEST(ObservationCommandServiceTest, CreatesAbsoluteHomeAndStopCommands) {
    auto commands = service();
    const auto point = commands.create(
        c2::ObservationTurretCommandType::absolute_angle, -90.0F, 45.0F, 100);
    const auto home = commands.create(c2::ObservationTurretCommandType::home, 99.0F, 99.0F, 200);
    const auto stop = commands.create(c2::ObservationTurretCommandType::stop, 99.0F, 99.0F, 300);

    EXPECT_EQ(point.command_id, 10U);
    EXPECT_FLOAT_EQ(point.target_pan_deg, -90.0F);
    EXPECT_FLOAT_EQ(point.target_tilt_deg, 45.0F);
    EXPECT_EQ(home.command_id, 11U);
    EXPECT_FLOAT_EQ(home.target_pan_deg, 0.0F);
    EXPECT_FLOAT_EQ(home.target_tilt_deg, 0.0F);
    EXPECT_EQ(stop.command_id, 12U);
    EXPECT_FLOAT_EQ(stop.target_pan_deg, 0.0F);
    EXPECT_FLOAT_EQ(stop.target_tilt_deg, 0.0F);
    EXPECT_EQ(stop.header.sequence, 22U);
}

TEST(ObservationCommandServiceTest, RejectsUnsupportedAndOutOfRangeTargetCommands) {
    auto commands = service();
    EXPECT_THROW(
        (void)commands.create(c2::ObservationTurretCommandType::unspecified, 0.0F, 0.0F, 100),
        std::invalid_argument);
    EXPECT_THROW(
        (void)commands.create(c2::ObservationTurretCommandType::scan, 90.1F, 0.0F, 100),
        std::out_of_range);
    EXPECT_THROW(
        (void)commands.create(c2::ObservationTurretCommandType::absolute_angle, 0.0F, -20.1F, 100),
        std::out_of_range);

    const auto first = commands.create(c2::ObservationTurretCommandType::scan, 0.0F, 0.0F, 100);
    EXPECT_EQ(first.command_id, 10U);
    EXPECT_EQ(first.header.sequence, 20U);
}

TEST(ObservationCommandServiceTest, RejectsInvalidTimeWithoutConsumingIdentifiers) {
    auto commands = service();
    EXPECT_THROW(
        (void)commands.create(c2::ObservationTurretCommandType::home, 0.0F, 0.0F, 0),
        std::invalid_argument);
    EXPECT_THROW(
        (void)commands.create(
            c2::ObservationTurretCommandType::home, 0.0F, 0.0F,
            std::numeric_limits<std::uint64_t>::max() - 499),
        std::overflow_error);

    const auto first = commands.create(c2::ObservationTurretCommandType::home, 0.0F, 0.0F, 100);
    EXPECT_EQ(first.command_id, 10U);
    EXPECT_EQ(first.header.sequence, 20U);
}

TEST(ObservationCommandServiceTest, WrapsIdentifiersWhileSkippingReservedZero) {
    auto commands = service(
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max());
    const auto last = commands.create(c2::ObservationTurretCommandType::home, 0.0F, 0.0F, 100);
    const auto wrapped = commands.create(c2::ObservationTurretCommandType::stop, 0.0F, 0.0F, 200);

    EXPECT_EQ(last.command_id, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(last.header.sequence, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(wrapped.command_id, 1U);
    EXPECT_EQ(wrapped.header.sequence, 1U);
}

TEST(ObservationCommandServiceTest, RejectsInvalidConfiguration) {
    EXPECT_THROW(
        (void)c2::ObservationCommandService({{-91.0F, 90.0F, -20.0F, 45.0F}, 0, 1, 1}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)c2::ObservationCommandService({{90.0F, -90.0F, -20.0F, 45.0F}, 1, 1, 1}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)c2::ObservationCommandService({{-90.0F, 90.0F, -20.0F, 45.0F}, 1, 0, 1}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)c2::ObservationCommandService({{-90.0F, 90.0F, -20.0F, 45.0F}, 1, 1, 0}),
        std::invalid_argument);
}
}  // namespace
