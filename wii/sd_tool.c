/* SD card helper for remote use: sent by wiiload with report=HOST:PORT, it
 * connects back to wii/sd_remote.py and serves requests until QUIT, then
 * returns to the Homebrew Channel. The Wii connects out, so no FTP server
 * or Wii-side network settings are involved.
 * Requests are one text line; replies are "OK <n>\n" followed by n bytes, or
 * "ERR <message>\n". File contents travel as a zlib stream in frames (32-bit
 * big-endian length, bytes; a zero length ends it): Wii Wi-Fi is slow.
 *   LIST <dir>         one line per entry: "d <name>" or "f <size> <name>"
 *   GET <path>         "OK 0\n" then the file as zlib frames
 *   PUT <path> <size>  then the file as zlib frames; parents are created
 * Each request shows one line, redrawn in place: a three-character status
 * then the full path. PUT/GET/DEL when it starts, 0%-99% while transferring,
 * AOK when done, ERR when it failed.
 *   DEL <path>         a file, or a directory with everything in it
 *   QUIT               return to the Homebrew Channel */
#include <gccore.h>
#include <network.h>
#include <fat.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>
int usleep(unsigned int);   /* not declared by this newlib configuration */

static int sock = -1;
static char line[1024];
static char buf[65536];

static int send_all(const void *p, size_t n) {
    const char *c = p;
    while (n) {
        int r = net_send(sock, c, n > 32768 ? 32768 : n, 0);
        if (r <= 0) return -1;
        c += r;
        n -= (size_t)r;
    }
    return 0;
}

static int recv_all(void *p, size_t n) {
    char *c = p;
    while (n) {
        int r = net_recv(sock, c, n > 32768 ? 32768 : n, 0);
        if (r <= 0) return -1;
        c += r;
        n -= (size_t)r;
    }
    return 0;
}

static int recv_line(void) {
    size_t n = 0;
    for (;;) {
        char ch;
        if (net_recv(sock, &ch, 1, 0) != 1) return -1;
        if (ch == '\n') break;
        if (n + 1 < sizeof line) line[n++] = ch;
    }
    line[n] = 0;
    return 0;
}

static int reply_err(const char *what) {
    char m[256];
    int n = snprintf(m, sizeof m, "ERR %s (errno %d)\n", what, errno);
    return send_all(m, (size_t)n);
}

static int reply_ok(const void *body, size_t n) {
    char h[32];
    int hn = snprintf(h, sizeof h, "OK %u\n", (unsigned)n);
    return send_all(h, (size_t)hn) || (n && send_all(body, n));
}

static void make_parents(const char *path) {
    char tmp[1280];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 4; *p; p++) {   /* skip "sd:/" */
        if (*p != '/') continue;
        *p = 0;
        mkdir(tmp, 0777);
        *p = '/';
    }
}

static int remove_tree(const char *path) {
    struct stat st;
    if (stat(path, &st)) return -1;
    if (!S_ISDIR(st.st_mode)) return remove(path);
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *e;
    char child[1280];
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(child, sizeof child, "%s/%s", path, e->d_name);
        if (remove_tree(child)) { closedir(d); return -1; }
    }
    closedir(d);
    return remove(path);
}

static int do_list(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return reply_err("opendir");
    static char out[65536];
    size_t n = 0;
    struct dirent *e;
    char child[1280];
    while ((e = readdir(d)) && n + 600 < sizeof out) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(child, sizeof child, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(child, &st)) continue;
        if (S_ISDIR(st.st_mode)) n += (size_t)snprintf(out + n, sizeof out - n, "d %s\n", e->d_name);
        else n += (size_t)snprintf(out + n, sizeof out - n, "f %ld %s\n", (long)st.st_size, e->d_name);
    }
    closedir(d);
    return reply_ok(out, n);
}

static void status(const char *state, const char *path) {
    printf("\r  %-3s %s", state, path);
    fflush(stdout);
}
static void finish(int ok, const char *path) {
    printf("\r  %s %s\n", ok ? "AOK" : "ERR", path);
}
static void progress(const char *path, long done, long total, int *shown) {
    int pct = total > 0 ? (int)((long long)done * 100 / total) : 100;
    if (pct > 99 || pct == *shown) return;
    *shown = pct;
    char p[16];
    snprintf(p, sizeof p, "%2d%%", pct);
    status(p, path);
}

static int send_frame(const void *p, uint32_t n) {
    unsigned char be[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n};
    return send_all(be, 4) || (n && send_all(p, n));
}

static int do_get(const char *path) {
    status("GET", path);
    FILE *f = fopen(path, "rb");
    if (!f) { finish(0, path); return reply_err("open"); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f), done = 0;
    fseek(f, 0, SEEK_SET);
    int shown = -1;
    if (reply_ok(NULL, 0)) { fclose(f); return -1; }
    static unsigned char out[65536];
    z_stream z;
    memset(&z, 0, sizeof z);
    deflateInit(&z, 1);
    int bad = 0, flush = Z_NO_FLUSH;
    while (!bad && flush != Z_FINISH) {
        z.avail_in = (uInt)fread(buf, 1, sizeof buf, f);
        z.next_in = (Bytef *)buf;
        done += z.avail_in;
        progress(path, done, size, &shown);
        flush = feof(f) || ferror(f) ? Z_FINISH : Z_NO_FLUSH;
        do {
            z.next_out = out;
            z.avail_out = sizeof out;
            deflate(&z, flush);
            uint32_t n = (uint32_t)(sizeof out - z.avail_out);
            if (n && send_frame(out, n)) { bad = 1; break; }
        } while (z.avail_out == 0);
    }
    deflateEnd(&z);
    fclose(f);
    if (!bad) bad = send_frame(NULL, 0);
    finish(!bad, path);
    return bad ? -1 : 0;
}

static int do_put(char *args) {
    char *sp = strrchr(args, ' ');
    long size = 0;
    if (sp) { *sp = 0; size = atol(sp + 1); }
    const char *path = args;
    int shown = -1;
    status("PUT", path);
    make_parents(path);
    FILE *f = fopen(path, "wb");
    int write_failed = !f, bad = 0;
    static unsigned char out[65536];
    z_stream z;
    memset(&z, 0, sizeof z);
    inflateInit(&z);
    long total = 0;
    for (;;) {
        unsigned char be[4];
        if (recv_all(be, 4)) { bad = 1; break; }
        uint32_t n = (uint32_t)be[0] << 24 | (uint32_t)be[1] << 16 | (uint32_t)be[2] << 8 | be[3];
        if (!n) break;
        if (n > sizeof buf || recv_all(buf, n)) { bad = 1; break; }
        z.next_in = (Bytef *)buf;
        z.avail_in = n;
        do {
            z.next_out = out;
            z.avail_out = sizeof out;
            int r = inflate(&z, Z_NO_FLUSH);
            if (r != Z_OK && r != Z_STREAM_END) { write_failed = 1; break; }
            size_t got = sizeof out - z.avail_out;
            total += (long)got;
            progress(path, total, size, &shown);
            if (f && got && fwrite(out, 1, got, f) != got) write_failed = 1;
        } while (z.avail_out == 0);
    }
    inflateEnd(&z);
    if (f && fclose(f)) write_failed = 1;
    finish(!bad && !write_failed, path);
    if (bad) return -1;
    return write_failed ? reply_err("write") : reply_ok(NULL, 0);
}

int main(int argc, char **argv) {
    VIDEO_Init();
    GXRModeObj *mode = VIDEO_GetPreferredMode(NULL);
    void *fb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    VIDEO_ClearFrameBuffer(mode, fb, COLOR_BLACK);
    console_init(fb, 20, 20, mode->fbWidth, mode->xfbHeight, mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(fb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    printf("\n\n  Viper SD helper\n\n");
    char host[40] = "";
    unsigned port = 0;
    for (int i = 1; i < argc; i++) {
        const char *colon = strrchr(argv[i], ':');
        if (strncmp(argv[i], "report=", 7) || !colon || colon - (argv[i] + 7) >= (int)sizeof host) continue;
        memcpy(host, argv[i] + 7, (size_t)(colon - (argv[i] + 7)));
        host[colon - (argv[i] + 7)] = 0;
        port = (unsigned)atoi(colon + 1);
    }
    if (!port) { printf("  no report=HOST:PORT argument\n"); usleep(3000000); return 0; }
    if (!fatInitDefault()) { printf("  cannot mount the SD card\n"); usleep(3000000); return 0; }
    int r;
    for (int tries = 0; (r = net_init()) == -EAGAIN && tries < 50; tries++) usleep(100000);
    if (r < 0) { printf("  network init failed (%d)\n", r); usleep(3000000); return 0; }
    sock = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_len = sizeof a;
    a.sin_port = htons(port);
    if (sock < 0 || !inet_aton(host, &a.sin_addr) || net_connect(sock, (struct sockaddr *)&a, sizeof a) < 0) {
        printf("  cannot reach %s:%u\n", host, port);
        usleep(3000000);
        return 0;
    }
    printf("  connected to %s:%u\n", host, port);
    while (!recv_line()) {
        int bad;
        if (!strncmp(line, "LIST ", 5)) bad = do_list(line + 5);
        else if (!strncmp(line, "GET ", 4)) bad = do_get(line + 4);
        else if (!strncmp(line, "PUT ", 4)) bad = do_put(line + 4);
        else if (!strncmp(line, "DEL ", 4)) {
            status("DEL", line + 4);
            int failed = remove_tree(line + 4) != 0;
            finish(!failed, line + 4);
            bad = failed ? reply_err("delete") : reply_ok(NULL, 0);
        } else if (!strcmp(line, "QUIT")) { reply_ok(NULL, 0); break; }
        else bad = reply_err("unknown request");
        if (bad) break;
    }
    net_close(sock);
    /* Blank before handing back: the loader's framebuffer is not cleared, so
     * the old buffer would flash as static while it starts. */
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    VIDEO_WaitVSync();
    return 0;
}
