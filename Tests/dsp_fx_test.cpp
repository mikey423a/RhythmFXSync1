// Tests for the phaser, ring modulator and bit crusher.   g++ -std=c++17 -O2 Tests/dsp_fx_test.cpp -ISource
#include "RhythmDSP2.h"
#include <cstdio>
#include <set>
using namespace rfx;
static int fails = 0;
static void check (const char* what, bool ok) { std::printf ("  %-62s %s\n", what, ok ? "ok" : "FAIL"); if (! ok) ++fails; }
static double sr = 48000.0;
static double amp (const std::vector<float>& v, double hz, size_t a, size_t n)     // single-bin DFT magnitude
{
    double c = 0, s = 0;
    for (size_t i = a; i < a + n; ++i) { c += v[i] * std::cos (2 * kPi * hz * (double) i / sr); s += v[i] * std::sin (2 * kPi * hz * (double) i / sr); }
    return 2.0 * std::sqrt (c * c + s * s) / (double) n;
}
static std::vector<float> sine (double hz, double a, size_t n) { std::vector<float> v (n); for (size_t i = 0; i < n; ++i) v[i] = (float) (a * std::sin (2 * kPi * hz * (double) i / sr)); return v; }

int main()
{
    std::printf ("Phaser\n");
    {
        // depth 0 -> fixed sweep position (fc = 1 kHz); 4 stages, mix 1: notches at fc*tan(22.5) = 414 Hz and fc*tan(67.5) = 2414 Hz
        auto gain = [&] (double hz)
        {
            Phaser p; p.prepare (sr); auto in = sine (hz, 0.5, 48000); std::vector<float> out (in.size());
            for (size_t i = 0; i < in.size(); ++i) { float l = in[i], r = in[i]; p.process (l, r, 0, 1.f, 1.f, 0.f, 0.f); out[i] = l; }
            return amp (out, hz, 24000, 24000) / 0.5;
        };
        const double gn = gain (414.0), gp = gain (1000.0), gn2 = gain (2414.0);
        std::printf ("  gain at 414 Hz %.3f, 1 kHz %.3f, 2414 Hz %.3f\n", gn, gp, gn2);
        check ("deep notch at 414 Hz (< -20 dB)", gn < 0.1);
        check ("deep notch at 2414 Hz (< -20 dB)", gn2 < 0.1);
        check ("passes 1 kHz at full level", std::fabs (gp - 1.0) < 0.05);
        Phaser off; off.prepare (sr); float l = 0.3f, r = -0.2f; off.process (l, r, 1, 0.f, 1.f, 1.f, 0.5f);
        check ("mix 0 is bit-transparent", l == 0.3f && r == -0.2f);
        // stability + stereo movement on noise, max feedback, every stage count
        unsigned rs = 1; bool fin = true; double corr = 0, eL = 0, eR = 0;
        for (int st = 0; st < 4; ++st)
        {
            Phaser p; p.prepare (sr);
            for (int i = 0; i < 48000 * 6; ++i)
            {
                rs = rs * 1664525u + 1013904223u; const float x = ((float) (rs >> 8) / 8388608.f - 1.f) * 0.5f;
                float a = x, b = x; p.process (a, b, st, 1.f, 0.7f, 1.f, 0.9f);
                if (! std::isfinite (a) || ! std::isfinite (b) || std::fabs (a) > 20.f || std::fabs (b) > 20.f) fin = false;
                if (st == 3 && i > 48000) { corr += (double) a * b; eL += (double) a * a; eR += (double) b * b; }
            }
        }
        check ("finite and bounded at feedback 0.9 (4/6/8/12 stages)", fin);
        std::printf ("  L/R correlation of a mono source = %.2f\n", corr / std::sqrt (eL * eR));
        check ("left/right notches sweep differently (correlation < 0.9)", corr / std::sqrt (eL * eR) < 0.9);
    }

    std::printf ("RingMod\n");
    {
        RingMod rm; rm.prepare (sr); auto in = sine (3000.0, 0.5, 48000); std::vector<float> o (in.size());
        for (size_t i = 0; i < in.size(); ++i) { float l = in[i], r = in[i]; rm.process (l, r, 1.f, 1000.0); o[i] = l; }
        const double a2 = amp (o, 2000.0, 12000, 24000), a4 = amp (o, 4000.0, 12000, 24000), a3 = amp (o, 3000.0, 12000, 24000);
        std::printf ("  2 kHz %.3f  4 kHz %.3f  3 kHz %.3f\n", a2, a4, a3);
        check ("3 kHz x 1 kHz -> sidebands at 2 kHz and 4 kHz (0.25 each)", std::fabs (a2 - 0.25) < 0.03 && std::fabs (a4 - 0.25) < 0.03);
        check ("original 3 kHz suppressed", a3 < 0.02);
        RingMod z; z.prepare (sr); float l = 0.4f, r = 0.1f; z.process (l, r, 0.f, 500.0);
        check ("mix 0 is bit-transparent", l == 0.4f && r == 0.1f);
    }

    std::printf ("BitCrush\n");
    {
        BitCrush b; std::set<float> vals; bool runs = true;
        for (int i = 0; i < 4000; ++i) { float l = (float) (i - 2000) / 2000.f, r = l; b.process (l, r, 4, 1); vals.insert (l); }
        std::printf ("  4-bit: %zu distinct output levels\n", vals.size());
        check ("4 bits -> at most 16 distinct levels", vals.size() <= 16 && vals.size() >= 8);
        BitCrush d; float prev = 0; int run = 0;
        for (int i = 0; i < 400; ++i) { float l = std::sin (0.05f * (float) i), r = l; d.process (l, r, 16, 4); if (i % 4 != 0 && l != prev) runs = false; if (i % 4 == 0) prev = l; (void) run; }
        check ("rate reduction holds each value for 4 samples", runs);
        BitCrush off; float l = 0.123456f, r = -0.654321f; off.process (l, r, 16, 1);
        check ("16 bits / no reduction is bit-exact", l == 0.123456f && r == -0.654321f);
    }
    std::printf (fails ? "FAILED (%d)\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
