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
const juce::StringArray kDfx     { "Off", "Tremolo", "Chorus", "Vibrato", "Ring Mod", "Filter" };
const juce::StringArray kVoice   { "Digital", "Tape", "Analog" };
const juce::StringArray kDrive   { "Soft", "Tube", "Fuzz", "Fold" };          // = rfx::HiDrive::Soft / Tube / Fuzz / Fold
const juce::StringArray kPitch   { "Off", "Thicken", "Octave Up", "Octave Down", "Octaves", "Fifth Up" };   // = rfx::PitchFx order
const juce::StringArray kPhStg   { "4", "6", "8", "12" };
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
    // --- effect inside the delay's feedback loop (dFb / dTime / dPing: -1 = leave as is)
    int   dfxMode = 0;
    float dfxRate = 3.f, dfxDepth = .5f, dfxTone = .5f, dFb = -1.f;
    int   dTime = -1, dPing = -1;
    // --- filter character, arpeggio, mod fx, swell
    int   fType = 0;                                   // 0 LP, 1 BP, 2 HP, 3 Vowel
    float fRand = 0.f;
    int   arpScale = 0, arpRoot = 0;
    int   mfType = 0, mfSync = 0, mfDiv = 3, mfRandom = 0;
    float mfRate = 1.f, mfDepth = .5f, mfFb = 0.f, mfMix = .5f;
    float swAmt = 0.f, swAtk = 600.f, swRel = 2000.f;
    // --- hi-def effects (defaults = the parameter defaults, so an untouched program leaves them off)
    int   drType = 0;      float drTone = 1.f;
    float eqLow = 0.f, eqMid = 0.f, eqMidF = 1000.f, eqHigh = 0.f;
    int   pxMode = 0;      float pxMix = .5f, pxCents = 12.f;
    float spWidth = 1.f, spSpace = 0.f, spDepth = 0.f;
    float rvMix = -1.f, rvSize = -1.f, rvDamp = .4f, rvPre = 15.f, rvShim = 0.f;     // rvMix / rvSize: -1 = leave as is
    float phMix = 0.f, phRate = .5f, phDepth = .7f, phFb = .4f;   int phStg = 1;
    float rmMix = 0.f, rmFreq = 440.f;   int bcBits = 16, bcDown = 1;
    int   dlyVoice = 0;  float dlyWow = 0.f, dlyDuck = 0.f, rvDuck = 0.f;
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
    { "Tremolo Echoes", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.5f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.dfxMode = 1; m.dfxRate = 4.f; m.dfxDepth = .85f; m.dFb = .6f; m.dTime = 2; m.dPing = 1; }) },
    { "Chorus Echoes", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.5f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.dfxMode = 2; m.dfxRate = .8f; m.dfxDepth = .75f; m.dFb = .62f; m.dTime = 2; m.dPing = 1; }) },
    { "Vibrato Echoes", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.5f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.dfxMode = 3; m.dfxRate = 5.f; m.dfxDepth = .5f; m.dFb = .6f; m.dTime = 1; m.dPing = 1; }) },
    { "Ring Mod Echoes", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.45f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.dfxMode = 4; m.dfxDepth = .75f; m.dfxTone = .45f; m.dFb = .6f; m.dTime = 1; m.dPing = 1; }) },
    { "Dark Wobble Echoes", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.5f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.dfxMode = 5; m.dfxRate = .5f; m.dfxDepth = .35f; m.dfxTone = .45f; m.dFb = .72f; m.dTime = 0; m.dPing = 0; }) },
    { "Hi-Def Tube Crunch", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.45f, 0.f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.drType = 1; m.eqLow = 2.f; m.eqMid = -2.f; m.eqMidF = 800.f; m.eqHigh = 3.f; m.spSpace = .15f; m.spDepth = .2f; m.rvMix = .12f; m.rvSize = .3f; }) },
    { "Thick & Wide (Double Track)", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.15f, 0.f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.pxMode = 1; m.pxMix = .8f; m.pxCents = 20.f; m.spSpace = .5f; m.spWidth = 1.4f; m.eqHigh = 2.f; }) },
    { "Shimmer Cathedral", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.rvMix = .55f; m.rvSize = .85f; m.rvShim = .6f; m.rvDamp = .35f; m.rvPre = 25.f; m.spSpace = .25f; }) },
    { "Octave Down Fuzz", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.6f, 0.f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.drType = 2; m.pxMode = 3; m.pxMix = .7f; m.eqLow = 3.f; m.eqMid = -3.f; }) },
    { "3D Phaser Clean", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.phMix = .8f; m.phRate = .35f; m.phDepth = .8f; m.phFb = .5f; m.phStg = 2; m.spSpace = .4f; m.spDepth = .25f; m.rvMix = .15f; m.rvSize = .4f; }) },
    { "Ring Mod Lo-Fi", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.2f, 0.f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.rmMix = .6f; m.rmFreq = 330.f; m.bcBits = 8; m.bcDown = 4; }) },
    { "Tape Echo", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.42f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.dlyVoice = 1; m.dlyWow = .5f; m.dFb = .45f; m.dTime = 2; m.dPing = 0; m.rvMix = .12f; m.rvSize = .35f; }) },
    { "Analog Echo (BBD)", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.40f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.dlyVoice = 2; m.dlyWow = .3f; m.dFb = .55f; m.dTime = 1; m.dPing = 0; }) },
    { "Ducked Tape + Shimmer", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.45f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.dlyVoice = 1; m.dlyWow = .35f; m.dlyDuck = .7f; m.dFb = .5f; m.dTime = 2; m.dPing = 1; m.rvMix = .38f; m.rvSize = .72f; m.rvShim = .5f; m.rvDuck = .5f; }) },
    { "Cathedral Swell", flat (.5f), flat (1.f),
      0,0,0,0, 2,16, 200,4000, 0.4f, 20, 0.f, 0.f, 0.f, 0.0f, 0, 0.f, 0.f,
      kCentre, 0.f, -1, -1, -1, -1, 0, M ([] (PresetMod& m) { m.rvMix = .6f; m.rvSize = .9f; m.rvDamp = .35f; m.rvPre = 30.f; m.rvShim = .25f; m.rvDuck = .4f; m.spSpace = .3f; }) },
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
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, juce::StringArray { "Off", "On" }, def ? 1 : 0));   // choice type snaps to exactly 0/1
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
    C ("drType", "Drive Type",   kDrive, 0);
    F ("drTone", "Drive Tone",   0.f, 1.f, 1.f);

    F ("eqLow",  "EQ Low",       -12.f, 12.f, 0.f);
    F ("eqMid",  "EQ Mid",       -12.f, 12.f, 0.f);
    F ("eqMidF", "EQ Mid Freq",  200.f, 5000.f, 1000.f, 0.5f);
    F ("eqHigh", "EQ Air",       -12.f, 12.f, 0.f);

    C ("pxMode", "Pitch Mode",   kPitch, 0);
    F ("pxMix",  "Pitch Mix",    0.f, 1.f, 0.5f);
    F ("pxCents","Pitch Detune", 0.f, 50.f, 12.f);

    C ("phStg",  "Phaser Stages", kPhStg, 1);
    F ("phMix",  "Phaser Mix",   0.f, 1.f, 0.f);
    F ("phRate", "Phaser Rate",  0.05f, 8.f, 0.5f, 0.4f);
    F ("phDepth","Phaser Depth", 0.f, 1.f, 0.7f);
    F ("phFb",   "Phaser Feedback", -0.9f, 0.9f, 0.4f);

    F ("rmMix",  "Ring Mod Mix", 0.f, 1.f, 0.f);
    F ("rmFreq", "Ring Mod Freq",20.f, 4000.f, 440.f, 0.35f);
    I ("bcBits", "Crush Bits",   2, 16, 16);
    I ("bcDown", "Crush Rate Div", 1, 32, 1);

    F ("spWidth","Stereo Width", 0.f, 2.f, 1.f);
    F ("spSpace","3D Space",     0.f, 1.f, 0.f);
    F ("spDepth","3D Room",      0.f, 1.f, 0.f);

    F ("dMix",   "Delay Mix",    0.f, 1.f, 0.f);
    C ("dTime",  "Delay Time",   kDelays, 2);
    F ("dFb",    "Feedback",     0.f, 0.95f, 0.4f);
    B ("dPing",  "Ping-Pong",    true);
    C ("dlyVoice","Delay Voice",  kVoice, 0);
    F ("dlyWow", "Delay Wow",     0.f, 1.f, 0.f);
    F ("dlyDuck","Delay Duck",    0.f, 1.f, 0.f);
    C ("dfxMode","Repeat FX",    kDfx, 0);
    F ("dfxRate","Repeat FX Rate",  0.1f, 12.f, 3.f, 0.5f);
    F ("dfxDepth","Repeat FX Depth", 0.f, 1.f, 0.5f);
    F ("dfxTone","Repeat FX Tone",  0.f, 1.f, 0.5f);

    F ("rvMix",  "Reverb Mix",   0.f, 1.f, 0.f);
    F ("rvSize", "Reverb Size",  0.f, 1.f, 0.5f);
    F ("rvDamp", "Reverb Damping", 0.f, 1.f, 0.4f);
    F ("rvPre",  "Reverb Pre-delay", 0.f, 120.f, 15.f);
    F ("rvShim", "Reverb Shimmer", 0.f, 1.f, 0.f);
    F ("rvDuck", "Reverb Duck",   0.f, 1.f, 0.f);

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
    scratchR.assign ((size_t) std::max (blockSize, 65536), 0.f);   // sized once here so processBlock never allocates
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
    hiDrive.prepare(); eq.reset(); pitchFx.prepare (sr); spatial.prepare (sr); fdn.prepare (sr); dryLine.reset();
    phaser.prepare (sr); ringMod.prepare (sr); crusher.reset();
    for (auto* sm : { &smTDepth, &smPDepth, &smDrive, &smFMix, &smDMix, &smRvMix, &smPxMix, &smPhMix, &smRmMix, &smMfMix,
                      &smSpWidth, &smSpSpace, &smSpDepth, &smComp, &smMix, &smMaster })
        { sm->setTime (sr, 20.f); sm->reset(); }
    rvDuckEnv = 0.f;
    setLatencySamples (rfx::Oversampler4::kLatency);                // the 4x oversampled drive always runs
}

// ============================================================ audio
void RhythmFXProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    const double sr = getSampleRate();
    if (nch < 1) return;
    if ((int) scratchR.size() < n) return;   // absurdly large block (> 65536 samples): pass audio through untouched rather than allocate

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
    rfx::FbFxParams dfx;
    dfx.mode = (int) p.dfxMode->load(); dfx.rate = p.dfxRate->load(); dfx.depth = p.dfxDepth->load(); dfx.tone = p.dfxTone->load();
    const float delaySamples = (float) (kDelayBeats[juce::jlimit (0, 5, (int) p.dTime->load())] * 60.0 / bpm * sr);
    const float rvMix = p.rvMix->load(), comp = p.comp->load();
    const int   drType = (int) p.drType->load();
    const float drTone = p.drTone->load();
    const float eqLow = p.eqLow->load(), eqMid = p.eqMid->load(), eqMidF = p.eqMidF->load(), eqHigh = p.eqHigh->load();
    const bool  eqOn = std::fabs (eqLow) > 0.05f || std::fabs (eqMid) > 0.05f || std::fabs (eqHigh) > 0.05f;
    if (eqOn) eq.setup (sr, eqLow, eqMid, eqMidF, eqHigh);
    const int   pxMode = (int) p.pxMode->load();
    const float pxMix = p.pxMix->load(), pxCents = p.pxCents->load();
    const float phMix = p.phMix->load(), phRate = p.phRate->load(), phDepth = p.phDepth->load(), phFb = p.phFb->load();
    const int   phStg = (int) p.phStg->load();
    const float rmMix = p.rmMix->load(), rmFreq = p.rmFreq->load();
    const int   bcBits = (int) p.bcBits->load(), bcDown = (int) p.bcDown->load();
    const float spWidth = p.spWidth->load(), spSpace = p.spSpace->load(), spDepth = p.spDepth->load();
    if (rvMix > 0.001f) fdn.setParams (p.rvSize->load(), p.rvDamp->load(), p.rvPre->load(), p.rvShim->load());
    const float sLevel = p.sLevel->load(), sRel = p.sRel->load(); const int sWave = (int) p.sWave->load();
    const bool drOn = p.drOn->load() > 0.5f; const float drLevel = p.drLevel->load(), master = p.master->load();
    const float inG = std::pow (10.f, p.inGain->load() / 20.f), gateAmt = p.gate->load(), gateTh = rfx::NoiseGate::thresholdLinear (gateAmt);
    const float mixAmt = p.mix->load();
    smTDepth.setTarget (tDepth); smPDepth.setTarget (pDepth); smDrive.setTarget (drive); smFMix.setTarget (fMix);
    smDMix.setTarget (dMix); smRvMix.setTarget (rvMix); smPxMix.setTarget (p.pxMix->load()); smPhMix.setTarget (p.phMix->load());
    smRmMix.setTarget (p.rmMix->load()); smMfMix.setTarget (p.mfMix->load());
    smSpWidth.setTarget (p.spWidth->load()); smSpSpace.setTarget (p.spSpace->load()); smSpDepth.setTarget (p.spDepth->load());
    smComp.setTarget (comp); smMix.setTarget (mixAmt); smMaster.setTarget (master);
    rfx::DelayVoice dv;
    dv.voice = (int) p.dlyVoice->load(); dv.wow = p.dlyWow->load(); dv.duck = p.dlyDuck->load();
    const float rvDuckAmt = p.rvDuck->load();
    const float duckAtk = 1.f - std::exp (-1.f / (0.004f * (float) sr)), duckRel = 1.f - std::exp (-1.f / (0.25f * (float) sr));
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

    float blockPeak = 0.f;
    for (int i = 0; i < n; ++i, ppq += ppqInc)
    {
        // smoothed copies of the mix / level controls (they shadow the per-block values above): no zipper noise or clicks
        const float tDepth = smTDepth.next(), pDepth = smPDepth.next(), drive = smDrive.next(), fMix = smFMix.next();
        const float dMix = smDMix.next(), rvMix = smRvMix.next(), pxMix = smPxMix.next(), phMix = smPhMix.next();
        const float rmMix = smRmMix.next(), mfMix = smMfMix.next(), comp = smComp.next();
        const float spWidth = smSpWidth.next(), spSpace = smSpSpace.next(), spDepth = smSpDepth.next();
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
        dryLine.process (xl, xr, dL[i], dR[i]);                 // dry tap for Dry/Wet, delayed to match the oversampled drive
        level = std::max (std::fabs (xl), std::fabs (xr));
        blockPeak = std::max (blockPeak, level);
        followN = std::min (1.f, envFollower.process (level) * 4.f);
        if (trigSrc != 1) audioTrig = noteDet.process (level, trigSens);
#endif

        // note triggers -> envelope generator, peak hold, optional sequence restart on pick
        if (midiTrig || audioTrig)
        {
            pluckCount.fetch_add (1, std::memory_order_relaxed);
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
        hiDrive.process (xl, xr, drType, drv, drTone, sr);       // 4x oversampled: no aliasing fizz

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

        // tone (3-band EQ) -> pitch (thicken / octaves) -> phaser
        if (eqOn)           eq.process (xl, xr);
        if (pxMode != 0)    pitchFx.process (xl, xr, pxMode, pxMix, pxCents);
        if (phMix > 0.001f) phaser.process (xl, xr, phStg, phMix, phRate, phDepth, phFb);

        // modulation effect: chorus / flanger / vibrato / rotary (synced, free, or random-stepped)
        if (mfType != 0)
        {
            const double mc = mfSync ? rp / mfBeats : (mfCycles += mfHz / sr);
            const float mod = mfRandom ? mfSmooth.process (mfLfo.get (4, mc)) : mfLfo.get (0, mc);
            modFx.process (xl, xr, mfType, mod, mc, mfDepth, mfFb, mfMix);
        }

        // ring modulator and bit / rate crusher
        ringMod.process (xl, xr, rmMix, rmFreq);
        crusher.process (xl, xr, bcBits, bcDown);

        // drums (always 16th-note grid, 16 steps = one 4/4 bar; restart rewinds them too)
        const auto dr = dClock.get (rp, 4.0, swing, 16);
        lastDrum = dr.step;
        if (drOn && dr.fresh)
            for (int lane = 0; lane < 4; ++lane)
                if ((drumMask[(size_t) lane].load() >> dr.step) & 1) drums.trigger (lane, 1.f);
        if (drOn) { const float d = drums.process() * drLevel; xl += d; xr += d; }

        // comp -> delay
        compressor.process (xl, xr, comp);
        if (dMix > 0.001f) delay.process (xl, xr, delaySamples, dFb, dMix, ping, dfx, dv);

        // FDN reverb (with shimmer) and the 3D stereo imager
        if (rvMix > 0.001f)
        {
            float wl, wr;
            fdn.process (xl, xr, wl, wr);
            rvDuckEnv += (level > rvDuckEnv ? duckAtk : duckRel) * (level - rvDuckEnv);                 // reverb ducking: tail returns when you stop
            const float rvG = rvMix * (1.f - rvDuckAmt * std::min (1.f, rvDuckEnv * 8.f));
            xl += rvG * wl; xr += rvG * wr;
        }
        spatial.process (xl, xr, spWidth, spSpace, spDepth);

        L[i] = xl; R[i] = xr;
    }

    // dry/wet, master and the soft output limiter
#if RFX_NEW_GUI
    int specWrite = specPos.load (std::memory_order_relaxed);
#endif
    for (int i = 0; i < n; ++i)
    {
        const float mixAmt = smMix.next(), master = smMaster.next();
        float oL = L[i], oR = R[i];
#if ! JucePlugin_IsSynth
        oL = dL[i] + mixAmt * (oL - dL[i]);                     // Dry/Wet
        oR = dR[i] + mixAmt * (oR - dR[i]);
#endif
        oL = juce::jlimit (-1.f, 1.f, rfx::softLimit (oL * master));      // soft knee, then a hard safety clip
        oR = juce::jlimit (-1.f, 1.f, rfx::softLimit (oR * master));
        if (nch > 1) { L[i] = oL; R[i] = oR; }
        else         L[i] = 0.5f * (oL + oR);                   // mono track: sum the stereo result
#if RFX_NEW_GUI
        specIn[(size_t) specWrite]  = 0.5f * (dL[i] + dR[i]);   // spectrum view taps: input and output, mono
        specOut[(size_t) specWrite] = 0.5f * (oL + oR);
        specWrite = (specWrite + 1) & (kSpecSize - 1);
#endif
    }
#if RFX_NEW_GUI
    specPos.store (specWrite, std::memory_order_release);
#endif
    inputLevel.store (blockPeak, std::memory_order_relaxed);

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
    for (auto id : { "dlyVoice", "dlyWow", "dlyDuck", "rvDuck", "drType", "drTone", "eqLow", "eqMid", "eqMidF", "eqHigh", "pxMode", "pxMix", "pxCents", "spWidth", "spSpace", "spDepth",
                     "rvDamp", "rvPre", "rvShim", "phMix", "phRate", "phDepth", "phFb", "phStg", "rmMix", "rmFreq", "bcBits", "bcDown", "dfxMode", "dfxRate", "dfxDepth", "dfxTone", "fEnv", "fEnvDst", "eAmt", "eDst", "pkAmt", "pkDst", "lfoShape", "lfoRate", "lfoSync", "lfoDiv",
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
    setParam ("drType", (float) m.drType); setParam ("drTone", m.drTone);
    setParam ("eqLow", m.eqLow); setParam ("eqMid", m.eqMid); setParam ("eqMidF", m.eqMidF); setParam ("eqHigh", m.eqHigh);
    setParam ("pxMode", (float) m.pxMode); setParam ("pxMix", m.pxMix); setParam ("pxCents", m.pxCents);
    setParam ("spWidth", m.spWidth); setParam ("spSpace", m.spSpace); setParam ("spDepth", m.spDepth);
    setParam ("rvDamp", m.rvDamp); setParam ("rvPre", m.rvPre); setParam ("rvShim", m.rvShim);
    if (m.rvMix >= 0.f)  setParam ("rvMix", m.rvMix);
    if (m.rvSize >= 0.f) setParam ("rvSize", m.rvSize);
    setParam ("phMix", m.phMix); setParam ("phRate", m.phRate); setParam ("phDepth", m.phDepth); setParam ("phFb", m.phFb);
    setParam ("phStg", (float) m.phStg); setParam ("rmMix", m.rmMix); setParam ("rmFreq", m.rmFreq);
    setParam ("bcBits", (float) m.bcBits); setParam ("bcDown", (float) m.bcDown);
    setParam ("dlyVoice", (float) m.dlyVoice); setParam ("dlyWow", m.dlyWow); setParam ("dlyDuck", m.dlyDuck); setParam ("rvDuck", m.rvDuck);
    setParam ("dfxMode", (float) m.dfxMode); setParam ("dfxRate", m.dfxRate);
    setParam ("dfxDepth", m.dfxDepth); setParam ("dfxTone", m.dfxTone);
    if (m.dFb >= 0.f)  setParam ("dFb", m.dFb);
    if (m.dTime >= 0)  setParam ("dTime", (float) m.dTime);
    if (m.dPing >= 0)  setParam ("dPing", (float) m.dPing);
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
    state.setProperty ("uiScale", uiScale.load(), nullptr);
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
            uiScale.store ((float) (double) vt.getProperty ("uiScale", 0.0));
            currentProgram = juce::jlimit (0, kNumPresets - 1, (int) vt.getProperty ("program", 0));
            apvts.replaceState (vt);
        }
}

juce::AudioProcessorEditor* RhythmFXProcessor::createEditor() { return new RhythmFXEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RhythmFXProcessor(); }
