/* Netplay release channel helpers (see release_channel.h). */
#include "release_channel.h"

#include <stdio.h>
#include <string.h>

int rc_parse_version(const char *s, int out[3])
{
    const char *p, *dash;
    int i, n;
    if (!s || !*s) return 0;
    dash = strrchr(s, '-');                         /* "<channel>-v1.2.3" */
    p = dash ? dash + 1 : s;
    if (*p == 'v') p++;
    for (i = 0; i < 3; i++) {
        if (*p < '0' || *p > '9') return 0;
        n = 0;
        while (*p >= '0' && *p <= '9') {
            if (n > 99999999) return 0;
            n = n * 10 + (*p++ - '0');
        }
        out[i] = n;
        if (i < 2) {
            if (*p != '.') return 0;
            p++;
        }
    }
    return *p == 0;
}

int rc_version_cmp(const char *a, const char *b)
{
    int va[3], vb[3], i;
    int pa = rc_parse_version(a, va), pb = rc_parse_version(b, vb);
    if (!pa || !pb) return pa - pb;                 /* unparsable sorts lowest */
    for (i = 0; i < 3; i++)
        if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
    return 0;
}

int rc_is_dev(const char *version)
{
    int v[3];
    return rc_parse_version(version, v) && v[2] == 0;
}

/* ── a small reader for GitHub's release list ─────────────────────────── */

/* End of the JSON string that starts at s (on its opening quote): the closing quote. */
static const char *str_end(const char *s)
{
    for (s++; *s; s++) {
        if (*s == '\\' && s[1]) s++;
        else if (*s == '"') return s;
    }
    return NULL;
}

/* End of the object or array that starts at s (on '{' or '['): its closing bracket. */
static const char *block_end(const char *s)
{
    int depth = 0;
    for (; *s; s++) {
        if (*s == '"') {
            s = str_end(s);
            if (!s) return NULL;
        } else if (*s == '{' || *s == '[') {
            depth++;
        } else if (*s == '}' || *s == ']') {
            if (--depth == 0) return s;
        }
    }
    return NULL;
}

/* In the object [obj, end], the value of the top-level member `key`: a pointer
 * to its first character (a quote for a string), or NULL. */
static const char *member(const char *obj, const char *end, const char *key)
{
    size_t klen = strlen(key);
    const char *p = obj + 1;
    while (p < end) {
        const char *k, *ke, *v;
        while (p < end && *p != '"') p++;           /* the next key */
        if (p >= end) return NULL;
        k = p;
        ke = str_end(k);
        if (!ke || ke >= end) return NULL;
        v = ke + 1;
        while (v < end && (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n' || *v == ':')) v++;
        if (v >= end) return NULL;
        if ((size_t)(ke - k - 1) == klen && !memcmp(k + 1, key, klen)) return v;
        /* skip the value */
        if (*v == '"') p = str_end(v);
        else if (*v == '{' || *v == '[') p = block_end(v);
        else { p = v; while (p < end && *p != ',' && *p != '}') p++; continue; }
        if (!p) return NULL;
        p++;
    }
    return NULL;
}

/* Copy the string value at v (on its quote) into out; 0 if it does not fit or has escapes. */
static int copy_str(const char *v, char *out, size_t n)
{
    const char *e;
    size_t len;
    if (!v || *v != '"' || !(e = str_end(v))) return 0;
    len = (size_t)(e - v - 1);
    if (len + 1 > n || memchr(v + 1, '\\', len)) return 0;
    memcpy(out, v + 1, len);
    out[len] = 0;
    return 1;
}

int rc_pick_release(const char *json, const char *channel,
                    char *tag, size_t tag_n, char *zip_url, size_t url_n)
{
    char prefix[40], t[RC_VERSION_MAX], u[1024];
    const char *p;
    size_t plen;
    int v[3];
    if (!json || !channel || !*channel || !tag || !zip_url) return 0;
    if (_snprintf(prefix, sizeof prefix, "%s-v", channel) < 0) return 0;
    prefix[sizeof prefix - 1] = 0;
    plen = strlen(prefix);
    p = json;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '[') return 0;                         /* an error object, not a list */
    for (p++; *p; p++) {
        const char *rel_end, *tv, *dv, *av, *aend, *a;
        if (*p == ']') break;
        if (*p != '{') continue;
        rel_end = block_end(p);
        if (!rel_end) return 0;
        tv = member(p, rel_end, "tag_name");
        dv = member(p, rel_end, "draft");
        if (copy_str(tv, t, sizeof t) && !strncmp(t, prefix, plen) && rc_parse_version(t, v) &&
            !(dv && !strncmp(dv, "true", 4)) && (av = member(p, rel_end, "assets")) && *av == '[' &&
            (aend = block_end(av))) {
            for (a = av + 1; a < aend; a++) {          /* each asset object */
                const char *a_end, *uv;
                size_t ul;
                if (*a != '{') continue;
                a_end = block_end(a);
                if (!a_end) return 0;
                uv = member(a, a_end, "browser_download_url");
                if (copy_str(uv, u, sizeof u) && (ul = strlen(u)) > 4 && !_stricmp(u + ul - 4, ".zip")) {
                    if (strlen(t) + 1 > tag_n || ul + 1 > url_n) return 0;   /* never half a tag */
                    strcpy(tag, t);
                    strcpy(zip_url, u);
                    return 1;
                }
                a = a_end;
            }
        }
        p = rel_end;
    }
    return 0;
}

static const char *bare(const char *version)       /* "netplay-v1.0.14" -> "v1.0.14" */
{
    const char *d = strrchr(version, '-');
    return d ? d + 1 : version;
}

void rc_mismatch_advice(const char *mine, const char *theirs, char *out, size_t n)
{
    int c, tv[3];
    if (!out || !n) return;
    if (!mine) mine = "";
    if (!theirs) theirs = "";
    if (!rc_parse_version(theirs, tv))
        _snprintf(out, n, "The other player has an older game without version checks: "
                          "they need the latest netplay release (Check for Updates).");
    else if (rc_is_dev(mine))
        _snprintf(out, n, "You run a self-built exe: both of you should use the latest netplay "
                          "release (press Check for Updates).");
    else if (rc_is_dev(theirs))
        _snprintf(out, n, "The other player runs a self-built exe: both of you should use the "
                          "latest netplay release (Check for Updates).");
    else if ((c = rc_version_cmp(mine, theirs)) < 0)
        _snprintf(out, n, "You have an older release (%s, the other player %s): press Check for "
                          "Updates, then restart the game.", bare(mine), bare(theirs));
    else if (c > 0)
        _snprintf(out, n, "The other player has an older release (%s, you %s): they need to press "
                          "Check for Updates.", bare(theirs), bare(mine));
    else
        _snprintf(out, n, "Both games say the same version (%s) but the builds differ: both press "
                          "Check for Updates to download the release again.", bare(mine));
    out[n - 1] = 0;
}
