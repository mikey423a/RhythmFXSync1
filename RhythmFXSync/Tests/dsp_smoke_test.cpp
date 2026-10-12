// Standalone smoke test for RhythmDSP.h (no JUCE needed):  g++ -std=c++17 -O2 Tests/dsp_smoke_test.cpp -ISource
#include "RhythmDSP.h"
#include <cstdio>
using namespace rfx;
int main()
{
    const double sr = 48000.0; const double bpm = 120.0; const int N = (int) (sr * 4);
    StepClock fc, dc; Svf svf[2]; OnePole sm; sm.setTimeMs (20.f, sr);
    NoiseGate gate; gate.prepare (sr); EnvFollower envf; envf.prepare (sr);
    StereoDelay dly; dly.prepare (sr); Compressor comp; comp.prepare (sr);
    DrumMachine drums; drums.prepare (sr); Synth syn; syn.prepare (sr);
    syn.noteOn (45, 0.9f); syn.noteOn (52, 0.9f);
    const float pat[16] = { 0,.25f,.5f,.75f,1,.75f,.5f,.25f,0,.25f,.5f,.75f,1,.75f,.5f,.25f };
    double ppq = 0, inc = bpm / (60.0 * sr); double peak = 0, sum = 0; int bad = 0, trig = 0, steps = 0;
    for (int i = 0; i < N; ++i, ppq += inc)
    {
        auto f = fc.get (ppq, StepClock::stepsPerBeat (2), 0.2f, 16); if (f.fresh) ++steps;
        auto d = dc.get (ppq, 4.0, 0.2f, 16);
        if (d.fresh) { if (d.step % 4 == 0) { drums.trigger (DrumMachine::Kick, 1.f); ++trig; } if (d.step % 2) drums.trigger (DrumMachine::Hat, 1.f); if (d.step == 4 || d.step == 12) drums.trigger (DrumMachine::Snare, 1.f); }
        const float cv = sm.process (pat[f.step]);
        if ((i & 7) == 0) for (auto& s : svf) s.setup (200.f * std::pow (20.f, cv), 0.6f, sr);
        float gl = syn.process (0, 0.3f), gr = gl; gate.process (gl, gr, 0.5f, NoiseGate::thresholdLinear (0.5f)); (void) envf.process (gl);
        float x = driveProcess (gl, 0.3f);
        float L = svf[0].process (x, 0), R = svf[1].process (x, 0);
        float dr = drums.process(); L += dr; R += dr;
        comp.process (L, R, 0.4f);
        dly.process (L, R, (float) (0.375 * 60.0 / bpm * sr), 0.4f, 0.3f, true);
        if (! std::isfinite (L) || ! std::isfinite (R)) ++bad;
        peak = std::max (peak, (double) std::fabs (L)); sum += (double) L * L;
    }
    std::printf ("steps=%d kicks=%d peak=%.3f rms=%.3f nonFinite=%d\n", steps, trig, peak, std::sqrt (sum / N), bad);

    // ---- pan law: centre = unity both sides, hard L/R silences the other side, depth 0 = centre
    int panBad = 0; float gl, gr;
    panGains (0.5f, 1.f, gl, gr); if (std::fabs (gl - 1.f) > 1e-4f || std::fabs (gr - 1.f) > 1e-4f) ++panBad;
    panGains (0.f,  1.f, gl, gr); if (gl < 0.999f || gr > 1e-4f) ++panBad;
    panGains (1.f,  1.f, gl, gr); if (gr < 0.999f || gl > 1e-4f) ++panBad;
    panGains (1.f,  0.f, gl, gr); if (std::fabs (gl - 1.f) > 1e-4f || std::fabs (gr - 1.f) > 1e-4f) ++panBad;
    std::printf ("panBad=%d\n", panBad);

    // ---- 24 dB slope: two cascaded low-pass stages attenuate 8 kHz more than one
    auto toneRms = [&] (bool two)
    {
        Svf a, b; a.setup (1000.f, 0.f, sr); b.setup (1000.f, 0.f, sr);
        double acc = 0; const int M = 48000;
        for (int i = 0; i < M; ++i)
        {
            float x = std::sin (2.0 * kPi * 8000.0 * i / sr) > 0 ? 1.f : -1.f;
            x = (float) std::sin (2.0 * kPi * 8000.0 * i / sr);
            float y = a.process (x, 0); if (two) y = b.process (y, 0);
            if (i > 4800) acc += (double) y * y;
        }
        return std::sqrt (acc / (M - 4800));
    };
    const double r1 = toneRms (false), r2 = toneRms (true);
    const int slopeBad = (r2 < r1 * 0.5 && std::isfinite (r2)) ? 0 : 1;
    std::printf ("lp 8k rms 12dB=%.5f 24dB=%.5f slopeBad=%d\n", r1, r2, slopeBad);

    // ---- 32-step clock: every step 0..31 is visited, and lengths 1..32 wrap correctly
    int stepBad = 0;
    {
        StepClock c32; bool seen[32] = {};
        for (int i = 0; i < (int) (sr * 8); ++i) { auto r = c32.get ((double) i * bpm / (60.0 * sr), 4.0, 0.f, 32); if (r.step < 0 || r.step > 31) ++stepBad; else seen[r.step] = true; }
        for (bool b : seen) if (! b) ++stepBad;
        StepClock c1; for (int i = 0; i < 1000; ++i) if (c1.get (i * 0.01, 4.0, 0.f, 1).step != 0) ++stepBad;
    }
    std::printf ("stepBad=%d\n", stepBad);

    // ---- attack/decay envelope: reaches 1 after attack, decays to ~-60 dB after decay time
    int envBad = 0;
    {
        AdEnv e; e.setTimes (10.f, 100.f, sr); e.trigger();
        float v = 0; for (int i = 0; i < (int) (0.010 * sr); ++i) v = e.process();      // exactly the attack time
        if (v < 0.99f) ++envBad;
        for (int i = 0; i < (int) (0.1 * sr); ++i) v = e.process();       // exactly the decay time
        if (v > 0.0015f || v < 0.0005f) ++envBad;        // ~0.001 = -60 dB
    }
    std::printf ("envBad=%d\n", envBad);

    // ---- pluck detector: 4 plucks in 2 s -> 4 triggers, silence -> none
    int trigCount = 0, silentTrig = 0;
    {
        NoteTrigger nt; nt.prepare (sr);
        for (int i = 0; i < (int) (sr * 2); ++i)
        {
            const double t = (double) i / sr, tt = std::fmod (t - 0.1, 0.5);
            float x = (t >= 0.1 && tt >= 0.0) ? (float) (0.5 * std::exp (-tt / 0.3) * std::sin (2.0 * kPi * 196.0 * tt)) : 0.f;
            if (nt.process (std::fabs (x), 0.5f)) ++trigCount;
        }
        NoteTrigger q; q.prepare (sr);
        for (int i = 0; i < (int) sr; ++i) if (q.process (0.f, 1.f)) ++silentTrig;
    }
    std::printf ("trigCount=%d silentTrig=%d\n", trigCount, silentTrig);

    // ---- peak hold: captures the loudest sample of the pluck window and holds it
    int pkBad = 0;
    {
        PeakHold ph; ph.prepare (sr); ph.setRelease (10.f, sr); ph.arm();
        float v = 0; for (int i = 0; i < 4000; ++i) v = ph.process (i == 100 ? 0.3f : 0.1f);
        if (std::fabs (v - 0.6f) > 1e-4f) ++pkBad;                       // 0.3 * 2
        for (int i = 0; i < (int) sr; ++i) v = ph.process (0.f);
        if (std::fabs (v - 0.6f) > 1e-4f) ++pkBad;                       // still held after 1 s
        ph.setRelease (0.5f, sr); for (int i = 0; i < (int) sr; ++i) v = ph.process (0.f);
        if (v > 0.1f) ++pkBad;                                           // released
        ph.setValue (0.8f); if (std::fabs (ph.process (0.f) - 0.8f * ph.rel) > 1e-4f) ++pkBad;
    }
    std::printf ("pkBad=%d\n", pkBad);

    // ---- LFO: starts low, hits +1 at half cycle, stays within -1..1, random shape holds per cycle
    int lfoBad = 0;
    {
        Lfo l;
        for (int shp = 0; shp < 5; ++shp)
        {
            Lfo x; if (shp < 4 && x.get (shp, 0.0) > -0.999f) ++lfoBad;
            if (shp < 4 && x.get (shp, 0.5) < 0.999f && shp != 2) ++lfoBad;
            for (double c = 0; c < 4.0; c += 0.013) { const float v = x.get (shp, c); if (v < -1.001f || v > 1.001f) ++lfoBad; }
        }
        const float a = l.get (4, 0.1), b = l.get (4, 0.9), c = l.get (4, 1.1);
        if (a != b || a == c) ++lfoBad;
    }
    std::printf ("lfoBad=%d\n", lfoBad);

    // ---- vowel formants hit the table at the ends; quantizer only returns in-scale notes
    int vwBad = 0;
    {
        float fm[3]; Vowel::formants (0.f, fm); if (std::fabs (fm[0] - 800.f) > 0.5f || std::fabs (fm[2] - 2900.f) > 0.5f) ++vwBad;
        Vowel::formants (1.f, fm); if (std::fabs (fm[0] - 325.f) > 0.5f || std::fabs (fm[1] - 700.f) > 0.5f) ++vwBad;
        Vowel::formants (0.25f, fm); if (std::fabs (fm[0] - 400.f) > 0.5f) ++vwBad;                  // exactly "E"
        const int scales[4] = { 2, 3, 4, 5 };
        for (int sc : scales)
            for (int root = 0; root < 12; ++root)
                for (float fc = 60.f; fc < 4000.f; fc *= 1.037f)
                {
                    const float q = quantizeFreq (fc, sc, root);
                    const int n = (int) std::lround (69.0 + 12.0 * std::log2 (q / 440.0));
                    static const int mk[6] = { 0xFFF, 0xFFF, 0xAB5, 0x5AD, 0x295, 0x4A9 };
                    if (! ((mk[sc] >> ((((n - root) % 12) + 12) % 12)) & 1)) ++vwBad;
                    if (std::fabs (std::log2 (q / fc)) > 0.5 / 12.0 * 4.0) ++vwBad;                    // never moves > 2 semitones
                }
        if (quantizeFreq (123.f, 0, 0) != 123.f) ++vwBad;
        Vowel v; v.setup (0.3f, 0.5f, sr); float acc = 0; for (int i = 0; i < 4800; ++i) { float y = v.process (i % 50 < 25 ? 0.5f : -0.5f); if (! std::isfinite (y)) ++vwBad; acc += y * y; }
        if (acc <= 0.f) ++vwBad;
    }
    std::printf ("vwBad=%d\n", vwBad);

    // ---- swell: pluck drops to floor, rises to 1 in the attack time, then falls back to the floor
    int swBad = 0;
    {
        Swell sw; sw.setup (1.f, 100.f, 200.f, sr);
        if (sw.process() != 1.f) ++swBad;                                  // untriggered: unity
        sw.trigger(); float v = sw.process(); if (v > 0.01f) ++swBad;
        for (int i = 0; i < (int) (0.1 * sr); ++i) v = sw.process();
        if (v < 0.97f) ++swBad;
        for (int i = 0; i < (int) (0.21 * sr); ++i) v = sw.process();
        if (v > 0.01f) ++swBad;
        sw.setup (0.4f, 100.f, 100.f, sr); sw.trigger(); if (std::fabs (sw.process() - 0.6f) > 0.01f) ++swBad;
    }
    std::printf ("swBad=%d\n", swBad);

    // ---- mod fx: all types finite and bounded; flanger with feedback stays stable; vibrato is a pure delay at mix 1
    int fxBad = 0;
    {
        for (int type = 1; type <= 4; ++type)
        {
            ModFx fx; fx.prepare (sr); double pk = 0; double ph = 0;
            for (int i = 0; i < (int) (sr * 2); ++i, ph += 0.8 / sr)
            {
                float x = (float) (0.5 * std::sin (2.0 * kPi * 220.0 * i / sr)), L = x, R = x;
                fx.process (L, R, type, (float) std::sin (2.0 * kPi * ph), ph, 1.f, type == 2 ? 0.9f : 0.f, 1.f);
                if (! std::isfinite (L) || ! std::isfinite (R)) ++fxBad;
                if (i > 4800) pk = std::max (pk, (double) std::max (std::fabs (L), std::fabs (R)));
            }
            if (pk < 0.05 || pk > 3.0) ++fxBad;
        }
        ModFx v; v.prepare (sr); float L, R; double e = 0;
        for (int i = 0; i < 20000; ++i)
        {
            float x = (float) std::sin (2.0 * kPi * 440.0 * i / sr); L = R = x;
            v.process (L, R, ModFx::Vibrato, 0.f, 0.0, 1.f, 0.f, 1.f);                      // mod 0: fixed 6 ms delay
            if (i > 1000) { const float ref = (float) std::sin (2.0 * kPi * 440.0 * (i - 288) / sr); e = std::max (e, (double) std::fabs (L - ref)); }
        }
        if (e > 0.02) ++fxBad;
    }
    std::printf ("fxBad=%d\n", fxBad);

    return (bad == 0 && vwBad == 0 && swBad == 0 && fxBad == 0 && peak < 4.0 && peak > 0.01 && steps >= 30 && panBad == 0 && slopeBad == 0
            && stepBad == 0 && envBad == 0 && trigCount == 4 && silentTrig == 0 && pkBad == 0 && lfoBad == 0) ? 0 : 1;
}
