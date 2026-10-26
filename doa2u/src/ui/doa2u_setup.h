/**
 * First-run setup overlay (Dear ImGui): picks the DOA2U disc image and extracts it into assets.
 * Owns its own D3D11 device and ImGui context, torn down before the game's device is made.
 */

#ifndef DOA2U_SETUP_H
#define DOA2U_SETUP_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Run the setup screen on `hwnd` until the assets are installed (returns 1)
 * or the user quits / closes the window (returns 0). */
int doa2u_setup_run(HWND hwnd, const WCHAR *assets_dir);

/* Window messages while setup is up. Returns non-zero when consumed. */
int doa2u_setup_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

#ifdef __cplusplus
}
#endif

#endif /* DOA2U_SETUP_H */
