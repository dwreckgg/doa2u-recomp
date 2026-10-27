/* Extras: unlock hidden characters, costumes and system voices in the loaded
 * profile's game-data block; changes are saved to ups.dat at the next frame. */
#ifndef DOA2U_EXTRAS_H
#define DOA2U_EXTRAS_H

#ifdef __cplusplus
extern "C" {
#endif

#define UNLOCK_HIDDEN_CHARACTERS 3
#define UNLOCK_CHARACTER_SLOTS   15
#define UNLOCK_SYSTEM_VOICES     5    /* Kasumi, Ayane, Ein, Raidou, Tengu */
#define UNLOCK_VOICE_PROGRESS    15   /* per-character Survival-clear bytes */
#define UNLOCK_DEFAULT_COSTUMES  2    /* a fresh profile's count per slot */

typedef struct unlock_status {
    int profile_loaded;
    int characters_unlocked;
    int costume_slots_maxed;
    int costume_slots_total;
    int voices_unlocked;
} unlock_status;

/* With no profile loaded, status returns 0 and the rest return -1; otherwise
 * the rest return how many values they changed. */
int extras_status(unlock_status *out);
int extras_unlock_characters(void);
int extras_unlock_costumes(void);
int extras_unlock_voices(void);
int extras_wipe(void);    /* resets everything above to fresh-profile values */
int doa2u_extras_take_save_request(void);   /* 1 once after a change */

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_EXTRAS_H */
