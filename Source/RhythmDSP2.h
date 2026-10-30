#pragma once
// RhythmDSP2.h - the "hi-def / 3D" effect set. JUCE-free so it can be unit-tested (Tests/dsp2_test.cpp).
//   Eq3            3-band tone shaping (low shelf / mid bell / high "air" shelf)
//   HiDrive        4x-oversampled waveshaper (Soft / Tube / Fuzz / Fold) with DC blocker and tone control
//   PitchShifter   granular two-tap pitch shifter with cubic interpolation
//   PitchFx        Thicken (stereo detune doubler), octave up/down, octaves, fifth
//   FdnReverb      8-line feedback-delay-network reverb: modulated lines, diffusion, damping, pre-delay, shimmer
//   Spatial        stereo "3D" imager: all-pass decorrelation, mid/side width, early reflections
//   softLimit      transparent soft-knee output limiter
#include "RhythmDSP.h"
#include <array>

namespace rfx
{
// ------------------------------------------------------------------------------------ biquad / EQ
struct Biquad
{
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;
    void reset() { z1 = z2 = 0.f; }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void set (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
    void peaking (double sr, double f, double q, double db)
    {
        f = std::min (f, 0.45 * sr);
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set (1.0 + al * A, -2.0 * c, 1.0 - al * A, 1.0 + al / A, -2.0 * c, 1.0 - al / A);
    }
    void lowShelf (double sr, double f, double db)
    {
        f = std::min (f, 0.45 * sr);
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * kPi * f / sr, c = std::cos (w);
        const double tb = 2.0 * std::sqrt (A) * std::sin (w) * 0.70710678;          // shelf slope S = 1
        set (A * ((A + 1) - (A - 1) * c + tb), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - tb),
             (A + 1) + (A - 1) * c + tb, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - tb);
    }
    void highShelf (double sr, double f, double db)
    {
        f = std::min (f, 0.45 * sr);
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * kPi * f / sr, c = std::cos (w);
        const double tb = 2.0 * std::sqrt (A) * std::sin (w) * 0.70710678;
        set (A * ((A + 1) + (A - 1) * c + tb), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - tb),
             (A + 1) - (A - 1) * c + tb, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - tb);
    }
};

struct Eq3
{
    Biquad lo[2], mid[2], hi[2];
    float cLo = 1e9f, cMid = 1e9f, cF = -1.f, cHi = 1e9f;
    void reset() { for (auto& b : lo) b.reset(); for (auto& b : mid) b.reset(); for (auto& b : hi) b.reset(); cLo = cMid = cHi = 1e9f; cF = -1.f; }
    // low shelf 110 Hz, mid bell (Q 0.9) at midHz, high shelf 7 kHz ("air"); gains in dB
    void setup (double sr, float lowDb, float midDb, float midHz, float highDb)
    {
        if (lowDb == cLo && midDb == cMid && midHz == cF && highDb == cHi) return;
        cLo = lowDb; cMid = midDb; cF = midHz; cHi = highDb;
        for (int c = 0; c < 2; ++c)
        {
            lo[c].lowShelf (sr, 110.0, lowDb);
            mid[c].peaking (sr, midHz, 0.9, midDb);
            hi[c].highShelf (sr, 7000.0, highDb);
        }
    }
    void process (float& L, float& R)
    {
        L = hi[0].process (mid[0].process (lo[0].process (L)));
        R = hi[1].process (mid[1].process (lo[1].process (R)));
    }
};

// ----------------------------------------------------------------------------- 4x oversampling
inline double besselI0 (double x)
{
    double s = 1.0, t = 1.0;
    for (int k = 1; k < 60; ++k) { const double h = x / (2.0 * k); t *= h * h; s += t; if (t < 1e-14 * s) break; }
    return s;
}

// Kaiser-windowed half-band FIR (cutoff = a quarter of its sample rate, unity DC gain), zero taps skipped.
struct Halfband
{
    int N = 0, pos = 0;
    std::vector<float> d, coef;
    std::vector<int> off;
    void design (int taps, double beta)
    {
        N = taps;
        const int M = (N - 1) / 2;
        std::vector<double> h ((size_t) N);
        double sum = 0.0;
        for (int n = 0; n < N; ++n)
        {
            const int m = n - M;
            const double x = m == 0 ? 1.0 : std::sin (kPi * m / 2.0) / (kPi * m / 2.0);
            const double r = (double) m / (double) M;
            h[(size_t) n] = 0.5 * x * besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
            sum += h[(size_t) n];
        }
        coef.clear(); off.clear();
        for (int n = 0; n < N; ++n)
            if (std::fabs (h[(size_t) n] / sum) > 1e-9) { coef.push_back ((float) (h[(size_t) n] / sum)); off.push_back (n); }
        d.assign ((size_t) (2 * N), 0.f);
        pos = 0;
    }
    void push (float x) { pos = pos == 0 ? N - 1 : pos - 1; d[(size_t) pos] = x; d[(size_t) (pos + N)] = x; }
    float dot() const
    {
        const float* p = &d[(size_t) pos];
        float y = 0.f;
        for (size_t k = 0; k < coef.size(); ++k) y += coef[k] * p[off[k]];
        return y;
    }
};

// 1 -> 4 -> 1 sample-rate conversion through two half-band stages. Total latency: 35 samples at the base rate.
struct Oversampler4
{
    static constexpr int kLatency = 35;
    Halfband u1, u2, d2, d1;
    void prepare()
    {
        u1.design (61, 8.0); u2.design (21, 8.0);
        d2.design (21, 8.0); d1.design (61, 8.0);
    }
    void up (float x, float* o)
    {
        u1.push (x);    const float a0 = 2.f * u1.dot();
        u1.push (0.f);  const float a1 = 2.f * u1.dot();
        u2.push (a0);   o[0] = 2.f * u2.dot();
        u2.push (0.f);  o[1] = 2.f * u2.dot();
        u2.push (a1);   o[2] = 2.f * u2.dot();
        u2.push (0.f);  o[3] = 2.f * u2.dot();
    }
    float down (const float* i)
    {
        d2.push (i[0]); const float b0 = d2.dot();
        d2.push (i[1]);
        d2.push (i[2]); const float b1 = d2.dot();
        d2.push (i[3]);
        d1.push (b0);   const float y = d1.dot();
        d1.push (b1);
        return y;
    }
};

// delays the dry tap by exactly the oversampler's latency so Dry/Wet stays phase-aligned
struct LatencyLine
{
    std::array<float, 64> bl {}, br {};
    int w = 0;
    void reset() { bl.fill (0.f); br.fill (0.f); w = 0; }
    void process (float inL, float inR, float& outL, float& outR)
    {
        bl[(size_t) w] = inL; br[(size_t) w] = inR;
        const int r = (w - Oversampler4::kLatency + 64) & 63;
        outL = bl[(size_t) r]; outR = br[(size_t) r];
        w = (w + 1) & 63;
    }
};

// ------------------------------------------------------------------------------------ hi-def drive
struct HiDrive
{
    enum { Soft, Tube, Fuzz, Fold };
    Oversampler4 os[2];
    float dcX[2] = {}, dcY[2] = {}, tone[2] = {};
    void prepare() { for (auto& o : os) o.prepare(); for (int c = 0; c < 2; ++c) dcX[c] = dcY[c] = tone[c] = 0.f; }

    static float shape (int type, float x, float amount)
    {
        if (amount <= 0.001f) return x;                              // clean: still passes the oversampler (constant latency)
        const float g = 1.f + amount * 29.f;
        switch (type)
        {
            case Tube:  return (std::tanh (x * g + 0.3f) - 0.29131f) * 0.8f;               // asymmetric: even harmonics
            case Fuzz:  return (x < 0.f ? -1.f : 1.f) * (1.f - std::exp (-std::fabs (x) * g * 1.4f)) * 0.75f;
            case Fold:  return std::sin (x * (1.f + (g - 1.f) * 0.25f)) * 0.8f;            // wavefolder
            default:    return std::tanh (x * g) * 0.8f;
        }
    }
    void process (float& L, float& R, int type, float amount, float toneAmt, double sr)
    {
        const float fc = 1500.f * std::pow (10.f, std::min (std::max (toneAmt, 0.f), 1.f));
        const float a = fc >= 0.45f * (float) sr ? 1.f : 1.f - std::exp (-2.f * (float) kPi * fc / (float) sr);
        float* io[2] = { &L, &R };
        for (int c = 0; c < 2; ++c)
        {
            float o[4];
            os[c].up (*io[c], o);
            for (float& v : o) v = shape (type, v, amount);
            float y = os[c].down (o);
            const float dc = y - dcX[c] + 0.9995f * dcY[c];          // DC blocker (the tube curve is asymmetric)
            dcX[c] = y; dcY[c] = dc; y = dc;
            tone[c] += a * (y - tone[c]);
            *io[c] = tone[c];
        }
    }
};

// ------------------------------------------------------------ pitch shifter: two taps + waveform alignment (SOLA)
// A plain two-tap granular shifter is up to half a semitone out of tune, and the error depends on the note played: every time
// a tap jumps back to the start of the buffer its phase jumps too, and the cross-fade drags the output frequency away from
// ratio * f. Here, each time a tap wraps (its weight is zero at that moment) its new delay is nudged by a lag chosen so that
// the waveform it will play lines up with the one the other tap is playing now (normalised cross-correlation over the last
// 20 ms, searched over one longest-period lag, coarse then fine). The cross-fade then blends in-phase signals: the pitch is
// exact for any periodic input and the comb-filter wobble disappears. A chord is still processed approximately (the
// best-matching lag is used), which is also how hardware shimmer effects behave.
struct PitchShifter
{
    std::vector<float> buf;
    int size = 0, w = 0, win = 0, maxLag = 0, corrLen = 0;
    double ph = 0.0;                         // unwrapped phase of tap A; tap B sits half a window ahead
    int offA = 0, offB = 0;                  // alignment lags chosen at the last wrap of each tap (samples)
    std::vector<float> refSeg, candSeg;      // scratch for align(): contiguous copies of the two signals being compared (allocated in prepare)

    void prepare (double sr, double windowMs = 55.0)
    {
        win = std::max (64, (int) (sr * windowMs / 1000.0));
        maxLag = std::max (16, (int) (sr * 0.026));               // search up to 26 ms = one period of a 38 Hz note
        corrLen = std::max (32, (int) (sr * 0.020));               // compare the last 20 ms
        size = win + maxLag + corrLen + 64;
        buf.assign ((size_t) size, 0.f);
        refSeg.assign ((size_t) corrLen, 0.f); candSeg.assign ((size_t) (maxLag + corrLen), 0.f);
        w = 0; ph = 0.0; offA = offB = 0;
    }
    // ratio > 1 shifts up
    float process (float x, float ratio)
    {
        buf[(size_t) w] = x;
        const double oldA = std::floor (ph), oldB = std::floor (ph + 0.5);
        ph += (1.0 - (double) ratio) / (double) win;
        const double nA = std::floor (ph), nB = std::floor (ph + 0.5);
        const double fa = ph - nA, fb = (ph + 0.5) - nB;                                  // phase of each tap, 0..1
        const float dA0 = 2.f + (float) (fa * win), dB0 = 2.f + (float) (fb * win);       // nominal delays
        if (nA != oldA) offA = align (w - (int) std::lround (dB0 + (float) offB), w - (int) std::lround (dA0));
        if (nB != oldB) offB = align (w - (int) std::lround (dA0 + (float) offA), w - (int) std::lround (dB0));
        const float w1 = 0.5f - 0.5f * (float) std::cos (2.0 * kPi * fa), w2 = 1.f - w1;
        const float y = w1 * readCubic (buf, size, (float) w - (dA0 + (float) offA)) + w2 * readCubic (buf, size, (float) w - (dB0 + (float) offB));
        if (++w >= size) w = 0;
        if (ph > 1.0e6) ph -= 1.0e6; else if (ph < -1.0e6) ph += 1.0e6;
        return y;
    }

private:
    float at (int pos) const { pos %= size; if (pos < 0) pos += size; return buf[(size_t) pos]; }

    // Lag in [0, maxLag] that makes the signal just before (candPos - lag) look most like the signal just before refPos.
    // Both signals are first copied into contiguous scratch arrays (chronological order), so the search loops are plain
    // multiply-accumulates over pointers (fast, and the compiler can vectorise them).
    int align (int refPos, int candPos)
    {
        for (int j = 0; j < corrLen; ++j) refSeg[(size_t) j] = at (refPos - corrLen + j);
        const int cn = maxLag + corrLen;
        for (int j = 0; j < cn; ++j) candSeg[(size_t) j] = at (candPos - cn + j);
        const float* ref = refSeg.data(); const float* cand = candSeg.data();
        double refE = 0.0; for (int j = 0; j < corrLen; j += 2) refE += (double) ref[j] * ref[j];
        if (refE < 1e-9) return 0;                                                          // silence: nothing to align
        auto score = [&] (int lag, int stride)
        {
            const float* c = cand + (maxLag - lag);                                         // candidate segment ending at candPos - lag
            double num = 0.0, den = 1e-12;
            for (int j = 0; j < corrLen; j += stride) { num += (double) ref[j] * c[j]; den += (double) c[j] * c[j]; }
            return num / std::sqrt (den);
        };
        int best = 0; double bs = -1e300;
        for (int lag = 0; lag <= maxLag; lag += 4) { const double sc = score (lag, 2); if (sc > bs) { bs = sc; best = lag; } }
        int fine = best; bs = -1e300;
        for (int lag = std::max (0, best - 4); lag <= std::min (maxLag, best + 4); ++lag) { const double sc = score (lag, 1); if (sc > bs) { bs = sc; fine = lag; } }
        return fine;
    }
};

struct PitchFx
{
    enum { Off, Thicken, OctUp, OctDown, Octaves, Fifth };
    PitchShifter a, b;                       // octave / fifth voices (granular shifter)
    std::vector<float> tb;                   // Thicken: automatic double tracking from a slowly wandering delay
    int tsz = 1, tw = 0;
    double t1 = 0.0, t2 = 0.0, t3 = 0.0, t4 = 0.0, sr = 48000.0;
    void prepare (double s)
    {
        sr = s; a.prepare (sr); b.prepare (sr);
        tsz = (int) (0.06 * sr) + 8; tb.assign ((size_t) tsz, 0.f); tw = 0; t1 = t2 = t3 = t4 = 0.0;
    }
    // Thicken (ADT): two voices, one per side, each delayed ~17 / ~23 ms by a smooth random-ish wander (two incommensurate
    //   sines). The wander gives a few cents of natural pitch drift, so the doubles never lock into a comb filter the way a
    //   fixed-offset detuner does. `cents` sets the wander depth (about +/- cents/2 of drift). Other modes: `cents` = fine detune.
    void process (float& L, float& R, int mode, float mix, float cents)
    {
        if (mode == Off) return;
        const float m = 0.5f * (L + R), k = 1.f / (1.f + 0.6f * mix);
        if (mode == Thicken)
        {
            tb[(size_t) tw] = m;
            t1 += 0.31 / sr; t2 += 0.77 / sr; t3 += 0.43 / sr; t4 += 0.91 / sr;
            t1 -= std::floor (t1); t2 -= std::floor (t2); t3 -= std::floor (t3); t4 -= std::floor (t4);
            const float depthMs = 0.1f * cents, ms = (float) (sr * 0.001);
            const float dA = (17.f + depthMs * (0.6f * (float) std::sin (2.0 * kPi * t1) + 0.4f * (float) std::sin (2.0 * kPi * t2))) * ms;
            const float dB = (23.f + depthMs * (0.6f * (float) std::sin (2.0 * kPi * t3) + 0.4f * (float) std::sin (2.0 * kPi * t4))) * ms;
            const float va = readCubic (tb, tsz, (float) tw - dA), vb = readCubic (tb, tsz, (float) tw - dB);
            if (++tw >= tsz) tw = 0;
            L = (L + mix * va) * k; R = (R + mix * vb) * k;
            return;
        }
        const float cr = std::pow (2.f, cents / 1200.f);
        float v;
        if (mode == Octaves) v = 0.7f * (a.process (m, 2.f * cr) + b.process (m, 0.5f * cr));
        else v = a.process (m, (mode == OctUp ? 2.f : mode == OctDown ? 0.5f : 1.49831f) * cr);
        L = (L + mix * v) * k; R = (R + mix * v) * k;
    }
};

// ------------------------------------------------------------------------------------- all-pass
struct Allpass
{
    std::vector<float> b;
    int w = 0;
    float g = 0.5f;
    void init (int n, float gain) { b.assign ((size_t) std::max (n, 1), 0.f); w = 0; g = gain; }
    float process (float x)
    {
        const float d = b[(size_t) w], v = x + g * d, y = d - g * v;
        b[(size_t) w] = v;
        if (++w >= (int) b.size()) w = 0;
        return y;
    }
};

// ---------------------------------------------------------------------- FDN reverb with shimmer
struct FdnReverb
{
    static constexpr int N = 16;                      // 16 lines: resonances close enough together to blend into a smooth tail
    std::vector<float> line[N];
    int lsz[N] = {}, wi[N] = {};
    float lenSm[N] = {}, lenTarget[N] = {}, gain[N] = {}, z[N] = {};
    double ph[N] = {}, phInc[N] = {};
    float modCur[N] = {}, modInc[N] = {};     // slow modulation is evaluated every 16 samples and interpolated between (16 sin() per sample was most of the cost)
    int modTick = 0;
    Allpass apL[4], apR[4];                                   // input diffusion: 4 Schroeder all-passes per side
    std::vector<float> pdL, pdR;
    int pdW = 0, pdN = 1, preSamples = 0;
    PitchShifter shifter;
    float shimPrev = 0.f, shim = 0.f, dampA = 0.5f, modDepth = 20.f, outGain = 1.f;
    double sr = 48000.0;

    static constexpr int kBase[N] = { 1103, 1201, 1319, 1433, 1567, 1709, 1867, 2039, 2221, 2423, 2647, 2887, 3163, 3449, 3761, 4111 };   // primes, lengths at 48 kHz
    static constexpr float kSgnL[N] = { 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1, -1 };                                        // orthogonal Hadamard rows
    static constexpr float kSgnR[N] = { 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1 };                                       //   -> decorrelated L / R
    // input injection: every line hears both channels with a different weight, so no mode of the tank is left unexcited
    static constexpr float kInL[N] = { 0.93f, 0.70f, -0.55f, 0.80f, -0.50f, 0.53f, -0.78f, 0.98f, -0.79f, -0.48f, -0.93f, -0.88f, 0.69f, -0.74f, 0.72f, -0.70f };
    static constexpr float kInR[N] = { 0.67f, -0.84f, 0.87f, 0.47f, -0.67f, 0.66f, -0.92f, -0.57f, -0.71f, 0.68f, -0.88f, 0.64f, 0.63f, 0.87f, -0.52f, -0.46f };
    static constexpr float kInGain = 0.318f, kOutScale = 0.707f;

    void prepare (double s)
    {
        sr = s;
        const double r = sr / 48000.0;
        modDepth = (float) (0.0005 * sr);
        for (int i = 0; i < N; ++i)
        {
            lsz[i] = (int) (kBase[i] * r * 1.6) + (int) modDepth + 16;
            line[i].assign ((size_t) lsz[i], 0.f);
            wi[i] = 0; z[i] = 0.f; ph[i] = 0.137 * i; phInc[i] = (0.30 + 0.115 * i) / sr;
            lenSm[i] = lenTarget[i] = (float) (kBase[i] * r);
        }
        apL[0].init ((int) (142 * r), 0.55f); apL[1].init ((int) (107 * r), 0.55f); apL[2].init ((int) (379 * r), 0.5f); apL[3].init ((int) (277 * r), 0.5f);
        apR[0].init ((int) (151 * r), 0.55f); apR[1].init ((int) (113 * r), 0.55f); apR[2].init ((int) (409 * r), 0.5f); apR[3].init ((int) (293 * r), 0.5f);
        pdN = (int) (0.13 * sr) + 4;
        pdL.assign ((size_t) pdN, 0.f); pdR.assign ((size_t) pdN, 0.f); pdW = 0;
        shifter.prepare (sr, 60.0);
        shimPrev = 0.f; modTick = 0;
        for (int i = 0; i < N; ++i) { modCur[i] = modInc[i] = 0.f; }
        setParams (0.5f, 0.4f, 15.f, 0.f);
    }
    // size 0..1 -> decay 0.25 s .. 9 s (and room scale); damp 0..1 -> 20 kHz .. 1.5 kHz in the loop; pre-delay ms; shimmer 0..1
    void setParams (float size01, float damp01, float preMs, float shim01)
    {
        const double r = sr / 48000.0;
        const float size = std::min (std::max (size01, 0.f), 1.f);
        const float scale = 0.55f + 0.95f * size;
        const double rt60 = 0.25 * std::pow (2.0, 5.2 * (double) size);
        for (int i = 0; i < N; ++i)
        {
            lenTarget[i] = (float) (kBase[i] * r * scale);
            gain[i] = (float) std::pow (10.0, -3.0 * ((double) lenTarget[i] / sr) / rt60);
        }
        const float fc = std::min (20000.f * std::pow (0.075f, std::min (std::max (damp01, 0.f), 1.f)), 0.45f * (float) sr);
        dampA = 1.f - std::exp (-2.f * (float) kPi * fc / (float) sr);
        preSamples = std::min ((int) (preMs * 0.001 * sr), pdN - 1);
        shim = 0.09f * std::min (std::max (shim01, 0.f), 1.f);       // loop gain: measured stable (decaying) up to ~0.12 at max size
    }
    void process (float inL, float inR, float& outL, float& outR)
    {
        pdL[(size_t) pdW] = inL; pdR[(size_t) pdW] = inR;
        int rp = pdW - preSamples; if (rp < 0) rp += pdN;
        float xL = pdL[(size_t) rp], xR = pdR[(size_t) rp];
        if (++pdW >= pdN) pdW = 0;
        xL = apL[3].process (apL[2].process (apL[1].process (apL[0].process (xL))));
        xR = apR[3].process (apR[2].process (apR[1].process (apR[0].process (xR))));

        const float sh = shimPrev * shim;
        float f[N], S = 0.f;
        for (int i = 0; i < N; ++i)
        {
            lenSm[i] += 0.0004f * (lenTarget[i] - lenSm[i]);
            if ((modTick & 15) == 0)
            {
                ph[i] += phInc[i] * 16.0; if (ph[i] >= 1.0) ph[i] -= 1.0;
                modInc[i] = (modDepth * (float) std::sin (2.0 * kPi * ph[i]) - modCur[i]) * (1.f / 16.f);
            }
            modCur[i] += modInc[i];
            const float d = lenSm[i] + modCur[i];
            float y = readCubic (line[i], lsz[i], (float) wi[i] - d) * gain[i];
            z[i] += dampA * (y - z[i]);
            f[i] = z[i];
            S += f[i];
        }
        const float k = S * (2.f / (float) N);                      // Householder: m_i = f_i - (2/N) * sum(f)
        ++modTick;
        float oL = 0.f, oR = 0.f;
        for (int i = 0; i < N; ++i)
        {
            line[i][(size_t) wi[i]] = (kInL[i] * xL + kInR[i] * xR) * kInGain + sh + (f[i] - k);
            if (++wi[i] >= lsz[i]) wi[i] = 0;
            oL += kSgnL[i] * f[i]; oR += kSgnR[i] * f[i];
        }
        oL *= outGain * kOutScale; oR *= outGain * kOutScale;
        if (shim > 0.001f) shimPrev = shifter.process (0.5f * (oL + oR), 2.f);          // octave-up of the tank, fed back into its input
        else               shimPrev = 0.f;                                               // shimmer off: do not run the shifter at all
        outL = oL; outR = oR;
    }
};

// ------------------------------------------------------------------------------ stereo 3D imager
struct Spatial
{
    Allpass dl[4], dr[4];
    std::vector<float> erL, erR;
    int erN = 1, erW = 0;
    float lpL = 0.f, lpR = 0.f, lpA = 0.5f;
    double sr = 48000.0;
    static constexpr int kTaps = 6;
    static constexpr float kTL[kTaps] = { 7.3f, 11.9f, 17.3f, 23.1f, 29.7f, 37.9f };     // ms, left output
    static constexpr float kTR[kTaps] = { 9.1f, 13.7f, 19.9f, 26.3f, 33.1f, 41.3f };     // ms, right output
    static constexpr float kG[kTaps]  = { .50f, .42f, .34f, .28f, .22f, .17f };

    void prepare (double s)
    {
        sr = s;
        const float ml[4] = { 1.31f, 2.73f, 4.51f, 6.43f }, mr[4] = { 1.67f, 3.19f, 5.03f, 7.13f };
        for (int i = 0; i < 4; ++i) { dl[i].init ((int) (ml[i] * 0.001 * sr), 0.55f); dr[i].init ((int) (mr[i] * 0.001 * sr), 0.55f); }
        erN = (int) (0.05 * sr) + 8;
        erL.assign ((size_t) erN, 0.f); erR.assign ((size_t) erN, 0.f); erW = 0;
        lpL = lpR = 0.f;
        lpA = 1.f - std::exp (-2.f * (float) kPi * 5000.f / (float) sr);
    }
    // width: 0 mono .. 1 neutral .. 2 extra wide; space: all-pass decorrelation (widens even a mono source); depth: early reflections
    void process (float& L, float& R, float width, float space, float depth)
    {
        if (space < 0.001f && depth < 0.001f && std::fabs (width - 1.f) < 0.001f) return;
        if (space > 0.001f)
        {
            float a = L, b = R;
            for (auto& ap : dl) a = ap.process (a);
            for (auto& ap : dr) b = ap.process (b);
            L += space * (a - L); R += space * (b - R);
        }
        const float M = 0.5f * (L + R), S = 0.5f * (L - R) * width;
        L = M + S; R = M - S;
        if (depth > 0.001f)
        {
            erL[(size_t) erW] = L; erR[(size_t) erW] = R;
            auto tap = [&] (const std::vector<float>& b, float ms)
            {
                int r = erW - (int) (ms * 0.001 * sr); if (r < 0) r += erN;
                return b[(size_t) r];
            };
            float eL = 0.f, eR = 0.f;
            for (int k = 0; k < kTaps; ++k)
            {
                eL += kG[k] * ((k & 1) ? tap (erL, kTL[k]) : tap (erR, kTL[k]));         // alternate sides = cross-room bounce
                eR += kG[k] * ((k & 1) ? tap (erR, kTR[k]) : tap (erL, kTR[k]));
            }
            lpL += lpA * (eL - lpL); lpR += lpA * (eR - lpR);
            L += depth * 0.7f * lpL; R += depth * 0.7f * lpR;
            if (++erW >= erN) erW = 0;
        }
    }
};

// --------------------------------------------------------------------------------- stereo phaser
// 4 / 6 / 8 / 12 first-order all-pass stages swept exponentially 250 Hz .. 4 kHz, feedback, and a 90 degree LFO offset
// between left and right so the notches sweep differently in each ear (wide, "3D" movement).
struct Phaser
{
    static constexpr int kMax = 12;
    float z[2][kMax] = {}, fbState[2] = {}, a[2] = { 0.f, 0.f };
    double ph = 0.0, sr = 48000.0;
    int tick = 0;
    void prepare (double s) { sr = s; ph = 0.0; tick = 0; for (auto& c : z) for (float& v : c) v = 0.f; fbState[0] = fbState[1] = 0.f; }
    static int stages (int idx) { static const int n[4] = { 4, 6, 8, 12 }; return n[std::min (std::max (idx, 0), 3)]; }
    void process (float& L, float& R, int stageIdx, float mix, float rateHz, float depth, float fb)
    {
        if (mix < 0.001f) return;
        ph += (double) rateHz / sr; if (ph >= 1.0) ph -= 1.0;
        if ((tick++ & 3) == 0)
        {
            const float lf[2] = { (float) std::sin (2.0 * kPi * ph), (float) std::cos (2.0 * kPi * ph) };
            for (int c = 0; c < 2; ++c)
            {
                const float pos = 0.5f + 0.5f * std::min (std::max (depth, 0.f), 1.f) * lf[c];
                const float fc = 250.f * std::pow (16.f, pos);
                const float t = std::tan ((float) kPi * std::min (fc, 0.45f * (float) sr) / (float) sr);
                a[c] = (t - 1.f) / (t + 1.f);
            }
        }
        const int n = stages (stageIdx);
        float* io[2] = { &L, &R };
        for (int c = 0; c < 2; ++c)
        {
            const float x = *io[c];
            float y = x + fb * fbState[c];
            for (int k = 0; k < n; ++k)
            {
                const float o = a[c] * y + z[c][k];
                z[c][k] = y - a[c] * o;
                y = o;
            }
            fbState[c] = y;
            *io[c] = x * (1.f - 0.5f * mix) + y * 0.5f * mix;
        }
    }
};

// ----------------------------------------------------------------------------------- ring modulator
// quadrature carriers (0 / 90 degrees) left / right so the metallic sidebands are different in each ear
struct RingMod
{
    double ph = 0.0, sr = 48000.0;
    void prepare (double s) { sr = s; ph = 0.0; }
    void process (float& L, float& R, float mix, float hz)
    {
        if (mix < 0.001f) return;
        ph += (double) hz / sr; if (ph >= 1.0) ph -= 1.0;
        const float cL = (float) std::sin (2.0 * kPi * ph), cR = (float) std::cos (2.0 * kPi * ph);
        L += mix * (L * cL - L);
        R += mix * (R * cR - R);
    }
};

// -------------------------------------------------------------------------------- bit / rate crusher
struct BitCrush
{
    float held[2] = {}; int count = 0;
    void reset() { held[0] = held[1] = 0.f; count = 0; }
    // bits 16 and down 1 = bit-exact passthrough
    void process (float& L, float& R, int bits, int down)
    {
        if (bits >= 16 && down <= 1) return;
        if (count <= 0)
        {
            const float lv = std::exp2 ((float) std::min (std::max (bits, 2), 16) - 1.f);
            // signed N-bit range: -2^(N-1) .. 2^(N-1)-1 (so 4 bits = exactly 16 levels)
            held[0] = std::min (std::max (std::round (L * lv), -lv), lv - 1.f) / lv;
            held[1] = std::min (std::max (std::round (R * lv), -lv), lv - 1.f) / lv;
            count = std::max (down, 1);
        }
        --count;
        L = held[0]; R = held[1];
    }
};

// ------------------------------------------------------------------------------- output limiter
// transparent below 0.8; above it a tanh knee approaches (never reaches) 1.0
inline float softLimit (float x)
{
    const float t = 0.8f, a = std::fabs (x);
    if (a <= t) return x;
    const float y = t + (1.f - t) * std::tanh ((a - t) / (1.f - t));
    return x < 0.f ? -y : y;
}
} // namespace rfx
