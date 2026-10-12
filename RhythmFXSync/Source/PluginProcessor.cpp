#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::StringArray kRates   { "1/4", "1/8", "1/16", "1/8T", "1/16T" };
const juce::StringArray kFilters { "Lowpass", "Bandpass", "Highpass", "Vowel" };
const juce::StringArray kScales  { "Off", "Chromatic", "Major", "Minor", "Maj Pent", "Min Pent" };
const juce::StringArray kRoots   { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const juce::StringArray kModFx   { "Off", "Chorus", "Flanger", "Vibrato", "Rotary" };
const juce::StringArray kDelays  { "1/4", "1/8", "1/8 dotted", "1/16", "1/4 dotted", "1/8T" };
const juce::StringArray kWaves   { "Saw", "Square", "Triangle" };
const juce::StringArray kSlopes  { "12 dB", "24 dB" };
const std::array<float, 16> kCentre {{ .5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f }};
const juce::StringArray kDests   { "Cutoff", "Reso", "Amp", "Pan", "Drive" };
const juce::StringArray kTrig    { "Audio", "MIDI", "Both" };
const juce::StringArray kLfoShapes { "Sine", "Triangle", "Saw", "Square", "Random" };
const juce::StringArray kLfoDivs { "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16" };
const juce::StringArray kRsModes { "Off", "MIDI note", "MIDI CC" };
const double kLfoBeats[6]  = { 8.0, 4.0, 2.0, 1.0, 0.5, 0.25 };
const double kDelayBeats[6] = { 1.0, 0.5, 0.75, 0.25, 1.5, 1.0 / 3.0 };

std::array<float, 16> flat (float v) { std::array<float, 16> a; a.fill (v); return a; }

struct PresetMod                          // modulation-section settings a program can carry
{
    float eAmt = 0.f, eAtk = 5.f, eDec = 300.f, lfoAmt = 0.f, lfoRate = 2.f;
    int   lfoShape = 0, lfoSync = 0, lfoDiv = 3, eDst = 0, lfoDst = 0;
    float nAmt = 0.f, pkAmt = 0.f;
    int   rsPick = 0;
    // --- filter character, arpeggio, mod fx, swell
    int   fType = 0;                                   // 0 LP, 1 BP, 2 HP, 3 Vowel
    float fRand = 0.f;
    int   arpScale = 0, arpRoot = 0;
    int   mfType = 0, mfSync = 0, mfDiv = 3, mfRandom = 0;
    float mfRate = 1.f, mfDepth = .5f, mfFb = 0.f, mfMix = .5f;
    float swAmt = 0.f, swAtk = 600.f, swRel = 2000.f;
};
template <class F> PresetMod M (F f) { PresetMod m; f (m); return m; }

struct Preset
{
    const char* name;
    std::array<float, 16> filt, amp;
    int kick, snare, hat, clap;          // 16-bit step masks
    int rate, length;
    float fLow, fHigh, fRes, fGlide, fMix, tDepth, drive, dMix, drOn, swing;
    float fEnv = 0.f;
    // --- per-lane extras (-1 = follow the filter lane's rate / length)
    std::array<float, 16> pan = kCentre;
    float pDepth = 0.f;
    int aRate = -1, aLen = -1, pRate = -1, pLen = -1, fSlope = 0;
    PresetMod mod {};
};

const Preset kPresets[] =
{
    { "Init", { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 }, { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.f, 0, 0.f },
    { "8th Wah", { 0,.3f,.7f,1,.7f,.3f,0,.3f, 0,.3f,.7f,1,.7f,.3f,0,.3f }, { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
      0,0,0,0, 1,16, 250,3500, 0.65f, 25, 1.f, 0.f, 0.15f, 0.15f, 0, 0.f },
    { "16th Gate Chop", { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 }, { 1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0 },
      0x1111,0x1010,0x5555,0, 2,16, 200,4000, 0.4f, 20, 0.f, 1.f, 0.2f, 0.2f, 1, 0.f },
    { "Sweep Up", { 0,.07f,.13f,.2f,.27f,.33f,.4f,.47f,.53f,.6f,.67f,.73f,.8f,.87f,.93f,1 }, { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
      0,0,0,0, 2,16, 120,6000, 0.75f, 90, 1.f, 0.f, 0.25f, 0.25f, 0, 0.f },
    { "Triplet Bounce", { 1,.2f,.6f,1,.2f,.6f,1,.2f,.6f,1,.2f,.6f,0,0,0,0 }, { 1,.5f,1,1,.5f,1,1,.5f,1,1,.5f,1,1,1,1,1 },
      0x0249,0,0,0, 3,12, 200,5000, 0.6f, 15, 1.f, 0.5f, 0.1f, 0.3f, 0, 0.f },
    { "Dub Machine", { 0,0,.6f,0,.9f,0,.4f,0,0,.3f,.8f,0,1,0,.5f,0 }, { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
      0x0101,0x0100,0x2222,0x0400, 2,16, 150,3000, 0.7f, 40, 1.f, 0.f, 0.35f, 0.45f, 1, 0.15f },
    { "Auto-Wah (Envelope)", { .15f,.15f,.15f,.15f,.15f,.15f,.15f,.15f,.15f,.15f,.15f,.15f,.15f,.15f,.15f,.15f }, { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
      0,0,0,0, 2,16, 250,3500, 0.7f, 5, 1.f, 0.f, 0.2f, 0.1f, 0, 0.f, 0.9f },
    { "Guitar Stutter", { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 }, { 1,0,1,0,1,0,1,1,1,0,1,0,1,0,0,1 },
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 1.f, 0.35f, 0.25f, 0, 0.f },
    { "Auto-Pan 8th", { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 }, { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
      0,0,0,0, 1,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.2f, 0, 0.f, 0.f,
      { 0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1 }, 1.f, -1, -1, -1, -1, 0 },
    { "Wah Chase (24dB)", { 0,.3f,.7f,1,.7f,.3f,0,.3f, 0,.3f,.7f,1,.7f,.3f,0,.3f }, { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
      0,0,0,0, 1,16, 250,3500, 0.6f, 25, 1.f, 0.f, 0.2f, 0.25f, 0, 0.f, 0.f,
      { 0,.25f,.5f,.75f,1,.75f,.5f,.25f, 0,.25f,.5f,.75f,1,.75f,.5f,.25f }, 0.8f, -1, -1, 0, 8, 1 },
    { "Polyrhythm 16 vs 12", { 0,.2f,.5f,.9f,1,.8f,.4f,.1f, 0,.3f,.7f,1,.6f,.2f,0,.1f }, { 1,.3f,1,.3f,1,.3f,1,.3f,1,.3f,1,.3f,1,1,1,1 },
      0,0,0,0, 2,16, 180,4500, 0.7f, 18, 1.f, 0.7f, 0.2f, 0.3f, 0, 0.f, 0.f,
      { 0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1 }, 0.6f, -1, 12, 1, 5, 1 },
    { "Pluck Wah (Envelope)", flat (.08f), flat (1.f),
      0,0,0,0, 2,16, 200,3500, 0.72f, 5, 1.f, 0.f, 0.15f, 0.15f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 1, { .9f, 3.f, 450.f } },
    { "LFO Wah (1/4 sync)", flat (.35f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.65f, 10, 1.f, 0.f, 0.2f, 0.2f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 1, { 0.f, 5.f, 300.f, .55f, 2.f, 0, 1, 3 } },
    { "Peak-Hold Brightness", flat (.1f), flat (1.f),
      0,0,0,0, 2,16, 200,5000, 0.5f, 10, 1.f, 0.f, 0.25f, 0.15f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, { 0.f, 5.f, 300.f, 0.f, 2.f, 0, 0, 3, 0, 0, 0.f, .85f } },
    { "Pick Restart Chop", { 0,.3f,.7f,1,.7f,.3f,0,.3f, 0,.3f,.7f,1,.7f,.3f,0,.3f }, { 1,0,1,0,1,1,0,1,1,0,1,0,1,1,0,1 },
      0,0,0,0, 2,16, 250,3500, 0.6f, 20, 1.f, 1.f, 0.2f, 0.2f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, { 0.f, 5.f, 300.f, 0.f, 2.f, 0, 0, 3, 0, 0, 0.f, 0.f, 1 } },
    { "Note Tracker (MIDI)", flat (.1f), flat (1.f),
      0,0,0,0, 2,16, 150,5000, 0.5f, 10, 1.f, 0.f, 0.15f, 0.15f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 1, { 0.f, 5.f, 300.f, 0.f, 2.f, 0, 0, 3, 0, 0, .9f } },
    { "Tremolo (Smooth)", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.lfoAmt = .7f; m.lfoSync = 1; m.lfoDiv = 4; m.lfoDst = 2; }) },
    { "Tremolo (Hard Chop)", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.lfoAmt = 1.f; m.lfoShape = 3; m.lfoSync = 1; m.lfoDiv = 4; m.lfoDst = 2; }) },
    { "Filter Tremolo", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.62f, 20, 1.f, 0.f, 0.15f, 0.15f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.lfoAmt = .5f; m.lfoShape = 1; m.lfoSync = 1; m.lfoDiv = 4; }) },
    { "Chorus Shimmer", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.mfType = 1; m.mfRate = .7f; m.mfDepth = .55f; m.mfMix = .5f; }) },
    { "Flanger Jet", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.15f, 0.f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.mfType = 2; m.mfRate = .25f; m.mfDepth = .9f; m.mfFb = .75f; m.mfMix = .6f; }) },
    { "Rotary Speaker", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.1f, 0.f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.mfType = 4; m.mfRate = .8f; m.mfDepth = .85f; m.mfMix = 1.f; }) },
    { "Rotary Speaker (Fast)", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.1f, 0.f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.mfType = 4; m.mfRate = 6.f; m.mfDepth = .85f; m.mfMix = 1.f; }) },
    { "Vibrato", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.mfType = 3; m.mfRate = 5.5f; m.mfDepth = .35f; m.mfMix = 1.f; }) },
    { "Random Filter", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4500, 0.7f, 12, 1.f, 0.f, 0.15f, 0.2f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.fType = 1; m.fRand = 1.f; }) },
    { "Random Flanger", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.1f, 0.f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.mfType = 2; m.mfSync = 1; m.mfDiv = 4; m.mfRandom = 1; m.mfDepth = .9f; m.mfFb = .7f; m.mfMix = .6f; }) },
    { "Arpeggio (A Minor Pent)", { 0,.146f,.25f,.354f,.5f,.354f,.25f,.146f, 0,.208f,.3125f,.396f,.5f,.396f,.3125f,.208f }, flat (1.f),
      0,0,0,0, 2,16, 110,1760, 0.9f, 2, 1.f, 0.f, 0.1f, 0.25f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.fType = 1; m.arpScale = 5; m.arpRoot = 9; }) },
    { "Vowel Talk Box", { 0,.25f,.5f,.75f,1,.75f,.5f,.25f, 0,.5f,1,.5f,.25f,.75f,.5f,.25f }, flat (1.f),
      0,0,0,0, 1,16, 200,4000, 0.6f, 40, 1.f, 0.f, 0.2f, 0.15f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.fType = 3; }) },
    { "Vowel Auto-Talk (Envelope)", flat (0.f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.6f, 15, 1.f, 0.f, 0.2f, 0.1f, 0, 0.f, 0.9f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.fType = 3; }) },
    { "Volume Swell", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.25f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.swAmt = 1.f; m.swAtk = 900.f; m.swRel = 3000.f; }) },
    { "Pulse Tremolo", { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 }, { 1,.25f,1,.25f,1,.25f,1,.25f,1,.25f,1,.25f,1,.25f,1,.25f },
      0,0,0,0, 1,16, 200,4000, 0.4f, 20, 0.f, 0.8f, 0.1f, 0.2f, 0, 0.f },
};
constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));

juce::String stepsToString (const std::array<std::atomic<float>, RhythmFXProcessor::kSteps>& a)
{
    juce::StringArray s;
    for (auto& v : a) s.add (juce::String (v.load(), 3));
    return s.joinIntoString (" ");
}
void stringToSteps (const juce::String& str, std::array<std::atomic<float>, RhythmFXProcessor::kSteps>& a)
{
    auto t = juce::StringArray::fromTokens (str, " ", "");
    const int have = std::min (t.size(), (int) a.size());
    for (int i = 0; i < have; ++i) a[(size_t) i].store (juce::jlimit (0.f, 1.f, t[i].getFloatValue()));
    for (int i = have; i < (int) a.size() && have > 0; ++i) a[(size_t) i].store (a[(size_t) (i % have)].load());   // old 16-step states: tile
}
}

// ============================================================ parameters
RhythmFXProcessor::APVTS::ParameterLayout RhythmFXProcessor::createLayout()
{
    APVTS::ParameterLayout l;
    auto F = [&] (const char* id, const char* name, float lo, float hi, float def, float skew = 1.f)
    {
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
               juce::NormalisableRange<float> (lo, hi, 0.f, skew), def));
    };
    auto C = [&] (const char* id, const char* name, const juce::StringArray& items, int def)
    {
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def));
    };
    auto B = [&] (const char* id, const char* name, bool def)
    {
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    };
    auto I = [&] (const char* id, const char* name, int lo, int hi, int def)
    {
        l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, lo, hi, def));
    };

    // pattern + the three 32-step lanes (each with its own rate and length)
    F ("swing",  "Swing",        0.f, 1.f, 0.f);
    C ("rate",   "Filter Rate",  kRates, 2);
    I ("length", "Filter Length", 1, 32, 16);
    C ("fType",  "Filter Type",  kFilters, 0);
    F ("fRand",  "Filter Random", 0.f, 1.f, 0.f);
    C ("arpScale","Arp Scale",   kScales, 0);
    C ("arpRoot","Arp Key",      kRoots, 0);
    C ("fSlope", "Filter Slope", kSlopes, 0);
    F ("fLow",   "Filter Low",   40.f, 8000.f, 200.f, 0.35f);
    F ("fHigh",  "Filter High",  200.f, 16000.f, 4000.f, 0.35f);
    F ("fRes",   "Resonance",    0.f, 1.f, 0.5f);
    F ("fGlide", "Glide",        0.f, 250.f, 20.f, 0.5f);
    F ("fMix",   "Filter Mix",   0.f, 1.f, 0.f);

    C ("aRate",  "Amp Rate",     kRates, 2);
    I ("aLen",   "Amp Length",   1, 32, 16);
    F ("tDepth", "Amp Depth",    0.f, 1.f, 0.f);
    F ("tGlide", "Amp Glide",    0.f, 50.f, 3.f, 0.5f);

    C ("pRate",  "Pan Rate",     kRates, 2);
    I ("pLen",   "Pan Length",   1, 32, 16);
    F ("pDepth", "Pan Depth",    0.f, 1.f, 0.f);
    F ("pGlide", "Pan Smooth",   0.f, 50.f, 5.f, 0.5f);

    // modulation sources: each has an amount (-1..1) and a destination
    F ("fAtk",   "Follower Attack",  0.5f, 100.f, 2.f, 0.4f);
    F ("fRel",   "Follower Release", 5.f, 1000.f, 90.f, 0.4f);
    F ("fEnv",   "Follower Amount",  -1.f, 1.f, 0.f);
    C ("fEnvDst","Follower Dest",    kDests, 0);

    C ("trigSrc","Trigger Source",   kTrig, 2);
    F ("trigSens","Trigger Sens",    0.f, 1.f, 0.5f);
    F ("eAtk",   "Env Attack",       0.5f, 500.f, 3.f, 0.4f);
    F ("eDec",   "Env Decay",        10.f, 4000.f, 400.f, 0.4f);
    F ("eAmt",   "Env Amount",       -1.f, 1.f, 0.f);
    C ("eDst",   "Env Dest",         kDests, 0);

    F ("pkRel",  "Peak Release",     0.05f, 10.f, 2.f, 0.5f);
    F ("pkAmt",  "Peak Amount",      -1.f, 1.f, 0.f);
    C ("pkDst",  "Peak Dest",        kDests, 0);

    C ("lfoShape","LFO Shape",       kLfoShapes, 0);
    F ("lfoRate","LFO Rate",         0.02f, 20.f, 2.f, 0.4f);
    B ("lfoSync","LFO Sync",         false);
    C ("lfoDiv", "LFO Division",     kLfoDivs, 3);
    F ("lfoAmt", "LFO Amount",       -1.f, 1.f, 0.f);
    C ("lfoDst", "LFO Dest",         kDests, 0);

    I ("noteLo", "Note Low",         0, 126, 36);
    I ("noteHi", "Note High",        1, 127, 96);
    F ("nAmt",   "Note Amount",      -1.f, 1.f, 0.f);
    C ("nDst",   "Note Dest",        kDests, 0);

    // sequence restart
    C ("rsMode", "Restart Mode",     kRsModes, 1);
    I ("rsNum",  "Restart Note/CC",  0, 127, 24);
    B ("rsPick", "Restart On Pick",  false);

    // modulation effect (one at a time) and volume swell
    C ("mfType", "ModFX Type",   kModFx, 0);
    F ("mfRate", "ModFX Rate",   0.05f, 10.f, 0.8f, 0.4f);
    B ("mfSync", "ModFX Sync",   false);
    C ("mfDiv",  "ModFX Division", kLfoDivs, 3);
    B ("mfRandom","ModFX Random", false);
    F ("mfDepth","ModFX Depth",  0.f, 1.f, 0.5f);
    F ("mfFb",   "ModFX Feedback", -0.9f, 0.9f, 0.f);
    F ("mfMix",  "ModFX Mix",    0.f, 1.f, 0.5f);
    F ("swAmt",  "Swell Depth",  0.f, 1.f, 0.f);
    F ("swAtk",  "Swell Rise",   20.f, 4000.f, 600.f, 0.5f);
    F ("swRel",  "Swell Fall",   100.f, 8000.f, 2000.f, 0.5f);

    F ("drive",  "Drive",        0.f, 1.f, 0.f);

    F ("dMix",   "Delay Mix",    0.f, 1.f, 0.f);
    C ("dTime",  "Delay Time",   kDelays, 2);
    F ("dFb",    "Feedback",     0.f, 0.95f, 0.4f);
    B ("dPing",  "Ping-Pong",    true);

    F ("rvMix",  "Reverb Mix",   0.f, 1.f, 0.f);
    F ("rvSize", "Reverb Size",  0.f, 1.f, 0.5f);

    F ("comp",   "Compressor",   0.f, 1.f, 0.f);

    F ("sLevel", "Synth Level",  0.f, 1.f, 0.7f);
    C ("sWave",  "Synth Wave",   kWaves, 0);
    F ("sRel",   "Synth Release",0.01f, 2.f, 0.3f, 0.4f);

    B ("drOn",   "Drums On",     false);
    F ("drLevel","Drum Level",   0.f, 1.f, 0.7f);

    F ("master", "Master",       0.f, 1.f, 1.f);

    F ("inGain", "Input Gain",   -12.f, 24.f, 0.f);
    F ("gate",   "Noise Gate",   0.f, 1.f, 0.f);
    F ("mix",    "Dry/Wet",      0.f, 1.f, 1.f);
    F ("bpm",    "Tempo",        40.f, 240.f, 120.f);
    B ("hostSync","Host Sync",   true);
    return l;
}

// ============================================================ construction
RhythmFXProcessor::RhythmFXProcessor()
    : AudioProcessor (BusesProperties()
#if ! JucePlugin_IsSynth
                      .withInput ("Input", juce::AudioChannelSet::stereo(), true)
#endif
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
#define X(n) p.n = apvts.getRawParameterValue (#n);
    RFX_PARAM_LIST(X)
#undef X

    for (size_t i = 0; i < (size_t) kSteps; ++i)
    {
        filterSteps[i].store (kPresets[0].filt[i % 16]);
        ampSteps[i].store (kPresets[0].amp[i % 16]);
        panSteps[i].store (0.5f);
    }
    for (auto& m : drumMask) m.store (0);
}

bool RhythmFXProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
#if JucePlugin_IsSynth
    return out == juce::AudioChannelSet::stereo();
#else
    const auto in = l.getMainInputChannelSet();
    if (in == out) return true;                                                     // mono->mono, stereo->stereo
    return in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo();   // guitar on a stereo track
#endif
}

void RhythmFXProcessor::prepareToPlay (double sr, int blockSize)
{
    scratchR.assign ((size_t) std::max (blockSize, 1), 0.f);
    dryL.assign (scratchR.size(), 0.f);
    dryR.assign (scratchR.size(), 0.f);
    gate.prepare (sr);
    envFollower.prepare (sr);
    envGen = rfx::AdEnv();
    noteDet.prepare (sr);
    peakHold.prepare (sr);
    lfo = rfx::Lfo();
    noteSmooth.setTimeMs (8.f, sr);
    swSmooth.setTimeMs (1.f, sr); swSmooth.z = 1.f;
    mfSmooth.setTimeMs (25.f, sr);
    swell.reset(); mfLfo = rfx::Lfo(); modFx.prepare (sr); mfCycles = 0.0;
    for (auto& v : vow) v.reset();
    noteTarget = 0.f; lfoCycles = 0.0; restartPpq = 0.0; wasPlaying = false; rsCCHigh = false;
    for (auto& s : svf) s.reset();
    for (auto& s : svf2) s.reset();
    delay.prepare (sr);
    compressor.prepare (sr);
    drums.prepare (sr);
    synth.prepare (sr);
    reverb.setSampleRate (sr);
    reverb.reset();
}

// ============================================================ audio
void RhythmFXProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    const double sr = getSampleRate();
    if (nch < 1) return;
    if ((int) scratchR.size() < n) { scratchR.resize ((size_t) n); dryL.resize ((size_t) n); dryR.resize ((size_t) n); }

#if JucePlugin_IsSynth
    buffer.clear();
#endif
    float* L = buffer.getWritePointer (0);
    float* R = nch > 1 ? buffer.getWritePointer (1) : scratchR.data();   // mono output: R is a scratch channel
#if ! JucePlugin_IsSynth
    if (getTotalNumInputChannels() == 1) std::copy (L, L + n, R);         // mono guitar -> both sides
#endif
    float* dL = dryL.data();
    float* dR = dryR.data();

    // ---- transport. Host Sync on: follow the host's tempo / position while it plays; otherwise (or with
    //      no host tempo, e.g. standalone) use the Tempo knob and free-run so the effect stays audible.
    double bpm = (double) p.bpm->load(), ppq = internalPpq;
    bool hostPlaying = false;
    if (p.hostSync->load() > 0.5f)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) if (*b > 20.0) bpm = *b;
                if (pos->getIsPlaying())
                {
                    hostPlaying = true;
                    if (auto q = pos->getPpqPosition()) ppq = *q;
                }
            }
    if (hostPlaying && ! wasPlaying) restartPpq = 0.0;       // transport start: realign to the host grid
    wasPlaying = hostPlaying;
    const double ppqInc = bpm / (60.0 * sr);

    // ---- parameter snapshot
    const int   rate = (int) p.rate->load(), len = juce::jlimit (1, kSteps, (int) p.length->load());
    const int   aLen = juce::jlimit (1, kSteps, (int) p.aLen->load()), pLen = juce::jlimit (1, kSteps, (int) p.pLen->load());
    const float swing = p.swing->load();
    const int   fType = (int) p.fType->load();
    const bool  slope24 = (int) p.fSlope->load() == 1;
    const float fLow = p.fLow->load(), fHigh = std::max (p.fHigh->load(), fLow * 1.01f), fRes = p.fRes->load(), fMix = p.fMix->load();
    const float tDepth = p.tDepth->load(), pDepth = p.pDepth->load(), drive = p.drive->load();
    const float dMix = p.dMix->load(), dFb = p.dFb->load(); const bool ping = p.dPing->load() > 0.5f;
    const float delaySamples = (float) (kDelayBeats[juce::jlimit (0, 5, (int) p.dTime->load())] * 60.0 / bpm * sr);
    const float rvMix = p.rvMix->load(), comp = p.comp->load();
    const float sLevel = p.sLevel->load(), sRel = p.sRel->load(); const int sWave = (int) p.sWave->load();
    const bool drOn = p.drOn->load() > 0.5f; const float drLevel = p.drLevel->load(), master = p.master->load();
    const float inG = std::pow (10.f, p.inGain->load() / 20.f), gateAmt = p.gate->load(), gateTh = rfx::NoiseGate::thresholdLinear (gateAmt);
    const float mixAmt = p.mix->load();
    const double spb  = rfx::StepClock::stepsPerBeat (rate);
    const double spbA = rfx::StepClock::stepsPerBeat ((int) p.aRate->load());
    const double spbP = rfx::StepClock::stepsPerBeat ((int) p.pRate->load());
    fSmooth.setTimeMs (p.fGlide->load(), sr);
    aSmooth.setTimeMs (p.tGlide->load(), sr);
    pSmooth.setTimeMs (p.pGlide->load(), sr);

    // modulation section: attack/decay shapes, triggers, LFO, restart
    envFollower.setTimes (p.fAtk->load(), p.fRel->load(), sr);
    envGen.setTimes (p.eAtk->load(), p.eDec->load(), sr);
    peakHold.setRelease (p.pkRel->load(), sr);
    const int   trigSrc = (int) p.trigSrc->load();                   // 0 audio, 1 MIDI, 2 both
    const float trigSens = p.trigSens->load();
    const int   lfoShape = (int) p.lfoShape->load();
    const bool  lfoSync = p.lfoSync->load() > 0.5f;
    const double lfoHz = (double) p.lfoRate->load(), lfoBeats = kLfoBeats[juce::jlimit (0, 5, (int) p.lfoDiv->load())];
    const int   noteLo = (int) p.noteLo->load(), noteHi = std::max ((int) p.noteHi->load(), noteLo + 1);
    const int   rsMode = (int) p.rsMode->load(), rsNum = (int) p.rsNum->load();
    const bool  rsPick = p.rsPick->load() > 0.5f;
    const float amts[5] = { p.fEnv->load(), p.eAmt->load(), p.pkAmt->load(), p.lfoAmt->load(), p.nAmt->load() };
    const int   dsts[5] = { (int) p.fEnvDst->load(), (int) p.eDst->load(), (int) p.pkDst->load(), (int) p.lfoDst->load(), (int) p.nDst->load() };

    const float fRand = p.fRand->load();
    const int   arpScale = (int) p.arpScale->load(), arpRoot = (int) p.arpRoot->load();
    const int   mfType = (int) p.mfType->load();
    const bool  mfSync = p.mfSync->load() > 0.5f, mfRandom = p.mfRandom->load() > 0.5f;
    const double mfHz = (double) p.mfRate->load(), mfBeats = kLfoBeats[juce::jlimit (0, 5, (int) p.mfDiv->load())];
    const float mfDepth = p.mfDepth->load(), mfFb = p.mfFb->load(), mfMix = p.mfMix->load();
    const float swDepth = p.swAmt->load();
    swell.setup (swDepth, p.swAtk->load(), p.swRel->load(), sr);
    if (swDepth <= 0.001f) { swell.reset(); swSmooth.z = 1.f; }
    float bpGain = 1.f;

    const float lnRatio = std::log (fHigh / fLow);
    int lastStep = 0, lastAmp = 0, lastPan = 0, lastDrum = 0;
    auto midiIt = midi.begin();

    for (int i = 0; i < n; ++i, ppq += ppqInc)
    {
        bool restart = (i == 0) && restartRequested.exchange (false);      // Restart button
        bool midiTrig = false; float trigVel = 1.f;

        // MIDI (sample-accurate): restart command, synth, note-number mod source, note trigger
        while (midiIt != midi.end() && (*midiIt).samplePosition <= i)
        {
            const auto m = (*midiIt).getMessage();
            const bool isRestartNote = (rsMode == 1 && (m.isNoteOn() || m.isNoteOff()) && m.getNoteNumber() == rsNum);
            if (m.isNoteOn())
            {
                if (isRestartNote) restart = true;                        // consumed: not played, not a trigger
                else
                {
                    synth.noteOn (m.getNoteNumber(), m.getFloatVelocity());
                    noteTarget = juce::jlimit (0.f, 1.f, (float) (m.getNoteNumber() - noteLo) / (float) (noteHi - noteLo));
                    if (trigSrc != 0) { midiTrig = true; trigVel = m.getFloatVelocity(); }
                }
            }
            else if (m.isNoteOff())           { if (! isRestartNote) synth.noteOff (m.getNoteNumber()); }
            else if (m.isAllNotesOff() || m.isAllSoundOff()) synth.allOff();
            else if (m.isController() && rsMode == 2 && m.getControllerNumber() == rsNum)
            {
                const bool hi = m.getControllerValue() >= 64;             // rising edge through 64 restarts
                if (hi && ! rsCCHigh) restart = true;
                rsCCHigh = hi;
            }
            ++midiIt;
        }

        // guitar input
        float xl = 0.f, xr = 0.f, level = 0.f, followN = 0.f;
        bool audioTrig = false;
#if ! JucePlugin_IsSynth
        xl = L[i] * inG; xr = R[i] * inG;                       // input trim
        gate.process (xl, xr, gateAmt, gateTh);
        dL[i] = xl; dR[i] = xr;                                 // dry tap for Dry/Wet
        level = std::max (std::fabs (xl), std::fabs (xr));
        followN = std::min (1.f, envFollower.process (level) * 4.f);
        if (trigSrc != 1) audioTrig = noteDet.process (level, trigSens);
#endif

        // note triggers -> envelope generator, peak hold, optional sequence restart on pick
        if (midiTrig || audioTrig)
        {
            envGen.trigger();
            swell.trigger();
            if (audioTrig) peakHold.arm(); else peakHold.setValue (trigVel);
            if (audioTrig && rsPick) restart = true;
        }
        if (restart) { restartPpq = ppq; lfoCycles = 0.0; mfCycles = 0.0; }
        if (swDepth > 0.001f) { const float sg = swSmooth.process (swell.process()); xl *= sg; xr *= sg; }   // guitar only, after trigger detection
        const double rp = ppq - restartPpq;                      // position since the last restart

        // pattern clocks
        const auto fr = fClock.get (rp, spb,  swing, len);
        const auto ar = aClock.get (rp, spbA, swing, aLen);
        const auto pr = pClock.get (rp, spbP, swing, pLen);

        // modulation sources (follower, envelope, peak hold, LFO, MIDI note number)
        const double cyc = lfoSync ? rp / lfoBeats : (lfoCycles += lfoHz / sr);
        const float lfoV = lfo.get (lfoShape, cyc);
        const float envV = envGen.process();
        const float pkV  = peakHold.process (level);
        const float noteV = noteSmooth.process (noteTarget);
        const float bip[5] = { followN, envV, pkV, lfoV, noteV };
        const float uni[5] = { followN, envV, pkV, 0.5f + 0.5f * lfoV, noteV };
        float cvMod = 0.f, resMod = 0.f, ampMod = 1.f, panMod = 0.f, driveMod = 0.f;
        for (int k = 0; k < 5; ++k)
        {
            const float a = amts[k];
            if (std::fabs (a) < 0.001f) continue;
            switch (dsts[k])
            {
                case 0:  cvMod += a * bip[k]; break;
                case 1:  resMod += a * bip[k]; break;
                case 2:  ampMod *= a >= 0.f ? 1.f - a * (1.f - uni[k]) : 1.f + a * uni[k]; break;
                case 3:  panMod += a * bip[k] * 0.5f; break;
                default: driveMod += a * bip[k]; break;
            }
        }

        float stepV = filterSteps[(size_t) fr.step].load();
        if (fr.fresh) fRandVal = rand01();                                 // new random value on every filter step
        if (fRand > 0.001f) stepV += fRand * (fRandVal - stepV);
        const float cv  = juce::jlimit (0.f, 1.f, fSmooth.process (stepV) + cvMod);
        const float amp = aSmooth.process (ampSteps[(size_t) ar.step].load());
        const float panStep = pSmooth.process (panSteps[(size_t) pr.step].load());
        lastStep = fr.step; lastAmp = ar.step; lastPan = pr.step;

        // source: guitar (effect build) + built-in synth (MIDI)
        const float s = synth.process (sWave, sRel) * sLevel;
        xl += s; xr += s;

        // drive -> step filter -> step amp -> step pan
        const float drv = juce::jlimit (0.f, 1.f, drive + driveMod);
        xl = rfx::driveProcess (xl, drv);
        xr = rfx::driveProcess (xr, drv);

        if ((i & 7) == 0)
        {
            const float res = juce::jlimit (0.f, 1.f, fRes + resMod);
            if (fType == 3)                                      // vowel: cv is the A-E-I-O-U position
            {
                vow[0].setup (cv, res, sr);
                vow[1].setup (cv, res, sr);
            }
            else
            {
                const float fc = rfx::quantizeFreq (fLow * std::exp (lnRatio * cv), arpScale, arpRoot);   // arpeggio: snap to scale
                svf[0].setup (fc, res, sr);
                svf[1].setup (fc, res, sr);
                svf2[0].setup (fc, 0.f, sr);                     // 2nd stage: no resonance, just extra slope
                svf2[1].setup (fc, 0.f, sr);
                bpGain = fType == 1 ? 1.f + 3.f * res * res : 1.f;     // narrow resonant band-pass needs make-up gain
            }
        }
        float yl, yr;
        if (fType == 3) { yl = vow[0].process (xl); yr = vow[1].process (xr); }
        else
        {
            yl = svf[0].process (xl, fType); yr = svf[1].process (xr, fType);
            if (slope24) { yl = svf2[0].process (yl, fType); yr = svf2[1].process (yr, fType); }
            yl *= bpGain; yr *= bpGain;
        }
        xl += fMix * (yl - xl);
        xr += fMix * (yr - xr);

        const float g = (1.f - tDepth * (1.f - amp)) * ampMod;
        xl *= g; xr *= g;

        const float pos = juce::jlimit (0.f, 1.f, 0.5f + (panStep - 0.5f) * pDepth + panMod);
        if (std::fabs (pos - 0.5f) > 0.002f)                     // guitar/synth only; drums stay centred
        {
            float gl, gr;
            rfx::panGains (pos, 1.f, gl, gr);
            xl *= gl; xr *= gr;
        }

        // modulation effect: chorus / flanger / vibrato / rotary (synced, free, or random-stepped)
        if (mfType != 0)
        {
            const double mc = mfSync ? rp / mfBeats : (mfCycles += mfHz / sr);
            const float mod = mfRandom ? mfSmooth.process (mfLfo.get (4, mc)) : mfLfo.get (0, mc);
            modFx.process (xl, xr, mfType, mod, mc, mfDepth, mfFb, mfMix);
        }

        // drums (always 16th-note grid, 16 steps = one 4/4 bar; restart rewinds them too)
        const auto dr = dClock.get (rp, 4.0, swing, 16);
        lastDrum = dr.step;
        if (drOn && dr.fresh)
            for (int lane = 0; lane < 4; ++lane)
                if ((drumMask[(size_t) lane].load() >> dr.step) & 1) drums.trigger (lane, 1.f);
        if (drOn) { const float d = drums.process() * drLevel; xl += d; xr += d; }

        // comp -> delay
        compressor.process (xl, xr, comp);
        if (dMix > 0.001f) delay.process (xl, xr, delaySamples, dFb, dMix, ping);

        L[i] = xl; R[i] = xr;
    }

    // reverb (block-based) and master
    if (rvMix > 0.001f)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.3f + 0.65f * p.rvSize->load();
        rp.damping = 0.5f; rp.wetLevel = rvMix; rp.dryLevel = 1.f; rp.width = 1.f; rp.freezeMode = 0.f;
        reverb.setParameters (rp);
        reverb.processStereo (L, R, n);
    }
    for (int i = 0; i < n; ++i)
    {
        float oL = L[i], oR = R[i];
#if ! JucePlugin_IsSynth
        oL = dL[i] + mixAmt * (oL - dL[i]);                     // Dry/Wet
        oR = dR[i] + mixAmt * (oR - dR[i]);
#endif
        oL = juce::jlimit (-1.f, 1.f, oL * master);
        oR = juce::jlimit (-1.f, 1.f, oR * master);
        if (nch > 1) { L[i] = oL; R[i] = oR; }
        else         L[i] = 0.5f * (oL + oR);                   // mono track: sum the stereo result
    }

    internalPpq = ppq;
    currentStep.store (lastStep);
    currentAmpStep.store (lastAmp);
    currentPanStep.store (lastPan);
    currentDrumStep.store (lastDrum);
}

// ============================================================ programs
int RhythmFXProcessor::getNumPrograms() { return kNumPresets; }
const juce::String RhythmFXProcessor::getProgramName (int i) { return juce::isPositiveAndBelow (i, kNumPresets) ? kPresets[i].name : ""; }
void RhythmFXProcessor::setCurrentProgram (int i) { if (juce::isPositiveAndBelow (i, kNumPresets)) { currentProgram = i; applyPreset (i); } }

void RhythmFXProcessor::setParam (const char* id, float v)
{
    if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

void RhythmFXProcessor::applyPreset (int idx)
{
    const auto& q = kPresets[idx];
    // programs are 16 steps; they are tiled across all 32 so a 32-step length still sounds right
    for (size_t i = 0; i < (size_t) kSteps; ++i)
    {
        filterSteps[i].store (q.filt[i % 16]); ampSteps[i].store (q.amp[i % 16]); panSteps[i].store (q.pan[i % 16]);
    }
    drumMask[0].store (q.kick); drumMask[1].store (q.snare); drumMask[2].store (q.hat); drumMask[3].store (q.clap);

    // modulation amounts/destinations/LFO/pick-restart go back to defaults first so a program never inherits the last one's.
    // (trigger source/sensitivity, attack/decay times, restart mode/number and note range are setup, so they stay put)
    for (auto id : { "fEnv", "fEnvDst", "eAmt", "eDst", "pkAmt", "pkDst", "lfoShape", "lfoRate", "lfoSync", "lfoDiv",
                     "lfoAmt", "lfoDst", "nAmt", "nDst", "rsPick",
                     "fRand", "arpScale", "arpRoot", "mfType", "mfRate", "mfSync", "mfDiv", "mfRandom", "mfDepth", "mfFb", "mfMix",
                     "swAmt", "swAtk", "swRel" })
        if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->getDefaultValue());

    setParam ("rate", (float) q.rate); setParam ("length", (float) q.length); setParam ("swing", q.swing);
    setParam ("fLow", q.fLow); setParam ("fHigh", q.fHigh); setParam ("fRes", q.fRes);
    setParam ("fGlide", q.fGlide); setParam ("fMix", q.fMix); setParam ("tDepth", q.tDepth);
    setParam ("drive", q.drive); setParam ("dMix", q.dMix); setParam ("drOn", q.drOn); setParam ("fEnv", q.fEnv);
    setParam ("aRate", (float) (q.aRate < 0 ? q.rate : q.aRate));     setParam ("aLen", (float) (q.aLen < 0 ? q.length : q.aLen));
    setParam ("pRate", (float) (q.pRate < 0 ? q.rate : q.pRate));     setParam ("pLen", (float) (q.pLen < 0 ? q.length : q.pLen));
    setParam ("pDepth", q.pDepth); setParam ("fSlope", (float) q.fSlope);

    const auto& m = q.mod;
    setParam ("eAmt", m.eAmt); setParam ("eAtk", m.eAtk); setParam ("eDec", m.eDec); setParam ("eDst", (float) m.eDst);
    setParam ("lfoAmt", m.lfoAmt); setParam ("lfoRate", m.lfoRate); setParam ("lfoShape", (float) m.lfoShape);
    setParam ("lfoSync", (float) m.lfoSync); setParam ("lfoDiv", (float) m.lfoDiv); setParam ("lfoDst", (float) m.lfoDst);
    setParam ("nAmt", m.nAmt); setParam ("pkAmt", m.pkAmt); setParam ("rsPick", (float) m.rsPick);
    setParam ("fType", (float) m.fType); setParam ("fRand", m.fRand);
    setParam ("arpScale", (float) m.arpScale); setParam ("arpRoot", (float) m.arpRoot);
    setParam ("mfType", (float) m.mfType); setParam ("mfRate", m.mfRate); setParam ("mfSync", (float) m.mfSync);
    setParam ("mfDiv", (float) m.mfDiv); setParam ("mfRandom", (float) m.mfRandom);
    setParam ("mfDepth", m.mfDepth); setParam ("mfFb", m.mfFb); setParam ("mfMix", m.mfMix);
    setParam ("swAmt", m.swAmt); setParam ("swAtk", m.swAtk); setParam ("swRel", m.swRel);
}

// ============================================================ state
void RhythmFXProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("fSteps", stepsToString (filterSteps), nullptr);
    state.setProperty ("aSteps", stepsToString (ampSteps), nullptr);
    state.setProperty ("pSteps", stepsToString (panSteps), nullptr);
    state.setProperty ("kick",  drumMask[0].load(), nullptr);
    state.setProperty ("snare", drumMask[1].load(), nullptr);
    state.setProperty ("hat",   drumMask[2].load(), nullptr);
    state.setProperty ("clap",  drumMask[3].load(), nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void RhythmFXProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            stringToSteps (vt.getProperty ("fSteps", stepsToString (filterSteps)).toString(), filterSteps);
            stringToSteps (vt.getProperty ("aSteps", stepsToString (ampSteps)).toString(), ampSteps);
            stringToSteps (vt.getProperty ("pSteps", stepsToString (panSteps)).toString(), panSteps);
            drumMask[0].store ((int) vt.getProperty ("kick", 0));
            drumMask[1].store ((int) vt.getProperty ("snare", 0));
            drumMask[2].store ((int) vt.getProperty ("hat", 0));
            drumMask[3].store ((int) vt.getProperty ("clap", 0));
            currentProgram = juce::jlimit (0, kNumPresets - 1, (int) vt.getProperty ("program", 0));
            apvts.replaceState (vt);
        }
}

juce::AudioProcessorEditor* RhythmFXProcessor::createEditor() { return new RhythmFXEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RhythmFXProcessor(); }
