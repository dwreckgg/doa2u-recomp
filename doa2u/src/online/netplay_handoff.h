/*
 * netplay_handoff.h - what an online session carries across the restart
 * (spec section 12). Once two players are connected, the host decides the
 * session (seed, input delay, its profile files) and both games restart into
 * it: each writes its side's hand-off next to the executable, relaunches, and
 * the new process reads it before any guest code runs.
 *
 * The files are the host's save device contents (UserData, TitleData),
 * written into the session folders on both machines so the two games load
 * the same profile. A file name is a path relative to the save root, under
 * "UserData/" or "TitleData/", of plain components (letters, digits, '.',
 * '_', '-'), never "..", never absolute: the joiner writes what a remote host
 * sent, so nothing may land outside those folders.
 *
 * No guest memory, no sockets: host-tested (tests/netplay_handoff_test.c).
 */
#ifndef DOA2U_NETPLAY_HANDOFF_H
#define DOA2U_NETPLAY_HANDOFF_H

#include <stdint.h>
#include "netplay_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NP_HANDOFF_MAX_FILES  16u
#define NP_HANDOFF_NAME_MAX   96u               /* bytes, with the terminating 0 */
#define NP_HANDOFF_FILE_MAX   (512u * 1024u)    /* one file */
#define NP_HANDOFF_ROLE_HOST   0
#define NP_HANDOFF_ROLE_JOINER 1

typedef struct {
    char     name[NP_HANDOFF_NAME_MAX];
    uint32_t len;
    uint8_t *data;                               /* owned */
} np_handoff_file;

typedef struct {
    uint32_t build_stamp;
    uint8_t  role;                               /* NP_HANDOFF_ROLE_* : this machine's side */
    uint8_t  battle;                             /* the host's battle type (0 Single, 1 Tag) */
    uint8_t  delay;                              /* input delay in frames */
    uint16_t local_port;                         /* this machine's UDP port */
    uint8_t  peer_ip[4];
    uint16_t peer_port;
    uint32_t token;                              /* the session's id, the same on both sides */
    uint32_t seed;
    uint8_t  joiner_layout[NP_LAYOUT_MAX];       /* the joiner's button layout for port 1 */
    uint8_t  joiner_laysel;
    uint32_t n_files;
    np_handoff_file files[NP_HANDOFF_MAX_FILES];
} np_handoff;

/* 1 when `name` may be written by a joiner (see the header comment). */
int      np_handoff_name_ok(const char *name);

/* Adds a copy of `data`. 0 when full, too large or the name is refused. */
int      np_handoff_add_file(np_handoff *h, const char *name, const void *data, uint32_t len);

/* The bytes of `h`: pack returns the length written, 0 when `cap` is short
 * (pack(h, NULL, 0) returns the length needed). unpack returns NULL when it
 * loaded, else why not; on failure `h` holds nothing. */
uint32_t    np_handoff_pack(const np_handoff *h, uint8_t *dst, uint32_t cap);
const char *np_handoff_unpack(np_handoff *h, const uint8_t *src, uint32_t len);

void     np_handoff_free(np_handoff *h);

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_NETPLAY_HANDOFF_H */
