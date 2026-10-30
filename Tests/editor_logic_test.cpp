// Tests for the JUCE-free editor logic.   g++ -std=c++17 -O2 Tests/editor_logic_test.cpp -ISource -o t4 && ./t4
#include "EditorLogic.h"
#include <cstdio>
using namespace rfx::ui;
static int fails = 0;
static void check (const char* what, bool ok) { std::printf ("  %-66s %s\n", what, ok ? "ok" : "FAIL"); if (! ok) ++fails; }
static bool near (float a, float b, float e = 1e-4f) { return std::fabs (a - b) <= e; }

int main()
{
    std::printf ("pattern tools\n");
    {
        float v[32]; for (int i = 0; i < 32; ++i) v[i] = (float) i / 31.f;
        toolRamp (v, 32, true);  check ("ramp up: 0 .. 1", near (v[0], 0.f) && near (v[31], 1.f) && v[10] < v[11]);
        toolRamp (v, 32, false); check ("ramp down: 1 .. 0", near (v[0], 1.f) && near (v[31], 0.f));
        toolSine (v, 32, 1.f);   check ("sine starts at the bottom, peaks mid-lane", v[0] < 0.01f && near (v[16], 1.f, 0.01f) && v[31] < 0.05f);
        toolSine (v, 24, 2.f);   check ("sine on a 24-step lane uses 24 steps, 2 cycles", near (v[12], 0.f, 0.02f) && near (v[6], 1.f, 0.02f));
        float u[32] = {}; toolLine (u, 32, 4, 12, 0.f, 1.f);
        check ("line: linear between two steps, outside untouched", near (u[4], 0.f) && near (u[12], 1.f) && near (u[8], 0.5f) && u[3] == 0.f && u[13] == 0.f);
        toolLine (u, 32, 20, 10, 1.f, 0.f); check ("line: end points in either order", near (u[10], 0.f) && near (u[20], 1.f));
        float m[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }; toolMirror (m, 8); check ("mirror reverses", m[0] == 8 && m[7] == 1);
        float s[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }; toolShift (s, 8, 1);  check ("shift right by 1 wraps", s[0] == 8 && s[1] == 1 && s[7] == 7);
        toolShift (s, 8, -1); check ("shift left by 1 undoes it", s[0] == 1 && s[7] == 8);
        float s2[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }; toolShift (s2, 8, 19); check ("shift by 19 on 8 steps == shift by 3", s2[3] == 1);
        float p[6] = { 0.f, 0.25f, 0.5f, 0.75f, 1.f, 0.5f }; toolInvert (p, 6); check ("invert: 1 - v (0.5 stays centre)", near (p[0], 1.f) && near (p[2], 0.5f) && near (p[5], 0.5f));
        float c[32]; for (auto& x : c) x = 0.3f; toolClear (c, 32, 0.5f); check ("clear to a value (pan centre 0.5)", near (c[0], 0.5f) && near (c[31], 0.5f));
        float r1[32], r2[32], r3[32]; toolRandom (r1, 32, 42); toolRandom (r2, 32, 42); toolRandom (r3, 32, 43);
        bool same = true, diff = false, rng = true; for (int i = 0; i < 32; ++i) { if (r1[i] != r2[i]) same = false; if (r1[i] != r3[i]) diff = true; if (r1[i] < 0 || r1[i] > 1) rng = false; }
        check ("random: same seed repeats, other seed differs, stays 0..1", same && diff && rng);
        float q[32]; toolRandom (q, 32, 7, 4); bool four = true; for (int i = 0; i < 32; ++i) if (! (near (q[i], 0.f) || near (q[i], 1.f / 3.f) || near (q[i], 2.f / 3.f) || near (q[i], 1.f))) four = false;
        check ("random with 4 levels only gives 0, 1/3, 2/3, 1", four);
    }
    std::printf ("orbit geometry\n");
    {
        const OrbitRing rings[3] = { { 32, 232.f, 290.f }, { 24, 172.f, 222.f }, { 16, 118.f, 160.f } };
        auto hit = [&] (float dx, float dy) { return orbitHitTest (rings, 3, dx, dy); };
        auto h = hit (0.f, -260.f);  check ("straight up in the outer ring = step 0", h.ring == 0 && h.step == 0);
        h = hit (260.f, 0.f);        check ("3 o'clock outer ring = step 8 of 32 (a quarter turn)", h.ring == 0 && h.step == 8);
        h = hit (0.f, 260.f);        check ("6 o'clock outer ring = step 16", h.ring == 0 && h.step == 16);
        h = hit (-260.f, 0.f);       check ("9 o'clock outer ring = step 24", h.ring == 0 && h.step == 24);
        h = hit (-1.f, -260.f);      check ("just left of 12 o'clock = last step (31)", h.ring == 0 && h.step == 31);
        h = hit (0.f, 200.f);        check ("middle ring, 6 o'clock = step 12 of 24", h.ring == 1 && h.step == 12);
        h = hit (140.f, 0.f);        check ("inner ring, 3 o'clock = step 4 of 16", h.ring == 2 && h.step == 4);
        h = hit (0.f, -50.f);        check ("inside the hole = no hit", h.ring == -1);
        h = hit (0.f, -400.f);       check ("outside everything = no hit", h.ring == -1);
        h = hit (0.f, -261.f);       check ("radial value: halfway across the ring = 0.5", near (h.value, 0.5f, 0.02f));
        check ("step angles tile the circle", near (orbitStepEnd (31, 32), (float) kTwoPi, 1e-4f) && near (orbitStepStart (8, 32), (float) kTwoPi / 4.f, 1e-4f));
        bool all = true;
        for (int r = 0; r < 3; ++r) for (int st = 0; st < rings[r].steps; ++st)
        {
            const float a = orbitStepMid (st, rings[r].steps), rad = 0.5f * (rings[r].rInner + rings[r].rOuter);
            const auto k = hit (rad * std::sin (a), -rad * std::cos (a)); if (k.ring != r || k.step != st) all = false;
        }
        check ("every step's centre point hits that exact step (72 steps)", all);
    }
    std::printf ("morph pad\n");
    {
        SceneMorph m; m.scene[0] = { 0.1f, 0.0f, 0.4f }; m.scene[1] = { 0.9f, 0.6f, 0.2f }; m.scene[2] = { 0.5f, 1.0f, 0.8f };
        std::vector<float> cur = { 0.33f, 0.33f, 0.33f }; std::vector<bool> none (3, false), lock1 = { false, true, false };
        auto onA = m.blend (m.pos[0][0], m.pos[0][1], cur, none);
        check ("exactly on scene A gives scene A", near (onA[0], 0.1f) && near (onA[1], 0.f) && near (onA[2], 0.4f));
        auto w = m.weightsAt (0.5f, 0.45f); check ("weights sum to 1 and are positive", near (w[0] + w[1] + w[2], 1.f) && w[0] > 0 && w[1] > 0 && w[2] > 0);
        auto nearB = m.blend (0.8f, 0.3f, cur, none); check ("near B is dominated by B", std::fabs (nearB[0] - 0.9f) < std::fabs (nearB[0] - 0.1f));
        auto locked = m.blend (m.pos[1][0], m.pos[1][1], cur, lock1); check ("a locked parameter never moves", near (locked[1], 0.33f) && near (locked[0], 0.9f));
        bool inside = true; for (float x = 0; x <= 1; x += 0.05f) for (float y = 0; y <= 1; y += 0.05f) for (float vv : m.blend (x, y, cur, none)) if (vv < 0.f || vv > 1.f) inside = false;
        check ("blend always stays in 0..1 over the whole pad", inside);
        auto r = randomise (cur, lock1, 5, 1.f); check ("dice: locked untouched, others change, all in 0..1", near (r[1], 0.33f) && r[0] != cur[0] && r[2] != cur[2] && r[0] >= 0 && r[0] <= 1);
        auto r0 = randomise (cur, none, 5, 0.f);   check ("dice with amount 0 changes nothing", near (r0[0], 0.33f) && near (r0[2], 0.33f));
    }
    std::printf ("tap tempo and pluck light\n");
    {
        TapTempo t; check ("first tap gives no tempo yet", t.tap (0.0) == 0.0);
        t.tap (500.0); const double b = t.tap (1000.0); check ("taps 500 ms apart = 120 BPM", near ((float) b, 120.f, 0.01f));
        TapTempo u; u.tap (0); u.tap (600); u.tap (1200); const double c = u.tap (1800); check ("taps 600 ms apart = 100 BPM", near ((float) c, 100.f, 0.01f));
        const double d = u.tap (5000.0); check ("a gap over 2 s starts over (no tempo from one tap)", d == 0.0);
        TapTempo f; f.tap (0); const double g = f.tap (100.0); check ("absurdly fast taps clamp to 240 BPM", near ((float) g, 240.f));
        TapTempo s; s.tap (0); const double h = s.tap (1900.0); check ("very slow taps clamp to 40 BPM", near ((float) h, 40.f));
        PluckLight p; p.trigger(); check ("pluck light jumps to 1", p.level == 1.f);
        for (int i = 0; i < 12; ++i) { p.tick (0.033f); } check ("and fades towards 0 within ~0.4 s", p.level < 0.05f);
    }
    std::printf ("modulation rings\n");
    {
        check ("cutoff / reso / amp / pan / drive knobs can take a dropped source", modDestForParam ("fLow") == 0 && modDestForParam ("fHigh") == 0 && modDestForParam ("fRes") == 1 && modDestForParam ("tDepth") == 2 && modDestForParam ("pDepth") == 3 && modDestForParam ("drive") == 4);
        check ("other knobs (e.g. reverb mix) are not modulatable yet", modDestForParam ("rvMix") == -1 && modDestForParam ("") == -1);
        auto r = modRingRange (0.5f, 0.3f); auto r2 = modRingRange (0.5f, -0.3f);
        check ("ring range runs base -> base+amount, either direction", near (r.lo, 0.5f) && near (r.hi, 0.8f) && near (r2.lo, 0.2f) && near (r2.hi, 0.5f));
        auto r3 = modRingRange (0.9f, 0.5f); check ("ring is clamped to the knob's travel", near (r3.hi, 1.f));
        check ("live dot follows the source", near (modRingPosition (0.4f, 0.3f, 0.f), 0.4f) && near (modRingPosition (0.4f, 0.3f, 1.f), 0.7f));
    }
    std::printf ("window scaling\n");
    {
        const float bw = 1510.f, bh = 872.f;
        check ("13-inch laptop (1440 x 875 usable) opens at 90 %", near (fitScale (1440, 875, bw, bh), 0.90f));
        check ("1080p monitor opens at 100 % (never enlarged on its own)", near (fitScale (1920, 1080, bw, bh), 1.00f));
        check ("1280 x 720 screen opens at 70 %", near (fitScale (1280, 720, bw, bh), 0.70f));
        check ("a tiny screen is clamped to the 50 % minimum", near (fitScale (500, 400, bw, bh), 0.50f));
        check ("the chosen size really fits the usable area", scaledSize (1510, fitScale (1440, 875, bw, bh)) <= 1440 && scaledSize (872, fitScale (1440, 875, bw, bh)) <= 875);
        check ("a saved scale wins over the screen", near (initialScale (0.75f, 1920, 1080, bw, bh), 0.75f));
        check ("a nonsense saved scale is clamped to 50 - 200 %", near (initialScale (9.f, 1920, 1080, bw, bh), 2.f) && near (initialScale (0.1f, 1920, 1080, bw, bh), 0.5f));
        check ("no saved scale (0) falls back to fitting the screen", near (initialScale (0.f, 1440, 875, bw, bh), 0.90f));
        check ("snapping goes down to a 5 % step, and 1.0 stays 1.0", near (snapScale (0.927f), 0.90f) && near (snapScale (1.0f), 1.0f) && near (snapScale (0.999f), 0.95f));
        check ("scaled sizes keep the aspect ratio within one pixel", std::fabs ((double) scaledSize (1510, 0.85f) / (double) scaledSize (872, 0.85f) - 1510.0 / 872.0) < 0.003);
        bool menuOk = true; for (int pct : kScalePercents) if (pct < (int) (kMinScale * 100.f) || pct > (int) (kMaxScale * 100.f)) menuOk = false;
        check ("every size-menu entry is inside the allowed 50 - 200 % range", menuOk);
    }
    std::printf (fails ? "FAILED (%d)\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
