# Inputs for the rollback design

Measured 2026-10-27 with the dirty-page counter (Online tab, "Count dirty
pages", or `netplay_dirty.on` next to the executable). Every 120 frames the
counter copies guest RAM (addresses 0 to 0x05000000, 80 MB) at one frame and
compares it page by page at the next, so each sample is the number of 4 KB
pages one frame wrote. One Single match and one Tag match were played by the
request-file driver: menus, character select, stage select, then a fight that
starts with two seconds of no input and continues with both pads attacking
(one-controller mirror) until the KO.

## Pages written in one frame

| Screen | Samples | Min | Median | Max | Of which image / heap (median) |
|---|---|---|---|---|---|
| Title | 11 | 168 | 234 | 315 | 52 / 181 |
| Mode menus | 8 | 180 | 215 | 240 | 57 / 156 |
| Character select, Single | 8 | 239 | 242 | 284 | 52 / 188 |
| Character select, Tag | 12 | 192 | 199 | 404 | 47 / 151 |
| Stage select | 4 | 135 | 187 | 229 | 47 / 131 |
| Single fight, no input | 3 | 268 | 291 | 292 | 77 / 213 |
| Single fight, both attacking | 29 | 270 | 393 | 421 | 82 / 308 |
| Tag fight, no input | 4 | 175 | 180 | 318 | 55 / 130 |
| Tag fight, both attacking | 51 | 128 | 349 | 388 | 71 / 276 |

The image is 0 to 0x0106DAE0 (the executable's sections and statics), the
heap is everything from the stack's end up to 0x05000000; the runtime area
and the stack account for about one page a frame. A fight writes up to about
420 pages, 1.7 MB, a frame.

## Region tables

| Table | Regions | Bytes |
|---|---|---|
| Carried at the start and at transitions (`k_sync`) | 52 | 84,117 (82 KB, about 21 pages) |
| Hashed every frame only (`k_extra`) | 4 | 340 |

## Facts the rollback spec starts from

1. Guest RAM is a file mapping, so Windows write-watch (`GetWriteWatch`)
   cannot report dirty pages; finding them costs a copy and a compare, or a
   protection-fault scheme.
2. A frame writes 130 to 420 pages, 0.5 to 1.7 MB. Saving every written page
   per frame is 20 to 80 times the 82 KB the region tables carry; the
   region tables are complete enough for whole-match replays from a fresh
   launch (Task 7), not proven complete for state written outside them.
3. Most written pages are heap pages (130 to 310 a frame); the image
   writes 36 to 82.
4. Lockstep sessions skip every movie (movie playback stalls on the
   session's frame-counted clock), so rollback never has to save or replay
   decoder state.
