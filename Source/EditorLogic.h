#pragma once
// JUCE-free logic behind the new editor widgets (see EditorWidgets.h). Everything here is unit-tested in
// Tests/editor_logic_test.cpp, so the widgets only have to draw it and forward mouse events.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rfx { namespace ui {

constexpr double kTwoPi = 6.283185307179586476925286766559;
inline float clamp01 (float x) { return std::min (std::max (x, 0.f), 1.f); }

// ============================================================ 1. pattern tools
// All tools work in place on the first n steps of a lane (values 0..1; for the pan lane 0.5 = centre).
inline void toolLine (float* v, int n, int i0, int i1, float y0, float y1)       // straight line between two steps (inclusive)
{
    if (i0 > i1) { std::swap (i0, i1); std::swap (y0, y1); }
    i0 = std::max (i0, 0); i1 = std::min (i1, n - 1);
    for (int i = i0; i <= i1; ++i)
        v[i] = clamp01 (i1 == i0 ? y1 : y0 + (y1 - y0) * (float) (i - i0) / (float) (i1 - i0));
}
inline void toolRamp (float* v, int n, bool up)                                  // 0 -> 1 across the lane (or 1 -> 0)
{
    for (int i = 0; i < n; ++i) { const float t = n > 1 ? (float) i / (float) (n - 1) : 0.f; v[i] = up ? t : 1.f - t; }
}
inline void toolSine (float* v, int n, float cycles, float phase01 = 0.f)         // starts at the bottom and rises, like the LFO
{
    for (int i = 0; i < n; ++i)
        v[i] = clamp01 (0.5f + 0.5f * (float) std::sin (kTwoPi * ((double) cycles * i / n + phase01) - kTwoPi / 4.0));
}
inline void toolRandom (float* v, int n, uint32_t seed, int levels = 0)           // levels 0 = continuous, N>=2 = N quantised values
{
    uint32_t s = seed ? seed : 0x9E3779B9u;
    for (int i = 0; i < n; ++i)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        float r = (float) (s & 0xFFFFFF) / 16777215.f;
        if (levels >= 2) r = std::floor (r * (float) levels * 0.99999f) / (float) (levels - 1);
        v[i] = clamp01 (r);
    }
}
inline void toolMirror (float* v, int n) { std::reverse (v, v + n); }
inline void toolShift (float* v, int n, int by)                                  // rotate; positive = to the right, wraps around
{
    if (n < 2) return;
    by %= n; if (by < 0) by += n;
    std::rotate (v, v + (n - by), v + n);
}
inline void toolInvert (float* v, int n) { for (int i = 0; i < n; ++i) v[i] = 1.f - v[i]; }   // also mirrors a bipolar lane about 0.5
inline void toolClear (float* v, int n, float value) { for (int i = 0; i < n; ++i) v[i] = clamp01 (value); }

// ============================================================ 4. orbit view geometry
// Angles are radians measured clockwise from 12 o'clock (the same convention as juce::Path::addPieSegment).
struct OrbitRing { int steps; float rInner, rOuter; };
struct OrbitHit  { int ring = -1, step = -1; float value = 0.f; };
inline float orbitStepStart (int step, int steps) { return (float) (kTwoPi * step / steps); }
inline float orbitStepEnd   (int step, int steps) { return (float) (kTwoPi * (step + 1) / steps); }
inline float orbitStepMid   (int step, int steps) { return (float) (kTwoPi * (step + 0.5) / steps); }
// dx, dy = mouse position relative to the centre, y pointing DOWN (screen coordinates). value = radial position 0..1 in the ring.
inline OrbitHit orbitHitTest (const OrbitRing* rings, int nRings, float dx, float dy)
{
    OrbitHit h;
    const float r = std::sqrt (dx * dx + dy * dy);
    double a = std::atan2 ((double) dx, (double) -dy);               // 0 at the top, clockwise
    if (a < 0.0) a += kTwoPi;
    for (int i = 0; i < nRings; ++i)
        if (r >= rings[i].rInner && r <= rings[i].rOuter && rings[i].steps > 0)
        {
            h.ring = i;
            h.step = std::min ((int) (a / kTwoPi * rings[i].steps), rings[i].steps - 1);
            h.value = clamp01 ((r - rings[i].rInner) / (rings[i].rOuter - rings[i].rInner));
            return h;
        }
    return h;
}

// ============================================================ 5. XY morph pad: three scenes, lockable parameters
// A scene is a vector of NORMALISED (0..1) parameter values. The pad blends the three scenes by inverse-square distance.
struct SceneMorph
{
    static constexpr int kScenes = 3;
    std::array<std::array<float, 2>, kScenes> pos { { { { 0.16f, 0.20f } }, { { 0.84f, 0.28f } }, { { 0.50f, 0.84f } } } };   // where A / B / C sit on the pad
    std::array<std::vector<float>, kScenes> scene;

    std::array<float, kScenes> weightsAt (float x, float y) const
    {
        std::array<float, kScenes> w {}; float sum = 0.f;
        for (int k = 0; k < kScenes; ++k)
        {
            const float dx = x - pos[(size_t) k][0], dy = y - pos[(size_t) k][1], d2 = dx * dx + dy * dy;
            if (d2 < 1e-6f) { w = {}; w[(size_t) k] = 1.f; return w; }               // exactly on a scene
            w[(size_t) k] = 1.f / d2; sum += w[(size_t) k];
        }
        for (auto& v : w) v /= sum;
        return w;
    }
    // locked parameters keep their current value; everything else is the weighted mix of the three scenes
    std::vector<float> blend (float x, float y, const std::vector<float>& current, const std::vector<bool>& locked) const
    {
        std::vector<float> out = current;
        const auto w = weightsAt (x, y);
        for (size_t i = 0; i < out.size(); ++i)
        {
            if (i < locked.size() && locked[i]) continue;
            float v = 0.f;
            for (int k = 0; k < kScenes; ++k) v += w[(size_t) k] * (i < scene[(size_t) k].size() ? scene[(size_t) k][i] : current[i]);
            out[i] = clamp01 (v);
        }
        return out;
    }
};
// Dice: unlocked parameters move towards random values (amount 1 = fully random, 0 = unchanged)
inline std::vector<float> randomise (const std::vector<float>& current, const std::vector<bool>& locked, uint32_t seed, float amount)
{
    std::vector<float> out = current; uint32_t s = seed ? seed : 0xA5A5A5A5u;
    for (size_t i = 0; i < out.size(); ++i)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        const float r = (float) (s & 0xFFFFFF) / 16777215.f;
        if (i < locked.size() && locked[i]) continue;
        out[i] = clamp01 (current[i] + (r - current[i]) * clamp01 (amount));
    }
    return out;
}

// ============================================================ 6. performance mode helpers
struct TapTempo                                              // average of the last 6 intervals; a gap over 2 s starts over
{
    double lastMs = -1.0; std::vector<double> iv;
    void reset() { lastMs = -1.0; iv.clear(); }
    double tap (double nowMs)                                // returns the BPM, or 0 until there are two taps
    {
        if (lastMs >= 0.0 && nowMs - lastMs > 2000.0) iv.clear();
        if (lastMs >= 0.0 && nowMs > lastMs && nowMs - lastMs <= 2000.0) iv.push_back (nowMs - lastMs);
        lastMs = nowMs;
        if (iv.size() > 6) iv.erase (iv.begin());
        if (iv.empty()) return 0.0;
        double sum = 0.0; for (double v : iv) sum += v;
        return std::min (std::max (60000.0 / (sum / (double) iv.size()), 40.0), 240.0);
    }
};
struct PluckLight                                            // 1.0 at the moment of a detected note, fades with a ~120 ms time constant
{
    float level = 0.f;
    void trigger() { level = 1.f; }
    void tick (float dtSec) { level *= std::exp (-dtSec / 0.12f); if (level < 0.002f) level = 0.f; }
};

// ============================================================ 2. drag-to-modulate
// The processor's modulation matrix has 5 destinations (0 Cutoff, 1 Reso, 2 Amp, 3 Pan, 4 Drive). A knob can only accept a
// dropped modulation source if it maps onto one of them; -1 means "not modulatable yet" (needs the matrix extended, see HANDOFF).
inline int modDestForParam (const char* id)
{
    struct E { const char* id; int d; };
    static const E tbl[] = { { "fLow", 0 }, { "fHigh", 0 }, { "fRes", 1 }, { "tDepth", 2 }, { "pDepth", 3 }, { "drive", 4 } };
    for (const auto& e : tbl) if (std::strcmp (e.id, id) == 0) return e.d;
    return -1;
}
struct ModRing { float lo, hi; };                            // normalised 0..1 positions on the knob; the ring is drawn lo -> hi
inline ModRing modRingRange (float base01, float amount)     // amount -1..+1 = fraction of the knob's travel, either direction
{
    const float a = clamp01 (base01), b = clamp01 (base01 + amount);
    return { std::min (a, b), std::max (a, b) };
}
inline float modRingPosition (float base01, float amount, float source01) { return clamp01 (base01 + amount * source01); }

// ============================================================ 7. window scaling
// The editor is laid out once at a fixed design size and then scaled as a whole (vector drawing, so it stays sharp on retina /
// high-DPI screens). These helpers pick and clamp that scale.
constexpr float kMinScale = 0.5f, kMaxScale = 2.0f;
inline const std::array<int, 7> kScalePercents { { 60, 75, 90, 100, 125, 150, 200 } };       // entries of the size menu
inline float clampScale (float s) { return std::min (std::max (s, kMinScale), kMaxScale); }
inline float snapScale (float s, float step = 0.05f) { return std::floor (s / step + 1e-4f) * step; }      // down to a 5 % step
inline int   scaledSize (int base, float s) { return (int) std::lround ((double) base * (double) s); }
// Largest snapped scale (never above maxScale) at which a baseW x baseH design fits a screen area (logical pixels), leaving room for
// the window title bar, menu bar and dock.
inline float fitScale (float areaW, float areaH, float baseW, float baseH, float marginW = 40.f, float marginH = 90.f, float maxScale = 1.f)
{
    const float s = std::min ((areaW - marginW) / baseW, (areaH - marginH) / baseH);
    return clampScale (snapScale (std::min (s, maxScale)));
}
// What the editor opens at: the size saved with the session (> 0) if there is one, otherwise whatever fits this screen.
inline float initialScale (float saved, float areaW, float areaH, float baseW, float baseH)
{
    return saved > 0.f ? clampScale (saved) : fitScale (areaW, areaH, baseW, baseH);
}

}} // namespace rfx::ui
