/* Tiny event scheduler in virtual CPU cycles. */
#include "runtime.h"

#define MAX_EVENTS 256

typedef struct Event { uint64_t when; SchedCb cb; void *arg; } Event;
static Event g_ev[MAX_EVENTS];
static int g_nev;

void rt_sched_at(uint64_t cycle, SchedCb cb, void *arg) {
    if (g_nev >= MAX_EVENTS) rt_fatal("scheduler full");
    int i = g_nev++;
    while (i > 0 && g_ev[i - 1].when > cycle) { g_ev[i] = g_ev[i - 1]; i--; }
    g_ev[i] = (Event){cycle, cb, arg};
    rt_shorten_slice(cycle);   /* make sure the running slice ends in time */
}

void rt_sched_cancel(SchedCb cb, void *arg) {
    int j = 0;
    for (int i = 0; i < g_nev; i++)
        if (!(g_ev[i].cb == cb && g_ev[i].arg == arg)) g_ev[j++] = g_ev[i];
    g_nev = j;
}

uint64_t rt_sched_next(void) {
    return g_nev ? g_ev[0].when : g_ctx.cycles + 1000000;
}

void rt_sched_run(uint64_t now) {
    while (g_nev && g_ev[0].when <= now) {
        Event e = g_ev[0];
        for (int i = 1; i < g_nev; i++) g_ev[i - 1] = g_ev[i];
        g_nev--;
        e.cb(e.arg);
    }
}
