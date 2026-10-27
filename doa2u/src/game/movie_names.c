/* Which guest movies are the boot intro (see movie_names.h). */
#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include "movie_names.h"

int doa2u_movie_is_intro(const char *guest_path)
{
    static const char *const intro[] = { "2_vi_wide_b.sfd", "4_vi_std_b.sfd", "doa2_op.sfd" };
    const char *base, *p;
    size_t i;
    if (!guest_path) return 0;
    base = guest_path;
    for (p = guest_path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':') base = p + 1;
    for (i = 0; i < sizeof intro / sizeof intro[0]; i++) {
        const char *a = base, *b = intro[i];
        while (*a && *b && tolower((unsigned char)*a) == *b) { a++; b++; }
        if (!*a && !*b) return 1;
    }
    return 0;
}
