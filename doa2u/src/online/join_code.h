/**
 * join_code.h - join codes for online play. A code carries one IPv4
 * address:port as symbols of the URL-safe base64 alphabet (A-Z a-z 0-9 - _,
 * case-sensitive):
 *
 *   6 symbols = address (32 bits) + port index (4 bits), the port being
 *               JOIN_CODE_PORT_BASE + index (7000..7015);
 *   8 symbols = address (32 bits) + the full port (16 bits), for a router
 *               that rewrote the port outside that range.
 *
 * The parser also takes a plain "a.b.c.d" or "a.b.c.d:port".
 */
#ifndef DOA2U_JOIN_CODE_H
#define DOA2U_JOIN_CODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JOIN_CODE_MAX_EP    1
#define JOIN_CODE_LEN       16      /* buffer size for the text */
#define JOIN_CODE_PORT_BASE 7000u
#define JOIN_CODE_PORTS     16u

typedef struct {
    uint8_t  ip[4];
    uint16_t port;
} join_endpoint;

/* Text for eps[0] (n >= 1): 6 symbols when its port is in the indexed range,
 * else 8. Returns 1, or 0 (out = "") when there is nothing to encode. */
int join_code_make(const join_endpoint *eps, int n, char out[JOIN_CODE_LEN]);

/* A join code (6 or 8 symbols, surrounding blanks ignored) or a dotted
 * address with optional ":port" (else `default_port`). Fills out[0] and
 * returns 1; 0 when the text is neither, the address is 0.0.0.0 or the
 * port is 0. */
int join_code_parse(const char *text, uint16_t default_port, join_endpoint *out, int max);

int  join_endpoint_from_text(const char *ipv4, uint16_t port, join_endpoint *out);
void join_endpoint_text(const join_endpoint *ep, char out[32]);

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_JOIN_CODE_H */
