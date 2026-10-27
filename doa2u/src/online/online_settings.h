/* [Online] in doa2u_settings.ini: online play's settings.
 *
 *   Port=7000        UDP port to host on, 7000..7015 (the join code's range)
 *   DelayAuto=1      input delay from the ping
 *   Delay=2          manual input delay, 1..8 frames
 *   LastJoin=        the last code joined, up to 63 characters
 *   Diag=0           diagnostic logs (events, digest logs, desync report)
 *   ShowStats=0      the ping / input delay box over the game while connected
 *                    (F9); written as soon as it is toggled
 *
 * Loads on first use; anything out of range reads as its default. Save
 * writes every key. */
#ifndef DOA2U_ONLINE_SETTINGS_H
#define DOA2U_ONLINE_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define ONLINE_PORT_DEFAULT 7000u
#define ONLINE_PORT_COUNT   16u
#define ONLINE_DELAY_DEFAULT 2
#define ONLINE_LAST_JOIN_MAX 64     /* buffer, including the terminator */

int  online_settings_load(void);   /* re-reads the ini; 1 when the file exists */
int  online_settings_save(void);   /* 1 on success */
/* Tests only: redirect to another ini; NULL restores doa2u_settings_ini_path(). */
void online_settings_set_ini_path(const char *path);

unsigned    online_get_port(void);
void        online_set_port(unsigned port);
int         online_get_delay_auto(void);
int         online_get_delay(void);
void        online_set_delay(int automatic, int manual);
const char *online_get_last_join(void);
void        online_set_last_join(const char *code);
int         online_get_diag(void);
void        online_set_diag(int on);
int         online_get_show_stats(void);
void        online_set_show_stats(int on);

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_ONLINE_SETTINGS_H */
