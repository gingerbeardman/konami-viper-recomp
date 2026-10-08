/* Profile-guided optimization, collection side (VIPER_WII_PGO_GEN).
 * Objects are built with -fprofile-arcs -fprofile-info-section=gcov_info:
 * GCC places one gcov_info pointer per object in section gcov_info and does
 * not register atexit dumps. At the scripted end this walks that section
 * and streams every object's gcda data into one file, sd:/viper/pgo.bin:
 *   'F' u32 length, filename (the gcda path chosen at compile time)
 *   'D' u32 length, bytes      (repeated: the gcda stream in chunks)
 *   'E'                        (end of this object)
 * wii/pgo_split.py turns it back into .gcda files beside the objects for
 * the -fprofile-use build. Diagnostic only: called after every measurement. */
#ifdef VIPER_WII_PGO_GEN
#include <gcov.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const struct gcov_info *const __start_gcov_info[];
extern const struct gcov_info *const __stop_gcov_info[];

static void put_record(FILE *f, char tag, const void *data, unsigned n) {
    unsigned char h[5] = {(unsigned char)tag, (unsigned char)(n >> 24), (unsigned char)(n >> 16),
                          (unsigned char)(n >> 8), (unsigned char)n};
    fwrite(h, 1, 5, f);
    if (n) fwrite(data, 1, n, f);
}
static void filename_fn(const char *name, void *arg) {
    put_record((FILE *)arg, 'F', name ? name : "", name ? (unsigned)strlen(name) : 0);
}
static void dump_fn(const void *data, unsigned n, void *arg) { put_record((FILE *)arg, 'D', data, n); }
static void *allocate_fn(unsigned n, void *arg) { (void)arg; return malloc(n); }

int wii_pgo_dump(void) {
    FILE *f = fopen("sd:/viper/pgo.bin", "wb");
    if (!f) return -1;
    int objects = 0;
    for (const struct gcov_info *const *p = __start_gcov_info; p < __stop_gcov_info; p++) {
        if (!*p) continue;
        __gcov_info_to_gcda(*p, filename_fn, dump_fn, allocate_fn, f);
        put_record(f, 'E', NULL, 0);
        objects++;
    }
    if (fclose(f)) return -1;
    return objects;
}
#endif
