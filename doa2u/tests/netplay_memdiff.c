/* netplay_memdiff - questions about guest RAM dumps (netplay_dump.h).
 *
 *   netplay_memdiff info DUMP...
 *   netplay_memdiff classes [--width 1|2|4] [--range LO:HI] [--limit N] NAME=DUMP[,DUMP...] NAME=...
 *       values the same within each class and different between classes
 *   netplay_memdiff show VA LEN DUMP...
 *   netplay_memdiff find FILE DUMP
 *       every guest address where FILE's bytes appear (the save image)
 *
 * Addresses are hex. Built with the host tests; not run by the test runner. */
#include "online/netplay_dump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DUMPS 16

typedef struct { np_dump_info info; uint8_t *data; uint32_t lo, hi; } dump;

static int load(const char *path, uint32_t lo, uint32_t hi, dump *d)
{
    FILE *f = fopen(path, "rb");
    if (!f || !np_dump_read_header(f, &d->info)) { fprintf(stderr, "%s: not a dump\n", path); if (f) fclose(f); return 0; }
    if (lo < d->info.lo) lo = d->info.lo;
    if (hi > d->info.hi || hi == 0) hi = d->info.hi;
    if (hi <= lo) { fprintf(stderr, "%s: range outside the dump\n", path); fclose(f); return 0; }
    d->lo = lo; d->hi = hi;
    d->data = (uint8_t *)malloc(hi - lo);
    if (!d->data || _fseeki64(f, (long long)NP_DUMP_HEADER + (lo - d->info.lo), SEEK_SET) ||
        fread(d->data, 1, hi - lo, f) != hi - lo) {
        fprintf(stderr, "%s: read failed\n", path);
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static void print_hit(uint32_t va, const uint32_t *values, int nclass, void *ctx)
{
    int i;
    (void)ctx;
    printf("%08X", va);
    for (i = 0; i < nclass; i++) printf(" %X", values[i]);
    printf("\n");
}

static void print_va(uint32_t va, void *ctx) { (void)ctx; printf("%08X\n", va); }

static int cmd_classes(int argc, char **argv)
{
    static dump d[MAX_DUMPS];
    const uint8_t *bufs[MAX_DUMPS];
    int cls[MAX_DUMPS], n = 0, nclass = 0, width = 1, i;
    uint32_t lo = 0, hi = 0, limit = 200, len = 0xFFFFFFFFu;
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--width") && i + 1 < argc) { width = atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "--limit") && i + 1 < argc) { limit = (uint32_t)strtoul(argv[++i], NULL, 10); continue; }
        if (!strcmp(argv[i], "--range") && i + 1 < argc) {
            char *colon;
            lo = (uint32_t)strtoul(argv[++i], &colon, 16);
            hi = (*colon == ':') ? (uint32_t)strtoul(colon + 1, NULL, 16) : 0;
            continue;
        }
        {
            char *eq = strchr(argv[i], '='), *p, *tok;
            if (!eq) { fprintf(stderr, "expected NAME=DUMP[,DUMP...]: %s\n", argv[i]); return 2; }
            printf("class %d = %.*s\n", nclass, (int)(eq - argv[i]), argv[i]);
            p = eq + 1;
            for (tok = strtok(p, ","); tok; tok = strtok(NULL, ",")) {
                if (n == MAX_DUMPS || !load(tok, lo, hi, &d[n])) return 2;
                bufs[n] = d[n].data;
                cls[n] = nclass;
                if (n && (d[n].lo != d[0].lo || d[n].hi != d[0].hi)) { fprintf(stderr, "dumps cover different ranges\n"); return 2; }
                if (d[n].hi - d[n].lo < len) len = d[n].hi - d[n].lo;
                n++;
            }
            nclass++;
        }
    }
    if (nclass < 2) { fprintf(stderr, "need at least two classes\n"); return 2; }
    printf("%u hit(s)\n", np_dump_classes(bufs, cls, n, nclass, len, d[0].lo, width, limit, print_hit, NULL));
    return 0;
}

int main(int argc, char **argv)
{
    int i;
    if (argc >= 3 && !strcmp(argv[1], "info")) {
        for (i = 2; i < argc; i++) {
            FILE *f = fopen(argv[i], "rb");
            np_dump_info info;
            if (f && np_dump_read_header(f, &info))
                printf("%s: %08X..%08X frame %u builder calls %u tag '%s'\n", argv[i], info.lo, info.hi,
                       info.frame, info.builder_calls, info.tag);
            else
                printf("%s: not a dump\n", argv[i]);
            if (f) fclose(f);
        }
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "classes")) return cmd_classes(argc - 2, argv + 2);
    if (argc >= 5 && !strcmp(argv[1], "show")) {
        uint32_t va = (uint32_t)strtoul(argv[2], NULL, 16), len = (uint32_t)strtoul(argv[3], NULL, 16), k;
        for (i = 4; i < argc; i++) {
            dump d;
            if (!load(argv[i], va, va + len, &d)) return 2;
            printf("%-40s", argv[i]);
            for (k = 0; k < d.hi - d.lo; k++) printf(" %02X", d.data[k]);
            printf("\n");
            free(d.data);
        }
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "find")) {
        FILE *f = fopen(argv[2], "rb");
        uint8_t *needle;
        long n;
        dump d;
        if (!f) { fprintf(stderr, "%s: cannot open\n", argv[2]); return 2; }
        fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
        needle = (uint8_t *)malloc((size_t)n);
        if (!needle || fread(needle, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 2; }
        fclose(f);
        if (!load(argv[3], 0, 0, &d)) return 2;
        printf("%u hit(s)\n", np_dump_find(d.data, d.hi - d.lo, d.lo, needle, (uint32_t)n, print_va, NULL));
        return 0;
    }
    fprintf(stderr, "usage: netplay_memdiff info|classes|show|find ... (see the source header)\n");
    return 2;
}
