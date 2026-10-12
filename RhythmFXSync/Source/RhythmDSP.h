// RhythmDSP.h - JUCE-independent DSP building blocks for RhythmFX Sync.
// Beat-synced step-modulated filter / amp, drive, ping-pong delay, compressor,
// a tiny drum machine and a poly synth. Header-only, no allocations in process().
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rfx
{
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- smoothing
struct OnePole
{
    float z = 0.f, a = 1.f;
    void setTimeMs (float ms, double sr)
    {
        a = ms <= 0.01f ? 1.f : 1.f - std::exp (-1.f / (ms * 0.001f * (float) sr));
    }
    float process (float x) { z += a * (x - z); return z; }
};

// ------------------------------------------- TPT state-variable filter (Zavalishin)
struct Svf
{
    float ic1 = 0.f, ic2 = 0.f, a1 = 0.f, a2 = 0.f, a3 = 0.f, k = 1.f;

    void setup (float fc, float res01, double sr)
    {
        fc = std::min (std::max (fc, 20.f), (float) (sr * 0.45));
        const float g = std::tan ((float) kPi * fc / (float) sr);
        const float Q = 0.6f + res01 * res01 * 14.f;
        k  = 1.f / Q;
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    // type: 0 = low-pass, 1 = band-pass, 2 = high-pass
    float process (float x, int type)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        if (type == 0) return v2;
        if (type == 1) return k * v1;
        return x - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = 0.f; }
};

// ----------------------------------------------------- tempo-synced step clock
struct StepClock
{
    struct Result { int step; bool fresh; };
    long long last = -1;

    // rate index: 0 = 1/4, 1 = 1/8, 2 = 1/16, 3 = 1/8T, 4 = 1/16T
    static double stepsPerBeat (int rate)
    {
        static const double t[5] = { 1.0, 2.0, 4.0, 3.0, 6.0 };
        return t[std::min (std::max (rate, 0), 4)];
    }

    // swing 0..1 delays every second step (up to a 3:1 feel)
    Result get (double ppq, double spb, float swing, int length)
    {
        const double sp   = ppq * spb * 0.5;
        const double pair = std::floor (sp);
        const double f    = sp - pair;
        const double pt   = 0.5 + 0.25 * (double) swing;
        const long long a = (long long) pair * 2 + (f >= pt ? 1 : 0);
        Result r;
        r.fresh = (a != last);
        last = a;
        long long m = a % length;
        if (m < 0) m += length;
        r.step = (int) m;
        return r;
    }
};

// ------------------------------------------------------------------- pan
// Balance-style pan: pos 0 = hard left, 0.5 = centre, 1 = hard right. depth scales the swing.
// The louder side stays at unity so enabling the pan lane never changes the overall level.
inline void panGains (float pos, float depth, float& gl, float& gr)
{
    const float p = 0.5f + (pos - 0.5f) * depth;
    const float a = std::min (std::max (p, 0.f), 1.f) * (float) kPi * 0.5f;
    gl = std::min (1.f, 1.41421356f * std::cos (a));
    gr = std::min (1.f, 1.41421356f * std::sin (a));
}

// ------------------------------------------------------------------- drive
inline float driveProcess (float x, float amount)
{
    if (amount <= 0.001f) return x;
    const float g = 1.f + amount * 29.f;
    return std::tanh (x * g) * 0.8f;
}

// ------------------------------------------------------ stereo ping-pong delay
struct StereoDelay
{
    std::vector<float> bl, br;
    int size = 0, w = 0;
    float tSmooth = 0.f, lpL = 0.f, lpR = 0.f;

    void prepare (double sr)
    {
        size = (int) (sr * 4.0) + 8;
        bl.assign ((size_t) size, 0.f);
        br.assign ((size_t) size, 0.f);
        w = 0; tSmooth = 0.f; lpL = lpR = 0.f;
    }
    float read (const std::vector<float>& b, float pos) const
    {
        while (pos < 0.f) pos += (float) size;
        int i0 = (int) pos;
        const float fr = pos - (float) i0;
        int i1 = i0 + 1;
        if (i0 >= size) i0 -= size;
        if (i1 >= size) i1 -= size;
        return b[(size_t) i0] + fr * (b[(size_t) i1] - b[(size_t) i0]);
    }
    void process (float& L, float& R, float delaySamples, float fb, float mix, bool ping)
    {
        delaySamples = std::min (std::max (delaySamples, 32.f), (float) (size - 8));
        if (tSmooth <= 0.f) tSmooth = delaySamples;
        tSmooth += 0.001f * (delaySamples - tSmooth);

        const float dl = read (bl, (float) w - tSmooth);
        const float dr = read (br, (float) w - tSmooth);
        lpL += 0.35f * (dl - lpL);          // damping in the feedback path
        lpR += 0.35f * (dr - lpR);
        if (ping) { bl[(size_t) w] = L + lpR * fb; br[(size_t) w] = R + lpL * fb; }
        else      { bl[(size_t) w] = L + lpL * fb; br[(size_t) w] = R + lpR * fb; }
        if (++w >= size) w = 0;
        L += dl * mix;
        R += dr * mix;
    }
};

// ------------------------------------------------------------- compressor
struct Compressor
{
    float env = 0.f, atk = 0.f, rel = 0.f;
    void prepare (double sr)
    {
        atk = 1.f - std::exp (-1.f / (0.005f * (float) sr));
        rel = 1.f - std::exp (-1.f / (0.120f * (float) sr));
        env = 0.f;
    }
    void process (float& L, float& R, float amount)
    {
        if (amount <= 0.001f) return;
        const float lvl = std::max (std::fabs (L), std::fabs (R));
        env += (lvl > env ? atk : rel) * (lvl - env);
        const float db   = 20.f * std::log10 (env + 1e-9f);
        const float th   = -6.f - amount * 30.f;
        const float over = db - th;
        const float gr   = over > 0.f ? over * 0.75f : 0.f;          // 4:1
        const float gain = std::pow (10.f, (-gr + amount * 8.f) / 20.f);
        L *= gain; R *= gain;
    }
};


// ------------------------------------------------------------- noise gate
struct NoiseGate
{
    float env = 0.f, gain = 1.f, atk = 0.f, rel = 0.f, gAtk = 0.f, gRel = 0.f;
    void prepare (double sr)
    {
        atk  = 1.f - std::exp (-1.f / (0.002f * (float) sr));
        rel  = 1.f - std::exp (-1.f / (0.050f * (float) sr));
        gAtk = 1.f - std::exp (-1.f / (0.003f * (float) sr));
        gRel = 1.f - std::exp (-1.f / (0.080f * (float) sr));
        env = 0.f; gain = 1.f;
    }
    // amount 0..1 -> threshold -80 dB .. -30 dB (0 = off)
    static float thresholdLinear (float amount) { return std::pow (10.f, (-80.f + amount * 50.f) / 20.f); }
    void process (float& L, float& R, float amount, float thLin)
    {
        if (amount < 0.01f) { gain = 1.f; return; }
        const float lvl = std::max (std::fabs (L), std::fabs (R));
        env += (lvl > env ? atk : rel) * (lvl - env);
        const float target = env > thLin ? 1.f : 0.f;
        gain += (target > gain ? gAtk : gRel) * (target - gain);
        L *= gain; R *= gain;
    }
};

// ------------------------------------------- envelope follower (adjustable attack / release)
struct EnvFollower
{
    float env = 0.f, atk = 0.f, rel = 0.f;
    static float coef (float ms, double sr) { return ms <= 0.01f ? 1.f : 1.f - std::exp (-1.f / (ms * 0.001f * (float) sr)); }
    void setTimes (float atkMs, float relMs, double sr) { atk = coef (atkMs, sr); rel = coef (relMs, sr); }
    void prepare (double sr) { setTimes (2.f, 90.f, sr); env = 0.f; }
    float process (float x) { x = std::fabs (x); env += (x > env ? atk : rel) * (x - env); return env; }
};

// ------------------------------------- note-triggered attack / decay envelope generator
// trigger() starts a linear attack to 1, then an exponential decay (decay time = time to -60 dB).
// Re-triggering mid-decay attacks again from the current level (no click).
struct AdEnv
{
    float v = 0.f, atkStep = 1.f, decMul = 0.999f;
    bool rising = false;
    void setTimes (float atkMs, float decMs, double sr)
    {
        atkStep = atkMs <= 0.1f ? 1.f : 1.f / (atkMs * 0.001f * (float) sr);
        decMul  = std::exp (-6.9078f / (std::max (decMs, 1.f) * 0.001f * (float) sr));
    }
    void trigger() { rising = true; }
    float process()
    {
        if (rising) { v += atkStep; if (v >= 1.f) { v = 1.f; rising = false; } }
        else        { v *= decMul; if (v < 1e-5f) v = 0.f; }
        return v;
    }
};

// ------------------------------------------------ pluck / note-onset detector (guitar audio)
// Fires when a fast envelope jumps well above a slow one. sens 0..1: higher = more sensitive.
struct NoteTrigger
{
    float fast = 0.f, slow = 0.f, fa = 1.f, fr = 0.f, sa = 0.f, sr2 = 0.f;
    int cool = 0, holdSamples = 0;
    void prepare (double sr)
    {
        fa = EnvFollower::coef (1.f, sr);   fr = EnvFollower::coef (20.f, sr);
        sa = EnvFollower::coef (40.f, sr);  sr2 = EnvFollower::coef (250.f, sr);
        holdSamples = (int) (0.06 * sr);
        fast = slow = 0.f; cool = 0;
    }
    bool process (float level, float sens)
    {
        fast += (level > fast ? fa : fr)  * (level - fast);
        slow += (level > slow ? sa : sr2) * (level - slow);
        if (cool > 0) { --cool; return false; }
        const float ratio    = 3.f - 1.8f * sens;                                 // 3x .. 1.2x
        const float floorLvl = std::pow (10.f, (-36.f - 24.f * sens) / 20.f);      // -36 .. -60 dB
        if (fast > floorLvl && fast > slow * ratio) { cool = holdSamples; return true; }
        return false;
    }
};

// ------------------------------------------------ note-triggered peak hold
// arm(): tracks the loudest sample for 30 ms after a pluck, then holds that value (x2 headroom
// scaling, clamped to 1). setValue(): hold an explicit value (MIDI velocity).
// The held value then falls over `release` seconds (T60); 10 s or more = hold until the next note.
struct PeakHold
{
    float peak = 0.f, held = 0.f, rel = 1.f;
    int win = 0, winLen = 0;
    bool infinite = true;
    void prepare (double sr) { winLen = (int) (0.03 * sr); peak = held = 0.f; win = 0; }
    void setRelease (float sec, double sr)
    {
        infinite = sec >= 9.9f;
        rel = std::exp (-6.9078f / (std::max (sec, 0.05f) * (float) sr));
    }
    void arm() { win = winLen; peak = 0.f; }
    void setValue (float v) { win = 0; held = std::min (std::max (v, 0.f), 1.f); }
    float process (float level)
    {
        if (win > 0) { --win; peak = std::max (peak, level * 2.f); held = std::min (1.f, peak); }
        else if (! infinite) { held *= rel; if (held < 1e-5f) held = 0.f; }
        return held;
    }
};

// ------------------------------------------------------------------------ LFO
// shape: 0 sine, 1 triangle, 2 saw (rising), 3 square, 4 random (sample & hold, one value per cycle).
// `cycles` is the total elapsed cycles (free-running: accumulated; synced: beats / beats-per-cycle),
// so a restart simply rewinds cycles to 0. All shapes start at their low point and move up.
struct Lfo
{
    long long lastCycle = -1;
    float sh = 0.f;
    uint32_t rng = 0x2545F491u;
    float get (int shape, double cycles)
    {
        const double fl = std::floor (cycles), ph = cycles - fl;
        const long long c = (long long) fl;
        switch (shape)
        {
            case 0:  return (float) -std::cos (2.0 * kPi * ph);
            case 1:  return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
            case 2:  return (float) (2.0 * ph - 1.0);
            case 3:  return ph < 0.5 ? -1.f : 1.f;
            default:
                if (c != lastCycle)
                {
                    lastCycle = c;
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    sh = (float) (rng & 0xFFFFFF) / 8388608.f - 1.f;
                }
                return sh;
        }
    }
};

// ------------------------------------------------- talk-box / vowel filter
// Three parallel resonant band-passes at the formants of A-E-I-O-U. pos 0..1 morphs A -> E -> I -> O -> U,
// so a step lane / envelope / LFO driving the position makes the guitar "talk".
struct Vowel
{
    Svf f[3];
    static void formants (float pos01, float out[3])
    {
        static const float t[5][3] = { { 800.f, 1150.f, 2900.f }, { 400.f, 1600.f, 2700.f }, { 350.f, 1700.f, 2700.f },
                                       { 450.f, 800.f, 2830.f },  { 325.f, 700.f, 2530.f } };
        const float p = std::min (std::max (pos01, 0.f), 1.f) * 4.f;
        const int   i = std::min ((int) p, 3);
        const float fr = p - (float) i;
        for (int k = 0; k < 3; ++k) out[k] = t[i][k] * std::pow (t[i + 1][k] / t[i][k], fr);
    }
    void setup (float pos01, float res01, double sr)
    {
        float fm[3]; formants (pos01, fm);
        for (int k = 0; k < 3; ++k) f[k].setup (fm[k], res01, sr);
    }
    float process (float x)
    {
        return 1.5f * (f[0].process (x, 1) + 0.6f * f[1].process (x, 1) + 0.35f * f[2].process (x, 1));
    }
    void reset() { for (auto& x : f) x.reset(); }
};

// ------------------------------------------------- arpeggio: snap a frequency to a musical scale
// scale: 0 off, 1 chromatic, 2 major, 3 minor, 4 major pentatonic, 5 minor pentatonic. root: 0 = C .. 11 = B.
inline float quantizeFreq (float fc, int scale, int root)
{
    static const int mask[6] = { 0xFFF, 0xFFF, 0xAB5, 0x5AD, 0x295, 0x4A9 };
    if (scale <= 0) return fc;
    const int m = mask[std::min (scale, 5)];
    const int r = (int) std::lround (69.f + 12.f * std::log2 (std::max (fc, 1.f) / 440.f));
    for (int d = 0; d <= 6; ++d)
        for (int sgn = 0; sgn < 2; ++sgn)
        {
            const int n = sgn == 0 ? r + d : r - d;
            const int rel = (((n - root) % 12) + 12) % 12;
            if ((m >> rel) & 1) return 440.f * std::pow (2.f, (float) (n - 69) / 12.f);
        }
    return fc;
}

// ------------------------------------------------------------- volume swell
// A pluck jumps the gain down to (1 - depth), swells up to unity over `atk`, then fades back over `rel`.
struct Swell
{
    enum { Idle, Rising, Falling };
    float g = 1.f, floorG = 1.f, riseStep = 1.f, fallStep = 1.f;
    int st = Idle;
    void setup (float depth, float atkMs, float relMs, double sr)
    {
        depth = std::min (std::max (depth, 0.f), 1.f);
        floorG   = 1.f - depth;
        riseStep = std::max (depth, 1e-3f) / (std::max (atkMs, 1.f) * 0.001f * (float) sr);
        fallStep = std::max (depth, 1e-3f) / (std::max (relMs, 1.f) * 0.001f * (float) sr);
    }
    void reset() { g = 1.f; st = Idle; }
    void trigger() { g = floorG; st = Rising; }
    float process()
    {
        if (st == Rising)       { g += riseStep; if (g >= 1.f) { g = 1.f; st = Falling; } }
        else if (st == Falling) { g -= fallStep; if (g <= floorG) { g = floorG; st = Idle; } }
        return g;
    }
};

// ------------------------------------------------------ modulation effects (one at a time)
// type: 0 off, 1 chorus, 2 flanger, 3 vibrato, 4 rotary speaker.
// `mod` (-1..1) is the LFO value for chorus / flanger / vibrato; `phase` (total cycles) drives the rotary.
struct ModFx
{
    enum { Off, Chorus, Flanger, Vibrato, Rotary };
    std::vector<float> bl, br;
    int size = 0, w = 0;
    double sr = 44100.0;
    float lp = 0.f, lpa = 0.1f;

    void prepare (double s)
    {
        sr = s; size = (int) (s * 0.08) + 8;
        bl.assign ((size_t) size, 0.f); br.assign ((size_t) size, 0.f);
        w = 0; lp = 0.f;
        lpa = 1.f - std::exp (-2.f * (float) kPi * 800.f / (float) s);        // 800 Hz rotary crossover
    }
    float rd (const std::vector<float>& b, float d) const
    {
        float pos = (float) w - std::max (d, 1.f);
        while (pos < 0.f) pos += (float) size;
        int i0 = (int) pos; const float fr = pos - (float) i0;
        int i1 = i0 + 1;
        if (i0 >= size) i0 -= size;
        if (i1 >= size) i1 -= size;
        return b[(size_t) i0] + fr * (b[(size_t) i1] - b[(size_t) i0]);
    }
    void process (float& L, float& R, int type, float mod, double phase, float depth, float fb, float mix)
    {
        if (type == Off) return;
        const float ms = (float) sr * 0.001f;
        const float inL = L, inR = R;

        if (type == Rotary)
        {
            const float m = 0.5f * (inL + inR);
            lp += lpa * (m - lp);
            bl[(size_t) w] = m - lp;                    // horn (highs)
            br[(size_t) w] = lp;                        // drum (lows)
            const double fh = phase - std::floor (phase), fd = phase * 0.88 - std::floor (phase * 0.88);
            const float th = (float) (2.0 * kPi * fh), td = (float) (2.0 * kPi * fd);
            const float h = depth * 0.6f * ms, dd = depth * 0.25f * ms, q = (float) kPi * 0.5f;
            const float aHL = 1.f - 0.225f * depth * (1.f - std::cos (th)),      aHR = 1.f - 0.225f * depth * (1.f - std::cos (th + q));
            const float aDL = 1.f - 0.1f   * depth * (1.f - std::cos (td)),      aDR = 1.f - 0.1f   * depth * (1.f - std::cos (td + q));
            const float wl = rd (bl, 1.f + h  * (1.f + std::sin (th))) * aHL + rd (br, 1.f + dd * (1.f + std::sin (td))) * aDL;
            const float wr = rd (bl, 1.f + h  * (1.f + std::sin (th + q))) * aHR + rd (br, 1.f + dd * (1.f + std::sin (td + q))) * aDR;
            L = inL + mix * (wl - inL);
            R = inR + mix * (wr - inR);
        }
        else if (type == Chorus)
        {
            bl[(size_t) w] = inL; br[(size_t) w] = inR;
            const float base = 12.f * ms, dev = depth * 6.f * ms * mod;
            const float wl = rd (bl, base + dev), wr = rd (br, base - dev);       // opposite phase = wide
            const float k = 1.f / (1.f + 0.7f * mix);
            L = (inL + mix * wl) * k;
            R = (inR + mix * wr) * k;
        }
        else if (type == Flanger)
        {
            const float d = (2.2f + 1.8f * depth * mod) * ms;
            const float wl = rd (bl, d), wr = rd (br, d);
            bl[(size_t) w] = inL + fb * wl;
            br[(size_t) w] = inR + fb * wr;
            const float k = 1.f / (1.f + 0.7f * mix);
            L = (inL + mix * wl) * k;
            R = (inR + mix * wr) * k;
        }
        else    // vibrato: pitch wobble from a modulated delay, crossfaded by Mix (1 = pure vibrato)
        {
            bl[(size_t) w] = inL; br[(size_t) w] = inR;
            const float d = (6.f + depth * 4.f * mod) * ms;
            L = inL + mix * (rd (bl, d) - inL);
            R = inR + mix * (rd (br, d) - inR);
        }
        if (++w >= size) w = 0;
    }
};

// ------------------------------------------------------ 4-lane drum machine
struct DrumMachine
{
    enum { Kick, Snare, Hat, Clap, NumLanes };
    struct Voice { bool on = false; double t = 0.0, ph = 0.0; float vel = 0.f, lp = 0.f; };
    Voice v[NumLanes];
    double sr = 44100.0;
    uint32_t rng = 0x1234567u;

    void prepare (double s) { sr = s; for (auto& x : v) x = Voice(); }
    float noise() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng & 0xFFFFFF) / 8388608.f - 1.f; }
    void trigger (int lane, float vel) { v[lane] = Voice(); v[lane].on = true; v[lane].vel = vel; }

    float process()
    {
        const double dt = 1.0 / sr;
        float out = 0.f;

        if (v[Kick].on)
        {
            auto& k = v[Kick];
            const double f = 45.0 + 110.0 * std::exp (-k.t / 0.035);
            k.ph += 2.0 * kPi * f * dt;
            out += (float) std::sin (k.ph) * (float) std::exp (-k.t / 0.22) * k.vel;
            k.t += dt; if (k.t > 1.0) k.on = false;
        }
        if (v[Snare].on)
        {
            auto& s = v[Snare];
            const float n = noise(); s.lp += 0.3f * (n - s.lp);
            const float hp = n - s.lp;
            s.ph += 2.0 * kPi * 190.0 * dt;
            out += ((float) std::sin (s.ph) * 0.5f * (float) std::exp (-s.t / 0.06)
                    + hp * 0.7f * (float) std::exp (-s.t / 0.11)) * s.vel;
            s.t += dt; if (s.t > 0.6) s.on = false;
        }
        if (v[Hat].on)
        {
            auto& h = v[Hat];
            const float n = noise(); h.lp += 0.7f * (n - h.lp);
            out += (n - h.lp) * 0.35f * (float) std::exp (-h.t / 0.03) * h.vel;
            h.t += dt; if (h.t > 0.25) h.on = false;
        }
        if (v[Clap].on)
        {
            auto& c = v[Clap];
            const float n = noise(); c.lp += 0.4f * (n - c.lp);
            float env = 0.f;
            for (int b = 0; b < 3; ++b)
            {
                const double tb = c.t - 0.012 * b;
                if (tb >= 0.0) env += (float) std::exp (-tb / 0.006);
            }
            if (c.t > 0.036) env += 0.6f * (float) std::exp (-(c.t - 0.036) / 0.09);
            out += (n - c.lp) * 0.45f * env * c.vel;
            c.t += dt; if (c.t > 0.7) c.on = false;
        }
        return out * 0.8f;
    }
};

// ------------------------------------------------------------ 8-voice synth
struct Synth
{
    static constexpr int N = 8;
    struct Voice
    {
        bool on = false, gate = false;
        int note = 0;
        float vel = 0.f, env = 0.f;
        double p1 = 0.0, p2 = 0.0, d1 = 0.0, d2 = 0.0;
        uint64_t age = 0;
    };
    Voice v[N];
    double sr = 44100.0;
    uint64_t counter = 0;

    void prepare (double s) { sr = s; allOff(); }
    void allOff() { for (auto& x : v) x = Voice(); }

    void noteOn (int note, float vel)
    {
        int idx = -1;
        for (int i = 0; i < N; ++i) if (! v[i].on) { idx = i; break; }
        if (idx < 0) { idx = 0; for (int i = 1; i < N; ++i) if (v[i].age < v[idx].age) idx = i; }
        Voice& x = v[idx];
        x = Voice();
        x.on = x.gate = true; x.note = note; x.vel = vel; x.age = ++counter;
        const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        x.d1 = f / sr; x.d2 = x.d1 * 1.0045;                          // ~8 cent unison detune
    }
    void noteOff (int note) { for (auto& x : v) if (x.on && x.gate && x.note == note) x.gate = false; }

    static double blep (double t, double dt)
    {
        if (t < dt)       { t /= dt;           return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }
    static float osc (int wave, double p, double dt)
    {
        if (wave == 0) return (float) (2.0 * p - 1.0 - blep (p, dt));                 // saw
        if (wave == 1)                                                                  // square
        {
            double p2 = p + 0.5; if (p2 >= 1.0) p2 -= 1.0;
            return (float) ((p < 0.5 ? 1.0 : -1.0) + blep (p, dt) - blep (p2, dt));
        }
        return (float) (4.0 * std::fabs (p - 0.5) - 1.0);                             // triangle
    }

    float process (int wave, float releaseSec)
    {
        const float relStep = 1.f / std::max (releaseSec * (float) sr, 1.f);
        const float atkStep = 1.f / (0.003f * (float) sr);
        float out = 0.f;
        for (auto& x : v)
        {
            if (! x.on) continue;
            if (x.gate) x.env = std::min (1.f, x.env + atkStep);
            else { x.env -= relStep; if (x.env <= 0.f) { x.on = false; x.env = 0.f; continue; } }
            x.p1 += x.d1; if (x.p1 >= 1.0) x.p1 -= 1.0;
            x.p2 += x.d2; if (x.p2 >= 1.0) x.p2 -= 1.0;
            out += (osc (wave, x.p1, x.d1) + osc (wave, x.p2, x.d2)) * 0.125f * x.env * x.vel;
        }
        return out;
    }
};
} // namespace rfx
