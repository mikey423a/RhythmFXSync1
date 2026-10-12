#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "RhythmDSP.h"

// Every entry is both the APVTS parameter ID and the name of its cached raw-value pointer in `p`.
#define RFX_PARAM_LIST(X) \
    X(rate) X(length) X(swing) X(fType) X(fSlope) X(fLow) X(fHigh) X(fRes) X(fGlide) X(fMix) \
    X(aRate) X(aLen) X(tDepth) X(tGlide) X(pRate) X(pLen) X(pDepth) X(pGlide) \
    X(drive) X(dMix) X(dTime) X(dFb) X(dPing) X(rvMix) X(rvSize) X(comp) \
    X(sLevel) X(sWave) X(sRel) X(drOn) X(drLevel) X(master) \
    X(inGain) X(gate) X(mix) X(bpm) X(hostSync) \
    X(fAtk) X(fRel) X(fEnv) X(fEnvDst) \
    X(trigSrc) X(trigSens) X(eAtk) X(eDec) X(eAmt) X(eDst) \
    X(pkRel) X(pkAmt) X(pkDst) \
    X(lfoShape) X(lfoRate) X(lfoSync) X(lfoDiv) X(lfoAmt) X(lfoDst) \
    X(noteLo) X(noteHi) X(nAmt) X(nDst) \
    X(rsMode) X(rsNum) X(rsPick) \
    X(fRand) X(arpScale) X(arpRoot) \
    X(mfType) X(mfRate) X(mfSync) X(mfDiv) X(mfRandom) X(mfDepth) X(mfFb) X(mfMix) \
    X(swAmt) X(swAtk) X(swRel)

class RhythmFXProcessor : public juce::AudioProcessor
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;
    static constexpr int kSteps = 32;

    RhythmFXProcessor();
    ~RhythmFXProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "RhythmFX Sync"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- pattern data shared with the editor (lock-free)
    std::array<std::atomic<float>, kSteps> filterSteps, ampSteps, panSteps;   // pan: 0 = L, 0.5 = centre, 1 = R
    std::array<std::atomic<int>, 4>    drumMask;          // bit n = step n; lanes: kick, snare, hat, clap
    std::atomic<int> currentStep { 0 }, currentAmpStep { 0 }, currentPanStep { 0 }, currentDrumStep { 0 };
    std::atomic<bool> restartRequested { false };          // set by the editor's Restart button

    APVTS apvts;

private:
    static APVTS::ParameterLayout createLayout();
    void setParam (const char* id, float plainValue);
    void applyPreset (int index);

    struct Ptrs
    {
#define X(n) std::atomic<float>* n = nullptr;
        RFX_PARAM_LIST(X)
#undef X
    } p;

    int currentProgram = 0;
    double internalPpq = 0.0, restartPpq = 0.0, lfoCycles = 0.0;
    bool wasPlaying = false, rsCCHigh = false;
    float noteTarget = 0.f, fRandVal = 0.5f;
    double mfCycles = 0.0;
    uint32_t rngState = 0x1badf00du;
    float rand01() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return (float) (rngState & 0xFFFFFF) / 16777216.f; }

    rfx::StepClock fClock, aClock, pClock, dClock;
    rfx::Svf svf[2], svf2[2];                       // svf2 = second stage for the 24 dB slope
    rfx::OnePole fSmooth, aSmooth, pSmooth, noteSmooth, swSmooth, mfSmooth;
    rfx::Vowel vow[2];                              // talk-box filter (filter type "Vowel")
    rfx::Swell swell;
    rfx::ModFx modFx;
    rfx::Lfo mfLfo;
    rfx::StereoDelay delay;
    rfx::Compressor compressor;
    rfx::NoiseGate gate;
    rfx::EnvFollower envFollower;
    rfx::AdEnv envGen;
    rfx::NoteTrigger noteDet;
    rfx::PeakHold peakHold;
    rfx::Lfo lfo;
    std::vector<float> scratchR, dryL, dryR;
    rfx::DrumMachine drums;
    rfx::Synth synth;
    juce::Reverb reverb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RhythmFXProcessor)
};
