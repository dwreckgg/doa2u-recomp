/* Host-test stand-ins for the clocks (kernel/xbox_det.c), the audio chip
 * (apu/apu_core.c) and the settings path (ui/video_settings.c), counting
 * the calls the session core makes. */
#include "kernel/xbox_det.h"
#include "netplay_stubs.h"

int g_det_begin, g_det_end, g_det_frames, g_apu_begin, g_apu_end;
uint32_t g_io;

void     xbox_det_current_bases(xbox_det_bases *b) { b->tick_ms = 1000; b->qpc = 2000; b->filetime = 3000; }
void     xbox_det_begin(const xbox_det_bases *b) { (void)b; g_det_begin++; g_det_frames = 0; }
void     xbox_det_end(void) { g_det_end++; }
void     xbox_det_frame(void) { g_det_frames++; }
uint32_t xbox_det_tick_ms(void) { return 1000u + (uint32_t)g_det_frames * 1000u / 60u; }
uint64_t xbox_det_filetime(void) { return 3000u + (uint64_t)g_det_frames * 166667u; }
uint32_t xbox_det_io_count(void) { return g_io; }
uint32_t xbox_det_kcalls(void) { return 0; }
uint64_t xbox_det_boost(void) { return 0; }

void     mcpx_apu_det_begin(uint64_t xgscnt_base) { (void)xgscnt_base; g_apu_begin++; }
uint64_t mcpx_apu_xgscnt_now(void) { return 4000; }
void     mcpx_apu_det_end(void) { g_apu_end++; }
void     mcpx_apu_det_frame(void) {}

/* online_settings.c reads Diag from here: a file that does not exist, so
 * every setting is its default (diagnostics off). */
const char *doa2u_settings_ini_path(void) { return "C:\\nonexistent_doa2u_dir\\none.ini"; }
