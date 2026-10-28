# Deterministic math

Rollback netplay between two PCs needs both games to compute the same bits. The C
runtime's transcendental functions live in `ucrtbase.dll`, which is part of Windows:
they change between Windows releases and choose a code path by processor (FMA3).

The `.c` and `*_data.h` files here are musl 1.2.5's `src/math` (fdlibm and Arm's
optimized-routines inside), unchanged; `COPYRIGHT.musl` is musl's license (MIT).
`features.h` is included first in each of them (`cmake/XbeRecomp.cmake`): it gives every
function and table a `detmath_` name, so they sit beside the C runtime's own, and turns
off fused multiply-add. `libm.h` stands in for musl's internal one.

The executable passes them to the game code for the x87 instructions
(`cpu/dispatch.cpp`: `sin`, `cos`, `tan`, `atan2`, `log2`, `exp2`) and uses them for the
C runtime functions it replaces (`xdk/crt_math.cpp`). `sqrt`, `fmod`, `ldexp`, `frexp`,
`floor` and `scalbn` are exact by definition and stay the C runtime's.
