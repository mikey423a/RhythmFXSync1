// Measured quality checks for the "pro pedal" upgrades. Not "does it run" tests: each check states a number a good design should hit.
//   g++ -std=c++17 -O2 Tests/quality_test.cpp -ISource -o t5 && ./t5
// These cannot say how it SOUNDS. They guard the properties that separate careful DSP from cheap DSP (tuning accuracy,
// smooth reverb tails, clean parameter changes) so a later edit cannot quietly undo them.
#include "RhythmDSP.h"
#include "RhythmDSP2.h"
#include <cstdio>
#include <random>
#include <set>
using namespace rfx;
static int fails = 0;
static void check (const char* what, bool ok) { std::printf ("  %-74s %s\n", what, ok ? "ok" : "FAIL"); if (! ok) ++fails; }
static const double sr = 48000.0;

static double dftPower (const std::vector<float>& v, double hz, size_t a, size_t n)         // Hann-windowed single-bin power
{
    double c = 0, s = 0;
    for (size_t i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * kPi * (double) i / (double) n), ph = 2 * kPi * hz * (double) (a + i) / sr; c += w * v[a + i] * std::cos (ph); s += w * v[a + i] * std::sin (ph); }
    return c * c + s * s;
}
static double peakFreq (const std::vector<float>& v, double center, size_t a, size_t n)       // coarse 1 Hz scan, then 0.05 Hz refinement
{
    double best = 0, bf = center;
    for (double f = center * 0.96; f < center * 1.04; f += 1.0) { const double p = dftPower (v, f, a, n); if (p > best) { best = p; bf = f; } }
    const double c0 = bf; best = 0;
    for (double f = c0 - 1.0; f <= c0 + 1.0; f += 0.05) { const double p = dftPower (v, f, a, n); if (p > best) { best = p; bf = f; } }
    return bf;
}
static double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }
static double rmsOf (const std::vector<float>& v, size_t a, size_t n) { double e = 0; for (size_t i = a; i < a + n; ++i) e += (double) v[i] * v[i]; return std::sqrt (e / (double) n); }
// how far any spectral bin sticks out above the median of its neighbours (+/- 1/6 octave), 99.5th percentile, in dB (tilt-proof)
static double ringing (const std::vector<float>& L, size_t a, size_t n = 1 << 15)
{
    std::vector<double> f, mag; const double df = sr / (double) n;
    for (double fr = 250; fr < 6500; fr += df)
    {
        double c = 0, s = 0;
        for (size_t i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * kPi * (double) i / (double) n), ph = 2 * kPi * fr * (double) i / sr; c += w * L[a + i] * std::cos (ph); s += w * L[a + i] * std::sin (ph); }
        f.push_back (fr); mag.push_back (std::sqrt (c * c + s * s) + 1e-30);
    }
    std::vector<double> dev;
    for (size_t i = 0; i < f.size(); ++i)
    {
        std::vector<double> nb; for (size_t j = 0; j < f.size(); ++j) if (f[j] > f[i] * 0.891 && f[j] < f[i] * 1.122) nb.push_back (mag[j]);
        std::sort (nb.begin(), nb.end()); dev.push_back (20 * std::log10 (mag[i] / nb[nb.size() / 2]));
    }
    std::sort (dev.begin(), dev.end()); return dev[(size_t) ((double) dev.size() * 0.995)];
}

int main()
{
    std::printf("pitch shifter\n");
    {
        const double notes[] = { 82.4, 110.0, 196.0, 247.0, 330.0, 440.0, 659.0, 880.0 };
        double worst = 0;
        for (double ratio : { 2.0, 1.4983, 1.0595, 0.5 })
            for (double f : notes)
            {
                PitchShifter p; p.prepare (sr, 55.0); const size_t N = 96000; std::vector<float> o (N);
                for (size_t i = 0; i < N; ++i) o[i] = p.process (0.5f * (float) std::sin (2 * kPi * f * (double) i / sr), (float) ratio);
                worst = std::max (worst, std::fabs (cents (peakFreq (o, f * ratio, 24000, 48000), f * ratio)));
            }
        std::printf("  worst tuning error over 8 notes (82-880 Hz) x 4 intervals: %.2f cents  (a plain two-tap shifter: 50+)\n", worst);
        check ("every note comes out within 3 cents of the intended pitch", worst <= 3.0);

        PitchShifter p; p.prepare (sr); const size_t N = 96000; std::vector<float> o (N);
        for (size_t i = 0; i < N; ++i) o[i] = p.process (0.5f * (float) std::sin (2 * kPi * 330.0 * (double) i / sr), 2.f);
        double mn = 1e9, mx = 0; for (size_t s = 24000; s + 960 < 72000; s += 960) { const double r = rmsOf (o, s, 960); mn = std::min (mn, r); mx = std::max (mx, r); }
        std::printf("  level ripple over 1 s: %.2f dB\n", 20 * std::log10 (mx / mn));
        check ("output level is steady (ripple under 0.5 dB, no tremolo-like wobble)", 20 * std::log10 (mx / mn) < 0.5);

        PitchShifter q; q.prepare (sr); std::vector<float> c (N);            // equal-tempered A major triad, an octave up
        const double tri[3] = { 220.0, 277.18, 329.63 };
        for (size_t i = 0; i < N; ++i) { const double t = (double) i / sr; c[i] = q.process (0.18f * (float) (std::sin (2 * kPi * tri[0] * t) + std::sin (2 * kPi * tri[1] * t) + std::sin (2 * kPi * tri[2] * t)), 2.f); }
        double worstC = 0; for (double f : tri) worstC = std::max (worstC, std::fabs (cents (peakFreq (c, 2 * f, 24000, 48000), 2 * f)));
        std::printf("  A major triad up an octave, worst partial: %.2f cents\n", worstC);
        check ("a chord keeps every note within 10 cents", worstC <= 10.0);
        PitchShifter z; z.prepare (sr); float m = 0; for (int i = 0; i < 20000; ++i) m = std::max (m, std::fabs (z.process (0.f, 1.5f)));
        check ("silence in gives exact silence out", m == 0.f);
    }

    std::printf("reverb\n");
    {
        const size_t N = (size_t) (sr * 3); std::mt19937 g (5); std::normal_distribution<float> nd (0.f, 1.f);
        std::vector<float> ideal (N); for (size_t i = 0; i < N; ++i) ideal[i] = nd (g) * (float) std::exp (-6.9078 * (double) i / sr / 1.85);
        const double ref = ringing (ideal, (size_t) (sr * 0.4));
        std::printf("  ringing of ideal decaying noise: %.1f dB (a perfect smooth tail)\n", ref);
        bool smooth = true, dense = true;
        for (float size : { 0.3f, 0.6f, 0.9f })
        {
            FdnReverb r; r.prepare (sr); r.setParams (size, 0.4f, 0.f, 0.f); std::vector<float> L (N);
            for (size_t i = 0; i < N; ++i) { float wl, wr; r.process (i == 0 ? 1.f : 0.f, i == 0 ? 1.f : 0.f, wl, wr); L[i] = wl; }
            const double rg = ringing (L, (size_t) (sr * 0.4)); double tNed = -1; const size_t W = (size_t) (0.02 * sr);
            for (size_t s = 0; s + W < N && tNed < 0; s += W / 4) { const double sd = rmsOf (L, s, W); if (sd < 1e-9) continue; size_t cnt = 0; for (size_t i = s; i < s + W; ++i) if (std::fabs (L[i]) > sd) ++cnt; if (((double) cnt / (double) W) / 0.3173 > 0.9) tNed = (double) s / sr * 1000.0; }
            std::printf("  size %.1f: ringing %.1f dB (%+.1f vs ideal), echo density 0.9 at %.0f ms\n", size, rg, rg - ref, tNed);
            if (rg - ref > 2.0) { smooth = false; }
            if (tNed < 0 || tNed > 110.0) { dense = false; }
        }
        check ("tail is smooth: within 2 dB of an ideal noise tail at every size (was +3)", smooth);
        check ("tail is dense within 110 ms at every size (was 150-230 ms)", dense);
        std::mt19937 g2 (3); std::normal_distribution<float> n2 (0.f, 0.3f); FdnReverb r; r.prepare (sr); r.setParams (0.6f, 0.4f, 0.f, 0.f); double ei = 0, eo = 0;
        for (int i = 0; i < (int) (sr * 12); ++i) { const float x = n2 (g2), y = n2 (g2); float wl, wr; r.process (x, y, wl, wr); if (i > (int) sr * 6) { ei += (double) x * x; eo += (double) wl * wl; } }
        const double lvl = std::sqrt (eo / ei); std::printf("  wet / dry level at size 0.6: %.3f (design value 0.727)\n", lvl);
        check ("output level unchanged from the original design (+/- 10 %)", std::fabs (lvl / 0.727 - 1.0) < 0.10);
    }

    std::printf("delay voicing, wow, ducking\n");
    {
        auto run = [&] (const std::vector<float>& in, const DelayVoice& v, float fb, float mix, float dly = 4800.f) { StereoDelay d; d.prepare (sr); std::vector<float> out; out.reserve (in.size());
            for (float x : in) { float l = x, r = x; d.process (l, r, dly, fb, mix, false, FbFxParams(), v); out.push_back (l - x); } return out; };
        auto tone = [&] (double hz, double a, size_t n) { std::vector<float> v (n); for (size_t i = 0; i < n; ++i) v[i] = (float) (a * std::sin (2 * kPi * hz * (double) i / sr)); return v; };
        const size_t N = 48000;
        auto hfDb = [&] (int voice) { DelayVoice v; v.voice = voice; auto lo = run (tone (500, 0.2, N), v, 0.f, 1.f), hi = run (tone (8000, 0.2, N), v, 0.f, 1.f); return 20 * std::log10 (rmsOf (hi, 12000, 24000) / rmsOf (lo, 12000, 24000)); };
        const double dDig = hfDb (0), dTape = hfDb (1), dBbd = hfDb (2);
        std::printf("  8 kHz level relative to 500 Hz in one repeat:  digital %.1f dB   tape %.1f dB   analog %.1f dB\n", dDig, dTape, dBbd);
        check ("tape repeats are darker than digital (by at least 4 dB at 8 kHz)", dTape < dDig - 4.0);
        check ("analog repeats are darker still than tape (by at least 6 dB)", dBbd < dTape - 6.0);
        auto thd = [&] (int voice) { DelayVoice v; v.voice = voice; auto o = run (tone (1000, 0.8, N), v, 0.f, 1.f); return std::sqrt (dftPower (o, 3000, 12000, 24000)) / std::sqrt (dftPower (o, 1000, 12000, 24000)); };
        std::printf("  3rd harmonic at 0.8 amplitude:  digital %.4f   tape %.4f   analog %.4f\n", thd (0), thd (1), thd (2));
        check ("digital is clean, tape and analog add gentle harmonics", thd (0) < 0.001 && thd (1) > 0.01 && thd (2) > 0.005);
        bool stable = true; std::mt19937 g (9); std::normal_distribution<float> nd (0.f, 0.3f); std::vector<float> nz (48000 * 6); for (auto& x : nz) x = nd (g);
        for (int voice = 0; voice < 3; ++voice) { DelayVoice v; v.voice = voice; v.wow = 1.f; v.duck = 0.5f; for (float y : run (nz, v, 0.95f, 1.f)) if (! std::isfinite (y) || std::fabs (y) > 10.f) stable = false; }
        check ("stable at maximum feedback with every voicing, wow and ducking on", stable);

        // wow: track the pitch of the repeat of a steady 1 kHz tone by I/Q demodulation, peak-to-peak deviation in cents
        auto pitchSwing = [&] (float wow) {
            DelayVoice v; v.wow = wow; auto o = run (tone (1000, 0.4, 96000), v, 0.f, 1.f); const size_t win = 960; std::vector<double> ph;
            for (size_t s = 12000; s + win < o.size(); s += 240) { double c = 0, q = 0; for (size_t i = 0; i < win; ++i) { const double a = 2 * kPi * 1000.0 * (double) (s + i) / sr; c += o[s + i] * std::cos (a); q -= o[s + i] * std::sin (a); } ph.push_back (std::atan2 (q, c)); }
            double mn = 1e9, mx = -1e9; for (size_t i = 1; i < ph.size(); ++i) { double d = ph[i] - ph[i - 1]; while (d > kPi) d -= 2 * kPi; while (d < -kPi) d += 2 * kPi; const double hz = d / (2 * kPi) / (240.0 / sr); const double ct = cents (1000.0 + hz, 1000.0); mn = std::min (mn, ct); mx = std::max (mx, ct); }
            return mx - mn; };
        const double s0 = pitchSwing (0.f), s1 = pitchSwing (1.f);
        std::printf("  pitch swing of a repeat: wow 0 -> %.1f cents, wow 1 -> %.1f cents peak-to-peak\n", s0, s1);
        check ("wow off: pitch is steady (under 1 cent)", s0 < 1.0);
        check ("wow full: audible but musical drift (4 to 30 cents peak-to-peak)", s1 > 4.0 && s1 < 30.0);

        // ducking: play for 0.9 s with a 0.5 s delay, then stop. The repeat is pulled down while you play and returns once you stop.
        std::vector<float> in ((size_t) (sr * 1.6), 0.f); auto t = tone (440, 0.5, (size_t) (0.9 * sr)); std::copy (t.begin(), t.end(), in.begin());
        DelayVoice off, on; on.duck = 1.f; auto a = run (in, off, 0.f, 1.f, 24000.f), b = run (in, on, 0.f, 1.f, 24000.f);
        auto ratio = [&] (double t0, double len) { return rmsOf (b, (size_t) (t0 * sr), (size_t) (len * sr)) / rmsOf (a, (size_t) (t0 * sr), (size_t) (len * sr)); };
        const double during = ratio (0.65, 0.2), after = ratio (1.33, 0.06);
        std::printf("  duck 1: repeat level while playing %.0f %% of normal, 0.4 s after you stop %.0f %%\n", 100 * during, 100 * after);
        check ("ducking pulls the repeat down while you play (under 35 %)", during < 0.35);
        check ("and lets it back within half a second of stopping (over 85 %)", after > 0.85);
    }

    std::printf("parameter smoothing\n");
    {
        Smoothed s; s.setTime (sr, 20.f); s.setTarget (0.7f); check ("the first target is taken at once (no ramp from zero at load)", s.v == 0.7f);
        s.setTarget (1.f); double prev = s.v; bool mono = true, over = false; int t99 = -1;
        for (int i = 0; i < 48000; ++i) { const float v = s.next(); if (v < prev - 1e-9) mono = false; if (v > 1.0001f) over = true; if (t99 < 0 && v >= 0.7f + 0.3f * 0.99f) t99 = i; prev = v; }
        std::printf("  20 ms smoother reaches 99 %% of a step after %.0f ms\n", t99 * 1000.0 / sr);
        check ("rises monotonically, never overshoots", mono && ! over);
        check ("settles within 5 time constants (100 ms)", t99 >= 0 && t99 * 1000.0 / sr < 100.0);
        auto maxJump = [&] (bool smooth) { Smoothed g; g.setTime (sr, 20.f); g.setTarget (0.f); float last = 0.f, worst = 0.f;
            for (int i = 0; i < 4800; ++i) { if (i == 2427) g.setTarget (1.f); const float gain = smooth ? g.next() : (i >= 2427 ? 1.f : 0.f); const float y = gain * (float) std::sin (2 * kPi * 440.0 * i / sr); worst = std::max (worst, std::fabs (y - last)); last = y; } return worst; };
        const float jU = maxJump (false), jS = maxJump (true), natural = (float) (2 * kPi * 440.0 / sr);
        std::printf("  gain stepped 0 -> 1 at a waveform peak: largest sample-to-sample jump unsmoothed %.3f, smoothed %.4f (a plain 440 Hz sine moves %.4f per sample)\n", jU, jS, natural);
        check ("unsmoothed, the step is a click (jump over 10x a normal sample step)", jU > 10.f * natural);
        check ("smoothed, there is no click (jump within 1.2x a normal sample step)", jS < 1.2f * natural);
    }
    std::printf (fails ? "FAILED (%d)\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
