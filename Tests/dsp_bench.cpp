// CPU benchmark of the audio chain (not a pass/fail test). Runs the DSP modules in the same order as processBlock with
// representative settings and prints how much of ONE core a 48 kHz stereo instance needs.
//   g++ -std=c++17 -O2 Tests/dsp_bench.cpp -ISource -o bench && ./bench
// Caveats: this models the DSP only (no JUCE, no GUI, no host); a real plugin is built with the host's compiler flags, so
// treat the numbers as a guide, and re-measure the real plugin in a DAW / with a profiler (HANDOFF.md section 7).
#include "RhythmDSP.h"
#include "RhythmDSP2.h"
#include <chrono>
#include <cstdio>
using namespace rfx;

struct Scenario { const char* name; bool drive, eq, pitch, phaser, modfx, ringcrush, delayFx, reverb, shimmer, spatial, slope24; int modType, pitchMode; };

static double run (const Scenario& sc, double sr, int seconds)
{
    Svf svf[2], svf2[2]; HiDrive drive; Eq3 eq; PitchFx pitch; Phaser phaser; ModFx modFx; RingMod ring; BitCrush crush;
    Compressor comp; StereoDelay delay; FdnReverb fdn; Spatial spatial; Lfo lfo;
    drive.prepare(); pitch.prepare (sr); phaser.prepare (sr); modFx.prepare (sr); ring.prepare (sr); delay.prepare (sr); fdn.prepare (sr);
    spatial.prepare (sr); comp.prepare (sr);
    eq.setup (sr, 2.f, -2.f, 800.f, 3.f);
    fdn.setParams (0.6f, 0.4f, 20.f, sc.shimmer ? 0.6f : 0.f);
    FbFxParams dfx; dfx.mode = sc.delayFx ? 2 : 0; dfx.rate = 0.8f; dfx.depth = 0.7f;
    DelayVoice dv; dv.voice = sc.delayFx ? 1 : 0; dv.wow = sc.delayFx ? 0.5f : 0.f; dv.duck = sc.delayFx ? 0.4f : 0.f;   // tape voice, wow and ducking when 'delay FX' is on
    const int N = (int) (sr * seconds); uint32_t rs = 1; double mc = 0.0; double acc = 0.0; float peak = 0.f;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i)
    {
        rs = rs * 1664525u + 1013904223u;
        const float env = (float) std::exp (-(double) (i % 24000) / 6000.0);                       // a pluck every half second
        float xl = ((float) (rs >> 8) / 8388608.f - 1.f) * 0.3f * env, xr = xl;
        const double ppq = (double) i / sr * 2.0;                                                   // 120 BPM
        const float cv = 0.5f + 0.5f * (float) std::sin (ppq * 3.0);
        if ((i & 7) == 0) { const float fc = 200.f * std::exp (std::log (4000.f / 200.f) * cv);
                            for (int c = 0; c < 2; ++c) { svf[c].setup (fc, 0.6f, sr); svf2[c].setup (fc, 0.f, sr); } }
        drive.process (xl, xr, sc.drive ? HiDrive::Tube : HiDrive::Soft, sc.drive ? 0.5f : 0.f, 1.f, sr);    // the oversampler always runs
        float yl = svf[0].process (xl, 1), yr = svf[1].process (xr, 1);
        if (sc.slope24) { yl = svf2[0].process (yl, 1); yr = svf2[1].process (yr, 1); }
        xl += 0.9f * (yl - xl); xr += 0.9f * (yr - xr);
        if (sc.eq)      eq.process (xl, xr);
        if (sc.pitch)   pitch.process (xl, xr, sc.pitchMode, 0.6f, 15.f);
        if (sc.phaser)  phaser.process (xl, xr, 3, 0.6f, 0.4f, 0.8f, 0.5f);
        if (sc.modfx)   { mc += 0.8 / sr; modFx.process (xl, xr, sc.modType, lfo.get (0, mc), mc, 0.7f, 0.4f, 0.5f); }
        if (sc.ringcrush) { ring.process (xl, xr, 0.4f, 330.f); crush.process (xl, xr, 10, 2); }
        comp.process (xl, xr, 0.4f);
        delay.process (xl, xr, 0.375f * (float) sr, 0.5f, 0.3f, true, dfx, dv);
        if (sc.reverb)  { float wl, wr; fdn.process (xl, xr, wl, wr); xl += 0.3f * wl; xr += 0.3f * wr; }
        if (sc.spatial) spatial.process (xl, xr, 1.4f, 0.5f, 0.3f);
        xl = softLimit (xl); xr = softLimit (xr);
        acc += (double) xl + (double) xr; peak = std::max (peak, std::fabs (xl));
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    if (! std::isfinite (acc) || peak > 1.2f) std::printf ("  WARNING: output not sane (acc %g, peak %g)\n", acc, (double) peak);
    return secs / (double) seconds;
}

int main()
{
    const double sr = 48000.0; const int secs = 20;
    const Scenario list[] = {
        { "Idle: oversampled drive stage + filter + delay + limiter only", false, false, false, false, false, false, false, false, false, false, false, 0, 0 },
        { "Typical: drive, EQ, chorus, reverb, 3D imager",                  true,  true,  false, false, true,  false, false, true,  false, true,  false, ModFx::Chorus, 0 },
        { "Rich: + thicken, phaser (12), delay repeat FX, 24 dB filter",    true,  true,  true,  true,  true,  false, true,  true,  false, true,  true,  ModFx::Rotary, 1 },
        { "Everything on: + shimmer reverb, ring mod, crusher, octaves",    true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  ModFx::Rotary, 4 },
    };
    std::printf ("CPU use of one 48 kHz stereo instance, single core (lower is better; 100%% = one whole core)\n");
    for (const auto& s : list) { const double r = run (s, sr, secs); std::printf ("  %5.1f %%   %s\n", r * 100.0, s.name); }
    return 0;
}
