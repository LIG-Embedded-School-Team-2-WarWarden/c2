#pragma once
#include "gui/ipc_protocol.hpp"

// Recovery changes logical availability only; it never sends movement or laser commands.
struct AssignmentHealth {
    unsigned state{}, resume{};
    c2::gui::Result reason{c2::gui::Result::success};
    void reset() { state=resume=0; reason=c2::gui::Result::success; }
    void start() { state=resume=1; reason=c2::gui::Result::success; }
    void fail(c2::gui::Result why) { state=3; reason=why; }
    void update(bool current, bool healthy, bool present, bool same_target,
                bool aligned, bool moving, bool demo_active, bool expired) {
        using R=c2::gui::Result;
        if (!state || state==3) return;
        if (current && !healthy) { fail(R::device_fault); return; }
        if (present && !same_target) { fail(R::target_changed); return; }
        if ((state==1 || (state==4 && resume==1)) && expired) {
            fail(R::alignment_required); return;
        }
        if (!current || !present) {
            if (state!=4) resume=state;
            state=4; reason=!current?R::status_pending:R::target_pending; return;
        }
        if (state==4) state=resume;
        reason=R::success;
        if (state==1 && aligned && !moving) state=2;
        else if (state==2 && !demo_active && (!aligned || moving)) fail(R::alignment_required);
    }
};
