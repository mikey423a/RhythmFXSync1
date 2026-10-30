#include "PluginEditor.h"

namespace
{
using namespace rfx::theme;                       // palette lives in Theme.h
constexpr int kKnobW = 80, kKnobH = 86;
}

// ================================================================ StepLane
StepLane::StepLane (std::function<float (int)> g, std::function<void (int, float)> s, bool b, juce::Colour c)
    : get (std::move (g)), set (std::move (s)), binary (b), colour (c) {}

void StepLane::paint (juce::Graphics& g)
{
    const float cw = (float) getWidth() / (float) numSteps;
    for (int i = 0; i < numSteps; ++i)
    {
        auto cell = juce::Rectangle<float> (cw * (float) i + 2.f, 1.f, cw - 4.f, (float) getHeight() - 2.f);
        const bool active = i < length;
        g.setColour ((i / 4) % 2 == 0 ? cellA : cellB);
        g.fillRoundedRectangle (cell, 4.f);

        const float v = get (i);
        if (bipolar)
        {
            const float midY = cell.getCentreY(), vy = cell.getY() + cell.getHeight() * (1.f - v);
            g.setColour (colour.withAlpha (active ? 0.88f : 0.22f));
            g.fillRoundedRectangle (cell.getX(), std::min (midY, vy), cell.getWidth(), std::max (2.f, std::abs (vy - midY)), 2.f);
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.drawHorizontalLine ((int) midY, cell.getX(), cell.getRight());
        }
        else if (v > 0.001f)
        {
            auto bar = cell.withTrimmedTop (cell.getHeight() * (1.f - v));
            g.setColour (colour.withAlpha (active ? 0.88f : 0.22f));
            g.fillRoundedRectangle (bar, 4.f);
        }
        if (i == playStep)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawRoundedRectangle (cell, 4.f, 1.5f);
        }
        else if (! active)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (cell, 4.f);
        }
    }
}

void StepLane::apply (const juce::MouseEvent& e, bool isDown)
{
    const int step = juce::jlimit (0, numSteps - 1, (int) ((float) e.x / ((float) getWidth() / (float) numSteps)));
    if (binary)
    {
        if (isDown) dragValue = get (step) > 0.5f ? 0.f : 1.f;
        set (step, dragValue);
    }
    else
    {
        float v = juce::jlimit (0.f, 1.f, 1.f - (float) e.y / (float) getHeight());
        if (bipolar) { if (std::abs (v - 0.5f) < 0.05f) v = 0.5f; }          // snap to centre
        else         { if (v < 0.06f) v = 0.f; }
        if (v > 0.94f) v = 1.f;
        if (bipolar && v < 0.06f) v = 0.f;
        set (step, v);
    }
    repaint();
}
void StepLane::mouseDown (const juce::MouseEvent& e) { apply (e, true); }
void StepLane::mouseDrag (const juce::MouseEvent& e) { apply (e, false); }

// ================================================================ Editor
RhythmFXEditor::Look::Look()
{
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, knobTrack);
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, well);
    setColour (juce::ComboBox::backgroundColourId, button);
    setColour (juce::ComboBox::outlineColourId, edgeStrong);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, panelTop.darker (0.1f));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, onAccent);
    setColour (juce::Label::textColourId, subText);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::TextButton::buttonColourId, button);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, onAccent);
    setColour (juce::TooltipWindow::backgroundColourId, panelTop.darker (0.2f));
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, edgeStrong);
    setColour (juce::BubbleComponent::backgroundColourId, panelTop.darker (0.2f));
    setColour (juce::BubbleComponent::outlineColourId, edgeStrong);
}

juce::Font RhythmFXEditor::Look::getTextButtonFont (juce::TextButton&, int) { return juce::Font (juce::FontOptions (13.f, juce::Font::bold)); }

// Rotary knob: a thin arc that fills with the accent colour, a soft disc inside it, and a short pointer. No hard outline.
void RhythmFXEditor::Look::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                             float startA, float endA, juce::Slider& s)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.f);
    const float radius = std::min (area.getWidth(), area.getHeight()) * 0.5f;
    const auto c = area.getCentre();
    const bool hot = s.isMouseOverOrDragging() && s.isEnabled();
    const float arcR = radius - 2.5f, thick = 4.5f, angle = startA + pos * (endA - startA);

    // disc
    const float faceR = radius - 10.f;
    g.setGradientFill (juce::ColourGradient (knobFace.brighter (hot ? 0.18f : 0.08f), c.x, c.y - faceR, knobFace.darker (0.30f), c.x, c.y + faceR, false));
    g.fillEllipse (c.x - faceR, c.y - faceR, 2.f * faceR, 2.f * faceR);

    // track and value arcs, rounded ends
    const juce::PathStrokeType stroke (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.f, startA, endA, true);
    g.setColour (knobTrack);
    g.strokePath (track, stroke);
    if (pos > 0.002f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.f, startA, angle, true);
        g.setColour (s.isEnabled() ? accent.withMultipliedBrightness (hot ? 1.12f : 1.f) : accent.withAlpha (0.35f));
        g.strokePath (value, stroke);
    }

    // pointer on the disc
    juce::Path pointer;
    pointer.startNewSubPath (c.x + faceR * 0.28f * std::sin (angle), c.y - faceR * 0.28f * std::cos (angle));
    pointer.lineTo (c.x + faceR * 0.82f * std::sin (angle), c.y - faceR * 0.82f * std::cos (angle));
    g.setColour (text.withAlpha (0.92f));
    g.strokePath (pointer, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void RhythmFXEditor::Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.f, 0.f, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.isMouseOver() ? buttonHover : button);
    g.fillRoundedRectangle (r, inputRadius);
    g.setColour (edgeStrong);
    g.drawRoundedRectangle (r, inputRadius, 1.f);
    juce::Path chevron;
    const float cx = (float) w - 14.f, cy = (float) h * 0.5f;
    chevron.startNewSubPath (cx - 4.f, cy - 2.f); chevron.lineTo (cx, cy + 2.f); chevron.lineTo (cx + 4.f, cy - 2.f);
    g.setColour (accent);
    g.strokePath (chevron, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// Tabs (radio group 4711) become pills: the selected one is filled with the accent colour, the others are flat until hovered.
void RhythmFXEditor::Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool tab = b.getRadioGroupId() == 4711, on = b.getToggleState();
    const float rad = tab ? r.getHeight() * 0.5f : inputRadius;
    if (tab) g.setColour (on ? accent.withAlpha (0.94f) : (hover ? buttonHover : juce::Colours::transparentBlack));
    else     g.setColour (down ? button.darker (0.2f) : (hover ? buttonHover : button));
    g.fillRoundedRectangle (r, rad);
    if (! (tab && on)) { g.setColour (tab && ! hover ? edge : edgeStrong); g.drawRoundedRectangle (r, rad, 1.f); }
}

void RhythmFXEditor::Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hover, bool)
{
    const float sz = 16.f;
    const auto box = juce::Rectangle<float> (2.f, ((float) b.getHeight() - sz) * 0.5f, sz, sz);
    const bool on = b.getToggleState();
    g.setColour (on ? accent : (hover ? buttonHover : well));
    g.fillRoundedRectangle (box, 5.f);
    g.setColour (on ? accent : edgeStrong);
    g.drawRoundedRectangle (box.reduced (0.5f), 5.f, 1.2f);
    if (on)
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + 4.f, box.getCentreY());
        tick.lineTo (box.getX() + 7.f, box.getCentreY() + 3.2f);
        tick.lineTo (box.getRight() - 4.f, box.getY() + 4.5f);
        g.setColour (onAccent);
        g.strokePath (tick, juce::PathStrokeType (2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (b.findColour (juce::ToggleButton::textColourId));
    g.setFont (juce::Font (juce::FontOptions (13.f)));
    g.drawText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (24), juce::Justification::centredLeft);
}

RhythmFXEditor::RhythmFXEditor (RhythmFXProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    content.setSize (kBaseW, kBaseH);
    content.onPaint = [this] (juce::Graphics& g) { paintContent (g); };
    addAndMakeVisible (content);

    // --- knobs, combos, toggles bound to parameters
    for (auto [id, text] : std::initializer_list<std::pair<const char*, const char*>> {
            { "length", "Length" }, { "aLen", "Length" }, { "pLen", "Length" }, { "swing", "Swing" }, { "bpm", "Tempo" },
            { "pDepth", "Depth" }, { "pGlide", "Smooth" },
            { "fAtk", "Attack" }, { "fRel", "Release" }, { "trigSens", "Sens" }, { "eAtk", "Attack" }, { "eDec", "Decay" },
            { "eAmt", "Amount" }, { "pkRel", "Release" }, { "pkAmt", "Amount" }, { "lfoRate", "Rate" }, { "lfoAmt", "Amount" },
            { "fRand", "Random" }, { "mfRate", "Rate" }, { "mfDepth", "Depth" }, { "mfFb", "Feedback" }, { "mfMix", "Mix" },
            { "swAmt", "Depth" }, { "swAtk", "Rise" }, { "swRel", "Fall" },
            { "drTone", "Tone" }, { "eqLow", "Low" }, { "eqMid", "Mid" }, { "eqMidF", "Mid Hz" }, { "eqHigh", "Air" },
            { "pxMix", "Mix" }, { "pxCents", "Detune" }, { "spWidth", "Width" }, { "spSpace", "Space" }, { "spDepth", "Room" },
            { "rvDamp", "Damping" }, { "rvPre", "Pre-delay" }, { "rvShim", "Shimmer" }, { "rvDuck", "Duck" },
            { "dlyWow", "Wow" }, { "dlyDuck", "Duck" },
            { "phMix", "Mix" }, { "phRate", "Rate" }, { "phDepth", "Depth" }, { "phFb", "Feedback" },
            { "rmMix", "Ring Mix" }, { "rmFreq", "Ring Hz" }, { "bcBits", "Bits" }, { "bcDown", "Rate Div" },
            { "noteLo", "Low" }, { "noteHi", "High" }, { "nAmt", "Amount" }, { "rsNum", "Note / CC" }, { "fLow", "Low" }, { "fHigh", "High" }, { "fRes", "Reso" },
            { "fGlide", "Glide" }, { "fMix", "Mix" }, { "tDepth", "Depth" }, { "tGlide", "Smooth" }, { "drive", "Drive" },
            { "dMix", "Mix" }, { "dFb", "Feedback" }, { "dfxRate", "Rate" }, { "dfxDepth", "Depth" }, { "dfxTone", "Tone" }, { "rvMix", "Mix" }, { "rvSize", "Size" }, { "comp", "Amount" },
            { "sLevel", "Level" }, { "sRel", "Release" }, { "drLevel", "Level" }, { "master", "Master" },
            { "inGain", "Input" }, { "gate", "Gate" }, { "fEnv", "Amount" }, { "mix", "Dry/Wet" } })
        addKnob (id, text);
    for (auto id : { "rate", "aRate", "pRate", "fType", "fSlope", "dTime", "dlyVoice", "dfxMode", "sWave",
                     "fEnvDst", "eDst", "pkDst", "lfoDst", "nDst", "trigSrc", "lfoShape", "lfoDiv", "rsMode",
                     "arpScale", "arpRoot", "mfType", "mfDiv", "drType", "pxMode", "phStg" }) addCombo (id);
    addToggle ("dPing", "Ping-pong");
    addToggle ("hostSync", "Host sync");
    addToggle ("lfoSync", "Sync");
    addToggle ("rsPick", "On pick");
    addToggle ("mfSync", "Sync");
    addToggle ("mfRandom", "Random");
    addToggle ("drOn", "Drums on");

    // --- step lanes
    auto& pr = proc;
    filterLane = std::make_unique<StepLane> ([&pr] (int i) { return pr.filterSteps[(size_t) i].load(); },
                                             [&pr] (int i, float v) { pr.filterSteps[(size_t) i].store (v); }, false, teal);
    ampLane    = std::make_unique<StepLane> ([&pr] (int i) { return pr.ampSteps[(size_t) i].load(); },
                                             [&pr] (int i, float v) { pr.ampSteps[(size_t) i].store (v); }, false, rose);
    panLane    = std::make_unique<StepLane> ([&pr] (int i) { return pr.panSteps[(size_t) i].load(); },
                                             [&pr] (int i, float v) { pr.panSteps[(size_t) i].store (v); }, false, sage);
    panLane->bipolar = true;
    const juce::Colour dc[4] = { drumKick, drumSnare, drumHat, drumClap };
    for (int k = 0; k < 4; ++k)
        drumLanes[k] = std::make_unique<StepLane> (
            [&pr, k] (int i) { return (float) ((pr.drumMask[(size_t) k].load() >> i) & 1); },
            [&pr, k] (int i, float v) { int m = pr.drumMask[(size_t) k].load(); m = v > 0.5f ? (m | (1 << i)) : (m & ~(1 << i)); pr.drumMask[(size_t) k].store (m); },
            true, dc[k]);
    for (auto* lane : { filterLane.get(), ampLane.get(), panLane.get() }) lane->numSteps = 32;
    for (auto& d : drumLanes) d->numSteps = 16;
    content.addAndMakeVisible (*filterLane);
    content.addAndMakeVisible (*ampLane);
    content.addAndMakeVisible (*panLane);
    for (auto& d : drumLanes) content.addAndMakeVisible (*d);

    // --- program menu
    for (int i = 0; i < proc.getNumPrograms(); ++i) programBox.addItem (proc.getProgramName (i), i + 1);
    programBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    programBox.onChange = [this] { proc.setCurrentProgram (programBox.getSelectedId() - 1); content.repaint(); };
    content.addAndMakeVisible (programBox);

    restartBtn.setTooltip ("Restart all sequences from step 1 (also: MIDI note / CC, or on pick - see RESTART panel)");
    restartBtn.onClick = [this] { proc.restartRequested.store (true); };
    content.addAndMakeVisible (restartBtn);

    // --- page tabs
    const char* pageNames[6] = { "SEQUENCER", "MODULATION", "EFFECTS", "SPACE", "STUDIO", "PERFORM" };
    for (int i = 0; i < kPages; ++i)
    {
        auto& b = pageBtn[i];
        b.setButtonText (pageNames[i]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (4711);
        b.setColour (juce::TextButton::buttonOnColourId, accent);
        b.setColour (juce::TextButton::textColourOnId, onAccent);
        b.setColour (juce::TextButton::textColourOffId, subText);
        b.setToggleState (i == 0, juce::dontSendNotification);
        b.onClick = [this, i] { setPage (i); };
        content.addAndMakeVisible (b);
    }

#if RFX_NEW_GUI
    {   // ---- new widgets (HANDOFF.md section 12). Lanes are described once and shared by the tool bar and the orbit view.
        auto lane = [this] (const char* name, juce::Colour col, std::array<std::atomic<float>, RhythmFXProcessor::kSteps>& steps,
                            const char* lenId, std::atomic<int>& playing, bool bipolar)
        {
            rfxw::LaneRef r;
            r.name = name; r.colour = col; r.bipolar = bipolar; r.maxSteps = RhythmFXProcessor::kSteps;
            r.get      = [&steps] (int i) { return steps[(size_t) i].load(); };
            r.set      = [&steps] (int i, float v) { steps[(size_t) i].store (rfx::ui::clamp01 (v)); };
            r.length   = [this, lenId] { return (int) proc.apvts.getRawParameterValue (lenId)->load(); };
            r.playStep = [&playing] { return playing.load(); };
            return r;
        };
        std::vector<rfxw::LaneRef> lanes { lane ("Filter", rfxw::kCyan,  proc.filterSteps, "length", proc.currentStep,    false),
                                           lane ("Amp",    rfxw::kPink,  proc.ampSteps,    "aLen",   proc.currentAmpStep, false),
                                           lane ("Pan",    rfxw::kGreen, proc.panSteps,    "pLen",   proc.currentPanStep, true) };
        toolBar  = std::make_unique<rfxw::PatternToolBar> (lanes);
        orbit    = std::make_unique<rfxw::OrbitView> (lanes);
        morph    = std::make_unique<rfxw::MorphPad> (proc.apvts,
                       std::vector<juce::String> { "rvMix", "rvShim", "spWidth", "pxCents", "dMix", "phMix", "drive" },
                       std::vector<juce::String> { "Reverb mix", "Shimmer", "Stereo width", "Detune", "Delay mix", "Phaser mix", "Drive" });
        perform  = std::make_unique<rfxw::PerformPanel> (proc);
        spectrum = std::make_unique<rfxw::SpectrumView> (proc);
        for (juce::Component* c : { (juce::Component*) toolBar.get(), (juce::Component*) orbit.get(), (juce::Component*) morph.get(),
                                    (juce::Component*) perform.get(), (juce::Component*) spectrum.get() })
            content.addAndMakeVisible (c);
    }
#endif

    scaleBtn.setTooltip ("Window size. You can also drag the bottom-right corner.");
    scaleBtn.onClick = [this] { showScaleMenu(); };
    content.addAndMakeVisible (scaleBtn);

    layoutContent();                                   // every control exists now: place them (at the design size)

    // opening size: the one saved with the session, else the largest that fits this screen (never above 100 %)
    float areaW = 1920.f, areaH = 1080.f;
    if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        areaW = (float) disp->userArea.getWidth();
        areaH = (float) disp->userArea.getHeight();
    }
    const float s0 = rfx::ui::initialScale (proc.uiScale.load(), areaW, areaH, (float) kBaseW, (float) kBaseH);
    constrainer.setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    constrainer.setSizeLimits (rfx::ui::scaledSize (kBaseW, rfx::ui::kMinScale), rfx::ui::scaledSize (kBaseH, rfx::ui::kMinScale),
                               rfx::ui::scaledSize (kBaseW, rfx::ui::kMaxScale), rfx::ui::scaledSize (kBaseH, rfx::ui::kMaxScale));
    setConstrainer (&constrainer);
    setResizable (true, true);                         // host may resize; bottom-right corner handle for standalone / hosts without one
    setSize (rfx::ui::scaledSize (kBaseW, s0), rfx::ui::scaledSize (kBaseH, s0));

    startTimerHz (30);
}

RhythmFXEditor::~RhythmFXEditor() { setLookAndFeel (nullptr); }

RhythmFXEditor::Knob* RhythmFXEditor::addKnob (const juce::String& id, const juce::String& label)
{
    auto k = std::make_unique<Knob>();
    k->l.setText (label, juce::dontSendNotification);
    k->l.setJustificationType (juce::Justification::centred);
    k->l.setFont (juce::Font (juce::FontOptions (12.5f)));
    k->l.setColour (juce::Label::textColourId, subText);
    k->s.setPopupDisplayEnabled (true, true, &content);
    content.addAndMakeVisible (k->s); content.addAndMakeVisible (k->l);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->s);
    auto* raw = k.get();
    knobs[id] = std::move (k);
    return raw;
}

RhythmFXEditor::Combo* RhythmFXEditor::addCombo (const juce::String& id)
{
    auto c = std::make_unique<Combo>();
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        c->c.addItemList (choice->choices, 1);
    content.addAndMakeVisible (c->c);
    c->att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, c->c);
    auto* raw = c.get();
    combos[id] = std::move (c);
    return raw;
}

RhythmFXEditor::Toggle* RhythmFXEditor::addToggle (const juce::String& id, const juce::String& text)
{
    auto t = std::make_unique<Toggle>();
    t->b.setButtonText (text);
    content.addAndMakeVisible (t->b);
    t->att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, t->b);
    auto* raw = t.get();
    toggles[id] = std::move (t);
    return raw;
}

void RhythmFXEditor::put (juce::Component& c, int x, int y, int w, int h)
{
    c.setBounds (x, y, w, h);
    tagged.emplace_back (&c, curPage);
}

void RhythmFXEditor::placeKnob (const juce::String& id, int x, int y)
{
    auto& k = *knobs[id];
    put (k.s, x + 8, y, 64, 64);
    put (k.l, x, y + 64, kKnobW, 18);
}

void RhythmFXEditor::addCaption (int x, int y, const juce::String& text) { captions.push_back ({ x, y, text, curPage }); }

void RhythmFXEditor::addPanel (int x, int y, int w, int h, const juce::String& title)
{
    panels.push_back ({ { x, y, w, h }, title, curPage });
}

void RhythmFXEditor::setPage (int p) { page = p; applyPage(); content.repaint(); }

void RhythmFXEditor::applyPage()
{
    for (auto& [c, pg] : tagged) c->setVisible (pg < 0 || pg == page);
}

void RhythmFXEditor::layoutContent()
{
    panels.clear();
    captions.clear();
    tagged.clear();
    programBox.setBounds (kBaseW - 270, 14, 256, 28);
    scaleBtn.setBounds (1128, 14, 104, 28);
    for (int i = 0; i < kPages; ++i) pageBtn[i].setBounds (340 + i * 128, 12, 122, 30);

    auto combo  = [this] (const char* id, int x, int y, int w) { put (combos[id]->c, x, y, w, 22); };
    auto toggle = [this] (const char* id, int x, int y, int w) { put (toggles[id]->b, x, y, w, 22); };
    const int h1 = 146;                                  // standard panel height

    // =============================== PAGE 0: SEQUENCER ===============================
    curPage = 0;
    {
        const int y1 = 58, ky = y1 + 52, cy = y1 + 26;
        addPanel (14, y1, 180, h1, "PATTERN");
        toggle ("hostSync", 24, cy, 150);
        placeKnob ("swing", 14, ky); placeKnob ("bpm", 94, ky);

        addPanel (202, y1, 650, h1, "STEP FILTER");
        combo ("fType", 212, cy, 100); combo ("fSlope", 318, cy, 80); combo ("rate", 404, cy, 80);
        combo ("arpScale", 500, cy, 100); combo ("arpRoot", 604, cy, 56);
        addCaption (500, y1 + 9, "ARP SCALE / KEY");
        int x = 212;
        for (auto id : { "length", "fLow", "fHigh", "fRes", "fGlide", "fRand", "fMix" }) { placeKnob (id, x, ky); x += kKnobW; }

        addPanel (860, y1, 250, h1, "STEP AMP");
        combo ("aRate", 870, cy, 80);
        placeKnob ("aLen", 860, ky); placeKnob ("tDepth", 940, ky); placeKnob ("tGlide", 1020, ky);

        addPanel (1118, y1, 268, h1, "STEP PAN");
        combo ("pRate", 1128, cy, 80);
        placeKnob ("pLen", 1118, ky); placeKnob ("pDepth", 1204, ky); placeKnob ("pGlide", 1290, ky);

        const int lx = 100, lw = kBaseW - lx - 14;
        put (*filterLane, lx, 214, lw, 70);
        put (*ampLane,    lx, 290, lw, 70);
        put (*panLane,    lx, 366, lw, 70);
        for (int k = 0; k < 4; ++k) put (*drumLanes[(size_t) k], lx, 446 + k * 32, lw, 28);

        const int y2 = 582, h2 = 136, k2 = y2 + 52;
        addPanel (14, y2, 190, h2, "SEQUENCE RESTART");
        combo ("rsMode", 24, y2 + 26, 96); toggle ("rsPick", 124, y2 + 26, 76);
        placeKnob ("rsNum", 14, k2);
        put (restartBtn, 104, k2 + 18, 90, 30);

        addPanel (212, y2, 190, h2, "SYNTH (MIDI)");
        combo ("sWave", 222, y2 + 26, 100);
        placeKnob ("sLevel", 212, k2); placeKnob ("sRel", 302, k2);

        addPanel (410, y2, 110, h2, "DRUMS");
        toggle ("drOn", 418, y2 + 26, 96);
        placeKnob ("drLevel", 420, k2);
    }

    // =============================== PAGE 1: MODULATION ===============================
    curPage = 1;
    {
        const int y2 = 58, k2 = y2 + 52, c2 = y2 + 26;
        addPanel (14, y2, 250, h1, "ENVELOPE FOLLOWER");
        combo ("fEnvDst", 24, c2, 90);
        placeKnob ("fAtk", 14, k2); placeKnob ("fRel", 94, k2); placeKnob ("fEnv", 174, k2);

        addPanel (272, y2, 410, h1, "NOTE ENVELOPE (PLUCK / MIDI)");
        combo ("trigSrc", 282, c2, 90); combo ("eDst", 378, c2, 90);
        placeKnob ("trigSens", 272, k2); placeKnob ("eAtk", 352, k2); placeKnob ("eDec", 432, k2); placeKnob ("eAmt", 512, k2);

        addPanel (690, y2, 170, h1, "PEAK HOLD");
        combo ("pkDst", 700, c2, 90);
        placeKnob ("pkRel", 690, k2); placeKnob ("pkAmt", 770, k2);

        addPanel (868, y2, 340, h1, "LFO");
        combo ("lfoShape", 878, c2, 84); toggle ("lfoSync", 966, c2, 62); combo ("lfoDiv", 1030, c2, 70); combo ("lfoDst", 1104, c2, 94);
        placeKnob ("lfoRate", 868, k2); placeKnob ("lfoAmt", 948, k2);

        addPanel (1216, y2, 280, h1, "MIDI NOTE NUMBER");
        combo ("nDst", 1226, c2, 90);
        placeKnob ("noteLo", 1216, k2); placeKnob ("noteHi", 1296, k2); placeKnob ("nAmt", 1376, k2);
    }

    // =============================== PAGE 2: EFFECTS ===============================
    curPage = 2;
    {
        int y = 58, k = y + 52, c = y + 26;
        addPanel (14, y, 180, h1, "DRIVE (4x OS)");
        combo ("drType", 24, c, 100);
        placeKnob ("drive", 14, k); placeKnob ("drTone", 94, k);

        addPanel (202, y, 340, h1, "EQ");
        placeKnob ("eqLow", 202, k); placeKnob ("eqMid", 282, k); placeKnob ("eqMidF", 362, k); placeKnob ("eqHigh", 442, k);

        addPanel (550, y, 180, h1, "PITCH / THICKEN");
        combo ("pxMode", 560, c, 120);
        placeKnob ("pxMix", 550, k); placeKnob ("pxCents", 630, k);

        addPanel (738, y, 100, h1, "COMP");
        placeKnob ("comp", 748, k);

        addPanel (846, y, 250, h1, "VOLUME SWELL (ON PLUCK)");
        placeKnob ("swAmt", 846, k); placeKnob ("swAtk", 926, k); placeKnob ("swRel", 1006, k);

        y = 212; k = y + 52; c = y + 26;
        addPanel (14, y, 360, h1, "MOD FX (CHORUS/FLANGER/VIBRATO/ROTARY)");
        combo ("mfType", 24, c, 100); toggle ("mfSync", 130, c, 62); combo ("mfDiv", 194, c, 70); toggle ("mfRandom", 270, c, 90);
        placeKnob ("mfRate", 14, k); placeKnob ("mfDepth", 94, k); placeKnob ("mfFb", 174, k); placeKnob ("mfMix", 254, k);

        addPanel (382, y, 340, h1, "PHASER (STAGES)");
        combo ("phStg", 392, c, 80);
        placeKnob ("phMix", 382, k); placeKnob ("phRate", 462, k); placeKnob ("phDepth", 542, k); placeKnob ("phFb", 622, k);

        addPanel (730, y, 340, h1, "RING MOD / LO-FI CRUSHER");
        placeKnob ("rmMix", 730, k); placeKnob ("rmFreq", 810, k); placeKnob ("bcBits", 890, k); placeKnob ("bcDown", 970, k);
    }

    // =============================== PAGE 3: SPACE ===============================
    curPage = 3;
    {
        const int y = 58, k = y + 52, c = y + 26;
        addPanel (14, y, 340, h1, "DELAY");
        combo ("dTime", 24, c, 100); toggle ("dPing", 128, c, 100); combo ("dlyVoice", 232, c, 100);
        placeKnob ("dMix", 14, k); placeKnob ("dFb", 94, k); placeKnob ("dlyWow", 174, k); placeKnob ("dlyDuck", 254, k);

        addPanel (362, y, 340, h1, "DELAY REPEAT FX (IN FEEDBACK LOOP)");
        combo ("dfxMode", 372, c, 110);
        placeKnob ("dfxRate", 362, k); placeKnob ("dfxDepth", 442, k); placeKnob ("dfxTone", 522, k);

        addPanel (710, y, 500, h1, "REVERB (16-LINE FDN + SHIMMER)");
        placeKnob ("rvMix", 710, k); placeKnob ("rvSize", 790, k); placeKnob ("rvDamp", 870, k);
        placeKnob ("rvPre", 950, k); placeKnob ("rvShim", 1030, k); placeKnob ("rvDuck", 1110, k);

        addPanel (1218, y, 260, h1, "3D STEREO IMAGER");
        placeKnob ("spWidth", 1218, k); placeKnob ("spSpace", 1298, k); placeKnob ("spDepth", 1378, k);
    }

#if RFX_NEW_GUI
    // =============================== new pages (HANDOFF.md section 12) ===============================
    curPage = 2;  put (*spectrum, 14, 372, 1474, 344);          // live spectrum: lower half of EFFECTS
    curPage = 4;                                                 // STUDIO
    put (*toolBar, 14, 58, 1474, 64);
    put (*orbit,   14, 130, 580, 580);
    put (*morph,   620, 130, 580, 580);
    curPage = 5;  put (*perform, 14, 58, 1474, 650);             // PERFORM
#endif

    // =============================== always visible: input + output ===============================
    curPage = -1;
    {
        const int y = 726, h = 136, k = y + 52;
        addPanel (14, y, 180, h, "INPUT (GUITAR)");
        placeKnob ("inGain", 14, k); placeKnob ("gate", 94, k);
        addPanel (1308, y, 180, h, "OUTPUT");
        placeKnob ("mix", 1308, k); placeKnob ("master", 1388, k);
    }
    applyPage();
}

void RhythmFXEditor::paint (juce::Graphics& g) { g.fillAll (bgBottom); }       // normally fully covered by `content`

void RhythmFXEditor::resized()
{
    const float s = (float) getWidth() / (float) kBaseW;
    content.setBounds (0, 0, kBaseW, kBaseH);
    content.setTransform (juce::AffineTransform::scale (s));      // the layout never changes; only this transform does
    proc.uiScale.store (s);                                        // remembered with the session
    scaleBtn.setButtonText ("UI " + juce::String (juce::roundToInt (s * 100.f)) + "%");
}

void RhythmFXEditor::setUiScale (float s)
{
    s = rfx::ui::clampScale (s);
    setSize (rfx::ui::scaledSize (kBaseW, s), rfx::ui::scaledSize (kBaseH, s));
}

void RhythmFXEditor::showScaleMenu()
{
    juce::PopupMenu m;
    const int now = juce::roundToInt ((float) getWidth() / (float) kBaseW * 100.f);
    for (int pct : rfx::ui::kScalePercents) m.addItem (pct, juce::String (pct) + "%", true, pct == now);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scaleBtn),
                     [safe = juce::Component::SafePointer<RhythmFXEditor> (this)] (int result)
                     { if (safe != nullptr && result > 0) safe->setUiScale ((float) result / 100.f); });
}

void RhythmFXEditor::paintContent (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient::vertical (bgTop, 0.f, bgBottom, (float) kBaseH));
    g.fillAll();

    // logo mark (three bars) and title
    g.setColour (accent);
    g.fillRoundedRectangle (16.f, 24.f, 4.f, 12.f, 2.f);
    g.fillRoundedRectangle (23.f, 15.f, 4.f, 21.f, 2.f);
    g.fillRoundedRectangle (30.f, 20.f, 4.f, 16.f, 2.f);
    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (23.f, juce::Font::bold)));
    g.drawText ("RhythmFX Sync", 44, 9, 280, 34, juce::Justification::centredLeft);

    for (auto& p : panels)                                  // all shadows first, so one panel's shadow never dims its neighbour
        if (p.page < 0 || p.page == page)
            juce::DropShadow (juce::Colours::black.withAlpha (0.30f), 12, { 0, 4 }).drawForRectangle (g, p.r);

    for (auto& p : panels)
    {
        if (p.page >= 0 && p.page != page) continue;
        const auto r = p.r.toFloat();
        g.setGradientFill (juce::ColourGradient::vertical (panelTop, r.getY(), panelBottom, r.getBottom()));
        g.fillRoundedRectangle (r, panelRadius);
        g.setColour (edge);
        g.drawRoundedRectangle (r.reduced (0.5f), panelRadius, 1.f);
        g.setColour (accent.withAlpha (0.85f));                                       // small marker before the title
        g.fillRoundedRectangle (r.getX() + 11.f, r.getY() + 10.f, 3.f, 12.f, 1.5f);
        g.setColour (dim);
        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        g.drawText (p.title, p.r.getX() + 20, p.r.getY() + 8, p.r.getWidth() - 32, 16, juce::Justification::centredLeft);
    }

    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (11.f, juce::Font::bold)));
    for (auto& c : captions) if (c.page < 0 || c.page == page) g.drawText (c.text, c.x, c.y, 160, 14, juce::Justification::centredLeft);

    if (page != 0) return;                                  // lane labels belong to the sequencer page
    g.setFont (juce::Font (juce::FontOptions (13.f, juce::Font::bold)));
    const char* names[7] = { "Filter", "Amp", "Pan (up=R)", "Kick", "Snare", "Hat", "Clap" };
    const int ys[7] = { 214 + 35, 290 + 35, 366 + 35, 446 + 14, 478 + 14, 510 + 14, 542 + 14 };
    for (int i = 0; i < 7; ++i)
    {
        g.setColour (i < 3 ? text : dim);
        g.drawText (names[i], 16, ys[i] - 10, 78, 20, juce::Justification::centredLeft);
    }
}

void RhythmFXEditor::timerCallback()
{
    filterLane->length = (int) proc.apvts.getRawParameterValue ("length")->load();
    ampLane->length    = (int) proc.apvts.getRawParameterValue ("aLen")->load();
    panLane->length    = (int) proc.apvts.getRawParameterValue ("pLen")->load();
    filterLane->playStep = proc.currentStep.load();
    ampLane->playStep    = proc.currentAmpStep.load();
    panLane->playStep    = proc.currentPanStep.load();
    for (auto& d : drumLanes) d->playStep = proc.currentDrumStep.load();
    filterLane->repaint(); ampLane->repaint(); panLane->repaint();
    for (auto& d : drumLanes) d->repaint();
}
