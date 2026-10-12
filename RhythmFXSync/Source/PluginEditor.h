#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>
#include <map>
#include "PluginProcessor.h"

// Paintable step lane: analog bars (filter / amp) or on-off cells (drums).
class StepLane : public juce::Component
{
public:
    StepLane (std::function<float (int)> getter, std::function<void (int, float)> setter, bool binaryLane, juce::Colour colour);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    int playStep = -1, length = 16;
    int numSteps = 32;             // cells across the lane (analog lanes 32, drum lanes 16)
    bool bipolar = false;          // pan lane: bars grow from the centre line (0.5)

private:
    void apply (const juce::MouseEvent&, bool isDown);
    std::function<float (int)> get;
    std::function<void (int, float)> set;
    bool binary;
    juce::Colour colour;
    float dragValue = 1.f;
};

class RhythmFXEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RhythmFXEditor (RhythmFXProcessor&);
    ~RhythmFXEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Look : juce::LookAndFeel_V4 { Look(); };
    struct Knob
    {
        juce::Slider s { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
        juce::Label l;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Combo
    {
        juce::ComboBox c;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };
    struct Toggle
    {
        juce::ToggleButton b;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };
    struct Panel { juce::Rectangle<int> r; juce::String title; };
    struct Caption { int x, y; juce::String text; };

    void timerCallback() override;
    Knob*   addKnob (const juce::String& id, const juce::String& label);
    Combo*  addCombo (const juce::String& id);
    Toggle* addToggle (const juce::String& id, const juce::String& text);
    void    placeKnob (const juce::String& id, int x, int y);
    void    addPanel (int x, int y, int w, int h, const juce::String& title);
    void    addCaption (int x, int y, const juce::String& text);

    RhythmFXProcessor& proc;
    Look look;
    std::map<juce::String, std::unique_ptr<Knob>>   knobs;
    std::map<juce::String, std::unique_ptr<Combo>>  combos;
    std::map<juce::String, std::unique_ptr<Toggle>> toggles;
    std::vector<Panel> panels;
    std::vector<Caption> captions;
    juce::ComboBox programBox;
    juce::TextButton restartBtn { "RESTART" };
    std::unique_ptr<StepLane> filterLane, ampLane, panLane, drumLanes[4];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RhythmFXEditor)
};
