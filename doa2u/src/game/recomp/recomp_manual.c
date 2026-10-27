/* Dead or Alive 2 Ultimate - manual / overridden functions (DOA2.xbe, XDK 5849).
 * Each sub_X here replaces the generated body, renamed sub_X_gen by postprocess.py. */

#define RECOMP_GENERATED_CODE
#include "gen/recomp_funcs.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>
#include "../../kernel/perf.h"
#pragma comment(lib, "winmm.lib")

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
extern void xbox_fiber_yield(void);
extern int  xbox_fiber_active(void);
extern void xbox_fiber_wake(uint32_t event_va);
extern int  xbox_fiber_is_primary(void);
extern int  xbox_fiber_is_coroutine(void);

/* ── Guest addresses (DOA2.xbe) ─────────────────────────────────────── */
#define D3D_G_PDEVICE        0x003554F8u   /* D3D_g_pDevice */
#define D3D_FAKE_CHANNEL     0x00355000u   /* XDK's GPU-less FIFO control block (+0x40 PUT, +0x44 GET) */
static void rt_sync(void);
extern void nv2a_pgraph_set_reg(uint32_t addr, uint32_t val);
#define DEV_VBLANK_EVENT_OFS 0x1DBCu       /* D3DDevice__m_VerticalBlankEvent_OFFSET */
#define DEV_VBLANK_CB_OFS    0x1DB8u       /* D3DDevice__m_VBlankCallback_OFFSET */
#define DEV_SWAP_CB_OFS      0x1DB4u       /* D3DDevice__m_SwapCallback_OFFSET */
#define DSOUND_LO            0x00363360u   /* DSOUND section (APU interrupt routines live here) */
#define DSOUND_HI            0x00380EA4u

/* ── Flags carried across a generated-fragment boundary ─────────────── */
/* Exact x86 flag semantics for CMP (a - b) and TEST (a & b) at the operand
 * width the compare used; see RC_SETF_* in recomp_types.h. */
uint32_t g_flg_a, g_flg_b;
int g_flg_w = 4, g_flg_test;

int rc_flg_cc(int cc)
{
    uint32_t mask = (g_flg_w == 1) ? 0xFFu : (g_flg_w == 2) ? 0xFFFFu : 0xFFFFFFFFu;
    uint32_t sign = (g_flg_w == 1) ? 0x80u : (g_flg_w == 2) ? 0x8000u : 0x80000000u;
    uint32_t a = g_flg_a & mask, b = g_flg_b & mask, r;
    int zf, sf, cf = 0, of = 0, pf, i, ones = 0;
    unsigned lo;

    if (g_flg_test) {
        r = (a & b) & mask;
    } else {
        r = (a - b) & mask;
        cf = (a < b);
        of = ((((a ^ b) & (a ^ r)) & sign) != 0);
    }
    zf = (r == 0);
    sf = ((r & sign) != 0);
    lo = (unsigned)(r & 0xFFu);
    for (i = 0; i < 8; i++) if (lo & (1u << i)) ones++;
    pf = ((ones & 1) == 0);

    switch (cc) {
    case 0:  return zf;
    case 1:  return !zf;
    case 2:  return cf;
    case 3:  return !cf;
    case 4:  return cf || zf;
    case 5:  return !cf && !zf;
    case 6:  return sf != of;
    case 7:  return sf == of;
    case 8:  return zf || (sf != of);
    case 9:  return !zf && (sf == of);
    case 10: return sf;
    case 11: return !sf;
    case 12: return of;
    case 13: return !of;
    case 14: return pf;
    case 15: return !pf;
    default: return 0;
    }
}

/* ── Unresolved indirect calls ──────────────────────────────────────── */
/* Indirect call/jump to a target with no translated body: log each target once. */
#define ICALL_SEEN_MAX 256
static uint32_t s_icall_seen[ICALL_SEEN_MAX];
static int s_icall_seen_n;

static int icall_first_time(uint32_t va)
{
    int i;
    for (i = 0; i < s_icall_seen_n; i++) if (s_icall_seen[i] == va) return 0;
    if (s_icall_seen_n < ICALL_SEEN_MAX) s_icall_seen[s_icall_seen_n++] = va;
    return 1;
}

/* Native callers are logged as image RVAs; resolve them against
 * build/release/DOA2U.map ("Rva+Base", base 0x140000000). */
static unsigned long long caller_rva(void *ra)
{
    return (unsigned long long)((uintptr_t)ra - (uintptr_t)GetModuleHandleA(NULL));
}

void recomp_icall_fail_log(uint32_t va)
{
    if (icall_first_time(va)) {
        void *bt[4];
        USHORT n = CaptureStackBackTrace(1, 4, bt, NULL);
        USHORT i;
        fprintf(stderr, "[ICALL] unresolved target 0x%08X (esp=%08X) from rva", va, esp);
        for (i = 0; i < n; i++) fprintf(stderr, " %llX", caller_rva(bt[i]));
        fprintf(stderr, "\n");
        fflush(stderr);
    }
}

void recomp_itail_fail_log(uint32_t va)
{
    if (icall_first_time(va | 0x80000000u)) {
        fprintf(stderr, "[ITAILF] unresolved tail target 0x%08X (esp=%08X)\n", va, esp);
        fflush(stderr);
    }
}

/* Manual entry points that have no generated counterpart (none yet). The
 * dispatch table already binds every sub_XXXXXXXX defined in this file. */
static const struct {
    uint32_t      xbox_va;
    recomp_func_t func;
} g_manual_funcs[] = {
    { 0u, 0 },  /* sentinel */
};
#define NUM_MANUAL_FUNCS (sizeof(g_manual_funcs) / sizeof(g_manual_funcs[0]))

recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    size_t i;
    for (i = 0; i < NUM_MANUAL_FUNCS; i++)
        if (g_manual_funcs[i].func && g_manual_funcs[i].xbox_va == xbox_va)
            return g_manual_funcs[i].func;
    return NULL;
}

/* ── Call targets with no valid code (data decoded as code) ─────────── */
/* Garbage call targets (operands decoded from data): consume the return slot and log. */
#define GARBAGE_TARGET(va) \
    void sub_##va(void) { \
        fprintf(stderr, "[GARBAGE] call to non-code target 0x" #va "\n"); \
        esp += 4; \
    }
GARBAGE_TARGET(02888C16)
GARBAGE_TARGET(97336EEB)
GARBAGE_TARGET(003C14DA)
GARBAGE_TARGET(003C14EA)
GARBAGE_TARGET(E91CEB10)
GARBAGE_TARGET(E970EB1C)
GARBAGE_TARGET(EE2A2E3C)

/* ── Runtime symbols the shared libraries expect from the game layer ── */
volatile int g_doa2u_post_movie = 0;      /* fiber timeslicing gate (kept off) */
void (*g_kernel_ptinfo_hook)(const char *where);   /* NtReadFile diagnostic hook (unused) */
uint32_t g_doa2u_offrt_offs[32]; int g_doa2u_offrt_n;

/* D3DDevice_SetRenderTarget (0x003447D0): record the colour offset of every
 * texture surface used as a render target (D3DSurface.Parent at +0x14 is set
 * only for texture surfaces), so the pgraph translator routes it offscreen.
 * The stage glow target is 720x480 with the frame buffer's pitch, and without
 * this its clear landed on the back buffer and blacked out the scene. */
void sub_003447D0(void)
{
    extern void sub_003447D0_gen(void);
    uint32_t surf = MEM32(esp + 4);
    sub_003447D0_gen();
    if (surf && MEM32(surf + 0x14)) {
        uint32_t data = MEM32(surf + 4) & 0x07FFFFFFu;
        static int s_next = 0;   /* oldest entry, once the table is full */
        int k, seen = 0;
        for (k = 0; k < g_doa2u_offrt_n; k++) if (g_doa2u_offrt_offs[k] == data) seen = 1;
        if (!seen && g_doa2u_offrt_n < 32) g_doa2u_offrt_offs[g_doa2u_offrt_n++] = data;
        else if (!seen) { g_doa2u_offrt_offs[s_next] = data; s_next = (s_next + 1) % 32; }
    }
}
/* Worker timeslice: every 4 ms the game thread hands the CRI server fibers a slice,
 * standing in for preemption (the game's load waits never block). */
int doa2u_workers_may_run(void) { return 1; }
/* A worker that entered the CRI server (guard 0xF2BC38) or took its file lock keeps the CPU:
 * the load coroutine spins on both without yielding, so a handoff there never comes back. */
static int s_cri_owner[2] = { -1, -1 }, s_cri_set_in[2];
static const uint32_t s_cri_flag[2] = { 0xF2BC38u, 0xF2AF50u };
void doa2u_fiber_switch_in(int idx)
{
    int k;
    for (k = 0; k < 2; k++) {
        if (!MEM32(s_cri_flag[k])) s_cri_owner[k] = -1;
        s_cri_set_in[k] = MEM32(s_cri_flag[k]) != 0;
    }
    (void)idx;
}
void doa2u_fiber_switch_out(int idx)
{
    int k;
    for (k = 0; k < 2; k++)
        if (MEM32(s_cri_flag[k]) && !s_cri_set_in[k] && s_cri_owner[k] < 0) s_cri_owner[k] = idx;
}
int doa2u_worker_may_yield(int idx)
{
    int k, ok = 1;
    for (k = 0; k < 2; k++) {
        if (!MEM32(s_cri_flag[k])) { s_cri_owner[k] = -1; continue; }
        if (!s_cri_set_in[k] && s_cri_owner[k] < 0) s_cri_owner[k] = idx;   /* set during this run */
        if (s_cri_owner[k] == idx) ok = 0;
    }
    return ok;
}
/* Display size from the guest's D3DPRESENT_PARAMETERS (0x00E658F0), the space of
 * 2D overlay coordinates. Returns 0 before CreateDevice fills it. */
int doa2u_guest_display_size(unsigned *w, unsigned *h)
{
    uint32_t dw = MEM32(0x00E658F0u), dh = MEM32(0x00E658F4u);
    if (dw < 64u || dw > 4096u || dh < 64u || dh > 4096u) return 0;
    *w = (unsigned)dw; *h = (unsigned)dh;
    return 1;
}

/* APU interrupt delivery: run the DirectSound ISR and its DPCs as guest calls. */
void doa2u_apu_deliver_irq(void)
{
    typedef struct MCPXAPUState MCPXAPUState;
    extern int mcpx_apu_take_irq(void);
    extern int xbox_kernel_get_isr(int, uint32_t *, uint32_t *, uint32_t *);
    extern int xbox_kernel_pop_dpc(uint32_t *, uint32_t *, uint32_t *);
    static int s_delivering = 0;
    uint32_t obj, routine, ctx, dpc, a1, a2;
    int i;
    if (s_delivering) return;
    if (!mcpx_apu_take_irq()) return;
    s_delivering = 1;
    for (i = 0; xbox_kernel_get_isr(i, &obj, &routine, &ctx); i++) {
        recomp_func_t fn;
        if (routine < DSOUND_LO || routine >= DSOUND_HI) continue;
        fn = recomp_lookup_manual(routine);
        if (!fn) fn = recomp_lookup(routine);
        if (!fn) continue;
        {
            uint32_t saved_esp = esp;
            PUSH32(esp, ctx);
            PUSH32(esp, obj);
            PUSH32(esp, 0);
            fn();
            esp = saved_esp;
        }
    }
    while (xbox_kernel_pop_dpc(&dpc, &a1, &a2)) {
        uint32_t droutine = MEM32(dpc + 12), dctx = MEM32(dpc + 16);
        recomp_func_t fn = recomp_lookup_manual(droutine);
        if (!fn) fn = recomp_lookup(droutine);
        if (!fn) continue;
        {
            uint32_t saved_esp = esp;
            PUSH32(esp, a2);
            PUSH32(esp, a1);
            PUSH32(esp, dctx);
            PUSH32(esp, dpc);
            PUSH32(esp, 0);
            fn();
            esp = saved_esp;
        }
    }
    s_delivering = 0;
}

/* DirectSound's wait for a voice to go off (0x0036A38C, thiscall ecx = voice):
 * spins on flag 0x8000 at +0x12, which the APU ISR clears when the hardware
 * reports the voice stopped. On the console that interrupt arrives during the
 * spin; here interrupts were only delivered at Present, so every voice waited
 * out sub_00369B74's 500 ms timeout and a stage change froze for seconds.
 * Deliver the interrupt from inside the wait; guest registers are preserved. */
void sub_0036A38C(void)
{
    uint32_t v = ecx;
    if (MEM8(v + 0x12) & 1) {
        while (MEM16(v + 0x12) & 0x8000) {
            uint32_t r_eax = eax, r_edx = edx, r_ebx = ebx, r_esi = esi,
                     r_edi = edi, r_esp = esp, r_seh = g_seh_ebp;
            doa2u_apu_deliver_irq();
            eax = r_eax; edx = r_edx; ebx = r_ebx; esi = r_esi;
            edi = r_edi; esp = r_esp; g_seh_ebp = r_seh;
            if (!(MEM16(v + 0x12) & 0x8000)) break;
            ecx = v;
            PUSH32(esp, 0);
            sub_00369B74();                 /* keeps the driver's 500 ms timeout */
            if (MEM16(v + 0x12) & 0x8000) Sleep(1);
        }
    }
    ecx = v;
    esp += 4;                               /* ret */
}

/* ── Game hooks called by the kernel bridge ─────────────────────────── */

/* Every guest file open. A .sfd is a movie the guest is about to play:
 * hand it to the host presenter (movie_present.c). */
void game_on_file_open(const char *xbox_path)
{
    size_t n = strlen(xbox_path);
    static int s_logged = 0;
    if (s_logged < 200) {
        s_logged++;
        fprintf(stderr, "[FILE] open %s\n", xbox_path);
        fflush(stderr);
    }
    (void)n;
}

/* mwPlyStartFname (0x0031BBB0): the host presenter shows the movie with its ADX track
 * while the guest player runs alongside. The guest's audio is muted with
 * mwSfdSetOutVol (0x0030E0E0, -96 dB) so the soundtrack does not echo; not
 * mwPlySetAudioSw(0), which makes the guest player end the movie early. */
void sub_0031BBB0(void)
{
    extern void sub_0031BBB0_gen(void);
    extern void doa2u_movie_select(const char *guest_path);
    extern int doa2u_movies_enabled(void);
    uint32_t fname = MEM32(esp + 8);
    uint32_t handle = MEM32(esp + 4);
    if (handle) {
        uint32_t saved_esp = esp, sv_ebx = ebx, sv_esi = esi, sv_edi = edi, sv_seh = g_seh_ebp;
        PUSH32(esp, (uint32_t)-960);        /* vol: -96 dB (CRI minimum) */
        PUSH32(esp, handle);
        PUSH32(esp, 0);                     /* dummy return */
        sub_0030E0E0();
        esp = saved_esp; ebx = sv_ebx; esi = sv_esi; edi = sv_edi; g_seh_ebp = sv_seh;
    }
    if (fname) {
        char name[MAX_PATH];
        int i;
        for (i = 0; i < MAX_PATH - 1 && MEM8(fname + i); i++) name[i] = (char)MEM8(fname + i);
        name[i] = 0;
        fprintf(stderr, "[MOVIE] mwPlyStartFname(0x%08X, \"%s\")\n", MEM32(esp + 4), name);
        fflush(stderr);
        rt_sync();
        extern int doa2u_skip_intro_enabled(void);
        extern int doa2u_movie_is_intro(const char *guest_path);
        if (doa2u_movies_enabled() && !(doa2u_skip_intro_enabled() && doa2u_movie_is_intro(name))) {
            doa2u_movie_select(name);
        } else {
            /* FMV off, or an intro movie with Skip intro on: no presenter; the
             * vblank pump's host-EOF trigger ends the guest movie once it
             * reaches PLAYING, the game's own end path. */
            extern int g_doa2u_host_movie_ended;
            g_doa2u_host_movie_ended = 1;
        }
    }
    sub_0031BBB0_gen();
}

/* Movie teardown (0x000F9A10): stops and destroys the mwPly handle at 0xA942D8, on
 * both end of stream and START pressed. The host presenter runs on its own clock and
 * only stops at end of file, so on a skip it kept owning the screen (guest frames are
 * not presented while it is active) and played on. Hand the screen back here. */
void sub_000F9A10(void)
{
    extern void sub_000F9A10_gen(void);
    extern void doa2u_movie_stop(void);
    doa2u_movie_stop();
    sub_000F9A10_gen();
}

/* Modelled vblank: the game may wait on vblank during movies without calling Swap,
 * so the presenter is ticked here too. */
void game_pump_vblank_servers(void)
{
    extern void doa2u_movie_tick(void);
    extern int doa2u_movie_active(void);
    extern volatile LONG g_doa2u_heartbeat;
    static LARGE_INTEGER s_last, s_freq;
    LARGE_INTEGER now;
    if (!doa2u_movie_active()) return;
    /* at most one presenter tick per 1/60 s from this path */
    if (!s_freq.QuadPart) QueryPerformanceFrequency(&s_freq);
    QueryPerformanceCounter(&now);
    if (s_last.QuadPart && (now.QuadPart - s_last.QuadPart) * 60 < s_freq.QuadPart) return;
    s_last = now;
    InterlockedIncrement(&g_doa2u_heartbeat);
    rt_sync();
    doa2u_movie_tick();
}

/* ── XAPI fibers -> host cooperative fibers ─────────────────────────── */
/* XAPI fibers backed by host coroutines (xbox_fiber.c), handles are
 * 0xF1BE0000|index, anything else means back to the dispatcher. */
extern int  xbox_fiber_create_dormant(uint32_t routine_va, uint32_t param, uint32_t stack_size);
extern void xbox_fiber_destroy(int idx);
extern void xbox_fiber_switch_direct(int idx);
#define XFIBER_TAG  0xF1BE0000u

void sub_002BB76A(void)   /* CreateFiber(stack_size, start_routine, param), stdcall ret 12 */
{
    uint32_t stack_sz = MEM32(esp + 4);
    uint32_t routine  = MEM32(esp + 8);
    uint32_t param    = MEM32(esp + 0xC);
    int idx = xbox_fiber_create_dormant(routine, param, stack_sz);
    eax = (idx > 0) ? (XFIBER_TAG | (uint32_t)idx) : 0;
    esp += 16;
}

void sub_002BB7F6(void)   /* DeleteFiber(handle), stdcall ret 4 */
{
    uint32_t h = MEM32(esp + 4);
    if ((h & 0xFFFF0000u) == XFIBER_TAG)
        xbox_fiber_destroy((int)(h & 0xFFFF));
    esp += 8;
}

void sub_002BB809(void)   /* SwitchToFiber(handle), stdcall ret 4 */
{
    uint32_t h = MEM32(esp + 4);
    esp += 8;                 /* finish the call first, then transfer */
    if ((h & 0xFFFF0000u) == XFIBER_TAG) {
        xbox_fiber_switch_direct((int)(h & 0xFFFF));
    } else {
        extern void xbox_fiber_yield_back(void);
        xbox_fiber_yield_back();
    }
}

/* ── CRT memcpy (0x00335A80) ────────────────────────────────────────── */
/* memcpy/memmove (0x335A80): its jump-table tail can't be lifted; overlap-safe native copy. */
void sub_00335A80(void)
{
    uint32_t dst = MEM32(esp + 4), src = MEM32(esp + 8), n = MEM32(esp + 0xC);
    if (n)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), n);
    eax = dst;
    esp += 4;
}

/* ── D3D (XDK 5849) push buffer, fences and present ─────────────────── */
/* No GPU behind the push-buffer ring: KickOff translates new commands to D3D11 and
 * retires them like the XDK's GPU-less branch (GET == PUT, all fences done). */
static uint32_t g_pb_parsed = 0;     /* next push-buffer word not yet translated */
static uint32_t g_pb_cb_arg[2];      /* 0x1D8C / 0x1D90: InsertCallback routine, context */
static uint32_t g_pb_cbq[64][2], g_pb_cbq_n;
static SRWLOCK  g_pb_cb_lock = SRWLOCK_INIT;
static LONG     s_gpu_irq;

/* Game thread: deliver a pending GPU interrupt as KeSetEvent on device+0x1DCC. */
void doa2u_gpu_irq_poll(void)
{
    if (s_gpu_irq && InterlockedExchange(&s_gpu_irq, 0)) {
        extern void xbox_fiber_wake(uint32_t event_va);
        uint32_t dev = MEM32(D3D_G_PDEVICE);
        if (dev) {
            MEM32(dev + 0x1DCC + 4) = 1;                 /* KEVENT SignalState */
            xbox_fiber_wake(dev + 0x1DCC);
        }
    }
}

/* Visibility tests. Each ZPASS_PIXEL_COUNT_ENABLE on/off pair is a host
 * occlusion query; the queries since CLEAR_REPORT_VALUE make up the value a
 * GET_REPORT reports. The report slot {time, count, done} is written once the
 * GPU has every count (pb_poll_reports); until then its done word stays
 * 0xFFFFFFFF and GetVisibilityTestResult (0x344450) returns TESTINCOMPLETE and
 * kicks the push buffer, which polls again. DOA2U reads its two stage-glare
 * tests a frame later, so the CPU no longer waits for the GPU to drain. */
extern void d3d8_OcclusionBegin(void);
extern int  d3d8_OcclusionEnd(void);
extern int  d3d8_OcclusionFetch(int slot, uint32_t *count, int block);
#define ZPASS_MAX 16
static int g_zpass_q[ZPASS_MAX], g_zpass_qn;      /* queries since CLEAR_REPORT_VALUE */
static uint32_t g_zpass_acc;                      /* counts of queries that did not fit */
typedef struct { uint32_t va; int q[ZPASS_MAX]; int qn; uint32_t acc; unsigned age; } PendingReport;
#define REPORTS_MAX 16
static PendingReport g_reports[REPORTS_MAX];
static unsigned g_reports_n;

static void report_write(uint32_t va, uint32_t count)
{
    MEM32(va) = GetTickCount(); MEM32(va + 4) = 0;
    MEM32(va + 8) = count;
    MEM32(va + 0xC) = 0;
}

/* Write every report whose counts have all arrived; block waits for them. */
static void pb_poll_reports(int block)
{
    unsigned r = 0;
    while (r < g_reports_n) {
        PendingReport *p = &g_reports[r];
        int k = 0;
        while (k < p->qn) {
            uint32_t c;
            if (!d3d8_OcclusionFetch(p->q[k], &c, block)) break;
            p->acc += c;
            p->q[k] = p->q[--p->qn];                 /* fetched: drop it */
        }
        if (p->qn == 0) {
            report_write(p->va, p->acc);
            g_reports[r] = g_reports[--g_reports_n];
        } else {
            r++;
        }
    }
}

/* Once per frame: anything still pending after two frames is waited for, so a
 * game that waits on a report cannot wait forever. */
static void pb_age_reports(void)
{
    unsigned r;
    int stale = 0;
    for (r = 0; r < g_reports_n; r++) if (++g_reports[r].age >= 2) stale = 1;
    pb_poll_reports(stale);
}

static void zpass_end(void)
{
    const int q = d3d8_OcclusionEnd();
    if (q < 0) return;
    if (g_zpass_qn < ZPASS_MAX) { g_zpass_q[g_zpass_qn++] = q; return; }
    {   /* more tests than one report holds: count this one now */
        uint32_t c;
        d3d8_OcclusionFetch(q, &c, 1);
        g_zpass_acc += c;
    }
}

/* Visibility tests above; NOP 6/7 queues the inserted GPU callback. */
static void pb_side_method(uint32_t method, uint32_t param)
{
    switch (method) {
    case 0x17C8:                                                    /* CLEAR_REPORT_VALUE */
        if (param == 1) {
            int k;
            for (k = 0; k < g_zpass_qn; k++) { uint32_t c; d3d8_OcclusionFetch(g_zpass_q[k], &c, 1); }
            g_zpass_qn = 0;
            g_zpass_acc = 0;
        }
        break;
    case 0x17CC:                                                    /* ZPASS_PIXEL_COUNT_ENABLE */
        if (param) d3d8_OcclusionBegin(); else zpass_end();
        break;
    case 0x17D0:                                                    /* GET_REPORT */
        {   /* Host heap pages sit above the 16 MB the 24-bit offset covers: match the
             * offset against the device's report pages (dev+0x7D4) instead. */
            uint32_t dev = MEM32(D3D_G_PDEVICE), va = 0, k;
            for (k = 0; dev && k < 64 && !va; k++) {
                uint32_t pg = MEM32(dev + 0x7D4 + k * 4);
                if (pg && !((pg ^ param) & 0x00FFF000u) && MEM32(pg + (param & 0xFF0u) + 0xC) == 0xFFFFFFFFu)
                    va = pg + (param & 0xFF0u);
            }
            if (va && g_zpass_qn == 0) {
                report_write(va, g_zpass_acc);
            } else if (va && g_reports_n < REPORTS_MAX) {
                /* The report owns the queries since CLEAR. The XDK brackets every
                 * test with CLEAR ... GET_REPORT (Begin/EndVisibilityTest); a second
                 * GET_REPORT without a CLEAR would report only the tests after the
                 * first, where the hardware would report the running total. */
                PendingReport *p = &g_reports[g_reports_n++];
                p->va = va;
                p->qn = g_zpass_qn;
                memcpy(p->q, g_zpass_q, sizeof(int) * (size_t)g_zpass_qn);
                p->acc = g_zpass_acc;
                p->age = 0;
                g_zpass_qn = 0;                         /* the report owns them now */
                g_zpass_acc = 0;
            } else if (va) {                            /* no room: count now */
                int k;
                for (k = 0; k < g_zpass_qn; k++) { uint32_t c; d3d8_OcclusionFetch(g_zpass_q[k], &c, 1); g_zpass_acc += c; }
                g_zpass_qn = 0;
                report_write(va, g_zpass_acc);
            }
        }
        break;
    case 0x1D8C: g_pb_cb_arg[0] = param; break;
    case 0x1D90: g_pb_cb_arg[1] = param; break;
    case 0x0100:
        /* NOP 5: GPU interrupt for the push-buffer space wait (sub_0034A1A8). */
        if (param == 5) InterlockedExchange(&s_gpu_irq, 1);
        AcquireSRWLockExclusive(&g_pb_cb_lock);
        if ((param == 6 || param == 7) && g_pb_cb_arg[0] && g_pb_cbq_n < 64) {
            g_pb_cbq[g_pb_cbq_n][0] = g_pb_cb_arg[0];
            g_pb_cbq[g_pb_cbq_n][1] = g_pb_cb_arg[1];
            g_pb_cbq_n++;
        }
        ReleaseSRWLockExclusive(&g_pb_cb_lock);
        break;
    }
}

/* Run the callbacks the translated commands reached, always on the game thread. */
static void pb_run_callbacks(void)
{
    uint32_t i, n, q[64][2];
    AcquireSRWLockExclusive(&g_pb_cb_lock);
    n = g_pb_cbq_n;
    memcpy(q, g_pb_cbq, n * sizeof q[0]);
    g_pb_cbq_n = 0;
    ReleaseSRWLockExclusive(&g_pb_cb_lock);
    for (i = 0; i < n; i++) {
        recomp_func_t fn = recomp_lookup_manual(q[i][0]);
        uint32_t sv[7] = { eax, ebx, ecx, edx, esi, edi, esp };
        if (!fn) fn = recomp_lookup(q[i][0]);
        if (!fn) continue;
        PUSH32(esp, q[i][1]);
        PUSH32(esp, 0);                       /* dummy return */
        fn();
        eax = sv[0]; ebx = sv[1]; ecx = sv[2]; edx = sv[3]; esi = sv[4]; edi = sv[5]; esp = sv[6];
    }
}

/* Drawing thread: KickOff queues the new commands and a second thread translates and presents
 * them, as the NV2A consumes the push buffer alongside the CPU. GET and fences follow its progress. */
typedef struct { uint32_t *w; uint32_t n, cap; uint32_t get, fence, done; int present; } RtItem;
#define RT_QN 16
static RtItem s_rtq[RT_QN];
static LONG s_rtq_head, s_rtq_tail, s_rt_busy;        /* head: producer, tail: consumer */
static SRWLOCK s_rt_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE s_rt_cv_work = CONDITION_VARIABLE_INIT, s_rt_cv_done = CONDITION_VARIABLE_INIT;
static LONG s_rt_present_enq, s_rt_present_done;
static int s_rt_on = -1, s_rt_collecting;
static HANDLE s_rt_thread;

static void pb_exec_words(const uint32_t *w, uint32_t n);

/* Drawing thread, at the end of an item: a game waiting on a visibility test
 * (GetVisibilityTestResult spins on TESTINCOMPLETE, kicking once at most)
 * sends no more work, so keep checking the pending reports until they are
 * written or new work arrives. The item still counts as busy, so rt_sync
 * waits for this before the game thread touches the device. */
static void rt_finish_reports(void)
{
    pb_poll_reports(0);
    while (g_reports_n) {
        int more;
        AcquireSRWLockExclusive(&s_rt_lock);
        if (s_rtq_head - s_rtq_tail <= 1) SleepConditionVariableSRW(&s_rt_cv_work, &s_rt_lock, 1, 0);
        more = s_rtq_head - s_rtq_tail > 1;
        ReleaseSRWLockExclusive(&s_rt_lock);
        if (more) break;
        pb_poll_reports(0);
    }
}

static DWORD WINAPI rt_main(LPVOID unused)
{
    extern void pgraph_d3d11_flush(void);
    extern void doa2u_present_host(void);
    (void)unused;
    for (;;) {
        RtItem *it;
        AcquireSRWLockExclusive(&s_rt_lock);
        while (s_rtq_tail == s_rtq_head) SleepConditionVariableSRW(&s_rt_cv_work, &s_rt_lock, INFINITE, 0);
        it = &s_rtq[s_rtq_tail % RT_QN];
        s_rt_busy = 1;
        ReleaseSRWLockExclusive(&s_rt_lock);
        if (it->present) {
            extern void d3d8_frame_pace(int extra_us, int force_timer);
            pgraph_d3d11_flush();
            pb_age_reports();                 /* visibility tests: this thread owns the device */
            d3d8_frame_pace(0, 0);            /* 60 Hz pacing (d3d/frame_pacer.h), next to the present */
            doa2u_present_host();
            InterlockedIncrement(&s_rt_present_done);
            rt_finish_reports();
        } else {
            pb_exec_words(it->w, it->n);
            MEM32(D3D_FAKE_CHANNEL + 0x44) = it->get;            /* DMA_GET: this batch consumed */
            if (it->done) {                                      /* its fences retired */
                MEM32(it->done) = it->fence - 2;
                nv2a_pgraph_set_reg(0xB10, (it->fence - 2) << 2);
            }
            /* The space wait patches its NOP-5 into already-copied commands: interrupt per batch. */
            InterlockedExchange(&s_gpu_irq, 1);
            rt_finish_reports();                                 /* fences first: a fence wait never waits on queries */
        }
        AcquireSRWLockExclusive(&s_rt_lock);
        s_rtq_tail++;
        s_rt_busy = 0;
        WakeAllConditionVariable(&s_rt_cv_done);
        ReleaseSRWLockExclusive(&s_rt_lock);
    }
}

static int rt_enabled(void)
{
    if (s_rt_on < 0) {
        s_rt_thread = CreateThread(NULL, 0, rt_main, NULL, 0, NULL);
        s_rt_on = s_rt_thread != NULL;
    }
    return s_rt_on;
}

/* Wait (pumping the window) until fewer than `items` slots are used and <= `presents` pending. */
static void rt_wait(LONG items, LONG presents)
{
    extern void doa2u_pump_messages(void);
    for (;;) {
        int ok;
        AcquireSRWLockExclusive(&s_rt_lock);
        ok = (s_rtq_head - s_rtq_tail) < items && (s_rt_present_enq - s_rt_present_done) <= presents;
        if (!ok) SleepConditionVariableSRW(&s_rt_cv_done, &s_rt_lock, 2, 0);
        ReleaseSRWLockExclusive(&s_rt_lock);
        if (ok) return;
        doa2u_pump_messages();
    }
}

/* Wait until everything queued is drawn (before game-thread use of the device). */
static void rt_sync(void)
{
    extern void doa2u_pump_messages(void);
    if (s_rt_on <= 0) return;
    for (;;) {
        int idle;
        AcquireSRWLockExclusive(&s_rt_lock);
        idle = s_rtq_head == s_rtq_tail && !s_rt_busy;
        if (!idle) SleepConditionVariableSRW(&s_rt_cv_done, &s_rt_lock, 2, 0);
        ReleaseSRWLockExclusive(&s_rt_lock);
        if (idle) return;
        doa2u_pump_messages();
    }
}

/* The slot KickOff fills; waits while the queue is full. */
static RtItem *rt_slot(void)
{
    RtItem *it;
    rt_wait(RT_QN, 0x7FFFFFFF);
    it = &s_rtq[s_rtq_head % RT_QN];
    it->n = 0; it->present = 0; it->done = 0;
    return it;
}

static void rt_append(RtItem *it, const uint32_t *w, uint32_t n)
{
    if (it->n + n > it->cap) {
        uint32_t cap = it->cap ? it->cap : 65536;
        while (cap < it->n + n) cap *= 2;
        it->w = (uint32_t *)realloc(it->w, (size_t)cap * 4);
        it->cap = cap;
    }
    memcpy(it->w + it->n, w, (size_t)n * 4);
    it->n += n;
}

static void rt_publish(void)
{
    AcquireSRWLockExclusive(&s_rt_lock);
    s_rtq_head++;
    WakeConditionVariable(&s_rt_cv_work);
    ReleaseSRWLockExclusive(&s_rt_lock);
}

static RtItem *s_rt_fill;   /* the batch KickOff is collecting into */

/* Translate [from, to). Stops at a JUMP/CALL/RETURN (wrap) and returns the
 * address it stopped at. */
static uint32_t pb_translate(uint32_t from, uint32_t to)
{
    extern int pgraph_d3d11_method(int subchannel, uint32_t method, uint32_t param);
    extern int pgraph_d3d11_method_bulk(uint32_t method, const uint32_t *params, uint32_t count);
    uint32_t pos = from;
    if (s_rt_collecting) {
        /* Same walk without executing: copy the complete packets for the drawing thread. */
        uint32_t good = from;
        while (pos + 4 <= to) {
            uint32_t word = MEM32(pos);
            if (word == 0) { pos += 4; good = pos; continue; }
            if ((word & 3) == 1 || (word & 0xE0000003u) == 0x20000000u || word == 0x00020000u ||
                (word & 3) == 2)
                break;                               /* jump / call / return: end of run */
            {
                uint32_t kind = word & 0xE0030003u;
                pos += 4;
                if (kind == 0 || kind == 0x40000000u) {
                    uint32_t count = (word >> 18) & 0x7FF;
                    if (pos + count * 4 > to) { pos = to; break; }
                    pos += count * 4;
                }
                good = pos;
            }
        }
        if (good > from) rt_append(s_rt_fill, (const uint32_t *)XBOX_PTR(from), (good - from) / 4);
        return pos;
    }
    while (pos + 4 <= to) {
        uint32_t word = MEM32(pos);
        if (word == 0) { pos += 4; continue; }
        if ((word & 3) == 1 || (word & 0xE0000003u) == 0x20000000u || word == 0x00020000u ||
            (word & 3) == 2)
            return pos;                          /* jump / call / return: end of run */
        {
            uint32_t kind = word & 0xE0030003u;
            if (kind == 0 || kind == 0x40000000u) {   /* increasing / non-increasing */
                uint32_t count   = (word >> 18) & 0x7FF;
                uint32_t method  = word & 0x1FFC;
                uint32_t subchan = (word >> 13) & 7;
                uint32_t i;
                pos += 4;
                if (pos + count * 4 > to) return to;
                /* Index and inline-vertex runs (no side effects in pb_side_method) in one call. */
                if ((kind == 0x40000000u || count == 1) &&
                    pgraph_d3d11_method_bulk(method, (const uint32_t *)XBOX_PTR(pos), count)) {
                    pos += count * 4;
                    continue;
                }
                for (i = 0; i < count; i++) {
                    uint32_t param = MEM32(pos), m = (kind == 0) ? method + i * 4 : method;
                    pos += 4;
                    pgraph_d3d11_method((int)subchan, m, param);
                    pb_side_method(m, param);
                }
            } else {
                pos += 4;
            }
        }
    }
    return pos;
}

/* Drawing thread: pb_translate over a copied batch (complete packets, no jumps). */
static void pb_exec_words(const uint32_t *w, uint32_t n)
{
    extern int pgraph_d3d11_method(int subchannel, uint32_t method, uint32_t param);
    extern int pgraph_d3d11_method_bulk(uint32_t method, const uint32_t *params, uint32_t count);
    uint32_t i = 0;
    while (i < n) {
        uint32_t word = w[i++], kind = word & 0xE0030003u;
        if (word == 0 || !(kind == 0 || kind == 0x40000000u)) continue;
        {
            uint32_t count = (word >> 18) & 0x7FF, method = word & 0x1FFC, subchan = (word >> 13) & 7, k;
            if (i + count > n) return;
            if ((kind == 0x40000000u || count == 1) && pgraph_d3d11_method_bulk(method, w + i, count)) {
                i += count;
                continue;
            }
            for (k = 0; k < count; k++) {
                uint32_t param = w[i++], m = (kind == 0) ? method + k * 4 : method;
                pgraph_d3d11_method((int)subchan, m, param);
                pb_side_method(m, param);
            }
        }
    }
}

static void pb_translate_pending(uint32_t dev, uint32_t cursor)
{
    uint32_t base = MEM32(dev + 0x24), end = MEM32(dev + 0x28);
    if (!base || end <= base || cursor < base || cursor > end)
        return;                                  /* not the ring (recording etc.) */
    if (g_pb_parsed < base || g_pb_parsed > end)
        g_pb_parsed = base;
    if (cursor >= g_pb_parsed) {
        uint32_t stop = pb_translate(g_pb_parsed, cursor);
        if (stop < cursor && cursor - stop > 4) {
            /* stopped on a jump before the cursor: the ring wrapped and the
             * cursor is past the base again */
            pb_translate(base, cursor);
        }
    } else {
        pb_translate(g_pb_parsed, end);          /* old tail up to its JUMP */
        pb_translate(base, cursor);              /* new head */
    }
    g_pb_parsed = cursor;
}

void sub_00349E80(void)   /* CDevice_KickOff(this = ecx), ret */
{
    uint32_t dev = ecx;
    extern int doa2u_movie_active(void);
    if (dev && rt_enabled() && !doa2u_movie_active()) {
        uint32_t flags  = MEM32(dev + 8);
        uint32_t cursor = (flags & 4) ? MEM32(dev + 0x770) : MEM32(dev);
        RtItem *it = rt_slot();
        s_rt_fill = it;
        s_rt_collecting = 1;
        pb_translate_pending(dev, cursor);
        s_rt_collecting = 0;
        it->get = cursor & 0x0FFFFFFFu;
        it->fence = MEM32(dev + 0x2C);
        it->done = MEM32(dev + 0x30);
        MEM32(dev + 0x1C20) = D3D_FAKE_CHANNEL;
        MEM32(D3D_FAKE_CHANNEL + 0x40) = cursor & 0x0FFFFFFFu;   /* DMA_PUT */
        MEM32(dev + 0x1DE4) = MEM32(dev + 0x2478);
        rt_publish();
        pb_run_callbacks();
    } else if (dev) {
        uint32_t flags  = MEM32(dev + 8);
        uint32_t cursor = (flags & 4) ? MEM32(dev + 0x770) : MEM32(dev);
        rt_sync();
        perf_begin(PERF_TRANSLATE);
        pb_translate_pending(dev, cursor);
        perf_end(PERF_TRANSLATE);
        pb_poll_reports(0);
        pb_run_callbacks();
        MEM32(dev + 0x1C20) = D3D_FAKE_CHANNEL;
        MEM32(D3D_FAKE_CHANNEL + 0x40) = cursor & 0x0FFFFFFFu;   /* DMA_PUT */
        MEM32(D3D_FAKE_CHANNEL + 0x44) = cursor & 0x0FFFFFFFu;   /* DMA_GET == PUT */
        {
            uint32_t done = MEM32(dev + 0x30);
            if (done) {                                          /* every inserted fence retired */
                MEM32(done) = MEM32(dev + 0x2C) - 2;
                nv2a_pgraph_set_reg(0xB10, (MEM32(dev + 0x2C) - 2) << 2);
            }
        }
        MEM32(dev + 0x1DE4) = MEM32(dev + 0x2478);
    }
    esp += 4;
}

/* Publish the D3D vblank event / counter to the kernel bridge once the device
 * exists (KeWaitForSingleObject on it is modelled as "vblank fired"). */
static void d3d_publish_vblank(uint32_t dev)
{
    extern uint32_t g_xbox_vblank_event_va;
    if (dev && !g_xbox_vblank_event_va) {
        g_xbox_vblank_event_va = dev + DEV_VBLANK_EVENT_OFS;
        fprintf(stderr, "[D3D] device 0x%08X, vblank event 0x%08X\n", dev, g_xbox_vblank_event_va);
        fflush(stderr);
    }
}

/* IDirectSound_SetI3DL2Listener (0x0036604A, stdcall pDS, pParams, dwApply): the
 * game's room-reverb settings, a DSI3DL2LISTENER (lRoom, lRoomHF, flRoomRolloffFactor,
 * flDecayTime, flDecayHFRatio, lReflections, flReflectionsDelay, lReverb,
 * flReverbDelay, flDiffusion, flDensity, flHFReference). The DSP that runs the
 * reverb is not emulated, so the host reverb (apu_reverb.cpp) takes them; then the
 * game's own call proceeds. */
void sub_0036604A(void)
{
    extern void xa2_reverb_set_i3dl2(const void *params);
    extern void sub_0036604A_gen(void);
    const uint32_t params = MEM32(esp + 8);
    if (params >= 0x1000 && params < 0x8000000u) {
        uint32_t p[12];
        for (int k = 0; k < 12; k++) p[k] = MEM32(params + 4u * (uint32_t)k);
        xa2_reverb_set_i3dl2(p);
    }
    sub_0036604A_gen();
}

/* D3DDevice_Swap (0x0034AF80): run the real Swap, present (or tick the movie),
 * then model the vblank (event + callback). */
void sub_0034AF80(void)
{
    extern void sub_0034AF80_gen(void);
    extern void pgraph_d3d11_flush(void);
    extern void doa2u_present_frame(void);
    extern void doa2u_movie_tick(void);
    extern int  doa2u_movie_active(void);
    uint32_t sv_esi = esi, sv_edi = edi, sv_ebx = ebx;
    uint32_t dev;
    sub_0034AF80_gen();
    if (!rt_enabled() || doa2u_movie_active()) {
        /* 60 Hz pacing (d3d/frame_pacer.h): the display or a 60 Hz timer is the clock.
         * With the drawing thread presenting, it paces there instead (rt_main). */
        extern void d3d8_frame_pace(int extra_us, int force_timer);
        rt_sync();
        d3d8_frame_pace(0, 0);
        pb_age_reports();
    }
    {
        uint32_t ret_eax = eax, ret_esp = esp;
        dev = MEM32(D3D_G_PDEVICE);
        d3d_publish_vblank(dev);
        {   /* Host-EOF trigger: the guest Sofdec parks in PLAYING at a movie's end, so once
             * the presenter hits EOF set SFD state 6 / mwPly state 3 (PLAYEND). */
            extern int g_doa2u_host_movie_ended;
            uint32_t mp = MEM32(0xA942D8);                 /* mwPly handle */
            if (g_doa2u_host_movie_ended && mp >= 0x1000 && mp < 0x8000000u &&
                MEM32(mp + 8) == 2) {
                uint32_t sfd = MEM32(mp + 0x40);
                if (sfd >= 0x1000 && sfd < 0x8000000u && MEM32(sfd + 0x48) == 4) {
                    MEM32(sfd + 0x48) = 6;                 /* SFD handle: PLAYEND */
                    MEM32(mp + 8) = 3;                     /* movie object: finished */
                }
            }
        }
        if (doa2u_movie_active()) {
            extern volatile LONG g_doa2u_heartbeat;
            InterlockedIncrement(&g_doa2u_heartbeat);
            rt_sync();
            doa2u_movie_tick();
        } else if (rt_enabled()) {
            /* Hand the frame to the drawing thread and let the game run one frame ahead. */
            extern void doa2u_present_game_side(void);
            extern void doa2u_ui_build(void);
            RtItem *it;
            doa2u_present_game_side();
            doa2u_ui_build();
            if (g_perf_on) perf_frame();
            it = rt_slot();
            it->present = 1;
            InterlockedIncrement(&s_rt_present_enq);
            rt_publish();
            rt_wait(RT_QN + 1, 1);
            pb_run_callbacks();
        } else {
            pgraph_d3d11_flush();
            doa2u_present_frame();
        }
        {   extern void doa2u_rdtsc_frame(void); doa2u_rdtsc_frame(); }   /* one vblank of rdtsc time */
        if (dev) {
            uint32_t ev = dev + DEV_VBLANK_EVENT_OFS;
            uint32_t cb = MEM32(dev + DEV_VBLANK_CB_OFS);
            MEM32(ev + 4) = 1;                      /* KEVENT.SignalState */
            xbox_fiber_wake(ev);
            if (cb) {
                recomp_func_t fn = recomp_lookup_manual(cb);
                if (!fn) fn = recomp_lookup(cb);
                if (fn) {
                    /* D3DVBLANKDATA { VBlank, Swap, Flags } in guest memory */
                    static uint32_t s_vbdata = 0, s_vbcount = 0;
                    uint32_t saved = esp;
                    if (!s_vbdata) s_vbdata = xbox_HeapAlloc(16, 16);
                    if (s_vbdata) {
                        MEM32(s_vbdata) = ++s_vbcount;
                        MEM32(s_vbdata + 4) = s_vbcount;
                        MEM32(s_vbdata + 8) = 0;
                    }
                    PUSH32(esp, s_vbdata);
                    PUSH32(esp, 0);                 /* dummy return */
                    fn();
                    esp = saved;
                }
            }
        }
        {   /* Extras changed the profile: run the game's own save of the loaded
             * profile (sub_00023D00, as after a settings change) on this thread. */
            extern int doa2u_extras_take_save_request(void);
            extern void sub_00023D00(void);
            if (doa2u_extras_take_save_request()) {
                uint32_t saved = esp, sv_seh = g_seh_ebp;
                PUSH32(esp, 0);                     /* dummy return */
                sub_00023D00();
                fprintf(stderr, "[EXTRAS] profile save -> %d\n", (int)eax);
                fflush(stderr);
                esp = saved; g_seh_ebp = sv_seh;
            }
        }
        eax = ret_eax; esp = ret_esp;
    }
    esi = sv_esi; edi = sv_edi; ebx = sv_ebx;
}

/* ── CRT _cinit (0x002BD44C) ────────────────────────────────────────── */
/* _cinit: run _FPinit and the __xi/__xc initializer tables, restoring the callee-saved
 * registers after each (one initializer leaks esi). */
static void cinit_call(uint32_t fn_va, uint32_t slot)
{
    recomp_func_t fn = recomp_lookup_manual(fn_va);
    uint32_t sv_esp = esp, sv_esi = esi, sv_edi = edi, sv_ebx = ebx;
    if (!fn) fn = recomp_lookup(fn_va);
    if (!fn) {
        fprintf(stderr, "[CINIT] slot 0x%08X: no body for 0x%08X\n", slot, fn_va);
        return;
    }
    PUSH32(esp, 0);
    fn();
    if (esp != sv_esp || esi != sv_esi || edi != sv_edi || ebx != sv_ebx)
        fprintf(stderr, "[CINIT] slot 0x%08X fn 0x%08X broke the ABI: esp %+d esi %08X->%08X "
                        "edi %08X->%08X ebx %08X->%08X\n", slot, fn_va, (int)(esp - sv_esp),
                sv_esi, esi, sv_edi, edi, sv_ebx, ebx);
    esp = sv_esp; esi = sv_esi; edi = sv_edi; ebx = sv_ebx;
}

void sub_002BD44C(void)
{
    static const uint32_t tables[2][2] = {
        { 0x004976B4u, 0x004976CCu },   /* C initializers */
        { 0x00497590u, 0x004976B0u },   /* C++ constructors */
    };
    uint32_t fp = MEM32(0x007C969Cu), p;
    int t;
    if (fp) cinit_call(fp, 0x007C969Cu);
    for (t = 0; t < 2; t++)
        for (p = tables[t][0]; p < tables[t][1]; p += 4) {
            uint32_t fn = MEM32(p);
            if (fn && fn != 0xFFFFFFFFu) cinit_call(fn, p);
        }
    fflush(stderr);
    esp += 4;   /* ret */
}

/* ADXF_GetPtStat (0x2FBAE0): the post-install mount spins on it
 * without yielding; pulse vblank and yield to the CRI workers until it's done. */
void sub_002FBAE0(void)
{
    extern void sub_002FBAE0_gen(void);
    extern uint32_t g_xbox_vblank_event_va;
    sub_002FBAE0_gen();
    if (eax != 3) {
        if (g_xbox_vblank_event_va) {
            MEM32(g_xbox_vblank_event_va + 4) = 1;  /* vblank KEVENT.SignalState */
            xbox_fiber_wake(g_xbox_vblank_event_va);
        }
        {   /* no presents happen during this spin -- service the window so it
             * doesn't go "Not Responding" under the user's clicks */
            extern void doa2u_pump_messages(void);
            static unsigned s_mp2 = 0;
            if ((++s_mp2 & 63) == 0) doa2u_pump_messages();
        }
        if (xbox_fiber_active())
            xbox_fiber_yield();
    }
}

/* ── XPP (Xbox Peripheral Port) input overrides — host-backed pads ─────────
 * There is no USB stack behind the XAPI pad calls, so the
 * gamepad ones are served from the host (src/input xbox_InputGetState: XInput
 * pads plus the Esc-menu keyboard mapping on port 0). Names from
 * tools/recomp/doa2u_cxbx_symbols.ini.
 *
 * The game's pad structs: base 0xA94838, stride 0x80. +0x00 caps blob
 * (XInputGetCapabilities), +0x19 XINPUT_STATE (XInputGetState), +0x2F
 * XINPUT_FEEDBACK (rumble motors at +0x71/+0x73), +0x78 XInputOpen handle.
 * Opened/polled by sub_000FA5D0 (boot) and sub_000FA6C0 (every frame).
 * Other device types (memory units, 0x3B83E0) keep the generated bodies. */
#define XPP_TYPE_GAMEPAD  0x003B845Cu
#define XPP_HANDLE_BASE   0x0AD00001u   /* fake but consistent nonzero handles */

static uint32_t s_xpp_prev_mask;   /* XGetDeviceChanges baseline (real XAPI PreviousConnected) */

static int xpp_handle_port(uint32_t h)
{
    return (h >= XPP_HANDLE_BASE && h < XPP_HANDLE_BASE + 4) ? (int)(h - XPP_HANDLE_BASE) : -1;
}

void sub_003B9913_gen(void);
void sub_003B9913(void)   /* XGetDevices(type) -> connected mask, stdcall ret 4 */
{
    extern DWORD xbox_InputHostMask(void);
    if (MEM32(esp + 4) != XPP_TYPE_GAMEPAD) { sub_003B9913_gen(); return; }
    /* Real XAPI semantics (cxbx Xapi.cpp): reporting the connected set also
     * resets the change baseline, so XGetDeviceChanges does not re-report
     * these pads as insertions. */
    s_xpp_prev_mask = xbox_InputHostMask();
    eax = s_xpp_prev_mask;
    esp += 8;
}

void sub_003B9935_gen(void);
void sub_003B9935(void)   /* XGetDeviceChanges(type, &ins, &rem), stdcall ret 12 */
{
    extern DWORD xbox_InputHostMask(void);
    uint32_t p_ins, p_rem, cur, ins, rem;
    if (MEM32(esp + 4) != XPP_TYPE_GAMEPAD) { sub_003B9935_gen(); return; }
    /* Both outputs are written on every call -- zero when nothing changed. */
    p_ins = MEM32(esp + 8);
    p_rem = MEM32(esp + 12);
    cur = xbox_InputHostMask();
    ins = cur & ~s_xpp_prev_mask;
    rem = s_xpp_prev_mask & ~cur;
    s_xpp_prev_mask = cur;
    if (p_ins) MEM32(p_ins) = ins;
    if (p_rem) MEM32(p_rem) = rem;
    eax = (ins | rem) ? 1u : 0u;
    esp += 16;
}

void sub_003B9E12_gen(void);
void sub_003B9E12(void)   /* XInputOpen(type, port, slot, params) -> handle, stdcall ret 16 */
{
    uint32_t port = MEM32(esp + 8);
    if (MEM32(esp + 4) != XPP_TYPE_GAMEPAD) { sub_003B9E12_gen(); return; }
    eax = port < 4 ? XPP_HANDLE_BASE + port : 0;
    esp += 20;
}

void sub_003B9E68_gen(void);
void sub_003B9E68(void)   /* XInputClose(handle), stdcall ret 4 */
{
    if (xpp_handle_port(MEM32(esp + 4)) < 0) { sub_003B9E68_gen(); return; }
    eax = 0;
    esp += 8;
}

void sub_003B9E74_gen(void);
void sub_003B9E74(void)   /* XInputGetCapabilities(handle, caps), stdcall ret 8 */
{
    uint32_t caps = MEM32(esp + 8), i;
    if (xpp_handle_port(MEM32(esp + 4)) < 0) { sub_003B9E74_gen(); return; }
    if (caps) {
        for (i = 0; i < 0x19; i++) MEM8(caps + i) = 0;
        MEM8(caps) = 1;   /* XINPUT_DEVSUBTYPE_GC_GAMEPAD */
    }
    eax = 0;
    esp += 12;
}

void sub_003BA04C_gen(void);
void sub_003BA04C(void)   /* XInputGetState(handle, state), stdcall ret 8 */
{
    extern DWORD xbox_InputGetState(DWORD, void *);
    static uint32_t s_packet = 0x1000;
    uint32_t st = MEM32(esp + 8);
    int port = xpp_handle_port(MEM32(esp + 4)), i;
    uint8_t raw[24] = {0};   /* XBOX_INPUT_STATE: packet, wButtons, 8 analog, 4 thumbs */
    if (port < 0) { sub_003BA04C_gen(); return; }
    if (xbox_InputGetState((DWORD)port, raw) != 0) {
        eax = 0x48F;   /* ERROR_DEVICE_NOT_CONNECTED */
        esp += 12;
        return;
    }
    if (st) {
        MEM32(st) = ++s_packet;                       /* dwPacketNumber */
        for (i = 0; i < 18; i++) MEM8(st + 4 + i) = raw[4 + i];   /* XINPUT_GAMEPAD */
    }
    eax = 0;
    esp += 12;
}

void sub_003BA0BF_gen(void);
void sub_003BA0BF(void)   /* XInputSetState(handle, feedback), stdcall ret 8 */
{
    extern DWORD xbox_InputSetState(DWORD, const void *);
    uint32_t fb = MEM32(esp + 8);
    int port = xpp_handle_port(MEM32(esp + 4));
    if (port < 0) { sub_003BA0BF_gen(); return; }
    if (fb) {
        /* XINPUT_FEEDBACK: Status at +0, rumble motors after the 0x42-byte
         * header. The real call completes asynchronously (Status 0x3E5 until
         * then); this one completes at once. */
        uint16_t motors[2];
        motors[0] = MEM16(fb + 0x42);
        motors[1] = MEM16(fb + 0x44);
        xbox_InputSetState((DWORD)port, motors);
        MEM32(fb) = 0;
    }
    eax = 0;
    esp += 12;
}

/* ── Startup hook (main.c, before the entry point) ──────────────────── */
/* Legal screen (0xD92C0) stubbed. On a first boot, wait here a game frame at a time
 * for the HDD cache copy (it pauses during movies), then go on to the Team Ninja FMV. */
void sub_000D92C0(void)
{
    extern uint32_t g_xbox_cache_install_routine;
    extern int xbox_fiber_thread_alive(uint32_t ctx1);
    while (MEM32(0x8A9908) && g_xbox_cache_install_routine &&
           xbox_fiber_thread_alive(g_xbox_cache_install_routine)) {
        PUSH32(esp, 1);
        PUSH32(esp, 0);
        sub_000FA3D0();
        esp += 4;
    }
    esp += 4;   /* ret */
}

void doa2u_game_init(void)
{
    /* The device is the static 0x355500, so its vblank event is known before the first Swap. */
    extern uint32_t g_xbox_vblank_event_va;
    extern uint32_t g_xbox_cache_install_routine;
    extern volatile int g_fib_slice_due;
    g_xbox_vblank_event_va = 0x00355500u + DEV_VBLANK_EVENT_OFS;
    /* First-boot HDD cache copy worker, started by sub_000F7390. */
    g_xbox_cache_install_routine = 0x000F70C0u;
    g_fib_slice_due = 1;   /* first slice check starts the 4 ms slice timer */
}
