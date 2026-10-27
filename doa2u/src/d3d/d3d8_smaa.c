/**
 * SMAA 1x (third_party/smaa, MIT) on the resolved guest frame, before the
 * present blit scales it into the window: edge detection, blending weights,
 * neighbourhood blending, each a full-screen triangle at the guest frame's
 * size. Works on top of MSAA or alone. The shader is SMAA.hlsl as published,
 * embedded by CMake (g_smaa_hlsl), between a prefix that sets the API and
 * preset and a suffix with the entry points.
 *
 * Every pipeline slot the passes touch is saved and restored: the pgraph
 * translator does not re-send state it believes is still bound.
 */
#define COBJMACROS
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../third_party/smaa/AreaTex.h"
#include "../../third_party/smaa/SearchTex.h"

extern const unsigned char g_smaa_hlsl[];
extern const unsigned g_smaa_hlsl_size;

static const char k_prefix[] =
    "#define SMAA_HLSL_4_1 1\n"
    "#define SMAA_PRESET_ULTRA 1\n"
    "cbuffer SmaaMetrics : register(b0) { float4 g_smaa_rt; };\n"
    "#define SMAA_RT_METRICS g_smaa_rt\n";

static const char k_suffix[] =
    "\nTexture2D colorTex : register(t0);\n"
    "Texture2D edgesTex : register(t1);\n"
    "Texture2D blendTex : register(t2);\n"
    "Texture2D areaTex : register(t3);\n"
    "Texture2D searchTex : register(t4);\n"
    "void smaa_fs(uint id, out float4 pos, out float2 uv) {\n"
    "  uv = float2((id << 1) & 2, id & 2); pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }\n"
    "void EdgeVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0, out float4 off[3] : TEXCOORD1) {\n"
    "  smaa_fs(id, pos, uv); SMAAEdgeDetectionVS(uv, off); }\n"
    "float2 EdgePS(float4 pos : SV_Position, float2 uv : TEXCOORD0, float4 off[3] : TEXCOORD1) : SV_Target {\n"
    "  return SMAAColorEdgeDetectionPS(uv, off, colorTex); }\n"
    "void BlendVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0, out float2 pix : TEXCOORD1, out float4 off[3] : TEXCOORD2) {\n"
    "  smaa_fs(id, pos, uv); SMAABlendingWeightCalculationVS(uv, pix, off); }\n"
    "float4 BlendPS(float4 pos : SV_Position, float2 uv : TEXCOORD0, float2 pix : TEXCOORD1, float4 off[3] : TEXCOORD2) : SV_Target {\n"
    "  return SMAABlendingWeightCalculationPS(uv, pix, off, edgesTex, areaTex, searchTex, float4(0, 0, 0, 0)); }\n"
    "void NbVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0, out float4 off : TEXCOORD1) {\n"
    "  smaa_fs(id, pos, uv); SMAANeighborhoodBlendingVS(uv, off); }\n"
    "float4 NbPS(float4 pos : SV_Position, float2 uv : TEXCOORD0, float4 off : TEXCOORD1) : SV_Target {\n"
    "  return SMAANeighborhoodBlendingPS(uv, off, colorTex, blendTex); }\n";

typedef struct {
    ID3D11VertexShader *vs;
    ID3D11PixelShader  *ps;
    UINT linear_slot, point_slot;       /* where SMAA's samplers landed in this pixel shader */
} SmaaPass;

static SmaaPass g_pass[3];
static ID3D11Texture2D *g_area_tex, *g_search_tex;
static ID3D11ShaderResourceView *g_area_srv, *g_search_srv;
static ID3D11SamplerState *g_linear, *g_point;
static ID3D11Buffer *g_metrics;
static ID3D11RasterizerState *g_rs;
static int g_failed;

/* Targets at the frame's size: edges (RG), weights (RGBA), the result. */
static struct { ID3D11Texture2D *tex; ID3D11RenderTargetView *rtv; ID3D11ShaderResourceView *srv; } g_rt[3];
static UINT g_w, g_h;

static void sampler_slots(ID3DBlob *ps, SmaaPass *p)
{
    ID3D11ShaderReflection *refl = NULL;
    D3D11_SHADER_INPUT_BIND_DESC bd;
    p->linear_slot = 0;
    p->point_slot = 1;
    if (FAILED(D3DReflect(ID3D10Blob_GetBufferPointer(ps), ID3D10Blob_GetBufferSize(ps),
                          &IID_ID3D11ShaderReflection, (void **)&refl)))
        return;
    if (SUCCEEDED(refl->lpVtbl->GetResourceBindingDescByName(refl, "LinearSampler", &bd))) p->linear_slot = bd.BindPoint;
    if (SUCCEEDED(refl->lpVtbl->GetResourceBindingDescByName(refl, "PointSampler", &bd))) p->point_slot = bd.BindPoint;
    refl->lpVtbl->Release(refl);
}

static int compile_pass(ID3D11Device *dev, const char *src, size_t len, const char *vs, const char *ps, SmaaPass *p)
{
    ID3DBlob *vb = NULL, *pb = NULL, *err = NULL;
    int ok = 0;
    if (FAILED(D3DCompile(src, len, "smaa", NULL, NULL, vs, "vs_4_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vb, &err)) ||
        FAILED(D3DCompile(src, len, "smaa", NULL, NULL, ps, "ps_4_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pb, &err))) {
        fprintf(stderr, "[D3D] SMAA shader %s/%s failed: %s\n", vs, ps,
                err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
        goto done;
    }
    if (FAILED(ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vb), ID3D10Blob_GetBufferSize(vb), NULL, &p->vs)) ||
        FAILED(ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(pb), ID3D10Blob_GetBufferSize(pb), NULL, &p->ps)))
        goto done;
    sampler_slots(pb, p);
    ok = 1;
done:
    if (vb) ID3D10Blob_Release(vb);
    if (pb) ID3D10Blob_Release(pb);
    if (err) ID3D10Blob_Release(err);
    return ok;
}

static int make_tex(ID3D11Device *dev, UINT w, UINT h, DXGI_FORMAT fmt, const void *data, UINT pitch,
                    ID3D11Texture2D **tex, ID3D11ShaderResourceView **srv)
{
    D3D11_TEXTURE2D_DESC td;
    D3D11_SUBRESOURCE_DATA sd;
    memset(&td, 0, sizeof td);
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    sd.pSysMem = data; sd.SysMemPitch = pitch; sd.SysMemSlicePitch = 0;
    return SUCCEEDED(ID3D11Device_CreateTexture2D(dev, &td, &sd, tex)) &&
           SUCCEEDED(ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)*tex, NULL, srv));
}

static int smaa_init(ID3D11Device *dev)
{
    char *src;
    size_t len;
    D3D11_SAMPLER_DESC sd;
    D3D11_BUFFER_DESC bd;
    D3D11_RASTERIZER_DESC rd;
    int ok;
    if (g_pass[2].ps) return 1;
    if (g_failed) return 0;
    g_failed = 1;
    len = sizeof(k_prefix) - 1 + g_smaa_hlsl_size + sizeof(k_suffix) - 1;
    src = (char *)malloc(len + 1);
    if (!src) return 0;
    memcpy(src, k_prefix, sizeof(k_prefix) - 1);
    memcpy(src + sizeof(k_prefix) - 1, g_smaa_hlsl, g_smaa_hlsl_size);
    memcpy(src + sizeof(k_prefix) - 1 + g_smaa_hlsl_size, k_suffix, sizeof(k_suffix) - 1);
    src[len] = 0;
    ok = compile_pass(dev, src, len, "EdgeVS", "EdgePS", &g_pass[0]) &&
         compile_pass(dev, src, len, "BlendVS", "BlendPS", &g_pass[1]) &&
         compile_pass(dev, src, len, "NbVS", "NbPS", &g_pass[2]);
    free(src);
    if (!ok) return 0;
    if (!make_tex(dev, AREATEX_WIDTH, AREATEX_HEIGHT, DXGI_FORMAT_R8G8_UNORM, areaTexBytes, AREATEX_PITCH, &g_area_tex, &g_area_srv) ||
        !make_tex(dev, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, DXGI_FORMAT_R8_UNORM, searchTexBytes, SEARCHTEX_PITCH, &g_search_tex, &g_search_srv))
        return 0;
    memset(&sd, 0, sizeof sd);
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    if (FAILED(ID3D11Device_CreateSamplerState(dev, &sd, &g_linear))) return 0;
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (FAILED(ID3D11Device_CreateSamplerState(dev, &sd, &g_point))) return 0;
    memset(&bd, 0, sizeof bd);
    bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(ID3D11Device_CreateBuffer(dev, &bd, NULL, &g_metrics))) return 0;
    memset(&rd, 0, sizeof rd);
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    if (FAILED(ID3D11Device_CreateRasterizerState(dev, &rd, &g_rs))) return 0;
    g_failed = 0;
    fprintf(stderr, "[D3D] SMAA ready (preset ultra)\n");
    return 1;
}

static void free_targets(void)
{
    for (int i = 0; i < 3; i++) {
        if (g_rt[i].srv) { ID3D11ShaderResourceView_Release(g_rt[i].srv); g_rt[i].srv = NULL; }
        if (g_rt[i].rtv) { ID3D11RenderTargetView_Release(g_rt[i].rtv); g_rt[i].rtv = NULL; }
        if (g_rt[i].tex) { ID3D11Texture2D_Release(g_rt[i].tex); g_rt[i].tex = NULL; }
    }
    g_w = g_h = 0;
}

static int ensure_targets(ID3D11Device *dev, UINT w, UINT h)
{
    static const DXGI_FORMAT fmt[3] = { DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM };
    if (g_rt[2].srv && g_w == w && g_h == h) return 1;
    free_targets();
    for (int i = 0; i < 3; i++) {
        D3D11_TEXTURE2D_DESC td;
        memset(&td, 0, sizeof td);
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt[i];
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &g_rt[i].tex)) ||
            FAILED(ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)g_rt[i].tex, NULL, &g_rt[i].rtv)) ||
            FAILED(ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)g_rt[i].tex, NULL, &g_rt[i].srv))) {
            free_targets();
            return 0;
        }
    }
    g_w = w; g_h = h;
    return 1;
}

/* Runs SMAA on `color` (w x h); returns the view of the result, or NULL to
 * present `color` as it is. */
ID3D11ShaderResourceView *d3d8_smaa_apply(ID3D11Device *dev, ID3D11DeviceContext *ctx,
                                          ID3D11ShaderResourceView *color, UINT w, UINT h)
{
    /* saved state */
    ID3D11RenderTargetView *rtv = NULL; ID3D11DepthStencilView *dsv = NULL;
    ID3D11VertexShader *vs = NULL; ID3D11PixelShader *ps = NULL;
    ID3D11ShaderResourceView *srv[5] = {0};
    ID3D11SamplerState *smp[4] = {0};
    ID3D11Buffer *vcb = NULL, *pcb = NULL;
    ID3D11RasterizerState *rs = NULL; ID3D11BlendState *bs = NULL; FLOAT bf[4]; UINT bmask;
    ID3D11DepthStencilState *dss = NULL; UINT sref;
    ID3D11InputLayout *il = NULL; D3D11_PRIMITIVE_TOPOLOGY topo;
    D3D11_VIEWPORT vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    const FLOAT zero[4] = { 0, 0, 0, 0 };
    D3D11_VIEWPORT vp;
    float metrics[4];
    ID3D11ShaderResourceView *none[5] = {0};

    if (!dev || !ctx || !color || !w || !h || !smaa_init(dev) || !ensure_targets(dev, w, h)) return NULL;

    ID3D11DeviceContext_OMGetRenderTargets(ctx, 1, &rtv, &dsv);
    ID3D11DeviceContext_VSGetShader(ctx, &vs, NULL, NULL);
    ID3D11DeviceContext_PSGetShader(ctx, &ps, NULL, NULL);
    ID3D11DeviceContext_PSGetShaderResources(ctx, 0, 5, srv);
    ID3D11DeviceContext_PSGetSamplers(ctx, 0, 4, smp);
    ID3D11DeviceContext_VSGetConstantBuffers(ctx, 0, 1, &vcb);
    ID3D11DeviceContext_PSGetConstantBuffers(ctx, 0, 1, &pcb);
    ID3D11DeviceContext_RSGetState(ctx, &rs);
    ID3D11DeviceContext_RSGetViewports(ctx, &nvp, vps);
    ID3D11DeviceContext_OMGetBlendState(ctx, &bs, bf, &bmask);
    ID3D11DeviceContext_OMGetDepthStencilState(ctx, &dss, &sref);
    ID3D11DeviceContext_IAGetInputLayout(ctx, &il);
    ID3D11DeviceContext_IAGetPrimitiveTopology(ctx, &topo);

    metrics[0] = 1.0f / (float)w; metrics[1] = 1.0f / (float)h; metrics[2] = (float)w; metrics[3] = (float)h;
    ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)g_metrics, 0, NULL, metrics, 0, 0);
    vp.TopLeftX = 0; vp.TopLeftY = 0; vp.Width = (FLOAT)w; vp.Height = (FLOAT)h; vp.MinDepth = 0; vp.MaxDepth = 1;
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_RSSetState(ctx, g_rs);
    ID3D11DeviceContext_OMSetBlendState(ctx, NULL, NULL, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx, NULL, 0);
    ID3D11DeviceContext_IASetInputLayout(ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &g_metrics);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &g_metrics);

    for (int p = 0; p < 3; p++) {
        ID3D11ShaderResourceView *in[5];
        ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 5, none);          /* nothing bound as both */
        ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &g_rt[p].rtv, NULL);
        ID3D11DeviceContext_ClearRenderTargetView(ctx, g_rt[p].rtv, zero);
        in[0] = color; in[1] = g_rt[0].srv; in[2] = g_rt[1].srv; in[3] = g_area_srv; in[4] = g_search_srv;
        if (p == 0) in[1] = in[2] = NULL;
        if (p == 1) in[0] = in[2] = NULL;
        if (p == 2) in[1] = NULL;
        ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 5, in);
        ID3D11DeviceContext_PSSetSamplers(ctx, g_pass[p].linear_slot, 1, &g_linear);
        ID3D11DeviceContext_PSSetSamplers(ctx, g_pass[p].point_slot, 1, &g_point);
        ID3D11DeviceContext_VSSetShader(ctx, g_pass[p].vs, NULL, 0);
        ID3D11DeviceContext_PSSetShader(ctx, g_pass[p].ps, NULL, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 0);
    }
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 5, none);

    /* restore */
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, dsv);
    ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 5, srv);
    ID3D11DeviceContext_PSSetSamplers(ctx, 0, 4, smp);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &vcb);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &pcb);
    ID3D11DeviceContext_RSSetState(ctx, rs);
    ID3D11DeviceContext_RSSetViewports(ctx, nvp, vps);
    ID3D11DeviceContext_OMSetBlendState(ctx, bs, bf, bmask);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx, dss, sref);
    ID3D11DeviceContext_IASetInputLayout(ctx, il);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, topo);
    if (rtv) ID3D11RenderTargetView_Release(rtv);
    if (dsv) ID3D11DepthStencilView_Release(dsv);
    if (vs) ID3D11VertexShader_Release(vs);
    if (ps) ID3D11PixelShader_Release(ps);
    for (int i = 0; i < 5; i++) if (srv[i]) ID3D11ShaderResourceView_Release(srv[i]);
    for (int i = 0; i < 4; i++) if (smp[i]) ID3D11SamplerState_Release(smp[i]);
    if (vcb) ID3D11Buffer_Release(vcb);
    if (pcb) ID3D11Buffer_Release(pcb);
    if (rs) ID3D11RasterizerState_Release(rs);
    if (bs) ID3D11BlendState_Release(bs);
    if (dss) ID3D11DepthStencilState_Release(dss);
    if (il) ID3D11InputLayout_Release(il);
    return g_rt[2].srv;
}
