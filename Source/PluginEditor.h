#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>
#include <map>
#include "PluginProcessor.h"
#include "Theme.h"
#include "EditorLogic.h"
#if RFX_NEW_GUI
 #include "EditorWidgets.h"
#endif

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
    struct Look : juce::LookAndFeel_V4
    {
        Look();
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                           int buttonW, int buttonH, juce::ComboBox&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    };
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
    struct Panel { juce::Rectangle<int> r; juce::String title; int page; };      // page -1 = always visible
    struct Caption { int x, y; juce::String text; int page; };

    void timerCallback() override;
    Knob*   addKnob (const juce::String& id, const juce::String& label);
    Combo*  addCombo (const juce::String& id);
    Toggle* addToggle (const juce::String& id, const juce::String& text);
    void    placeKnob (const juce::String& id, int x, int y);
    void    addPanel (int x, int y, int w, int h, const juce::String& title);
    void    addCaption (int x, int y, const juce::String& text);
    void    put (juce::Component& c, int x, int y, int w, int h);   // place + tag with the page being laid out
    void    setPage (int p);
    void    applyPage();
    void    layoutContent();                                    // places every control at the design size (called once)
    void    paintContent (juce::Graphics&);                     // draws the background and panels at the design size
    void    showScaleMenu();
    void    setUiScale (float s);

    RhythmFXProcessor& proc;
    Look look;

    // The whole interface is laid out once at this design size, inside `content`, and `content` is scaled to the window by a
    // transform (everything is vector drawing, so it stays sharp on retina / high-DPI screens at any size).
    static constexpr int kBaseW = 1510, kBaseH = 872;
    struct Content : juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint;
        void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
    };
    Content content;                                   // declared before the controls it holds, so it is destroyed after them
    juce::ComponentBoundsConstrainer constrainer;      // fixed aspect ratio, 50 - 200 %
    juce::TextButton scaleBtn { "UI 100%" };           // opens the size menu
    std::map<juce::String, std::unique_ptr<Knob>>   knobs;
    std::map<juce::String, std::unique_ptr<Combo>>  combos;
    std::map<juce::String, std::unique_ptr<Toggle>> toggles;
    std::vector<Panel> panels;
    std::vector<Caption> captions;
    juce::ComboBox programBox;
    juce::TextButton restartBtn { "RESTART" };
    static constexpr int kPages = RFX_NEW_GUI ? 6 : 4;          // tabs: Sequencer, Modulation, Effects, Space (+ Studio, Perform)
    juce::TextButton pageBtn[6];
#if RFX_NEW_GUI
    std::unique_ptr<rfxw::PatternToolBar> toolBar;
    std::unique_ptr<rfxw::OrbitView>      orbit;
    std::unique_ptr<rfxw::MorphPad>       morph;
    std::unique_ptr<rfxw::PerformPanel>   perform;
    std::unique_ptr<rfxw::SpectrumView>   spectrum;
#endif
    int page = 0, curPage = 0;                                  // current page / page being laid out
    std::vector<std::pair<juce::Component*, int>> tagged;       // every page-bound control and its page
    std::unique_ptr<StepLane> filterLane, ampLane, panLane, drumLanes[4];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RhythmFXEditor)
};
