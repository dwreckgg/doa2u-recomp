/* Unlockables (see unlockables.h for the layout and where it came from). */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unlockables.h"

/* Xbox VA -> host pointer. Every address here is a fixed Xbox VA, so the
 * plain offset add is exactly what recomp_types.h's XBOX_PTR does for it. */
extern ptrdiff_t g_xbox_mem_offset;

#define GAMEDATA_PTR_VA   0x87CFA8u   /* uint32_t: Xbox VA of the game-data block */
#define PROFILE_LOADED_VA 0x801A1Cu   /* byte: 1 once a player profile is loaded  */
#define SLOT_ID_TABLE     0x6959DCu   /* byte per slot: character id              */
#define COSTUME_TOTALS    0x6E0F08u   /* byte per character id: costume total     */
#define XBOX_RAM_LIMIT    0x04000000u /* 64 MB; anything above is not a block     */

#define OFF_COSTUME_COUNTS 0x2ED8u
#define OFF_VOICE_BONUS    0x2EE7u    /* 5 bytes: the unlockable System Voices       */
#define OFF_VOICES         0x2EF8u    /* 15 bytes: Survival cleared per character    */
#define CHARACTER_ID_MAX   0x18u      /* highest id the totals table covers       */
#define COSTUME_TOTAL_MAX  64u        /* totals above this are not a real table   */

static const uint32_t k_character_flag_offsets[UNLOCK_HIDDEN_CHARACTERS] =
    { 0x2ED2u, 0x2ED3u, 0x2ED4u };

static volatile uint8_t *xb8(uint32_t va)
{
    return (volatile uint8_t *)((uintptr_t)va + g_xbox_mem_offset);
}

static uint32_t xb32_read(uint32_t va)
{
    uint32_t v;
    memcpy(&v, (const void *)xb8(va), sizeof v);
    return v;
}

/* Xbox VA of the live game-data block, or 0 when none is reachable or no
 * player profile has been loaded into it yet. The pointer is set at boot and
 * the block then holds defaults until a profile is picked, so the game's own
 * loaded flag (read by its sub_00023BB0 accessor) decides. */
static uint32_t gamedata_va(void)
{
    uint32_t va = xb32_read(GAMEDATA_PTR_VA);
    if (va == 0 || va >= XBOX_RAM_LIMIT)
        return 0;
    if (*xb8(PROFILE_LOADED_VA) != 1)
        return 0;
    return va;
}

/* Costume total for the character in a slot, or 0 when either table entry is
 * not believable. */
static uint32_t costume_cap(int slot)
{
    uint8_t id = *xb8(SLOT_ID_TABLE + (uint32_t)slot);
    if (id > CHARACTER_ID_MAX)
        return 0;
    uint8_t total = *xb8(COSTUME_TOTALS + id);
    if (total == 0 || total > COSTUME_TOTAL_MAX)
        return 0;
    return total;
}

/* The 20 bytes the voice unlock owns: the 15 progress bytes first, then the
 * 5 voice bytes. */
#define VOICE_BYTES (UNLOCK_VOICE_PROGRESS + UNLOCK_SYSTEM_VOICES)

static volatile uint8_t *voice_byte(uint32_t gd, int i)
{
    if (i < UNLOCK_VOICE_PROGRESS)
        return xb8(gd + OFF_VOICES + (uint32_t)i);
    return xb8(gd + OFF_VOICE_BONUS + (uint32_t)(i - UNLOCK_VOICE_PROGRESS));
}

/* Writes v to *p when it differs; returns 1 when it wrote. */
static int set_byte(volatile uint8_t *p, uint8_t v)
{
    if (*p == v)
        return 0;
    *p = v;
    return 1;
}

int unlockables_status(unlock_status *out)
{
    memset(out, 0, sizeof *out);

    uint32_t gd = gamedata_va();
    if (!gd)
        return 0;

    out->profile_loaded = 1;
    for (int i = 0; i < UNLOCK_HIDDEN_CHARACTERS; i++)
        if (*xb8(gd + k_character_flag_offsets[i]) == 1)
            out->characters_unlocked++;

    for (int slot = 0; slot < UNLOCK_CHARACTER_SLOTS; slot++) {
        uint32_t cap = costume_cap(slot);
        if (!cap)
            continue;
        out->costume_slots_total++;
        if (*xb8(gd + OFF_COSTUME_COUNTS + (uint32_t)slot) >= cap)
            out->costume_slots_maxed++;
    }

    for (int i = 0; i < UNLOCK_SYSTEM_VOICES; i++)
        if (*xb8(gd + OFF_VOICE_BONUS + (uint32_t)i) == 1)
            out->voices_unlocked++;
    return 1;
}

int unlockables_unlock_characters(void)
{
    uint32_t gd = gamedata_va();
    if (!gd)
        return -1;

    int changed = 0;
    for (int i = 0; i < UNLOCK_HIDDEN_CHARACTERS; i++) {
        volatile uint8_t *flag = xb8(gd + k_character_flag_offsets[i]);
        if (*flag != 1) {
            *flag = 1;
            changed++;
        }
    }
    return changed;
}

int unlockables_unlock_costumes(void)
{
    uint32_t gd = gamedata_va();
    if (!gd)
        return -1;

    int raised = 0;
    for (int slot = 0; slot < UNLOCK_CHARACTER_SLOTS; slot++) {
        uint32_t cap = costume_cap(slot);
        if (!cap)
            continue;
        volatile uint8_t *count = xb8(gd + OFF_COSTUME_COUNTS + (uint32_t)slot);
        if (*count < cap) {
            *count = (uint8_t)cap;
            raised++;
        }
    }
    return raised;
}

int unlockables_unlock_voices(void)
{
    uint32_t gd = gamedata_va();
    if (!gd)
        return -1;

    int changed = 0;
    for (int i = 0; i < VOICE_BYTES; i++)
        changed += set_byte(voice_byte(gd, i), 1);
    return changed;
}

int unlockables_wipe(void)
{
    uint32_t gd = gamedata_va();
    if (!gd)
        return -1;

    int changed = 0;
    for (int i = 0; i < UNLOCK_HIDDEN_CHARACTERS; i++)
        changed += set_byte(xb8(gd + k_character_flag_offsets[i]), 0);
    for (int slot = 0; slot < UNLOCK_CHARACTER_SLOTS; slot++)
        changed += set_byte(xb8(gd + OFF_COSTUME_COUNTS + (uint32_t)slot), UNLOCK_DEFAULT_COSTUMES);
    for (int i = 0; i < VOICE_BYTES; i++)
        changed += set_byte(voice_byte(gd, i), 0);
    return changed;
}
