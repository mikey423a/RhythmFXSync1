// Objective checks for RhythmDSP2.h. Build: g++ -std=c++17 -O2 Tests/dsp2_test.cpp -ISource -o dsp2_test && ./dsp2_test
#include "RhythmDSP2.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace rfx;
static const double sr = 48000.0;

static double goertzel (const std::vector<float>& v, size_t a, size_t n, double hz)      // amplitude of a sine at hz
{
    double c = 0, s = 0;
    for (size_t i = 0; i < n; ++i) { const double ph = 2.0 * kPi * hz * (double) (a + i) / sr; c += v[a + i] * std::cos (ph); s += v[a + i] * std::sin (ph); }
    return 2.0 * std::sqrt (c * c + s * s) / (double) n;
}
static double rmsOf (const std::vector<float>& v, size_t a, size_t n) { double e = 0; for (size_t i = a; i < a + n; ++i) e += (double) v[i] * v[i]; return std::sqrt (e / (double) n); }
static double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-12)); }
static std::vector<float> sine (double hz, double amp, size_t n) { std::vector<float> v (n); for (size_t i = 0; i < n; ++i) v[i] = (float) (amp * std::sin (2.0 * kPi * hz * (double) i / sr)); return v; }
static std::vector<float> noise (size_t n, float amp, unsigned seed = 1)
{
    std::vector<float> v (n); unsigned s = seed;
    for (auto& x : v) { s = s * 1664525u + 1013904223u; x = amp * ((float) (s >> 8) / 8388608.f - 1.f); }
    return v;
}
static double corr (const std::vector<float>& a, const std::vector<float>& b, size_t from)
{
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = from; i < a.size(); ++i) { ab += (double) a[i] * b[i]; aa += (double) a[i] * a[i]; bb += (double) b[i] * b[i]; }
    return ab / std::sqrt (aa * bb + 1e-30);
}

int main()
{
    int fails = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("  %-58s %s\n", what, ok ? "ok" : "FAIL"); if (! ok) ++fails; };

    // ---------------- EQ
    {
        std::printf ("EQ\n");
        auto gainDb = [&] (double hz, float lo, float mid, float midHz, float hi)
        {
            Eq3 e; e.setup (sr, lo, mid, midHz, hi);
            auto x = sine (hz, 0.3, 48000); std::vector<float> y (x.size());
            for (size_t i = 0; i < x.size(); ++i) { float l = x[i], r = x[i]; e.process (l, r); y[i] = l; }
            return db (rmsOf (y, 24000, 20000) / rmsOf (x, 24000, 20000));
        };
        check (std::fabs (gainDb (30, 6, 0, 1000, 0) - 6.0) < 1.0, "low shelf +6 dB at 30 Hz");
        check (std::fabs (gainDb (4000, 6, 0, 1000, 0)) < 0.7, "low shelf leaves 4 kHz alone");
        check (std::fabs (gainDb (1000, 0, 6, 1000, 0) - 6.0) < 0.3, "mid bell +6 dB at its centre");
        check (std::fabs (gainDb (12000, 0, 0, 1000, 6) - 6.0) < 1.0, "high shelf +6 dB at 12 kHz");
        check (std::fabs (gainDb (1000, 0, 0, 1000, 0)) < 0.01, "flat settings are transparent");
    }

    // ---------------- oversampler latency + transparency
    int latency = -1;
    {
        std::printf ("Oversampler\n");
        Oversampler4 o; o.prepare();
        auto x = sine (1000, 0.5, 8000); std::vector<float> y (x.size());
        for (size_t i = 0; i < x.size(); ++i) { float h[4]; o.up (x[i], h); y[i] = o.down (h); }
        double best = 1e9;
        for (int L = 30; L < 40; ++L)
        {
            double e = 0; for (size_t i = 4000; i < 7000; ++i) e += std::pow ((double) y[i + (size_t) L] - x[i], 2.0);
            if (e < best) { best = e; latency = L; }
        }
        std::printf ("  measured latency = %d samples, residual rms = %.2e\n", latency, std::sqrt (best / 3000.0));
        check (latency == Oversampler4::kLatency, "latency equals kLatency (35 samples)");
        check (std::sqrt (best / 3000.0) < 2e-3, "1 kHz passes through unchanged (> 54 dB below signal)");
        auto hi = sine (16000, 0.5, 8000); std::vector<float> yh (hi.size());
        Oversampler4 o2; o2.prepare();
        for (size_t i = 0; i < hi.size(); ++i) { float h[4]; o2.up (hi[i], h); yh[i] = o2.down (h); }
        check (std::fabs (db (rmsOf (yh, 4000, 3000) / rmsOf (hi, 4000, 3000))) < 0.5, "16 kHz passband within 0.5 dB");
    }

    // ---------------- drive: aliasing + character
    {
        std::printf ("HiDrive\n");
        const size_t N = 48000;
        auto x = sine (6200.0, 0.9, N);
        std::vector<float> naive (N), os (N);
        HiDrive d; d.prepare();
        for (size_t i = 0; i < N; ++i)
        {
            naive[i] = std::tanh (x[i] * 30.f) * 0.8f;
            float l = x[i], r = x[i]; d.process (l, r, HiDrive::Soft, 1.f, 1.f, sr); os[i] = l;
        }
        auto aliasDb = [&] (const std::vector<float>& v)
        {
            double a = 0;                                            // inharmonic aliases of 6.2 kHz harmonics
            for (double hz : { 4600.0, 7800.0, 17000.0, 20200.0 }) a += std::pow (goertzel (v, 12000, 24000, hz), 2.0);
            return db (std::sqrt (a) / goertzel (v, 12000, 24000, 6200.0));
        };
        const double an = aliasDb (naive), ao = aliasDb (os);
        std::printf ("  alias level vs fundamental: naive %.1f dB, oversampled %.1f dB\n", an, ao);
        check (ao < an - 20.0, "oversampling cuts aliasing by more than 20 dB");
        int bad = 0;
        for (int t = 0; t < 4; ++t)
        {
            HiDrive e; e.prepare(); auto n = noise (48000, 0.8f, 7 + (unsigned) t);
            for (float v : n) { float l = v, r = v; e.process (l, r, t, 1.f, 0.5f, sr); if (! std::isfinite (l) || std::fabs (l) > 2.f) ++bad; }
        }
        check (bad == 0, "all four types finite and bounded at maximum drive");
        HiDrive e; e.prepare(); auto cl = sine (440, 0.4, 48000); std::vector<float> y (cl.size());
        for (size_t i = 0; i < cl.size(); ++i) { float l = cl[i], r = cl[i]; e.process (l, r, HiDrive::Tube, 0.f, 1.f, sr); y[i] = l; }
        check (std::fabs (db (rmsOf (y, 24000, 20000) / rmsOf (cl, 24000 - 35, 20000))) < 0.3, "drive at 0 is transparent");
        HiDrive tb; tb.prepare(); std::vector<float> yt (cl.size());
        for (size_t i = 0; i < cl.size(); ++i) { float l = cl[i], r = cl[i]; tb.process (l, r, HiDrive::Tube, 0.8f, 1.f, sr); yt[i] = l; }
        check (goertzel (yt, 24000, 24000, 880.0) > 0.01, "tube adds even harmonics (880 Hz present)");
        double mean = 0; for (size_t i = 24000; i < yt.size(); ++i) mean += yt[i]; mean /= (double) (yt.size() - 24000);
        check (std::fabs (mean) < 0.01, "DC blocker removes the tube curve's offset");
    }

    // ---------------- pitch shifter
    {
        std::printf ("PitchShifter\n");
        auto crossings = [&] (const std::vector<float>& v, size_t a, size_t n)
        { int c = 0; for (size_t i = a + 1; i < a + n; ++i) if (v[i - 1] <= 0.f && v[i] > 0.f) ++c; return (double) c * sr / (double) n; };
        auto shifted = [&] (double hz, float ratio)
        {
            PitchShifter p; p.prepare (sr); auto x = sine (hz, 0.5, 96000); std::vector<float> y (x.size());
            for (size_t i = 0; i < x.size(); ++i) y[i] = p.process (x[i], ratio);
            return y;
        };
        auto up = shifted (440, 2.f);
        check (std::fabs (crossings (up, 48000, 48000) - 880.0) < 18.0, "octave up: 440 Hz -> ~880 Hz");
        auto dn = shifted (440, 0.5f);
        check (std::fabs (crossings (dn, 48000, 48000) - 220.0) < 8.0, "octave down: 440 Hz -> ~220 Hz");
        const float r12 = std::pow (2.f, 12.f / 1200.f);
        auto cents = shifted (440, r12);
        const double fe = crossings (cents, 24000, 72000), want = 440.0 * r12;
        std::printf ("  +12 cents: measured %.2f Hz, expected %.2f Hz\n", fe, want);
        check (std::fabs (fe - want) < 1.5, "+12 cents lands within 1.5 Hz");
        { double r = rmsOf (up, 48000, 48000) / (0.5 / std::sqrt (2.0)); std::printf ("  octave-up level vs input = %.2f\n", r); check (r > 0.8 && r < 1.2, "octave-up keeps its level (+/-20%)"); }
        PitchShifter u; u.prepare (sr); auto x1 = sine (440, 0.5, 48000); std::vector<float> y1 (x1.size());
        for (size_t i = 0; i < x1.size(); ++i) y1[i] = u.process (x1[i], 1.f);
        check (std::fabs (db (rmsOf (y1, 24000, 20000) / rmsOf (x1, 24000, 20000))) < 0.5, "ratio 1.0 passes level unchanged");
    }

    // ---------------- thicken: stereo decorrelation
    {
        std::printf ("PitchFx Thicken (double tracking)\n");
        PitchFx f; f.prepare (sr); auto x = noise (96000, 0.3f, 3);
        std::vector<float> l (x.size()), r (x.size());
        for (size_t i = 0; i < x.size(); ++i) { float a = x[i], b = x[i]; f.process (a, b, PitchFx::Thicken, 0.7f, 14.f); l[i] = a; r[i] = b; }
        const double c = corr (l, r, 24000);
        std::printf ("  L/R correlation = %.2f\n", c);
        check (c < 0.85, "dry centre + two side doubles widen a mono source");
        check (db (rmsOf (l, 24000, 60000) / rmsOf (x, 24000, 60000)) < 3.0, "no big level jump");
    }

    // ---------------- reverb
    {
        std::printf ("FdnReverb\n");
        FdnReverb v; v.prepare (sr); v.setParams (0.5f, 0.4f, 0.f, 0.f);
        const double rt60 = 0.25 * std::pow (2.0, 5.2 * 0.5);
        const size_t n = (size_t) (sr * (rt60 * 2.5));
        std::vector<float> l (n), r (n);
        for (size_t i = 0; i < n; ++i) { float a, b; v.process (i == 0 ? 1.f : 0.f, i == 0 ? 1.f : 0.f, a, b); l[i] = a; r[i] = b; }
        const size_t w = (size_t) (0.1 * sr), t1 = (size_t) (0.5 * rt60 * sr), t2 = (size_t) (1.5 * rt60 * sr);
        const double drop = db (rmsOf (l, t1, w)) - db (rmsOf (l, t2, w));
        std::printf ("  RT60 setting %.2f s: level drop over one RT60 = %.1f dB (60 expected, damping adds a little)\n", rt60, drop);
        check (drop > 45.0 && drop < 95.0, "decay time matches the Size setting (45..95 dB per RT60)");
        const double c = corr (l, r, (size_t) (0.05 * sr));
        std::printf ("  L/R correlation of the tail = %.2f\n", c);
        check (std::fabs (c) < 0.35, "tail is decorrelated left/right (wide)");
        bool fin = true; for (float x : l) if (! std::isfinite (x)) fin = false;
        check (fin, "output finite");

        FdnReverb big; big.prepare (sr); big.setParams (1.f, 0.f, 0.f, 1.f);                    // longest tail, no damping, max shimmer
        std::vector<float> o (size_t (sr * 25)); auto nz = noise (size_t (sr * 1.0), 0.5f, 5);
        for (size_t i = 0; i < o.size(); ++i) { float a, b; big.process (i < nz.size() ? nz[i] : 0.f, i < nz.size() ? nz[i] : 0.f, a, b); o[i] = a; }
        bool stable = true; for (float x : o) if (! std::isfinite (x) || std::fabs (x) > 50.f) stable = false;
        const double early = rmsOf (o, (size_t) (sr * 1.2), 24000), late = rmsOf (o, (size_t) (sr * 22), 24000);
        std::printf ("  max shimmer + max size: level at 1.2 s %.1f dB, at 22 s %.1f dB\n", db (early), db (late));
        check (stable && late < early * 0.5, "shimmer feedback is stable and still decays");

        FdnReverb s0, s1; s0.prepare (sr); s1.prepare (sr);
        s0.setParams (0.7f, 0.2f, 0.f, 0.f); s1.setParams (0.7f, 0.2f, 0.f, 1.f);
        auto burst = sine (440.0, 0.5, (size_t) (sr * 0.5));
        std::vector<float> a0 (size_t (sr * 4)), a1 (a0.size());
        for (size_t i = 0; i < a0.size(); ++i) { float x = i < burst.size() ? burst[i] : 0.f, a, b; s0.process (x, x, a, b); a0[i] = a; s1.process (x, x, a, b); a1[i] = a; }
        const double g0 = goertzel (a0, 72000, 96000, 880.0), g1 = goertzel (a1, 72000, 96000, 880.0);
        std::printf ("  880 Hz in the tail: shimmer off %.5f, on %.5f\n", g0, g1);
        check (g1 > 4.0 * g0, "shimmer puts energy an octave above the input");
        std::printf ("  (calibration) wet RMS / input RMS on noise at Size 0.5: ");
        FdnReverb cal; cal.prepare (sr); cal.setParams (0.5f, 0.4f, 0.f, 0.f); auto cn = noise (size_t (sr * 6), 0.2f, 11);
        std::vector<float> cw (cn.size());
        for (size_t i = 0; i < cn.size(); ++i) { float a, b; cal.process (cn[i], cn[i], a, b); cw[i] = a; }
        std::printf ("%.2f\n", rmsOf (cw, 96000, 100000) / rmsOf (cn, 96000, 100000));
    }

    // ---------------- spatial imager
    {
        std::printf ("Spatial\n");
        auto x = noise (96000, 0.3f, 9); std::vector<float> l (x.size()), r (x.size());
        Spatial s; s.prepare (sr);
        for (size_t i = 0; i < x.size(); ++i) { float a = x[i], b = x[i]; s.process (a, b, 1.f, 1.f, 0.f); l[i] = a; r[i] = b; }
        const double c = corr (l, r, 12000);
        std::printf ("  mono source, space 1: L/R correlation = %.2f, level change %.2f dB\n", c, db (rmsOf (l, 12000, 60000) / rmsOf (x, 12000, 60000)));
        check (c < 0.6, "decorrelation widens a mono source");
        check (std::fabs (db (rmsOf (l, 12000, 60000) / rmsOf (x, 12000, 60000))) < 1.5, "decorrelation preserves level (+/-1.5 dB)");
        Spatial m; m.prepare (sr); double maxd = 0; auto st = noise (20000, 0.4f, 21), st2 = noise (20000, 0.4f, 22);
        for (size_t i = 0; i < st.size(); ++i) { float a = st[i], b = st2[i]; m.process (a, b, 0.f, 0.f, 0.f); maxd = std::max (maxd, (double) std::fabs (a - b)); }
        check (maxd < 1e-6, "width 0 collapses to mono");
        Spatial id; id.prepare (sr); double md = 0;
        for (size_t i = 0; i < st.size(); ++i) { float a = st[i], b = st2[i]; id.process (a, b, 1.f, 0.f, 0.f); md = std::max (md, (double) (std::fabs (a - st[i]) + std::fabs (b - st2[i]))); }
        check (md < 1e-9, "neutral settings are bit-transparent");
        Spatial w; w.prepare (sr); double sideIn = 0, sideOut = 0;
        for (size_t i = 0; i < st.size(); ++i) { float a = st[i], b = st2[i]; sideIn += std::pow (0.5 * (a - b), 2.0); w.process (a, b, 1.8f, 0.f, 0.f); sideOut += std::pow (0.5 * (a - b), 2.0); }
        check (std::fabs (db (std::sqrt (sideOut / sideIn)) - db (1.8)) < 0.1, "width 1.8 scales the side signal by 1.8");
        Spatial er; er.prepare (sr); std::vector<float> ir (6000);
        for (size_t i = 0; i < ir.size(); ++i) { float a = i == 0 ? 1.f : 0.f, b = a; er.process (a, b, 1.f, 0.f, 1.f); ir[i] = a; }
        int taps = 0; for (size_t i = 100; i < ir.size(); ++i) if (std::fabs (ir[i]) > 0.01f && std::fabs (ir[i - 1]) <= 0.01f) ++taps;
        check (rmsOf (ir, 100, 5900) > 0.0005, "early reflections are produced after the direct sound");
    }

    // ---------------- limiter
    {
        std::printf ("Limiter\n");
        check (softLimit (0.5f) == 0.5f && softLimit (-0.79f) == -0.79f, "transparent below 0.8");
        check (softLimit (10.f) <= 1.f && softLimit (10.f) > 0.99f && softLimit (-10.f) >= -1.f && softLimit (1e6f) <= 1.f, "large peaks approach but never exceed 1.0");
        bool mono = true; float prev = softLimit (0.f); for (float v = 0.f; v < 5.f; v += 0.01f) { const float y = softLimit (v); if (y < prev - 1e-6f) mono = false; prev = y; }
        check (mono, "monotonic (no fold-back)");
    }

    std::printf (fails == 0 ? "ALL OK\n" : "%d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
