/* Unlockables: hidden characters and costumes.
 *
 * DOA2U keeps both in the player's profile (ups.dat). The profile's trailing
 * "game data" block (profile + 0xF200) is reached through the global pointer
 * at 0x87CFA8, which the game sets when the profile system comes up:
 *
 *   +0x2ED2 / +0x2ED3 / +0x2ED4   byte, 1 = hidden character available
 *                                 (character ids 0x11, 0x18, 0x15; the game's
 *                                 own check is sub_0011E5F0)
 *   +0x2ED8[slot]                 byte per character slot, number of costumes
 *                                 unlocked. The 15 slots map to character ids
 *                                 through the byte list at 0x6959DC, and the
 *                                 character's costume total is the byte at
 *                                 0x6E0F08[id] (sub_0011E5E0). The game's own
 *                                 unlock routine (sub_000DC640) raises the
 *                                 count to that total; the character select
 *                                 shows min(count, total) (sub_0011E41B).
 *                                 (0x695AF0 is only the story-mode target.)
 *   +0x2EE7..+0x2EEB             five bytes, 1 = that System Voice is selectable
 *                                 in Settings: Kasumi, Ayane, Ein, Tengu, Raidou
 *                                 (the option builder sub_0011CE58 reads them
 *                                 through sub_00021440..80; Miyako, Monica and
 *                                 DJ are always available). The post-Survival
 *                                 routine sub_000DC4C0 sets them and queues the
 *                                 "The %s System Voice has been unlocked!" notice.
 *   +0x2EF8[slot]                 byte per character slot, 1 = Survival cleared
 *                                 with that character; all fifteen together
 *                                 unlock the Raidou voice. Set alongside the
 *                                 voices so the game's own bookkeeping agrees.
 *
 * The block exists from boot and holds defaults until a profile is picked;
 * the byte at 0x801A1C is 1 once one is loaded, and nothing here writes
 * before that.
 *
 * A fresh profile (captured before anything was unlocked) has the three
 * character flags at 0, every costume count at 2 and every voice byte at 0;
 * the wipe restores exactly those and nothing else.
 *
 * The buttons write the live block; the game writes it to ups.dat the next
 * time it saves the profile. Nothing here touches the file.
 */
#ifndef DOA2U_UNLOCKABLES_H
#define DOA2U_UNLOCKABLES_H

#ifdef __cplusplus
extern "C" {
#endif

#define UNLOCK_HIDDEN_CHARACTERS 3
#define UNLOCK_CHARACTER_SLOTS   15
#define UNLOCK_SYSTEM_VOICES     5    /* Kasumi, Ayane, Ein, Raidou, Tengu   */
#define UNLOCK_VOICE_PROGRESS    15   /* per-character Survival-clear bytes  */
#define UNLOCK_DEFAULT_COSTUMES  2    /* a fresh profile's count per slot */

typedef struct unlock_status {
    int profile_loaded;      /* 0 when the game-data pointer is not set yet */
    int characters_unlocked; /* hidden characters already available, 0..3 */
    int costume_slots_maxed; /* character slots whose costume count is at the cap */
    int costume_slots_total; /* slots with a sane cap in the game's table */
    int voices_unlocked;     /* unlockable system voices available, 0..5 */
} unlock_status;

/* Fills *out. Returns 0 (and zeroes *out) when no profile block is reachable. */
int unlockables_status(unlock_status *out);

/* Sets the three hidden-character flags. Returns how many were newly set,
 * or -1 when no profile block is reachable. */
int unlockables_unlock_characters(void);

/* Raises every character's costume count to the game's cap for that slot.
 * Never lowers a count. Returns how many slots were raised, or -1 when no
 * profile block is reachable. */
int unlockables_unlock_costumes(void);

/* Sets the five voice bytes and the fifteen progress bytes behind them.
 * Returns how many bytes were newly set, or -1 when no profile block is
 * reachable. */
int unlockables_unlock_voices(void);

/* Resets the hidden-character flags, costume counts and system voice bytes to
 * a fresh profile's values. Touches nothing else. Returns how many bytes it
 * changed, or -1 when no profile block is reachable. */
int unlockables_wipe(void);

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_UNLOCKABLES_H */
