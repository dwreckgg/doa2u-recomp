# DOA2U guest addresses used by online play

Measured 2026-10-26 on branch `netplay` (build of 906b395 + Task 4) with the
RAM dumps and `netplay_memdiff` from Tasks 2 and 3. Dumps: guest 0..0x05000000,
tags name the screen. Queries are `netplay_memdiff classes NAME=dump,... NAME=...`
over the image range `00010000:0106DAE0` unless noted; `show` prints bytes.

| Name | Value | How found | Confirmed by |
|---|---|---|---|
| `GAME_MODE_VA` | `0x00878392` (byte) | classes versus={vs_single_a,vs_tag_a,vs_stage,vs_fight_a,results} sparring={spar_charsel} menu={menu,menu2}: 2 hits, 0x878392 = 5/9/2 | `show 878388 20` over 18 dumps: 1 title/profile, 2 mode menu, 5 Versus (all Versus screens incl. Tag and results), 9 Sparring; 146 readers in the generated code |
| `GAME_MODE_VERSUS` | 5 | as above | Tag Battle from the Versus submenu reads 5 too (tag_charsel..tag_late3) |
| `MODE_VA` | `0x00878391` (byte) | neighbour of the mode byte; `show` over 18 dumps | 0 title/fight/results, 1 mode menu + Versus submenu, 2 character select (Versus, Tag, Sparring), 3 stage select; 58 readers/writers (`CMP_NE(MEM8(0x878391), 2)` in the character-select code) |
| `SCRST_*` | same byte as MODE_VA: `SCRST_VS_SUBMENU` 1, `SCRST_CHARSEL` 2, `SCRST_STAGESEL` 3 | DOA2U keeps one byte for the menu screen where DOA3 had two; `np_game_screen_key()` folds it with the in-round flag | — |
| `MODE_FIGHT` / in-round flag | `0x008783C8` (byte) = 1 from the fight intro to the results | classes fight={vs_fight_a,vs_fight_b,vs_paused,results} other={8 menu dumps}, small values | `show` over 18 dumps: 1 in every fight/pause/results dump of both walks, 0 elsewhere; 6 references (`CMP_EQ(MEM8(0x8783C8), 5)` elsewhere, a state value) |
| `MODE_TITLE` | `0x00878392` = 1 (the game-mode byte itself at the title and profile screens) | `show 878388` | title and profile dumps both 1 |
| `TAG_FLAG_VA` | `0x0087D49D` (byte) | classes single={vs_single_a,vs_single_b,vs_fight_a,vs_stage} tag={vs_tag_a,vs_tag_b} over BSS: 0x87D49D 0/1 | second walk: 1 on the Tag character select, stage select, fight, pause and results; 255 readers. (0x8076E4 and 0x8083C4 are 1 only on the Tag character select: screen state, not the battle type.) |
| pause byte | `0x0080B55F` (byte) = 1 while paused | classes play={vs_fight_a,vs_fight_b} paused={vs_paused} over BSS | second walk: 1 only in tag_paused; 2 references |
| `POSE_VA(side)` | `0x00875C5C` (P1), `0x0087645C` (P2) (word) | classes --width 4 loading={vs_entry} settled={vs_single_a,vs_single_b} over the portrait bone banks: the bank's first row is identity (0x3F800000 at +0x3C) once the preview model is posed, 0 before | P2 word 0 on vs_entry, 0x3F800000 on vs_single_a and vs_tag_a, 0 on the mode menu |
| `POSE_EMPTY` | `0x00000000` | as above | — |
| `SAVE_VA` | `0x007EF8E0` | the game-data pointer at 0x87CFA8 reads 0x7FEAE0 in every dump; the Unlockables work established game data = profile + 0xF200 | the music volume byte lands inside this window (below); the profile-loaded byte 0x801A1C sits at the window's end (+0x1213C); the profile name "Dwreck" (UTF-16) appears at 0x7FAA88 (+0xB1A8). The on-disk ups.dat (74,056 bytes) is not stored verbatim: no 64-byte run of the file appears in memory, so `find` cannot confirm the length |
| `SAVE_LEN` | `0x00012148` (74,056, the size of ups.dat) | file size | the window ends at 0x801A28, just before the profile-loaded byte 0x801A1C? (no: 0x801A1C is inside at +0x1213C; see note) |
| `SAVE_VOL_MUSIC` | `+0xF21A` (0x7FEAFA) | Audio: Music Volume 80 -> 79: classes before={audio_before} after={audio_after} over the window: one hit, 0x7FEAFA 50 -> 4F | the live mirror 0x6A2548 changed 50 -> 4F too (data segment, not the image) |
| `SAVE_VOL_SE` | `+0xF21B` (0x7FEAFB) | Audio: S.E. Volume 80 -> 79 (dumps ctrl_before3 / se_after3): 0x7FEAFB 50 -> 4F, the music byte unchanged | the three volumes sit together: `2E 50 50 50` at +0xF219..+0xF21C (music, S.E., voice) |
| `SAVE_LAYOUT(p)` | `+0xF21E + 0x14 * p` (0x7FEAFE for port 0) | Controller 1 > Button Config: Type A -> Type B (ctrl_before3 / ctrl_typeB): words at 0x7FEB06..0x7FEB0C changed `02 01 04 03` -> `04 02 03 01`, the four words before them unchanged, nothing else in the window | the block repeats every 0x14 bytes for ports 0..3 (`05 00 00 00 06 00 07 00 02 00 01 00 04 00 03 00 \| 00 01 00 00`); Type A restored and re-dumped byte-identical (ctrl_restored) |
| `SAVE_LAYOUT_LEN` | 16 (the 8 button words) | as above | 16 = `NP_LAYOUT_MAX`, no change to the format |
| `SAVE_LAYSEL(p)` | `+0xF22E + 0x14 * p` (0x7FEB0E for port 0) | the byte after the 8 words went 0 -> 1 with Type A -> Type B | the selector; the three bytes after it stayed 0 |
| `SCRST_VA` note | DOA2U has one menu byte (0x878391) where DOA3 had a mode byte and a screen byte; the in-round flag 0x8783C8 plays the part of DOA3's mode 1 | — | the adapter's predicates are written from these two bytes (ruling in the ledger) |
| `MENU_PANEL_VA` | `0x00806458` (byte; the word at 0x806454 is that panel's tick) | the menu-screen byte 0x878391 keeps 2 after backing out of the character select to the Versus submenu (dumps x_sel 2, x_sub_back 2, x_tag_sub_back 2; it resets to 1 only at the mode menu), so a second signal was needed: classes charsel={x_sel,x_tag_sel,vs_single_a,vs_tag_a,spar_charsel,tag_charsel,vs_entry} rest={13 other dumps} found no single byte or dword; 0x806458 is 1 on the title, profile, mode menu, both submenus (fresh and after backing out), stage selects and fights, and 0 on every character select, the results and Settings | `np_game_at_charsel()` = mode 5, screen 2, not in a round, panel 0; `left_versus` also treats a live panel as the submenu; no direct readers in the generated code (reached through a pointer), so this one rests on the 20 dumps |
| preview pose note | the bone bank's first row stays 1.0f after leaving the character select (x_sub_back, x_menu_back, x_tag_sel) | `show 875C5C` | the barrier also waits for 10 frames without guest file I/O, which covers a re-entered select's preview load |

## Pause menu (Task 6, 2026-10-26)

| Name | VA | Values | Evidence |
|---|---|---|---|
| PAUSE_VA | 0x80B55F | 1 while the pause menu is up, 0 otherwise | dump `pausetest` (paused) 01; every fight dump of Task 4 00 |
| PAUSE_OWNER_VA | 0x878390 | port whose pause menu it is: 0 = Player 1, 1 = Player 2 | dump `pausetest` (PAUSED PLAYER1) 00; dumps `pause_p2_0/1` (PAUSED PLAYER2) 01; the pause-menu routine sub_000DB42B reads this byte three times |

With the one-controller mirror the mirrored Start press is taken by either pad (observed: Player 1 first, Player 2 on every later pause of the same match). On that menu only the owning pad's input counts; the other pad pressing the same keys makes the game show "Please reconnect the controller to controller port %d" and ignore the menu (reproduced with the mirror request file and no session).

## Fight and character-select flags (Task 6, 2026-10-26)

ROUND_VA (0x8783C8) is sticky after a pause-menu Quit: it stays 1 on the title, mode menu, Versus submenu and character select that follow, so a predicate built on it never sees the character select again. The predicates now use two flags found with `netplay_memdiff classes` over 9 fight dumps (incl. paused) against 5 character-select dumps (incl. the one after a Quit), confirmed on all 44 dumps of the day:

| Name | VA | Values | Evidence |
|---|---|---|---|
| SEL_FLAG_VA(p) | 0x852F9F + 0x70·p | 1 while player p's character-select panel is live, else 0 | 1 on every character select (Versus Single/Tag, Sparring, re-entered, after a Quit); 0 on the title, profile, mode menu, submenu, stage select, loads, fights, pause, results, and the first frame of the character select (`vs_entry`) |
| FIGHT_FLAG_VA | 0x852F45 | 0xFF from the fight intro through the results, 0 otherwise | FF in all fight/pause/late-round dumps and the results; 0 on every menu and character select, including after a Quit |

at_charsel = mode 5 and both SEL flags; in_fight = FIGHT flag 0xFF; screen_key low byte = fight | sel0<<1 | sel1<<2; left_versus uses these instead of the round byte.

## Sync regions added in Task 7 (2026-10-26)

Found by replaying recordings in fresh launches and diffing RAM dumps taken at the same session frame (`netplay_dump_at.on`, frames absolute or `f+N` from the first fight frame, `f-1` the frame before it) with `netplay_memdiff classes`. All regions are pointer-free (checked word by word across four dumps).

| Region | VA / len | What it is | Evidence |
|---|---|---|---|
| frames_a, frames_b | 0x6E3104, 0x87D56C / 4 | frames since boot | two fresh launches at session frame 2548 differed by the same 9 in both; the idle-pick code reads 0x87D56C |
| uitick0..5, uitick0b | 0x8509A0, 0x850A7C, 0x852FB0, 0x853020, 0x853090, 0x8A7F24, 0x8A7F8C / 4 | per-screen frame counters | same 9-frame offset |
| idle0, idle1 | 0x850A5C, 0x850B38 / 8 | the idle-animation countdown and last pick of the two select panels (records 0x8509A0 / 0x850A7C, stride 0xDC: +0xBE countdown, +0xC0/+0xC2 pick) | record-vs-replay dumps at frame 400 differed in exactly these words; sub_0005FE10 -> sub_0005D5E0 -> rand() |
| tickfine2, timerw0..2, timer_a, timer_b, uislots, misc_a..c | 0x850730, 0x7CA864, 0x7CA9AC, 0x7CA9FC, 0x87D5B4, 0x87D630, 0x87F2B0, 0x881B94, 0x8847E4, 0x88B0B0 | boot-relative counters and timer words | all offset by the boot difference in every pair of dumps |
| swapcnt_a, swapcnt_b | 0x3572E4, 0x357978 / 4 | the D3D device's swap counter (device 0x355500 +0x1DE4 / +0x2478) | the game reads it through sub_00344180 as its idle timer |
| boottab, bootf_a/b/c, cnt_a, flag_a | 0x83D37C / 0x4094, 0x85052C, 0x852F2C, 0x850698 / 0x60, 0x850878, 0x88F85C | boot-time constants | differ per launch, never change afterwards (fight frames 9, 10 and 300 identical within a run) |
| camlag | 0x87A900 / 0x478 | camera smoothing state | nine words differed one frame before the fight; the camera matrices (fightC1/fightD) parted on fight frame 10 and re-converged by frame 300 |
| fightB | 0x85FE90 / 0x3C00 (was 0x3200) | +0xA00: 0x863090..0x863A90 | equal at fight frame 9, 504 words changed by frame 300 and differed between recording and replay |
| round | 0x8783C8 / 1 (moved from k_extra) | sticky after a pause-menu Quit | see the fight-flag section |
| mt19937 | 0x8A5768 / 0x9C8 | the game's Mersenne Twister (624 state words + index; seeded by sub_0001FD20, drawn by sub_0001FF40 through sub_000D93E0/F0, ~40 call sites) | seeded at boot; three record/replay pairs with equal inputs and CRT rand parted at fight frames 10, 130 and 1290, camera first; its words look like pointers by chance (they point at zeros) |

Not carried: 0x850720 (the D3D fence number, host timing), the CRI counters in 0x355xxx/0x357xxx other than the swap counter.

The game-side rand() runs on the session's seed word for the primary fiber and the XAPI task fibers (recomp_manual.c): the select panels' idle picks drew different numbers on two machines while the game thread's seed word matched every frame.
