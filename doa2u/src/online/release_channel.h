/* Netplay release channel: the builds both players download from GitHub.
 *
 * The online code refuses a player whose build differs (the build id covers
 * the compiled code, so two compilers never match). Instead of passing exe
 * files around, the netplay branch's CI publishes every push as a GitHub
 * pre-release tagged "<channel>-v<major>.<minor>.<CI run>", and Check for
 * Updates fetches the newest one. These helpers are pure (no network):
 * version parsing and comparison, picking the release from the GitHub
 * release-list JSON, and the advice shown when two players' builds differ. */
#ifndef RELEASE_CHANNEL_H
#define RELEASE_CHANNEL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RC_VERSION_MAX 32    /* "netplay-v1.0.12345" and a terminator, with room */

/* "netplay-v1.2.3", "v1.2.3" or "1.2.3" -> 1 and out[0..2]; anything else -> 0. */
int rc_parse_version(const char *s, int out[3]);

/* Like strcmp on the three numbers; a string that does not parse sorts lowest. */
int rc_version_cmp(const char *a, const char *b);

/* A self-built exe: version parses with build number 0 (CI builds count from 1). */
int rc_is_dev(const char *version);

/* From the JSON of GET /repos/<owner>/<repo>/releases (newest first): the first
 * release that is not a draft, whose tag is "<channel>-v<version>" and which has
 * a .zip asset. Writes its tag and the zip's download URL. 1 = found, 0 = none. */
int rc_pick_release(const char *json, const char *channel,
                    char *tag, size_t tag_n, char *zip_url, size_t url_n);

/* What to tell a player whose build differs from the other player's: who has
 * the newer release and who should press Check for Updates. `theirs` may be ""
 * (a build from before versions were exchanged). */
void rc_mismatch_advice(const char *mine, const char *theirs, char *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* RELEASE_CHANNEL_H */
