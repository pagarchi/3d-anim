// NeonAura v2  -  GTA San Andreas 1.0 US  (.asi plugin, Direct3D9 overlay, no textures)
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
    I_MASTER, I_SHELL, I_SCAN, I_FLOW, I_ARCS, I_GLOW, I_WHEELS,   // 0..6 = toggles
    I_COLOR, I_INT, I_OFF, I_SPD, I_DENS, I_FOV, I_RESET, I_COUNT
};
static const char* LB[I_COUNT] = { "MASTER SWITCH", "BODY SHELL", "SCANNER RING", "EDGE FLOW", "LIGHTNING", "GROUND GLOW", "WHEEL RINGS",
                                   "COLOR", "INTENSITY", "AURA DISTANCE", "SPEED", "DENSITY", "FOV CAL", "RESET DEFAULTS" };
static const char* COLN[6] = { "CYAN", "MAGENTA", "GOLD", "GREEN", "RED", "RAINBOW" };
static const char* DENN[3] = { "LOW", "MEDIUM", "HIGH" };

static bool  g_tg[7];
static int   g_col, g_dens;
static float g_int, g_off, g_spd, g_fovCal;
static bool  g_force = false;
static bool  g_ver = false;      // exe looks like GTA SA 1.0 US

static void ResetDefaults() {
    for (int i = 0; i < 7; i++) g_tg[i] = true;
    g_col = 0; g_dens = 1; g_int = 1.0f; g_off = 0.9f; g_spd = 1.0f; g_fovCal = 1.0f;
}
static float* SliderPtr(int i) { return i == I_INT ? &g_int : (i == I_OFF ? &g_off : (i == I_SPD ? &g_spd : &g_fovCal)); }
static void SliderRange(int i, float& lo, float& hi, float& st) {
    switch (i) {
    case I_INT: lo = 0.2f;  hi = 2.0f;  st = 0.05f; break;
    case I_OFF: lo = 0.3f;  hi = 3.0f;  st = 0.1f;  break;
    case I_SPD: lo = 0.25f; hi = 3.0f;  st = 0.05f; break;
    default:    lo = 0.7f;  hi = 1.3f;  st = 0.01f; break;
    }
}
static void SliderText(int i, char* out) {
    float v = *SliderPtr(i);
    if (i == I_OFF) sprintf(out, "%d CM", (int)(v * 100.f + 0.5f));
    else sprintf(out, "%d%%", (int)(v * 100.f + 0.5f));
}
static void Adjust(int idx, int dir) {
    if (idx <= I_WHEELS) { g_tg[idx] = dir == 0 ? !g_tg[idx] : (dir > 0); return; }
    switch (idx) {
    case I_COLOR: g_col = (g_col + (dir >= 0 ? 1 : 5)) % 6; break;
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
static int g_col_;   // (unused alias kept out of the way)
static Col Pal(float u) {
    if (g_col == 5) return Hsv(u + g_T * 0.12f, 0.85f, 1.f);
    static const Col P1[5] = { { 0.f, 255.f, 235.f }, { 255.f, 40.f, 200.f }, { 255.f, 190.f, 40.f }, { 60.f, 255.f, 100.f }, { 255.f, 50.f, 60.f } };
    static const Col P2[5] = { { 255.f, 50.f, 220.f }, { 80.f, 160.f, 255.f }, { 255.f, 100.f, 20.f }, { 0.f, 220.f, 255.f }, { 255.f, 170.f, 40.f } };
    float m = 0.5f + 0.5f * sinf(u * 2.f * PI_ - g_T * 1.4f);
    return mixc(P1[g_col], P2[g_col], m);
}

// ======================================================================== vehicle body hull
struct Body {
    V3 o, r, f, u;                      // vehicle origin and axes (right / forward / up)
    float x0, x1, y0, y1, z0, z1;       // local bounding box
    float W, L, H, off;
};
static float ProfTop(float t) {         // roof profile: 0 = rear, 1 = front (fraction of height)
    static const float tx[8] = { 0.f, .10f, .26f, .40f, .62f, .78f, .92f, 1.f };
    static const float tv[8] = { .58f, .62f, .90f, 1.f, 1.f, .68f, .60f, .52f };
    for (int i = 0; i < 7; i++)
        if (t <= tx[i + 1]) return lerpf(tv[i], tv[i + 1], sm((t - tx[i]) / (tx[i + 1] - tx[i])));
    return tv[7];
}
// point + outward normal on the hull.  t: 0 rear .. 1 front,  a: angle around the cross-section
static void HullPt(const Body& B, float t, float a, V3& p, V3& n) {
    float e = fabsf(2.f * t - 1.f), e2 = e * e, sh = e2 * e2 * e2;
    float hw = 0.5f * B.W * (1.f - 0.42f * sh) + B.off;
    float zt = B.z0 + B.H * ProfTop(t) + B.off, zb = fmaxf(B.z0 + 0.05f, B.z0 + B.H * 0.10f - B.off * 0.3f);
    float zc = (zt + zb) * 0.5f, hh = (zt - zb) * 0.5f;
    float c = cosf(a), s = sinf(a);
    float px = hw * sgnf(c) * sqrtf(fabsf(c)), pz = hh * sgnf(s) * sqrtf(fabsf(s));
    float yy = lerpf(B.y0 - B.off, B.y1 + B.off, t);
    float cx = (B.x0 + B.x1) * 0.5f;
    p = B.o + B.r * (cx + px) + B.f * yy + B.u * (zc + pz);
    float nx = px / hw, nz = pz / hh;
    nx = nx * nx * nx / hw; nz = nz * nz * nz / hh;
    float ny = (2.f * t - 1.f) * sh * 1.2f;
    float nl = sqrtf(nx * nx + nz * nz + ny * ny); if (nl < 1e-6f) nl = 1.f;
    n = B.r * (nx / nl) + B.u * (nz / nl) + B.f * (ny / nl);
}
// faces away from camera -> dim (gives a cheap depth cue even without a depth buffer)
static float FaceA(V3 p, V3 n) {
    float d = dot(n, Nz(g_cp - p));
    return 0.14f + 0.86f * sm((d + 0.1f) / 0.5f);
}

// ---- body shell: ring + longitudinal grid hugging the car
static void Shell(const Body& B, float T) {
    const int NS = 14 + 4 * g_dens, K = 24 + 4 * g_dens;
    static V3 P[24][34]; static float A[24][34];
    for (int i = 0; i < NS; i++) {
        float t = (float)i / (NS - 1);
        for (int k = 0; k < K; k++) {
            V3 n; HullPt(B, t, k * 2.f * PI_ / K, P[i][k], n);
            A[i][k] = FaceA(P[i][k], n);
        }
    }
    for (int i = 0; i < NS; i++) {
        float t = (float)i / (NS - 1);
        float pulse = 0.78f + 0.22f * sinf(T * 2.2f * g_spd - t * 5.f);
        Col c = Pal(t);
        for (int k = 0; k < K; k++) {
            int k2 = (k + 1) % K;
            float al = 0.34f * pulse;
            Neon(P[i][k], P[i][k2], 0.010f, c, al * A[i][k], al * A[i][k2]);
        }
    }
    for (int k = 0; k < K; k++) {
        for (int i = 0; i < NS - 1; i++) {
            float t = (float)i / (NS - 1);
            float al = 0.26f * (0.8f + 0.2f * sinf(T * 1.7f * g_spd + k * 0.5f));
            Neon(P[i][k], P[i + 1][k], 0.009f, Pal(t), al * A[i][k], al * A[i + 1][k]);
        }
    }
}
// ---- one closed ring around the body at station t
static void Ring(const Body& B, float t, float ww, float u, float al) {
    const int K = 40;
    V3 prev, pn; HullPt(B, t, 0.f, prev, pn);
    float pa = FaceA(prev, pn);
    Col c = Pal(u);
    for (int k = 1; k <= K; k++) {
        V3 p, n; HullPt(B, t, k * 2.f * PI_ / K, p, n);
        float fa = FaceA(p, n);
        Neon(prev, p, ww, c, al * pa, al * fa);
        prev = p; pa = fa;
    }
}
static void Scanner(const Body& B, float T) {
    float ph = T * 0.40f * g_spd; ph -= floorf(ph);
    float t = -0.05f + 1.1f * ph;
    float env = sm(fminf(ph * 8.f, (1.f - ph) * 8.f));
    for (int j = 0; j < 5; j++) {
        float tt = t - j * 0.035f;
        if (tt < 0.f || tt > 1.f) continue;
        Ring(B, tt, 0.024f - 0.003f * j, tt, (1.f - j * 0.19f) * env * 0.95f);
    }
}
// ---- light streaks flowing along the body contour lines
static void Flow(const Body& B, float T) {
    static const float AN[5] = { 1.5708f, 0.7854f, 2.3562f, 0.05f, 3.0916f };
    for (int j = 0; j < 5; j++) {
        float h = T * 0.33f * g_spd + j * 0.21f; h -= floorf(h);
        bool rev = (j & 1) != 0;
        float env = sm(fminf(h * 6.f, (1.f - h) * 6.f));
        float head = rev ? 1.f - h : h;
        const int S = 16; const float LEN = 0.28f;
        V3 prev, pn; float pa = 0.f, pal = 0.f; bool have = false;
        for (int s = 0; s <= S; s++) {
            float tt = rev ? head + s * (LEN / S) : head - s * (LEN / S);
            if (tt < 0.f || tt > 1.f) { have = false; continue; }
            V3 p, n; HullPt(B, tt, AN[j], p, n);
            float fa = FaceA(p, n);
            float al = powf(1.f - (float)s / S, 1.4f) * env;
            if (have) Neon(prev, p, 0.020f - 0.010f * ((float)s / S), mixc(Pal(tt), col(255, 255, 255), 0.25f * (1.f - (float)s / S)), pal * pa, al * fa);
            if (s == 0) Orb(p, 0.05f, Pal(tt), 0.9f * env * fa);
            prev = p; pa = fa; pal = al; have = true;
        }
    }
}
// ---- lightning crawling over the body
static void Arcs(const Body& B, float T) {
    uint32_t slot = (uint32_t)(T * 14.f * g_spd);
    for (int j = 0; j < 3; j++) {
        Rng R; R.s = slot * 2654435761u + (uint32_t)j * 7919u + 12345u; if (!R.s) R.s = 1; R.f(); R.f();
        if (R.f() < 0.25f) continue;
        float t = R.f(), a = (R.f() < 0.7f) ? R.f() * PI_ : R.f() * 2.f * PI_;
        float flick = 0.7f + 0.3f * R.f();
        V3 pts[10]; float fa[10];
        for (int q = 0; q < 10; q++) {
            V3 p, n; HullPt(B, clampf(t, 0.f, 1.f), a, p, n);
            pts[q] = p + n * (0.015f + R.f() * 0.05f); fa[q] = FaceA(p, n);
            t += (R.f() - 0.4f) * 0.09f; a += (R.f() - 0.5f) * 0.9f;
        }
        Col c = mixc(Pal(R.f()), col(255, 255, 255), 0.45f);
        for (int q = 0; q < 9; q++) Neon(pts[q], pts[q + 1], 0.016f, c, flick * fa[q], flick * fa[q + 1]);
        // branch
        V3 bp = pts[4]; float bt = t, ba = a;
        for (int q = 0; q < 4; q++) {
            V3 p, n; HullPt(B, clampf(bt, 0.f, 1.f), ba, p, n);
            V3 np = p + n * (0.015f + R.f() * 0.05f);
            Neon(bp, np, 0.010f, c, 0.7f * flick * fa[4] * (1.f - q * 0.2f), 0.6f * flick * fa[4] * (1.f - q * 0.25f));
            bp = np; bt += (R.f() - 0.2f) * 0.07f; ba += (R.f() - 0.5f) * 1.2f;
        }
    }
}
// ---- ripples sliding out from under the car along the ground
static void Underglow(const Body& B, float T) {
    float cx = (B.x0 + B.x1) * 0.5f, cy = (B.y0 + B.y1) * 0.5f, zz = B.z0 + 0.05f;
    for (int j = 0; j < 4; j++) {
        float ph = (j == 3) ? 0.f : (T * 0.45f * g_spd + j / 3.f); ph -= floorf(ph);
        float k = 1.f + 0.9f * ph;
        float al = (j == 3) ? 0.55f : (1.f - ph) * sm(ph * 6.f) * 0.9f;
        const int K = 48;
        V3 prev = v3(0, 0, 0);
        for (int i = 0; i <= K; i++) {
            float a = i * 2.f * PI_ / K, c = cosf(a), s = sinf(a);
            float x = cx + (B.W * 0.5f + B.off) * k * sgnf(c) * sqrtf(fabsf(c)) * 1.0f;
            float y = cy + (B.L * 0.5f + B.off) * k * sgnf(s) * sqrtf(fabsf(s));
            V3 p = B.o + B.r * x + B.f * y + B.u * zz;
            if (i > 0) Neon(prev, p, 0.03f * (1.f - ph * 0.5f), Pal((float)i / K), al, al);
            prev = p;
        }
    }
}
// ---- spinning rings on the four wheels
static void Wheels(const Body& B, float ang) {
    float R = clampf(B.H * 0.20f, 0.27f, 0.5f);
    float cx = (B.x0 + B.x1) * 0.5f, cy = (B.y0 + B.y1) * 0.5f;
    float wy[2] = { cy + 0.31f * B.L, cy - 0.31f * B.L };
    for (int w = 0; w < 2; w++) for (int sg = -1; sg <= 1; sg += 2) {
        float s = (float)sg;
        float wx = cx + s * (B.W * 0.5f + 0.15f + B.off * 0.5f), zc = B.z0 + R;
        V3 ctr = B.o + B.r * wx + B.f * wy[w] + B.u * zc;
        float fa = FaceA(ctr, B.r * s);
        const int K = 28;
        V3 po = v3(0, 0, 0), pi = v3(0, 0, 0);
        for (int i = 0; i <= K; i++) {
            float a = i * 2.f * PI_ / K;
            V3 o2 = B.o + B.r * wx + B.f * (wy[w] + R * cosf(a)) + B.u * (zc + R * sinf(a));
            V3 i2 = B.o + B.r * wx + B.f * (wy[w] + R * 0.6f * cosf(a)) + B.u * (zc + R * 0.6f * sinf(a));
            if (i > 0) { Neon(po, o2, 0.022f, Pal((float)i / K), 0.9f * fa); Neon(pi, i2, 0.012f, Pal((float)i / K + 0.3f), 0.5f * fa); }
            po = o2; pi = i2;
        }
        for (int sp = 0; sp < 5; sp++) {
            float a = -ang + sp * 2.f * PI_ / 5.f;
            V3 in = B.o + B.r * wx + B.f * (wy[w] + R * 0.12f * cosf(a)) + B.u * (zc + R * 0.12f * sinf(a));
            V3 ou = B.o + B.r * wx + B.f * (wy[w] + R * 0.6f * cosf(a)) + B.u * (zc + R * 0.6f * sinf(a));
            Neon(in, ou, 0.012f, Pal(0.15f * sp), 0.7f * fa);
        }
        Orb(ctr, 0.05f, Pal(0.5f), 0.7f * fa);
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
    const float pw = 190 * px, pad = 10 * px, rh = 15 * px, headH = 30 * px, footH = 30 * px;
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
    Text("CAR BODY FX  V2", x0 + pad, y0 + 20 * px, px, col(120, 170, 190), 1.f);
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
            if (i <= I_WHEELS) {
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
static void MakeBody(Body& B, void* ent, V3 pos, V3 r, V3 f, V3 u) {
    float mn[3] = { -0.95f, -2.3f, -0.6f }, mx[3] = { 0.95f, 2.3f, 0.85f };
    float tmn[3], tmx[3];
    if (ReadBounds(ent, tmn, tmx)) {
        float w = tmx[0] - tmn[0], l = tmx[1] - tmn[1], h = tmx[2] - tmn[2];
        if (tmn[0] < -0.2f && tmx[0] > 0.2f && tmn[1] < -0.2f && tmx[1] > 0.2f && w > 0.7f && w < 8.f && l > 1.1f && l < 30.f && h > 0.5f && h < 10.f)
            for (int i = 0; i < 3; i++) { mn[i] = tmn[i]; mx[i] = tmx[i]; }
    }
    B.o = pos; B.r = r; B.f = f; B.u = u;
    B.x0 = mn[0]; B.x1 = mx[0]; B.y0 = mn[1]; B.y1 = mx[1]; B.z0 = mn[2]; B.z1 = mx[2];
    B.W = mx[0] - mn[0]; B.L = mx[1] - mn[1]; B.H = mx[2] - mn[2]; B.off = g_off;
}

// ======================================================================== frame
static LARGE_INTEGER g_freq, g_last;
static bool g_first = true;
static float g_wang = 0.f;

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
static void DrawVehicleFX(void* ent, float dtE) {
    __try {
        V3 pos, r, f, u;
        if (GetXform(ent, pos, r, f, u)) {
            Body B; MakeBody(B, ent, pos, r, f, u);
            float* ms = (float*)((char*)ent + 0x44);
            V3 vel = v3(ms[0], ms[1], ms[2]) * 50.f;
            float fs = dot(vel, f); if (!(fs > -300.f && fs < 300.f)) fs = 0.f;
            g_wang += fs * dtE / 0.33f;
            g_gain = g_int;
            if (g_tg[I_GLOW])   Underglow(B, g_T);
            if (g_tg[I_SHELL])  Shell(B, g_T);
            if (g_tg[I_FLOW])   Flow(B, g_T);
            if (g_tg[I_SCAN])   Scanner(B, g_T);
            if (g_tg[I_WHEELS]) Wheels(B, g_wang);
            if (g_tg[I_ARCS])   Arcs(B, g_T);
        }
    }
    __except (FaultFilter(GetExceptionInformation(), "vehicle fx")) { g_fxFaults++; g_n = 0; }
    g_gain = 1.f;
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
        if (i <= I_WHEELS) g_kn[i] = lerpf(g_kn[i], g_tg[i] ? 1.f : 0.f, clampf(dt * 16.f, 0.f, 1.f));
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
    D3DVIEWPORT9 vp; d->GetViewport(&vp);
    g_sw = (float)vp.Width; g_sh = (float)vp.Height;
    g_focal = (g_sw * 0.5f) / tanf(fov * PI_ / 360.f) * g_fovCal;

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
    Log("---- NeonAura v2 loaded.  GTA SA 1.0 US exe detected: %s", g_ver ? "yes" : "NO (addresses may be wrong)");
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
