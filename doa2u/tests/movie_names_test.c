/* Host test for the intro-movie names (src/game/movie_names.c). */
#include <stdio.h>
#include "game/movie_names.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main(void)
{
    /* The boot logos (16:9 and 4:3) and the opening, however the guest spells the path. */
    CHECK(doa2u_movie_is_intro("d:\\doa2\\2_vi_wide_b.sfd"));
    CHECK(doa2u_movie_is_intro("D:\\DOA2\\2_VI_WIDE_B.sfd"));
    CHECK(doa2u_movie_is_intro("d:\\doa2\\4_vi_std_b.sfd"));
    CHECK(doa2u_movie_is_intro("d:\\doa2\\doa2_op.sfd"));
    CHECK(doa2u_movie_is_intro("doa2_op.sfd"));
    CHECK(doa2u_movie_is_intro("d:/doa2/doa2_op.sfd"));
    /* Anything else plays. */
    CHECK(!doa2u_movie_is_intro("d:\\doa2\\doa_g4.sfd"));
    CHECK(!doa2u_movie_is_intro("d:\\doa2\\xdoa2_op.sfd"));
    CHECK(!doa2u_movie_is_intro("d:\\doa2\\doa2_op.sfd.bak"));
    CHECK(!doa2u_movie_is_intro(""));
    CHECK(!doa2u_movie_is_intro(NULL));
    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
