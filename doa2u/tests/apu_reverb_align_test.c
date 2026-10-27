/* Host test for the reverb send's alignment (src/apu/apu_reverb_align.c): the
 * wet buffer must start with as many buffers ahead of it as the dry buffer of
 * the same samples, or the reverb plays before the sound it belongs to. */
#include <stdio.h>
#include "apu/apu_reverb.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main(void)
{
    /* the dry voice holds 4 (its new buffer is the 4th), the wet voice is
     * empty: 3 silent buffers go first */
    CHECK(apu_reverb_pad(4, 0) == 3);
    CHECK(apu_reverb_pad(4, 2) == 1);
    CHECK(apu_reverb_pad(4, 3) == 0);       /* aligned */
    CHECK(apu_reverb_pad(2, 3) == 0);       /* the dry voice ran short: never a negative pad */
    CHECK(apu_reverb_pad(1, 0) == 0);
    CHECK(apu_reverb_pad(0, 0) == 0);
    CHECK(apu_reverb_pad(40, 0) == 6);      /* never more than the wet ring can hold beside the new buffer */
    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
