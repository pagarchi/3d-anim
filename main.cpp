// NeonAura v2  -  GTA San Andreas 1.0 US  (.asi plugin, Direct3D9 overlay, no textures)   [dragon complete - part 1 + 2]
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
    I_MASTER, I_EAGLE, I_DRAGON, I_WOLF, I_JAG, I_PHOENIX,   // 0..5 = toggles
    I_COLOR, I_INT, I_SIZE, I_SPD, I_SPREAD, I_DENS, I_RESET, I_COUNT
};
static const char* LB[I_COUNT] = { "MASTER SWITCH", "EAGLE PAIR", "DRAGON", "WOLF PAIR", "JAGUAR PAIR", "PHOENIX",
                                   "COLOR", "INTENSITY", "SIZE", "SPEED", "SPREAD", "DETAIL", "RESET DEFAULTS" };
static const char* COLN[7] = { "CYAN", "MAGENTA", "GOLD", "GREEN", "RED", "RAINBOW", "MIXED" };
static const char* DENN[3] = { "LOW", "MEDIUM", "HIGH" };

static bool  g_tg[6];
static int   g_col, g_dens;
static float g_int, g_size, g_spd, g_spread;
static bool  g_force = false;
static bool  g_ver = false;      // exe looks like GTA SA 1.0 US

static void ResetDefaults() {
    for (int i = 0; i < 6; i++) g_tg[i] = false;
    g_tg[I_MASTER] = g_tg[I_EAGLE] = g_tg[I_DRAGON] = g_tg[I_WOLF] = g_tg[I_JAG] = g_tg[I_PHOENIX] = true;
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
    if (idx <= I_PHOENIX) { g_tg[idx] = dir == 0 ? !g_tg[idx] : (dir > 0); return; }
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
// glowing neon segment between two world points (3 layers: wide halo, mid glow, white-hot core)
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
    if (z > 160.f) return;
    float w = ww * g_focal / z;
    float wg = clampf(w * 3.2f + 2.f, 2.f, 60.f), wm = clampf(w * 1.5f + 1.f, 1.f, 30.f), wc = clampf(w * 0.6f + 0.9f, 0.9f, 14.f);
    SegS(ax, ay, bx, by, wg, PK(c.r, c.g, c.b, a0 * 0.16f), PK(c.r, c.g, c.b, a1 * 0.16f));
    SegS(ax, ay, bx, by, wm, PK(c.r, c.g, c.b, a0 * 0.42f), PK(c.r, c.g, c.b, a1 * 0.42f));
    Col h = mixc(c, col(255, 255, 255), 0.65f);
    SegS(ax, ay, bx, by, wc, PK(h.r, h.g, h.b, a0), PK(h.r, h.g, h.b, a1));
}
// soft glowing orb
static void Orb(V3 p, float rw, Col c, float a) {
    float sx, sy, z;
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

static inline V3 Wp(V3 l) { return X.o + (X.r * l.x + X.f * l.y + X.u * l.z) * X.s; }
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

// ======================================================================== quadruped (wolf / jaguar)
struct QuadP {
    float len, hs, a, b, m, chest, depth, stride, lift, tail, tailUp, neckL, neckA, headL, hr, headDown, ear, flex, bob;
};
//                               len   hs    a     b     m     chest depth stride lift  tail  tUp   neckL neckA headL hr    hDown ear   flex  bob
static const QuadP QP_WOLF = { 1.25f,0.68f,0.30f,0.30f,0.18f,0.14f,0.17f,0.55f, 0.18f,0.55f,0.00f,0.38f,0.75f,0.34f,0.09f,0.30f,0.12f,0.05f,0.03f };
static const QuadP QP_JAG  = { 1.35f,0.62f,0.28f,0.28f,0.16f,0.17f,0.19f,0.60f, 0.20f,0.95f,0.15f,0.30f,0.55f,0.30f,0.12f,0.25f,0.07f,0.10f,0.05f };

static void Quad(const QuadP& Q, float gph, float T, int K) {
    const float TW = 2.f * PI_, L = Q.len;
    const float ww = 0.014f, wr = 0.008f, ws = 0.005f;      // bone / rib / skin widths
    const float bob = Q.bob * sinf(TW * gph * 2.f);
    const int NB = 8;
    V3 sp[NB], ct[NB], RR[NB], UU[NB];
    float rx[NB], rz[NB];
    for (int k = 0; k < NB; k++) {
        float t = (float)k / (NB - 1);
        rx[k] = Q.chest * (0.62f + 0.38f * sinf(PI_ * (0.12f + 0.8f * t)));
        rz[k] = Q.depth * (0.55f + 0.45f * sm(t * 1.5f));
        sp[k] = v3(0.f, lerpf(-L * 0.5f, L * 0.5f, t), Q.hs + bob + Q.flex * sinf(TW * gph - t * 2.4f));
        ct[k] = v3(sp[k].x, sp[k].y, sp[k].z - rz[k] * 0.85f);
        RR[k] = v3(1.f, 0.f, 0.f); UU[k] = v3(0.f, 0.f, 1.f);
    }
    // body: faint skin + bright rib rings + spine with vertebra spikes
    Tube(ct, RR, UU, rx, rz, NB, K, ws, 0.30f, 0.70f, 0.16f, 0.11f);
    for (int k = 2; k <= 6; k++) RingL(ct[k], RR[k], UU[k], rx[k], rz[k], 12, wr, Pal(0.40f + 0.05f * k), 0.55f);
    for (int k = 0; k < NB; k++) {
        if (k < NB - 1) Ln(sp[k], sp[k + 1], ww, Pal(0.5f), 0.95f);
        Ln(sp[k], sp[k] + v3(0.f, -0.02f, 0.045f), wr, Pal(0.55f), 0.8f);
    }
    // neck + head
    V3 sh = sp[NB - 1];
    float na = Q.neckA + 0.07f * sinf(TW * gph * 2.f + 1.f);
    V3 hb = sh + v3(0.f, cosf(na) * Q.neckL, sinf(na) * Q.neckL);
    V3 nd = Nz(hb - sh);
    V3 nR = v3(1.f, 0.f, 0.f), nU = cross(nR, nd);
    V3 nk[5], nR5[5], nU5[5]; float nrx[5], nrz[5];
    for (int k = 0; k < 5; k++) {
        float t = k / 4.f;
        nk[k] = sh + (hb - sh) * t + v3(0.f, 0.f, 0.05f * Q.neckL * sinf(PI_ * t));
        nR5[k] = nR; nU5[k] = nU;
        nrx[k] = Q.chest * 0.60f * (1.f - 0.35f * t); nrz[k] = nrx[k] * 1.1f;
    }
    Tube(nk, nR5, nU5, nrx, nrz, 5, K, ws, 0.60f, 0.80f, 0.16f, 0.11f);
    for (int k = 0; k < 4; k++) Ln(nk[k], nk[k + 1], ww, Pal(0.65f), 0.95f);

    float pitch = Q.headDown + 0.08f * sinf(TW * gph * 2.f + 0.5f);
    V3 hd = v3(0.f, cosf(pitch), -sinf(pitch));
    V3 hR = v3(1.f, 0.f, 0.f), hU = cross(hR, hd);
    V3 nose = hb + hd * Q.headL;
    {
        V3 hp[4], hR4[4], hU4[4]; float hrx[4], hrz[4];
        static const float qq[4] = { 0.f, 0.30f, 0.65f, 1.f }, rr[4] = { 1.0f, 0.95f, 0.65f, 0.35f };
        for (int k = 0; k < 4; k++) {
            hp[k] = hb + hd * (Q.headL * qq[k]); hR4[k] = hR; hU4[k] = hU;
            hrx[k] = Q.hr * rr[k]; hrz[k] = Q.hr * rr[k] * 0.9f;
        }
        Tube(hp, hR4, hU4, hrx, hrz, 4, K < 8 ? K : 8, ws, 0.85f, 1.0f, 0.22f, 0.14f);
    }
    Ln(hb, nose, ww, Pal(0.9f), 0.95f);
    float open = 0.06f * Q.headL * (0.5f + 0.5f * sinf(TW * gph * 2.f + 2.f));
    Ln(hb - hU * (Q.hr * 0.7f), nose - hU * (Q.hr * 0.3f + open) - hd * (0.05f * Q.headL), ww * 0.8f, Pal(0.9f), 0.85f);
    for (int sd = -1; sd <= 1; sd += 2) {
        float s = (float)sd;
        V3 b1 = hb + hR * (s * Q.hr * 0.55f) + hU * (Q.hr * 0.75f);
        V3 b2 = hb + hd * (Q.hr * 1.1f) + hR * (s * Q.hr * 0.45f) + hU * (Q.hr * 0.8f);
        V3 tip = b1 + hU * Q.ear + hd * (-0.2f * Q.ear) + hR * (s * 0.2f * Q.ear);
        Ln(b1, tip, wr, Pal(0.95f), 0.9f); Ln(b2, tip, wr, Pal(0.95f), 0.9f); Ln(b1, b2, wr, Pal(0.95f), 0.7f);
        Jt(hb + hd * (Q.hr * 1.5f) + hR * (s * Q.hr * 0.6f) + hU * (Q.hr * 0.35f), 0.018f, col(255.f, 255.f, 255.f), 0.95f);
    }
    Jt(nose, 0.022f, Pal(0.95f), 0.9f);

    // tail
    V3 tl[7]; tl[0] = sp[0];
    for (int k = 1; k < 7; k++) {
        float t = k / 6.f;
        tl[k] = v3(0.08f * Q.tail * t * sinf(TW * gph - t * 4.f), sp[0].y - Q.tail * t * 0.95f,
                   sp[0].z + Q.tailUp * Q.tail * t - Q.tail * 0.35f * t * t + 0.06f * Q.tail * sinf(TW * gph * 2.f - t * 3.f));
        Ln(tl[k - 1], tl[k], ww * (1.f - 0.5f * t), Pal(0.1f + 0.1f * t), 0.9f - 0.3f * t);
    }
    // legs: FL FR HL HR
    const float hw = Q.chest * 0.85f;
    const float yHind = sp[1].y, yFore = sp[6].y;
    const float zHind = sp[1].z - Q.depth * 0.6f, zFore = sp[6].z - Q.depth * 0.55f;
    static const float offG[4] = { 0.00f, 0.12f, 0.50f, 0.62f };
    for (int j = 0; j < 4; j++) {
        bool fore = j < 2;
        float side = (j & 1) ? 1.f : -1.f;
        float s = gph + offG[j]; s -= floorf(s);
        float hy = fore ? yFore : yHind, hz = fore ? zFore : zHind;
        V3 hip = v3(side * hw, hy, hz);
        float fy, fz;
        if (s < 0.55f) { fy = hy + Q.stride * (0.5f - s / 0.55f); fz = 0.f; }
        else { float u = (s - 0.55f) / 0.45f; fy = hy + Q.stride * (-0.5f + u); fz = Q.lift * sinf(PI_ * u); }
        V3 foot = v3(side * hw, fy, fz);
        V3 ankle = foot + v3(0.f, fore ? 0.02f : -0.05f, Q.m);
        V3 knee = IK2(hip, ankle, Q.a, Q.b, fore ? -1.f : 1.f);
        Col c1 = Pal(0.20f + 0.1f * j);
        Ln(hip, knee, ww * 1.1f, c1, 0.95f);
        Ln(knee, ankle, ww, c1, 0.95f);
        Ln(ankle, foot, ww * 0.8f, c1, 0.9f);
        Jt(knee, 0.016f, c1, 0.7f); Jt(ankle, 0.013f, c1, 0.6f);
        for (int t = -1; t <= 1; t++) Ln(foot, foot + v3(t * 0.025f, 0.07f, 0.f), wr * 0.8f, c1, 0.8f);
        Ln(hip, fore ? sp[5] : sp[1], wr, c1, 0.6f);        // shoulder blade / pelvis strut
    }
}

// ======================================================================== bird (eagle / phoenix)
struct BirdP { float bl, span, tail, hz, amp; int kind; };
static const BirdP BP_EAGLE = { 0.95f, 1.35f, 0.55f, 1.4f, 0.55f, 0 };
static const BirdP BP_PHX   = { 1.30f, 2.40f, 3.20f, 1.0f, 0.50f, 1 };

static void Bird(const BirdP& B, float T, float seed, int dens) {
    const float TW = 2.f * PI_, bl = B.bl;
    const float ww = 0.012f, wr = 0.007f;
    float ft = TW * (B.hz * T + seed);
    float bz = 0.05f * bl * sinf(ft - 0.4f);
    const int NS = 5;
    V3 bp[NS], RR[NS], UU[NS]; float rx[NS], rz[NS];
    static const float by_[NS] = { 0.46f, 0.28f, 0.05f, -0.18f, -0.40f };
    static const float bz_[NS] = { 0.04f, 0.03f, 0.00f, -0.01f, 0.00f };
    static const float br_[NS] = { 0.05f, 0.06f, 0.11f, 0.09f, 0.04f };
    for (int i = 0; i < NS; i++) {
        bp[i] = v3(0.f, by_[i] * bl, bz_[i] * bl + bz);
        RR[i] = v3(1.f, 0.f, 0.f); UU[i] = v3(0.f, 0.f, 1.f);
        rx[i] = br_[i] * bl; rz[i] = br_[i] * bl * 0.9f;
    }
    Tube(bp, RR, UU, rx, rz, NS, 8, 0.006f, 0.1f, 0.5f, 0.20f, 0.12f);
    for (int i = 0; i < NS - 1; i++) Ln(bp[i], bp[i + 1], ww, Pal(0.3f + 0.1f * i), 0.95f);
    RingL(bp[2], RR[2], UU[2], rx[2], rz[2], 10, wr, Pal(0.4f), 0.55f);
    RingL(lerp3(bp[2], bp[3], 0.5f), RR[2], UU[2], rx[2] * 0.95f, rz[2] * 0.95f, 10, wr, Pal(0.45f), 0.5f);
    // head, hooked beak, eyes
    V3 head = bp[0], beak = head + v3(0.f, 0.16f * bl, -0.04f * bl);
    Ln(head, beak, ww, Pal(0.9f), 0.95f);
    Ln(beak, beak + v3(0.f, -0.03f * bl, -0.05f * bl), wr, Pal(0.9f), 0.9f);
    Ln(head + v3(0.f, 0.f, -0.02f * bl), beak + v3(0.f, -0.05f * bl, -0.02f * bl), wr, Pal(0.9f), 0.8f);
    for (int sd = -1; sd <= 1; sd += 2) Jt(head + v3(sd * 0.04f * bl, 0.04f * bl, 0.02f * bl), 0.012f, col(255.f, 255.f, 255.f), 0.95f);
    if (B.kind == 1)    // phoenix crest
        for (int i = 0; i < 3; i++) {
            float w = sinf(T * 3.f + i * 1.3f);
            V3 b0 = head + v3(0.f, -0.02f * bl, 0.04f * bl);
            V3 p1 = b0 + v3((i - 1) * 0.05f * bl, -0.14f * bl, 0.14f * bl);
            V3 p2 = p1 + v3((i - 1) * 0.08f * bl + 0.05f * bl * w, -0.18f * bl, 0.04f * bl);
            Ln(b0, p1, wr, Pal(0.1f), 0.9f); Ln(p1, p2, wr, Pal(0.15f), 0.8f);
        }
    // tail feathers
    int nt = B.kind == 1 ? 9 : 7;
    int segs = B.kind == 1 ? 6 : 2;
    V3 tipPrev = bp[4], tipFirst = bp[4];
    for (int i = 0; i < nt; i++) {
        float a = lerpf(-0.5f, 0.5f, (float)i / (nt - 1));
        V3 dir = v3(sinf(a), -cosf(a), 0.f);
        float tlen = B.tail * (0.75f + 0.25f * cosf(a * 2.f));
        V3 prev = bp[4];
        for (int q = 1; q <= segs; q++) {
            float t = (float)q / segs;
            float wx = B.kind == 1 ? 0.15f * tlen * t * t * sinf(T * 3.f - t * 4.f + i) : 0.f;
            float wz = B.kind == 1 ? (-0.12f * tlen * t * t + 0.10f * tlen * t * t * cosf(T * 2.5f - t * 4.f + i * 0.7f))
                                   : 0.04f * tlen * t * sinf(ft + i * 0.5f);
            V3 pt = bp[4] + dir * (tlen * t) + v3(wx, 0.f, wz);
            Ln(prev, pt, ww * 0.8f, Pal(0.6f + 0.3f * t), 0.9f - 0.4f * t);
            prev = pt;
        }
        if (B.kind == 0 && i > 0) Ln(tipPrev, prev, wr * 0.8f, Pal(0.7f), 0.55f);
        tipPrev = prev; if (i == 0) tipFirst = prev;
    }
    (void)tipFirst;
    // wings
    int np = 6 + dens * 2;        // primaries
    int ns = 5 + dens;            // secondaries
    for (int sg = -1; sg <= 1; sg += 2) {
        float s = (float)sg;
        V3 S = v3(s * 0.08f * bl, 0.10f * bl, 0.04f * bl + bz);
        float a1 = B.amp * 0.7f * sinf(ft), a2 = B.amp * 1.0f * sinf(ft - 0.7f), a3 = B.amp * 1.3f * sinf(ft - 1.4f);
        float L1 = B.span * 0.30f, L2 = B.span * 0.30f, L3 = B.span * 0.40f;
        V3 E  = S  + v3(s * cosf(a1), -0.10f, sinf(a1)) * L1;
        V3 Wr = E  + v3(s * cosf(a2), -0.12f, sinf(a2)) * L2;
        V3 Tp = Wr + v3(s * cosf(a3), -0.30f, sinf(a3)) * L3;
        Col ca = Pal(0.35f);
        Ln(S, E, ww * 1.3f, ca, 1.f); Ln(E, Wr, ww * 1.2f, ca, 1.f); Ln(Wr, Tp, ww, ca, 1.f);
        Jt(E, 0.025f, ca, 0.8f); Jt(Wr, 0.022f, ca, 0.8f);
        V3 pt[10]; V3 st[10];
        for (int j = 0; j < np; j++) {
            float f = (float)j / (np - 1);
            V3 base = Wr + (Tp - Wr) * (f * 0.92f);
            float lf = B.span * (0.55f - 0.15f * f);
            V3 d = Nz(v3(s * (0.10f + 0.40f * f), -1.f, 0.35f * sinf(a3 - 0.4f - f)));
            V3 e = base + d * lf;
            Col cf = Pal(0.55f + 0.3f * f);
            Ln(base, e, ww * 0.7f, cf, 0.9f, 0.55f);
            if (j > 0) Ln(pt[j - 1], e, wr * 0.7f, cf, 0.6f);
            pt[j] = e;
        }
        for (int j = 0; j < ns; j++) {
            float t = (float)j / (ns - 1);
            V3 base = t < 0.5f ? lerp3(Wr, E, t * 2.f) : lerp3(E, S, t * 2.f - 1.f);
            float lf = B.span * 0.46f * (1.f - 0.25f * t);
            V3 d = Nz(v3(s * 0.03f, -1.f, 0.25f * sinf(a2 - 0.2f - t)));
            V3 e = base + d * lf;
            Col cf = Pal(0.4f + 0.2f * t);
            Ln(base, e, ww * 0.7f, cf, 0.9f, 0.5f);
            if (j == 0) Ln(pt[0], e, wr * 0.7f, cf, 0.6f); else Ln(st[j - 1], e, wr * 0.7f, cf, 0.6f);
            st[j] = e;
            if (j == ns - 1) Ln(e, bp[3], wr * 0.7f, cf, 0.5f);
        }
        // coverts: short cross lines on the arm
        for (int j = 1; j < 4; j++) {
            float t = j / 4.f;
            V3 a = lerp3(S, E, t), b = a + v3(s * 0.04f, -0.22f * B.span * 0.5f, 0.f);
            Ln(a, b, wr * 0.6f, ca, 0.5f);
        }
        if (B.kind == 1) {   // phoenix: flame sparks off the wing tips
            Jt(Tp, 0.07f, col(255.f, 150.f, 30.f), 0.8f);
            for (int j = 0; j < 3; j++) {
                float ph = T * 0.9f + j * 0.33f + s * 0.1f; ph -= floorf(ph);
                Jt(Tp + v3(0.f, -0.6f * ph, 0.15f * sinf(T * 5.f + j)), 0.05f * (1.f - ph), col(255.f, 90.f + 100.f * ph, 20.f), 0.8f * (1.f - ph));
            }
        }
    }
    if (B.kind == 1)
        for (int j = 0; j < 8; j++) {
            float ph = T * 0.7f + j * 0.125f; ph -= floorf(ph);
            Jt(bp[4] + v3(0.4f * sinf(T * 5.f + j * 2.f), -B.tail * (0.3f + 0.75f * ph), 0.3f * cosf(T * 4.f + j)),
               0.06f * (1.f - ph), col(255.f, 80.f + 120.f * ph, 20.f), 0.9f * (1.f - ph));
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
            if (i <= I_PHOENIX) {
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
            if (Begin(C, P, 0.10f * sinf(T * 0.5f + e), 0.05f * sinf(T * 0.8f + e), 0.18f * sinf(T * 0.7f + e * 1.5f), sz, 1.8f, 0.52f + 0.08f * e))
                Bird(BP_EAGLE, T, e * 0.37f, g_dens);
        }
    if (g_tg[I_PHOENIX]) {
        V3 P = v3(0.f, -L * 0.5f - 2.6f * sp, zt + 2.8f * sp + 0.4f * sinf(T * 0.7f));
        if (Begin(C, P, 0.f, 0.04f * sinf(T * 0.6f), 0.12f * sinf(T * 0.5f), sz, 3.5f, 0.05f))
            Bird(BP_PHX, T, 0.f, g_dens);
    }
    if (g_tg[I_WOLF])                       // two wolves running beside the car
        for (int w = 0; w < 2; w++) {
            float s = w ? 1.f : -1.f;
            V3 P = v3(s * (hx + 1.3f * sp), (w ? -0.15f : 0.12f) * L, zg);
            if (Begin(C, P, 0.f, 0.f, 0.f, sz, 1.5f, 0.60f + 0.05f * w))
                Quad(QP_WOLF, g_gait + 0.33f * w, T, K);
        }
    if (g_tg[I_JAG])                        // two jaguars, a little further out
        for (int w = 0; w < 2; w++) {
            float s = w ? 1.f : -1.f;
            V3 P = v3(s * (hx + 2.6f * sp), (w ? 0.28f : -0.25f) * L, zg);
            if (Begin(C, P, 0.f, 0.f, 0.f, sz, 1.6f, 0.12f + 0.04f * w))
                Quad(QP_JAG, g_gait + 0.17f + 0.4f * w, T, K);
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
            float fs = dot(vel, f); if (!(fs > -300.f && fs < 300.f)) fs = 0.f;
            float hz = clampf(0.9f + fabsf(fs) * 0.11f, 0.9f, 3.4f);     // run faster when the car is faster
            g_gait += hz * dtE * g_spd; g_gait -= floorf(g_gait);
            g_gain = g_int;
            DrawBeasts(C, g_T * g_spd);
        }
    }
    __except (FaultFilter(GetExceptionInformation(), "vehicle fx")) { g_fxFaults++; g_n = 0; }
    g_gain = 1.f;
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
        if (i <= I_PHOENIX) g_kn[i] = lerpf(g_kn[i], g_tg[i] ? 1.f : 0.f, clampf(dt * 16.f, 0.f, 1.f));
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
    void* ent = (playing && camOk && g_fxFaults < 30) ? FindPlayerVehicle(-1, false) : NULL;
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
    Log("---- NeonAura v2.1 (camera fix) loaded.  GTA SA 1.0 US exe detected: %s", g_ver ? "yes" : "NO (addresses may be wrong)");
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
