# Deterministic math

Online play runs both games in lockstep, so both PCs must compute the same bits. The C
runtime's transcendental functions (`sin`, `cos`, `pow`, ...) live in `ucrtbase.dll`, which
is part of Windows: they change between Windows releases and choose a code path by
processor (FMA3). Two copies on one PC agree; two PCs can differ in the last bit, and
lockstep turns that into a desync.

The `.c` and `*_data.h` files here are musl 1.2.5's `src/math` (fdlibm and Arm's
optimized-routines inside), unchanged; `COPYRIGHT.musl` is musl's licence (MIT).
`features.h` is force-included first in each of them (`src/game/CMakeLists.txt`): it gives
every function and table a `detmath_` name, so they sit beside the C runtime's own, and
turns off fused multiply-add. `libm.h` stands in for musl's internal one.

The game code (the recompiled C sources) is compiled with `src/game/detmath_redirect.h`
force-included, which sends its `sin`, `cos`, `tan`, `atan2`, `log` and `pow` here. `sqrt`,
`fmod` and `fabs` are exact by definition and stay the C runtime's.

Taken from Jstackzz/MK-3D-rollback (MIT), `recomp/patches/xboxxberecomp-0015-deterministic-math.patch`
(its README is kept as `README.mk3d.md`). Changes for DOA2U: `detmath.h` is plain C with a
C++ guard; `features.h` adds MSVC spellings (`__builtin_expect`, `#pragma fp_contract(off)`).
