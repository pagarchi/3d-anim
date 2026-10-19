// NeonAura.asi - menu (single page) connected to the NeonAura engine. Still no intro animation.
// The design is taken from the BodyTex menu: neon purple glass panel, sweeping beam + rising sparks,
// RGB borders, "create by" header with the Watch Dogs 2 glitch name, big buttons with shine,
// right-hand status panel, gold-cross cursor with RGB ring.
// Type 3 and D one after another to open / close the menu (camera frozen, free mouse cursor).
//   The settings (mode, trails, glitch, FOV calibration, ignore game checks) are the same ones
//   the NeonAura engine uses (F5..F11), so the next step can simply read them.
#include "plugin.h"
#include "common.h"
#include "CTimer.h"
#include "CFont.h"
#include "CSprite2d.h"
#include "CPad.h"
#include "CRect.h"
#include "CRGBA.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <math.h>
#include <d3d9.h>

// ---------------- config ----------------
static const char* LOG_FILE = "NeonAura.log";

// log file goes next to the .asi itself (works even if the game is started from another folder)
static void LogPath(char* out, size_t n) {
    out[0] = 0;
    HMODULE hm = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&LogPath, &hm) && hm) {
        char full[MAX_PATH]; DWORD l = GetModuleFileNameA(hm, full, MAX_PATH);
        if (l > 0 && l < MAX_PATH) {
            char* sl = strrchr(full, '\\'); if (sl) { *(sl + 1) = 0; strncpy(out, full, n - 1); out[n - 1] = 0; }
        }
    }
    strncat(out, LOG_FILE, n - strlen(out) - 1);
}
static void Log(const char* fmt, ...) {
    char lp[MAX_PATH + 64]; LogPath(lp, sizeof(lp));
    FILE* f = fopen(lp, "a");
    if (!f) f = fopen(LOG_FILE, "a");
    if (!f) return;
    va_list a; va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fputc('\n', f);
    fclose(f);
}

// ---------------- small math helpers ----------------
static float Hh(int a, int b = 0, int c = 0) {
    unsigned x = (unsigned)a * 374761393u + (unsigned)b * 668265263u + (unsigned)c * 2147483647u;
    x = (x ^ (x >> 13)) * 1274126177u;
    x ^= x >> 16;
    return (x & 0xFFFF) / 65535.0f;
}
static inline float Cl(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline float Fr(float x) { return x - floorf(x); }
static void Hsv(float h, float s, float v, float& r, float& g, float& b) {
    h = h - floorf(h);
    float k = h * 6.0f; int i = (int)k; float f = k - i;
    float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

// ---------------- NeonAura settings (what the menu edits) ----------------
static const int N_MODES = 6;
static const char* const MODE_NAME[N_MODES]  = {"Auto  (all in turn)", "Golden Dragon", "Eagles", "Simurgh", "Animals", "All together"};
static const char* const MODE_SHORT[N_MODES] = {"Auto", "Dragon", "Eagles", "Simurgh", "Animals", "All"};
static const char* const MODE_DESC[N_MODES]  = {"switches by itself", "long golden serpent", "neon skeleton eagles", "mythic neon bird", "leopard, cheetah, wolf", "everything at once"};

static bool  g_on     = true;      // master switch (F5)
static int   g_cmode  = 0;         // creature mode (F6): 0 auto, 1 dragon, 2 eagles, 3 simurgh, 4 animals, 5 all
static bool  g_trails = true;      // light trails (F7)
static bool  g_glitch = true;      // glitch frame (F8)
static float g_fovCal = 1.0f;      // projection calibration (F9 / F10)
static bool  g_force  = false;     // ignore game-state checks (F11, debug)

// ============================================================================
//  NeonAura engine (golden dragon, eagles, Simurgh, animals, trails, aura, glitch) - drawn as a
//  Direct3D9 overlay. It lives in its own namespace and reads the settings of the menu above.
// ============================================================================
namespace aura {
static const float PI_ = 3.14159265f;

// ---------------------------------------------------------------- math
struct V3 { float x, y, z; };
static inline V3 v3(float x, float y, float z) { V3 r = { x, y, z }; return r; }
static inline V3 operator+(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 operator-(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 operator*(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float len(V3 a) { return sqrtf(dot(a, a)); }
static inline V3 Nz(V3 a) { float l = len(a); return l > 1e-6f ? a * (1.f / l) : v3(0, 0, 1); }
static inline V3 lerp3(V3 a, V3 b, float t) { return a + (b - a) * t; }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float sm(float t) { t = clampf(t, 0.f, 1.f); return t * t * (3.f - 2.f * t); }

static uint32_t g_rs = 12345u;
static float frand() { g_rs ^= g_rs << 13; g_rs ^= g_rs >> 17; g_rs ^= g_rs << 5; return (g_rs & 0xFFFFFF) / 16777216.f; }

struct Col { float r, g, b; };
static inline Col col(float r, float g, float b) { Col c = { r, g, b }; return c; }

// ---------------------------------------------------------------- renderer
struct Vtx { float x, y, z, rhw; DWORD c; };
#define AU_MAXV 59990
static Vtx g_vb[AU_MAXV + 16];
static int g_n = 0;
static IDirect3DDevice9* g_dev = NULL;

static V3 g_cp, g_cr, g_cu, g_cf;          // camera pos / right / up / forward
static float g_sw = 800, g_sh = 600, g_focal = 600;
static float g_jx = 0, g_jy = 0, g_fade = 1.f;

static DWORD PK(float r, float g, float b, float a) {
    a *= g_fade; a = clampf(a, 0.f, 1.f);
    return D3DCOLOR_ARGB((int)(a * 255.f), (int)clampf(r, 0.f, 255.f), (int)clampf(g, 0.f, 255.f), (int)clampf(b, 0.f, 255.f));
}
static void Flush() {
    if (g_n >= 3 && g_dev) g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, g_n / 3, g_vb, sizeof(Vtx));
    g_n = 0;
}
static inline void PV(float x, float y, DWORD c) {
    Vtx& v = g_vb[g_n++]; v.x = x; v.y = y; v.z = 0.f; v.rhw = 1.f; v.c = c;
}
static inline void Tri(float x0, float y0, DWORD c0, float x1, float y1, DWORD c1, float x2, float y2, DWORD c2) {
    if (g_n + 3 > AU_MAXV) Flush();
    PV(x0, y0, c0); PV(x1, y1, c1); PV(x2, y2, c2);
}
static void SegS(float x0, float y0, float x1, float y1, float w, DWORD c0, DWORD c1) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-4f) { dx = 1; dy = 0; l = 1; }
    dx /= l; dy /= l;
    float px = -dy * w * 0.5f, py = dx * w * 0.5f, ex = dx * w * 0.5f, ey = dy * w * 0.5f;
    x0 -= ex; y0 -= ey; x1 += ex; y1 += ey;
    Tri(x0 + px, y0 + py, c0, x0 - px, y0 - py, c0, x1 + px, y1 + py, c1);
    Tri(x1 + px, y1 + py, c1, x0 - px, y0 - py, c0, x1 - px, y1 - py, c1);
}
static bool Proj(V3 p, float& sx, float& sy, float& z) {
    V3 d = p - g_cp;
    z = dot(d, g_cf);
    if (z < 0.3f) return false;
    float x = dot(d, g_cr), y = dot(d, g_cu);
    sx = g_sw * 0.5f + x / z * g_focal + g_jx;
    sy = g_sh * 0.5f - y / z * g_focal + g_jy;
    return true;
}
// glowing neon bone between two world points
static void Neon(V3 a, V3 b, float ww, Col c, float a0, float a1 = -1.f) {
    if (a1 < 0.f) a1 = a0;
    const float NZ = 0.4f;
    float za = dot(a - g_cp, g_cf), zb = dot(b - g_cp, g_cf);
    if (za < NZ && zb < NZ) return;
    if (za < NZ) { float t = (NZ - za) / (zb - za); a = a + (b - a) * t; a0 = a0 + (a1 - a0) * t; }
    else if (zb < NZ) { float t = (NZ - zb) / (za - zb); b = b + (a - b) * t; a1 = a1 + (a0 - a1) * t; }
    float ax, ay, az, bx, by, bz;
    if (!Proj(a, ax, ay, az) || !Proj(b, bx, by, bz)) return;
    float z = (az + bz) * 0.5f;
    if (z > 230.f) return;
    float w = ww * g_focal / z;
    float wg = clampf(w * 3.2f + 2.f, 2.f, 70.f), wm = clampf(w * 1.5f + 1.f, 1.f, 40.f), wc = clampf(w * 0.6f + 0.9f, 0.9f, 20.f);
    SegS(ax, ay, bx, by, wg, PK(c.r, c.g, c.b, a0 * 0.16f), PK(c.r, c.g, c.b, a1 * 0.16f));
    SegS(ax, ay, bx, by, wm, PK(c.r, c.g, c.b, a0 * 0.42f), PK(c.r, c.g, c.b, a1 * 0.42f));
    Col h = col(c.r + (255.f - c.r) * 0.65f, c.g + (255.f - c.g) * 0.65f, c.b + (255.f - c.b) * 0.65f);
    SegS(ax, ay, bx, by, wc, PK(h.r, h.g, h.b, a0), PK(h.r, h.g, h.b, a1));
}
static void Poly(const V3* p, int n, float ww, Col c, float a) { for (int i = 0; i < n - 1; i++) Neon(p[i], p[i + 1], ww, c, a); }
static void PolyF(const V3* p, int n, float ww, Col c, float a0, float a1) {
    for (int i = 0; i < n - 1; i++) {
        float t0 = (float)i / (n - 1), t1 = (float)(i + 1) / (n - 1);
        Neon(p[i], p[i + 1], ww, c, lerpf(a0, a1, t0), lerpf(a0, a1, t1));
    }
}
// soft glowing orb (joint / eye / pearl)
static void Orb(V3 p, float rw, Col c, float a) {
    float sx, sy, z;
    if (!Proj(p, sx, sy, z) || z > 230.f) return;
    float r = clampf(rw * g_focal / z, 1.6f, 300.f);
    for (int pass = 0; pass < 2; pass++) {
        float rr = pass == 0 ? r : r * 0.38f;
        Col cc = pass == 0 ? c : col(c.r + (255.f - c.r) * 0.7f, c.g + (255.f - c.g) * 0.7f, c.b + (255.f - c.b) * 0.7f);
        DWORD c0 = PK(cc.r, cc.g, cc.b, a * (pass == 0 ? 0.55f : 0.95f)), c1 = PK(cc.r, cc.g, cc.b, 0.f);
        const int K = 10;
        for (int i = 0; i < K; i++) {
            float a0 = i * 2.f * PI_ / K, a1 = (i + 1) * 2.f * PI_ / K;
            Tri(sx, sy, c0, sx + cosf(a0) * rr, sy + sinf(a0) * rr, c1, sx + cosf(a1) * rr, sy + sinf(a1) * rr, c1);
        }
    }
}

// ---------------------------------------------------------------- local frame
struct Fr {
    V3 o, F, L, U; float sc;
    V3 W(float x, float y, float z) const { return o + F * (x * sc) + L * (y * sc) + U * (z * sc); }
};

// ================================================================ DRAGON (golden Eastern dragon)
static const Col AU_GOLD = { 255.f, 190.f, 50.f }, AU_GOLD2 = { 255.f, 228.f, 130.f }, AU_FIRE = { 255.f, 115.f, 30.f };

static V3 DragonRel(float t) {
    float a = 0.9f * t;
    float R = 8.5f + 2.6f * sinf(0.37f * t) + 1.2f * sinf(0.9f * t + 1.f);
    float h = 3.3f + 2.2f * sinf(0.53f * t + 0.6f) + 0.8f * sinf(1.3f * t);
    return v3(R * cosf(a), R * sinf(a), h);
}
static float DRad(float s) {
    if (s < 0.12f) return 0.30f + 0.10f * (s / 0.12f);
    if (s < 0.45f) return 0.40f + 0.16f * sm((s - 0.12f) / 0.33f);
    float k = (s - 0.45f) / 0.55f;
    return 0.56f * (1.f - sm(k)) + 0.025f;
}
static void Dragon(V3 car, float T) {
    enum { N = 112 };
    V3 P[N], Q[N], Tn[N], Sd[N], Nm[N];
    for (int i = 0; i < N; i++) P[i] = car + DragonRel(T - i * 0.034f);
    for (int i = 0; i < N; i++) { V3 a = P[i > 0 ? i - 1 : 0], b = P[i < N - 1 ? i + 1 : N - 1]; Tn[i] = Nz(a - b); }
    for (int i = 0; i < N; i++) {
        float s = (float)i / (N - 1);
        V3 sd = Nz(cross(Tn[i], v3(0, 0, 1))); V3 nm = cross(sd, Tn[i]);
        float amp = 0.12f + 0.75f * sm(s * 2.f), ph = 0.36f * i - 4.4f * T;
        Q[i] = P[i] + nm * (amp * sinf(ph)) + sd * (0.4f * amp * cosf(ph));
    }
    for (int i = 0; i < N; i++) {
        V3 a = Q[i > 0 ? i - 1 : 0], b = Q[i < N - 1 ? i + 1 : N - 1];
        Tn[i] = Nz(a - b); Sd[i] = Nz(cross(Tn[i], v3(0, 0, 1))); Nm[i] = cross(Sd[i], Tn[i]);
    }
    // spine (dorsal line) + vertebra joints
    for (int i = 0; i < N - 1; i++) {
        float s = (float)i / (N - 1);
        Neon(Q[i], Q[i + 1], 0.035f + 0.07f * DRad(s), AU_GOLD2, 0.95f);
        if (i % 4 == 0) Orb(Q[i], 0.05f + 0.09f * DRad(s), AU_GOLD, 0.5f);
    }
    // rib cage (rings around body tube) along neck + torso
    for (int i = 5; i < 80; i += 2) {
        float r = DRad((float)i / (N - 1));
        V3 C = Q[i] - Nm[i] * (r * 0.9f);
        static const float ang[5] = { -1.35f, -0.7f, -0.05f, 0.65f, 1.3f };
        for (int sg = -1; sg <= 1; sg += 2) {
            V3 pts[5];
            for (int k = 0; k < 5; k++) pts[k] = C + Sd[i] * (sg * r * 0.85f * cosf(ang[k])) + Nm[i] * (-r * sinf(ang[k]));
            Poly(pts, 5, 0.02f + r * 0.03f, AU_GOLD, 0.8f);
        }
    }
    // belly line + scutes
    {
        V3 prev = Q[5] - Nm[5] * (DRad(5.f / (N - 1)) * 1.9f);
        for (int i = 6; i < N; i++) {
            float r = DRad((float)i / (N - 1));
            V3 cur = Q[i] - Nm[i] * (r * 1.9f);
            Neon(prev, cur, 0.025f, AU_FIRE, 0.7f);
            if (i % 3 == 0) Neon(cur + Sd[i] * (r * 0.45f), cur - Sd[i] * (r * 0.45f), 0.015f, AU_GOLD, 0.55f);
            prev = cur;
        }
    }
    // dorsal spines / mane
    {
        V3 tips[60]; int nt = 0;
        for (int i = 3; i < N - 6; i += 2) {
            float s = (float)i / (N - 1), r = DRad(s);
            float h = 0.12f + 0.35f * (r / 0.56f) + (i < 30 ? 0.35f : 0.f);
            V3 base = Q[i] + Nm[i] * 0.02f;
            V3 tip = base + Nm[i] * h - Tn[i] * (h * 0.8f) + Sd[i] * (0.06f * sinf(0.5f * i - 6.f * T));
            Neon(base, tip, 0.028f, AU_FIRE, 0.9f, 0.5f);
            if (nt < 60) tips[nt++] = tip;
        }
        Poly(tips, nt, 0.012f, AU_FIRE, 0.45f);
    }
    // tail tuft
    for (int k = 0; k < 10; k++) {
        int idx = N - 1 - k;
        for (int sg = -1; sg <= 1; sg += 2) {
            V3 dir = Nz(Tn[idx] * -0.7f + Sd[idx] * (sg * 0.5f) + Nm[idx] * 0.1f);
            float L2 = 1.4f - 0.1f * k;
            V3 b0 = Q[idx];
            V3 mid = b0 + dir * (L2 * 0.5f) + Nm[idx] * (0.15f * sinf(3.f * T - k * 0.7f));
            V3 tip = b0 + dir * L2 + Nm[idx] * (0.35f * sinf(3.f * T - k * 0.7f + 0.8f));
            V3 pp[3] = { b0, mid, tip };
            PolyF(pp, 3, 0.014f, AU_FIRE, 0.85f, 0.15f);
        }
    }
    // four legs, three claws
    {
        int li[2] = { 26, 62 };
        for (int q = 0; q < 2; q++) for (int sg = -1; sg <= 1; sg += 2) {
            int i = li[q]; float r = DRad((float)i / (N - 1));
            float ph = 5.2f * T + q * 1.7f + (sg > 0 ? 0.f : 1.9f);
            V3 sh = Q[i] - Nm[i] * (r * 1.2f) + Sd[i] * (sg * r * 0.55f);
            V3 el = sh + Sd[i] * (sg * 0.26f) - Nm[i] * 0.34f - Tn[i] * (0.10f + 0.20f * sinf(ph));
            V3 wr = el + Sd[i] * (sg * 0.14f) - Nm[i] * 0.30f - Tn[i] * (0.05f + 0.18f * sinf(ph - 1.0f));
            Neon(sh, el, 0.04f, AU_GOLD, 0.9f); Neon(el, wr, 0.032f, AU_GOLD, 0.9f);
            Orb(sh, 0.07f, AU_GOLD2, 0.7f); Orb(el, 0.05f, AU_GOLD2, 0.6f); Orb(wr, 0.05f, AU_GOLD2, 0.6f);
            for (int j = -1; j <= 1; j++) {
                V3 kn = wr + Tn[i] * 0.20f + Sd[i] * (sg * 0.06f + j * 0.11f) - Nm[i] * 0.10f;
                V3 tip = kn + Tn[i] * 0.12f + Sd[i] * (j * 0.05f) - Nm[i] * 0.12f;
                Neon(wr, kn, 0.02f, AU_GOLD2, 0.9f); Neon(kn, tip, 0.014f, AU_GOLD2, 0.95f);
            }
        }
    }
    // head
    {
        V3 H0 = Q[0] - Nm[0] * 0.18f; float hs = 0.62f;
#define HP_(x, y, z) (H0 + Tn[0] * ((x) * hs) + Nm[0] * ((y) * hs) + Sd[0] * ((z) * hs))
        float open = 0.10f + 0.08f * sinf(1.7f * T);
        V3 ridge[4] = { HP_(0.15f, 0.45f, 0.f), HP_(0.55f, 0.55f, 0.f), HP_(1.2f, 0.42f, 0.f), HP_(1.8f, 0.22f, 0.f) };
        Poly(ridge, 4, 0.035f, AU_GOLD2, 0.95f);
        for (int sg = -1; sg <= 1; sg += 2) {
            float z = (float)sg;
            V3 brow[4] = { HP_(0.15f, 0.45f, z * 0.28f), HP_(0.55f, 0.55f, z * 0.28f), HP_(1.2f, 0.42f, z * 0.16f), HP_(1.8f, 0.22f, z * 0.10f) };
            Poly(brow, 4, 0.03f, AU_GOLD2, 0.9f);
            V3 uj[4] = { HP_(1.8f, 0.15f, z * 0.12f), HP_(1.2f, 0.08f, z * 0.24f), HP_(0.55f, 0.08f, z * 0.34f), HP_(0.12f, 0.12f, z * 0.34f) };
            Poly(uj, 4, 0.03f, AU_GOLD, 0.95f);
            static const float lx[4] = { 0.12f, 0.7f, 1.25f, 1.6f }, ly[4] = { 0.05f, -0.05f, -0.1f, -0.1f }, lz[4] = { 0.34f, 0.27f, 0.16f, 0.04f };
            V3 lj[4];
            for (int k = 0; k < 4; k++) lj[k] = HP_(lx[k], ly[k] - (lx[k] - 0.12f) * open * 1.2f, z * lz[k]);
            Poly(lj, 4, 0.028f, AU_GOLD, 0.9f);
            Neon(uj[3], lj[0], 0.03f, AU_GOLD, 0.9f);
            // fangs
            Neon(HP_(1.55f, 0.12f, z * 0.12f), HP_(1.57f, -0.14f, z * 0.12f), 0.014f, col(255, 255, 255), 0.95f);
            Neon(HP_(0.95f, 0.08f, z * 0.27f), HP_(0.97f, -0.10f, z * 0.27f), 0.012f, col(255, 255, 255), 0.9f);
            // eye ring + glowing eye
            V3 ec = HP_(0.72f, 0.38f, z * 0.33f);
            V3 ring[9];
            for (int k = 0; k < 9; k++) { float a = k * 2.f * PI_ / 8.f; ring[k] = ec + (Tn[0] * cosf(a) + Nm[0] * sinf(a)) * 0.05f; }
            Poly(ring, 9, 0.012f, AU_GOLD2, 0.9f);
            Orb(ec, 0.035f, col(255, 70, 40), 0.95f);
            // antlered horn
            V3 horn[4] = { HP_(0.35f, 0.52f, z * 0.22f), HP_(0.f, 0.9f, z * 0.32f), HP_(-0.55f, 1.05f, z * 0.4f), HP_(-1.1f, 0.95f, z * 0.42f) };
            PolyF(horn, 4, 0.03f, AU_GOLD2, 0.95f, 0.6f);
            Neon(horn[1], HP_(0.05f, 1.3f, z * 0.34f), 0.018f, AU_GOLD2, 0.85f, 0.5f);
            Neon(horn[2], HP_(-0.55f, 1.45f, z * 0.45f), 0.016f, AU_GOLD2, 0.85f, 0.5f);
            // frill behind jaw
            for (int j = 0; j < 4; j++)
                Neon(HP_(0.1f, 0.1f, z * 0.38f), HP_(-0.5f - 0.15f * j, 0.15f + 0.12f * j, z * (0.6f + 0.1f * j)) + Nm[0] * (0.05f * sinf(4.f * T + j)), 0.014f, AU_FIRE, 0.8f, 0.3f);
            // long whiskers
            V3 wk[12]; V3 p0 = HP_(1.45f, 0.12f, z * 0.2f);
            for (int k = 0; k < 12; k++)
                wk[k] = p0 - Tn[0] * (0.30f * k) + Sd[0] * (z * (0.18f * k)) + Nm[0] * (0.028f * k * sinf(0.7f * k - 6.f * T) - 0.02f * k);
            PolyF(wk, 12, 0.014f, AU_GOLD2, 0.9f, 0.1f);
        }
        // flaming pearl
        V3 pearl = Q[0] + Tn[0] * (5.5f + 0.6f * sinf(2.f * T)) + Nm[0] * (0.5f * sinf(3.f * T));
        Orb(pearl, 0.55f, col(255, 240, 160), 0.9f);
        for (int j = 0; j < 6; j++) {
            float a = T * 4.f + j * 1.047f;
            Orb(pearl + Sd[0] * (cosf(a) * 0.55f) + Nm[0] * (sinf(a) * 0.55f), 0.12f, AU_FIRE, 0.8f);
        }
#undef HP_
    }
}

// ================================================================ BIRDS (eagle / Simurgh)
struct BirdP { float sc, wing, neck, tail; int tailN, tailPts; float waveAmp; bool simurgh; Col c1, c2, c3; float rate; };

static V3 BirdPath(float t, float ph, float orbR, float h0) {
    float a = 0.5f * t + ph;
    float R = orbR + 2.5f * sinf(0.31f * t + ph * 1.7f);
    float hh = h0 + 1.6f * sinf(0.43f * t + ph);
    return v3(R * cosf(a), R * sinf(a), hh);
}
static void Bird(V3 car, float T, const BirdP& b, float ph, float orbR, float h0) {
    V3 p0 = car + BirdPath(T, ph, orbR, h0), p1 = car + BirdPath(T + 0.05f, ph, orbR, h0);
    V3 F = Nz(p1 - p0);
    V3 L = Nz(cross(v3(0, 0, 1), F)); V3 U = cross(F, L);
    float bank = 0.5f, cb = cosf(bank), sb = sinf(bank);
    Fr fr; fr.o = p0; fr.F = F; fr.L = L * cb - U * sb; fr.U = U * cb + L * sb; fr.sc = b.sc;
    float flap = sm(0.5f + 1.2f * sinf(0.55f * T + ph * 2.f));
    float A = 0.12f + 0.78f * flap, phi = b.rate * T + ph;
    float k = b.sc * b.wing;
    const Col c1 = b.c1, c2 = b.c2;

    // ---- wings
    for (int sg = -1; sg <= 1; sg += 2) {
        float s = (float)sg;
        float th1 = 0.10f + A * sinf(phi), th2 = 0.05f + 1.05f * A * sinf(phi - 0.6f), th3 = -0.03f + 1.3f * A * sinf(phi - 1.2f);
        float fold = flap * fmaxf(0.f, -sinf(phi - 0.4f));
        struct DD { static V3 D(const Fr& f, float th, float sw, float sgn) { return Nz(f.L * (sgn * cosf(th)) + f.U * sinf(th) - f.F * sw); } };
        V3 d1 = DD::D(fr, th1, 0.12f, s), d2 = DD::D(fr, th2, 0.18f + 0.5f * fold, s), d3 = DD::D(fr, th3, 0.25f + 0.7f * fold, s);
        V3 S = fr.W(0.20f, s * 0.07f, 0.07f);
        V3 E = S + d1 * (0.20f * k), Wr = E + d2 * (0.26f * k), Hd = Wr + d3 * (0.16f * k);
        Neon(S, E, 0.028f * b.sc, c1, 0.95f); Neon(E, Wr, 0.024f * b.sc, c1, 0.95f); Neon(Wr, Hd, 0.02f * b.sc, c1, 0.95f);
        Neon(E - fr.F * (0.03f * b.sc), Wr - fr.F * (0.03f * b.sc), 0.012f * b.sc, c1, 0.6f);
        Orb(S, 0.035f * b.sc, c2, 0.7f); Orb(E, 0.03f * b.sc, c2, 0.7f); Orb(Wr, 0.03f * b.sc, c2, 0.7f); Orb(Hd, 0.025f * b.sc, c2, 0.7f);
        V3 tips[22]; int nt = 0;
        for (int i = 0; i < 10; i++) {   // primaries, outermost first
            float t = i / 9.f;
            V3 base = lerp3(Hd, Wr, 0.06f + t * 0.8f);
            V3 dir = Nz(d3 * (1.f - 0.65f * t) - fr.F * (0.15f + 0.85f * t));
            float ln = (0.46f - 0.016f * i) * k;
            V3 bend = fr.U * (ln * (0.10f - 0.25f * flap * cosf(phi - 1.1f)));
            V3 pp[3] = { base, base + dir * (ln * 0.5f) + bend * 0.15f, base + dir * ln + bend };
            PolyF(pp, 3, 0.011f * b.sc, c1, 0.9f, 0.6f);
            tips[nt++] = pp[2];
        }
        for (int i = 0; i < 12; i++) {   // secondaries
            float t = i / 11.f;
            V3 base = lerp3(Wr, E, 0.05f + 0.9f * t);
            V3 dir = Nz(fr.F * -1.f + d2 * (0.30f * (1.f - t)));
            float ln = (0.30f - 0.06f * t) * k;
            V3 bend = fr.U * (ln * (0.08f - 0.20f * flap * cosf(phi - 0.8f)));
            V3 pp[3] = { base, base + dir * (ln * 0.5f) + bend * 0.15f, base + dir * ln + bend };
            PolyF(pp, 3, 0.011f * b.sc, c1, 0.9f, 0.6f);
            tips[nt++] = pp[2];
        }
        Poly(tips, nt, 0.007f * b.sc, c2, 0.45f);
    }

    // ---- spine / neck / head / beak
    static const float sx[11] = { -0.15f, -0.05f, 0.07f, 0.18f, 0.27f, 0.32f, 0.36f, 0.42f, 0.47f, 0.51f, 0.53f };
    static const float sz[11] = { 0.0f, 0.02f, 0.05f, 0.07f, 0.09f, 0.13f, 0.16f, 0.15f, 0.13f, 0.11f, 0.07f };
    V3 sp[11];
    for (int i = 0; i < 11; i++) {
        float x = sx[i], z = sz[i];
        if (i >= 5) {
            int ii = i <= 7 ? i : 7;
            x = sx[i] - sx[ii] + (0.27f + (sx[ii] - 0.27f) * b.neck);
            z = sz[i] - sz[ii] + (0.09f + (sz[ii] - 0.09f) * b.neck);
        }
        sp[i] = fr.W(x, 0.f, z);
    }
    for (int i = 0; i < 10; i++) Neon(sp[i], sp[i + 1], 0.024f * b.sc, c1, 0.95f);
    for (int i = 0; i < 11; i += 2) Orb(sp[i], 0.028f * b.sc, c2, 0.6f);
    Neon(sp[8] - fr.U * (0.015f * b.sc), sp[9] - fr.U * (0.013f * b.sc), 0.014f * b.sc, c1, 0.9f);
    {
        V3 eye = sp[7] + fr.F * (0.02f * b.sc) + fr.U * (0.01f * b.sc);
        for (int sg = -1; sg <= 1; sg += 2) Orb(eye + fr.L * (sg * 0.03f * b.sc), 0.018f * b.sc, col(255, 245, 140), 0.95f);
    }
    // ribs + keel
    for (int i = 0; i < 4; i++) {
        float x = 0.04f + 0.05f * i, zs = 0.03f + (x + 0.05f) * 0.22f;
        for (int sg = -1; sg <= 1; sg += 2) {
            float s = (float)sg;
            V3 pp[5] = { fr.W(x, 0, zs), fr.W(x, s * 0.040f, zs - 0.012f), fr.W(x, s * 0.058f, zs - 0.045f), fr.W(x, s * 0.045f, zs - 0.085f), fr.W(x, 0, zs - 0.105f) };
            Poly(pp, 5, 0.01f * b.sc, c1, 0.75f);
        }
    }
    { V3 kl[3] = { fr.W(0.02f, 0, -0.045f), fr.W(0.10f, 0, -0.065f), fr.W(0.20f, 0, -0.045f) }; Poly(kl, 3, 0.012f * b.sc, c1, 0.8f); }
    // tucked legs with talons
    for (int sg = -1; sg <= 1; sg += 2) {
        float s = (float)sg;
        V3 hip = fr.W(0.f, s * 0.04f, -0.02f), kn = fr.W(-0.04f, s * 0.05f, -0.06f), an = fr.W(-0.13f, s * 0.05f, -0.07f);
        Neon(hip, kn, 0.014f * b.sc, c1, 0.85f); Neon(kn, an, 0.012f * b.sc, c1, 0.85f);
        for (int j = -1; j <= 1; j++) Neon(an, fr.W(-0.19f, s * 0.05f + j * 0.02f, -0.09f), 0.008f * b.sc, c2, 0.85f);
    }
    // crest (Simurgh)
    if (b.simurgh) {
        for (int j = 0; j < 5; j++) {
            V3 base = sp[7] + fr.U * (0.02f * b.sc);
            V3 dir = Nz(fr.F * -0.7f + fr.U * (0.5f + 0.1f * j) + fr.L * ((j - 2) * 0.15f));
            V3 pp[4];
            for (int q = 0; q < 4; q++) pp[q] = base + dir * (0.07f * q * b.sc) + fr.U * (0.015f * q * sinf(3.f * T - q + j) * b.sc);
            PolyF(pp, 4, 0.01f * b.sc, b.c2, 0.95f, 0.5f);
            Orb(pp[3], 0.02f * b.sc, b.c3, 0.9f);
        }
    }
    // ---- tail
    {
        V3 base = fr.W(-0.14f, 0, 0.f);
        for (int j = 0; j < b.tailN; j++) {
            float u = (j + 0.5f) / b.tailN * 2.f - 1.f;
            V3 dir = Nz(fr.F * -1.f + fr.L * (u * (b.simurgh ? 0.30f : 0.40f)) - fr.U * 0.04f);
            float ln = b.simurgh ? b.tail * (0.65f + 0.35f * (1.f - fabsf(u))) * (1.f + 0.1f * sinf(j * 2.3f)) : b.tail * (1.f - 0.12f * fabsf(u));
            V3 pp[16]; int n = b.tailPts > 16 ? 16 : b.tailPts;
            for (int q = 0; q < n; q++) {
                float kf = (float)q / (n - 1);
                pp[q] = base + dir * (ln * kf * b.sc)
                    + fr.U * (sinf(3.1f * kf - 2.6f * T + u * 2.f + ph) * b.waveAmp * kf * b.sc - 0.08f * kf * kf * ln * b.sc)
                    + fr.L * (cosf(2.2f * kf - 2.f * T + u * 1.3f) * b.waveAmp * 0.5f * kf * b.sc);
            }
            PolyF(pp, n, 0.011f * b.sc, (b.simurgh && (j & 1)) ? c2 : c1, 0.9f, 0.45f);
            if (b.simurgh) Orb(pp[n - 1], 0.07f * b.sc, b.c3, 0.85f);
        }
    }
}

// ================================================================ RUNNING ANIMALS (leopard, cheetah, wolf)
struct QP { float sc, lg, tail, headS; Col c1, c2; };

static void IK2(float hx, float hz, float ax, float az, float a, float b, float dirSign, float& ex, float& ez, float& wx, float& wz) {
    float dx = ax - hx, dz = az - hz, d = sqrtf(dx * dx + dz * dz);
    float dmax = a + b - 0.002f, dmin = fabsf(a - b) + 0.002f;
    d = clampf(d, dmin, dmax);
    float base = atan2f(dz, dx);
    float c = clampf((a * a + d * d - b * b) / (2.f * a * d), -1.f, 1.f);
    float al = acosf(c), ang = base + dirSign * al;
    ex = hx + a * cosf(ang); ez = hz + a * sinf(ang);
    wx = hx + d * cosf(base); wz = hz + d * sinf(base);
}
static void Quad(V3 pos, V3 F, float g, const QP& q) {
    V3 L = Nz(cross(v3(0, 0, 1), F));
    Fr fr; fr.o = pos; fr.F = F; fr.L = L; fr.U = v3(0, 0, 1); fr.sc = q.sc;
    const float tw = 2.f * PI_ * g;
    const float bob = 0.035f * sinf(2.f * tw + 0.5f), pitch = 0.10f * sinf(tw + 0.6f);
    const float base = 0.40f + 0.22f * q.lg;
    struct ZS { static float f(float x, float base, float bob, float pitch, float tw) { return base + bob + pitch * x - 0.05f * sinf(tw + 0.9f) * (1.f - 4.f * x * x); } };
#define ZSP(x) ZS::f((x), base, bob, pitch, tw)
#define Q3(x, y, z) fr.W((x), (y), (z))
    const Col c1 = q.c1, c2 = q.c2; const float W1 = 0.016f, W2 = 0.012f;
    // spine
    V3 sp[13]; int n = 0;
    for (int i = 0; i < 10; i++) { float x = -0.50f + 0.10f * i; sp[n++] = Q3(x, 0.f, ZSP(x)); }
    float zn = ZSP(0.40f);
    sp[n++] = Q3(0.48f, 0.f, zn); sp[n++] = Q3(0.56f, 0.f, zn - 0.02f); sp[n++] = Q3(0.64f, 0.f, zn - 0.04f);
    Poly(sp, n, W1, c1, 0.95f);
    for (int i = 0; i < n; i++) Orb(sp[i], 0.022f, c2, 0.5f);
    // ribs
    for (int i = 0; i < 7; i++) {
        float x = -0.20f + 0.093f * i, z0 = ZSP(x);
        for (int sg = -1; sg <= 1; sg += 2) {
            float s = (float)sg;
            V3 pp[5] = { Q3(x, 0, z0), Q3(x, s * 0.075f, z0 - 0.03f), Q3(x, s * 0.115f, z0 - 0.11f), Q3(x, s * 0.095f, z0 - 0.20f), Q3(x, s * 0.035f, z0 - 0.27f) };
            Poly(pp, 5, W2, c1, 0.8f);
        }
    }
    { // sternum / belly line
        V3 bl[9]; int m = 0;
        for (int i = 0; i < 7; i++) { float x = 0.40f - 0.10f * i; bl[m++] = Q3(x, 0, ZSP(x) - 0.27f); }
        bl[m++] = Q3(-0.32f, 0, ZSP(-0.32f) - 0.19f); bl[m++] = Q3(-0.40f, 0, ZSP(-0.40f) - 0.10f);
        Poly(bl, m, W2, c2, 0.7f);
    }
    // pelvis + shoulder girdle
    for (int sg = -1; sg <= 1; sg += 2) {
        float s = (float)sg;
        Neon(Q3(-0.40f, 0, ZSP(-0.40f)), Q3(-0.28f, s * 0.09f, ZSP(-0.28f) - 0.01f), W2, c1, 0.85f);
        Neon(Q3(-0.28f, s * 0.09f, ZSP(-0.28f) - 0.01f), Q3(-0.34f, s * 0.09f, ZSP(-0.34f) - 0.10f), W2, c1, 0.85f);
        Neon(Q3(-0.34f, s * 0.09f, ZSP(-0.34f) - 0.10f), Q3(-0.46f, s * 0.07f, ZSP(-0.46f) - 0.06f), W2, c1, 0.85f);
        Neon(Q3(0.30f, s * 0.07f, ZSP(0.30f) + 0.01f), Q3(0.40f, s * 0.09f, ZSP(0.40f) - 0.12f), W1, c1, 0.85f);
    }
    // legs (rotary gallop, 2-bone IK)
    for (int fl = 0; fl < 2; fl++) for (int sg = -1; sg <= 1; sg += 2) {
        bool front = fl == 1; float s = (float)sg;
        float off = front ? (sg > 0 ? 0.45f : 0.55f) : (sg > 0 ? 0.10f : 0.00f);
        float p = g + off; p -= floorf(p);
        float hx = front ? 0.40f : -0.34f, hz = ZSP(hx) - 0.11f;
        float a = (front ? 0.25f : 0.29f) * q.lg, bb = (front ? 0.27f : 0.29f) * q.lg;
        float S = (front ? 0.30f : 0.34f) * (0.8f + 0.2f * q.lg), Lift = 0.18f * q.lg, paw = 0.15f * q.lg;
        float fx, fz, lift01 = 0.f;
        if (p < 0.32f) { float u = p / 0.32f; fx = S * (1.f - 2.f * u); fz = 0.f; }
        else { float u = (p - 0.32f) / 0.68f, e = sm(u); fx = -S + 2.f * S * e; fz = Lift * sinf(PI_ * u); lift01 = sinf(PI_ * u); }
        float ex, ez, wx, wz;
        IK2(hx, hz, hx + fx, paw + fz, a, bb, front ? -1.f : 1.f, ex, ez, wx, wz);
        float ty = s * 0.085f;
        V3 H = Q3(hx, ty, hz), E = Q3(ex, ty, ez), Wr = Q3(wx, ty, wz);
        V3 Toe = Q3(wx + 0.07f - 0.06f * lift01, ty, wz - paw * (1.f - 0.3f * lift01));
        Neon(H, E, W1, c1, 0.95f); Neon(E, Wr, W1, c1, 0.95f); Neon(Wr, Toe, W2, c2, 0.95f);
        Orb(H, 0.03f, c2, 0.65f); Orb(E, 0.024f, c2, 0.6f); Orb(Wr, 0.022f, c2, 0.6f);
        for (int j = -1; j <= 1; j++) Neon(Toe, Q3(wx + 0.12f - 0.05f * lift01, ty + j * 0.02f, wz - paw * (1.f - 0.3f * lift01) - 0.005f), 0.007f, c2, 0.85f);
    }
    // head + skull
    {
        float phi = -0.10f + 0.08f * sinf(tw + 1.2f), cph = cosf(phi), sph = sinf(phi);
        float hbx = 0.64f, hbz = zn - 0.04f, hs = q.headS;
#define HQ(x, y, z) Q3(hbx + (cph * (x) - sph * (z)) * hs, (y) * hs, hbz + (sph * (x) + cph * (z)) * hs)
        float o = 0.04f + 0.05f * (0.5f + 0.5f * sinf(2.f * tw + 1.f));
        V3 top[4] = { HQ(0.f, 0.f, 0.06f), HQ(0.09f, 0.f, 0.075f), HQ(0.18f, 0.f, 0.045f), HQ(0.25f, 0.f, 0.f) };
        Poly(top, 4, W1, c1, 0.95f);
        for (int sg = -1; sg <= 1; sg += 2) {
            float s = (float)sg;
            V3 brow[4] = { HQ(0.f, s * 0.05f, 0.05f), HQ(0.10f, s * 0.048f, 0.07f), HQ(0.18f, s * 0.03f, 0.04f), HQ(0.25f, s * 0.012f, 0.f) };
            Poly(brow, 4, W2, c1, 0.9f);
            V3 uj[3] = { HQ(0.25f, s * 0.012f, -0.01f), HQ(0.14f, s * 0.04f, -0.025f), HQ(0.f, s * 0.052f, -0.015f) };
            Poly(uj, 3, W2, c1, 0.9f);
            static const float LX[3] = { 0.f, 0.12f, 0.23f }, LY[3] = { 0.05f, 0.04f, 0.02f }, LZ[3] = { -0.03f, -0.055f, -0.04f };
            V3 lj[3];
            for (int k = 0; k < 3; k++) lj[k] = HQ(LX[k], s * LY[k], LZ[k] - LX[k] * o);
            Poly(lj, 3, W2, c1, 0.9f);
            Neon(uj[2], lj[0], W2, c1, 0.9f);
            V3 ring[8];
            for (int k = 0; k < 8; k++) { float a = k * 2.f * PI_ / 7.f; ring[k] = HQ(0.12f + 0.018f * cosf(a), s * 0.047f, 0.035f + 0.018f * sinf(a)); }
            Poly(ring, 8, 0.007f, c2, 0.85f);
            Orb(HQ(0.12f, s * 0.047f, 0.035f), 0.012f, col(255, 250, 150), 0.95f);
            V3 ear[3] = { HQ(0.f, s * 0.045f, 0.055f), HQ(-0.015f, s * 0.07f, 0.115f), HQ(0.045f, s * 0.04f, 0.10f) };
            Poly(ear, 3, W2, c1, 0.85f);
            Neon(HQ(0.215f, s * 0.02f, -0.015f), HQ(0.218f, s * 0.02f, -0.045f), 0.006f, col(255, 255, 255), 0.9f);
        }
#undef HQ
    }
    // tail
    {
        V3 tp[10]; float x = -0.50f, y = 0.f, z = ZSP(-0.50f) - 0.02f;
        tp[0] = Q3(x, y, z);
        for (int k = 0; k < 9; k++) {
            float phk = 0.25f + 0.45f * sinf(tw - 0.8f * k) * 0.6f + (k >= 6 ? 0.10f * (k - 5) : 0.f);
            float lat = 0.12f * sinf(tw * 0.5f - 0.6f * k);
            V3 d = Nz(v3(-cosf(phk), lat, sinf(phk)));
            float ln = 0.095f * q.tail;
            x += d.x * ln; y += d.y * ln; z += d.z * ln;
            tp[k + 1] = Q3(x, y, z);
        }
        PolyF(tp, 10, W1, c1, 0.95f, 0.6f);
        for (int k = 0; k < 10; k += 2) Orb(tp[k], 0.018f, c2, 0.5f);
    }
#undef ZSP
#undef Q3
}

// ================================================================ light streaks, aura, glitch
static void Trails(V3 car, V3 cr, V3 cf, V3 cu, float speed, float T) {
    const int N = 14, M = 20;
    Col cols[4] = { col(0, 255, 255), col(255, 40, 200), col(255, 255, 255), col(255, 200, 60) };
    float vel = 0.9f + speed * 0.03f;
    for (int i = 0; i < N; i++) {
        float th0 = i * (2.f * PI_ / N);
        float ph = T * vel * (0.35f + 0.1f * (i % 4)) + i * 0.173f; ph -= floorf(ph);
        float uHead = 5.0f - ph * 14.0f, life = sm(ph * 5.f) * sm((1.f - ph) * 5.f);
        V3 pts[M];
        for (int k = 0; k < M; k++) {
            float u = uHead + k * 0.42f;
            float th = th0 + 0.6f * sinf(0.45f * u + T * 0.9f + i) + ((i & 1) ? 1.f : -1.f) * 0.22f * u;
            float rad = 1.15f + 0.55f * fabsf(sinf(0.21f * u + i * 0.7f)) + 0.25f * (i % 3);
            pts[k] = car + cf * u + (cr * cosf(th) + cu * sinf(th)) * rad + cu * 0.45f;
        }
        for (int k = 0; k < M - 1; k++)
            Neon(pts[k], pts[k + 1], 0.03f, cols[i & 3], (1.f - (float)k / M) * life, (1.f - (float)(k + 1) / M) * life);
    }
}
static void Aura(V3 car, float T) {
    for (int i = 0; i < 28; i++) {
        float a = T * 0.9f + i * (2.f * PI_ / 28.f);
        Orb(car + v3(cosf(a) * 3.0f, sinf(a) * 3.0f, -0.35f + 0.1f * sinf(T * 3.f + i)), 0.09f, col(0, 255, 240), 0.7f);
        float b = -T * 0.7f + i * (2.f * PI_ / 28.f);
        Orb(car + v3(cosf(b) * 3.7f, sinf(b) * 3.7f, -0.3f + 0.1f * sinf(T * 2.f + i)), 0.07f, col(255, 50, 210), 0.6f);
    }
}
static void GlitchOverlay(V3 car, V3 cr, V3 cf, V3 cu) {
    V3 c[8]; int k = 0;
    for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) for (int sz = 0; sz < 2; sz++)
        c[k++] = car + cr * (sx * 1.0f) + cf * (sy * 2.4f) + cu * (sz ? 1.0f : -0.6f);
    float px[8], py[8], pz, x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < 8; i++) {
        if (!Proj(c[i], px[i], py[i], pz)) return;
        x0 = fminf(x0, px[i]); y0 = fminf(y0, py[i]); x1 = fmaxf(x1, px[i]); y1 = fmaxf(y1, py[i]);
    }
    float w = x1 - x0, h = y1 - y0;
    if (w < 8.f || h < 8.f) return;
    static const int E[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
    float dx = 4.f + frand() * 14.f;
    for (int e = 0; e < 12; e++) {
        SegS(px[E[e][0]] + dx, py[E[e][0]], px[E[e][1]] + dx, py[E[e][1]], 2.5f, PK(255, 40, 70, 0.8f), PK(255, 40, 70, 0.8f));
        SegS(px[E[e][0]] - dx, py[E[e][0]], px[E[e][1]] - dx, py[E[e][1]], 2.5f, PK(40, 230, 255, 0.8f), PK(40, 230, 255, 0.8f));
    }
    for (int i = 0; i < 9; i++) {
        float yy = y0 + frand() * h, hh = 2.f + frand() * h * 0.10f;
        float xs = x0 - w * 0.15f + (frand() - 0.5f) * w * 0.5f, ww = w * (0.3f + frand() * 0.9f);
        DWORD cc = (i % 3 == 0) ? PK(255, 50, 200, 0.55f) : ((i % 3 == 1) ? PK(40, 255, 255, 0.5f) : PK(255, 255, 255, 0.35f));
        SegS(xs, yy, xs + ww, yy, hh, cc, cc);
    }
    for (int i = 0; i < 6; i++) {
        float bx = x0 + frand() * w, by = y0 + frand() * h, s = 4.f + frand() * 16.f;
        DWORD cc = PK(255, 255, 255, 0.4f);
        SegS(bx, by, bx + s, by, s, cc, cc);
    }
}

// ================================================================ game access (GTA SA 1.0 US)
typedef void* (__cdecl* FnPlayerVeh)(int, bool);
typedef void* (__cdecl* FnPlayerPed)(int);
typedef float(__cdecl* FnGround)(float, float, float, bool*, void**);
static FnPlayerVeh FindPlayerVehicle = (FnPlayerVeh)0x56E0D0;
static FnPlayerPed FindPlayerPed = (FnPlayerPed)0x56E210;
static FnGround FindGroundZ = (FnGround)0x5696C0;

static bool GetXform(void* e, V3& pos, V3& right, V3& fwd, V3& up) {
    float* m = *(float**)((char*)e + 0x14);
    if (m) { right = v3(m[0], m[1], m[2]); fwd = v3(m[4], m[5], m[6]); up = v3(m[8], m[9], m[10]); pos = v3(m[12], m[13], m[14]); return true; }
    float* sp = (float*)((char*)e + 4); float h = *(float*)((char*)e + 0x10);
    pos = v3(sp[0], sp[1], sp[2]); fwd = v3(-sinf(h), cosf(h), 0); right = v3(cosf(h), sinf(h), 0); up = v3(0, 0, 1);
    return true;
}

// ================================================================ scene
static bool& g_enabled = ::g_on;      // shared with the menu
static int& g_mode = ::g_cmode;       // 0 auto, 1 dragon, 2 eagles, 3 simurgh, 4 animals, 5 all
static float g_T = 0.f, g_gait[4] = { 0, 0.25f, 0.5f, 0.75f }, g_gz[4] = { 0, 0, 0, 0 };
static float g_nextG = 6.f, g_endG = 0.f;
static LARGE_INTEGER g_freq, g_last;


static void Animals(V3 car, V3 cfFlat, V3 crFlat, float speed, float T, float dt, float fade) {
    static const QP list[4] = {
        { 1.7f, 1.00f, 1.00f, 1.00f, { 255.f, 150.f, 30.f }, { 255.f, 235.f, 160.f } },   // leopard
        { 1.7f, 1.00f, 1.00f, 1.00f, { 255.f, 90.f, 40.f }, { 255.f, 220.f, 150.f } },    // leopard 2
        { 1.7f, 1.20f, 1.15f, 0.85f, { 255.f, 215.f, 60.f }, { 255.f, 255.f, 200.f } },   // cheetah
        { 1.7f, 1.15f, 0.90f, 1.15f, { 110.f, 200.f, 255.f }, { 220.f, 245.f, 255.f } }   // wolf
    };
    for (int i = 0; i < 4; i++) {
        float b = sm((speed - 1.5f) / 4.f);
        float a = 0.8f * T + i * (PI_ * 0.5f), R = 9.f + 1.2f * i;
        V3 po = car + v3(R * cosf(a), R * sinf(a), 0), tang = v3(-sinf(a), cosf(a), 0);
        float so = ((i & 1) ? 1.f : -1.f) * (5.5f + 1.2f * (i / 2));
        float fo = -1.f + 4.5f * sinf(0.35f * T + i * 1.7f) - i * 1.5f;
        V3 pe = car + crFlat * so + cfFlat * fo;
        V3 pos = lerp3(po, pe, b), dir = Nz(lerp3(tang, cfFlat, b));
        bool found = false;
        float gz = FindGroundZ(pos.x, pos.y, car.z + 3.f, &found, NULL);
        if (!found) gz = car.z - 0.6f;
        g_gz[i] = (g_gz[i] == 0.f) ? gz : lerpf(g_gz[i], gz, 0.25f);
        pos.z = g_gz[i];
        float v = lerpf(8.f, fmaxf(speed, 8.f), b);
        float cyc = clampf(1.9f + v * 0.05f, 2.0f, 3.3f);
        g_gait[i] += cyc * dt; g_gait[i] -= floorf(g_gait[i]);
        (void)fade;
        Quad(pos, dir, g_gait[i], list[i]);
    }
}

static void SetStates(IDirect3DDevice9* d) {
    d->SetPixelShader(NULL); d->SetVertexShader(NULL); d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    d->SetTexture(0, NULL);
    d->SetRenderState(D3DRS_ZENABLE, FALSE); d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE); d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD); d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE); d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_LIGHTING, FALSE); d->SetRenderState(D3DRS_FOGENABLE, FALSE); d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE); d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF); d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE); d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

static void Render(IDirect3DDevice9* d) {
    if (!g_enabled) return;
    if (!g_force) {
        if (*(volatile int*)0xC8D4C0 != 9) return;          // gGameState: playing
        if (*(volatile uint8_t*)0xBA67A4) return;            // menu active
    }
    bool paused = (*(volatile uint8_t*)0xB7CB49) || (*(volatile uint8_t*)0xB7CB48);

    // timing
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    float dt = (float)((double)(now.QuadPart - g_last.QuadPart) / (double)g_freq.QuadPart);
    g_last = now; dt = clampf(dt, 0.f, 0.1f);
    if (paused) dt = 0.f;
    g_T += dt;
    float T = g_T;

    // player entity
    void* ent = FindPlayerVehicle(-1, false);
    if (!ent) ent = FindPlayerPed(-1);
    if (!ent) return;
    V3 car, cr, cf, cu;
    GetXform(ent, car, cr, cf, cu);
    float* ms = (float*)((char*)ent + 0x44);
    float speed = sqrtf(ms[0] * ms[0] + ms[1] * ms[1] + ms[2] * ms[2]) * 50.f;
    if (!(speed >= 0.f && speed < 400.f)) speed = 0.f;

    // camera
    float* cm = *(float**)((char*)0xB6F028 + 0x14);
    if (!cm) return;
    g_cr = v3(cm[0], cm[1], cm[2]); g_cf = v3(cm[4], cm[5], cm[6]); g_cu = v3(cm[8], cm[9], cm[10]); g_cp = v3(cm[12], cm[13], cm[14]);
    float fov = *(float*)0x8D5038; if (fov < 20.f || fov > 130.f) fov = 70.f;
    D3DVIEWPORT9 vp; d->GetViewport(&vp);
    g_sw = (float)vp.Width; g_sh = (float)vp.Height;
    g_focal = (g_sw * 0.5f) / tanf(fov * PI_ / 360.f) * g_fovCal;

    g_dev = d; g_n = 0;
    SetStates(d);

    // glitch scheduling
    if (T > g_nextG) { g_endG = T + 0.12f + frand() * 0.35f; g_nextG = T + 4.f + frand() * 6.f; }
    bool glitching = g_glitch && T < g_endG;
    g_jx = g_jy = 0.f; g_fade = 1.f;

    // which acts are visible
    bool sD = false, sE = false, sS = false, sA = false; float actFade = 1.f;
    if (g_mode == 0) {
        int act = ((int)(T / 30.f)) % 4; float ta = fmodf(T, 30.f);
        actFade = fminf(1.f, fminf(ta / 2.f, (30.f - ta) / 2.f));
        sD = act == 0; sE = act == 1; sS = act == 2; sA = act == 3;
    }
    else if (g_mode == 5) { sD = sE = sS = sA = true; }
    else { sD = g_mode == 1; sE = g_mode == 2; sS = g_mode == 3; sA = g_mode == 4; }

    V3 cfFlat = Nz(v3(cf.x, cf.y, 0.f)), crFlat = Nz(v3(cr.x, cr.y, 0.f));
    if (glitching) { g_jx = (frand() - 0.5f) * 30.f; g_jy = (frand() - 0.5f) * 8.f; }
    g_fade = actFade * (glitching ? (frand() > 0.4f ? 1.f : 0.3f) : 1.f);

    if (sD) Dragon(car, T);
    if (sE) {
        BirdP eagle = { 2.0f, 1.0f, 1.0f, 0.30f, 12, 3, 0.01f, false, { 255.f, 190.f, 60.f }, { 255.f, 245.f, 200.f }, { 255.f, 255.f, 255.f }, 3.4f };
        for (int i = 0; i < 3; i++) Bird(car, T, eagle, i * 2.09f, 9.f + i * 1.2f, 5.f + i * 0.8f);
    }
    if (sS) {
        BirdP sim = { 3.0f, 1.25f, 1.6f, 2.4f, 19, 11, 0.22f, true, { 40.f, 255.f, 220.f }, { 255.f, 205.f, 80.f }, { 255.f, 60.f, 210.f }, 2.4f };
        Bird(car, T, sim, 0.7f, 10.f, 6.f);
    }
    if (sA) Animals(car, cfFlat, crFlat, speed, T, dt, actFade);

    g_fade = glitching ? (frand() > 0.3f ? 1.f : 0.4f) : 1.f;
    if (g_trails) Trails(car, cr, cf, cu, speed, T);
    g_fade = 1.f;
    Aura(car, T);
    if (glitching) { g_jx = g_jy = 0.f; GlitchOverlay(car, cr, cf, cu); }
    Flush();
}

// ================================================================ hook
typedef HRESULT(WINAPI* EndScene_t)(IDirect3DDevice9*);
typedef HRESULT(WINAPI* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static EndScene_t oEndScene = NULL;
static Reset_t oReset = NULL;
static IDirect3DStateBlock9* g_sb = NULL;
static int g_faults = 0;

static void SafeRender(IDirect3DDevice9* d) {
    __try { Render(d); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_faults++; g_n = 0; if (g_faults > 20) g_enabled = false; }
}
static HRESULT WINAPI hkEndScene(IDirect3DDevice9* d) {
    if (g_enabled) {
        IDirect3DSurface9 *rt = NULL, *bb = NULL; bool ok = false;
        if (SUCCEEDED(d->GetRenderTarget(0, &rt)) && SUCCEEDED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) ok = (rt == bb);
        if (rt) rt->Release();
        if (bb) bb->Release();
        if (ok) {
            if (!g_sb) d->CreateStateBlock(D3DSBT_ALL, &g_sb);
            if (g_sb) { g_sb->Capture(); SafeRender(d); g_sb->Apply(); }
        }
    }
    return oEndScene(d);
}
static HRESULT WINAPI hkReset(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS* p) {
    if (g_sb) { g_sb->Release(); g_sb = NULL; }
    return oReset(d, p);
}
static bool PatchVT(void** slot, void* fn) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
    *slot = fn;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}
static DWORD WINAPI InitThread(LPVOID) {
    QueryPerformanceFrequency(&g_freq); QueryPerformanceCounter(&g_last);
    HMODULE h = LoadLibraryA("d3d9.dll");
    if (!h) return 0;
    typedef IDirect3D9* (WINAPI * DC)(UINT);
    DC dc = (DC)GetProcAddress(h, "Direct3DCreate9");
    if (!dc) return 0;
    IDirect3D9* d3d = dc(D3D_SDK_VERSION);
    if (!d3d) return 0;
    WNDCLASSA wc; ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "NeonAuraDummy";
    RegisterClassA(&wc);
    HWND hw = CreateWindowA("NeonAuraDummy", "", WS_OVERLAPPED, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    D3DPRESENT_PARAMETERS pp; ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = hw; pp.BackBufferFormat = D3DFMT_UNKNOWN;
    IDirect3DDevice9* dev = NULL;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hw, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT, &pp, &dev);
    if (SUCCEEDED(hr) && dev) {
        void** vt = *(void***)dev;
        oEndScene = (EndScene_t)vt[42]; oReset = (Reset_t)vt[16];
        PatchVT(&vt[16], (void*)hkReset);
        PatchVT(&vt[42], (void*)hkEndScene);
        dev->Release();
    }
    d3d->Release();
    if (hw) DestroyWindow(hw);
    return 0;
}

} // namespace aura

using namespace plugin;

// ---------------- input ----------------
static bool Pressed(int vk) {
    static bool prev[256];
    bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool hit = now && !prev[vk & 255];
    prev[vk & 255] = now;
    return hit;
}

// typing the keys 3 and D one after another (like a cheat code) toggles the menu
static bool CheatTyped() {
    static bool prev[256];
    static char buf[2] = {0, 0};
    bool hit = false;
    for (int vk = '0'; vk <= 'Z'; vk++) {
        if (vk > '9' && vk < 'A') continue;
        bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (now && !prev[vk]) {
            buf[0] = buf[1]; buf[1] = (char)vk;
            if (buf[0] == '3' && buf[1] == 'D') {
                hit = true;
                buf[0] = buf[1] = 0;
            }
        }
        prev[vk] = now;
    }
    return hit;
}

// ---------------- menu state ----------------
static bool g_menu = false;
static float g_cx = 0, g_cy = 0;           // virtual cursor (screen px)
static bool g_lmbPrev = true, g_click = false;
static bool g_invert = false;              // false = normal mouse, true = inverted (both axes)
static char g_status[96] = "";
static unsigned g_statusT = 0;
static const float MOUSE_SENS = 1.0f;      // tune if cursor feels too slow/fast

// reveal / fade state (used by FillRect / TextF / Hover; the intro animation of the next step drives these)
static bool   g_uiBlock = false;           // menu is animating: no hover / clicks / cursor
static bool   g_revealOn = false;          // top-down reveal below the dragon's neck
static float  g_clipY = 0.0f, g_neckY = 0.0f, g_topA = 1.0f, g_menuA = 1.0f;

static float dgSm(float a, float b, float x) {
    float t = (x - a) / (b - a); t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}
// opacity of a menu element whose top edge is at screen y
static float ElemAlpha(float y) {
    if (!g_revealOn) return g_menuA;
    if (y < g_neckY) return g_topA * g_menuA;
    float d = g_clipY - y;
    if (d <= 0.0f) return 0.0f;
    return dgSm(0.0f, 26.0f * (float)RsGlobal.maximumHeight / 720.0f, d) * g_menuA;
}

static float SW() { return (float)RsGlobal.maximumWidth; }
static float SH() { return (float)RsGlobal.maximumHeight; }
static void Status(const char* fmt, ...) {
    va_list a; va_start(a, fmt);
    vsnprintf(g_status, sizeof(g_status), fmt, a);
    va_end(a);
    g_statusT = CTimer::m_snTimeInMilliseconds;
}

static void ToggleMenu() {
    if (g_menu) {
        g_menu = false;
    } else {
        g_menu = true;
        g_cx = SW() * 0.5f; g_cy = SH() * 0.5f;
        g_lmbPrev = true;
    }
}

// plugin-sdk's CMouseControllerState has no members named X/Y; access the two mouse-delta floats by
// their fixed GTA SA layout: 7 bools + pad (8 bytes), float wheel (+8), float X (+12), float Y (+16)
static float& MX(CMouseControllerState& m) { return *(float*)((char*)&m + 12); }
static float& MY(CMouseControllerState& m) { return *(float*)((char*)&m + 16); }

// ---------------- mouse (own virtual cursor, camera frozen) ----------------
static void UpdateMouse() {
    auto& m = CPad::NewMouseControllerState;
    float sgn = g_invert ? -1.0f : 1.0f;
    g_cx += MX(m) * MOUSE_SENS * sgn;
    g_cy += MY(m) * MOUSE_SENS * sgn;
    if (g_cx < 0) g_cx = 0; if (g_cx > SW() - 1) g_cx = SW() - 1;
    if (g_cy < 0) g_cy = 0; if (g_cy > SH() - 1) g_cy = SH() - 1;
    bool lmb = m.lmb;
    g_click = lmb && !g_lmbPrev;
    g_lmbPrev = lmb;
    MX(m) = MY(m) = 0; m.lmb = m.rmb = m.mmb = false;  // camera/shoot get nothing
    auto& t = CPad::PCTempMouseControllerState;
    MX(t) = MY(t) = 0; t.lmb = t.rmb = t.mmb = false;
    CPad::GetPad(0)->DisablePlayerControls = true;
}

// ---------------- drawing helpers ----------------
#define C_GOLD    CRGBA(160, 70, 255, 255)
#define C_GOLD_LT CRGBA(214, 160, 255, 255)
#define C_GOLD_DK CRGBA(84, 36, 150, 255)
#define C_TXT     CRGBA(226, 210, 248, 255)

static float NowSec() { return CTimer::m_snTimeInMilliseconds / 1000.0f; }
static CRGBA Mix(CRGBA a, CRGBA b, float t) {
    return CRGBA((unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t));
}
static CRGBA HueCol(float h, unsigned char a = 255, float s = 1.0f) {
    float r, g, b; Hsv(h, s, 1.0f, r, g, b);
    return CRGBA((unsigned char)(r * 255), (unsigned char)(g * 255), (unsigned char)(b * 255), a);
}
static void RawRect(float x, float y, float w, float h, CRGBA c) {
    CSprite2d::DrawRect(CRect(x, y + h, x + w, y), c);
}
// menu rectangles (the reveal clipping is used by the intro animation of the next step)
static void FillRect(float x, float y, float w, float h, CRGBA c) {
    if (g_revealOn) {
        if (y < g_neckY && y + h > g_neckY) {            // spans the neck line: draw the two halves separately
            float h1 = g_neckY - y;
            FillRect(x, y, w, h1, c);
            FillRect(x, g_neckY, w, h - h1, c);
            return;
        }
        if (y >= g_clipY) return;
        if (y + h > g_clipY) h = g_clipY - y;
    }
    float ea = ElemAlpha(y);
    if (ea <= 0.004f) return;
    if (ea < 0.999f) c.a = (unsigned char)(c.a * ea);
    RawRect(x, y, w, h, c);
}
static void VGrad(float x, float y, float w, float h, CRGBA a, CRGBA b, int bands = 18) {
    float bh = h / bands;
    for (int i = 0; i < bands; i++)
        FillRect(x, y + i * bh, w, bh + 0.6f, Mix(a, b, (float)i / (float)(bands - 1)));
}
static void TextF(float x, float y, const char* s, CRGBA col, float scale, bool pricedown, int edge) {
    float ea = ElemAlpha(y);
    if (ea <= 0.01f) return;
    if (ea < 0.999f) col.a = (unsigned char)(col.a * ea);
    char buf[200];
    strncpy(buf, s, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    if (pricedown) CFont::SetFontStyle(FONT_PRICEDOWN); else CFont::SetFontStyle(FONT_SUBTITLES);
    CFont::SetScale(0.36f * scale * SW() / 640.0f, 0.78f * scale * SH() / 448.0f);
    CFont::SetColor(col);
    CFont::SetOrientation(ALIGN_LEFT);
    CFont::SetEdge(edge);
    CFont::SetWrapx(SW());
    CFont::PrintString(x, y, buf);
}
static void Text(float x, float y, const char* s, CRGBA col, float scale = 1.0f) {
    TextF(x, y, s, col, scale, false, 1);
}
// per-letter RGB text: longer prefixes are drawn first, shorter prefixes are drawn over them in
// their own colour, so every letter ends up with a different hue (no glyph width needed)
static void TextRainbow(float x, float y, const char* s, float scale) {
    int n = (int)strlen(s); if (n > 60) n = 60;
    const int G = 2;
    int m = (n + G - 1) / G;
    float t = NowSec();
    char buf[64];
    for (int g = m; g >= 1; g--) {
        int len = g * G; if (len > n) len = n;
        memcpy(buf, s, len); buf[len] = 0;
        TextF(x, y, buf, HueCol((float)(g - 1) / (float)m * 0.85f - t * 0.35f, 255, 0.85f), scale, false, 0);
    }
}
// rotating RGB border: a rainbow that travels around the perimeter
static void RgbBorder(float x, float y, float w, float h, float th) {
    float t = NowSec();
    float L = 2 * (w + h);
    int n = (int)(L / 6.0f); if (n < 16) n = 16;
    float seg = L / n;
    for (int i = 0; i < n; i++) {
        float p = i * seg;
        CRGBA c = HueCol(p / L - t * 0.5f);
        float sx, sy, sw, sh;
        if (p < w)                  { sx = x + p;                 sy = y;                       sw = seg + 0.6f; sh = th; }
        else if (p < w + h)         { sx = x + w - th;            sy = y + (p - w);             sw = th;         sh = seg + 0.6f; }
        else if (p < 2 * w + h)     { sx = x + w - (p - w - h) - seg; sy = y + h - th;          sw = seg + 0.6f; sh = th; }
        else                        { sx = x;                     sy = y + h - (p - 2 * w - h) - seg; sw = th;     sh = seg + 0.6f; }
        FillRect(sx, sy, sw, sh, c);
    }
}
static void RgbLine(float x, float y, float w, float th) {
    float t = NowSec();
    int n = (int)(w / 5.0f); if (n < 6) n = 6;
    float seg = w / n;
    for (int i = 0; i < n; i++)
        FillRect(x + i * seg, y, seg + 0.6f, th, HueCol((float)i / (float)n * 0.9f - t * 0.5f));
}
static void GoldBorder(float x, float y, float w, float h, float th, CRGBA c) {
    FillRect(x, y, w, th, c);
    FillRect(x, y + h - th, w, th, c);
    FillRect(x, y, th, h, c);
    FillRect(x + w - th, y, th, h, c);
}
static bool Hover(float x, float y, float w, float h) {
    if (g_uiBlock) return false;
    return g_cx >= x && g_cx <= x + w && g_cy >= y && g_cy <= y + h;
}
// purple energy inside the main panel: a slow sweeping beam, glow along the bottom edge and rising sparks.
static void PurpleFX(float x, float y, float w, float h) {
    float t = NowSec(), u = SW() / 1280.0f;
    for (int k = 0; k < 6; k++)                                   // bottom glow
        FillRect(x + 1, y + h - (k + 1) * 5.0f * u, w - 2, 5.0f * u, CRGBA(150, 60, 255, (unsigned char)(34 - k * 5)));
    float by = y + (fmodf(t * 0.20f, 1.3f) - 0.15f) * h;          // sweeping beam
    for (int k = 0; k < 7; k++) {
        float yy = by + (k - 3) * 3.0f * u;
        if (yy < y + 2 || yy > y + h - 4) continue;
        int al = 34 - abs(k - 3) * 9;
        FillRect(x + 1, yy, w - 2, 3.0f * u, CRGBA(190, 110, 255, (unsigned char)al));
    }
    for (int i = 0; i < 28; i++) {                                // rising sparks
        float ph = Fr(Hh(i, 2) + t * (0.04f + 0.08f * Hh(i, 1)));
        float sx = x + 8.0f + Hh(i, 3) * (w - 16.0f) + sinf(t * 0.9f + i) * 6.0f * u;
        float sy = y + h - ph * h;
        float s = (1.5f + 2.5f * Hh(i, 4)) * u;
        FillRect(sx, sy, s, s, CRGBA(205, 140, 255, (unsigned char)(170.0f * sinf(ph * 3.14159f))));
    }
}
// black glass panel: soft RGB glow outside, black->dark-purple gradient, purple frame, thin RGB inner line, corners
static void Panel(float x, float y, float w, float h, bool glow) {
    float t = NowSec();
    if (glow)
        for (int k = 4; k >= 1; k--) {
            float e = k * 3.0f;
            FillRect(x - e, y - e, w + 2 * e, h + 2 * e, HueCol(t * 0.15f, (unsigned char)(10 + (5 - k) * 4)));
        }
    VGrad(x, y, w, h, CRGBA(8, 6, 14, 238), CRGBA(26, 8, 46, 238));
    if (glow) PurpleFX(x, y, w, h);
    GoldBorder(x, y, w, h, 1.2f, C_GOLD);
    RgbBorder(x + 3, y + 3, w - 6, h - 6, 1.2f);
    float len = 16.0f + 3.0f * sinf(t * 2.5f), th = 2.4f;
    CRGBA g = C_GOLD_LT;
    FillRect(x - 1, y - 1, len, th, g);                    FillRect(x - 1, y - 1, th, len, g);
    FillRect(x + w - len + 1, y - 1, len, th, g);          FillRect(x + w - th + 1, y - 1, th, len, g);
    FillRect(x - 1, y + h - th + 1, len, th, g);           FillRect(x - 1, y + h - len + 1, th, len, g);
    FillRect(x + w - len + 1, y + h - th + 1, len, th, g); FillRect(x + w - th + 1, y + h - len + 1, th, len, g);
}
static bool ButtonS(const char* label, float x, float y, float w, float h, bool selected, float ts) {
    bool hov = Hover(x, y, w, h);
    CRGBA bg = selected ? CRGBA(38, 14, 64, 242) : (hov ? CRGBA(32, 14, 54, 242) : CRGBA(12, 12, 14, 232));
    FillRect(x, y, w, h, bg);
    if (selected) {                                                       // selected button breathes
        float pz = 0.5f + 0.5f * sinf(NowSec() * 3.0f);
        FillRect(x, y, w, h * 0.5f, CRGBA(160, 70, 255, (unsigned char)(30 + 28 * pz)));
    }
    if (hov) {                                                            // shine that runs across a hovered button
        float sx2 = x + fmodf(NowSec() * 1.4f, 1.0f) * w, sw2 = w * 0.14f;
        if (sx2 + sw2 > x + w) sw2 = x + w - sx2;
        if (sw2 > 0.5f) FillRect(sx2, y, sw2, h, CRGBA(225, 175, 255, 40));
    }
    GoldBorder(x, y, w, h, 1.0f, (selected || hov) ? C_GOLD_LT : C_GOLD_DK);
    if (selected || hov) RgbLine(x + 2, y + h - 3.0f, w - 4, 1.6f);
    if (selected) FillRect(x, y, 3.0f, h, C_GOLD);
    CRGBA tc = selected ? C_GOLD_LT : (hov ? CRGBA(255, 255, 255, 255) : CRGBA(222, 222, 226, 255));
    Text(x + 8, y + h * 0.18f, label, tc, ts);
    return hov && g_click;
}
static bool Button(const char* label, float x, float y, float w, float h, bool selected) {
    return ButtonS(label, x, y, w, h, selected, 0.85f);
}

// Watch Dogs 2 style name: RGB-split ghosts, random glitch bursts, jitter and flicker
static void DrawNameText(float x, float y) {
    unsigned ms = CTimer::m_snTimeInMilliseconds;
    float t = ms / 1000.0f, u = SW() / 1280.0f;
    float ph = fmodf(t, 2.4f);
    bool burst = ph < 0.22f || (ph > 1.1f && ph < 1.18f);
    int k = (int)(ms / 45);
    float amp = (1.4f + 0.8f * sinf(t * 3.0f)) * u;
    float jx = 0, jy = 0;
    if (burst) { amp += (3.0f + 9.0f * Hh(k)) * u; jx = (Hh(k, 1) - 0.5f) * 10.0f * u; jy = (Hh(k, 2) - 0.5f) * 3.0f * u; }
    unsigned char a = (Hh(k, 3) > 0.94f) ? 190 : 255;
    TextF(x - amp + jx, y, "Parsa Rsi", CRGBA(0, 255, 255, 210), 1.7f, true, 0);
    TextF(x + amp + jx, y + jy, "Parsa Rsi", CRGBA(255, 0, 90, 210), 1.7f, true, 0);
    TextF(x + jx * 0.5f, y, "Parsa Rsi", CRGBA(255, 255, 255, a), 1.7f, true, 0);
}
static void DrawHeader(float px, float py, float pw, float hh, float pad) {
    float H = SH(), u = SW() / 1280.0f, t = NowSec();
    unsigned ms = CTimer::m_snTimeInMilliseconds;
    FillRect(px + 1, py + 1, pw - 2, hh, CRGBA(160, 70, 255, 16));
    {   // pulsing neon-purple glow behind the name
        float gp = 0.5f + 0.5f * sinf(t * 2.0f);
        for (int k = 4; k >= 1; k--)
            FillRect(px + pad - k * 4.0f, py + H * 0.043f - k * 2.0f, pw * 0.36f + k * 8.0f, H * 0.062f + k * 4.0f,
                     CRGBA(150, 60, 255, (unsigned char)(6 + 5 * gp)));
    }
    TextRainbow(px + pad, py + H * 0.014f, "neon aura menu create by :", 0.8f);
    CFont::DrawFonts();
    float ny = py + H * 0.043f;
    DrawNameText(px + pad + 2, ny);
    CFont::DrawFonts();
    // everything below is drawn after the text has been flushed, so it lays over the letters
    for (float yy = py + 2; yy < py + hh; yy += 3.0f) FillRect(px + 1, yy, pw - 2, 1.0f, CRGBA(0, 0, 0, 70));  // scanlines
    float ph = fmodf(t, 2.4f);
    bool burst = ph < 0.22f || (ph > 1.1f && ph < 1.18f);
    if (burst) {
        int k = (int)(ms / 45);
        float nh = H * 0.062f;
        for (int i = 0; i < 3; i++) {
            float by = ny + Hh(k, 10 + i) * nh;
            float bx = px + pad + Hh(k, 30 + i) * pw * 0.25f;
            float bw = (0.08f + 0.22f * Hh(k, 20 + i)) * pw;
            float bh = (1.0f + 3.0f * Hh(k, 40 + i)) * u;
            CRGBA c = (i == 0) ? CRGBA(0, 255, 255, 150) : (i == 1 ? CRGBA(255, 0, 90, 150) : CRGBA(0, 0, 0, 255));
            FillRect(bx, by, bw, bh, c);
        }
        for (int i = 0; i < 6; i++) {
            float bx = px + pad + Hh(k, 50 + i) * pw * 0.45f, by = ny + Hh(k, 60 + i) * nh;
            FillRect(bx, by, (2.0f + 5.0f * Hh(k, 70 + i)) * u, (2.0f + 3.0f * Hh(k, 80 + i)) * u, HueCol(Hh(k, 90 + i), 200));
        }
    }
    // divider: purple hairline, travelling highlight, RGB line
    float dy = py + hh;
    FillRect(px + pad, dy, pw - 2 * pad, 1.2f, C_GOLD);
    float sx = px + pad + (fmodf(t * 0.45f, 1.4f) - 0.2f) * (pw - 2 * pad);
    float sw = 0.2f * (pw - 2 * pad);
    float x0 = sx < px + pad ? px + pad : sx;
    float x1 = (sx + sw) > (px + pw - pad) ? (px + pw - pad) : (sx + sw);
    if (x1 > x0) FillRect(x0, dy - 0.4f, x1 - x0, 2.0f, C_GOLD_LT);
    RgbLine(px + pad, dy + 3.0f, pw - 2 * pad, 1.6f);
}

// colour of a creature mode in the preview
static CRGBA PrevCol(int mode) {
    switch (mode) {
        case 1: return CRGBA(255, 190, 50, 255);     // golden dragon
        case 2: return CRGBA(120, 220, 255, 255);    // eagles
        case 3: return CRGBA(255, 80, 200, 255);     // simurgh
        case 4: return CRGBA(255, 140, 40, 255);     // animals
        default: return CRGBA(255, 255, 255, 255);
    }
}
// small animated neon ribbon with twinkling stars
static void DrawPreview(float x, float y, float s, int mode) {
    float t = NowSec();
    FillRect(x, y, s, s, CRGBA(0, 0, 0, 255));
    const int N = 34;
    for (int i = 0; i < N; i++) {
        float u = (float)i / (N - 1);
        float ph = u * 8.0f - t * 3.2f;
        float cx = x + s * (0.07f + 0.86f * u);
        float cy = y + s * (0.5f + 0.2f * sinf(ph) * (0.35f + 0.65f * u));
        float r = s * (0.008f + 0.022f * u);
        CRGBA c = (mode == 0 || mode == 5) ? HueCol(u * 0.8f - t * 0.3f) : PrevCol(mode);
        FillRect(cx - r * 3.0f, cy - r * 3.0f, r * 6.0f, r * 6.0f, CRGBA(c.r, c.g, c.b, 34));
        FillRect(cx - r * 1.8f, cy - r * 1.8f, r * 3.6f, r * 3.6f, CRGBA(c.r, c.g, c.b, 90));
        FillRect(cx - r, cy - r, r * 2.0f, r * 2.0f, Mix(c, CRGBA(255, 255, 255, 255), 0.6f));
    }
    for (int i = 0; i < 12; i++) {
        float tw = sinf(t * 2.0f + Hh(i, 3) * 6.0f);
        if (tw <= 0.0f) continue;
        FillRect(x + 3.0f + Hh(i, 1) * (s - 6.0f), y + 3.0f + Hh(i, 2) * (s - 6.0f), 2.0f, 2.0f,
                 CRGBA(255, 255, 255, (unsigned char)(210.0f * tw)));
    }
    GoldBorder(x, y, s, s, 1.0f, C_GOLD);
}

// small checkbox row used by the status panel
static bool CheckRow(const char* label, float x, float y, float w, float h, bool on) {
    bool hov = Hover(x, y, w, h - 2);
    if (hov) { FillRect(x, y, w, h - 2, CRGBA(160, 70, 255, 30)); FillRect(x, y, 2.0f, h - 2, C_GOLD); }
    float s = h * 0.34f, sy = y + (h - 2 - s) * 0.5f;
    if (on) FillRect(x + 6, sy, s, s, C_GOLD_LT);
    else GoldBorder(x + 6, sy, s, s, 1.0f, C_GOLD_DK);
    Text(x + 6 + s + 8, y + 1, label, on ? C_GOLD_LT : CRGBA(150, 150, 156, 255), 0.7f);
    return hov && g_click;
}

static void ResetDefaults() {
    g_on = true; g_cmode = 0; g_trails = true; g_glitch = true; g_fovCal = 1.0f; g_force = false;
}

// right-hand panel: the current NeonAura settings at a glance. Click a row to switch it.
static void DrawSide(float px, float py, float pw, float ph) {
    float W = SW(), H = SH();
    float x0 = px + pw + 10.0f;
    float w0 = W - x0 - 10.0f;
    if (w0 < 80.0f) return;
    float pad = 10.0f, gap = 6.0f, bh = H * 0.045f, rowH = H * 0.045f;
    Panel(x0, py, w0, ph, false);
    char head[96];
    sprintf(head, "Aura  %s | %s", g_on ? "ON" : "OFF", MODE_SHORT[g_cmode]);
    Text(x0 + pad, py + 8, head, C_GOLD_LT, 0.8f);
    FillRect(x0 + pad, py + H * 0.05f, w0 - 2 * pad, 1.0f, C_GOLD_DK);
    float rx = x0 + pad, rw = w0 - 2 * pad;
    float y = py + H * 0.062f;
    if (CheckRow("Aura enabled", rx, y, rw, rowH, g_on)) g_on = !g_on;
    y += rowH;
    if (CheckRow("Light trails", rx, y, rw, rowH, g_trails)) g_trails = !g_trails;
    y += rowH;
    if (CheckRow("Screen glitch", rx, y, rw, rowH, g_glitch)) g_glitch = !g_glitch;
    y += rowH;
    if (CheckRow("Ignore game checks", rx, y, rw, rowH, g_force)) g_force = !g_force;
    y += rowH + H * 0.012f;
    FillRect(rx, y, rw, 1.0f, C_GOLD_DK);
    y += H * 0.014f;
    char line[96];
    sprintf(line, "Creature: %s", MODE_NAME[g_cmode]);
    Text(rx, y, line, C_TXT, 0.68f);
    y += H * 0.036f;
    sprintf(line, "FOV calibration: x%.2f", g_fovCal);
    Text(rx, y, line, C_TXT, 0.68f);

    float y3 = py + ph - bh - pad;
    float y2 = y3 - bh - gap;
    float bw3 = (rw - 2 * gap) / 3;
    if (Button("Next creature", rx, y2, rw, bh, false)) { g_cmode = (g_cmode + 1) % N_MODES; g_on = true; }
    if (ButtonS("All on", rx, y3, bw3, bh, false, 0.7f)) { g_on = true; g_trails = true; g_glitch = true; }
    if (ButtonS("Reset", rx + bw3 + gap, y3, bw3, bh, false, 0.7f)) { ResetDefaults(); Status("settings reset"); }
    if (ButtonS("All off", rx + 2 * (bw3 + gap), y3, bw3, bh, false, 0.7f)) { g_trails = false; g_glitch = false; }
}

static void DrawMenu() {
    static bool logged = false;
    if (!logged) { logged = true; Log("menu drawn"); }
    float W = SW(), H = SH();
    float pw = W * 0.48f, ph = H * 0.84f, px = (W - pw) * 0.5f, py = (H - ph) * 0.5f;
    float pad = 14.0f, gap = 8.0f, bh = H * 0.058f, hh = H * 0.115f;
    Panel(px, py, pw, ph, true);
    DrawHeader(px, py, pw, hh, pad);

    float y = py + hh + H * 0.02f;
    float bw3 = (pw - 2 * pad - 2 * gap) / 3;
    unsigned now = CTimer::m_snTimeInMilliseconds;
    float fy = py + ph - bh - pad;

    Text(px + pad, y, "Pick what flies / runs around your car, then close the menu", C_TXT, 0.68f);
    y += H * 0.032f;
    char sel[96];
    sprintf(sel, "Selected: %s", MODE_NAME[g_cmode]);
    Text(px + pad, y, sel, C_GOLD_LT, 0.85f);
    y += H * 0.04f;

    // creature list (left) + live preview (right)
    float rightW = (pw - 2 * pad) * 0.36f, listW = pw - 2 * pad - rightW - gap;
    float rowH = H * 0.048f;
    int hoverId = -1;
    for (int i = 0; i < N_MODES; i++) {
        float ry = y + i * (rowH + 3.0f);
        if (Hover(px + pad, ry, listW, rowH)) hoverId = i;
        if (ButtonS(MODE_NAME[i], px + pad, ry, listW, rowH, g_cmode == i, 0.78f)) { g_cmode = i; g_on = true; }
    }
    int pvId = hoverId >= 0 ? hoverId : g_cmode;
    float pxl = px + pad + listW + gap;
    float s = rightW;
    float maxS = N_MODES * (rowH + 3.0f) - H * 0.06f;
    if (s > maxS) s = maxS;
    DrawPreview(pxl, y, s, pvId);
    Text(pxl, y + s + 4, MODE_SHORT[pvId], C_GOLD_LT, 0.7f);
    Text(pxl, y + s + 4 + H * 0.03f, MODE_DESC[pvId], CRGBA(170, 170, 176, 255), 0.62f);
    y += N_MODES * (rowH + 3.0f) + gap;

    // effects: two toggles side by side
    float ew = (pw - 2 * pad - gap) / 2;
    char lab[64];
    sprintf(lab, "Light trails :  %s", g_trails ? "On" : "Off");
    if (ButtonS(lab, px + pad, y, ew, bh, g_trails, 0.8f)) g_trails = !g_trails;
    sprintf(lab, "Screen glitch :  %s", g_glitch ? "On" : "Off");
    if (ButtonS(lab, px + pad + ew + gap, y, ew, bh, g_glitch, 0.8f)) g_glitch = !g_glitch;
    y += bh + gap;

    // FOV calibration row: lines do not sit on the car / world -> tune here
    char fv[64];
    sprintf(fv, "FOV calibration: x%.2f", g_fovCal);
    float labW = (pw - 2 * pad) * 0.40f;
    Text(px + pad, y + bh * 0.2f, fv, C_TXT, 0.75f);
    float cbw = ((pw - 2 * pad) - labW - 3 * gap) / 3;
    float cx0 = px + pad + labW + gap;
    if (ButtonS("-  3%", cx0, y, cbw, bh, false, 0.8f)) { g_fovCal *= 0.97f; if (g_fovCal < 0.5f) g_fovCal = 0.5f; }
    if (ButtonS("+  3%", cx0 + cbw + gap, y, cbw, bh, false, 0.8f)) { g_fovCal *= 1.03f; if (g_fovCal > 2.0f) g_fovCal = 2.0f; }
    if (ButtonS("Reset", cx0 + 2 * (cbw + gap), y, cbw, bh, false, 0.8f)) g_fovCal = 1.0f;

    // status line + bottom row (mouse mode, aura switch, close)
    if (g_status[0] && now - g_statusT < 6000)
        Text(px + pad, fy - H * 0.04f, g_status, C_GOLD_LT, 0.8f);
    if (Button(g_invert ? "Mouse: Inverted" : "Mouse: Normal", px + pad, fy, bw3, bh, g_invert)) {
        g_invert = !g_invert;
        Status(g_invert ? "mouse inverted" : "mouse normal");
    }
    if (Button(g_on ? "Aura: On" : "Aura: Off", px + pad + bw3 + gap, fy, bw3, bh, g_on)) g_on = !g_on;
    if (Button("Close  (type 3D)", px + pw - pad - bw3, fy, bw3, bh, false)) ToggleMenu();

    DrawSide(px, py, pw, ph);

    // cursor: purple cross with black outline and its own RGB ring
    if (!g_uiBlock) {
        FillRect(g_cx - 2, g_cy - 10, 4, 20, CRGBA(0, 0, 0, 255));
        FillRect(g_cx - 10, g_cy - 2, 20, 4, CRGBA(0, 0, 0, 255));
        FillRect(g_cx - 1, g_cy - 9, 2, 18, C_GOLD_LT);
        FillRect(g_cx - 9, g_cy - 1, 18, 2, C_GOLD_LT);
        RgbBorder(g_cx - 6, g_cy - 6, 12, 12, 1.5f);
    }
    g_click = false;
}

static bool g_procRan = false;
static void Process() {
    g_procRan = true;
    static bool firstP = true;
    if (firstP) { firstP = false; Log("Process running"); }
    if (CheatTyped()) { ToggleMenu(); Log("3D typed, menu=%d", g_menu ? 1 : 0); }
    if (g_menu) UpdateMouse();
}

// ============================================================================
//  Fancy HUD line (sits right under the money counter, right-aligned)
// ============================================================================
#define HUD_FONT      FONT_PRICEDOWN
#define HUD_RIGHT_X   32.0f
#define HUD_TOP_Y     128.0f
#define HUD_SCALE_X   0.27f
#define HUD_SCALE_Y   0.58f
enum { HK_PLAIN = 0, HK_RAINBOW, HK_PULSE, HK_SHIMMER };
struct HudSeg { char txt[64]; int kind; };

static void HudPrintRight(float x, float y, const char* s, CRGBA col, float mul = 1.0f) {
    char buf[200];
    strncpy(buf, s, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    CFont::SetFontStyle(HUD_FONT);
    CFont::SetScale(HUD_SCALE_X * mul * SW() / 640.0f, HUD_SCALE_Y * mul * SH() / 448.0f);
    CFont::SetColor(col);
    CFont::SetOrientation(ALIGN_RIGHT);
    CFont::SetRightJustifyWrap(0.0f);
    CFont::SetEdge(0);
    CFont::SetWrapx(SW());
    CFont::PrintString(x, y, buf);
}

// Right-aligned multi-colour text without measuring glyphs: the suffix that starts at letter i is
// drawn in letter i's colour, from the longest suffix to the shortest.
static void HudFancyLine(const HudSeg* segs, int nSeg, float rx, float y) {
    char full[200]; CRGBA col[200]; bool emit[200];
    int n = 0;
    float t = NowSec();
    float px = SW() / 640.0f, py = SH() / 448.0f;
    int pulseStart = -1;
    for (int s = 0; s < nSeg && n < 190; s++) {
        int len = (int)strlen(segs[s].txt);
        for (int k = 0; k < len && n < 190; k++, n++) {
            full[n] = segs[s].txt[k];
            float u = len > 1 ? (float)k / (float)(len - 1) : 0.0f;
            CRGBA c = CRGBA(205, 205, 215, 255);
            bool anim = false;
            switch (segs[s].kind) {
            case HK_RAINBOW: c = HueCol(u * 0.8f - t * 0.45f, 255, 0.85f); anim = true; break;
            case HK_SHIMMER: {
                float hp = fmodf(t * 0.8f, 1.8f) - 0.4f;          // bright spot sweeping over the word
                float d = (u - hp) * 3.2f;
                float k2 = expf(-d * d);
                c = Mix(CRGBA(170, 100, 255, 255), CRGBA(255, 255, 235, 255), k2);
                anim = true; break; }
            case HK_PULSE: {
                float p = 0.5f + 0.5f * sinf(t * 4.5f);
                c = Mix(CRGBA(255, 45, 130, 255), CRGBA(120, 245, 255, 255), p);
                if (pulseStart < 0) pulseStart = n;
                break; }
            default: break;
            }
            col[n] = c;
            emit[n] = (k == 0) || (anim && (k % 2 == 0));
        }
    }
    full[n] = 0;
    if (n == 0) return;

    // dark drop shadow for readability on any background
    HudPrintRight(rx + 1.5f * px, y + 1.5f * py, full, CRGBA(0, 0, 0, 200));

    // soft neon glow behind the last keyword (3D)
    if (pulseStart >= 0) {
        CRGBA g = col[pulseStart]; g.a = 70;
        const float o = 1.7f;
        HudPrintRight(rx - o * px, y,          full + pulseStart, g);
        HudPrintRight(rx + o * px, y,          full + pulseStart, g);
        HudPrintRight(rx,          y - o * py, full + pulseStart, g);
        HudPrintRight(rx,          y + o * py, full + pulseStart, g);
    }

    // colour pass: longest suffix first, shortest last
    for (int i = 0; i < n; i++)
        if (emit[i] && full[i] != ' ') HudPrintRight(rx, y, full + i, col[i]);
}

static void DrawHud() {
    static bool firstD = true;
    if (firstD) { firstD = false; Log("DrawHud running"); }
    // gameProcessEvent does not fire on this setup, so run the logic from the HUD event when needed
    if (!g_procRan) Process();
    g_procRan = false;
    if (g_menu) {
        // camera must stay frozen even if the pad was refreshed after Process
        auto& m = CPad::NewMouseControllerState;
        MX(m) = MY(m) = 0;
        DrawMenu();
        CFont::DrawFonts();
        return;
    }
    HudSeg segs[8]; int ns = 0;
    snprintf(segs[ns].txt, sizeof(segs[ns].txt), "NeonAura"); segs[ns].kind = HK_RAINBOW; ns++;
    snprintf(segs[ns].txt, sizeof(segs[ns].txt), " - "); segs[ns].kind = HK_PLAIN; ns++;
    snprintf(segs[ns].txt, sizeof(segs[ns].txt), "%s", g_on ? MODE_SHORT[g_cmode] : "off"); segs[ns].kind = HK_SHIMMER; ns++;
    snprintf(segs[ns].txt, sizeof(segs[ns].txt), " - menu: type "); segs[ns].kind = HK_PLAIN; ns++;
    snprintf(segs[ns].txt, sizeof(segs[ns].txt), "3D"); segs[ns].kind = HK_PULSE; ns++;

    float rx = SW() - HUD_RIGHT_X * SW() / 640.0f;
    float y  = HUD_TOP_Y * SH() / 448.0f;
    HudFancyLine(segs, ns, rx, y);
    CFont::DrawFonts();
}

class NeonAuraPlugin {
public:
    NeonAuraPlugin() {
        Log("---- NeonAura menu v0.2 loaded ----");
        MessageBeep(MB_ICONINFORMATION);
        CreateThread(NULL, 0, [](LPVOID) -> DWORD {
            MessageBoxA(NULL, "NeonAura loaded OK.\nOpen the menu by typing 3 then D (top-row keys) while playing.", "NeonAura", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            return 0; }, NULL, 0, NULL);
        CreateThread(NULL, 0, aura::InitThread, NULL, 0, NULL);   // starts the D3D9 overlay hook
        Events::gameProcessEvent += Process;
        Events::drawHudEvent += DrawHud;
    }
} neonAuraPlugin;
