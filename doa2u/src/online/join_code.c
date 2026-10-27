/* join_code.c - see join_code.h. Adapted from parkerallan/doa3-recomp
 * (MIT) doa3/src/online/join_code.c at 919c49f, which encoded 48 bits per
 * endpoint in 8 symbols; the 6-symbol form with a port index is new here. */
#include "join_code.h"
#include <stdio.h>
#include <string.h>

static const char k_alpha[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int parse_ipv4(const char *s, uint8_t b[4], const char **end)
{
    int i;
    for (i = 0; i < 4; i++) {
        int v = 0, n = 0;
        while (*s >= '0' && *s <= '9' && n < 3) { v = v * 10 + (*s - '0'); s++; n++; }
        if (!n || v > 255) return 0;
        b[i] = (uint8_t)v;
        if (i < 3) { if (*s != '.') return 0; s++; }
    }
    if (end) *end = s;
    return 1;
}

static int is_blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

int join_endpoint_from_text(const char *ipv4, uint16_t port, join_endpoint *out)
{
    if (!ipv4 || !out || !parse_ipv4(ipv4, out->ip, NULL)) return 0;
    out->port = port;
    return 1;
}

void join_endpoint_text(const join_endpoint *ep, char out[32])
{
    snprintf(out, 32, "%u.%u.%u.%u:%u", ep->ip[0], ep->ip[1], ep->ip[2], ep->ip[3], (unsigned)ep->port);
}

int join_code_make(const join_endpoint *eps, int n, char out[JOIN_CODE_LEN])
{
    uint64_t v = 0;
    int i, len;
    out[0] = 0;
    if (!eps || n < 1 || eps[0].port == 0) return 0;
    for (i = 0; i < 4; i++) v = (v << 8) | eps[0].ip[i];
    if (eps[0].port >= JOIN_CODE_PORT_BASE && eps[0].port < JOIN_CODE_PORT_BASE + JOIN_CODE_PORTS) {
        v = (v << 4) | (uint64_t)(eps[0].port - JOIN_CODE_PORT_BASE);
        len = 6;
    } else {
        v = (v << 16) | eps[0].port;
        len = 8;
    }
    for (i = len - 1; i >= 0; i--) { out[i] = k_alpha[v & 63]; v >>= 6; }
    out[len] = 0;
    return 1;
}

int join_code_parse(const char *text, uint16_t default_port, join_endpoint *out, int max)
{
    char clean[9];
    int n = 0, i;
    const char *s = text;
    uint64_t v = 0;
    if (!text || !out || max < 1) return 0;
    while (is_blank(*s)) s++;
    {   /* a dotted address, with or without :port */
        uint8_t b[4];
        const char *e;
        if (parse_ipv4(s, b, &e)) {
            unsigned p = default_port;
            if (*e == ':') {
                p = 0; e++;
                if (!(*e >= '0' && *e <= '9')) return 0;
                while (*e >= '0' && *e <= '9') { p = p * 10 + (unsigned)(*e - '0'); e++; if (p > 65535) return 0; }
            }
            while (is_blank(*e)) e++;
            if (*e || !p || !(b[0] | b[1] | b[2] | b[3])) return 0;
            memcpy(out[0].ip, b, 4);
            out[0].port = (uint16_t)p;
            return 1;
        }
    }
    for (; *s; s++) {
        if (is_blank(*s)) continue;
        if (n >= 8) return 0;
        clean[n++] = *s;
    }
    if (n != 6 && n != 8) return 0;
    for (i = 0; i < n; i++) {
        const char *p = strchr(k_alpha, clean[i]);
        if (!p) return 0;
        v = (v << 6) | (uint64_t)(p - k_alpha);
    }
    if (n == 6) {
        out[0].port = (uint16_t)(JOIN_CODE_PORT_BASE + (unsigned)(v & 15));
        v >>= 4;
    } else {
        out[0].port = (uint16_t)(v & 0xFFFF);
        v >>= 16;
    }
    for (i = 0; i < 4; i++) out[0].ip[i] = (uint8_t)(v >> (24 - 8 * i));
    if (!out[0].port || !(out[0].ip[0] | out[0].ip[1] | out[0].ip[2] | out[0].ip[3])) return 0;
    return 1;
}
