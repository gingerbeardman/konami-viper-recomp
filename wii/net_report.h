#pragma once
/* See net_report.c: send SD results to a host named by wiiload's report= argument. */
void wii_net_report_args(int argc, char **argv);
int wii_net_report_wanted(void);
int wii_net_report_send(const char *dir, const char *const *names);
