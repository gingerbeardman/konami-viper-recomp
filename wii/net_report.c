/* Remote hardware testing: when a DOL is sent by wiiload with the argument
 * report=HOST:PORT, the scripted benchmark sends its SD results to that host
 * (wii/hw_results.py) and returns to the Homebrew Channel, ready for the next
 * DOL. Stream: per file "ZFILE <name>\n", then a zlib stream in frames of a
 * 32-bit big-endian length and that many bytes, ended by a zero length; then
 * "DONE\n". Compression matters: Wii Wi-Fi is slow and a RAM snapshot is 16 MB
 * of mostly zeros. */
#include <gccore.h>
#include <network.h>
#include <errno.h>
#include <fcntl.h>
int usleep(unsigned int);   /* not declared by this newlib configuration */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "net_report.h"

static char target_host[40];
static unsigned target_port;

void wii_net_report_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "report=", 7)) continue;
        const char *colon = strrchr(a + 7, ':');
        size_t n = colon ? (size_t)(colon - (a + 7)) : 0;
        if (!colon || !n || n >= sizeof target_host) continue;
        memcpy(target_host, a + 7, n);
        target_host[n] = 0;
        target_port = (unsigned)atoi(colon + 1);
    }
}

int wii_net_report_wanted(void) { return target_port != 0; }

/* Non-blocking, so a receiver that goes away cannot hang the Wii: give up
 * after 15 s without progress (the caller then returns to the loader). */
static int send_all(int s, const void *p, size_t n) {
    const char *c = p;
    unsigned idle = 0;
    while (n) {
        int r = net_send(s, c, n > 32768 ? 32768 : n, 0);
        if (r <= 0) {   /* would block, or failed: either way, wait and see */
            if (++idle > 1500) return -1;
            usleep(10000);
            continue;
        }
        idle = 0;
        c += r;
        n -= (size_t)r;
    }
    return 0;
}

static int send_frame(int s, const void *p, uint32_t n) {
    unsigned char be[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n};
    return send_all(s, be, 4) || (n && send_all(s, p, n));
}

static int send_file(int s, const char *dir, const char *name) {
    char path[96];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char head[64];
    int hn = snprintf(head, sizeof head, "ZFILE %s\n", name);
    int bad = send_all(s, head, (size_t)hn);
    static unsigned char in[32768], out[32768];
    z_stream z;
    memset(&z, 0, sizeof z);
    if (deflateInit(&z, 1) != Z_OK) bad = 1;
    long done = 0;
    int shown = -1, flush = Z_NO_FLUSH;
    while (!bad && flush != Z_FINISH) {
        z.avail_in = (uInt)fread(in, 1, sizeof in, f);
        z.next_in = in;
        done += z.avail_in;
        flush = feof(f) || ferror(f) ? Z_FINISH : Z_NO_FLUSH;
        do {
            z.next_out = out;
            z.avail_out = sizeof out;
            deflate(&z, flush);
            uint32_t n = (uint32_t)(sizeof out - z.avail_out);
            if (n && send_frame(s, out, n)) { bad = 1; break; }
        } while (z.avail_out == 0);
        int pct = size ? (int)(done * 100 / size) : 100;
        if (pct != shown) {
            shown = pct;
            char bar[26];
            for (int i = 0; i < 25; i++) bar[i] = i < pct / 4 ? '#' : '-';
            bar[25] = 0;
            printf("\r  %-8s [%s] %3d%%", name, bar, pct);
            fflush(stdout);
        }
    }
    printf("\n");
    deflateEnd(&z);
    fclose(f);
    if (!bad) bad = send_frame(s, NULL, 0);
    return bad ? -1 : 1;
}

static int connect_target(void) {
    int r;
    for (int tries = 0; (r = net_init()) == -EAGAIN && tries < 50; tries++) usleep(100000);
    if (r < 0) return -1;
    int s = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_len = sizeof a;
    a.sin_port = htons(target_port);
    if (!inet_aton(target_host, &a.sin_addr) || net_connect(s, (struct sockaddr *)&a, sizeof a) < 0) {
        net_close(s);
        return -1;
    }
    net_fcntl(s, F_SETFL, net_fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    return s;
}

/* A file made from memory (no SD access): the watchdog's report. */
int wii_net_report_text(const char *name, const char *text) {
    if (!target_port) return 0;
    int s = connect_target();
    if (s < 0) return -1;
    static unsigned char out[4096];
    uLongf n = sizeof out;
    char head[64];
    int hn = snprintf(head, sizeof head, "ZFILE %s\n", name);
    int bad = compress2(out, &n, (const Bytef *)text, (uLong)strlen(text), 1) != Z_OK ||
              send_all(s, head, (size_t)hn) || send_frame(s, out, (uint32_t)n) || send_frame(s, NULL, 0) ||
              send_all(s, "DONE\n", 5);
    net_close(s);
    return bad ? -1 : 1;
}

int wii_net_report_send(const char *dir, const char *const *names) {
    if (!target_port) return 0;
    printf("Sending results to %s:%u...\n", target_host, target_port);
    int r;
    for (int tries = 0; (r = net_init()) == -EAGAIN && tries < 50; tries++) usleep(100000);
    if (r < 0) { printf("network init failed (%d)\n", r); return -1; }
    int s = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0) { printf("socket failed (%d)\n", s); return -1; }
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_len = sizeof a;
    a.sin_port = htons(target_port);
    if (!inet_aton(target_host, &a.sin_addr) || net_connect(s, (struct sockaddr *)&a, sizeof a) < 0) {
        printf("cannot reach %s:%u\n", target_host, target_port);
        net_close(s);
        return -1;
    }
    net_fcntl(s, F_SETFL, net_fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    int bad = 0, sent = 0;
    for (unsigned i = 0; names[i] && !bad; i++) {
        int fr = send_file(s, dir, names[i]);
        if (fr < 0) bad = 1; else sent += fr;
    }
    if (!bad) bad = send_all(s, "DONE\n", 5);
    net_close(s);
    printf(bad ? "send failed\n" : "sent %d files\n", sent);
    return bad ? -1 : sent;
}

/* Remote runs must always return to the Homebrew Channel, or the console
 * needs a person to reset it. A top-priority thread waits out the deadline
 * and, if the run is still going (a hang, or a stop that did not return),
 * reports that and exits to the loader. It does not touch the SD card. */
static unsigned watchdog_seconds;
static void *watchdog_main(void *arg) {
    (void)arg;
    for (unsigned i = 0; i < watchdog_seconds; i++) usleep(1000000);
    char text[96];
    snprintf(text, sizeof text, "VIPER WII WATCHDOG no result after %u s; returned to the loader\n", watchdog_seconds);
    wii_net_report_text("boot.log", text);
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    exit(0);
    return NULL;
}

void wii_net_report_watchdog(unsigned seconds) {
    static lwp_t thread;
    static unsigned char stack[16384] __attribute__((aligned(32)));
    if (!target_port || !seconds) return;
    watchdog_seconds = seconds;
    LWP_CreateThread(&thread, watchdog_main, NULL, stack, sizeof stack, LWP_PRIO_HIGHEST);
}
