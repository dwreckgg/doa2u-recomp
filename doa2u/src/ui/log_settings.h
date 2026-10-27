/**
 * Log files on/off, persisted as Logging= in the [General] section of
 * doa2u_settings.ini. Off by default: stderr goes to NUL and xbox_kernel.log is
 * not written. On: stderr goes to doa2u_log.txt and the kernel layer writes
 * xbox_kernel.log, both next to the working directory as before.
 *
 * A stderr the launcher redirected (a shell `2> file`) is left alone either
 * way, so a redirected run still captures everything.
 */
#ifndef DOA2U_LOG_SETTINGS_H
#define DOA2U_LOG_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read the setting and route stderr. Call once, before anything is logged. */
void doa2u_log_init(void);

int  doa2u_log_enabled(void);
/* Takes effect immediately; turning it on starts fresh log files. */
void doa2u_log_set_enabled(int on);
/* Returns non-zero on success. */
int  doa2u_log_save(void);

/* FMV playback, persisted as Movies= in [General] (default on). Off: every
 * movie reports finished as soon as it starts, as if it played to its end. */
int  doa2u_movies_enabled(void);
void doa2u_movies_set_enabled(int on);
int  doa2u_movies_save(void);

/* 5.1 surround sound, persisted as Surround= in [General] (default off).
 * The game reads its speaker setup at boot and the audio device opens then,
 * so doa2u_surround_active() is the value this run started with; a change
 * applies at the next start. */
int  doa2u_surround_enabled(void);
void doa2u_surround_set_enabled(int on);
int  doa2u_surround_save(void);
int  doa2u_surround_active(void);

/* Room reverb (the game's I3DL2 settings on the host), persisted as Reverb= in
 * [General] (default on). Takes effect immediately. */
int  doa2u_reverb_enabled(void);
void doa2u_reverb_set_enabled(int on);
int  doa2u_reverb_save(void);

/* FPS counter, persisted as FpsCounter= (0/1) and FpsCorner= in [General]. */
enum { FPS_TOP_LEFT, FPS_TOP_RIGHT, FPS_BOTTOM_LEFT, FPS_BOTTOM_RIGHT };
int  doa2u_fps_enabled(void);
void doa2u_fps_set_enabled(int on);
int  doa2u_fps_corner(void);
void doa2u_fps_set_corner(int corner);
int  doa2u_fps_save(void);

/* Performance monitor, persisted as PerfMonitor= in [General] (default off).
 * Setting it starts or stops measuring and doa2u_perf.log at once. */
int  doa2u_perfmon_enabled(void);
void doa2u_perfmon_set_enabled(int on);
/* Log threshold in ms, persisted as PerfThreshold= (default 20). */
int  doa2u_perfmon_threshold(void);
void doa2u_perfmon_set_threshold(int ms);
int  doa2u_perfmon_save(void);

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_LOG_SETTINGS_H */
