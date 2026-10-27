/* NV2A fixed-function vertex path on the GPU.
 *
 * The pgraph translator used to transform, light and near-clip every array
 * vertex on the CPU and hand the D3D8 layer screen-space XYZRHW. This path
 * takes the packed object-space vertices instead, with the per-draw constants
 * the CPU math used, and a dedicated vertex shader does the same arithmetic:
 *   clip = Clip * pos          (composite x viewport origin x window fit x z range,
 *                               folded into one matrix by the translator)
 *   colour = vertex colour | NV2A fixed-function lighting | ambient+emission | white
 *   clip distances = (w - 0.01, z)   -- the two planes the CPU clipper used
 */
#ifndef D3D8_NV2AFF_H
#define D3D8_NV2AFF_H

#include <stdint.h>

/* One packed vertex: 40 bytes. */
typedef struct {
    float    pos[4];
    float    nrm[3];
    uint32_t color;      /* D3DCOLOR (A8R8G8B8) */
    float    uv[2];
} Nv2aFFVertex;

#define NV2AFF_FLAG_LIT          0x01u
#define NV2AFF_FLAG_AMBIENT_ONLY 0x02u
#define NV2AFF_FLAG_HAS_COLOR    0x04u
#define NV2AFF_FLAG_FOLD         0x08u   /* multiply the colour by `fold` (combiner factor) */

/* Constant buffer, HLSL float4-packed. Keep in step with the shader. */
typedef struct {
    float    clip[4][4];      /* row i: coefficients of clip component i */
    float    mv[3][4];        /* model-view rows (xyz coefficients + translation) */
    float    light_dir[4][4]; /* xyz: normalised direction (type 1) or eye-space position; w: type 0/1/2 */
    float    light_amb[4][4];
    float    light_dif[4][4];
    float    light_att[4][4]; /* a0, a1, a2, range */
    float    ambemis[4];      /* scene ambient + emission; w = material alpha */
    float    fold[4];         /* combiner factor r,g,b,a in 0..255 */
    uint32_t flags;
    uint32_t pad[3];
} Nv2aFFConstants;

/* Texture-shader draws: FF transform/lighting plus four stages of texgen and
 * the specular colour, for the NV2A pixel shader. 100 bytes. */
typedef struct {
    float    pos[4];
    float    nrm[3];
    uint32_t color;      /* D3DCOLOR */
    uint32_t spec;       /* D3DCOLOR */
    float    tex[4][4];  /* raw stage texcoords, unset components (0,0,0,1) */
} Nv2aTSVertex;

#define NV2ATS_NRM_OK    0x10u   /* Flags: a normal is supplied for reflection texgen */

#define NV2ATS_S_EYEPROJ 0x01u   /* stage flags: eye-linear texgen + texture matrix */
#define NV2ATS_S_EYELIN0 0x02u   /* << c: component c takes its eye-linear plane */
#define NV2ATS_S_REFLECT 0x20u   /* REFLECTION_MAP texgen */
#define NV2ATS_S_TEXMTX  0x40u   /* texture matrix on (reflection path) */

typedef struct {
    Nv2aFFConstants ff;
    float    plane[4][4][4];  /* [stage][component]: eye-linear plane */
    float    mtx[4][4][4];    /* [stage][row]: texture matrix */
    float    scale[4][4];     /* [stage]: s, t texel scale for projected linear targets */
    uint32_t sflags[4];
} Nv2aTSConstants;

/* d3d8_device.c: upload, bind and draw. `topology` is a D3D11_PRIMITIVE_TOPOLOGY. */
long d3d8_DrawNv2aFF(const Nv2aFFVertex *verts, unsigned nverts,
                     const uint16_t *idx, unsigned nidx, int topology,
                     const Nv2aFFConstants *cb);
long d3d8_DrawNv2aTS(const Nv2aTSVertex *verts, unsigned nverts,
                     const uint16_t *idx, unsigned nidx, int topology,
                     const Nv2aTSConstants *cb);

/* Raw vertex arrays: guest attribute bytes decoded by the input assembler. Elements:
 * FF = POSITION, NORMAL, COLOR0, TEXCOORD0; TS adds COLOR1 and TEXCOORD0..3. */
enum {
    NV2ARAW_F_R32 = 1, NV2ARAW_F_R32G32, NV2ARAW_F_R32G32B32, NV2ARAW_F_R32G32B32A32,
    NV2ARAW_F_R8, NV2ARAW_F_R8G8, NV2ARAW_F_R8G8B8A8, NV2ARAW_F_B8G8R8A8,
    NV2ARAW_F_S16, NV2ARAW_F_S16S16, NV2ARAW_F_S16X4
};
#define NV2ARAW_MAX_STREAMS 8
#define NV2ARAW_DEFAULTS    0xFF   /* stream id of the constant defaults: +0 = (0,0,0,0), +16 = (0,0,0,1) */
typedef struct {
    const void *src;     /* host pointer to the first byte used */
    unsigned    size;    /* bytes */
    unsigned    stride;
} Nv2aRawStream;
typedef struct {
    uint8_t  stream;     /* index into the streams, or NV2ARAW_DEFAULTS */
    uint8_t  fmt;        /* NV2ARAW_F_* */
    uint16_t offset;     /* byte offset inside the stream's vertex */
} Nv2aRawElem;

long d3d8_DrawNv2aRaw(int ts, const Nv2aRawStream *streams, unsigned nstreams, const Nv2aRawElem *elems,
                      const uint16_t *idx, unsigned nidx, int topology, const void *cb);

/* d3d8_shaders.c: compile once; bind VS + layout + constants for one draw. */
long d3d8_Nv2aFF_Init(void);
void d3d8_Nv2aFF_Shutdown(void);
int  d3d8_Nv2aFF_Bind(const Nv2aFFConstants *cb);
int  d3d8_Nv2aTS_Bind(const Nv2aTSConstants *cb);
/* The raw-array input layout for this element set (cached); NULL if it cannot be built. */
void *d3d8_Nv2aRaw_Layout(int ts, const Nv2aRawElem *elems, unsigned defaults_slot);

/* NV2A texture shader + register combiners as one pixel shader, keyed on the
 * raw pushbuffer registers (stage program, combiner words, alpha test). */
typedef struct {
    uint32_t shader_prog, other_input, comb_control;
    uint32_t c_icw[8], c_ocw[8], a_icw[8], a_ocw[8];
    uint32_t final0, final1;
    uint32_t alpha_test, alpha_func;
    uint32_t dot_map;              /* NV097_SET_DOT_RGBMAPPING */
} Nv2aPSKey;

typedef struct {
    float    c0[8][4], c1[8][4];   /* per-stage factors, rgba 0..1 */
    float    fog[4];               /* fog colour rgb, factor in a */
    float    bump[4][4];           /* per stage: m00 m01 m10 m11 */
    float    alpha_ref;
    float    dot_st_scale[2];      /* DOT_ST result -> 0..1 (1/rect size for a linear texture) */
    float    pad;
} Nv2aPSConsts;

/* d3d8_combiners.c: arm the shader for the next draw; 0 if it cannot be built. */
int  d3d8_Nv2aPS_Arm(const Nv2aPSKey *key, const Nv2aPSConsts *cb);
void d3d8_Nv2aPS_Disarm(void);

#endif
