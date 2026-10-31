#pragma once

// Motor coordinates keep north at 180 degrees. Only the GUI boundary uses
// north = 0; retain the motor's direction of increasing azimuth.
constexpr int fcs_motor_to_gui_azimuth(int motor_degrees) {
    const int shifted = (motor_degrees % 360 - 180) % 360;
    return shifted < 0 ? shifted + 360 : shifted;
}

static_assert(fcs_motor_to_gui_azimuth(180) == 0);
static_assert(fcs_motor_to_gui_azimuth(184) == 4);
static_assert(fcs_motor_to_gui_azimuth(120) == 300);
static_assert(fcs_motor_to_gui_azimuth(179) == 359);
static_assert(fcs_motor_to_gui_azimuth(0) == 180);
static_assert(fcs_motor_to_gui_azimuth(360) == 180);
