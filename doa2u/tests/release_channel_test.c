/* release_channel.c: version parsing and order, picking the netplay release from
 * a GitHub release list, and the advice when two players' builds differ. */
#include "online/release_channel.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

/* Shaped like GET /repos/<owner>/<repo>/releases: newest first, nested author /
 * uploader objects, a body with braces, brackets and escaped quotes. In order:
 *  1. a draft of netplay-v1.0.21 (never offered),
 *  2. a release of the other channel, v1.0.20,
 *  3. netplay-nightly (not a version),
 *  4. netplay-v1.0.19 whose only asset is a .txt (the zip is missing),
 *  5. netplay-v1.0.18 with a .txt and the zip   <- the one to pick,
 *  6. netplay-v1.0.17 with a zip (older).
 * Pairing release 4's tag with release 5's zip is the likeliest wrong reading. */
static const char *k_list =
"[\n"
" {\"url\":\"https://api.github.com/repos/o/r/releases/6\",\"tag_name\":\"netplay-v1.0.21\",\"draft\":true,"
"  \"prerelease\":true,\"author\":{\"login\":\"x\",\"id\":1},"
"  \"assets\":[{\"name\":\"DOA2U-netplay-v1.0.21-win64.zip\","
"   \"browser_download_url\":\"https://github.com/o/r/releases/download/netplay-v1.0.21/DOA2U-netplay-v1.0.21-win64.zip\"}],"
"  \"body\":\"draft\"},\n"
" {\"url\":\"https://api.github.com/repos/o/r/releases/5\",\"tag_name\":\"v1.0.20\",\"draft\":false,"
"  \"prerelease\":false,\"author\":{\"login\":\"x\",\"id\":1},"
"  \"assets\":[{\"name\":\"DOA2U-v1.0.20-win64.zip\","
"   \"browser_download_url\":\"https://github.com/o/r/releases/download/v1.0.20/DOA2U-v1.0.20-win64.zip\"}],"
"  \"body\":\"master\"},\n"
" {\"url\":\"https://api.github.com/repos/o/r/releases/4\",\"tag_name\":\"netplay-nightly\",\"draft\":false,"
"  \"prerelease\":true,\"assets\":[{\"name\":\"n.zip\","
"   \"browser_download_url\":\"https://github.com/o/r/releases/download/netplay-nightly/n.zip\"}],\"body\":\"\"},\n"
" {\"url\":\"https://api.github.com/repos/o/r/releases/3\",\"tag_name\":\"netplay-v1.0.19\",\"draft\":false,"
"  \"prerelease\":true,\"author\":{\"login\":\"x\",\"id\":1},"
"  \"assets\":[{\"name\":\"notes.txt\",\"uploader\":{\"login\":\"y\"},"
"   \"browser_download_url\":\"https://github.com/o/r/releases/download/netplay-v1.0.19/notes.txt\"}],"
"  \"zipball_url\":\"https://api.github.com/repos/o/r/zipball/netplay-v1.0.19\","
"  \"body\":\"fixes {a} [b] and \\\"quoted\\\" text, a brace } and a bracket ]\"},\n"
" {\"url\":\"https://api.github.com/repos/o/r/releases/2\",\"tag_name\":\"netplay-v1.0.18\",\"draft\":false,"
"  \"prerelease\":true,\"author\":{\"login\":\"x\",\"id\":1},"
"  \"assets\":[{\"name\":\"notes.txt\","
"   \"browser_download_url\":\"https://github.com/o/r/releases/download/netplay-v1.0.18/notes.txt\"},"
"   {\"name\":\"DOA2U-netplay-v1.0.18-win64.zip\",\"uploader\":{\"login\":\"y\"},"
"   \"browser_download_url\":\"https://github.com/o/r/releases/download/netplay-v1.0.18/DOA2U-netplay-v1.0.18-win64.zip\"}],"
"  \"body\":\"\"},\n"
" {\"url\":\"https://api.github.com/repos/o/r/releases/1\",\"tag_name\":\"netplay-v1.0.17\",\"draft\":false,"
"  \"prerelease\":true,\"assets\":[{\"name\":\"DOA2U-netplay-v1.0.17-win64.zip\","
"   \"browser_download_url\":\"https://github.com/o/r/releases/download/netplay-v1.0.17/DOA2U-netplay-v1.0.17-win64.zip\"}],"
"  \"body\":\"\"}\n"
"]\n";

int main(void)
{
    int v[3];
    char tag[RC_VERSION_MAX], url[512], small[8], adv[200];

    /* parsing: the three spellings, and what is not a version */
    CHECK(rc_parse_version("netplay-v1.0.14", v) == 1 && v[0] == 1 && v[1] == 0 && v[2] == 14);
    CHECK(rc_parse_version("v2.3.4", v) == 1 && v[0] == 2 && v[1] == 3 && v[2] == 4);
    CHECK(rc_parse_version("1.0.0", v) == 1 && v[2] == 0);
    CHECK(rc_parse_version("netplay-nightly", v) == 0);
    CHECK(rc_parse_version("", v) == 0);
    CHECK(rc_parse_version(NULL, v) == 0);
    CHECK(rc_parse_version("v1.0", v) == 0);                     /* three numbers or nothing */

    /* order is numeric, not textual: 9 < 10, and the major number dominates */
    CHECK(rc_version_cmp("netplay-v1.0.9", "netplay-v1.0.10") < 0);
    CHECK(rc_version_cmp("netplay-v1.0.10", "netplay-v1.0.9") > 0);
    CHECK(rc_version_cmp("v2.0.0", "v1.9.99") > 0);
    CHECK(rc_version_cmp("netplay-v1.0.3", "v1.0.3") == 0);
    CHECK(rc_version_cmp("garbage", "1.0.0") < 0);

    /* a self-built exe is build number 0 */
    CHECK(rc_is_dev("netplay-v1.0.0") == 1);
    CHECK(rc_is_dev("netplay-v1.0.4") == 0);
    CHECK(rc_is_dev("") == 0);

    /* picking: release 5 of the list, its own tag with its own zip */
    memset(tag, 0, sizeof tag); memset(url, 0, sizeof url);
    CHECK(rc_pick_release(k_list, "netplay", tag, sizeof tag, url, sizeof url) == 1);
    CHECK(strcmp(tag, "netplay-v1.0.18") == 0);
    CHECK(strcmp(url, "https://github.com/o/r/releases/download/netplay-v1.0.18/DOA2U-netplay-v1.0.18-win64.zip") == 0);
    CHECK(rc_pick_release("[]", "netplay", tag, sizeof tag, url, sizeof url) == 0);
    CHECK(rc_pick_release(NULL, "netplay", tag, sizeof tag, url, sizeof url) == 0);
    CHECK(rc_pick_release("{\"message\":\"API rate limit exceeded\"}", "netplay", tag, sizeof tag, url, sizeof url) == 0);
    /* a tag that does not fit is not written half */
    CHECK(rc_pick_release(k_list, "netplay", small, sizeof small, url, sizeof url) == 0);

    /* advice: who is older, self-built exes, a build from before the check */
    rc_mismatch_advice("netplay-v1.0.12", "netplay-v1.0.14", adv, sizeof adv);
    CHECK(strstr(adv, "You have an older release") != NULL && strstr(adv, "v1.0.14") != NULL &&
          strstr(adv, "Check for Updates") != NULL);
    rc_mismatch_advice("netplay-v1.0.14", "netplay-v1.0.12", adv, sizeof adv);
    CHECK(strstr(adv, "The other player has an older release") != NULL && strstr(adv, "v1.0.12") != NULL);
    rc_mismatch_advice("netplay-v1.0.0", "netplay-v1.0.14", adv, sizeof adv);
    CHECK(strstr(adv, "You run a self-built exe") != NULL);
    rc_mismatch_advice("netplay-v1.0.14", "netplay-v1.0.0", adv, sizeof adv);
    CHECK(strstr(adv, "The other player runs a self-built exe") != NULL);
    rc_mismatch_advice("netplay-v1.0.14", "", adv, sizeof adv);
    CHECK(strstr(adv, "older game") != NULL);
    rc_mismatch_advice("netplay-v1.0.14", "netplay-v1.0.14", adv, sizeof adv);
    CHECK(strstr(adv, "same version") != NULL);
    rc_mismatch_advice("netplay-v1.0.12", "netplay-v1.0.14", small, sizeof small);
    CHECK(strlen(small) < sizeof small);                        /* never overruns */

    if (failures) fprintf(stderr, "release_channel_test: %d failure(s)\n", failures);
    else printf("release_channel_test: all checks passed\n");
    return failures;
}
