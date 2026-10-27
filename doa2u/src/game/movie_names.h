/* Which guest movies are the boot intro (pure). */
#ifndef DOA2U_MOVIE_NAMES_H
#define DOA2U_MOVIE_NAMES_H

/* Non-zero for the boot logo (2_vi_wide_b / 4_vi_std_b) and the opening
 * (doa2_op, also the title screen's idle movie); any path, any case. */
int doa2u_movie_is_intro(const char *guest_path);

#endif
