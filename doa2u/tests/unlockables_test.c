/* unlockables_test: runs the unlock pokes against a fake Xbox memory image.
 *
 * The module resolves Xbox VAs through g_xbox_mem_offset exactly like the
 * recompiled code, so the test owns that symbol and points it at a zeroed
 * buffer large enough to hold every address the module touches.
 *
 * Expected bytes below are hand-derived from the layout in unlockables.h,
 * not computed with the module's own rules.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

#include "unlockables.h"

ptrdiff_t g_xbox_mem_offset = 0;

#define GAMEDATA_PTR   0x87CFA8u
#define PROFILE_LOADED 0x801A1Cu   /* byte, 1 once a player profile is loaded */
#define GAMEDATA       0x7FEAE0u
#define SLOT_IDS       0x6959DCu   /* byte per slot: character id          */
#define TOTALS         0x6E0F08u   /* byte per character id: costume total */
#define FLAG_A         (GAMEDATA + 0x2ED2)
#define FLAG_B         (GAMEDATA + 0x2ED3)
#define FLAG_C         (GAMEDATA + 0x2ED4)
#define COSTUMES       (GAMEDATA + 0x2ED8)
#define VOICES         (GAMEDATA + 0x2EF8)   /* 15 per-character bytes */
#define VOICE_BONUS    (GAMEDATA + 0x2EE7)   /* 5 bonus bytes */
#define MEM_SIZE       (0x900000u)

static uint8_t *g_mem;
static int g_failures;
static int g_checks;

static uint8_t  r8(uint32_t va)  { return g_mem[va]; }
static void     w8(uint32_t va, uint8_t v)  { g_mem[va] = v; }
static void     w32(uint32_t va, uint32_t v) { memcpy(g_mem + va, &v, 4); }

#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* Real values from the game's tables: the slot -> character id list, and the
 * costume total per character id (Ayane is id 5 with 20 costumes). */
static const uint8_t k_slot_ids[UNLOCK_CHARACTER_SLOTS] =
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x0C, 0x0D, 0x11, 0x18, 0x15 };
static const uint8_t k_totals[0x19] =
    { 8, 16, 8, 7, 9, 20, 8, 13, 7, 8, 1, 3, 20, 20, 1, 1, 1, 3, 1, 1, 1, 14, 1, 1, 6 };

static void reset_image(void)
{
    memset(g_mem, 0, MEM_SIZE);
    for (int i = 0; i < UNLOCK_CHARACTER_SLOTS; i++) w8(SLOT_IDS + i, k_slot_ids[i]);
    for (int i = 0; i < (int)sizeof k_totals; i++) w8(TOTALS + i, k_totals[i]);
}

static void load_profile(void)
{
    w32(GAMEDATA_PTR, GAMEDATA);
    w8(PROFILE_LOADED, 1);
    /* A fresh profile: every slot starts with 2 costumes. */
    for (int i = 0; i < UNLOCK_CHARACTER_SLOTS; i++) w8(COSTUMES + i, 2);
}

static void test_no_profile_pointer(void)
{
    unlock_status st;
    reset_image();
    memset(&st, 0x5A, sizeof st);
    CHECK(unlockables_status(&st) == 0, "status should report no profile");
    CHECK(st.profile_loaded == 0, "profile_loaded should be 0");
    CHECK(st.characters_unlocked == 0 && st.costume_slots_maxed == 0 && st.costume_slots_total == 0,
          "status fields should be zeroed without a profile");
    CHECK(unlockables_unlock_characters() == -1, "unlock characters should return -1 without a profile");
    CHECK(unlockables_unlock_costumes() == -1, "unlock costumes should return -1 without a profile");
}

static void test_bad_profile_pointer_is_ignored(void)
{
    unlock_status st;
    reset_image();
    w32(GAMEDATA_PTR, 0x80000000u); /* outside the 64 MB Xbox RAM window */
    CHECK(unlockables_status(&st) == 0, "an out-of-range pointer must be treated as no profile");
    CHECK(unlockables_unlock_characters() == -1, "unlock characters must refuse an out-of-range pointer");
}

static void test_block_without_loaded_profile_is_ignored(void)
{
    /* Before a profile is picked the block exists but holds defaults; the
     * game's own loaded flag is the only thing that tells the two apart. */
    unlock_status st;
    reset_image();
    w32(GAMEDATA_PTR, GAMEDATA);
    w8(PROFILE_LOADED, 0);
    CHECK(unlockables_status(&st) == 0, "status must report no profile while the loaded flag is 0");
    CHECK(unlockables_unlock_characters() == -1, "unlock characters must refuse while the loaded flag is 0");
    CHECK(unlockables_unlock_costumes() == -1, "unlock costumes must refuse while the loaded flag is 0");
    CHECK(unlockables_unlock_voices() == -1, "unlock voices must refuse while the loaded flag is 0");
    CHECK(unlockables_wipe() == -1, "wipe must refuse while the loaded flag is 0");
    CHECK(r8(FLAG_A) == 0 && r8(COSTUMES + 0) == 0, "nothing may be written while the loaded flag is 0");
}

static void test_unlock_characters_sets_only_the_three_flags(void)
{
    reset_image();
    load_profile();
    w8(FLAG_B, 1);                 /* one already unlocked */
    w8(GAMEDATA + 0x2ED1, 0x77);  /* neighbours must stay untouched */
    w8(GAMEDATA + 0x2ED5, 0x33);

    int n = unlockables_unlock_characters();
    CHECK(n == 2, "expected 2 newly unlocked, got %d", n);
    CHECK(r8(FLAG_A) == 1 && r8(FLAG_B) == 1 && r8(FLAG_C) == 1, "all three flags should be 1");
    CHECK(r8(GAMEDATA + 0x2ED1) == 0x77, "byte before the flags was modified");
    CHECK(r8(GAMEDATA + 0x2ED5) == 0x33, "byte after the flags was modified");

    n = unlockables_unlock_characters();
    CHECK(n == 0, "second call should unlock nothing, got %d", n);
}

static void test_unlock_costumes_raises_to_total_and_never_lowers(void)
{
    reset_image();
    load_profile();
    w8(COSTUMES + 1, 16);  /* already at the total (id 1) */
    w8(COSTUMES + 2, 9);   /* above the total of 8: must not be lowered */
    w8(COSTUMES + 14, 0);  /* zero start */
    w8(COSTUMES + 15, 0x44); /* first byte after the array (a flag) must stay */

    int n = unlockables_unlock_costumes();
    CHECK(n == 13, "expected 13 slots raised, got %d", n);
    CHECK(r8(COSTUMES + 0) == 8, "slot 0 (id 0) should be 8, got %d", r8(COSTUMES + 0));
    CHECK(r8(COSTUMES + 1) == 16, "slot 1 should stay 16, got %d", r8(COSTUMES + 1));
    CHECK(r8(COSTUMES + 2) == 9, "slot 2 should stay 9, got %d", r8(COSTUMES + 2));
    CHECK(r8(COSTUMES + 5) == 20, "slot 5 (Ayane, id 5) should be 20, got %d", r8(COSTUMES + 5));
    CHECK(r8(COSTUMES + 12) == 3, "slot 12 (id 0x11) should be 3, got %d", r8(COSTUMES + 12));
    CHECK(r8(COSTUMES + 13) == 6, "slot 13 (id 0x18) should be 6, got %d", r8(COSTUMES + 13));
    CHECK(r8(COSTUMES + 14) == 14, "slot 14 (id 0x15) should be 14, got %d", r8(COSTUMES + 14));
    CHECK(r8(COSTUMES + 15) == 0x44, "byte after the costume array was modified");

    n = unlockables_unlock_costumes();
    CHECK(n == 0, "second call should raise nothing, got %d", n);
}

static void test_unlock_costumes_skips_slots_with_an_insane_table(void)
{
    reset_image();
    load_profile();
    w8(TOTALS + 5, 0);      /* total not initialised for id 5 */
    w8(SLOT_IDS + 9, 0x7F); /* slot 9 points at an id outside the table */

    int n = unlockables_unlock_costumes();
    CHECK(n == 13, "expected 13 slots raised with two skipped, got %d", n);
    CHECK(r8(COSTUMES + 5) == 2, "slot 5 with total 0 must be untouched, got %d", r8(COSTUMES + 5));
    CHECK(r8(COSTUMES + 9) == 2, "slot 9 with a bad id must be untouched, got %d", r8(COSTUMES + 9));

    unlock_status st;
    unlockables_status(&st);
    CHECK(st.costume_slots_total == 13, "total should exclude skipped slots, got %d", st.costume_slots_total);
    CHECK(st.costume_slots_maxed == 13, "maxed should be 13, got %d", st.costume_slots_maxed);
}

static void test_status_counts_fresh_and_unlocked_profiles(void)
{
    unlock_status st;
    reset_image();
    load_profile();
    w8(COSTUMES + 2, 9); /* above the total of 8 counts as maxed */

    CHECK(unlockables_status(&st) == 1, "status should report a loaded profile");
    CHECK(st.profile_loaded == 1, "profile_loaded should be 1");
    CHECK(st.characters_unlocked == 0, "fresh profile has 0 hidden characters, got %d", st.characters_unlocked);
    CHECK(st.costume_slots_total == 15, "total slots should be 15, got %d", st.costume_slots_total);
    CHECK(st.costume_slots_maxed == 1, "only the above-cap slot is maxed, got %d", st.costume_slots_maxed);

    unlockables_unlock_characters();
    unlockables_unlock_costumes();
    unlockables_status(&st);
    CHECK(st.characters_unlocked == 3, "after unlock 3 hidden characters, got %d", st.characters_unlocked);
    CHECK(st.costume_slots_maxed == 15, "after unlock 15 slots maxed, got %d", st.costume_slots_maxed);
}

static void test_unlock_voices_sets_all_twenty_bytes(void)
{
    reset_image();
    load_profile();
    w8(VOICES + 3, 1);            /* one already unlocked */
    w8(VOICE_BONUS + 4, 1);       /* the all-collected bonus already set */
    w8(GAMEDATA + 0x2EF7, 0x55);  /* byte before the voice array (a counter) must stay */
    w8(GAMEDATA + 0x2F07, 0x66);  /* byte after the voice array must stay */

    int n = unlockables_unlock_voices();
    CHECK(n == 18, "expected 18 newly set, got %d", n);
    int all = 1;
    for (int i = 0; i < 15; i++) if (r8(VOICES + i) != 1) all = 0;
    for (int i = 0; i < 5; i++)  if (r8(VOICE_BONUS + i) != 1) all = 0;
    CHECK(all, "every voice byte should be 1");
    CHECK(r8(GAMEDATA + 0x2EF7) == 0x55, "byte before the voice array was modified");
    CHECK(r8(GAMEDATA + 0x2F07) == 0x66, "byte after the voice array was modified");

    n = unlockables_unlock_voices();
    CHECK(n == 0, "second call should set nothing, got %d", n);
}

static void test_unlock_voices_and_wipe_without_profile(void)
{
    reset_image();
    CHECK(unlockables_unlock_voices() == -1, "unlock voices should return -1 without a profile");
    CHECK(unlockables_wipe() == -1, "wipe should return -1 without a profile");
}

static void test_wipe_restores_fresh_profile_values_only(void)
{
    reset_image();
    load_profile();
    unlockables_unlock_characters();
    unlockables_unlock_costumes();
    unlockables_unlock_voices();
    w8(COSTUMES + 4, 1);          /* below the default: wipe still sets 2 */
    w8(GAMEDATA + 0x2ED1, 0x77);  /* neighbours of each range must stay */
    w8(GAMEDATA + 0x2ED5, 0x01);  /* the 0..3 level byte between flags and counts */
    w8(GAMEDATA + 0x2EF7, 0x55);
    w8(GAMEDATA + 0x2F07, 0x66);
    w8(GAMEDATA + 0x2F08, 0x99);

    int n = unlockables_wipe();
    CHECK(n == 38, "expected 3 + 15 + 20 = 38 bytes changed, got %d", n);
    CHECK(r8(FLAG_A) == 0 && r8(FLAG_B) == 0 && r8(FLAG_C) == 0, "character flags should be 0");
    int ok = 1;
    for (int i = 0; i < UNLOCK_CHARACTER_SLOTS; i++) if (r8(COSTUMES + i) != 2) ok = 0;
    CHECK(ok, "every costume count should be 2");
    ok = 1;
    for (int i = 0; i < 15; i++) if (r8(VOICES + i) != 0) ok = 0;
    for (int i = 0; i < 5; i++)  if (r8(VOICE_BONUS + i) != 0) ok = 0;
    CHECK(ok, "every voice byte should be 0");
    CHECK(r8(GAMEDATA + 0x2ED1) == 0x77, "byte before the flags was modified");
    CHECK(r8(GAMEDATA + 0x2ED5) == 0x01, "level byte was modified");
    CHECK(r8(GAMEDATA + 0x2EF7) == 0x55, "byte before the voice array was modified");
    CHECK(r8(GAMEDATA + 0x2F07) == 0x66, "byte after the voice array was modified");
    CHECK(r8(GAMEDATA + 0x2F08) == 0x99, "play-time dword was modified");

    n = unlockables_wipe();
    CHECK(n == 0, "second wipe should change nothing, got %d", n);

    unlock_status st;
    unlockables_status(&st);
    CHECK(st.characters_unlocked == 0 && st.voices_unlocked == 0,
          "status after wipe should show nothing unlocked, got %d chars %d voices",
          st.characters_unlocked, st.voices_unlocked);
    CHECK(st.costume_slots_maxed == 0, "no slot should be maxed after wipe, got %d", st.costume_slots_maxed);
}

static void test_status_counts_voices(void)
{
    unlock_status st;
    reset_image();
    load_profile();
    w8(VOICES + 0, 1);       /* per-character progress bytes are not voices */
    w8(VOICES + 14, 1);
    w8(VOICE_BONUS + 2, 1);  /* Ein */
    w8(VOICE_BONUS + 3, 2);  /* not 1: not counted */
    unlockables_status(&st);
    CHECK(st.voices_unlocked == 1, "expected 1 voice, got %d", st.voices_unlocked);
    unlockables_unlock_voices();
    unlockables_status(&st);
    CHECK(st.voices_unlocked == UNLOCK_SYSTEM_VOICES, "expected every voice after unlock, got %d", st.voices_unlocked);
    CHECK(UNLOCK_SYSTEM_VOICES == 5, "the game gates exactly 5 voices, header says %d", UNLOCK_SYSTEM_VOICES);
}

int main(void)
{
    g_mem = (uint8_t *)calloc(MEM_SIZE, 1);
    if (!g_mem) { puts("out of memory"); return 2; }
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)g_mem;

    test_no_profile_pointer();
    test_bad_profile_pointer_is_ignored();
    test_block_without_loaded_profile_is_ignored();
    test_unlock_characters_sets_only_the_three_flags();
    test_unlock_costumes_raises_to_total_and_never_lowers();
    test_unlock_costumes_skips_slots_with_an_insane_table();
    test_status_counts_fresh_and_unlocked_profiles();
    test_unlock_voices_sets_all_twenty_bytes();
    test_unlock_voices_and_wipe_without_profile();
    test_wipe_restores_fresh_profile_values_only();
    test_status_counts_voices();

    printf("unlockables_test: %d checks, %d failures\n", g_checks, g_failures);
    free(g_mem);
    return g_failures ? 1 : 0;
}
