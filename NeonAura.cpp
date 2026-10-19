// NeonAura.asi  -  GTA San Andreas 1.0 US (no textures, no images, pure code)
// Neon skeleton creatures (golden Eastern dragon, eagles, Simurgh, leopard/cheetah/wolf pack),
// light streaks, aura rings and screen glitch drawn as a Direct3D9 overlay.
// Keys: F5 on/off | F6 mode | F7 trails | F8 glitch | F9/F10 calibrate FOV | F11 ignore game-state checks
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <d3d9.h>
#include <math.h>
#include <stdint.h>

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
#define MAXV 59990
static Vtx g_vb[MAXV + 16];
static int g_n = 0;
static IDirect3DDevice9* g_dev = NULL;

static V3 g_cp, g_cr, g_cu, g_cf;          // camera pos / right / up / forward
static float g_sw = 800, g_sh = 600, g_focal = 600;
static float g_jx = 0, g_jy = 0, g_fade = 1.f;
static float g_fovCal = 1.0f;

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
static const Col GOLD = { 255.f, 190.f, 50.f }, GOLD2 = { 255.f, 228.f, 130.f }, FIRE = { 255.f, 115.f, 30.f };

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
        Neon(Q[i], Q[i + 1], 0.035f + 0.07f * DRad(s), GOLD2, 0.95f);
        if (i % 4 == 0) Orb(Q[i], 0.05f + 0.09f * DRad(s), GOLD, 0.5f);
    }
    // rib cage (rings around body tube) along neck + torso
    for (int i = 5; i < 80; i += 2) {
        float r = DRad((float)i / (N - 1));
        V3 C = Q[i] - Nm[i] * (r * 0.9f);
        static const float ang[5] = { -1.35f, -0.7f, -0.05f, 0.65f, 1.3f };
        for (int sg = -1; sg <= 1; sg += 2) {
            V3 pts[5];
            for (int k = 0; k < 5; k++) pts[k] = C + Sd[i] * (sg * r * 0.85f * cosf(ang[k])) + Nm[i] * (-r * sinf(ang[k]));
            Poly(pts, 5, 0.02f + r * 0.03f, GOLD, 0.8f);
        }
    }
    // belly line + scutes
    {
        V3 prev = Q[5] - Nm[5] * (DRad(5.f / (N - 1)) * 1.9f);
        for (int i = 6; i < N; i++) {
            float r = DRad((float)i / (N - 1));
            V3 cur = Q[i] - Nm[i] * (r * 1.9f);
            Neon(prev, cur, 0.025f, FIRE, 0.7f);
            if (i % 3 == 0) Neon(cur + Sd[i] * (r * 0.45f), cur - Sd[i] * (r * 0.45f), 0.015f, GOLD, 0.55f);
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
            Neon(base, tip, 0.028f, FIRE, 0.9f, 0.5f);
            if (nt < 60) tips[nt++] = tip;
        }
        Poly(tips, nt, 0.012f, FIRE, 0.45f);
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
            PolyF(pp, 3, 0.014f, FIRE, 0.85f, 0.15f);
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
            Neon(sh, el, 0.04f, GOLD, 0.9f); Neon(el, wr, 0.032f, GOLD, 0.9f);
            Orb(sh, 0.07f, GOLD2, 0.7f); Orb(el, 0.05f, GOLD2, 0.6f); Orb(wr, 0.05f, GOLD2, 0.6f);
            for (int j = -1; j <= 1; j++) {
                V3 kn = wr + Tn[i] * 0.20f + Sd[i] * (sg * 0.06f + j * 0.11f) - Nm[i] * 0.10f;
                V3 tip = kn + Tn[i] * 0.12f + Sd[i] * (j * 0.05f) - Nm[i] * 0.12f;
                Neon(wr, kn, 0.02f, GOLD2, 0.9f); Neon(kn, tip, 0.014f, GOLD2, 0.95f);
            }
        }
    }
    // head
    {
        V3 H0 = Q[0] - Nm[0] * 0.18f; float hs = 0.62f;
#define HP_(x, y, z) (H0 + Tn[0] * ((x) * hs) + Nm[0] * ((y) * hs) + Sd[0] * ((z) * hs))
        float open = 0.10f + 0.08f * sinf(1.7f * T);
        V3 ridge[4] = { HP_(0.15f, 0.45f, 0.f), HP_(0.55f, 0.55f, 0.f), HP_(1.2f, 0.42f, 0.f), HP_(1.8f, 0.22f, 0.f) };
        Poly(ridge, 4, 0.035f, GOLD2, 0.95f);
        for (int sg = -1; sg <= 1; sg += 2) {
            float z = (float)sg;
            V3 brow[4] = { HP_(0.15f, 0.45f, z * 0.28f), HP_(0.55f, 0.55f, z * 0.28f), HP_(1.2f, 0.42f, z * 0.16f), HP_(1.8f, 0.22f, z * 0.10f) };
            Poly(brow, 4, 0.03f, GOLD2, 0.9f);
            V3 uj[4] = { HP_(1.8f, 0.15f, z * 0.12f), HP_(1.2f, 0.08f, z * 0.24f), HP_(0.55f, 0.08f, z * 0.34f), HP_(0.12f, 0.12f, z * 0.34f) };
            Poly(uj, 4, 0.03f, GOLD, 0.95f);
            static const float lx[4] = { 0.12f, 0.7f, 1.25f, 1.6f }, ly[4] = { 0.05f, -0.05f, -0.1f, -0.1f }, lz[4] = { 0.34f, 0.27f, 0.16f, 0.04f };
            V3 lj[4];
            for (int k = 0; k < 4; k++) lj[k] = HP_(lx[k], ly[k] - (lx[k] - 0.12f) * open * 1.2f, z * lz[k]);
            Poly(lj, 4, 0.028f, GOLD, 0.9f);
            Neon(uj[3], lj[0], 0.03f, GOLD, 0.9f);
            // fangs
            Neon(HP_(1.55f, 0.12f, z * 0.12f), HP_(1.57f, -0.14f, z * 0.12f), 0.014f, col(255, 255, 255), 0.95f);
            Neon(HP_(0.95f, 0.08f, z * 0.27f), HP_(0.97f, -0.10f, z * 0.27f), 0.012f, col(255, 255, 255), 0.9f);
            // eye ring + glowing eye
            V3 ec = HP_(0.72f, 0.38f, z * 0.33f);
            V3 ring[9];
            for (int k = 0; k < 9; k++) { float a = k * 2.f * PI_ / 8.f; ring[k] = ec + (Tn[0] * cosf(a) + Nm[0] * sinf(a)) * 0.05f; }
            Poly(ring, 9, 0.012f, GOLD2, 0.9f);
            Orb(ec, 0.035f, col(255, 70, 40), 0.95f);
            // antlered horn
            V3 horn[4] = { HP_(0.35f, 0.52f, z * 0.22f), HP_(0.f, 0.9f, z * 0.32f), HP_(-0.55f, 1.05f, z * 0.4f), HP_(-1.1f, 0.95f, z * 0.42f) };
            PolyF(horn, 4, 0.03f, GOLD2, 0.95f, 0.6f);
            Neon(horn[1], HP_(0.05f, 1.3f, z * 0.34f), 0.018f, GOLD2, 0.85f, 0.5f);
            Neon(horn[2], HP_(-0.55f, 1.45f, z * 0.45f), 0.016f, GOLD2, 0.85f, 0.5f);
            // frill behind jaw
            for (int j = 0; j < 4; j++)
                Neon(HP_(0.1f, 0.1f, z * 0.38f), HP_(-0.5f - 0.15f * j, 0.15f + 0.12f * j, z * (0.6f + 0.1f * j)) + Nm[0] * (0.05f * sinf(4.f * T + j)), 0.014f, FIRE, 0.8f, 0.3f);
            // long whiskers
            V3 wk[12]; V3 p0 = HP_(1.45f, 0.12f, z * 0.2f);
            for (int k = 0; k < 12; k++)
                wk[k] = p0 - Tn[0] * (0.30f * k) + Sd[0] * (z * (0.18f * k)) + Nm[0] * (0.028f * k * sinf(0.7f * k - 6.f * T) - 0.02f * k);
            PolyF(wk, 12, 0.014f, GOLD2, 0.9f, 0.1f);
        }
        // flaming pearl
        V3 pearl = Q[0] + Tn[0] * (5.5f + 0.6f * sinf(2.f * T)) + Nm[0] * (0.5f * sinf(3.f * T));
        Orb(pearl, 0.55f, col(255, 240, 160), 0.9f);
        for (int j = 0; j < 6; j++) {
            float a = T * 4.f + j * 1.047f;
            Orb(pearl + Sd[0] * (cosf(a) * 0.55f) + Nm[0] * (sinf(a) * 0.55f), 0.12f, FIRE, 0.8f);
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
static bool g_enabled = true, g_trails = true, g_glitch = true, g_force = false;
static int g_mode = 0;   // 0 auto, 1 dragon, 2 eagles, 3 simurgh, 4 animals, 5 all
static float g_T = 0.f, g_gait[4] = { 0, 0.25f, 0.5f, 0.75f }, g_gz[4] = { 0, 0, 0, 0 };
static float g_nextG = 6.f, g_endG = 0.f;
static LARGE_INTEGER g_freq, g_last;

static bool Key(int vk, bool& prev) { bool d = (GetAsyncKeyState(vk) & 0x8000) != 0; bool r = d && !prev; prev = d; return r; }

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
    // hotkeys (only when the game window is focused)
    static bool k5 = false, k6 = false, k7 = false, k8 = false, k9 = false, k10 = false, k11 = false;
    DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid == GetCurrentProcessId()) {
        if (Key(VK_F5, k5)) g_enabled = !g_enabled;
        if (Key(VK_F6, k6)) g_mode = (g_mode + 1) % 6;
        if (Key(VK_F7, k7)) g_trails = !g_trails;
        if (Key(VK_F8, k8)) g_glitch = !g_glitch;
        if (Key(VK_F9, k9)) g_fovCal *= 0.97f;
        if (Key(VK_F10, k10)) g_fovCal *= 1.03f;
        if (Key(VK_F11, k11)) g_force = !g_force;
    }
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

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
    }
    return TRUE;
}
