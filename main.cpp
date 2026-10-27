// NeonAura v3  -  GTA San Andreas 1.0 US  (.asi plugin, Direct3D9 overlay, no textures)   [v3: car hides animals, animals stay after leaving the car, neon FX]
// Effects float AROUND the player's vehicle at a distance (AURA DISTANCE), detached from the
// body so they never overlap body-texture mods. Hull is built from the car's real collision box:
// energy shell, scanner ring, flowing light, lightning arcs, ground ripples and wheel rings.  Everything is controlled from an in-game 3D-style menu.
//
// Keys:  type  3 then D  (like a cheat code, within 2 seconds) = open / close menu
//        UP / DOWN = select   LEFT / RIGHT = change   ENTER = toggle / press
//        F11 = ignore game-state checks (debug, only if nothing is drawn)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <d3d9.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

static const float PI_ = 3.14159265f;

// simple log file (NeonAura.log in the game folder) to help diagnose problems
static void Log(const char* fmt, ...) {
    FILE* f = fopen("NeonAura.log", "a"); if (!f) return;
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fputc('\n', f); fclose(f);
}

// ======================================================================== math
struct V3 { float x, y, z; };
static inline V3 v3(float x, float y, float z) { V3 r = { x, y, z }; return r; }
static inline V3 operator+(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 operator-(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 operator*(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float len(V3 a) { return sqrtf(dot(a, a)); }
static inline V3 Nz(V3 a) { float l = len(a); return l > 1e-6f ? a * (1.f / l) : v3(0, 0, 1); }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float sm(float t) { t = clampf(t, 0.f, 1.f); return t * t * (3.f - 2.f * t); }
static inline float sgnf(float v) { return v < 0.f ? -1.f : 1.f; }

struct Rng {
    uint32_t s;
    float f() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.f; }
};

struct Col { float r, g, b; };
static inline Col col(float r, float g, float b) { Col c = { r, g, b }; return c; }
static inline Col mixc(Col a, Col b, float t) { return col(lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t)); }
static Col Hsv(float h, float s, float v) {
    h -= floorf(h);
    int i = (int)(h * 6.f); float f = h * 6.f - i;
    float p = v * (1.f - s), q = v * (1.f - f * s), t = v * (1.f - (1.f - f) * s), r = v, g = t, b = p;
    switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return col(r * 255.f, g * 255.f, b * 255.f);
}

// ======================================================================== settings
enum {
    I_MASTER, I_EAGLE, I_DRAGON, I_WOLF, I_JAG, I_PHOENIX, I_FX,   // 0..6 = toggles
    I_COLOR, I_INT, I_SIZE, I_SPD, I_SPREAD, I_DENS, I_RESET, I_COUNT
};
static const char* LB[I_COUNT] = { "MASTER SWITCH", "EAGLE PAIR", "DRAGON", "WOLF PAIR", "JAGUAR PAIR", "PHOENIX", "NEON FX",
                                   "COLOR", "INTENSITY", "SIZE", "SPEED", "SPREAD", "DETAIL", "RESET DEFAULTS" };
static const char* COLN[7] = { "CYAN", "MAGENTA", "GOLD", "GREEN", "RED", "RAINBOW", "MIXED" };
static const char* DENN[3] = { "LOW", "MEDIUM", "HIGH" };

static bool  g_tg[7];
static int   g_col, g_dens;
static float g_int, g_size, g_spd, g_spread;
static bool  g_force = false;
static bool  g_ver = false;      // exe looks like GTA SA 1.0 US

static void ResetDefaults() {
    for (int i = 0; i < 7; i++) g_tg[i] = false;
    g_tg[I_MASTER] = g_tg[I_EAGLE] = g_tg[I_DRAGON] = g_tg[I_WOLF] = g_tg[I_JAG] = g_tg[I_PHOENIX] = g_tg[I_FX] = true;
    g_col = 6; g_dens = 1; g_int = 1.0f; g_size = 1.0f; g_spd = 1.0f; g_spread = 1.0f;
}
static float* SliderPtr(int i) { return i == I_INT ? &g_int : (i == I_SIZE ? &g_size : (i == I_SPD ? &g_spd : &g_spread)); }
static void SliderRange(int i, float& lo, float& hi, float& st) {
    switch (i) {
    case I_INT:  lo = 0.2f;  hi = 2.0f; st = 0.05f; break;
    case I_SIZE: lo = 0.5f;  hi = 2.0f; st = 0.05f; break;
    case I_SPD:  lo = 0.25f; hi = 3.0f; st = 0.05f; break;
    default:     lo = 0.5f;  hi = 2.0f; st = 0.05f; break;
    }
}
static void SliderText(int i, char* out) {
    float v = *SliderPtr(i);
    sprintf(out, "%d%%", (int)(v * 100.f + 0.5f));
}
static void Adjust(int idx, int dir) {
    if (idx <= I_FX) { g_tg[idx] = dir == 0 ? !g_tg[idx] : (dir > 0); return; }
    switch (idx) {
    case I_COLOR: g_col = (g_col + (dir >= 0 ? 1 : 6)) % 7; break;
    case I_DENS:  g_dens = (int)clampf((float)(g_dens + (dir >= 0 ? 1 : -1)), 0.f, 2.f); break;
    case I_RESET: if (dir == 0) ResetDefaults(); break;
    default:
        if (dir != 0) {
            float lo, hi, st; SliderRange(idx, lo, hi, st);
            float* p = SliderPtr(idx); *p = clampf(*p + dir * st, lo, hi);
            *p = floorf(*p / st + 0.5f) * st; *p = clampf(*p, lo, hi);
        }
        break;
    }
}

// ======================================================================== renderer
struct Vtx { float x, y, z, rhw; DWORD c; };
#define MAXV 59990
static Vtx g_vb[MAXV + 16];
static int g_n = 0;
static IDirect3DDevice9* g_dev = NULL;

static V3 g_cp, g_cr, g_cu, g_cf;                 // camera pos / right / up / forward
static float g_sw = 800, g_sh = 600, g_focal = 600;
static float g_fade = 1.f, g_gain = 1.f;
static float g_T = 0.f;                            // effect clock (stops when game paused)
static float g_uiT = 0.f;                          // ui clock

static DWORD PK(float r, float g, float b, float a) {
    a *= g_fade * g_gain; a = clampf(a, 0.f, 1.f);
    return D3DCOLOR_ARGB((int)(a * 255.f), (int)clampf(r, 0.f, 255.f), (int)clampf(g, 0.f, 255.f), (int)clampf(b, 0.f, 255.f));
}
static void Flush() {
    if (g_n >= 3 && g_dev) g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, g_n / 3, g_vb, sizeof(Vtx));
    g_n = 0;
}
static void SetBlend(bool additive) {
    Flush();
    if (g_dev) g_dev->SetRenderState(D3DRS_DESTBLEND, additive ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
}
static inline void PV(float x, float y, DWORD c) {
    Vtx& v = g_vb[g_n++]; v.x = x; v.y = y; v.z = 0.f; v.rhw = 1.f; v.c = c;
}
static inline void Tri(float x0, float y0, DWORD c0, float x1, float y1, DWORD c1, float x2, float y2, DWORD c2) {
    if (g_n + 3 > MAXV) Flush();
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
    sx = g_sw * 0.5f + x / z * g_focal;
    sy = g_sh * 0.5f - y / z * g_focal;
    return true;
}
// ---- the car body hides everything that is behind it (software occlusion against the car's collision box)
static bool  g_occOn = false;
static V3    g_oc = { 0.f, 0.f, 0.f }, g_or = { 1.f, 0.f, 0.f }, g_of = { 0.f, 1.f, 0.f }, g_ou = { 0.f, 0.f, 1.f }, g_camL = { 0.f, 0.f, 0.f };
static float g_ob[6] = { -1.f, 1.f, -2.f, 2.f, -0.5f, 0.8f };   // x0 x1 y0 y1 z0 z1 in car space
// 1 = point is visible from the camera, 0 = the car body is in the way (soft edge ~30 cm)
static float Vis(V3 w) {
    if (!g_occOn) return 1.f;
    V3 d = w - g_oc;
    V3 p = v3(dot(d, g_or), dot(d, g_of), dot(d, g_ou));
    V3 dv = p - g_camL;
    const float oo[3] = { g_camL.x, g_camL.y, g_camL.z }, dd[3] = { dv.x, dv.y, dv.z };
    float t0 = 0.f, t1 = 1.f;
    for (int i = 0; i < 3; i++) {
        float lo = g_ob[i * 2], hi = g_ob[i * 2 + 1];
        if (fabsf(dd[i]) < 1e-6f) { if (oo[i] < lo || oo[i] > hi) return 1.f; }
        else {
            float ta = (lo - oo[i]) / dd[i], tb = (hi - oo[i]) / dd[i];
            if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
            if (ta > t0) t0 = ta;
            if (tb < t1) t1 = tb;
            if (t0 >= t1) return 1.f;
        }
    }
    float chord = (t1 - t0) * len(dv);
    return 1.f - sm(chord / 0.30f);
}
// glowing neon segment between two world points (3 layers: wide halo, mid glow, white-hot core)
static void NeonRaw(V3 a, V3 b, float ww, Col c, float a0, float a1) {
    const float NZ = 0.4f;
    float za = dot(a - g_cp, g_cf), zb = dot(b - g_cp, g_cf);
    if (za < NZ && zb < NZ) return;
    if (za < NZ) { float t = (NZ - za) / (zb - za); a = a + (b - a) * t; a0 = a0 + (a1 - a0) * t; }
    else if (zb < NZ) { float t = (NZ - zb) / (za - zb); b = b + (a - b) * t; a1 = a1 + (a0 - a1) * t; }
    float ax, ay, az, bx, by, bz;
    if (!Proj(a, ax, ay, az) || !Proj(b, bx, by, bz)) return;
    float z = (az + bz) * 0.5f;
    if (z > 160.f) return;
    float w = ww * g_focal / z;
    float wg = clampf(w * 3.2f + 2.f, 2.f, 60.f), wm = clampf(w * 1.5f + 1.f, 1.f, 30.f), wc = clampf(w * 0.6f + 0.9f, 0.9f, 14.f);
    SegS(ax, ay, bx, by, wg, PK(c.r, c.g, c.b, a0 * 0.16f), PK(c.r, c.g, c.b, a1 * 0.16f));
    SegS(ax, ay, bx, by, wm, PK(c.r, c.g, c.b, a0 * 0.42f), PK(c.r, c.g, c.b, a1 * 0.42f));
    Col h = mixc(c, col(255, 255, 255), 0.65f);
    SegS(ax, ay, bx, by, wc, PK(h.r, h.g, h.b, a0), PK(h.r, h.g, h.b, a1));
}
static void Neon(V3 a, V3 b, float ww, Col c, float a0, float a1 = -1.f) {
    if (a1 < 0.f) a1 = a0;
    if (!g_occOn) { NeonRaw(a, b, ww, c, a0, a1); return; }
    float L = len(b - a);
    int n = 1 + (int)(L / 0.5f); if (n > 8) n = 8;
    float v[9]; bool anyHid = false, allHid = true;
    for (int i = 0; i <= n; i++) {
        v[i] = Vis(a + (b - a) * ((float)i / (float)n));
        if (v[i] < 0.999f) anyHid = true;
        if (v[i] > 0.004f) allHid = false;
    }
    if (allHid) return;
    if (!anyHid) { NeonRaw(a, b, ww, c, a0, a1); return; }
    for (int i = 0; i < n; i++) {
        if (v[i] < 0.004f && v[i + 1] < 0.004f) continue;
        float t0 = (float)i / (float)n, t1 = (float)(i + 1) / (float)n;
        NeonRaw(a + (b - a) * t0, a + (b - a) * t1, ww, c, lerpf(a0, a1, t0) * v[i], lerpf(a0, a1, t1) * v[i + 1]);
    }
}
// soft glowing orb
static void Orb(V3 p, float rw, Col c, float a) {
    float sx, sy, z;
    a *= Vis(p);
    if (a < 0.004f) return;
    if (!Proj(p, sx, sy, z) || z > 160.f) return;
    float r = clampf(rw * g_focal / z, 1.6f, 200.f);
    for (int pass = 0; pass < 2; pass++) {
        float rr = pass == 0 ? r : r * 0.38f;
        Col cc = pass == 0 ? c : mixc(c, col(255, 255, 255), 0.7f);
        DWORD c0 = PK(cc.r, cc.g, cc.b, a * (pass == 0 ? 0.55f : 0.95f)), c1 = PK(cc.r, cc.g, cc.b, 0.f);
        const int K = 10;
        for (int i = 0; i < K; i++) {
            float a0 = i * 2.f * PI_ / K, a1 = (i + 1) * 2.f * PI_ / K;
            Tri(sx, sy, c0, sx + cosf(a0) * rr, sy + sinf(a0) * rr, c1, sx + cosf(a1) * rr, sy + sinf(a1) * rr, c1);
        }
    }
}

// ======================================================================== palette
static float g_cHue = 0.f;     // per-creature hue for the MIXED colour mode
static Col Pal(float u) {
    if (g_col == 5) return Hsv(u + g_T * 0.12f, 0.85f, 1.f);
    if (g_col == 6) return Hsv(g_cHue + u * 0.10f, 0.88f, 1.f);
    static const Col P1[5] = { { 0.f, 255.f, 235.f }, { 255.f, 40.f, 200.f }, { 255.f, 190.f, 40.f }, { 60.f, 255.f, 100.f }, { 255.f, 50.f, 60.f } };
    static const Col P2[5] = { { 255.f, 50.f, 220.f }, { 80.f, 160.f, 255.f }, { 255.f, 100.f, 20.f }, { 0.f, 220.f, 255.f }, { 255.f, 170.f, 40.f } };
    float m = 0.5f + 0.5f * sinf(u * 2.f * PI_ - g_T * 1.4f);
    return mixc(P1[g_col], P2[g_col], m);
}

// ======================================================================== neon skeleton toolkit
// Every animal is built from many tiny neon "bones" (spine, ribs, limb bones, skull, feathers, fingers)
// plus faint skin rings, so the 3D shape appears from the bones alone. Everything is placed in the
// vehicle's local space, so the creatures keep a fixed position relative to the car.
struct Car { V3 o, r, f, u; float x0, x1, y0, y1, z0, z1, W, L, H; };
struct Xf  { V3 o, r, f, u; float s; };
static Xf    X;
static float g_zc = 10.f, g_zr = 3.f, g_dfade = 1.f;

static inline V3 operator-(V3 a) { return v3(-a.x, -a.y, -a.z); }
static inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline V3 lerp3(V3 a, V3 b, float t) { return a + (b - a) * t; }

// ---- glitch: while g_gl > 0 the creature is torn into horizontal slices that slide sideways (wolves)
static bool  g_fxOn = true;                       // NEON FX switch (trails, sparks, glitch, shock rings)
static float g_gl = 0.f, g_glStep = 0.f;
static inline float Gh(float x) { float s_ = sinf(x * 12.9898f) * 43758.5453f; return s_ - floorf(s_); }
static inline V3 Wp(V3 l) {
    if (g_gl > 0.f) {
        float band = floorf(l.z * 8.f);
        float h = Gh(band * 1.91f + g_glStep);
        if (h > 0.45f) l.x += (h - 0.45f) * (h > 0.8f ? 1.6f : 0.8f) * g_gl * ((((int)(h * 1000.f)) & 1) ? 1.f : -1.f) * 0.5f;
        l.y += (Gh(band * 3.7f + g_glStep * 1.3f) - 0.5f) * 0.08f * g_gl;
    }
    return X.o + (X.r * l.x + X.f * l.y + X.u * l.z) * X.s;
}
static inline V3 Wd(V3 l) { return X.r * l.x + X.f * l.y + X.u * l.z; }      // direction only (no position / scale)
// depth cue: parts nearer to the camera are brighter, far parts dimmer -> reads as 3D
static inline float Cue(V3 w) { float z = dot(w - g_cp, g_cf); return clampf(0.82f + 0.45f * (g_zc - z) / g_zr, 0.3f, 1.25f); }
static void Ln(V3 a, V3 b, float ww, Col c, float al, float al1 = -1.f) {
    if (al1 < 0.f) al1 = al;
    V3 wa = Wp(a), wb = Wp(b);
    Neon(wa, wb, ww * X.s, c, al * Cue(wa) * g_dfade, al1 * Cue(wb) * g_dfade);
}
static void Jt(V3 p, float rw, Col c, float al) {
    V3 w = Wp(p);
    Orb(w, rw * X.s, c, al * Cue(w) * g_dfade);
}
// sets the local->world mapping for one creature. P = position in car space, yaw/pitch/roll in car space
static bool Begin(const Car& C, V3 P, float yaw, float pitch, float roll, float scale, float size, float hue) {
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp_ = sinf(pitch), cr = cosf(roll), sr = sinf(roll);
    V3 fc = v3(sy, cy, 0.f), rc = v3(cy, -sy, 0.f), uc = v3(0.f, 0.f, 1.f);
    V3 f2 = fc * cp + uc * sp_, u2 = uc * cp - fc * sp_;
    V3 r3 = rc * cr + u2 * sr, u3 = u2 * cr - rc * sr;
    X.o = C.o + C.r * P.x + C.f * P.y + C.u * P.z;
    X.r = C.r * r3.x + C.f * r3.y + C.u * r3.z;
    X.f = C.r * f2.x + C.f * f2.y + C.u * f2.z;
    X.u = C.r * u3.x + C.f * u3.y + C.u * u3.z;
    X.s = scale;
    g_zc = dot(X.o - g_cp, g_cf);
    g_zr = fmaxf(size * scale, 0.5f);
    float d = len(X.o - g_cp);
    g_dfade = clampf((120.f - d) / 50.f, 0.f, 1.f);
    g_cHue = hue;
    return g_dfade > 0.01f;
}

static void RingL(V3 c, V3 a1, V3 a2, float r1, float r2, int K, float ww, Col col_, float al) {
    V3 prev = c + a1 * r1;
    for (int k = 1; k <= K; k++) {
        float a = k * 2.f * PI_ / K;
        V3 p = c + a1 * (cosf(a) * r1) + a2 * (sinf(a) * r2);
        Ln(prev, p, ww, col_, al);
        prev = p;
    }
}
// skin tube around a polyline: faint rings + faint long lines (the "volume" the bones sit in)
static void Tube(const V3* P, const V3* R, const V3* U, const float* rx, const float* rz, int n, int K, float ww,
                 float u0, float u1, float alRing, float alLong) {
    static V3 rp[40][12];
    if (n > 40) n = 40;
    if (K > 12) K = 12;
    float cs[12], sn[12];
    for (int k = 0; k < K; k++) { float a = k * 2.f * PI_ / K; cs[k] = cosf(a); sn[k] = sinf(a); }
    for (int i = 0; i < n; i++)
        for (int k = 0; k < K; k++) rp[i][k] = P[i] + R[i] * (cs[k] * rx[i]) + U[i] * (sn[k] * rz[i]);
    for (int i = 0; i < n; i++) {
        float u = n > 1 ? lerpf(u0, u1, (float)i / (n - 1)) : u0;
        Col c = Pal(u);
        if (alRing > 0.f) for (int k = 0; k < K; k++) Ln(rp[i][k], rp[i][(k + 1) % K], ww, c, alRing);
        if (alLong > 0.f && i < n - 1) for (int k = 0; k < K; k++) Ln(rp[i][k], rp[i + 1][k], ww * 0.8f, c, alLong);
    }
}
// two-bone IK in the y/z plane (hip and foot share the same x)
static V3 IK2(V3 hip, V3 foot, float a, float b, float bend) {
    V3 d = foot - hip;
    float L = len(d);
    if (L < 1e-4f) return hip + v3(0.f, 0.f, -a);
    L = clampf(L, fabsf(a - b) + 0.01f, a + b - 0.01f);
    V3 dir = Nz(d);
    float cA = clampf((a * a + L * L - b * b) / (2.f * a * L), -1.f, 1.f);
    float sA = sqrtf(fmaxf(0.f, 1.f - cA * cA));
    V3 perp = v3(0.f, -dir.z, dir.y) * bend;
    return hip + dir * (a * cA) + perp * (a * sA);
}

// ======================================================================== NEON FX
// ribbon trails (wing tips, tails, eyes) + sparks + glitch bursts + paw shock rings.  All trails/sparks live in WORLD space,
// so they stay behind in the air while the car drives on - that is what draws the long neon lines behind the wings.
static Rng   g_rng = { 2463534242u };
static float g_dtE = 0.f, g_densK = 1.f;           // effect delta time / detail multiplier
static V3    g_carV = { 0.f, 0.f, 0.f };           // car velocity (m/s)
static int   g_tid = 0, g_tid2 = 0, g_eid = 0;     // trail ids / emitter ids of the creature being drawn
#define NTR 32
#define TRN 36
struct Tr { V3 p[TRN]; float t[TRN]; int n; };
static Tr g_tr[NTR];
static void TrPush(int id, V3 w) {
    if (!g_fxOn || id < 0 || id >= NTR) return;
    Tr& q = g_tr[id];
    if (q.n > 0 && (g_T - q.t[0] > 0.5f || len(w - q.p[0]) > 8.f)) q.n = 0;      // stale or teleported
    if (q.n > 0 && g_T - q.t[0] < 0.018f) { q.p[0] = w; return; }
    int m = q.n < TRN ? q.n : TRN - 1;
    for (int i = m; i > 0; i--) { q.p[i] = q.p[i - 1]; q.t[i] = q.t[i - 1]; }
    q.p[0] = w; q.t[0] = g_T; q.n = m + 1;
}
// fading neon ribbon: c0 at the head -> c1 at the end, thins out, travelling light pulse
static void TrDraw(int id, float ww, Col c0, Col c1, float al, float maxLen, float life) {
    if (!g_fxOn || id < 0 || id >= NTR) return;
    Tr& q = g_tr[id];
    if (q.n < 2 || g_T - q.t[0] > 0.3f) return;
    float d = 0.f;
    for (int i = 0; i + 1 < q.n; i++) {
        float seg = len(q.p[i] - q.p[i + 1]);
        float f0 = fmaxf(d / maxLen, (g_T - q.t[i]) / life);
        float f1 = fmaxf((d + seg) / maxLen, (g_T - q.t[i + 1]) / life);
        if (f0 >= 1.f) break;
        if (f1 > 1.f) f1 = 1.f;
        float pul = 0.72f + 0.28f * sinf(d * 7.f - g_T * 16.f);
        float a0 = al * g_dfade * powf(1.f - f0, 1.5f) * pul, a1 = al * g_dfade * powf(1.f - f1, 1.5f) * pul;
        Neon(q.p[i], q.p[i + 1], ww * (1.f - 0.8f * f0), mixc(c0, c1, f0), a0, a1);
        d += seg;
    }
}
// sparks
#define NSK 300
struct Spk { V3 p, v; float life, mx, w; Col c; };
static Spk g_sk[NSK];
static int g_skI = 0;
static void SparkAdd(V3 p, V3 v, float life, float w, Col c) {
    if (!g_fxOn) return;
    Spk& k = g_sk[g_skI]; g_skI = (g_skI + 1) % NSK;
    k.p = p; k.v = v; k.life = k.mx = life; k.w = w; k.c = c;
}
static int EmitN(int id, float rate) {        // how many particles emitter 'id' spawns this frame
    static float acc[64];
    if (!g_fxOn || id < 0 || id >= 64) return 0;
    acc[id] += rate * g_densK * g_dtE;
    int n = (int)acc[id]; if (n > 6) n = 6;
    acc[id] -= (float)n; if (acc[id] > 1.f) acc[id] = 1.f;
    return n;
}
static V3 RandV(float sx, float sy, float sz) { return v3((g_rng.f() - 0.5f) * sx, (g_rng.f() - 0.5f) * sy, (g_rng.f() - 0.5f) * sz); }
static void SparksRun(float dt) {
    for (int i = 0; i < NSK; i++) {
        Spk& k = g_sk[i];
        if (k.life <= 0.f) continue;
        k.life -= dt; if (k.life <= 0.f) continue;
        k.v.z -= 2.5f * dt; k.v = k.v * (1.f - clampf(1.4f * dt, 0.f, 0.5f));
        k.p = k.p + k.v * dt;
    }
}
static void SparksDraw() {
    for (int i = 0; i < NSK; i++) {
        Spk& k = g_sk[i];
        if (k.life <= 0.f) continue;
        float f = k.life / k.mx;
        V3 tail = k.p - Nz(k.v) * (0.05f + len(k.v) * 0.035f);
        Neon(k.p, tail, k.w * (0.5f + f), k.c, 0.95f * f, 0.f);
    }
}
// glitch bursts: two periodic windows, intensity re-rolled ~22 times a second
static float GlitchAmt(float T, float seed) {
    float p1 = T * 0.31f + seed * 0.37f; p1 -= floorf(p1);
    float p2 = T * 0.173f + seed * 0.61f + 0.4f; p2 -= floorf(p2);
    float b = 0.f;
    if (p1 < 0.06f) b = 1.f; else if (p1 < 0.085f) b = 0.5f;
    if (p2 < 0.045f) b = fmaxf(b, 0.8f);
    if (b <= 0.f) return 0.f;
    float step = floorf(T * 22.f + seed * 13.f);
    return b * (0.35f + 0.65f * Gh(step * 1.3f + seed));
}

// ======================================================================== solid body renderer
// Painter's algorithm (no depth buffer): faces are collected, sorted far->near and drawn with alpha blending.
// Gouraud lighting (fixed key light + ambient) plus a neon rim light that follows the palette / colour setting.
struct MF { V3 p[8]; DWORD c[8]; int n; float key; };
#define MAXMF 3000
static MF    g_mf[MAXMF];
static int   g_nmf = 0;
static float g_gk = 1e9f, g_ly = 0.f, g_lsg = 1.f;     // group depth key / layer / view side (for overlapping feathers)
static Col   g_rim = { 0.f, 255.f, 235.f };
static float g_rimK = 0.85f;
static const V3 g_Ld = { 0.43f, 0.25f, 0.87f };
typedef Col (*ColFn)(float u, float a);

static inline float Hs(float x) { float s = sinf(x * 12.9898f) * 43758.5453f; return s - floorf(s); }
static inline V3 Wn(V3 n) { return Nz(X.r * n.x + X.f * n.y + X.u * n.z); }
static float CR(const float* a, int n, float x) {     // Catmull-Rom sample of a table at fractional index x
    int i = (int)x; if (i > n - 2) i = n - 2; if (i < 0) i = 0;
    float t = x - (float)i;
    float p0 = a[i > 0 ? i - 1 : 0], p1 = a[i], p2 = a[i + 1], p3 = a[i + 2 < n ? i + 2 : n - 1];
    return 0.5f * ((2.f * p1) + (-p0 + p2) * t + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t * t + (-p0 + 3.f * p1 - 3.f * p2 + p3) * t * t * t);
}

static void FaceV(int n, const V3* lp, const V3* ln, const Col* cc, float alpha, bool two, float bias) {
    if (g_nmf >= MAXMF || n < 3 || n > 8) return;
    V3 wp[8], wn[8], cen = v3(0.f, 0.f, 0.f), nav = v3(0.f, 0.f, 0.f);
    for (int i = 0; i < n; i++) { wp[i] = Wp(lp[i]); cen = cen + wp[i]; }
    cen = cen * (1.f / (float)n);
    float z = dot(cen - g_cp, g_cf);
    if (z < 0.3f) return;
    float vv[8], vmax = 0.f;
    for (int i = 0; i < n; i++) { vv[i] = Vis(wp[i]); if (vv[i] > vmax) vmax = vv[i]; }
    if (vmax < 0.01f) return;                                // hidden behind the car
    if (ln) { for (int i = 0; i < n; i++) { wn[i] = Wn(ln[i]); nav = nav + wn[i]; } }
    else { V3 fn = Nz(cross(wp[1] - wp[0], wp[n - 1] - wp[0])); for (int i = 0; i < n; i++) wn[i] = fn; nav = fn; }
    nav = Nz(nav);
    float fl = dot(nav, Nz(g_cp - cen));
    if (!two && fl < -0.05f) return;                         // back face
    float sg = (two && fl < 0.f) ? -1.f : 1.f;
    MF& f = g_mf[g_nmf++];
    for (int i = 0; i < n; i++) {
        V3 nn = wn[i] * sg;
        float nd = dot(nn, Nz(g_cp - wp[i]));
        float df = fmaxf(0.f, dot(nn, g_Ld));
        float sh = 0.30f + 0.12f * (0.5f + 0.5f * nn.z) + 0.70f * df;
        float rim = powf(clampf(1.f - fabsf(nd), 0.f, 1.f), 2.6f) * g_rimK;
        f.c[i] = PK(cc[i].r * sh + g_rim.r * rim, cc[i].g * sh + g_rim.g * rim, cc[i].b * sh + g_rim.b * rim, alpha * g_dfade * vv[i]);
        f.p[i] = wp[i];
    }
    f.n = n;
    f.key = (g_gk < 1e8f) ? (g_gk - g_lsg * g_ly * 0.01f + z * 1e-4f) : (z - bias);
}
static int CmpMF(const void* a, const void* b) { float x = ((const MF*)a)->key, y = ((const MF*)b)->key; return x < y ? 1 : (x > y ? -1 : 0); }
static void SolidFlush() {
    if (g_nmf > 1) qsort(g_mf, g_nmf, sizeof(MF), CmpMF);
    if (g_nmf > 0) {
        SetBlend(false);
        for (int i = 0; i < g_nmf; i++) {
            const MF& f = g_mf[i];
            float sx[8], sy[8], sz_[8]; bool ok = true;
            for (int j = 0; j < f.n; j++) if (!Proj(f.p[j], sx[j], sy[j], sz_[j])) { ok = false; break; }
            if (!ok) continue;
            for (int j = 1; j < f.n - 1; j++) Tri(sx[0], sy[0], f.c[0], sx[j], sy[j], f.c[j], sx[j + 1], sy[j + 1], f.c[j + 1]);
        }
        SetBlend(true);
    }
    g_nmf = 0; g_gk = 1e9f; g_ly = 0.f;
}
// flat two-sided triangle / quad with per-vertex colours (feathers, fur tufts, teeth, mouth)
static void F3(V3 a, V3 b, V3 c, Col ca, Col cb, Col cc, float al, float bias = 0.f) {
    V3 p[3] = { a, b, c }; Col k[3] = { ca, cb, cc }; FaceV(3, p, NULL, k, al, true, bias);
}
static void F4(V3 a, V3 b, V3 c, V3 d, Col ca, Col cb, Col cc, Col cd, float al, float bias = 0.f) {
    V3 p[4] = { a, b, c, d }; Col k[4] = { ca, cb, cc, cd }; FaceV(4, p, NULL, k, al, true, bias);
}
// lofted skin around a polyline of elliptical rings (ru = radius above the ring centre, rd = below)
static void Loft(const V3* P, const V3* R, const V3* U, const float* rx, const float* ru, const float* rd,
                 int n, int K, ColFn cf, float al, bool cap0, bool cap1) {
    static V3 vp[16][18], vn[16][18]; static Col vc[16][18];
    if (n > 16) n = 16; if (n < 2) return; if (K > 16) K = 16; if (K < 3) K = 3;
    for (int i = 0; i < n; i++) {
        int i0 = i > 0 ? i - 1 : 0, i2 = i < n - 1 ? i + 1 : n - 1;
        V3 d = P[i2] - P[i0]; float dl = len(d);
        V3 ax = dl > 1e-5f ? d * (1.f / dl) : v3(0.f, 1.f, 0.f);
        float m0 = (rx[i0] + 0.5f * (ru[i0] + rd[i0])) * 0.5f, m2 = (rx[i2] + 0.5f * (ru[i2] + rd[i2])) * 0.5f;
        float sl = dl > 1e-5f ? -(m2 - m0) / dl : 0.f;
        for (int k = 0; k <= K; k++) {
            float a = k * 2.f * PI_ / K, cs = cosf(a), sn = sinf(a), r2 = sn >= 0.f ? ru[i] : rd[i];
            vp[i][k] = P[i] + R[i] * (cs * rx[i]) + U[i] * (sn * r2);
            V3 nn = Nz(R[i] * (cs / fmaxf(rx[i], 1e-4f)) + U[i] * (sn / fmaxf(r2, 1e-4f)));
            vn[i][k] = Nz(nn + ax * sl);
            vc[i][k] = cf((float)i / (float)(n - 1), a);
        }
    }
    for (int i = 0; i < n - 1; i++)
        for (int k = 0; k < K; k++) {
            V3 p[4] = { vp[i][k], vp[i][k + 1], vp[i + 1][k + 1], vp[i + 1][k] };
            V3 nn[4] = { vn[i][k], vn[i][k + 1], vn[i + 1][k + 1], vn[i + 1][k] };
            Col c[4] = { vc[i][k], vc[i][k + 1], vc[i + 1][k + 1], vc[i + 1][k] };
            FaceV(4, p, nn, c, al, false, 0.f);
        }
    for (int e = 0; e < 2; e++) {
        if (e == 0 ? !cap0 : !cap1) continue;
        int i = e ? n - 1 : 0;
        V3 ax = Nz(e ? P[n - 1] - P[n - 2] : P[0] - P[1]);
        for (int k = 0; k < K; k++) {
            V3 p[3] = { P[i], vp[i][k], vp[i][k + 1] }; V3 nn[3] = { ax, ax, ax };
            Col c[3] = { vc[i][k], vc[i][k], vc[i][k + 1] };
            FaceV(3, p, nn, c, al, false, 0.f);
        }
    }
}
// ellipsoid with axes e0,e1,e2 (e2 = pole axis); muscle masses, heads, paws, eyes
static void Ellip(V3 c, V3 e0, V3 e1, V3 e2, float a0, float a1, float a2, ColFn cf, float al, int NL = 5, int NK = 9) {
    static V3 vp[8][14], vn[8][14]; static Col vc[8][14];
    if (NL > 7) NL = 7; if (NK > 13) NK = 13;
    for (int i = 0; i <= NL; i++) {
        float ph = PI_ * i / NL, z = cosf(ph), rr = sinf(ph);
        for (int k = 0; k <= NK; k++) {
            float th = k * 2.f * PI_ / NK, x = rr * cosf(th), y = rr * sinf(th);
            vp[i][k] = c + e0 * (a0 * x) + e1 * (a1 * y) + e2 * (a2 * z);
            vn[i][k] = Nz(e0 * (x / a0) + e1 * (y / a1) + e2 * (z / a2));
            vc[i][k] = cf(0.5f * (1.f - z), th);
        }
    }
    for (int i = 0; i < NL; i++)
        for (int k = 0; k < NK; k++) {
            V3 p[4] = { vp[i][k], vp[i][k + 1], vp[i + 1][k + 1], vp[i + 1][k] };
            V3 nn[4] = { vn[i][k], vn[i][k + 1], vn[i + 1][k + 1], vn[i + 1][k] };
            Col cc[4] = { vc[i][k], vc[i][k + 1], vc[i + 1][k + 1], vc[i + 1][k] };
            FaceV(4, p, nn, cc, al, false, 0.f);
        }
}
static void Seg(V3 a, V3 b, float ra, float rb, ColFn cf, float al = 1.f, int K = 8, bool c0 = true, bool c1 = true) {
    V3 d = Nz(b - a), ref = fabsf(d.x) < 0.9f ? v3(1.f, 0.f, 0.f) : v3(0.f, 1.f, 0.f);
    V3 R = Nz(cross(d, ref)), U = cross(R, d);
    V3 P[2] = { a, b }, RR[2] = { R, R }, UU[2] = { U, U }; float r0[2] = { ra, rb };
    Loft(P, RR, UU, r0, r0, r0, 2, K, cf, al, c0, c1);
}
static void Tuft(V3 base, V3 dir, V3 wid, Col c0, Col c1, float al) {      // fur tuft / feather tip
    F3(base + wid, base - wid, base + dir, c0, c0, c1, al);
}
// flat spot on a skin surface (jaguar rosettes). t1/t2 = tangent axes
static void Disc(V3 pt, V3 nn, V3 t1, V3 t2, float rr, Col c, float bias) {
    V3 p[8], n6[8]; Col k[8];
    for (int j = 0; j < 8; j++) {
        float th = j * PI_ / 4.f + 0.2f;
        p[j] = pt + nn * 0.006f + t1 * (cosf(th) * rr) + t2 * (sinf(th) * rr * 1.2f); n6[j] = nn; k[j] = c;
    }
    FaceV(8, p, n6, k, 1.f, false, bias);
}
static void Rosettes(const V3* P, const V3* R, const V3* U, const float* rx, const float* ru, const float* rd, int n,
                     float t0, float t1, int rows, float rad, float seed, bool ring) {
    for (int r = 0; r < rows; r++) {
        float t = lerpf(t0, t1, rows > 1 ? (float)r / (float)(rows - 1) : 0.5f);
        float fi = t * (float)(n - 1); int i0 = (int)fi; if (i0 > n - 2) i0 = n - 2; if (i0 < 0) i0 = 0;
        float f = fi - (float)i0; int i1 = i0 + 1;
        V3 c = lerp3(P[i0], P[i1], f), Rv = Nz(lerp3(R[i0], R[i1], f)), Uv = Nz(lerp3(U[i0], U[i1], f));
        float x = lerpf(rx[i0], rx[i1], f), up = lerpf(ru[i0], ru[i1], f), dn = lerpf(rd[i0], rd[i1], f);
        V3 ax = Nz(P[i1] - P[i0]);
        for (int s = 0; s < 8; s++) {
            float jit = Hs(seed + r * 3.1f + s * 7.7f);
            float a = (-0.27f + 0.215f * s + ((r & 1) ? 0.107f : 0.f) + (jit - 0.5f) * 0.12f) * PI_;
            float cs = cosf(a), sn = sinf(a), r2 = sn >= 0.f ? up : dn;
            V3 pt = c + Rv * (cs * x) + Uv * (sn * r2);
            V3 nn = Nz(Rv * (cs / x) + Uv * (sn / r2));
            V3 t1v = Nz(cross(nn, ax)), t2v = cross(nn, t1v);
            float rr = rad * (0.75f + 0.5f * Hs(seed + r * 5.3f + s * 2.9f));
            Disc(pt, nn, t1v, t2v, rr, col(22.f, 15.f, 10.f), 0.05f);
            if (ring) Disc(pt, nn, t1v, t2v, rr * 0.55f, col(168.f, 108.f, 26.f), 0.07f);
        }
    }
}

// ---- colour functions (u = along the part, a = angle around it: sin(a) > 0 is the upper side)
static Col cWolfBody(float u, float a) {
    float s = sinf(a), n = (Hs(u * 23.f + a * 5.f) - 0.5f) * 0.18f;
    Col c = mixc(col(158.f, 156.f, 150.f), col(60.f, 58.f, 60.f), sm((s - 0.10f) / 0.6f) * 0.85f * sm((u - 0.04f) / 0.25f));
    c = mixc(c, col(218.f, 208.f, 190.f), sm((-s - 0.20f) / 0.5f));
    return col(c.r * (1.f + n), c.g * (1.f + n), c.b * (1.f + n));
}
static Col cWolfMusc(float u, float a) { (void)u; (void)a; return col(138.f, 136.f, 132.f); }
static Col cWolfNeck(float u, float a) { return cWolfBody(0.75f + 0.25f * u, a); }
static Col cWolfHead(float u, float a) {
    float s = sinf(a);
    Col c = mixc(col(112.f, 110.f, 108.f), col(176.f, 166.f, 150.f), sm((u - 0.35f) / 0.4f));
    return mixc(c, col(226.f, 218.f, 200.f), sm((-s + 0.05f) / 0.6f));
}
static Col cWolfJaw(float u, float a) { (void)a; return mixc(col(222.f, 214.f, 198.f), col(190.f, 180.f, 165.f), u); }
static Col cWolfTail(float u, float a) {
    float s = sinf(a);
    Col c = mixc(col(150.f, 146.f, 140.f), col(70.f, 68.f, 70.f), 0.5f * sm((s + 0.2f) / 0.8f) + 0.3f);
    return mixc(c, col(24.f, 22.f, 24.f), sm((u - 0.72f) / 0.25f));
}
static Col cWolfLeg(float u, float a) { (void)a; return mixc(col(150.f, 140.f, 126.f), col(196.f, 180.f, 150.f), u); }
static Col cWolfPaw(float u, float a) { (void)u; (void)a; return col(92.f, 82.f, 72.f); }
static Col cWolfEar(float u, float a) { (void)a; return mixc(col(104.f, 100.f, 98.f), col(30.f, 28.f, 30.f), sm((u - 0.5f) / 0.5f)); }
static Col cNose(float u, float a) { (void)u; (void)a; return col(18.f, 16.f, 18.f); }
static Col cJagBody(float u, float a) {
    float s = sinf(a), n = (Hs(u * 19.f + a * 4.f) - 0.5f) * 0.12f;
    Col c = mixc(col(208.f, 152.f, 50.f), col(186.f, 126.f, 34.f), sm((s - 0.2f) / 0.6f));
    c = mixc(c, col(238.f, 226.f, 198.f), sm((-s - 0.30f) / 0.45f));
    return col(c.r * (1.f + n), c.g * (1.f + n), c.b * (1.f + n));
}
static Col cJagMusc(float u, float a) { (void)u; (void)a; return cJagBody(0.5f, 0.7f); }
static Col cJagHead(float u, float a) {
    float s = sinf(a);
    Col c = col(204.f, 148.f, 48.f);
    return mixc(c, col(238.f, 226.f, 200.f), sm((u - 0.55f) / 0.35f) * sm((0.35f - s) / 0.6f));
}
static Col cJagTail(float u, float a) {
    (void)a; Col c = col(206.f, 150.f, 48.f);
    if (sinf(u * PI_ * 9.f) > 0.35f || u > 0.92f) c = col(24.f, 18.f, 14.f);
    return c;
}
static Col cJagLeg(float u, float a) { (void)a; return mixc(col(200.f, 144.f, 44.f), col(220.f, 176.f, 84.f), u); }
static Col cJagPaw(float u, float a) { (void)u; (void)a; return col(210.f, 160.f, 70.f); }
static Col cJagEar(float u, float a) { (void)u; (void)a; return col(26.f, 21.f, 18.f); }
static Col cJagNose(float u, float a) { (void)u; (void)a; return col(112.f, 62.f, 62.f); }

// ======================================================================== quadruped (wolf / jaguar)
struct QuadP { int kind; float len, hs, a, b, m, stride, lift, tail, neckL, neckA, headL, hr, headDown, bob, flex; };
static const QuadP QP_WOLF = { 0, 1.20f, 0.72f, 0.29f, 0.29f, 0.15f, 0.55f, 0.18f, 0.58f, 0.34f, 0.65f, 0.29f, 0.080f, 0.30f, 0.03f, 0.05f };
static const QuadP QP_JAG  = { 1, 1.25f, 0.68f, 0.27f, 0.27f, 0.13f, 0.60f, 0.20f, 0.95f, 0.24f, 0.50f, 0.26f, 0.105f, 0.22f, 0.05f, 0.10f };

static void Quad(const QuadP& Q, float gph, float T, int K) {
    const bool wolf = Q.kind == 0;
    const float TW = 2.f * PI_, L = Q.len, lk = wolf ? 1.f : 1.3f;
    const int Kb = K + 2 > 12 ? 12 : K + 2, Kl = K > 8 ? 8 : K;
    g_rim = Pal(0.35f); g_rimK = 0.42f;
    const float bob = Q.bob * sinf(TW * gph * 2.f);
    // ---- body: 12 rings from rump (0) to chest (1)
    enum { NC = 9, NB = 12 };
    static const float cWx[NC] = { 0.095f, 0.105f, 0.105f, 0.100f, 0.105f, 0.120f, 0.130f, 0.125f, 0.110f };
    static const float cWu[NC] = { 0.120f, 0.140f, 0.130f, 0.120f, 0.120f, 0.135f, 0.150f, 0.150f, 0.140f };
    static const float cWd[NC] = { 0.110f, 0.130f, 0.120f, 0.100f, 0.100f, 0.140f, 0.190f, 0.210f, 0.190f };
    static const float cJx[NC] = { 0.130f, 0.150f, 0.150f, 0.145f, 0.150f, 0.170f, 0.180f, 0.180f, 0.160f };
    static const float cJu[NC] = { 0.150f, 0.170f, 0.160f, 0.150f, 0.150f, 0.170f, 0.190f, 0.190f, 0.170f };
    static const float cJd[NC] = { 0.140f, 0.170f, 0.170f, 0.170f, 0.170f, 0.190f, 0.210f, 0.210f, 0.180f };
    const float *tx = wolf ? cWx : cJx, *tu = wolf ? cWu : cJu, *td = wolf ? cWd : cJd;
    V3 sp[NB], RR[NB], UU[NB]; float rx[NB], ru[NB], rd[NB];
    for (int k = 0; k < NB; k++) {
        float t = (float)k / (NB - 1), fi = t * (NC - 1);
        rx[k] = CR(tx, NC, fi); ru[k] = CR(tu, NC, fi); rd[k] = CR(td, NC, fi);
        sp[k] = v3(0.f, lerpf(-L * 0.5f, L * 0.5f, t), Q.hs - ru[k] + (wolf ? 0.03f * t : 0.f) + bob + Q.flex * sinf(TW * gph - t * 2.4f));
        RR[k] = v3(1.f, 0.f, 0.f); UU[k] = v3(0.f, 0.f, 1.f);
    }
    Loft(sp, RR, UU, rx, ru, rd, NB, Kb, wolf ? cWolfBody : cJagBody, 1.f, true, true);
    {   // round off both ends of the torso
        V3 ex0 = v3(1.f, 0.f, 0.f), ey0 = v3(0.f, 1.f, 0.f), ez0 = v3(0.f, 0.f, 1.f);
        ColFn mc = wolf ? cWolfMusc : cJagMusc;
        Ellip(sp[0] + v3(0.f, 0.01f, (rd[0] - ru[0]) * 0.5f), ex0, ey0, ez0, rx[0] * 1.02f, 0.10f, (ru[0] + rd[0]) * 0.5f, mc, 1.f, 5, 10);
        Ellip(sp[NB - 1] + v3(0.f, -0.01f, (rd[NB - 1] - ru[NB - 1]) * -0.5f), ex0, ey0, ez0, rx[NB - 1] * 1.02f, 0.09f, (ru[NB - 1] + rd[NB - 1]) * 0.5f, mc, 1.f, 5, 10);
    }
    if (!wolf) Rosettes(sp, RR, UU, rx, ru, rd, NB, 0.03f, 0.97f, 9, 0.048f, 1.f, true);
    // ---- neck
    V3 sh = sp[NB - 1];
    float na = Q.neckA + 0.07f * sinf(TW * gph * 2.f + 1.f);
    V3 nd = v3(0.f, cosf(na), sinf(na));
    V3 hb = sh + nd * Q.neckL;
    enum { NN = 4 };
    V3 nk[NN], nR[NN], nU[NN]; float nrx[NN], nru[NN], nrd[NN];
    V3 nU0 = cross(v3(1.f, 0.f, 0.f), nd);
    for (int i = 0; i < NN; i++) {
        float t = (float)i / (NN - 1);
        nk[i] = sh - nd * 0.04f + (hb - sh + nd * 0.04f) * t; nR[i] = v3(1.f, 0.f, 0.f); nU[i] = nU0;
        float r = lerpf(wolf ? 0.115f : 0.135f, Q.hr * (wolf ? 1.0f : 1.05f), t) * (wolf ? 1.f + 0.35f * sinf(PI_ * (0.3f + 0.5f * t)) : 1.f);
        nrx[i] = r; nru[i] = r * (wolf ? 1.05f : 1.f); nrd[i] = r * (wolf ? 1.1f : 0.95f);
    }
    Loft(nk, nR, nU, nrx, nru, nrd, NN, Kb, wolf ? cWolfNeck : cJagBody, 1.f, false, false);
    if (!wolf) Rosettes(nk, nR, nU, nrx, nru, nrd, NN, 0.15f, 0.8f, 3, 0.040f, 5.f, true);
    // ---- head
    float hp = Q.headDown + 0.08f * sinf(TW * gph * 2.f + 0.5f);
    V3 hd = v3(0.f, cosf(hp), -sinf(hp)), hR = v3(1.f, 0.f, 0.f), hU = cross(hR, hd);
    const float hl = Q.headL, hr = Q.hr;
    enum { NH = 6 };
    static const float st_[NH] = { 0.f, 0.15f, 0.35f, 0.55f, 0.80f, 1.0f };
    static const float wX[NH] = { 1.00f, 1.05f, 0.95f, 0.60f, 0.46f, 0.40f }, wU[NH] = { 1.00f, 1.00f, 0.85f, 0.50f, 0.40f, 0.34f }, wD[NH] = { 0.85f, 0.90f, 0.75f, 0.42f, 0.30f, 0.26f };
    static const float gX[NH] = { 1.00f, 1.12f, 1.05f, 0.80f, 0.68f, 0.56f }, gU[NH] = { 1.00f, 1.00f, 0.88f, 0.62f, 0.52f, 0.45f }, gD[NH] = { 0.90f, 0.95f, 0.85f, 0.60f, 0.50f, 0.42f };
    V3 hpP[NH], hpR[NH], hpU[NH]; float hx_[NH], hu_[NH], hd_[NH];
    for (int i = 0; i < NH; i++) {
        hpP[i] = hb + hd * (hl * st_[i]); hpR[i] = hR; hpU[i] = hU;
        hx_[i] = hr * (wolf ? wX[i] : gX[i]); hu_[i] = hr * (wolf ? wU[i] : gU[i]); hd_[i] = hr * (wolf ? wD[i] : gD[i]);
    }
    Loft(hpP, hpR, hpU, hx_, hu_, hd_, NH, Kl + 2 > 10 ? 10 : Kl + 2, wolf ? cWolfHead : cJagHead, 1.f, true, true);
    if (!wolf) Rosettes(hpP, hpR, hpU, hx_, hu_, hd_, NH, 0.12f, 0.50f, 3, 0.016f, 9.f, false);
    // nose
    Ellip(hb + hd * (hl * 0.99f) + hU * (hr * 0.04f), hR, hU, hd, hr * (wolf ? 0.24f : 0.30f), hr * 0.20f, hr * 0.20f, wolf ? cNose : cJagNose, 1.f, 4, 8);
    // lower jaw + mouth interior
    float open = (wolf ? 0.05f : 0.08f) + 0.05f * (0.5f + 0.5f * sinf(TW * gph * 2.f + 2.f));
    V3 jh = hb + hd * (hl * 0.28f) - hU * (hr * 0.50f);
    V3 jd = hd * cosf(open) - hU * sinf(open), jU = cross(hR, jd);
    float jl = hl * (wolf ? 0.72f : 0.64f);
    V3 jP[3] = { jh, jh + jd * (jl * 0.5f), jh + jd * jl }, jR[3] = { hR, hR, hR }, jUU[3] = { jU, jU, jU };
    float jx[3] = { hr * 0.55f, hr * (wolf ? 0.34f : 0.45f), hr * (wolf ? 0.20f : 0.30f) }, jz[3] = { jx[0] * 0.8f, jx[1] * 0.8f, jx[2] * 0.8f };
    Loft(jP, jR, jUU, jx, jz, jz, 3, Kl, wolf ? cWolfJaw : cJagHead, 1.f, false, true);
    {
        Col dk = col(86.f, 26.f, 32.f);
        V3 a0 = hb + hd * (hl * 0.30f) - hU * (hr * 0.40f), a1 = hb + hd * (hl * 0.97f) - hU * (hr * 0.16f);
        F4(a0 + hR * (hr * 0.30f), a0 - hR * (hr * 0.30f), a1 - hR * (hr * 0.18f), a1 + hR * (hr * 0.18f), dk, dk, dk, dk, 1.f);
        float tl = wolf ? 0.040f : 0.048f; Col wh = col(244.f, 240.f, 226.f);
        for (int sd = -1; sd <= 1; sd += 2) {
            float s = (float)sd;
            V3 ub = hb + hd * (hl * 0.88f) + hR * (s * hr * 0.26f) - hU * (hr * 0.20f);
            Tuft(ub, -hU * tl, hR * 0.007f, wh, col(210.f, 205.f, 190.f), 1.f);
            V3 lb = jh + jd * (jl * 0.90f) + hR * (s * hr * 0.20f) + jU * (hr * 0.10f);
            Tuft(lb, jU * (tl * 0.9f), hR * 0.007f, wh, col(210.f, 205.f, 190.f), 1.f);
        }
    }
    // ears
    V3 earTip[2];
    for (int sd = -1; sd <= 1; sd += 2) {
        float s = (float)sd;
        V3 eb = hb + hd * (hl * 0.14f) + hU * (hr * 0.78f) + hR * (s * hr * 0.58f);
        V3 ea = Nz(hU + hR * (s * 0.30f) - hd * 0.15f);
        V3 Rr = Nz(hR - ea * dot(hR, ea)), Ur = cross(Rr, ea);
        float el = wolf ? 0.115f : 0.045f, ew = 0.040f;
        if (wolf) {
            V3 P3[3] = { eb, eb + ea * (el * 0.55f), eb + ea * el }, R3[3] = { Rr, Rr, Rr }, U3[3] = { Ur, Ur, Ur };
            float x3[3] = { ew, ew * 0.65f, ew * 0.05f }, z3[3] = { ew * 0.38f, ew * 0.28f, ew * 0.04f };
            Loft(P3, R3, U3, x3, z3, z3, 3, 6, cWolfEar, 1.f, false, true);
        } else {
            V3 P4[4], R4[4], U4[4]; float x4[4] = { ew, ew * 0.95f, ew * 0.70f, ew * 0.15f }, z4[4];
            static const float fs[4] = { 0.f, 0.4f, 0.75f, 1.f };
            for (int i = 0; i < 4; i++) { P4[i] = eb + ea * (el * fs[i]); R4[i] = Rr; U4[i] = Ur; z4[i] = x4[i] * 0.45f; }
            Loft(P4, R4, U4, x4, z4, z4, 4, 6, cJagEar, 1.f, false, true);
        }
        earTip[sd > 0 ? 1 : 0] = eb + ea * el;
    }
    // fur: cheek ruff + chest mane (wolf only)
    if (wolf) {
        Col c0 = col(222.f, 214.f, 198.f), c1 = col(160.f, 152.f, 140.f);
        for (int sd = -1; sd <= 1; sd += 2) {
            float s = (float)sd;
            for (int i = 0; i < 4; i++) {
                V3 b = hb + hd * (hl * (0.16f + 0.03f * i)) + hR * (s * hr * 0.95f) - hU * (hr * (0.05f + 0.22f * i));
                Tuft(b, -hd * 0.07f + hR * (s * 0.025f) - hU * 0.02f, hU * 0.022f, c0, c1, 0.95f);
            }
            for (int i = 0; i < 4; i++) {
                V3 b = lerp3(sh, hb, 0.2f + 0.22f * i) + hR * (s * 0.095f) - hU * 0.02f;
                Tuft(b, -nd * 0.085f + hR * (s * 0.02f) - v3(0.f, 0.f, 0.03f), hU * 0.03f, cWolfNeck(0.5f, 0.f), c1, 0.95f);
            }
        }
        for (int i = 0; i < 5; i++) {
            V3 b = sh + v3(0.f, 0.06f * i - 0.1f, -rd[NB - 1] * 0.9f + 0.01f * i);
            Tuft(b, v3(0.f, -0.05f, -0.05f), hR * 0.03f, c0, c1, 0.95f);
        }
    }
    // ---- tail (8 rings)
    enum { NT = 8 };
    V3 tp[NT], tR[NT], tU[NT]; float trx[NT];
    static const float tW[NT] = { 0.045f, 0.058f, 0.068f, 0.072f, 0.068f, 0.056f, 0.040f, 0.020f };
    static const float tJ[NT] = { 0.055f, 0.050f, 0.046f, 0.044f, 0.042f, 0.040f, 0.038f, 0.030f };
    const float tupk = wolf ? 0.f : 0.15f;
    for (int k = 0; k < NT; k++) {
        float t = (float)k / (NT - 1);
        tp[k] = v3(0.08f * Q.tail * t * sinf(TW * gph - t * 4.f), sp[0].y - 0.03f - Q.tail * t * 0.95f,
                   sp[0].z + ru[0] * 0.35f + tupk * Q.tail * t - Q.tail * 0.35f * t * t + 0.06f * Q.tail * sinf(TW * gph * 2.f - t * 3.f));
        trx[k] = wolf ? tW[k] : tJ[k];
    }
    for (int k = 0; k < NT; k++) {
        V3 d = Nz(tp[k > 0 ? k - 1 : 0] - tp[k < NT - 1 ? k + 1 : NT - 1]);       // points backwards->forwards
        tR[k] = v3(1.f, 0.f, 0.f); tU[k] = cross(-d, tR[k]);
        tU[k] = Nz(tU[k]);
        if (tU[k].z < 0.f) tU[k] = -tU[k];
    }
    Loft(tp, tR, tU, trx, trx, trx, NT, Kl, wolf ? cWolfTail : cJagTail, 1.f, false, true);
    if (wolf) {   // bushy fur along the tail
        Col c0 = col(120.f, 117.f, 114.f), c1 = col(84.f, 81.f, 80.f);
        for (int k = 1; k < NT - 1; k++)
            for (int sd = -1; sd <= 1; sd += 2)
                Tuft(tp[k] + v3((float)sd * trx[k] * 0.8f, 0.f, 0.f), v3((float)sd * 0.025f, -0.07f, -0.01f), v3(0.f, 0.f, 0.03f), c0, c1, 0.9f);
    }
    // ---- legs
    static const float offG[4] = { 0.00f, 0.12f, 0.50f, 0.62f };
    V3 jointK[4], jointF[4]; float legPh[4];
    for (int j = 0; j < 4; j++) {
        bool fore = j < 2;
        float side = (j & 1) ? 1.f : -1.f;
        float s = gph + offG[j]; s -= floorf(s);
        int ri = fore ? NB - 3 : 2;
        float hw = rx[ri] * 0.80f + (fore ? 0.01f : 0.02f);
        float hy = sp[ri].y + (fore ? 0.f : -0.03f), hz = sp[ri].z - rd[ri] * 0.30f;
        V3 hip = v3(side * hw, hy, hz);
        float fy, fz;
        if (s < 0.55f) { fy = hy + Q.stride * (0.5f - s / 0.55f); fz = 0.f; }
        else { float u = (s - 0.55f) / 0.45f; fy = hy + Q.stride * (-0.5f + u); fz = Q.lift * sinf(PI_ * u); }
        V3 foot = v3(side * hw, fy, fz);
        V3 ankle = foot + v3(0.f, fore ? 0.02f : -0.05f, Q.m);
        V3 knee = IK2(hip, ankle, Q.a, Q.b, fore ? -1.f : 1.f);
        ColFn lc = wolf ? cWolfLeg : cJagLeg, pc = wolf ? cWolfPaw : cJagPaw;
        V3 ex = v3(1.f, 0.f, 0.f), ey = v3(0.f, 1.f, 0.f), ez = v3(0.f, 0.f, 1.f);
        if (fore) {
            Ellip(hip + v3(side * 0.015f, 0.f, 0.04f * lk), ex, ey, ez, 0.060f * lk, 0.12f * lk, 0.13f * lk, wolf ? cWolfMusc : cJagMusc, 1.f, 5, 10);
            Seg(hip, knee, 0.050f * lk, 0.038f * lk, lc, 1.f, Kl, false, false);
            Seg(knee, ankle, 0.036f * lk, 0.026f * lk, lc, 1.f, Kl, false, false);
            Ellip(knee, ex, ey, ez, 0.040f * lk, 0.040f * lk, 0.040f * lk, lc, 1.f, 4, 8);
        } else {
            Ellip(hip + v3(side * 0.02f, -0.02f, 0.f), ex, ey, ez, 0.060f * lk, 0.115f * lk, 0.115f * lk, wolf ? cWolfMusc : cJagMusc, 1.f, 5, 10);
            Seg(hip, knee, 0.075f * lk, 0.045f * lk, lc, 1.f, Kl, false, false);
            Seg(knee, ankle, 0.045f * lk, 0.028f * lk, lc, 1.f, Kl, false, false);
            Ellip(knee, ex, ey, ez, 0.050f * lk, 0.050f * lk, 0.050f * lk, lc, 1.f, 4, 8);
            Ellip(ankle, ex, ey, ez, 0.030f * lk, 0.030f * lk, 0.030f * lk, lc, 1.f, 4, 8);
        }
        Seg(ankle, foot + v3(0.f, 0.f, 0.02f), 0.026f * lk, 0.024f * lk, lc, 1.f, Kl, false, false);
        Ellip(foot + v3(0.f, 0.035f, 0.022f), ex, ey, ez, 0.034f * lk, 0.058f * lk, 0.028f * lk, pc, 1.f, 4, 8);
        jointK[j] = knee; jointF[j] = foot; legPh[j] = s;
    }
    SolidFlush();
    // ---- neon glow on top of the solid body: dorsal line, tail line, ears, eyes, joints, paws
    for (int k = 0; k < NB - 1; k++)
        Ln(sp[k] + v3(0.f, 0.f, ru[k] + 0.004f), sp[k + 1] + v3(0.f, 0.f, ru[k + 1] + 0.004f), 0.006f, Pal(0.40f + 0.3f * k / NB), 0.80f);
    for (int k = 0; k < NT - 1; k++)
        Ln(tp[k] + tU[k] * trx[k], tp[k + 1] + tU[k + 1] * trx[k + 1], 0.005f, Pal(0.10f + 0.1f * k), 0.65f);
    for (int e = 0; e < 2; e++) Jt(earTip[e], 0.012f, Pal(0.95f), 0.7f);
    for (int sd = -1; sd <= 1; sd += 2) {
        V3 eye = hb + hd * (hl * 0.40f) + hR * ((float)sd * hr * (wolf ? 0.62f : 0.70f)) + hU * (hr * 0.32f);
        Jt(eye, 0.016f, col(255.f, wolf ? 190.f : 215.f, 60.f), 0.95f);
    }
    for (int j = 0; j < 4; j++) { Jt(jointK[j], 0.012f, Pal(0.20f + 0.1f * j), 0.45f); Jt(jointF[j] + v3(0.f, 0.03f, 0.01f), 0.020f, Pal(0.30f + 0.1f * j), 0.55f); }
    if (g_fxOn) {
        // eye streaks and tail-tip ribbon (they stay behind in the air while the car drives on)
        for (int sd = -1; sd <= 1; sd += 2) {
            V3 eye = hb + hd * (hl * 0.40f) + hR * ((float)sd * hr * (wolf ? 0.62f : 0.70f)) + hU * (hr * 0.32f);
            int id = g_tid + (sd > 0 ? 1 : 0);
            TrPush(id, Wp(eye));
            TrDraw(id, 0.012f * X.s, Pal(0.95f), Pal(0.35f), 0.9f, 3.5f * X.s, 0.55f);
        }
        TrPush(g_tid2, Wp(tp[NT - 1]));
        TrDraw(g_tid2, 0.016f * X.s, Pal(0.10f), Pal(0.60f), 0.8f, 4.f * X.s, 0.6f);
        // paw shock rings + sparks at every touch-down
        Col rc = Pal(0.55f);
        for (int j = 0; j < 4; j++) {
            float ph = legPh[j];
            if (ph < 0.35f) {
                float u = ph / 0.35f, rr = (0.05f + 0.40f * u) * lk;
                RingL(v3(jointF[j].x, jointF[j].y + 0.03f, 0.014f), v3(1.f, 0.f, 0.f), v3(0.f, 1.f, 0.f), rr, rr * 1.15f, 14, 0.008f, rc, 0.85f * powf(1.f - u, 1.3f));
            }
            if (ph < 0.10f) {
                int n = EmitN(g_eid + j, 80.f);
                for (int q = 0; q < n; q++) {
                    float rnd = g_rng.f();
                    Col sc = wolf ? mixc(col(40.f, 240.f, 255.f), col(255.f, 50.f, 200.f), rnd) : mixc(col(255.f, 190.f, 50.f), col(255.f, 80.f, 20.f), rnd);
                    SparkAdd(Wp(jointF[j] + v3(0.f, 0.03f, 0.02f)), RandV(2.2f, 2.2f, 0.f) + v3(0.f, 0.f, 0.8f + g_rng.f() * 1.6f), 0.35f + g_rng.f() * 0.35f, 0.010f, sc);
                }
            }
        }
        // glitch burst: red / cyan ghost of the spine + torn scan lines
        if (g_gl > 0.f) {
            for (int k = 0; k < NB - 1; k++)
                for (int sd = -1; sd <= 1; sd += 2) {
                    float o = (float)sd * 0.07f * g_gl;
                    Ln(sp[k] + v3(o, 0.f, ru[k] + 0.004f), sp[k + 1] + v3(o, 0.f, ru[k + 1] + 0.004f), 0.007f,
                       sd < 0 ? col(255.f, 40.f, 90.f) : col(40.f, 230.f, 255.f), 0.85f * g_gl);
                }
            for (int q = 0; q < 6; q++) {
                float zz = Gh(g_glStep * 1.7f + q * 3.3f) * 0.95f, yy = lerpf(-0.6f, 0.6f, Gh(g_glStep + q * 5.1f));
                float wd = 0.18f + 0.5f * Gh(g_glStep * 2.1f + (float)q);
                Ln(v3(-wd, yy, zz), v3(wd, yy, zz), 0.008f, (q & 1) ? col(255.f, 40.f, 200.f) : col(40.f, 255.f, 230.f), 0.9f * g_gl);
            }
        }
    }
}


// ======================================================================== bird (eagle / phoenix)
struct BirdP { float bl, span, tail, hz, amp; int kind; };
static const BirdP BP_EAGLE = { 0.95f, 1.35f, 0.55f, 1.4f, 0.55f, 0 };
static const BirdP BP_PHX   = { 1.30f, 2.40f, 3.20f, 1.0f, 0.50f, 1 };

static int g_bk = 0;     // 0 = eagle, 1 = phoenix (used by the colour functions)
static Col cBBody(float u, float a) {
    float s = sinf(a), n = (Hs(u * 31.f + a * 4.f) - 0.5f) * 0.18f;
    Col c;
    if (g_bk == 0) {
        c = mixc(col(78.f, 52.f, 33.f), col(120.f, 86.f, 56.f), sm((-s + 0.1f) / 0.8f) * 0.6f);
        c = mixc(c, col(238.f, 236.f, 228.f), sm((0.20f - u) / 0.20f));
    } else {
        c = mixc(col(205.f, 30.f, 22.f), col(255.f, 120.f, 25.f), sm((0.4f - s) * 0.9f));
        c = mixc(c, col(255.f, 196.f, 60.f), sm((-s - 0.1f) / 0.7f));
    }
    return col(c.r * (1.f + n), c.g * (1.f + n), c.b * (1.f + n));
}
static Col cBHead(float u, float a) { (void)u; (void)a; return g_bk == 0 ? col(244.f, 242.f, 234.f) : col(255.f, 205.f, 70.f); }
static Col cBeak(float u, float a) { (void)u; (void)a; return g_bk == 0 ? col(250.f, 196.f, 40.f) : col(255.f, 160.f, 30.f); }
static Col cBLeg(float u, float a) { (void)u; (void)a; return col(240.f, 188.f, 40.f); }
static Col cBThigh(float u, float a) { (void)u; (void)a; return g_bk == 0 ? col(86.f, 58.f, 38.f) : col(220.f, 60.f, 25.f); }

// one feather: kite shape, base colour -> tip colour, drawn in a given layer of the current wing/tail group
static void Feather(V3 base, V3 tip, float w0, float w1, Col c0, Col c1, float al, float layer) {
    V3 d = tip - base, wd = cross(d, v3(0.f, 0.f, 1.f));
    wd = len(wd) < 1e-4f ? v3(1.f, 0.f, 0.f) : Nz(wd);
    g_ly = layer;
    F4(base + wd * (w0 * 0.5f), base - wd * (w0 * 0.5f), tip - wd * (w1 * 0.5f), tip + wd * (w1 * 0.5f), c0, c0, c1, c1, al);
}
static void GroupBegin(V3 localCentre) {
    V3 wc = Wp(localCentre);
    g_gk = dot(wc - g_cp, g_cf);
    g_lsg = dot(g_cp - wc, X.u) >= 0.f ? 1.f : -1.f;
}
static void GroupEnd() { g_gk = 1e9f; g_ly = 0.f; }

static void Bird(const BirdP& B, float T, float seed, int dens) {
    const float TW = 2.f * PI_, bl = B.bl;
    const bool ph = B.kind == 1;
    g_bk = B.kind; g_rim = Pal(0.35f); g_rimK = 0.30f;
    const int Kl = 6;
    float ft = TW * (B.hz * T + seed);
    float bz = 0.05f * bl * sinf(ft - 0.4f);
    // ---- body: 9 rings, head end (0) to tail root (1)
    enum { NC = 5, NS = 9 };
    static const float by_[NC] = { 0.40f, 0.27f, 0.05f, -0.18f, -0.38f };
    static const float bz_[NC] = { 0.04f, 0.03f, 0.00f, -0.01f, 0.00f };
    static const float bx_[NC] = { 0.046f, 0.075f, 0.115f, 0.090f, 0.040f };
    static const float bu_[NC] = { 0.046f, 0.070f, 0.110f, 0.085f, 0.036f };
    static const float bd_[NC] = { 0.046f, 0.075f, 0.120f, 0.090f, 0.036f };
    V3 bp[NS], RR[NS], UU[NS]; float rx[NS], ru[NS], rd[NS];
    for (int i = 0; i < NS; i++) {
        float fi = (float)i / (NS - 1) * (NC - 1);
        bp[i] = v3(0.f, CR(by_, NC, fi) * bl, CR(bz_, NC, fi) * bl + bz); RR[i] = v3(1.f, 0.f, 0.f); UU[i] = v3(0.f, 0.f, 1.f);
        rx[i] = CR(bx_, NC, fi) * bl; ru[i] = CR(bu_, NC, fi) * bl; rd[i] = CR(bd_, NC, fi) * bl;
    }
    Loft(bp, RR, UU, rx, ru, rd, NS, 10, cBBody, 1.f, false, true);
    V3 tailRoot = bp[NS - 1];
    // head, beak
    V3 ex = v3(1.f, 0.f, 0.f), ey = v3(0.f, 1.f, 0.f), ez = v3(0.f, 0.f, 1.f);
    V3 head = bp[0] + v3(0.f, 0.05f * bl, 0.015f * bl);
    Ellip(head, ex, ey, ez, 0.050f * bl, 0.065f * bl, 0.050f * bl, cBHead, 1.f, 5, 9);
    V3 bb = head + v3(0.f, 0.058f * bl, -0.006f * bl), bt = bb + v3(0.f, 0.085f * bl, -0.012f * bl);
    Seg(bb, bt, 0.030f * bl, 0.012f * bl, cBeak, 1.f, 6, false, true);
    Seg(bt - v3(0.f, 0.01f * bl, 0.f), bt + v3(0.f, 0.012f * bl, -0.040f * bl), 0.012f * bl, 0.004f * bl, cBeak, 1.f, 5, false, true);
    Seg(bb + v3(0.f, 0.f, -0.012f * bl), bt + v3(0.f, -0.015f * bl, -0.018f * bl), 0.020f * bl, 0.008f * bl, cBeak, 1.f, 5, false, true);
    // legs: feathered thigh, yellow shank, three toes with talons (trailing under the body)
    for (int sd = -1; sd <= 1; sd += 2) {
        float s = (float)sd;
        V3 th = bp[6] + v3(s * 0.055f * bl, 0.05f * bl, -0.06f * bl), kn = th + v3(s * 0.005f * bl, -0.10f * bl, -0.08f * bl), ft_ = kn + v3(0.f, -0.08f * bl, -0.05f * bl);
        Ellip(th, ex, ey, ez, 0.045f * bl, 0.07f * bl, 0.055f * bl, cBThigh, 1.f, 4, 8);
        Seg(th, kn, 0.030f * bl, 0.020f * bl, cBThigh, 1.f, 5, false, false);
        Seg(kn, ft_, 0.016f * bl, 0.012f * bl, cBLeg, 1.f, 5, false, true);
        for (int q = -1; q <= 1; q++)
            Seg(ft_, ft_ + v3((float)q * 0.025f * bl, 0.07f * bl, -0.04f * bl), 0.010f * bl, 0.004f * bl, cBLeg, 1.f, 4, false, true);
    }
    // ---- tail
    {
        int nt = ph ? 9 : 7;
        GroupBegin(tailRoot + v3(0.f, -B.tail * 0.5f, 0.f));
        for (int i = 0; i < nt; i++) {
            float a = lerpf(-0.5f, 0.5f, (float)i / (nt - 1));
            V3 dir = v3(sinf(a), -cosf(a), 0.f);
            float tlen = B.tail * (0.75f + 0.25f * cosf(a * 2.f)), lay = (float)(nt / 2 - abs(i - nt / 2));
            if (!ph) {
                float wz = 0.04f * tlen * sinf(ft + i * 0.5f);
                V3 tip = tailRoot + dir * tlen + v3(0.f, 0.f, wz);
                Feather(tailRoot, tip, 0.14f * bl, 0.24f * bl, col(240.f, 238.f, 230.f), col(214.f, 212.f, 204.f), 1.f, lay);
            } else {          // ribbon feather: 6 quads, red -> orange -> gold, fading out
                V3 prev = tailRoot; float wprev = 0.09f * bl;
                for (int q = 1; q <= 6; q++) {
                    float t = (float)q / 6.f;
                    float wx = 0.15f * tlen * t * t * sinf(T * 3.f - t * 4.f + i);
                    float wz = -0.12f * tlen * t * t + 0.10f * tlen * t * t * cosf(T * 2.5f - t * 4.f + i * 0.7f);
                    V3 pt = tailRoot + dir * (tlen * t) + v3(wx, 0.f, wz);
                    float wq = 0.09f * bl * (1.f - 0.55f * t) + 0.05f * bl * sinf(PI_ * t);
                    Col c0 = mixc(col(220.f, 35.f, 25.f), col(255.f, 210.f, 70.f), sm((t - 0.1f - 0.1f / 6.f) * 1.1f));
                    Col c1 = mixc(col(220.f, 35.f, 25.f), col(255.f, 210.f, 70.f), sm((t - 0.1f) * 1.1f));
                    V3 d = pt - prev, wd = cross(d, v3(0.f, 0.f, 1.f)); wd = len(wd) < 1e-4f ? v3(1.f, 0.f, 0.f) : Nz(wd);
                    g_ly = lay;
                    F4(prev + wd * (wprev * 0.5f), prev - wd * (wprev * 0.5f), pt - wd * (wq * 0.5f), pt + wd * (wq * 0.5f), c0, c0, c1, c1, 0.95f * (1.f - 0.55f * t * t));
                    prev = pt; wprev = wq;
                }
            }
        }
        GroupEnd();
    }
    // phoenix crest: three flame plumes
    if (ph)
        for (int i = 0; i < 3; i++) {
            float w = sinf(T * 3.f + i * 1.3f);
            V3 b0 = head + v3(0.f, -0.02f * bl, 0.04f * bl);
            V3 p1 = b0 + v3((i - 1) * 0.05f * bl, -0.14f * bl, 0.14f * bl);
            V3 p2 = p1 + v3((i - 1) * 0.08f * bl + 0.05f * bl * w, -0.18f * bl, 0.04f * bl);
            Feather(b0, p1, 0.035f * bl, 0.03f * bl, col(255.f, 90.f, 25.f), col(255.f, 150.f, 40.f), 0.95f, 0.f);
            Feather(p1, p2, 0.03f * bl, 0.004f * bl, col(255.f, 150.f, 40.f), col(255.f, 225.f, 90.f), 0.9f, 0.f);
        }
    // ---- wings
    int np = 6 + dens * 2, ns = 5 + dens;
    static V3 Sj[2], Ej[2], Wj[2], Tj[2], Pe[2][10], Se[2][8];
    for (int sg = -1; sg <= 1; sg += 2) {
        float s = (float)sg; int w = sg > 0 ? 1 : 0;
        V3 S = v3(s * 0.08f * bl, 0.10f * bl, 0.04f * bl + bz);
        float a1 = B.amp * 0.7f * sinf(ft), a2 = B.amp * 1.0f * sinf(ft - 0.7f), a3 = B.amp * 1.3f * sinf(ft - 1.4f);
        float L1 = B.span * 0.30f, L2 = B.span * 0.30f, L3 = B.span * 0.40f;
        V3 E  = S  + v3(s * cosf(a1), -0.10f, sinf(a1)) * L1;
        V3 Wr = E  + v3(s * cosf(a2), -0.12f, sinf(a2)) * L2;
        V3 Tp = Wr + v3(s * cosf(a3), -0.30f, sinf(a3)) * L3;
        Sj[w] = S; Ej[w] = E; Wj[w] = Wr; Tj[w] = Tp;
        Col pa, pb, sa, sb, ca_, cb_, arm;
        if (!ph) { pa = col(84.f, 58.f, 36.f); pb = col(30.f, 20.f, 14.f); sa = col(92.f, 64.f, 42.f); sb = col(58.f, 40.f, 28.f); ca_ = col(118.f, 84.f, 54.f); cb_ = col(98.f, 68.f, 44.f); arm = col(96.f, 66.f, 42.f); }
        else     { pa = col(230.f, 60.f, 25.f); pb = col(255.f, 205.f, 60.f); sa = col(200.f, 35.f, 25.f); sb = col(255.f, 120.f, 25.f); ca_ = col(170.f, 25.f, 20.f); cb_ = col(225.f, 80.f, 25.f); arm = col(200.f, 40.f, 25.f); }
        GroupBegin((S + Wr + Tp) * (1.f / 3.f));
        // primaries (layer 0)
        V3 pt[10];
        for (int j = 0; j < np; j++) {
            float f = (float)j / (np - 1);
            V3 base = Wr + (Tp - Wr) * (f * 0.92f);
            float lf = B.span * (0.55f - 0.15f * f);
            V3 d = Nz(v3(s * (0.10f + 0.40f * f), -1.f, 0.35f * sinf(a3 - 0.4f - f)));
            V3 e = base + d * lf;
            Feather(base, e, B.span * 0.085f, B.span * 0.060f, pa, mixc(pa, pb, 0.9f), 1.f, 0.f);
            pt[j] = e; Pe[w][j] = e;
        }
        // secondaries (layer 1)
        V3 st[8];
        for (int j = 0; j < ns; j++) {
            float t = (float)j / (ns - 1);
            V3 base = t < 0.5f ? lerp3(Wr, E, t * 2.f) : lerp3(E, S, t * 2.f - 1.f);
            float lf = B.span * 0.46f * (1.f - 0.25f * t);
            V3 d = Nz(v3(s * 0.03f, -1.f, 0.25f * sinf(a2 - 0.2f - t)));
            V3 e = base + d * lf;
            Feather(base, e, B.span * 0.20f, B.span * 0.16f, sa, sb, 1.f, 1.f);
            st[j] = e; Se[w][j] = e;
        }
        // coverts along the arm (layer 2, on top)
        for (int j = 0; j < 6; j++) {
            float t = (float)j / 5.f;
            V3 base = t < 0.5f ? lerp3(Wr, E, t * 2.f) : lerp3(E, S, t * 2.f - 1.f);
            V3 d = Nz(v3(s * 0.03f, -1.f, 0.1f));
            Feather(base, base + d * (B.span * 0.17f * (1.f - 0.3f * t)), B.span * 0.17f, B.span * 0.12f, ca_, cb_, 1.f, 2.f);
        }
        // arm bones/muscle (layer 3)
        g_ly = 3.f;
        Seg(S, E, 0.032f * bl, 0.026f * bl, cBThigh, 1.f, 6, true, true);
        Seg(E, Wr, 0.026f * bl, 0.018f * bl, cBThigh, 1.f, 6, false, true);
        GroupEnd();
    }
    SolidFlush();
    // ---- neon glow: eyes, wing edge lines, fingertip links, flame sparks
    for (int sd = -1; sd <= 1; sd += 2) Jt(head + v3((float)sd * 0.040f * bl, 0.035f * bl, 0.018f * bl), 0.014f, col(255.f, ph ? 230.f : 220.f, 120.f), 0.95f);
    for (int w = 0; w < 2; w++) {
        float s = w ? 1.f : -1.f;
        Col ca = Pal(0.35f);
        Ln(Sj[w], Ej[w], 0.012f, ca, 0.9f); Ln(Ej[w], Wj[w], 0.011f, ca, 0.9f); Ln(Wj[w], Tj[w], 0.010f, ca, 0.9f);
        Jt(Ej[w], 0.022f, ca, 0.6f); Jt(Wj[w], 0.020f, ca, 0.6f);
        for (int j = 1; j < np; j++) Ln(Pe[w][j - 1], Pe[w][j], 0.006f, Pal(0.55f + 0.3f * j / np), 0.55f);
        if (ph) {
            Jt(Tj[w], 0.07f, col(255.f, 150.f, 30.f), 0.8f);
            for (int j = 0; j < 3; j++) {
                float p = T * 0.9f + j * 0.33f + s * 0.1f; p -= floorf(p);
                Jt(Tj[w] + v3(0.f, -0.6f * p, 0.15f * sinf(T * 5.f + j)), 0.05f * (1.f - p), col(255.f, 90.f + 100.f * p, 20.f), 0.8f * (1.f - p));
            }
        }
    }
    if (ph)
        for (int j = 0; j < 8; j++) {
            float p = T * 0.7f + j * 0.125f; p -= floorf(p);
            Jt(tailRoot + v3(0.4f * sinf(T * 5.f + j * 2.f), -B.tail * (0.3f + 0.75f * p), 0.3f * cosf(T * 4.f + j)),
               0.06f * (1.f - p), col(255.f, 80.f + 120.f * p, 20.f), 0.9f * (1.f - p));
        }
    if (g_fxOn) {
        // long neon ribbons flowing out of the wing tips (and a second one from the outer feathers)
        for (int w = 0; w < 2; w++) {
            int ia = g_tid + w * 2, ib = ia + 1;
            Col c0 = ph ? col(255.f, 215.f, 70.f) : Pal(0.30f), c1 = ph ? col(255.f, 40.f, 20.f) : Pal(0.80f);
            float ww2 = (ph ? 0.040f : 0.020f) * X.s, mxl = (ph ? 8.f : 5.f) * X.s;
            TrPush(ia, Wp(Tj[w])); TrPush(ib, Wp(Pe[w][np - 2]));
            TrDraw(ia, ww2, c0, c1, 0.95f, mxl, ph ? 0.9f : 0.7f);
            TrDraw(ib, ww2 * 0.6f, c1, c0, 0.7f, mxl * 0.8f, 0.6f);
            int n = EmitN(g_eid + w, ph ? 60.f : 26.f);
            for (int q = 0; q < n; q++) {
                float rnd = g_rng.f();
                Col sc = ph ? mixc(col(255.f, 210.f, 70.f), col(255.f, 60.f, 20.f), rnd) : mixc(col(220.f, 255.f, 255.f), Pal(0.4f), rnd);
                SparkAdd(Wp(Tj[w]), RandV(1.6f, 1.6f, 0.f) + v3(0.f, 0.f, 0.5f + g_rng.f()), 0.45f + g_rng.f() * 0.5f, 0.010f * X.s, sc);
            }
        }
        if (ph) {      // phoenix tail ribbon
            TrPush(g_tid + 4, Wp(tailRoot + v3(0.f, -B.tail * 0.95f, -0.1f)));
            TrDraw(g_tid + 4, 0.050f * X.s, col(255.f, 200.f, 60.f), col(255.f, 40.f, 20.f), 0.9f, 9.f * X.s, 1.0f);
        }
    }
}


// ======================================================================== western dragon  (scaled body, skull, jaw, teeth, horns, frill, fire, 4 muscular clawed legs, bat wings)
// Flies random loops around the car. Body = 44 points following the flight path, with a swim wave, an arched neck,
// a rib cage, belly plates, a dorsal crest, a detailed skull with hinged lower jaw, teeth, tongue, whiskers and flames.
static V3 DragonPath(float t, const Car& C, float sp) {
    float a = t * 0.9f;
    float x = (C.W * 0.5f + 3.2f * sp) * (0.75f * sinf(a + 0.6f) + 0.35f * sinf(a * 2.3f + 1.7f));
    float y = (C.L * 0.5f + 3.5f * sp) * (0.75f * sinf(a * 0.8f + 2.1f) + 0.35f * sinf(a * 1.9f + 0.4f));
    float z = C.z1 + 2.2f + 0.8f * sp + 1.3f * sinf(a * 1.4f + 0.3f) + 0.6f * sinf(a * 3.1f);
    return v3(x, y, z);
}
// lays n points `spacing` apart along the path, walking back in time from the head
static void Trail(const Car& C, float sp, float t, float spacing, int n, V3* out) {
    out[0] = DragonPath(t, C, sp);
    int k = 1, guard = 0;
    float tt = t;
    while (k < n && guard < 6000) {
        tt -= 0.02f;
        V3 nx = DragonPath(tt, C, sp);
        V3 d = nx - out[k - 1];
        float dl = len(d);
        if (dl >= spacing) { out[k] = out[k - 1] + d * (spacing / dl); k++; }
        guard++;
    }
    while (k < n) { out[k] = out[k - 1] + (k >= 2 ? out[k - 1] - out[k - 2] : v3(0.f, -spacing, 0.f)); k++; }
}

static inline float DrHash(float x) { float s = sinf(x * 12.9898f) * 43758.5453f; return s - floorf(s); }
static inline float DrBump(float s, float c, float w) { float d = (s - c) / w; return expf(-d * d); }
static float DrInterp(const float* a, const float* t, int n, float x) {
    if (x <= t[0]) return a[0];
    for (int i = 0; i < n - 1; i++)
        if (x <= t[i + 1]) { float f = (x - t[i]) / (t[i + 1] - t[i]); return a[i] + (a[i + 1] - a[i]) * f; }
    return a[n - 1];
}

// curved, tapering horn made of 7 bone segments with little ridge rings
static void DrHorn(V3 b, V3 back, V3 up, V3 out, float Lh, float curl, float ww, Col c, float al) {
    V3 prev = b;
    for (int j = 1; j <= 7; j++) {
        float u = (float)j / 7.f;
        V3 p = b + back * (Lh * 0.85f * u) + up * (Lh * (0.55f * u - curl * u * u)) + out * (Lh * 0.30f * sinf(u * 2.2f));
        Ln(prev, p, ww * (1.9f - 1.4f * u), c, al, al * (1.f - 0.45f * u));
        float cr = Lh * 0.035f * (1.f - u);
        Ln(p - out * cr, p + out * cr, ww * 0.6f, c, al * 0.6f);
        prev = p;
    }
    Jt(prev, ww * 2.f, mixc(c, col(255.f, 255.f, 255.f), 0.5f), al * 0.6f);
}

// breath of fire: soft fire balls that grow and cool, three flame tongues, flying sparks
static void DrFire(V3 mouth, V3 hT, V3 hR, V3 hU, float sz, float T, float gate) {
    if (gate < 0.02f) return;
    for (int q = 0; q < 18; q++) {
        float p = (float)q / 17.f;
        float ang = q * 2.399f + T * 8.f;
        float wob = 0.5f + 0.5f * DrHash(q * 1.7f + floorf(T * 6.f));
        float d = (0.18f + p * 3.4f) * sz;
        float spread = (0.03f + 0.42f * p * p + 0.05f * p) * sz * (0.6f + 0.4f * wob);
        V3 pos = mouth + hT * d + (hR * cosf(ang) + hU * sinf(ang)) * spread + hU * (0.25f * sz * p * p);
        Col cc = p < 0.3f ? mixc(col(255.f, 250.f, 200.f), col(255.f, 170.f, 30.f), p / 0.3f)
                          : mixc(col(255.f, 170.f, 30.f), col(210.f, 40.f, 10.f), (p - 0.3f) / 0.7f);
        Jt(pos, (0.045f + 0.30f * p) * sz, cc, gate * powf(1.f - p, 0.8f) * 0.9f);
    }
    for (int t = 0; t < 3; t++) {
        V3 prev = mouth;
        float ph = t * 2.1f;
        for (int j = 1; j <= 9; j++) {
            float u = (float)j / 9.f;
            V3 pos = mouth + hT * (u * 3.0f * sz) + hR * (sinf(T * 7.f + u * 6.f + ph) * 0.20f * sz * u)
                           + hU * (cosf(T * 6.f + u * 5.f + ph * 1.3f) * 0.16f * sz * u);
            Col cc = mixc(col(255.f, 230.f, 150.f), col(255.f, 70.f, 10.f), u);
            Ln(prev, pos, 0.012f * sz * (1.4f - u), cc, gate * 0.9f * (1.05f - (u - 0.111f)), gate * 0.9f * (1.05f - u));
            prev = pos;
        }
    }
    for (int q = 0; q < 10; q++) {
        float ph = T * 0.9f + q * 0.137f; ph -= floorf(ph);
        float r1 = DrHash(q * 3.1f) - 0.5f, r2 = DrHash(q * 5.7f) - 0.5f;
        V3 pos = mouth + hT * (ph * 4.2f * sz) + hR * (r1 * ph * 1.6f * sz) + hU * (r2 * ph * 1.2f * sz + ph * ph * 0.4f * sz);
        Jt(pos, 0.012f * sz, col(255.f, 200.f, 80.f), gate * (1.f - ph));
    }
}

// skull: 8 elliptical stations, hinged lower jaw (opens by `ang`), teeth, fangs, tongue, eyes, brow, horns, frill, whiskers
static void DrHead(V3 H0, V3 hT, V3 hR, V3 hU, float hl, float hr, float ww, float sz, float T, float ang, float gate, int K) {
    enum { NS = 8, NJ = 5 };
    static const float tt[NS] = { 0.f, 0.16f, 0.32f, 0.48f, 0.64f, 0.78f, 0.90f, 1.00f };
    static const float kx[NS] = { 0.95f, 1.06f, 0.96f, 0.72f, 0.58f, 0.52f, 0.47f, 0.36f };
    static const float kz[NS] = { 0.85f, 0.95f, 0.78f, 0.52f, 0.40f, 0.36f, 0.34f, 0.28f };
    static const float ko[NS] = { 0.00f, 0.06f, 0.02f, -0.08f, -0.13f, -0.15f, -0.15f, -0.13f };
    static const float jt[NJ] = { 0.f, 0.25f, 0.5f, 0.75f, 1.0f };
    static const float jx[NJ] = { 0.62f, 0.58f, 0.48f, 0.38f, 0.27f };
    static const float jz[NJ] = { 0.42f, 0.36f, 0.30f, 0.24f, 0.18f };
    static V3 HP[NS][12], JP[NJ][12];
    V3 HC[NS], JC[NJ];
    float cs[12], sn[12];
    if (K > 10) K = 10;
    for (int k = 0; k < K; k++) { float a = k * 2.f * PI_ / K; cs[k] = cosf(a); sn[k] = sinf(a); }
    const float w0 = ww * 0.55f;
    const Col WH = col(255.f, 255.f, 255.f);

    // ---- upper skull skin
    for (int i = 0; i < NS; i++) {
        HC[i] = H0 + hT * (hl * tt[i]) + hU * (hr * ko[i]);
        for (int k = 0; k < K; k++) HP[i][k] = HC[i] + hR * (cs[k] * hr * kx[i]) + hU * (sn[k] * hr * kz[i]);
    }
    for (int i = 0; i < NS; i++) {
        Col c = Pal(0.82f + 0.18f * tt[i]);
        for (int k = 0; k < K; k++) {
            int k2 = (k + 1) % K;
            Ln(HP[i][k], HP[i][k2], w0, c, 0.34f);
            if (i < NS - 1) {
                Ln(HP[i][k], HP[i + 1][k], w0, c, 0.26f);
                Ln(HP[i][k], HP[i + 1][k2], w0 * 0.8f, c, 0.14f);
            }
        }
    }
    // ridges: skull / nose ridge, lip lines, little crest spikes on top of the skull
    for (int i = 0; i < NS - 1; i++) {
        Col c = Pal(0.9f);
        V3 a = HC[i] + hU * (hr * kz[i]), b = HC[i + 1] + hU * (hr * kz[i + 1]);
        Ln(a, b, ww * 1.5f, c, 0.95f);
        if (i >= 1 && i <= 5) Ln(a, a + hU * (hr * 0.30f) - hT * (hr * 0.30f), ww * 0.9f, c, 0.85f, 0.25f);
        for (int sd = -1; sd <= 1; sd += 2) {
            float s = (float)sd;
            V3 la = HC[i] + hR * (s * hr * kx[i]) - hU * (hr * kz[i] * 0.15f);
            V3 lb = HC[i + 1] + hR * (s * hr * kx[i + 1]) - hU * (hr * kz[i + 1] * 0.15f);
            Ln(la, lb, ww * 1.2f, c, 0.85f);
        }
    }

    // ---- lower jaw
    V3 Hj = H0 + hT * (hl * 0.16f) - hU * (hr * 0.40f);
    V3 jd = hT * cosf(ang) - hU * sinf(ang);
    V3 ju = hU * cosf(ang) + hT * sinf(ang);
    float jl = hl * 0.84f;
    for (int i = 0; i < NJ; i++) {
        JC[i] = Hj + jd * (jl * jt[i]);
        for (int k = 0; k < K; k++) JP[i][k] = JC[i] + hR * (cs[k] * hr * jx[i]) + ju * (sn[k] * hr * jz[i]);
    }
    for (int i = 0; i < NJ; i++) {
        Col c = Pal(0.88f + 0.1f * jt[i]);
        for (int k = 0; k < K; k++) {
            int k2 = (k + 1) % K;
            Ln(JP[i][k], JP[i][k2], w0, c, 0.30f);
            if (i < NJ - 1) Ln(JP[i][k], JP[i + 1][k], w0, c, 0.22f);
        }
        if (i < NJ - 1) {
            Ln(JC[i] - ju * (hr * jz[i]), JC[i + 1] - ju * (hr * jz[i + 1]), ww * 1.3f, c, 0.9f);                 // chin line
            for (int sd = -1; sd <= 1; sd += 2) {
                float s = (float)sd;
                Ln(JC[i] + hR * (s * hr * jx[i]) + ju * (hr * jz[i] * 0.2f),
                   JC[i + 1] + hR * (s * hr * jx[i + 1]) + ju * (hr * jz[i + 1] * 0.2f), ww * 1.1f, c, 0.85f);  // lower lip
            }
        }
    }
    Ln(Hj, JC[NJ - 1], ww * 1.4f, Pal(0.92f), 0.9f);                                                            // jaw bone
    Jt(Hj, 0.025f * sz, Pal(0.9f), 0.8f);

    // ---- teeth and fangs
    {
        Col tc = mixc(Pal(0.9f), WH, 0.6f);
        for (int sd = -1; sd <= 1; sd += 2) {
            float s = (float)sd;
            for (int q = 0; q < 7; q++) {          // upper row, q==1 is the big fang
                float t = 0.40f + q * 0.085f;
                float kxt = DrInterp(kx, tt, NS, t), kzt = DrInterp(kz, tt, NS, t), kot = DrInterp(ko, tt, NS, t);
                V3 base = H0 + hT * (hl * t) + hR * (s * hr * kxt * 0.97f) + hU * (hr * (kot - kzt * 0.18f));
                float tl = hl * ((q == 1) ? 0.17f : (0.06f + 0.012f * (float)(q & 1)));
                V3 tip = base - hU * tl - hT * (tl * 0.18f);
                Ln(base + hT * (hl * 0.02f), tip, ww * 0.8f, tc, 0.95f, 0.5f);
                Ln(base - hT * (hl * 0.02f), tip, ww * 0.8f, tc, 0.95f, 0.5f);
                Ln(base, tip, ww * 0.5f, WH, 0.9f, 0.4f);
            }
            for (int q = 0; q < 6; q++) {          // lower row, last one is a tusk
                float t = 0.30f + q * 0.12f;
                float jxt = DrInterp(jx, jt, NJ, t), jzt = DrInterp(jz, jt, NJ, t);
                V3 base = Hj + jd * (jl * t) + hR * (s * hr * jxt * 0.95f) + ju * (hr * jzt * 0.15f);
                float tl = hl * ((q == 5) ? 0.15f : 0.055f);
                V3 tip = base + ju * tl + jd * (tl * 0.20f);
                Ln(base + jd * (hl * 0.02f), tip, ww * 0.8f, tc, 0.95f, 0.5f);
                Ln(base - jd * (hl * 0.02f), tip, ww * 0.8f, tc, 0.95f, 0.5f);
                Ln(base, tip, ww * 0.5f, WH, 0.9f, 0.4f);
            }
        }
    }
    // ---- tongue (forked) and throat glow
    if (ang > 0.14f) {
        float vis = clampf((ang - 0.14f) * 6.f, 0.f, 1.f);
        V3 prev = Hj + jd * (jl * 0.15f) + ju * (hr * 0.12f);
        for (int j = 1; j <= 6; j++) {
            float u = (float)j / 6.f;
            V3 p = Hj + jd * (jl * (0.15f + 0.75f * u)) + ju * (hr * (0.12f + 0.05f * sinf(T * 8.f + u * 6.f)))
                      + hR * (hr * 0.10f * sinf(T * 6.f + u * 8.f));
            Ln(prev, p, ww * (1.4f - 0.7f * u), col(255.f, 70.f, 90.f), 0.85f * vis);
            prev = p;
        }
        Ln(prev, prev + jd * (hl * 0.10f) + hR * (hr * 0.22f), ww * 0.7f, col(255.f, 90.f, 110.f), 0.8f * vis, 0.3f * vis);
        Ln(prev, prev + jd * (hl * 0.10f) - hR * (hr * 0.22f), ww * 0.7f, col(255.f, 90.f, 110.f), 0.8f * vis, 0.3f * vis);
    }
    if (gate > 0.02f) {
        Jt(H0 + hT * (hl * 0.45f) - hU * (hr * 0.15f), 0.07f * sz, col(255.f, 160.f, 40.f), gate * 0.9f);
        Jt(H0 + hT * (hl * 0.92f) - hU * (hr * 0.25f), 0.05f * sz, col(255.f, 220.f, 120.f), gate);
    }

    // ---- eyes, brow ridge, nostrils, cheek spikes, chin spikes, horns, frill, whiskers
    for (int sd = -1; sd <= 1; sd += 2) {
        float s = (float)sd;
        Col c = Pal(0.92f);
        V3 eye = HC[2] + hR * (s * hr * kx[2] * 0.88f) + hU * (hr * kz[2] * 0.42f);
        Jt(eye, 0.050f * sz, col(255.f, 190.f, 40.f), 1.f);
        Jt(eye, 0.018f * sz, WH, 1.f);
        Ln(eye + hU * (0.030f * sz), eye - hU * (0.030f * sz), ww * 0.6f, WH, 1.f);                           // slit pupil
        V3 bF = HC[3] + hR * (s * hr * kx[3] * 0.70f) + hU * (hr * kz[3] * 0.75f);
        V3 bM = eye + hU * (hr * 0.28f);
        V3 bB = HC[1] + hR * (s * hr * kx[1] * 0.85f) + hU * (hr * kz[1] * 0.80f);
        Ln(bF, bM, ww * 1.3f, c, 0.95f); Ln(bM, bB, ww * 1.3f, c, 0.95f);
        Ln(bM, bM + hU * (hr * 0.28f) - hT * (hr * 0.45f) + hR * (s * hr * 0.20f), ww * 1.0f, c, 0.9f, 0.3f);   // brow spike
        V3 nos = HC[6] + hR * (s * hr * kx[6] * 0.45f) + hU * (hr * kz[6] * 0.55f);
        Jt(nos, 0.016f * sz, col(255.f, 140.f, 40.f), 0.5f + 0.5f * gate);                                    // nostril
        Ln(nos - hT * (hr * 0.15f), nos + hT * (hr * 0.15f) + hR * (s * hr * 0.10f), ww * 0.8f, c, 0.8f);

        for (int j = 0; j < 3; j++) {         // cheek spikes
            V3 pb = H0 + hT * (hl * (0.10f + 0.08f * j)) + hR * (s * hr * (1.0f - 0.06f * j)) - hU * (hr * 0.25f);
            V3 tip = pb - hT * (hl * (0.28f - 0.05f * j)) + hR * (s * hr * 0.55f) - hU * (hr * 0.30f * (1.f - 0.3f * j));
            Ln(pb, tip, ww * 1.0f, c, 0.9f, 0.3f);
            Ln(pb + hT * (hl * 0.07f), tip, ww * 0.8f, c, 0.8f, 0.3f);
        }
        for (int j = 0; j < 2; j++) {         // chin spikes
            V3 base = JC[2 + j] + hR * (s * hr * jx[2 + j] * 0.5f) - ju * (hr * jz[2 + j]);
            V3 tip = base - ju * (hr * 0.5f) - jd * (hl * 0.10f);
            Ln(base, tip, ww * 0.9f, c, 0.85f, 0.3f);
            Ln(base + jd * (hl * 0.05f), tip, ww * 0.7f, c, 0.7f, 0.3f);
        }
        // main horn (swept back and up) + smaller lower horn
        V3 hb = H0 + hT * (hl * 0.12f) + hR * (s * hr * 0.55f) + hU * (hr * 0.70f);
        DrHorn(hb, -hT, hU, hR * s, 0.62f * hl, 0.35f, ww, c, 0.95f);
        V3 hb2 = H0 + hT * (hl * 0.06f) + hR * (s * hr * 0.90f) + hU * (hr * 0.25f);
        DrHorn(hb2, -hT, hU * 0.5f, hR * s, 0.38f * hl, 0.15f, ww * 0.8f, c, 0.85f);

        // frill: four fin rays fanning back from the skull, scalloped membrane between them
        V3 fb[4], ft[4];
        for (int j = 0; j < 4; j++) {
            fb[j] = H0 + hT * (hl * 0.05f) + hR * (s * hr * 0.85f) + hU * (hr * (0.20f - 0.25f * j));
            ft[j] = fb[j] - hT * (hl * 0.45f * (1.f - 0.10f * j)) + hR * (s * hl * 0.35f * (0.5f + 0.2f * j))
                          + hU * (hl * 0.10f - hl * 0.12f * j + 0.05f * hl * sinf(T * 3.f + j + s));
            Ln(fb[j], ft[j], ww * 1.1f, c, 0.9f, 0.4f);
        }
        for (int j = 0; j < 3; j++) {
            V3 mid = lerp3(ft[j], ft[j + 1], 0.5f); mid = mid + (fb[j] - mid) * 0.12f;
            Ln(ft[j], mid, ww * 0.8f, c, 0.7f); Ln(mid, ft[j + 1], ww * 0.8f, c, 0.7f);
            for (int q = 1; q <= 2; q++) {
                float f = 0.4f + 0.25f * q;
                Ln(lerp3(fb[j], ft[j], f), lerp3(fb[j + 1], ft[j + 1], f), ww * 0.5f, c, 0.35f);
            }
        }
        // two long wavy whiskers per side
        for (int w = 0; w < 2; w++) {
            V3 st = H0 + hT * (hl * 0.86f) + hR * (s * hr * 0.48f) - hU * (hr * 0.05f);
            float L = 1.1f * sz * (1.f - 0.25f * w);
            V3 prev = st;
            for (int k = 1; k <= 7; k++) {
                float u = (float)k / 7.f;
                V3 p = st - hT * (L * 0.8f * u) + hR * (s * L * (0.35f + 0.25f * w) * u) - hU * (L * (0.15f + 0.25f * w) * u * u)
                          + hU * (L * 0.09f * sinf(T * 2.2f + u * 5.f + w * 1.7f + s) * u)
                          + hR * (s * L * 0.05f * sinf(T * 1.7f + u * 4.f + w) * u);
                Ln(prev, p, ww * (0.7f - 0.5f * u), Pal(0.95f), 0.7f * (1.f - 0.6f * u));
                prev = p;
            }
            Jt(prev, 0.012f * sz, Pal(0.95f), 0.6f);
        }
    }
}

// ---- part 2 helpers: muscular limb segment, claws, feet/hands, legs, wings ---------------------------------------

// muscular limb segment: `ns` rings (tapered + muscle bulge), K-gon skin, long lines and a diagonal scale lattice
static void DrSeg(V3 a, V3 b, float r0, float r1, float bulge, int ns, int K, float ww, float u0, float u1, float alRing, float alLong) {
    V3 d = b - a; float L = len(d);
    if (L < 1e-5f) return;
    if (ns > 6) ns = 6;
    if (ns < 2) ns = 2;
    if (K > 8) K = 8;
    if (K < 4) K = 4;
    V3 t = d * (1.f / L);
    V3 ref = fabsf(t.z) > 0.9f ? v3(1.f, 0.f, 0.f) : v3(0.f, 0.f, 1.f);
    V3 p1 = Nz(cross(t, ref)), p2 = cross(t, p1);
    static V3 rp[6][8];
    for (int i = 0; i < ns; i++) {
        float u = (float)i / (float)(ns - 1);
        V3 c = a + d * u;
        float r = lerpf(r0, r1, u) + bulge * sinf(u * PI_);
        for (int k = 0; k < K; k++) {
            float an = k * 2.f * PI_ / K;
            rp[i][k] = c + p1 * (cosf(an) * r) + p2 * (sinf(an) * r);
        }
    }
    for (int i = 0; i < ns; i++) {
        Col c = Pal(lerpf(u0, u1, (float)i / (float)(ns - 1)));
        for (int k = 0; k < K; k++) {
            int k2 = (k + 1) % K;
            Ln(rp[i][k], rp[i][k2], ww * 0.6f, c, alRing);
            if (i < ns - 1) {
                Ln(rp[i][k], rp[i + 1][k], ww * 0.55f, c, alLong);
                Ln(rp[i][k], rp[i + 1][k2], ww * 0.45f, c, alLong * 0.6f);
            }
        }
    }
}

// curved, tapering claw: 4 bone segments that hook toward `curlTo`
static void DrClaw(V3 b, V3 dir, V3 curlTo, float L, float ww, Col c, float al) {
    V3 prev = b;
    const Col WH = col(255.f, 255.f, 255.f);
    for (int j = 1; j <= 4; j++) {
        float u = (float)j / 4.f;
        V3 p = b + dir * (L * u) + curlTo * (L * 0.60f * u * u);
        Ln(prev, p, ww * (1.6f - 1.1f * u), mixc(c, WH, 0.30f + 0.45f * u), al, al * (1.f - 0.45f * u));
        prev = p;
    }
    Jt(prev, ww * 1.3f, WH, al * 0.5f);
}

// foot / hand: toes with 3 phalanges + knuckle joints, webbing between toes, curved claws, optional rear spur
static void DrFoot(V3 b, V3 fw, V3 out, V3 dn, float sz, float ww, Col c, float splay, float grip, float al, bool rear) {
    const int nT = rear ? 3 : 4;
    static const float seg[3] = { 0.062f, 0.050f, 0.042f };
    V3 kn[4], rt[4];
    for (int k = 0; k < nT; k++) {
        float lat = (float)k - 0.5f * (float)(nT - 1);
        float lk = 1.f - 0.12f * fabsf(lat);
        V3 root = b + out * (lat * 0.034f * sz);
        rt[k] = root;
        V3 prev = root, dprev = fw;
        for (int m = 0; m < 3; m++) {
            float am = 0.10f + grip * (0.38f + 0.36f * (float)m);
            V3 dir = Nz(fw * cosf(am) + dn * sinf(am) + out * (lat * splay * 0.30f));
            V3 p = prev + dir * (seg[m] * sz * lk);
            Ln(prev, p, ww * (1.5f - 0.25f * (float)m), c, 0.92f);
            Jt(p, 0.010f * sz, c, 0.65f);
            if (m == 0) kn[k] = p;
            dprev = dir; prev = p;
        }
        DrClaw(prev, dprev, dn, 0.062f * sz, ww, c, 0.95f);
        Ln(root, root + dn * (0.02f * sz) + fw * (0.01f * sz), ww * 0.9f, mixc(c, col(255.f, 235.f, 190.f), 0.5f), 0.7f);    // toe pad
    }
    for (int k = 0; k < nT - 1; k++) {
        Ln(rt[k], rt[k + 1], ww * 0.9f, c, 0.7f);
        Ln(kn[k], kn[k + 1], ww * 0.6f, c, 0.45f);                                                                  // webbing
    }
    if (rear) DrClaw(b, Nz(-fw * 0.8f + dn * 0.3f), dn, 0.055f * sz, ww, c, 0.9f);                                  // rear toe / spur
}

// one leg. front = arm (elbow back, wrist forward), rear = digitigrade haunch (thigh, shin, hock, long foot)
static void DrLeg(V3 anc, V3 out, V3 dn, V3 fw, bool front, float sz, float ww, float T, float ph, int K, Col c) {
    const Col WH = col(255.f, 255.f, 255.f);
    const float sw1 = sinf(T * 2.2f + ph), sw2 = sinf(T * 2.2f + ph + 1.1f);
    Col cb = mixc(c, col(255.f, 235.f, 190.f), 0.4f);
    int Kl = K < 6 ? K : 6;
    if (front) {
        V3 el = anc + out * (0.15f * sz) + dn * (0.17f * sz) - fw * (0.17f * sz + 0.04f * sz * sw1);
        V3 wr = el + out * (0.04f * sz) + dn * (0.15f * sz) + fw * (0.22f * sz + 0.05f * sz * sw2);
        V3 hb = wr + dn * (0.045f * sz) + fw * (0.05f * sz);
        DrSeg(anc, el, 0.085f * sz, 0.050f * sz, 0.022f * sz, 5, Kl, ww, 0.20f, 0.30f, 0.34f, 0.26f);   // upper arm + shoulder muscle
        DrSeg(el, wr, 0.050f * sz, 0.028f * sz, 0.012f * sz, 4, Kl, ww, 0.30f, 0.38f, 0.34f, 0.26f);     // forearm
        Ln(anc, el, ww * 1.5f, c, 0.9f); Ln(el, wr, ww * 1.3f, c, 0.9f); Ln(wr, hb, ww * 1.1f, c, 0.9f);   // bones
        Ln(anc + out * (0.06f * sz), el + out * (0.05f * sz), ww * 0.9f, cb, 0.6f);                         // outer muscle line
        Jt(anc, 0.030f * sz, c, 0.55f); Jt(el, 0.026f * sz, c, 0.8f); Jt(wr, 0.020f * sz, c, 0.75f);
        Ln(el, el - fw * (0.13f * sz) + out * (0.04f * sz) + dn * (0.01f * sz), ww * 1.0f, c, 0.9f, 0.25f);     // elbow spike
        for (int q = 0; q < 2; q++) {                                                                       // forearm spikes
            V3 b0 = lerp3(el, wr, 0.30f + 0.35f * q) + out * (0.03f * sz);
            Ln(b0, b0 + out * (0.05f * sz) - fw * (0.05f * sz), ww * 0.8f, c, 0.85f, 0.25f);
        }
        DrFoot(hb, fw, out, dn, sz, ww, Pal(0.34f), 0.9f, 0.85f + 0.2f * sw2, 1.f, false);
    } else {
        V3 kn = anc + out * (0.13f * sz) + dn * (0.09f * sz) + fw * (0.27f * sz + 0.04f * sz * sw1);
        V3 hk = kn + out * (0.02f * sz) + dn * (0.20f * sz) - fw * (0.26f * sz);
        V3 fb = hk + dn * (0.19f * sz) + fw * (0.10f * sz + 0.04f * sz * sw2);
        DrSeg(anc, kn, 0.115f * sz, 0.062f * sz, 0.030f * sz, 5, Kl, ww, 0.50f, 0.58f, 0.36f, 0.28f);     // big thigh
        DrSeg(kn, hk, 0.060f * sz, 0.034f * sz, 0.014f * sz, 4, Kl, ww, 0.58f, 0.64f, 0.34f, 0.26f);      // shin
        DrSeg(hk, fb, 0.032f * sz, 0.022f * sz, 0.004f * sz, 3, Kl, ww, 0.64f, 0.68f, 0.30f, 0.24f);      // foot bone
        Ln(anc, kn, ww * 1.5f, c, 0.9f); Ln(kn, hk, ww * 1.3f, c, 0.9f); Ln(hk, fb, ww * 1.1f, c, 0.9f);
        Ln(anc + out * (0.09f * sz), kn + out * (0.05f * sz), ww * 0.9f, cb, 0.6f);
        Jt(anc, 0.040f * sz, c, 0.55f); Jt(kn, 0.030f * sz, c, 0.8f); Jt(hk, 0.024f * sz, c, 0.8f);
        Ln(kn, kn + fw * (0.10f * sz) + out * (0.04f * sz) + dn * (0.01f * sz), ww * 1.0f, c, 0.9f, 0.25f);   // knee spike
        Ln(hk, hk - fw * (0.15f * sz) + out * (0.03f * sz) + dn * (0.03f * sz), ww * 1.0f, c, 0.9f, 0.25f);   // hock spur
        for (int q = 0; q < 2; q++) {
            V3 b0 = lerp3(kn, hk, 0.30f + 0.35f * q) + out * (0.03f * sz);
            Ln(b0, b0 + out * (0.05f * sz) - fw * (0.05f * sz), ww * 0.8f, c, 0.85f, 0.25f);
        }
        DrFoot(fb, fw, out, dn, sz, ww, Pal(0.70f), 0.9f, 0.75f + 0.2f * sw2, 1.f, true);
    }
    (void)WH;
}

// point on a polyline (n points) at fraction u of its length-by-index
static V3 DrAt(const V3* F, int n, float u) {
    float x = clampf(u, 0.f, 1.f) * (float)(n - 1);
    int i = (int)x;
    if (i >= n - 1) return F[n - 1];
    return lerp3(F[i], F[i + 1], x - (float)i);
}

// one membrane panel between two finger polylines: sagging cross lines (scalloped trailing edge), fan veins
static void DrPanel(const V3* FA, const V3* FB, int n, V3 Wr, V3 flut, float sz, float ww, float u0, float al, float T, float ph) {
    const int rows = 5, SEG = 4;
    static V3 pr[2][5];   // [fan line][row]
    for (int q = 1; q <= rows; q++) {
        float u = (float)q / (float)rows;
        V3 pa = DrAt(FA, n, u), pb = DrAt(FB, n, u);
        float sag = (q == rows) ? 0.24f : 0.08f + 0.02f * (float)q;
        float fl = 0.025f * sz * sinf(T * 6.f + u * 5.f + ph) * u;
        Col c = Pal(u0 + 0.04f * u);
        V3 prev = pa;
        for (int sgi = 1; sgi <= SEG; sgi++) {
            float v = (float)sgi / (float)SEG;
            V3 p = lerp3(pa, pb, v);
            p = p + (Wr - p) * (sag * 4.f * v * (1.f - v)) + flut * (fl * 4.f * v * (1.f - v));
            Ln(prev, p, ww * (q == rows ? 1.1f : 0.6f), c, q == rows ? 0.85f : 0.30f);
            prev = p;
        }
        for (int f = 0; f < 2; f++) {
            float v = f == 0 ? 0.33f : 0.66f;
            V3 p = lerp3(pa, pb, v);
            p = p + (Wr - p) * (sag * 4.f * v * (1.f - v)) + flut * (fl * 4.f * v * (1.f - v));
            pr[f][q - 1] = p;
        }
    }
    for (int f = 0; f < 2; f++) {
        V3 prev = Wr;
        Col c = Pal(u0 + 0.06f);
        for (int q = 0; q < rows; q++) { Ln(prev, pr[f][q], ww * 0.5f, c, 0.30f); prev = pr[f][q]; }
    }
}

// bat-style wing: shoulder muscle, arm bones, thumb claw, 4 jointed fingers with tip claws, membrane panels attached to the flank
static void DrWing(V3 S, V3 out, V3 upv, V3 back, V3 rootA, V3 rootB, float sz, float ww, float ft, float T, int K, float s) {
    const float ws = 1.7f * sz;
    const Col WH = col(255.f, 255.f, 255.f);
    int Kl = K < 6 ? K : 6;
    float a1 = 0.55f * sinf(ft), a2 = 0.8f * sinf(ft - 0.7f);
    V3 E  = S + (out * cosf(a1) + upv * sinf(a1) + back * 0.35f) * (ws * 0.35f);
    V3 Wr = E + (out * cosf(a2) + upv * sinf(a2) - back * 0.25f) * (ws * 0.35f);
    Col ca = Pal(0.40f);
    DrSeg(S, E, 0.080f * sz, 0.050f * sz, 0.014f * sz, 5, Kl, ww, 0.36f, 0.42f, 0.32f, 0.26f);
    DrSeg(E, Wr, 0.050f * sz, 0.028f * sz, 0.008f * sz, 5, Kl, ww, 0.42f, 0.46f, 0.32f, 0.26f);
    Ln(S, E, ww * 1.8f, ca, 1.f); Ln(E, Wr, ww * 1.6f, ca, 1.f);
    Jt(S, 0.040f * sz, ca, 0.6f); Jt(E, 0.034f * sz, ca, 0.85f); Jt(Wr, 0.034f * sz, ca, 0.9f);
    Ln(E, E + back * (0.14f * sz) + upv * (0.05f * sz), ww * 1.0f, ca, 0.9f, 0.25f);                                      // elbow spike
    {   // thumb claw at the wrist
        V3 td = Nz(upv * 0.6f - back * 0.55f + out * 0.2f);
        DrClaw(Wr, td, -back, 0.15f * sz, ww * 1.1f, mixc(ca, WH, 0.2f), 1.f);
    }
    // fingers: 3 bone segments each; later segments lag behind in the flap and curve backwards
    static V3 F[5][4];
    for (int j = 0; j < 4; j++) {
        float be = 0.06f + j * 0.40f, fl = ws * (1.0f - 0.13f * (float)j);
        F[j][0] = Wr;
        V3 dl = out;
        for (int m = 1; m <= 3; m++) {
            float angm = be + 0.10f * (float)m;
            float em = 0.95f * sinf(ft - 1.4f - 0.30f * (float)m - 0.08f * (float)j);
            V3 dir = (out * cosf(em) + upv * sinf(em)) * cosf(angm) + back * sinf(angm);
            F[j][m] = F[j][m - 1] + dir * (fl / 3.f);
            dl = dir;
            Col cf = Pal(0.50f + 0.10f * (float)j);
            Ln(F[j][m - 1], F[j][m], ww * (j == 0 ? 1.9f : 1.5f) * (1.15f - 0.18f * (float)m), cf, 0.97f, 0.85f);
            Jt(F[j][m], 0.014f * sz, cf, m == 3 ? 0.9f : 0.65f);
        }
        DrClaw(F[j][3], Nz(dl), upv * -1.f, 0.05f * sz, ww, Pal(0.5f + 0.1f * (float)j), 0.9f);
    }
    if (g_fxOn) {          // neon ribbons + embers from the finger tips
        int ia = 26 + (s > 0.f ? 2 : 0);
        TrPush(ia, Wp(F[1][3])); TrPush(ia + 1, Wp(F[3][3]));
        TrDraw(ia, 0.030f * sz, Pal(0.50f), Pal(0.95f), 0.95f, 7.f * sz, 0.9f);
        TrDraw(ia + 1, 0.022f * sz, Pal(0.75f), Pal(0.20f), 0.8f, 6.f * sz, 0.8f);
        int n = EmitN(57 + (s > 0.f ? 1 : 0), 38.f);
        for (int q = 0; q < n; q++) {
            float rnd = g_rng.f();
            SparkAdd(Wp(F[(q & 1) ? 3 : 1][3]), RandV(1.8f, 1.8f, 0.f) + v3(0.f, 0.f, 0.6f + g_rng.f() * 1.2f), 0.5f + g_rng.f() * 0.5f, 0.012f * sz,
                     mixc(col(255.f, 170.f, 50.f), Pal(0.5f), rnd));
        }
    }
    // body attachment edge: bezier from wrist, bending through rootA, ending at rootB
    for (int m = 0; m < 4; m++) {
        float u = (float)m / 3.f;
        F[4][m] = Wr * ((1.f - u) * (1.f - u)) + rootA * (2.f * u * (1.f - u)) + rootB * (u * u);
    }
    Ln(rootA, rootB, ww * 1.3f, Pal(0.62f), 0.9f);
    Jt(rootA, 0.02f * sz, Pal(0.62f), 0.6f); Jt(rootB, 0.02f * sz, Pal(0.62f), 0.6f);
    // membrane panels
    V3 flut = upv;
    for (int j = 0; j < 4; j++) DrPanel(F[j], F[j + 1], 4, Wr, flut, sz, ww, 0.52f + 0.05f * (float)j, 0.4f, T, s * 1.3f + (float)j);
    // leading membrane between shoulder, elbow and the first finger
    Ln(E, F[0][1], ww * 0.6f, Pal(0.46f), 0.35f);
    Ln(S, lerp3(E, Wr, 0.5f), ww * 0.5f, Pal(0.44f), 0.30f);
}

static void Dragon(const Car& C, float T, int K) {
    enum { n = 44 };
    if (K > 10) K = 10;
    if (K < 6) K = 6;
    const float sz = g_size, spacing = 0.285f * sz, rmax = 0.34f * sz, ww = 0.010f * sz;
    const float TW = 2.f * PI_;
    static V3 P[48], Tn[48], R[48], U[48];
    static V3 RP[48][12];
    static float rad[48], radU[48];
    Trail(C, g_spread, T, spacing, n, P);
    const V3 up = v3(0.f, 0.f, 1.f);

    // ---- shape the spine: swim wave (grows toward the tail) + arched neck that carries the head high
    for (int i = 0; i < n; i++) {
        V3 a = P[i > 0 ? i - 1 : i], b = P[i < n - 1 ? i + 1 : i];
        Tn[i] = Nz(a - b);
    }
    for (int i = 0; i < n; i++) {
        float s = (float)i / (n - 1);
        V3 r = cross(Tn[i], up);
        r = len(r) < 1e-3f ? v3(1.f, 0.f, 0.f) : Nz(r);
        float amp = 0.12f * sz * sm(s * 5.f) * (0.4f + 0.6f * s);
        P[i] = P[i] + r * (amp * sinf(T * 2.4f - i * 0.36f));
        float neck = 1.f - sm((float)i / 13.f);
        P[i] = P[i] + up * (0.60f * sz * neck + 0.05f * sz * sinf(T * 1.7f - i * 0.2f));
    }
    // ---- final frames and body radius profile (neck -> chest -> haunch -> tapering tail)
    for (int i = 0; i < n; i++) {
        V3 a = P[i > 0 ? i - 1 : i], b = P[i < n - 1 ? i + 1 : i];
        Tn[i] = Nz(a - b);
        V3 r = cross(Tn[i], up);
        R[i] = len(r) < 1e-3f ? v3(1.f, 0.f, 0.f) : Nz(r);
        U[i] = cross(R[i], Tn[i]);
        float s = (float)i / (n - 1), prof;
        if (s < 0.30f) prof = lerpf(0.40f, 1.0f, sm((s - 0.02f) / 0.28f));
        else if (s < 0.52f) prof = 1.0f;
        else prof = powf(1.f - (s - 0.52f) / 0.48f, 1.25f) * 0.98f + 0.02f;
        prof *= 1.f + 0.10f * DrBump(s, 0.33f, 0.07f) + 0.12f * DrBump(s, 0.50f, 0.06f) + 0.03f * sinf(T * 3.f - i * 0.4f);
        rad[i] = rmax * prof + 0.006f * sz;
        radU[i] = rad[i] * 0.88f;
    }
    for (int i = 0; i < n; i++)
        for (int k = 0; k < K; k++) {
            float a = k * TW / K;
            RP[i][k] = P[i] + R[i] * (cosf(a) * rad[i]) + U[i] * (sinf(a) * radU[i]);
        }
    float kSin[12];
    for (int k = 0; k < K; k++) kSin[k] = sinf((k + 0.5f) * TW / K);

    // ---- scale skin: rings (bright on the rib cage), long lines, diagonal scale lattice, paler belly plates
    for (int i = 0; i < n; i++) {
        float s = (float)i / (n - 1);
        Col c = Pal(0.0f + 0.8f * s);
        Col cb = mixc(c, col(255.f, 235.f, 190.f), 0.45f);
        float sh = 0.75f + 0.25f * sinf(T * 3.f - i * 0.5f);           // travelling shimmer
        float rib = DrBump(s, 0.40f, 0.11f);
        for (int k = 0; k < K; k++) {
            int k2 = (k + 1) % K;
            bool belly = kSin[k] < -0.5f;
            Col cc = belly ? cb : c;
            Ln(RP[i][k], RP[i][k2], ww * 0.55f, cc, (0.22f + 0.45f * rib + (belly ? 0.25f : 0.f)) * sh);
            if (i < n - 1) {
                Ln(RP[i][k], RP[i + 1][k], ww * 0.5f, c, 0.20f * sh);
                Ln(RP[i][k], RP[i + 1][k2], ww * 0.45f, c, 0.16f * sh);
            }
        }
    }
    // ---- bones: spine, belly line, dorsal spikes
    for (int i = 0; i < n - 1; i++) {
        float s = (float)i / (n - 1);
        Col c = Pal(0.15f + 0.6f * s);
        Ln(P[i], P[i + 1], ww * 1.5f, c, 0.9f);
        V3 d0 = P[i] + U[i] * radU[i], d1 = P[i + 1] + U[i + 1] * radU[i + 1];
        Ln(d0, d1, ww * 1.2f, c, 0.9f);
        Ln(P[i] - U[i] * radU[i], P[i + 1] - U[i + 1] * radU[i + 1], ww, mixc(c, col(255.f, 235.f, 190.f), 0.5f), 0.7f);
        if (i >= 3) {
            float h = sz * (0.07f + 0.19f * sm(s * 4.f) * (1.f - sm((s - 0.30f) * 2.2f)) + 0.05f * (1.f - s));
            if (i & 1) h *= 0.78f;
            V3 tip = d0 + U[i] * h - Tn[i] * (h * 0.6f);
            Ln(d0 + Tn[i] * (spacing * 0.38f), tip, ww * 0.9f, c, 0.9f, 0.25f);
            Ln(d0 - Tn[i] * (spacing * 0.38f), tip, ww * 0.9f, c, 0.9f, 0.25f);
            Ln(d0, tip, ww * 0.7f, mixc(c, col(255.f, 255.f, 255.f), 0.4f), 0.8f, 0.3f);
            if (i % 3 == 0) Jt(tip, 0.012f * sz, c, 0.5f);
        }
    }
    // ---- leaf-shaped tail blade with veins
    {
        V3 q = P[n - 1], t = Tn[n - 1], r = R[n - 1];
        Col c = Pal(0.92f);
        V3 tipB = q - t * (0.70f * sz);
        Ln(q, tipB, ww * 1.2f, c, 0.95f);
        for (int sd = -1; sd <= 1; sd += 2) {
            float s = (float)sd;
            V3 a1 = q - t * (0.18f * sz) + r * (s * 0.10f * sz);
            V3 a2 = q - t * (0.34f * sz) + r * (s * 0.20f * sz);
            V3 a3 = q - t * (0.52f * sz) + r * (s * 0.13f * sz);
            Ln(q, a1, ww, c, 0.9f); Ln(a1, a2, ww, c, 0.9f); Ln(a2, a3, ww, c, 0.9f); Ln(a3, tipB, ww, c, 0.9f);
            Ln(a2, a2 + r * (s * 0.08f * sz) - t * (0.05f * sz), ww * 0.8f, c, 0.8f, 0.3f);
            Ln(q - t * (0.18f * sz), a2, ww * 0.5f, c, 0.45f);
            Ln(q - t * (0.34f * sz), a2, ww * 0.5f, c, 0.45f);
            Ln(q - t * (0.34f * sz), a3, ww * 0.5f, c, 0.45f);
        }
        Jt(tipB, 0.03f * sz, c, 0.8f);
        if (g_fxOn) { TrPush(30, Wp(tipB)); TrDraw(30, 0.035f * sz, Pal(0.92f), Pal(0.45f), 0.95f, 8.f * sz, 1.0f); }
    }
    // ---- head + fire
    float gate = sm((sinf(T * 0.8f) - 0.3f) * 3.f);
    {
        V3 hT = Nz(P[0] - P[2]);
        V3 hR = cross(hT, up); hR = len(hR) < 1e-3f ? v3(1.f, 0.f, 0.f) : Nz(hR);
        V3 hU = cross(hR, hT);
        float hl = 0.78f * sz, hr = 0.17f * sz;
        V3 H0 = P[0] - hT * (0.10f * hl);
        float ang = 0.10f + 0.06f * sinf(T * 2.5f) + 0.40f * gate;
        DrHead(H0, hT, hR, hU, hl, hr, ww, sz, T, ang, gate, K);
        DrFire(H0 + hT * (hl * 0.98f) - hU * (hr * 0.30f), hT, hR, hU, sz, T, gate);
        if (g_fxOn && gate > 0.25f) {          // fire breath throws sparks
            V3 mp = H0 + hT * (hl * 0.98f) - hU * (hr * 0.30f);
            int n = EmitN(56, 150.f * gate);
            for (int q = 0; q < n; q++) {
                V3 v = Wd(hT) * (5.f + g_rng.f() * 5.f) + RandV(2.f, 2.f, 2.f) + g_carV * 0.7f;
                SparkAdd(Wp(mp), v, 0.4f + g_rng.f() * 0.5f, 0.014f * sz, mixc(col(255.f, 200.f, 70.f), col(255.f, 70.f, 20.f), g_rng.f()));
            }
        }
    }
    // ---- chest glow and rising embers
    Jt(P[(int)(n * 0.33f)], 0.14f * sz, col(255.f, 150.f, 40.f), 0.35f + 0.15f * sinf(T * 3.f));
    for (int q = 0; q < 14; q++) {
        int i = (int)(DrHash(q * 7.3f) * (n - 8)) + 4;
        float ph = T * 0.35f + q * 0.211f; ph -= floorf(ph);
        V3 pos = P[i] + U[i] * radU[i] + up * (ph * 0.9f * sz) + R[i] * (sinf(T * 1.3f + q) * 0.12f * sz * ph);
        Jt(pos, 0.010f * sz, col(255.f, 190.f, 70.f), 0.8f * (1.f - ph) * sm(ph * 8.f));
    }

    // ---- legs: four muscular limbs (arms with elbow spikes + clawed hands, digitigrade haunches with hock spurs + clawed feet)
    const int iF = (int)(n * 0.30f), iB = (int)(n * 0.55f);
    const int st[2] = { iF, iB };
    for (int g = 0; g < 2; g++)
        for (int sd = -1; sd <= 1; sd += 2) {
            int i = st[g]; float s = (float)sd;
            V3 anc = P[i] + R[i] * (s * rad[i] * 0.82f) - U[i] * (radU[i] * 0.30f);
            DrLeg(anc, R[i] * s, -U[i], Tn[i], g == 0, sz, ww, T, (float)i + s * 1.7f, K, Pal(0.28f + 0.40f * (float)g));
        }
    // ---- wings: shoulder muscle, arm, thumb claw, 4 jointed fingers, membrane attached along the flank
    const int iW = (int)(n * 0.25f);
    const int iRt = (iW + 7 < n) ? iW + 7 : n - 1;
    const int iRb = (iW + 13 < n) ? iW + 13 : n - 1;
    const float ft = TW * 1.1f * T;
    for (int sd = -1; sd <= 1; sd += 2) {
        float s = (float)sd;
        V3 out = R[iW] * s, upv = U[iW], back = -Tn[iW];
        V3 S = P[iW] + out * (rad[iW] * 0.7f) + upv * (rad[iW] * 0.6f);
        V3 rootA = P[iRt] + R[iRt] * (s * rad[iRt] * 0.85f) + U[iRt] * (radU[iRt] * 0.10f);
        V3 rootB = P[iRb] + R[iRb] * (s * rad[iRb] * 0.80f) - U[iRb] * (radU[iRb] * 0.10f);
        DrWing(S, out, upv, back, rootA, rootB, sz, ww, ft, T, K, s);
    }
}

// ======================================================================== 2D font + 3D-style UI
struct Glyph { char c; uint8_t r[7]; };
static const Glyph GL[] = {
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } }, { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } }, { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } }, { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } }, { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } }, { 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } }, { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } }, { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } }, { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } }, { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } }, { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } }, { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 } }, { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } }, { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } }, { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } }, { '3', { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } }, { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } }, { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } }, { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { ':', { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 } }, { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } }, { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
    { '/', { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 } }, { '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
    { '<', { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 } }, { '>', { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 } },
    { '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } }, { ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
    { '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } }, { '_', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F } },
};
static const uint8_t* GlyphOf(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (size_t i = 0; i < sizeof(GL) / sizeof(GL[0]); i++) if (GL[i].c == c) return GL[i].r;
    return NULL;
}
static float TextW(const char* s, float px) { int n = 0; while (*s++) n++; return n > 0 ? n * 6.f * px - px : 0.f; }

static void RectV(float x, float y, float w, float h, DWORD ct, DWORD cb) {
    Tri(x, y, ct, x + w, y, ct, x, y + h, cb);
    Tri(x + w, y, ct, x + w, y + h, cb, x, y + h, cb);
}
static void RectS(float x, float y, float w, float h, DWORD c) { RectV(x, y, w, h, c, c); }
static void FrameR(float x, float y, float w, float h, float t, DWORD c) {
    RectS(x, y, w, t, c); RectS(x, y + h - t, w, t, c); RectS(x, y + t, t, h - 2 * t, c); RectS(x + w - t, y + t, t, h - 2 * t, c);
}
static void TextRaw(const char* s, float x, float y, float px, DWORD c) {
    for (; *s; s++, x += 6.f * px) {
        const uint8_t* g = GlyphOf(*s); if (!g) continue;
        for (int row = 0; row < 7; row++) {
            int bits = g[row], cl = 0;
            while (cl < 5) {
                if (bits & (0x10 >> cl)) {
                    int st = cl; while (cl < 5 && (bits & (0x10 >> cl))) cl++;
                    RectS(x + st * px, y + row * px, (cl - st) * px, px, c);
                }
                else cl++;
            }
        }
    }
}
static void Text(const char* s, float x, float y, float px, Col c, float a) {
    x = floorf(x + 0.5f); y = floorf(y + 0.5f);
    TextRaw(s, x + px, y + px, px, PK(0, 0, 0, a * 0.7f));
    TextRaw(s, x, y, px, PK(c.r, c.g, c.b, a));
}
// raised 3D button: drop shadow, gradient body, gloss, light top/left bevel, dark bottom/right bevel
static void Btn3D(float x, float y, float w, float h, float px, float hi, Col base, float press) {
    RectS(x + px * 1.5f, y + px * 1.5f, w, h, PK(0, 0, 0, 0.45f));
    y += press * px;
    Col t0 = mixc(col(62, 70, 92), col(base.r * 0.55f + 25, base.g * 0.55f + 25, base.b * 0.55f + 35), hi);
    Col b0 = mixc(col(24, 28, 40), col(base.r * 0.16f, base.g * 0.16f, base.b * 0.16f + 12), hi);
    RectV(x, y, w, h, PK(t0.r, t0.g, t0.b, 0.96f), PK(b0.r, b0.g, b0.b, 0.96f));
    RectV(x, y, w, h * 0.45f, PK(255, 255, 255, 0.16f), PK(255, 255, 255, 0.02f));
    RectS(x, y, w, px * 0.8f, PK(255, 255, 255, 0.45f));
    RectS(x, y, px * 0.8f, h, PK(255, 255, 255, 0.22f));
    RectS(x, y + h - px * 1.4f, w, px * 1.4f, PK(0, 0, 0, 0.55f));
    RectS(x + w - px * 0.8f, y, px * 0.8f, h, PK(0, 0, 0, 0.38f));
    if (hi > 0.01f) {
        for (int i = 3; i >= 1; i--) FrameR(x - px * i, y - px * i, w + 2 * px * i, h + 2 * px * i, px, PK(base.r, base.g, base.b, hi * 0.10f * (4 - i)));
        FrameR(x, y, w, h, px * 0.8f, PK(base.r, base.g, base.b, hi * 0.9f));
    }
}
static void Toggle3D(float x, float y, float px, float anim, bool on) {
    float w = 24 * px, h = 9 * px;
    Col oc = on ? col(40, 230, 150) : col(60, 66, 80);
    RectV(x, y, w, h, PK(oc.r * 0.55f, oc.g * 0.55f, oc.b * 0.55f, 1.f), PK(oc.r * 0.25f, oc.g * 0.25f, oc.b * 0.25f, 1.f));
    RectS(x, y, w, px * 0.8f, PK(0, 0, 0, 0.5f)); RectS(x, y + h - px * 0.8f, w, px * 0.8f, PK(255, 255, 255, 0.2f));
    float kw = 9 * px, kx = x + lerpf(px * 0.5f, w - kw - px * 0.5f, anim);
    RectS(kx + px * 0.8f, y + px * 1.2f, kw, h - px * 0.4f, PK(0, 0, 0, 0.4f));
    RectV(kx, y, kw, h, PK(235, 240, 250, 1.f), PK(125, 135, 160, 1.f));
    RectS(kx, y, kw, px * 0.8f, PK(255, 255, 255, 0.9f)); RectS(kx, y + h - px, kw, px, PK(0, 0, 0, 0.4f));
}
static void Slider3D(float x, float x2, float cy, float px, float frac, Col c) {
    float w = x2 - x, th = 3 * px;
    RectS(x, cy - th * 0.5f, w, th, PK(8, 10, 16, 0.95f));
    RectV(x, cy - th * 0.5f, w * frac, th, PK(c.r, c.g, c.b, 1.f), PK(c.r * 0.5f, c.g * 0.5f, c.b * 0.5f, 1.f));
    RectS(x, cy + th * 0.5f - px * 0.6f, w, px * 0.6f, PK(255, 255, 255, 0.15f));
    float kw = 5 * px, kh = 9 * px, kx = x + w * frac - kw * 0.5f;
    RectS(kx + px * 0.8f, cy - kh * 0.5f + px, kw, kh, PK(0, 0, 0, 0.45f));
    RectV(kx, cy - kh * 0.5f, kw, kh, PK(240, 244, 252, 1.f), PK(120, 130, 156, 1.f));
    RectS(kx, cy - kh * 0.5f, kw, px * 0.8f, PK(255, 255, 255, 0.9f));
}

// ---- menu state
static bool  g_menuOpen = false;
static float g_menuT = 0.f, g_hint = 12.f, g_flash = 0.f;
static int   g_sel = 0;
static int   g_gstate = 0;
static float g_hi[I_COUNT], g_kn[I_COUNT];

static void DrawHint(float px) {
    float a = clampf(g_hint, 0.f, 1.f) * (1.f - sm(g_menuT));
    if (a <= 0.01f || g_gstate < 6) return;       // not during the loading screen
    g_fade = a;
    const char* s = g_ver ? "NEON AURA   TYPE 3D" : "NEON AURA   TYPE 3D  (UNKNOWN EXE)";
    float w = TextW(s, px) + 12 * px, h = 13 * px, x = 10 * px, y = 10 * px;
    Btn3D(x, y, w, h, px, 0.f, col(0, 255, 235), 0.f);
    Text(s, x + 6 * px, y + 3 * px, px, col(200, 255, 250), 1.f);
    g_fade = 1.f;
}
static void DrawMenu(float px) {
    float e = sm(g_menuT);
    if (e <= 0.002f) { DrawHint(px); return; }
    const float pw = 190 * px, pad = 10 * px, rh = 13 * px, headH = 30 * px, footH = 30 * px;
    float ph = headH + I_COUNT * rh + footH + 6 * px;
    float x0 = lerpf(-pw - 20 * px, 14 * px, e), y0 = (g_sh - ph) * 0.5f; if (y0 < 4.f) y0 = 4.f;
    g_fade = e;
    Col acc = col(0, 255, 235);
    // panel
    RectS(x0 + 3 * px, y0 + 3 * px, pw, ph, PK(0, 0, 0, 0.4f));
    RectV(x0, y0, pw, ph, PK(14, 20, 34, 0.90f), PK(5, 7, 13, 0.92f));
    for (int i = 3; i >= 1; i--) FrameR(x0 - px * i, y0 - px * i, pw + 2 * px * i, ph + 2 * px * i, px, PK(acc.r, acc.g, acc.b, 0.05f * (4 - i)));
    FrameR(x0, y0, pw, ph, px * 0.8f, PK(acc.r, acc.g, acc.b, 0.75f));
    // header
    RectV(x0, y0, pw, headH, PK(0, 90, 100, 0.55f), PK(0, 20, 30, 0.1f));
    Text("NEON AURA", x0 + pad, y0 + 4 * px, px * 2.f, col(180, 255, 250), 1.f);
    Text("NEON BEASTS  V3", x0 + pad, y0 + 20 * px, px, col(120, 170, 190), 1.f);
    RectS(x0 + px, y0 + headH - px, pw - 2 * px, px * 0.8f, PK(acc.r, acc.g, acc.b, 0.25f));
    float sx = x0 + pad + (0.5f + 0.5f * sinf(g_uiT * 2.f)) * (pw - 2 * pad - 40 * px);
    RectS(sx, y0 + headH - px * 1.4f, 40 * px, px * 1.4f, PK(255, 255, 255, 0.9f));
    // rows
    for (int i = 0; i < I_COUNT; i++) {
        float hi = g_hi[i];
        float ry = y0 + headH + 3 * px + i * rh;
        float bx = x0 + pad * 0.5f - hi * px, by = ry - hi * px * 0.5f, bw = pw - pad + hi * 2 * px, bh = rh - 2 * px;
        Col base = (i == I_RESET) ? col(255, 80, 90) : acc;
        float press = (i == I_RESET && g_flash > 0.f) ? 1.f : 0.f;
        Btn3D(bx, by, bw, bh, px, hi, base, press);
        float ty = by + (bh - 7 * px) * 0.5f + press * px;
        Col tc = mixc(col(185, 195, 215), col(255, 255, 255), hi);
        float rx = x0 + pw - pad - 4 * px;     // right edge for values
        if (i == I_RESET) {
            Text(LB[i], x0 + (pw - TextW(LB[i], px)) * 0.5f, ty, px, tc, 1.f);
        }
        else {
            Text(LB[i], x0 + pad + 2 * px, ty, px, tc, 1.f);
            if (i <= I_FX) {
                Toggle3D(rx - 24 * px, by + (bh - 9 * px) * 0.5f, px, g_kn[i], g_tg[i]);
            }
            else if (i == I_COLOR || i == I_DENS) {
                char buf[40]; sprintf(buf, "< %s >", i == I_COLOR ? COLN[g_col] : DENN[g_dens]);
                Col vc = (i == I_COLOR && g_col != 5) ? Pal(0.f) : (i == I_COLOR ? Pal(g_uiT * 0.2f) : col(255, 220, 120));
                Text(buf, rx - TextW(buf, px), ty, px, vc, 1.f);
            }
            else {
                char buf[24]; SliderText(i, buf);
                float lo, hi2, st; SliderRange(i, lo, hi2, st);
                float frac = clampf((*SliderPtr(i) - lo) / (hi2 - lo), 0.f, 1.f);
                float tw = TextW(buf, px);
                Text(buf, rx - tw, ty, px, col(255, 220, 120), 1.f);
                Slider3D(x0 + pw * 0.47f, rx - tw - 7 * px, by + bh * 0.5f, px, frac, acc);
            }
        }
    }
    // footer
    float fy = y0 + ph - footH + 2 * px;
    Col fc = col(140, 160, 185);
    Text("UP/DOWN: SELECT", x0 + pad, fy, px, fc, 1.f);
    Text("LEFT/RIGHT: CHANGE", x0 + pad, fy + 9 * px, px, fc, 1.f);
    Text("ENTER: TOGGLE   3D: CLOSE", x0 + pad, fy + 18 * px, px, fc, 1.f);
    g_fade = 1.f;
}

// ======================================================================== game access (GTA SA 1.0 US)
typedef void* (__cdecl* FnPlayerVeh)(int, bool);
static FnPlayerVeh FindPlayerVehicle = (FnPlayerVeh)0x56E0D0;

static bool GetXform(void* e, V3& pos, V3& right, V3& fwd, V3& up) {
    float* m = *(float**)((char*)e + 0x14);
    if (!m) return false;
    right = v3(m[0], m[1], m[2]); fwd = v3(m[4], m[5], m[6]); up = v3(m[8], m[9], m[10]); pos = v3(m[12], m[13], m[14]);
    return true;
}
// reads the vehicle model's collision bounding box; falls back to a normal car size if anything looks wrong
static bool ReadBounds(void* ent, float* mn, float* mx) {
    __try {
        uint16_t mi = *(uint16_t*)((char*)ent + 0x22);
        if (mi >= 20000) return false;
        char** tbl = (char**)0xA9B0C8; char* mdl = tbl[mi]; if (!mdl) return false;
        char* cm = *(char**)(mdl + 0x14); if (!cm) return false;
        float* b = (float*)cm;
        for (int i = 0; i < 3; i++) { mn[i] = b[i]; mx[i] = b[3 + i]; }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void MakeCar(Car& B, void* ent, V3 pos, V3 r, V3 f, V3 u) {
    float mn[3] = { -0.95f, -2.3f, -0.6f }, mx[3] = { 0.95f, 2.3f, 0.85f };
    float tmn[3], tmx[3];
    if (ReadBounds(ent, tmn, tmx)) {
        float w = tmx[0] - tmn[0], l = tmx[1] - tmn[1], h = tmx[2] - tmn[2];
        if (tmn[0] < -0.2f && tmx[0] > 0.2f && tmn[1] < -0.2f && tmx[1] > 0.2f && w > 0.7f && w < 8.f && l > 1.1f && l < 30.f && h > 0.5f && h < 10.f)
            for (int i = 0; i < 3; i++) { mn[i] = tmn[i]; mx[i] = tmx[i]; }
    }
    B.o = pos; B.r = r; B.f = f; B.u = u;
    B.x0 = mn[0]; B.x1 = mx[0]; B.y0 = mn[1]; B.y1 = mx[1]; B.z0 = mn[2]; B.z1 = mx[2];
    B.W = mx[0] - mn[0]; B.L = mx[1] - mn[1]; B.H = mx[2] - mn[2];
}

// ======================================================================== frame
static LARGE_INTEGER g_freq, g_last;
static bool g_first = true;

static bool Edge(int vk, bool& prev) { bool d = (GetAsyncKeyState(vk) & 0x8000) != 0; bool r = d && !prev; prev = d; return r; }
// "cheat code" style typing detector: type 3 then D within 2 s
static char  g_typed[8];
static bool  g_kp[256];
static float g_typeT = 0.f;
static bool PollTyped(float dt) {
    g_typeT += dt;
    if (g_typeT > 2.0f) memset(g_typed, 0, sizeof(g_typed));
    bool hit = false;
    for (int vk = 0x30; vk <= 0x69; vk++) {
        char ch;
        if (vk >= 0x30 && vk <= 0x39) ch = (char)vk;                 // top row 0-9
        else if (vk >= 0x41 && vk <= 0x5A) ch = (char)vk;            // A-Z
        else if (vk >= 0x60 && vk <= 0x69) ch = (char)('0' + (vk - 0x60)); // numpad 0-9
        else continue;
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (down && !g_kp[vk]) {
            memmove(g_typed, g_typed + 1, 6); g_typed[6] = ch; g_typed[7] = 0; g_typeT = 0.f;
            if (g_typed[5] == '3' && g_typed[6] == 'D') { hit = true; memset(g_typed, 0, sizeof(g_typed)); }
        }
        g_kp[vk] = down;
    }
    return hit;
}
struct KS { bool down; float t; };
static bool Rep(int vk, KS& k, float dt, bool repeat) {
    bool d = (GetAsyncKeyState(vk) & 0x8000) != 0, fire = false;
    if (d) {
        if (!k.down) { fire = true; k.t = 0.35f; }
        else if (repeat) { k.t -= dt; if (k.t <= 0.f) { fire = true; k.t = 0.06f; } }
    }
    k.down = d; return fire;
}

static void SetStates(IDirect3DDevice9* d) {
    d->SetPixelShader(NULL); d->SetVertexShader(NULL); d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    d->SetTexture(0, NULL);
    d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
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

static int g_excLogged = 0, g_fxFaults = 0;
static int FaultFilter(EXCEPTION_POINTERS* ep, const char* where) {
    if (g_excLogged < 12) {
        g_excLogged++;
        Log("EXCEPTION in %s  code=0x%08lX  addr=%p", where, (unsigned long)ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
// vehicle effects live in their own __try so a crash here can never kill the menu
static float g_gait = 0.f;
static void DrawBeasts(const Car& C, float T) {
    const int K = 6 + 2 * g_dens;
    const float sp = g_spread, sz = g_size, hx = C.W * 0.5f, zt = C.z1, zg = C.z0, L = C.L;
    if (g_tg[I_EAGLE])                      // two eagles flying above and behind the car
        for (int e = 0; e < 2; e++) {
            float s = e ? 1.f : -1.f;
            V3 P = v3(s * (hx + 0.2f + 0.6f * sp) + 0.25f * sinf(T * 0.6f + e), -L * 0.30f - 1.2f * sp - e * 1.2f,
                      zt + 1.8f * sp + 0.35f * sinf(T * 0.9f + e * 2.f));
            if (Begin(C, P, 0.10f * sinf(T * 0.5f + e), 0.05f * sinf(T * 0.8f + e), 0.18f * sinf(T * 0.7f + e * 1.5f), sz, 1.8f, 0.52f + 0.08f * e)) {
                g_tid = e * 4; g_eid = 48 + e * 2;
                Bird(BP_EAGLE, T, e * 0.37f, g_dens);
            }
        }
    if (g_tg[I_PHOENIX]) {
        V3 P = v3(0.f, -L * 0.5f - 2.6f * sp, zt + 2.8f * sp + 0.4f * sinf(T * 0.7f));
        if (Begin(C, P, 0.f, 0.04f * sinf(T * 0.6f), 0.12f * sinf(T * 0.5f), sz, 3.5f, 0.05f)) {
            g_tid = 8; g_eid = 52;
            Bird(BP_PHX, T, 0.f, g_dens);
        }
    }
    if (g_tg[I_WOLF])                       // two wolves running beside the car (they glitch now and then)
        for (int w = 0; w < 2; w++) {
            float s = w ? 1.f : -1.f;
            V3 P = v3(s * (hx + 1.3f * sp), (w ? -0.15f : 0.12f) * L, zg);
            if (Begin(C, P, 0.f, 0.f, 0.f, sz, 1.5f, 0.60f + 0.05f * w)) {
                g_tid = 14 + w * 2; g_tid2 = 18 + w; g_eid = 32 + w * 4;
                float gph = g_gait + 0.33f * w;
                if (g_fxOn) {
                    g_gl = GlitchAmt(T, 1.7f * (float)w + 0.3f);
                    g_glStep = floorf(T * 22.f + (float)w * 5.f);
                    if (g_gl > 0.f) {
                        gph = floorf(gph * 6.f) / 6.f;                                    // stuttering animation
                        if (Gh(g_glStep * 2.3f + (float)w) > 0.62f) g_dfade *= 0.45f;      // flicker
                    }
                }
                Quad(QP_WOLF, gph, T, K);
                g_gl = 0.f;
            }
        }
    if (g_tg[I_JAG])                        // two jaguars, a little further out
        for (int w = 0; w < 2; w++) {
            float s = w ? 1.f : -1.f;
            V3 P = v3(s * (hx + 2.6f * sp), (w ? 0.28f : -0.25f) * L, zg);
            if (Begin(C, P, 0.f, 0.f, 0.f, sz, 1.6f, 0.12f + 0.04f * w)) {
                g_tid = 20 + w * 2; g_tid2 = 24 + w; g_eid = 40 + w * 4;
                Quad(QP_JAG, g_gait + 0.17f + 0.4f * w, T, K);
            }
        }
    if (g_tg[I_DRAGON])                     // dragon flying random loops around the car
        if (Begin(C, v3(0.f, 0.f, 0.f), 0.f, 0.f, 0.f, 1.f, 8.f, 0.85f))
            Dragon(C, T, K);
}
static void DrawVehicleFX(void* ent, float dtE) {
    __try {
        V3 pos, r, f, u;
        if (GetXform(ent, pos, r, f, u)) {
            Car C; MakeCar(C, ent, pos, r, f, u);
            float* ms = (float*)((char*)ent + 0x44);
            V3 vel = v3(ms[0], ms[1], ms[2]) * 50.f;
            if (!(len(vel) < 300.f)) vel = v3(0.f, 0.f, 0.f);
            g_carV = vel;
            float fs = dot(vel, f); if (!(fs > -300.f && fs < 300.f)) fs = 0.f;
            float hz = clampf(0.9f + fabsf(fs) * 0.11f, 0.9f, 3.4f);     // run faster when the car is faster
            g_gait += hz * dtE * g_spd; g_gait -= floorf(g_gait);
            g_gain = g_int;
            g_dtE = dtE; g_densK = g_dens == 0 ? 0.6f : (g_dens == 1 ? 1.f : 1.6f);
            // ---- occlusion box: the car body hides every animal that is behind it
            g_oc = C.o; g_or = C.r; g_of = C.f; g_ou = C.u;
            {
                float sx_ = C.W * 0.04f, sy_ = C.L * 0.03f;
                g_ob[0] = C.x0 + sx_; g_ob[1] = C.x1 - sx_; g_ob[2] = C.y0 + sy_; g_ob[3] = C.y1 - sy_;
                g_ob[4] = C.z0 - 0.15f; g_ob[5] = C.z1 - 0.05f * C.H;
            }
            V3 dc = g_cp - C.o; g_camL = v3(dot(dc, C.r), dot(dc, C.f), dot(dc, C.u));
            bool inside = g_camL.x > g_ob[0] - 0.3f && g_camL.x < g_ob[1] + 0.3f && g_camL.y > g_ob[2] - 0.3f && g_camL.y < g_ob[3] + 0.3f &&
                          g_camL.z > g_ob[4] - 0.3f && g_camL.z < g_ob[5] + 0.3f;
            g_occOn = !inside;                                            // camera inside the car (hood / first person): no hiding
            DrawBeasts(C, g_T * g_spd);
            if (g_fxOn) { SparksRun(dtE); SparksDraw(); }
        }
    }
    __except (FaultFilter(GetExceptionInformation(), "vehicle fx")) { g_fxFaults++; g_n = 0; g_nmf = 0; }
    g_gain = 1.f; g_occOn = false; g_gl = 0.f;
}

// ---- the animals stay with the LAST vehicle the player drove, also after getting out, until another car is entered
static void*   g_veh = NULL;
static uint8_t g_vehFlag = 0;
static int     g_vehMi = -1;
// 1 = slot holds a live vehicle (flag byte returned), 0 = slot is free, -1 = pool layout not recognised
static int VehSlot(void* e, uint8_t& fl) {
    __try {
        char* pool = *(char**)0xB74494;                       // CPools::ms_pVehiclePool
        if (!pool) return -1;
        char* objs = *(char**)pool; uint8_t* flg = *(uint8_t**)(pool + 4); int size = *(int*)(pool + 8);
        if (!objs || !flg || size <= 0 || size > 5000) return -1;
        INT_PTR off = (char*)e - objs;
        if (off < 0 || (off % 0xA18) != 0 || (off / 0xA18) >= size) return -1;
        fl = flg[off / 0xA18];
        return (fl & 0x80) ? 0 : 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static bool VehSoftOk(void* e) {
    __try {
        float* m = *(float**)((char*)e + 0x14);
        if (!m || (uintptr_t)m < 0x10000) return false;
        float lr = len(v3(m[0], m[1], m[2])), lf = len(v3(m[4], m[5], m[6]));
        if (!(lr > 0.9f && lr < 1.1f && lf > 0.9f && lf < 1.1f)) return false;
        return *(uint16_t*)((char*)e + 0x22) == (uint16_t)g_vehMi;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void* TrackedVehicle(bool playing, int gstate) {
    if (gstate != 9) { g_veh = NULL; return NULL; }              // loading / main menu: forget the car
    if (!playing) return NULL;                                   // pause menu: keep it but draw nothing
    void* pv = FindPlayerVehicle(-1, false);
    if (pv) {
        if (pv != g_veh) {
            g_veh = pv; g_vehFlag = 0;
            uint8_t fl = 0; if (VehSlot(pv, fl) == 1) g_vehFlag = fl;
            __try { g_vehMi = *(uint16_t*)((char*)pv + 0x22); } __except (EXCEPTION_EXECUTE_HANDLER) { g_vehMi = -1; }
        }
        return pv;
    }
    if (g_veh) {                                                 // on foot: keep using the last car while it still exists
        uint8_t fl = 0; int st = VehSlot(g_veh, fl);
        bool ok = (st == 1 && fl == g_vehFlag) || (st == -1 && VehSoftOk(g_veh));
        if (ok) return g_veh;
        g_veh = NULL;
    }
    return NULL;
}

// ---- camera fix: read the camera the game REALLY renders with (RenderWare camera frame, set right before the
// 3D scene is drawn) instead of only CCamera's placeable matrix, which can be a frame behind while the mouse turns.
// With a one-frame lag every creature slides sideways whenever the camera rotates, which looks like it follows the mouse.
// Everything is validated against the old camera matrix; if anything looks wrong we silently keep the old one.
static int   g_camSrc = -1;      // 0 = TheCamera matrix, 1 = RenderWare camera frame
static float g_rwVW = 0.f;       // RwCamera view window half-width (tan of half horizontal fov)
static bool ReadRwCam() {
    __try {
        char* cam = *(char**)0xC1703C;                    // Scene.m_pRwCamera
        if (!cam) return false;
        char* fr = *(char**)(cam + 0x04);                 // camera's RwFrame
        if (!fr) return false;
        const float* l = (const float*)(fr + 0x50);       // frame LTM (world matrix)
        V3 A = v3(l[8], l[9], l[10]), B = v3(l[4], l[5], l[6]), P = v3(l[12], l[13], l[14]);
        float la = len(A), lb = len(B);
        if (!(la > 0.9f && la < 1.1f && lb > 0.9f && lb < 1.1f)) return false;
        if (fabsf(dot(A, B)) > 0.1f * la * lb) return false;
        if (!(len(P - g_cp) < 8.f)) return false;
        V3 f, u;
        if (dot(A, g_cf) > 0.7f * la && dot(B, g_cu) > 0.5f * lb) { f = A; u = B; }
        else if (dot(B, g_cf) > 0.7f * lb && dot(A, g_cu) > 0.5f * la) { f = B; u = A; }
        else return false;
        f = Nz(f); u = Nz(u);
        g_cf = f; g_cu = u; g_cr = Nz(cross(f, u)); g_cp = P;
        g_rwVW = *(float*)(cam + 0x68);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void Render(IDirect3DDevice9* d) {
    // ---- timing
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    float dtRaw = (float)((double)(now.QuadPart - g_last.QuadPart) / (double)g_freq.QuadPart);
    if (!g_first && dtRaw < 0.0005f) return;      // EndScene called twice in one frame
    g_first = false; g_last = now;
    float dt = clampf(dtRaw, 0.f, 0.1f);

    // ---- game state
    int gstate = *(volatile int*)0xC8D4C0;                   // gGameState (9 = playing)
    bool fe = *(volatile uint8_t*)0xBA67A4 != 0;             // front-end (pause) menu active
    g_gstate = gstate;
    static int lastGs = -999, lastFe = -1;
    if (gstate != lastGs || (int)fe != lastFe) { Log("state change: gameState=%d frontEnd=%d menuOpen=%d", gstate, (int)fe, (int)g_menuOpen); lastGs = gstate; lastFe = (int)fe; }
    static float hb = 0.f; hb += dt;
    if (hb > 20.f) { hb = 0.f; Log("alive: gameState=%d frontEnd=%d menuOpen=%d fxFaults=%d", gstate, (int)fe, (int)g_menuOpen, g_fxFaults); }
    // menu works everywhere (also main menu); vehicle effects only while really playing
    bool playing = g_force || (gstate == 9 && !fe);
    bool paused = (*(volatile uint8_t*)0xB7CB49) || (*(volatile uint8_t*)0xB7CB48);
    float dtE = paused ? 0.f : dt;

    // ---- input (only when the game window is focused)
    static bool kF11 = false;
    static KS kUp = { false, 0 }, kDn = { false, 0 }, kLf = { false, 0 }, kRt = { false, 0 }, kEn = { false, 0 };
    DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid == GetCurrentProcessId()) {
        if (PollTyped(dt)) { g_menuOpen = !g_menuOpen; if (!g_menuOpen) g_hint = 3.f; Log("menu %s (typed 3D)", g_menuOpen ? "OPEN" : "CLOSED"); }
        if (Edge(VK_F11, kF11)) g_force = !g_force;
        if (g_menuOpen) {
            if (Rep(VK_UP, kUp, dt, false)) g_sel = (g_sel + I_COUNT - 1) % I_COUNT;
            if (Rep(VK_DOWN, kDn, dt, false)) g_sel = (g_sel + 1) % I_COUNT;
            if (Rep(VK_LEFT, kLf, dt, true)) Adjust(g_sel, -1);
            if (Rep(VK_RIGHT, kRt, dt, true)) Adjust(g_sel, +1);
            if (Rep(VK_RETURN, kEn, dt, false)) { Adjust(g_sel, 0); if (g_sel == I_RESET) g_flash = 0.15f; }
        }
    }
    // ---- ui animation
    g_uiT += dt;
    g_menuT = clampf(g_menuT + (g_menuOpen ? 1.f : -1.f) * dt * 5.f, 0.f, 1.f);
    if (!g_menuOpen && g_hint > 0.f && gstate >= 6) g_hint -= dt;
    if (g_flash > 0.f) g_flash -= dt;
    for (int i = 0; i < I_COUNT; i++) {
        g_hi[i] = lerpf(g_hi[i], (g_menuOpen && i == g_sel) ? 1.f : 0.f, clampf(dt * 14.f, 0.f, 1.f));
        if (i <= I_FX) g_kn[i] = lerpf(g_kn[i], g_tg[i] ? 1.f : 0.f, clampf(dt * 16.f, 0.f, 1.f));
    }
    g_T += dtE;

    // ---- camera
    float* cm = *(float**)((char*)0xB6F028 + 0x14);
    bool camOk = (cm != NULL);
    float fov = 70.f;
    if (camOk) {
        g_cr = v3(cm[0], cm[1], cm[2]); g_cf = v3(cm[4], cm[5], cm[6]); g_cu = v3(cm[8], cm[9], cm[10]); g_cp = v3(cm[12], cm[13], cm[14]);
        fov = *(float*)0x8D5038; if (fov < 20.f || fov > 130.f) fov = 70.f;
    }
    int camSrc = 0; g_rwVW = 0.f;
    if (camOk && ReadRwCam()) camSrc = 1;
    if (camSrc != g_camSrc) { g_camSrc = camSrc; Log("camera source: %s", camSrc ? "RenderWare camera frame" : "TheCamera matrix (fallback)"); }
    D3DVIEWPORT9 vp; d->GetViewport(&vp);
    g_sw = (float)vp.Width; g_sh = (float)vp.Height;
    g_focal = (g_sw * 0.5f) / tanf(fov * PI_ / 360.f);
    if (camSrc == 1) {     // use the game's real projection when it matches the fov-based estimate
        float est = tanf(fov * PI_ / 360.f);
        if (g_rwVW > 0.2f && g_rwVW < 3.f && g_rwVW > est * 0.65f && g_rwVW < est * 1.35f) g_focal = (g_sw * 0.5f) / g_rwVW;
    }

    g_dev = d; g_n = 0; g_fade = 1.f; g_gain = 1.f;
    SetStates(d);

    // ---- vehicle effects (additive glow)
    g_fxOn = g_tg[I_FX];
    void* ent = (camOk && g_fxFaults < 30) ? TrackedVehicle(playing, gstate) : NULL;
    if (g_tg[0] && ent) DrawVehicleFX(ent, dtE);

    // ---- menu (normal alpha blending)
    SetBlend(false);
    float px = clampf(floorf(g_sh / 540.f + 0.5f), 2.f, 4.f);
    DrawMenu(px);
    Flush();
}

// ======================================================================== hook
typedef HRESULT(WINAPI* EndScene_t)(IDirect3DDevice9*);
typedef HRESULT(WINAPI* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static EndScene_t volatile oEndScene = NULL;
static Reset_t oReset = NULL;
static IDirect3DStateBlock9* g_sb = NULL;
static int g_faults = 0;
static bool g_dead = false;

static void SafeRender(IDirect3DDevice9* d) {
    __try { Render(d); }
    __except (FaultFilter(GetExceptionInformation(), "Render")) { g_faults++; g_n = 0; if (g_faults > 300) { g_dead = true; Log("too many faults, overlay disabled"); } }
}
static volatile DWORD g_lastCall = 0;      // tick of the last time our EndScene hook ran
static HRESULT WINAPI hkEndScene(IDirect3DDevice9* d) {
    g_lastCall = GetTickCount();
    if (!g_dead) {
        IDirect3DSurface9 *rt = NULL, *bb = NULL; bool ok = false;
        if (SUCCEEDED(d->GetRenderTarget(0, &rt)) && SUCCEEDED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) ok = (rt == bb);
        if (rt) rt->Release();
        if (bb) bb->Release();
        if (ok) {
            // state block is created and released every frame, so nothing of ours can ever block a device Reset
            IDirect3DStateBlock9* sb = NULL;
            if (SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL, &sb)) && sb) {
                sb->Capture(); SafeRender(d); sb->Apply(); sb->Release();
            }
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
static bool IsUS10() {
    __try { return *(volatile uint32_t*)0x82457C == 0x94BF; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool HookDevice(IDirect3DDevice9* dev) {
    void** vt = *(void***)dev;
    if (vt[42] == (void*)hkEndScene) return true;
    oEndScene = (EndScene_t)vt[42]; oReset = (Reset_t)vt[16];
    return PatchVT(&vt[16], (void*)hkReset) && PatchVT(&vt[42], (void*)hkEndScene);
}
// fallback: build a throw-away device just to read the shared D3D9 vtable
static bool HookViaDummy() {
    HMODULE h = LoadLibraryA("d3d9.dll");
    if (!h) return false;
    typedef IDirect3D9* (WINAPI * DC)(UINT);
    DC dc = (DC)GetProcAddress(h, "Direct3DCreate9");
    if (!dc) return false;
    IDirect3D9* d3d = dc(D3D_SDK_VERSION);
    if (!d3d) return false;
    WNDCLASSA wc; ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "NeonAuraDummy";
    RegisterClassA(&wc);
    HWND hw = CreateWindowA("NeonAuraDummy", "", WS_OVERLAPPED, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    D3DPRESENT_PARAMETERS pp; ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = hw; pp.BackBufferFormat = D3DFMT_UNKNOWN;
    IDirect3DDevice9* dev = NULL;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hw, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT, &pp, &dev);
    bool ok = false;
    if (SUCCEEDED(hr) && dev) { ok = HookDevice(dev); dev->Release(); }
    else Log("dummy CreateDevice failed hr=0x%08lX", (unsigned long)hr);
    d3d->Release();
    if (hw) DestroyWindow(hw);
    return ok;
}
static DWORD WINAPI InitThread(LPVOID) {
    ResetDefaults();
    QueryPerformanceFrequency(&g_freq); QueryPerformanceCounter(&g_last);
    g_ver = IsUS10();
    Log("---- NeonAura v3 (occlusion + persistent car + neon fx) loaded.  GTA SA 1.0 US exe detected: %s", g_ver ? "yes" : "NO (addresses may be wrong)");
    bool hooked = false;
    if (g_ver) {                                   // preferred: patch the game's real device (works with wrappers too)
        for (int i = 0; i < 1200 && !hooked; i++) {
            IDirect3DDevice9* gd = NULL;
            __try { gd = *(IDirect3DDevice9**)0xC97C28; }
            __except (EXCEPTION_EXECUTE_HANDLER) { gd = NULL; }
            if (gd) { hooked = HookDevice(gd); Log("hooked game device %p -> %s", (void*)gd, hooked ? "ok" : "FAILED"); }
            else Sleep(50);
        }
    }
    if (!hooked) { hooked = HookViaDummy(); Log("hook via dummy device -> %s", hooked ? "ok" : "FAILED"); }
    // watchdog: if something else overwrites our vtable slot AND our hook stops being called, take it back
    if (hooked && g_ver) {
        void** hookedVt = NULL; int lastOk = -1;
        for (;;) {
            Sleep(250);
            __try {
                IDirect3DDevice9* gd = *(IDirect3DDevice9**)0xC97C28;
                if (gd) {
                    void** vt = *(void***)gd;
                    if (!hookedVt) hookedVt = vt;
                    int ok = (vt[42] == (void*)hkEndScene) ? 1 : 0;
                    if (ok != lastOk) {
                        char who[MAX_PATH] = "?"; HMODULE hm = NULL;
                        if (!ok && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)vt[42], &hm) && hm)
                            GetModuleFileNameA(hm, who, MAX_PATH);
                        Log("watchdog: device %p EndScene hook %s  (slot now points into: %s)", (void*)gd, ok ? "intact" : "NOT ACTIVE (overwritten)", ok ? "-" : who);
                        lastOk = ok;
                    }
                    if (!ok) {
                        bool newVt = (vt != hookedVt);
                        bool stale = (GetTickCount() - g_lastCall) > 1000;   // nobody calls us any more
                        if (newVt || stale) {
                            hookedVt = vt;
                            // chain through whatever sits in the slot now (their hook calls the real one), so both keep working
                            void* cur = vt[42];
                            if (cur != (void*)hkEndScene) oEndScene = (EndScene_t)cur;
                            void* curR = vt[16];
                            if (curR != (void*)hkReset) oReset = (Reset_t)curR;
                            bool r = PatchVT(&vt[16], (void*)hkReset) && PatchVT(&vt[42], (void*)hkEndScene);
                            Log("watchdog: hook lost (%s) -> re-hook %s", newVt ? "new vtable" : "slot overwritten", r ? "ok" : "FAILED");
                            g_lastCall = GetTickCount();
                            lastOk = -1;
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        ResetDefaults();
        CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
    }
    return TRUE;
}
