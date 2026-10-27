SMAA: Enhanced Subpixel Morphological Antialiasing
https://github.com/iryoku/smaa (master, fetched 2026-10-27), MIT licence (LICENSE.txt).
Files unchanged: SMAA.hlsl, Textures/AreaTex.h, Textures/SearchTex.h.
Used by src/d3d/d3d8_smaa.c (the "SMAA" video option). CMake embeds SMAA.hlsl as a
byte array at configure time (src/d3d/CMakeLists.txt).
