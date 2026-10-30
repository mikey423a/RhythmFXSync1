#pragma once
// ============================================================================================================
// New editor widgets (see HANDOFF.md section 12). Compiled ONLY when the project is configured with
// -DRFX_NEW_GUI=ON, so the default build is unaffected. NOT YET COMPILED against JUCE: expect a few fixes.
// The logic behind every widget lives in EditorLogic.h and is unit-tested (Tests/editor_logic_test.cpp);
// these classes only draw it and forward mouse events.
// ============================================================================================================
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include "PluginProcessor.h"
#include "EditorLogic.h"
#include "Theme.h"

#if RFX_NEW_GUI

#include <juce_dsp/juce_dsp.h>        // FFT for the spectrum view (CMake links juce_dsp when RFX_NEW_GUI is ON)

namespace rfxw
{
inline const juce::Colour kPanel   = rfx::theme::panelBottom, kOutline = rfx::theme::edgeStrong, kAccent = rfx::theme::accent,
                          kText    = rfx::theme::text,        kDim     = rfx::theme::dim,        kCell   = rfx::theme::cellA,
                          kBgDark  = rfx::theme::well,        kCyan    = rfx::theme::teal,       kPink   = rfx::theme::rose,
                          kGreen   = rfx::theme::sage;

inline juce::Font boldFont (float h) { return juce::Font (juce::FontOptions (h, juce::Font::bold)); }
inline juce::Font plainFont (float h) { return juce::Font (juce::FontOptions (h)); }

// A lane as the widgets see it: read / write a step, how many steps are active, which one is playing.
struct LaneRef
{
    juce::String name;
    juce::Colour colour;
    bool bipolar = false;                         // pan lane: 0.5 = centre
    int maxSteps = 32;
    std::function<float (int)>       get;
    std::function<void (int, float)> set;
    std::function<int()>             length;
    std::function<int()>             playStep;
};

// ============================================================================================ IDEA 1: pattern tools
class PatternToolBar : public juce::Component
{
public:
    explicit PatternToolBar (std::vector<LaneRef> l) : lanes (std::move (l))
    {
        for (auto* n : { "Ramp up", "Ramp down", "Sine", "Random", "Mirror", "< Shift", "Shift >", "Invert", "Clear", "Copy", "Paste" })
        {
            auto* b = buttons.add (new juce::TextButton (n));
            addAndMakeVisible (b);
            b->onClick = [this, name = juce::String (n)] { apply (name); };
        }
        for (size_t i = 0; i < lanes.size(); ++i) laneBox.addItem (lanes[i].name, (int) i + 1);
        laneBox.setSelectedId (1);
        addAndMakeVisible (laneBox);
        allLanes.setButtonText ("All lanes");
        addAndMakeVisible (allLanes);
        hint.setText ("Shift+Sine = 2 cycles.  Copy / Paste work on the selected lane only.", juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, kDim);
        addAndMakeVisible (hint);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (30);
        laneBox.setBounds (top.removeFromLeft (110));
        top.removeFromLeft (8);
        allLanes.setBounds (top.removeFromLeft (100));
        top.removeFromLeft (8);
        const int w = std::max (50, std::min (84, top.getWidth() / std::max (1, buttons.size())));
        for (auto* b : buttons) { b->setBounds (top.removeFromLeft (w).reduced (2, 0)); }
        hint.setBounds (r.removeFromTop (22));
    }

private:
    void apply (const juce::String& tool)
    {
        std::vector<int> targets;
        if (allLanes.getToggleState() && tool != "Copy" && tool != "Paste") for (size_t i = 0; i < lanes.size(); ++i) targets.push_back ((int) i);
        else targets.push_back (juce::jlimit (0, (int) lanes.size() - 1, laneBox.getSelectedId() - 1));

        for (int li : targets)
        {
            auto& L = lanes[(size_t) li];
            const int n = juce::jlimit (1, L.maxSteps, L.length());
            std::array<float, 32> v {};
            for (int i = 0; i < n; ++i) v[(size_t) i] = L.get (i);

            if      (tool == "Ramp up")   rfx::ui::toolRamp (v.data(), n, true);
            else if (tool == "Ramp down") rfx::ui::toolRamp (v.data(), n, false);
            else if (tool == "Sine")      rfx::ui::toolSine (v.data(), n, juce::ModifierKeys::currentModifiers.isShiftDown() ? 2.f : 1.f);
            else if (tool == "Random")    rfx::ui::toolRandom (v.data(), n, (uint32_t) juce::Random::getSystemRandom().nextInt());
            else if (tool == "Mirror")    rfx::ui::toolMirror (v.data(), n);
            else if (tool == "< Shift")   rfx::ui::toolShift (v.data(), n, -1);
            else if (tool == "Shift >")   rfx::ui::toolShift (v.data(), n, 1);
            else if (tool == "Invert")    rfx::ui::toolInvert (v.data(), n);
            else if (tool == "Clear")     rfx::ui::toolClear (v.data(), n, L.bipolar ? 0.5f : 0.f);
            else if (tool == "Copy")      { clipboard = v; clipboardLen = n; return; }
            else if (tool == "Paste")     { if (clipboardLen == 0) return; for (int i = 0; i < n; ++i) v[(size_t) i] = clipboard[(size_t) (i % clipboardLen)]; }

            for (int i = 0; i < n; ++i) L.set (i, v[(size_t) i]);
        }
    }

    std::vector<LaneRef> lanes;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::ComboBox laneBox;
    juce::ToggleButton allLanes;
    juce::Label hint;
    std::array<float, 32> clipboard {};
    int clipboardLen = 0;
};

// ============================================================================================ IDEA 4: orbit view
class OrbitView : public juce::Component, private juce::Timer
{
public:
    explicit OrbitView (std::vector<LaneRef> l) : lanes (std::move (l)) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (kPanel);        g.fillRoundedRectangle (b, 8.f);
        g.setColour (kOutline);      g.drawRoundedRectangle (b.reduced (0.5f), 8.f, 1.f);

        const auto c = b.getCentre();
        const auto rings = ringGeometry();
        for (size_t r = 0; r < lanes.size(); ++r)
        {
            const auto& lane = lanes[r];
            const auto& ring = rings[r];
            const int n = ring.steps, play = lane.playStep();
            const float gap = 0.012f;
            for (int s = 0; s < n; ++s)
            {
                const float a1 = rfx::ui::orbitStepStart (s, n) + gap, a2 = rfx::ui::orbitStepEnd (s, n) - gap;
                juce::Path track;
                track.addPieSegment (c.x - ring.rOuter, c.y - ring.rOuter, 2.f * ring.rOuter, 2.f * ring.rOuter, a1, a2, ring.rInner / ring.rOuter);
                g.setColour (kCell);
                g.fillPath (track);

                const float v = rfx::ui::clamp01 (lane.get (s));
                if (v > 0.02f)
                {
                    const float rv = ring.rInner + (ring.rOuter - ring.rInner) * v;
                    juce::Path bar;
                    bar.addPieSegment (c.x - rv, c.y - rv, 2.f * rv, 2.f * rv, a1, a2, ring.rInner / rv);
                    g.setColour (lane.colour.withAlpha (s == play ? 1.f : 0.55f));
                    g.fillPath (bar);
                }
            }
            // playhead
            const float a = rfx::ui::orbitStepMid (play % std::max (1, n), n), r2 = ring.rOuter + 10.f;
            const juce::Point<float> p1 (c.x + ring.rInner * std::sin (a), c.y - ring.rInner * std::cos (a));
            const juce::Point<float> p2 (c.x + r2 * std::sin (a), c.y - r2 * std::cos (a));
            g.setColour (juce::Colours::white);
            g.drawLine (juce::Line<float> (p1, p2), 2.f);
            g.setColour (lane.colour);
            g.fillEllipse (p2.x - 5.f, p2.y - 5.f, 10.f, 10.f);
        }
        g.setColour (kText);
        g.setFont (boldFont (22.f));
        g.drawText ("ORBIT", getLocalBounds().withSizeKeepingCentre (200, 30).translated (0, -10), juce::Justification::centred);
        g.setColour (kDim);
        g.setFont (plainFont (13.f));
        g.drawText ("click / drag = edit    wheel = rotate", getLocalBounds().withSizeKeepingCentre (260, 20).translated (0, 16), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override { edit (e); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto rings = ringGeometry();
        const auto c = getLocalBounds().toFloat().getCentre();
        const auto h = rfx::ui::orbitHitTest (rings.data(), (int) rings.size(), (float) e.x - c.x, (float) e.y - c.y);
        if (h.ring < 0) return;
        auto& L = lanes[(size_t) h.ring];
        const int n = rings[(size_t) h.ring].steps;
        std::array<float, 32> v {};
        for (int i = 0; i < n; ++i) v[(size_t) i] = L.get (i);
        rfx::ui::toolShift (v.data(), n, w.deltaY > 0.f ? 1 : -1);
        for (int i = 0; i < n; ++i) L.set (i, v[(size_t) i]);
    }

private:
    void timerCallback() override { repaint(); }

    std::vector<rfx::ui::OrbitRing> ringGeometry() const       // outer lane first (filter), then amp, then pan
    {
        const float maxR = std::min (getWidth(), getHeight()) * 0.5f - 16.f;
        const float lo[3] = { 0.80f, 0.59f, 0.41f }, hi[3] = { 1.00f, 0.77f, 0.56f };
        std::vector<rfx::ui::OrbitRing> r;
        for (size_t i = 0; i < lanes.size() && i < 3; ++i)
            r.push_back ({ juce::jlimit (1, lanes[i].maxSteps, lanes[i].length()), maxR * lo[i], maxR * hi[i] });
        return r;
    }
    void edit (const juce::MouseEvent& e)
    {
        const auto rings = ringGeometry();
        const auto c = getLocalBounds().toFloat().getCentre();
        const auto h = rfx::ui::orbitHitTest (rings.data(), (int) rings.size(), (float) e.x - c.x, (float) e.y - c.y);
        if (h.ring >= 0) lanes[(size_t) h.ring].set (h.step, h.value);
    }

    std::vector<LaneRef> lanes;
};

// ============================================================================================ IDEA 5: XY morph pad
class MorphPad : public juce::Component
{
public:
    // ids: the parameters the pad drives (they must all exist in the APVTS). labels: shown beside the bars.
    MorphPad (juce::AudioProcessorValueTreeState& s, std::vector<juce::String> paramIds, std::vector<juce::String> labelTexts)
        : apvts (s), ids (std::move (paramIds)), labels (std::move (labelTexts)), locked (ids.size(), false)
    {
        // starting scenes (normalised 0..1, same order as ids): A dry and tight, B wide and wet, C huge and thick
        const std::array<std::vector<float>, 3> start = { { { .10f, 0.f, .35f, 0.f, .15f, 0.f, .25f }, { .55f, .6f, .90f, .5f, .45f, .2f, .15f }, { .85f, .35f, .6f, .15f, .75f, .85f, .5f } } };
        for (size_t k = 0; k < 3; ++k) { morph.scene[k] = start[k]; morph.scene[k].resize (ids.size(), 0.5f); }

        for (size_t i = 0; i < ids.size(); ++i)
        {
            auto* sl = bars.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox));
            addAndMakeVisible (sl);
            attach.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, ids[i], *sl));
            auto* lb = nameLabels.add (new juce::Label ({}, labels[i]));
            lb->setColour (juce::Label::textColourId, kText);
            addAndMakeVisible (lb);
            auto* lk = lockBtns.add (new juce::ToggleButton ("lock"));
            lk->onClick = [this, i, lk] { locked[i] = lk->getToggleState(); };
            addAndMakeVisible (lk);
        }
        for (int k = 0; k < 3; ++k)
        {
            auto* b = sceneBtns.add (new juce::TextButton (juce::String ("Scene ") + juce::String::charToString ((juce::juce_wchar) ('A' + k))));
            b->setTooltip ("Click = recall.  Shift+click = save the current sound into this scene.");
            b->onClick = [this, k] { sceneClicked (k); };
            addAndMakeVisible (b);
        }
        dice.setButtonText ("RANDOMISE");
        dice.onClick = [this] { writeParams (rfx::ui::randomise (readParams(), locked, (uint32_t) juce::Random::getSystemRandom().nextInt(), 1.f)); };
        addAndMakeVisible (dice);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int side = std::min (r.getHeight() - 30, r.getWidth() * 55 / 100);
        pad = juce::Rectangle<int> (0, 0, side, side);
        auto right = r.withTrimmedLeft (side + 24);
        auto row = right.removeFromTop (34);
        for (auto* b : sceneBtns) { b->setBounds (row.removeFromLeft (84).reduced (2)); }
        dice.setBounds (row.removeFromLeft (120).reduced (2));
        right.removeFromTop (12);
        const int rh = std::min (54, right.getHeight() / std::max (1, (int) ids.size()));
        for (size_t i = 0; i < ids.size(); ++i)
        {
            auto line = right.removeFromTop (rh);
            nameLabels[(int) i]->setBounds (line.removeFromLeft (150));
            lockBtns[(int) i]->setBounds (line.removeFromRight (70));
            bars[(int) i]->setBounds (line.reduced (6, 8));
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (kBgDark);   g.fillRect (pad);
        g.setColour (kOutline);  g.drawRect (pad, 1);
        g.setColour (juce::Colour (0xff24262d));
        for (int i = 1; i < 8; ++i)
        {
            g.drawVerticalLine (pad.getX() + i * pad.getWidth() / 8, (float) pad.getY(), (float) pad.getBottom());
            g.drawHorizontalLine (pad.getY() + i * pad.getHeight() / 8, (float) pad.getX(), (float) pad.getRight());
        }
        g.setColour (kDim);
        g.setFont (boldFont (13.f));
        g.drawText ("SPACE  >", pad.withTop (pad.getBottom() + 4).withHeight (20), juce::Justification::centred);
        for (int k = 0; k < 3; ++k)
        {
            const auto p = toScreen (morph.pos[(size_t) k][0], morph.pos[(size_t) k][1]);
            g.setColour (kAccent);
            g.drawEllipse (p.x - 18.f, p.y - 18.f, 36.f, 36.f, 2.f);
            g.setColour (kText);
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + k)), juce::Rectangle<float> (p.x - 18.f, p.y - 18.f, 36.f, 36.f), juce::Justification::centred);
        }
        const auto q = toScreen (puck.x, puck.y);
        g.setGradientFill (juce::ColourGradient (kCyan.withAlpha (0.8f), q.x, q.y, kCyan.withAlpha (0.f), q.x + 26.f, q.y, true));
        g.fillEllipse (q.x - 26.f, q.y - 26.f, 52.f, 52.f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (q.x - 10.f, q.y - 10.f, 20.f, 20.f);
        g.setColour (kCyan);
        g.drawEllipse (q.x - 10.f, q.y - 10.f, 20.f, 20.f, 2.f);
    }

    void mouseDown (const juce::MouseEvent& e) override { if (pad.contains (e.getPosition())) { beginGestures(); dragging = true; moveTo (e); } }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging) moveTo (e); }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { dragging = false; endGestures(); } }

private:
    juce::Point<float> toScreen (float x, float y) const { return { (float) pad.getX() + x * (float) pad.getWidth(), (float) pad.getY() + y * (float) pad.getHeight() }; }
    void moveTo (const juce::MouseEvent& e)
    {
        puck.x = rfx::ui::clamp01 (((float) e.x - (float) pad.getX()) / (float) pad.getWidth());
        puck.y = rfx::ui::clamp01 (((float) e.y - (float) pad.getY()) / (float) pad.getHeight());
        writeParams (morph.blend (puck.x, puck.y, readParams(), locked));
        repaint();
    }
    std::vector<float> readParams() const
    {
        std::vector<float> v;
        for (auto& id : ids) v.push_back (apvts.getParameter (id)->getValue());     // normalised 0..1
        return v;
    }
    void writeParams (const std::vector<float>& v)
    {
        for (size_t i = 0; i < ids.size(); ++i)
            if (! locked[i]) apvts.getParameter (ids[i])->setValueNotifyingHost (v[i]);
    }
    void beginGestures() { for (auto& id : ids) apvts.getParameter (id)->beginChangeGesture(); }
    void endGestures()   { for (auto& id : ids) apvts.getParameter (id)->endChangeGesture(); }
    void sceneClicked (int k)
    {
        if (juce::ModifierKeys::currentModifiers.isShiftDown()) { morph.scene[(size_t) k] = readParams(); return; }      // save
        puck = { morph.pos[(size_t) k][0], morph.pos[(size_t) k][1] };                                                   // recall
        beginGestures(); writeParams (morph.blend (puck.x, puck.y, readParams(), locked)); endGestures();
        repaint();
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<juce::String> ids, labels;
    std::vector<bool> locked;
    rfx::ui::SceneMorph morph;
    juce::Point<float> puck { 0.5f, 0.5f };
    juce::Rectangle<int> pad;
    bool dragging = false;
    juce::OwnedArray<juce::Slider> bars;
    juce::OwnedArray<juce::Label> nameLabels;
    juce::OwnedArray<juce::ToggleButton> lockBtns;
    juce::OwnedArray<juce::TextButton> sceneBtns;
    juce::TextButton dice;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attach;
};

// ============================================================================================ IDEA 6: performance mode
class PerformPanel : public juce::Component, private juce::Timer
{
public:
    explicit PerformPanel (RhythmFXProcessor& p) : proc (p)
    {
        restart.setButtonText ("RESTART");
        restart.onClick = [this] { proc.restartRequested.store (true); };
        addAndMakeVisible (restart);
        tap.setButtonText ("TAP TEMPO");
        tap.onClick = [this]
        {
            const double bpm = tapper.tap (juce::Time::getMillisecondCounterHiRes());
            if (bpm > 0.0)
                if (auto* prm = proc.apvts.getParameter ("bpm")) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) bpm));
        };
        addAndMakeVisible (tap);
        sens.setSliderStyle (juce::Slider::LinearHorizontal);
        sens.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (sens);
        sensAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "trigSens", sens);
        startTimerHz (30);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (r.getHeight() * 55 / 100);
        restart.setBounds (top.removeFromLeft (top.getWidth() * 30 / 100).reduced (6));
        tap.setBounds (top.removeFromRight (top.getWidth() * 42 / 100).reduced (6));
        lightArea = top.reduced (6);
        meterArea = r.removeFromTop (60).reduced (40, 10);
        sens.setBounds (meterArea.withY (meterArea.getBottom() + 6).withHeight (24));
    }

    void paint (juce::Graphics& g) override
    {
        // pluck light
        const auto c = lightArea.toFloat().getCentre();
        const float R = std::min (lightArea.getWidth(), lightArea.getHeight()) * 0.30f;
        g.setGradientFill (juce::ColourGradient (kAccent.withAlpha (light.level), c.x, c.y, kAccent.withAlpha (0.f), c.x + R * 2.f, c.y, true));
        g.fillEllipse (c.x - R * 2.f, c.y - R * 2.f, R * 4.f, R * 4.f);
        g.setColour (kCell.interpolatedWith (kAccent, light.level));
        g.fillEllipse (c.x - R, c.y - R, 2.f * R, 2.f * R);
        g.setColour (kOutline);
        g.drawEllipse (c.x - R, c.y - R, 2.f * R, 2.f * R, 3.f);
        g.setColour (light.level > 0.5f ? juce::Colours::black : kDim);
        g.setFont (boldFont (R * 0.4f));
        g.drawText ("PLUCK", juce::Rectangle<float> (c.x - R, c.y - R, 2.f * R, 2.f * R), juce::Justification::centred);

        // tempo read-out under the tap button
        const float bpm = proc.apvts.getRawParameterValue ("bpm")->load();
        g.setColour (kAccent);
        g.setFont (boldFont (60.f));
        g.drawText (juce::String (bpm, 1), tap.getBounds().withTrimmedTop (tap.getHeight() / 2), juce::Justification::centred);
        g.setColour (kDim);
        g.setFont (plainFont (14.f));
        g.drawText ("BPM  (with Host sync ON the DAW tempo wins)", tap.getBounds().withTop (tap.getBottom() - 26), juce::Justification::centred);

        // input level meter (dB) with the pluck-detector floor marked
        g.setColour (kBgDark);   g.fillRoundedRectangle (meterArea.toFloat(), 6.f);
        const float db = juce::Decibels::gainToDecibels (levelShown, -72.f);
        const float w = (float) meterArea.getWidth() * juce::jlimit (0.f, 1.f, (db + 72.f) / 72.f);
        g.setColour (db > -6.f ? juce::Colour (0xffe6c93b) : kGreen);
        g.fillRoundedRectangle (meterArea.toFloat().withWidth (w), 6.f);
        const float floorDb = -36.f - 24.f * proc.apvts.getRawParameterValue ("trigSens")->load();     // same mapping as rfx::NoteTrigger
        const float fx = (float) meterArea.getX() + (float) meterArea.getWidth() * (floorDb + 72.f) / 72.f;
        g.setColour (kAccent);
        g.drawLine (fx, (float) meterArea.getY() - 8.f, fx, (float) meterArea.getBottom() + 8.f, 3.f);
        g.setColour (kDim);
        g.setFont (plainFont (13.f));
        g.drawText ("input level", meterArea.withX (meterArea.getX() - 40).withWidth (40), juce::Justification::centredRight);
        g.drawText ("pluck sensitivity: notes quieter than the orange line are ignored", sens.getBounds().withY (sens.getBottom()).withHeight (20), juce::Justification::centred);
    }

private:
    void timerCallback() override
    {
        const int pc = proc.pluckCount.load (std::memory_order_relaxed);
        if (pc != seenPlucks) { seenPlucks = pc; light.trigger(); }
        light.tick (1.f / 30.f);
        levelShown = std::max (levelShown * 0.85f, proc.inputLevel.load (std::memory_order_relaxed));
        repaint();
    }

    RhythmFXProcessor& proc;
    juce::TextButton restart, tap;
    juce::Slider sens;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sensAttach;
    juce::Rectangle<int> lightArea, meterArea;
    rfx::ui::TapTempo tapper;
    rfx::ui::PluckLight light;
    int seenPlucks = 0;
    float levelShown = 0.f;
};

// ============================================================================================ IDEA 3: live spectrum + filter curve
class SpectrumView : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView (RhythmFXProcessor& p) : proc (p), fft (kOrder), window ((size_t) kSize, juce::dsp::WindowingFunction<float>::hann, false)
    {
        magIn.fill (-100.f); magOut.fill (-100.f);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (kPanel);   g.fillRoundedRectangle (b, 8.f);
        g.setColour (kOutline); g.drawRoundedRectangle (b.reduced (0.5f), 8.f, 1.f);
        area = getLocalBounds().reduced (12, 12).withTrimmedBottom (14);

        g.setFont (plainFont (11.f));
        for (float hz : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
        {
            const float x = xOf (hz);
            g.setColour (juce::Colour (0xff2a2d36));
            g.drawVerticalLine ((int) x, (float) area.getY(), (float) area.getBottom());
            g.setColour (kDim);
            g.drawText (hz >= 1000.f ? juce::String (hz / 1000.f, 0) + "k" : juce::String ((int) hz), (int) x - 20, area.getBottom() + 1, 40, 14, juce::Justification::centred);
        }

        const int N = 160;
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        const float bw = (float) area.getWidth() / (float) N + 0.5f;
        for (int pass = 0; pass < 2; ++pass)
        {
            g.setColour (pass == 0 ? juce::Colour (0xff3a3d48) : kAccent);
            const auto& mag = pass == 0 ? magIn : magOut;
            for (int i = 0; i < N; ++i)
            {
                const float f0 = 30.f * std::pow (16000.f / 30.f, (float) i / (float) N), f1 = 30.f * std::pow (16000.f / 30.f, (float) (i + 1) / (float) N);
                const int b0 = juce::jlimit (1, kSize / 2 - 1, (int) (f0 / (sr * 0.5) * (kSize / 2))), b1 = juce::jlimit (b0, kSize / 2 - 1, (int) (f1 / (sr * 0.5) * (kSize / 2)));
                float m = -100.f; for (int k = b0; k <= b1; ++k) m = std::max (m, mag[(size_t) k]);
                const float y = yOfDb (m);
                if (y < (float) area.getBottom()) g.fillRect ((float) area.getX() + (float) i * (float) area.getWidth() / (float) N, y, bw, (float) area.getBottom() - y);
            }
        }
        drawFilter (g, sr);
    }

    void mouseDown (const juce::MouseEvent& e) override { dragging = handle.getDistanceFrom (e.position) < 18.f; if (dragging) for (auto* id : { "fLow", "fHigh", "fRes" }) proc.apvts.getParameter (id)->beginChangeGesture(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float newFc = hzOf (e.position.x);
        const float ratio = newFc / std::max (currentFc, 1.f);
        for (auto* id : { "fLow", "fHigh" })                                     // slide the whole sweep, keeping its width
        {
            auto* prm = proc.apvts.getParameter (id);
            const float cur = prm->convertFrom0to1 (prm->getValue());
            prm->setValueNotifyingHost (prm->convertTo0to1 (cur * ratio));        // convertTo0to1 clamps to the parameter's range
        }
        const float q = juce::jmap (juce::jlimit (0.f, 1.f, (e.position.y - (float) area.getY()) / (float) area.getHeight()), 0.f, 1.f, 14.6f, 0.6f);
        const float res = std::sqrt (std::max (0.f, (q - 0.6f) / 14.f));          // inverse of Q = 0.6 + res^2 * 14 (rfx::Svf::setup)
        proc.apvts.getParameter ("fRes")->setValueNotifyingHost (juce::jlimit (0.f, 1.f, res));
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { dragging = false; for (auto* id : { "fLow", "fHigh", "fRes" }) proc.apvts.getParameter (id)->endChangeGesture(); } }

private:
    static constexpr int kOrder = 11, kSize = 1 << kOrder;

    float xOf (float hz) const { return (float) area.getX() + (float) area.getWidth() * (std::log10 (hz) - std::log10 (30.f)) / (std::log10 (16000.f) - std::log10 (30.f)); }
    float hzOf (float x) const { return std::pow (10.f, std::log10 (30.f) + (x - (float) area.getX()) / (float) area.getWidth() * (std::log10 (16000.f) - std::log10 (30.f))); }
    float yOfDb (float db) const { return (float) area.getY() + (float) area.getHeight() * juce::jlimit (0.f, 1.f, -db / 90.f); }

    void timerCallback() override
    {
        const int pos = proc.specPos.load (std::memory_order_acquire);
        for (int pass = 0; pass < 2; ++pass)
        {
            const auto& src = pass == 0 ? proc.specIn : proc.specOut;
            std::array<float, 2 * kSize> buf {};
            for (int i = 0; i < kSize; ++i) buf[(size_t) i] = src[(size_t) ((pos - kSize + i) & (RhythmFXProcessor::kSpecSize - 1))];
            window.multiplyWithWindowingTable (buf.data(), (size_t) kSize);
            fft.performFrequencyOnlyForwardTransform (buf.data());
            auto& mag = pass == 0 ? magIn : magOut;
            for (int k = 0; k < kSize / 2; ++k)
            {
                const float db = juce::Decibels::gainToDecibels (buf[(size_t) k] / ((float) kSize * 0.25f), -100.f);   // 0 dB = full-scale sine
                mag[(size_t) k] = std::max (db, mag[(size_t) k] - 2.5f);                                               // fast rise, slow fall
            }
        }
        repaint();
    }

    void drawFilter (juce::Graphics& g, double sr)
    {
        const int step = juce::jlimit (0, 31, proc.currentStep.load());
        const float cv = proc.filterSteps[(size_t) step].load();
        const float lo = proc.apvts.getRawParameterValue ("fLow")->load(), hi = std::max (proc.apvts.getRawParameterValue ("fHigh")->load(), lo * 1.01f);
        const float res = proc.apvts.getRawParameterValue ("fRes")->load();
        const int type = (int) proc.apvts.getRawParameterValue ("fType")->load();
        const bool s24 = (int) proc.apvts.getRawParameterValue ("fSlope")->load() == 1;
        currentFc = lo * std::exp (std::log (hi / lo) * cv);
        const float Q = 0.6f + res * res * 14.f;
        if (type > 2) { handle = { -100.f, -100.f }; return; }                    // vowel filter has no simple curve
        juce::Path curve;
        for (int i = 0; i <= 200; ++i)
        {
            const float f = 30.f * std::pow (16000.f / 30.f, (float) i / 200.f), x = f / currentFc;
            const float den = std::sqrt ((1.f - x * x) * (1.f - x * x) + (x / Q) * (x / Q));
            float gain = type == 0 ? 1.f / den : (type == 1 ? (x / Q) / den : (x * x) / den);
            if (s24) gain *= gain;
            const float y = yOfDb (juce::Decibels::gainToDecibels (std::max (gain, 1e-4f), -90.f) - 6.f);
            if (i == 0) curve.startNewSubPath (xOf (f), y); else curve.lineTo (xOf (f), y);
        }
        g.setColour (kCyan);
        g.strokePath (curve, juce::PathStrokeType (2.5f));
        const float hy = (float) area.getY() + (float) area.getHeight() * (1.f - (Q - 0.6f) / 14.f) * 0.5f;
        handle = { xOf (currentFc), hy };
        g.setColour (juce::Colours::white);
        g.fillEllipse (handle.x - 8.f, handle.y - 8.f, 16.f, 16.f);
        g.setColour (kCyan);
        g.drawEllipse (handle.x - 8.f, handle.y - 8.f, 16.f, 16.f, 2.f);
        g.setColour (kText);
        g.setFont (plainFont (12.f));
        g.drawText (juce::String (currentFc / 1000.f, 2) + " kHz   Q " + juce::String (Q, 1), (int) handle.x + 12, (int) handle.y - 8, 130, 16, juce::Justification::left);
        juce::ignoreUnused (sr);
    }

    RhythmFXProcessor& proc;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, kSize / 2> magIn {}, magOut {};
    juce::Rectangle<int> area;
    juce::Point<float> handle { -100.f, -100.f };
    float currentFc = 1000.f;
    bool dragging = false;
};

} // namespace rfxw

#endif // RFX_NEW_GUI
