#include "../runtime/mission_rules.h"
#include <assert.h>
#include <stdio.h>
#include <limits.h>
static MissionRun run(unsigned contacts) {
    MissionRun r={0}; r.phase=MISSION_RUNNING; r.gate_count=2;
    r.gates[0]=(MissionGate){{10,0,0},1,0,3,2,0};
    r.gates[1]=(MissionGate){{20,0,0},1,0,3,2,0};
    r.previous=(MissionPosition){0,0,0}; r.started=1000;
    r.limit_ms=1000; r.contact_limit=contacts; return r;
}
int main(void) {
    MissionRun unordered=run(UINT_MAX);
    unordered.any_order=1; unordered.limit_ms=0;
    unordered.previous=(MissionPosition){25,0,0};
    mission_rules_step(&unordered,(MissionPosition){15,0,0},0,1100);
    assert(unordered.next_gate==1 && unordered.met_mask==2 && unordered.phase==MISSION_RUNNING);
    mission_rules_step(&unordered,(MissionPosition){25,0,0},0,1200);
    assert(unordered.next_gate==1); /* Crossing the same bridge twice is not another target. */
    mission_rules_step(&unordered,(MissionPosition){5,0,0},0,1400);
    assert(unordered.next_gate==2 && unordered.met_mask==3 && unordered.phase==MISSION_PASSED);
    /* Failure gates beat the success gate, including a reverse crossing. */
    MissionRun failure=run(UINT_MAX);failure.failure_gate_count=1;
    failure.failure_gates[0]=(MissionGate){{15,0,0},1,0,3,2,0};
    mission_rules_step(&failure,(MissionPosition){25,0,0},0,2000);
    assert(failure.phase==MISSION_OBJECTIVE_FAILED);
    failure=run(UINT_MAX);failure.previous=(MissionPosition){25,0,0};
    failure.failure_gate_count=1;failure.failure_gates[0]=(MissionGate){{15,0,0},1,0,3,2,0};
    mission_rules_step(&failure,(MissionPosition){5,0,0},0,2000);
    assert(failure.phase==MISSION_OBJECTIVE_FAILED);
    MissionRun r=run(0);
    mission_rules_step(&r,(MissionPosition){25,0,0},0,2000);
    assert(r.phase==MISSION_PASSED && r.next_gate==2 && r.elapsed_ms==1000);
    r=run(0); mission_rules_step(&r,(MissionPosition){25,0,0},0,2001);
    assert(r.phase==MISSION_TIME_FAILED);
    r=run(0); mission_rules_step(&r,(MissionPosition){25,0,0},1,1500);
    assert(r.phase==MISSION_CONTACT_FAILED && !r.next_gate);
    r=run(UINT_MAX); mission_rules_step(&r,(MissionPosition){5,0,0},1,1100);
    mission_rules_step(&r,(MissionPosition){6,0,0},1,1200); assert(r.contacts==1);
    mission_rules_step(&r,(MissionPosition){6,0,0},0,1300);
    mission_rules_step(&r,(MissionPosition){6,0,0},1,1350); assert(r.contacts==1);
    mission_rules_step(&r,(MissionPosition){6,0,0},0,1550);
    mission_rules_step(&r,(MissionPosition){6,0,0},1,1600); assert(r.contacts==2);
    r=run(0); mission_rules_step(&r,(MissionPosition){25,9,0},0,1500);
    assert(r.phase==MISSION_RUNNING && !r.next_gate); /* stacked road */
    r=run(0); mission_rules_step(&r,(MissionPosition){25,0,10},0,1500);
    assert(!r.next_gate); /* bypassing section */
    r=run(0); r.previous=(MissionPosition){25,0,0};
    mission_rules_step(&r,(MissionPosition){0,0,0},0,1500); assert(!r.next_gate);
    mission_rules_step(&r,(MissionPosition){25,0,0},0,1800); assert(r.phase==MISSION_PASSED);
    r=run(0); r.gates[0].centre.x=20; r.gates[1].centre.x=10;
    mission_rules_step(&r,(MissionPosition){25,0,0},0,1600); assert(r.next_gate==1);
    r=run(0); mission_rules_step(&r,(MissionPosition){NAN,0,0},0,1500);
    assert(r.phase==MISSION_SETUP_FAILED);
    r=run(0); mission_rules_step(&r,(MissionPosition){25,0,0},0,1034);
    assert(r.phase==MISSION_RECOVERY_FAILED); /* recovery is not driving through gates */
    r=run(UINT_MAX); r.required_pass_mask=3; r.previous.z=8;
    mission_rules_step(&r,(MissionPosition){15,0,8},0,1500);
    assert(r.phase==MISSION_GATE_FAILED && r.next_gate==0);
    r=run(UINT_MAX); r.required_pass_mask=3;
    mission_rules_step(&r,(MissionPosition){15,0,0},0,1400);
    assert(r.phase==MISSION_RUNNING && r.next_gate==1);
    mission_rules_step(&r,(MissionPosition){25,0,10},0,1800);
    assert(r.phase==MISSION_GATE_FAILED && r.next_gate==1); /* Second stop bypass. */
    r=run(UINT_MAX); r.required_pass_mask=3;
    mission_rules_step(&r,(MissionPosition){25,0,0},0,1800);
    assert(r.phase==MISSION_PASSED); /* Both correct in one swept update. */
    r=run(UINT_MAX); r.required_pass_mask=3; r.previous=(MissionPosition){15,0,8};
    mission_rules_step(&r,(MissionPosition){5,0,8},0,1500);
    assert(r.phase==MISSION_RUNNING); /* Reverse movement is not passing the stop. */
    r=run(UINT_MAX); r.required_pass_mask=3; r.previous=(MissionPosition){5,0,100};
    mission_rules_step(&r,(MissionPosition){15,0,100},0,1500);
    assert(r.phase==MISSION_RUNNING && r.next_gate==0); /* Distant road crosses extended plane. */
    r=run(UINT_MAX); r.required_pass_mask=3; r.previous=(MissionPosition){5,20,0};
    mission_rules_step(&r,(MissionPosition){15,20,0},0,1500);
    assert(r.phase==MISSION_RUNNING && r.next_gate==0); /* Other road level. */
    /* M2's authored finish overlaps the body, but not its road-level origin. */
    MissionRun m2={0};m2.phase=MISSION_RUNNING;m2.gate_count=1;m2.contact_limit=UINT_MAX;
    m2.started=m2.last_sample=1000;m2.vehicle_height=2;
    m2.gates[0]=(MissionGate){{724,11,913},.0185f,.9998f,9.225f,4.965f,-.04363323f};
    m2.previous=(MissionPosition){724,5.5f,908};
    mission_rules_step(&m2,(MissionPosition){724,5.5f,918},0,1500);
    assert(m2.phase==MISSION_PASSED);
    MissionGate flat={{0,0,0},1,0,3,2,1.57079632679f};
    assert(mission_crossing(flat,(MissionPosition){0,-2,0},(MissionPosition){0,2,0})>=0);
    assert(mission_crossing(flat,(MissionPosition){4,-2,0},(MissionPosition){4,2,0})<0);
    puts("mission judging: swept gates, direction, bounds, ordering, exact deadline and contact debounce passed");
}
