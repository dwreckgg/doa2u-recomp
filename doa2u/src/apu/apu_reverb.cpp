/**
 * Room reverb (I3DL2) for the APU output.
 *
 * On the Xbox the game's I3DL2 listener settings drive a reverb that runs on
 * the APU's DSP, fed by mix bin 10 (the I3DL2 send). The DSP is not emulated,
 * so the reverb runs on the host: the game's settings, captured from
 * IDirectSound_SetI3DL2Listener (recomp_manual.c), are converted with
 * XAudio2's own ReverbConvertI3DL2ToNative and applied to XAudio2's built-in
 * reverb on a stereo submix. Bin 10 is submitted to it as its own source
 * voice, alongside and at the same cadence as the dry output; the reverb
 * return lands on the front pair (also in 5.1).
 */
#include <windows.h>
#include <xaudio2.h>
#include <xaudio2fx.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "apu_reverb.h"

#define RV_SAMPLE_RATE 48000
#define RV_BUF_SAMPLES 512
#define RV_NUM_BUFS    APU_REVERB_BUFS

static IXAudio2              *g_rv_xa2;
static IXAudio2SubmixVoice   *g_rv_submix;
static IXAudio2SourceVoice   *g_rv_source;
static int16_t                g_rv_bufs[RV_NUM_BUFS][RV_BUF_SAMPLES][2];
static int                    g_rv_next;
static volatile LONG          g_rv_have_params;     /* the game has set its room */
static XAUDIO2FX_REVERB_I3DL2_PARAMETERS g_rv_i3dl2;
static volatile LONG          g_rv_dirty;           /* g_rv_i3dl2 changed, not yet applied */
static CRITICAL_SECTION       g_rv_lock;
static int                    g_rv_lock_ready;

static void apply_params(void)
{
    XAUDIO2FX_REVERB_PARAMETERS native;
    XAUDIO2FX_REVERB_I3DL2_PARAMETERS p;
    if (!g_rv_submix || !InterlockedExchange(&g_rv_dirty, 0)) return;
    EnterCriticalSection(&g_rv_lock);
    p = g_rv_i3dl2;
    LeaveCriticalSection(&g_rv_lock);
    ReverbConvertI3DL2ToNative(&p, &native);
    g_rv_submix->SetEffectParameters(0, &native, sizeof(native), XAUDIO2_COMMIT_NOW);
}

extern "C" int xa2_reverb_init(IXAudio2 *xa2)
{
    IUnknown *apo = NULL;
    XAUDIO2_EFFECT_DESCRIPTOR fx;
    XAUDIO2_EFFECT_CHAIN chain;
    XAUDIO2_SEND_DESCRIPTOR send;
    XAUDIO2_VOICE_SENDS sends;
    WAVEFORMATEX wfx;
    HRESULT hr;

    if (!g_rv_lock_ready) { InitializeCriticalSection(&g_rv_lock); g_rv_lock_ready = 1; }
    if (!xa2 || g_rv_submix) return g_rv_submix != NULL;
    if (FAILED(XAudio2CreateReverb(&apo)) || !apo) return 0;
    fx.pEffect = apo;
    fx.InitialState = TRUE;
    fx.OutputChannels = 2;
    chain.EffectCount = 1;
    chain.pEffectDescriptors = &fx;
    hr = xa2->CreateSubmixVoice(&g_rv_submix, 2, RV_SAMPLE_RATE, 0, 0, NULL, &chain);
    apo->Release();
    if (FAILED(hr)) { g_rv_submix = NULL; return 0; }

    memset(&wfx, 0, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = RV_SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 4;
    wfx.nAvgBytesPerSec = RV_SAMPLE_RATE * 4;
    send.Flags = 0;
    send.pOutputVoice = g_rv_submix;
    sends.SendCount = 1;
    sends.pSends = &send;
    hr = xa2->CreateSourceVoice(&g_rv_source, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, NULL, &sends, NULL);
    if (FAILED(hr)) {
        g_rv_source = NULL;
        g_rv_submix->DestroyVoice();
        g_rv_submix = NULL;
        return 0;
    }
    g_rv_source->Start(0, XAUDIO2_COMMIT_NOW);
    g_rv_xa2 = xa2;
    g_rv_next = 0;
    apply_params();
    return 1;
}

extern "C" void xa2_reverb_shutdown(void)
{
    if (g_rv_source) {
        g_rv_source->Stop(0, XAUDIO2_COMMIT_NOW);
        g_rv_source->FlushSourceBuffers();
        g_rv_source->DestroyVoice();
        g_rv_source = NULL;
    }
    if (g_rv_submix) { g_rv_submix->DestroyVoice(); g_rv_submix = NULL; }
    g_rv_xa2 = NULL;
}

/* DSI3DL2LISTENER as the game passes it: 12 dwords, LONGs and FLOATs. */
extern "C" void xa2_reverb_set_i3dl2(const void *params)
{
    const uint32_t *w = (const uint32_t *)params;
    XAUDIO2FX_REVERB_I3DL2_PARAMETERS p;
    float f[12];
    static int logged;
    memcpy(f, w, sizeof(f));
    p.WetDryMix = 100.0f;                       /* the dry signal is its own voice */
    p.Room = (int)w[0];
    p.RoomHF = (int)w[1];
    p.RoomRolloffFactor = f[2];
    p.DecayTime = f[3];
    p.DecayHFRatio = f[4];
    p.Reflections = (int)w[5];
    p.ReflectionsDelay = f[6];
    p.Reverb = (int)w[7];
    p.ReverbDelay = f[8];
    p.Diffusion = f[9];
    p.Density = f[10];
    p.HFReference = f[11];
    if (!g_rv_lock_ready) { InitializeCriticalSection(&g_rv_lock); g_rv_lock_ready = 1; }
    EnterCriticalSection(&g_rv_lock);
    g_rv_i3dl2 = p;
    LeaveCriticalSection(&g_rv_lock);
    InterlockedExchange(&g_rv_dirty, 1);
    InterlockedExchange(&g_rv_have_params, 1);
    if (logged < 4) {
        logged++;
        fprintf(stderr, "[AUDIO] I3DL2 room: room %d hf %d decay %.2f s reflections %d reverb %d delay %.3f diffusion %.0f density %.0f\n",
                p.Room, p.RoomHF, p.DecayTime, p.Reflections, p.Reverb, p.ReverbDelay, p.Diffusion, p.Density);
    }
}

extern "C" int xa2_reverb_ready(void)
{
    return g_rv_source != NULL && g_rv_have_params;
}

static const int16_t g_rv_silence[RV_BUF_SAMPLES][2] = {{0}};

extern "C" void xa2_reverb_submit(const int16_t *stereo, int num_samples, int dry_queued)
{
    XAUDIO2_VOICE_STATE state;
    XAUDIO2_BUFFER xbuf;
    int idx;
    if (!g_rv_source || num_samples <= 0) return;
    apply_params();
    g_rv_source->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if ((int)state.BuffersQueued >= RV_NUM_BUFS - 1) return;   /* never overwrite a queued buffer */
    if (num_samples > RV_BUF_SAMPLES) num_samples = RV_BUF_SAMPLES;
    /* behind as many buffers as the dry voice, so the reverb never plays ahead
     * of its sound (an empty wet voice would start at once) */
    for (int pad = apu_reverb_pad(dry_queued, (int)state.BuffersQueued); pad > 0; pad--) {
        memset(&xbuf, 0, sizeof(xbuf));
        xbuf.AudioBytes = (UINT32)(num_samples * 2 * sizeof(int16_t));
        xbuf.pAudioData = (const BYTE *)g_rv_silence;
        g_rv_source->SubmitSourceBuffer(&xbuf, NULL);
    }
    idx = g_rv_next;
    memcpy(g_rv_bufs[idx], stereo, (size_t)num_samples * 2 * sizeof(int16_t));
    memset(&xbuf, 0, sizeof(xbuf));
    xbuf.AudioBytes = (UINT32)(num_samples * 2 * sizeof(int16_t));
    xbuf.pAudioData = (const BYTE *)g_rv_bufs[idx];
    g_rv_source->SubmitSourceBuffer(&xbuf, NULL);
    g_rv_next = (idx + 1) % RV_NUM_BUFS;
}
