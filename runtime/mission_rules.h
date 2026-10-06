/* Host-side mission judging; no guest memory or rendering dependencies. */
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define MISSION_MAX_GATES 32

typedef struct { float x, y, z; } MissionPosition;
typedef struct {
    MissionPosition centre;
    float nx, nz, half_width, half_height;
} MissionGate;
typedef enum {
    MISSION_OFF, MISSION_ARMED, MISSION_LOADING, MISSION_COUNTDOWN,
    MISSION_ROLLING, MISSION_RUNNING, MISSION_PASSED, MISSION_TIME_FAILED,
    MISSION_CONTACT_FAILED, MISSION_SETUP_FAILED, MISSION_RECOVERY_FAILED, MISSION_GATE_FAILED, MISSION_OBJECTIVE_FAILED
} MissionPhase;
typedef struct {
    MissionPhase phase;
    MissionGate gates[MISSION_MAX_GATES];
    unsigned gate_count, next_gate, contacts, contact_limit;
    uint32_t required_pass_mask; /* Passing these planes outside their gate fails. */
    uint32_t met_mask;
    int any_order;
    uint64_t started, last_clear, last_sample, limit_ms, elapsed_ms;
    int touching;
    MissionPosition previous;
} MissionRun;

/* Return the intersection fraction, or -1. Only forward, bounded crossings
 * count. Swept segments catch gates passed between two simulation updates. */
static float mission_crossing(MissionGate g, MissionPosition a, MissionPosition b) {
    float da = (a.x-g.centre.x)*g.nx + (a.z-g.centre.z)*g.nz;
    float db = (b.x-g.centre.x)*g.nx + (b.z-g.centre.z)*g.nz;
    if (!(da < 0 && db >= 0)) return -1;
    float t = -da / (db-da);
    float x = a.x + (b.x-a.x)*t - g.centre.x;
    float z = a.z + (b.z-a.z)*t - g.centre.z;
    float y = a.y + (b.y-a.y)*t - g.centre.y;
    if (!isfinite(t) || !isfinite(x) || !isfinite(y) || !isfinite(z) ||
        fabsf(x*g.nz-z*g.nx) > g.half_width || fabsf(y) > g.half_height) return -1;
    return t;
}

static void mission_rules_step(MissionRun *run, MissionPosition p, int contact, uint64_t ms) {
    if (run->phase != MISSION_RUNNING) return;
    if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z) || ms < run->started) {
        run->phase = MISSION_SETUP_FAILED; return;
    }
    uint64_t previous_ms = run->last_sample ? run->last_sample : run->started;
    float travel = hypotf(p.x-run->previous.x, p.z-run->previous.z);
    /* Native recovery must not sweep through gates as if it were driving. */
    if (ms < previous_ms || travel > fmaxf(10, (ms-previous_ms)*.12f)) {
        run->phase = MISSION_RECOVERY_FAILED; return;
    }
    run->last_sample = ms;
    run->elapsed_ms = ms-run->started;
    /* A contact ends only after 200 ms continuously clear: scrapes do not
     * become dozens of collisions when the native flag flickers. */
    if (contact) {
        if (!run->touching) run->contacts++;
        run->touching = 1; run->last_clear = ms;
    } else if (run->touching && ms-run->last_clear >= 200) run->touching = 0;
    if (run->contacts > run->contact_limit) {
        run->phase = MISSION_CONTACT_FAILED; return;
    }
    float previous_t = -1;
    if(run->any_order) {
        for(unsigned i=0;i<run->gate_count;i++) {
            if(run->met_mask & (UINT32_C(1)<<i)) continue;
            MissionGate g=run->gates[i];
            float t=mission_crossing(g,run->previous,p);
            if(t<0) { g.nx=-g.nx; g.nz=-g.nz; t=mission_crossing(g,run->previous,p); }
            if(t>=0) { run->met_mask|=UINT32_C(1)<<i; run->next_gate++; }
        }
    }
    while (!run->any_order && run->next_gate < run->gate_count) {
        MissionGate g=run->gates[run->next_gate];
        float t = mission_crossing(g, run->previous, p);
        if (t < 0 || t <= previous_t) {
            float before=(run->previous.x-g.centre.x)*g.nx+(run->previous.z-g.centre.z)*g.nz;
            float after=(p.x-g.centre.x)*g.nx+(p.z-g.centre.z)*g.nz;
            MissionGate road=g; road.half_width=24;
            if ((run->required_pass_mask & (UINT32_C(1)<<run->next_gate)) &&
                before<0 && after>=0 && mission_crossing(road,run->previous,p)>=0)
                run->phase=MISSION_GATE_FAILED;
            break;
        }
        previous_t = t; run->next_gate++;
    }
    if (run->phase==MISSION_GATE_FAILED) { run->previous=p; return; }
    /* Exact-limit finishes succeed; late finishes do not. Collision failure
     * wins when a contact and the final crossing occur on the same tick. */
    if (run->limit_ms && run->elapsed_ms > run->limit_ms) run->phase = MISSION_TIME_FAILED;
    else if (run->gate_count && run->next_gate == run->gate_count) run->phase = MISSION_PASSED;
    run->previous = p;
}
