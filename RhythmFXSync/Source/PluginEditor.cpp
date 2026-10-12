#include "PluginEditor.h"

namespace
{
const juce::Colour kBg      (0xff15161a);
const juce::Colour kPanel   (0xff202228);
const juce::Colour kOutline (0xff363943);
const juce::Colour kAccent  (0xfff0902f);
const juce::Colour kText    (0xffe9e9ee);
const juce::Colour kDim     (0xff8e93a1);
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
        g.setColour ((i / 4) % 2 == 0 ? juce::Colour (0xff25272e) : juce::Colour (0xff1d1f25));
        g.fillRoundedRectangle (cell, 3.f);

        const float v = get (i);
        if (bipolar)
        {
            const float midY = cell.getCentreY(), vy = cell.getY() + cell.getHeight() * (1.f - v);
            g.setColour (colour.withAlpha (active ? 0.95f : 0.25f));
            g.fillRoundedRectangle (cell.getX(), std::min (midY, vy), cell.getWidth(), std::max (2.f, std::abs (vy - midY)), 2.f);
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.drawHorizontalLine ((int) midY, cell.getX(), cell.getRight());
        }
        else if (v > 0.001f)
        {
            auto bar = cell.withTrimmedTop (cell.getHeight() * (1.f - v));
            g.setColour (colour.withAlpha (active ? 0.95f : 0.25f));
            g.fillRoundedRectangle (bar, 3.f);
        }
        if (i == playStep)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawRoundedRectangle (cell, 3.f, 1.6f);
        }
        else if (! active)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (cell, 3.f);
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
    setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3a3d48));
    setColour (juce::Slider::thumbColourId, kText);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2d36));
    setColour (juce::ComboBox::outlineColourId, kOutline);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::arrowColourId, kAccent);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff23252c));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent.darker (0.3f));
    setColour (juce::Label::textColourId, kDim);
    setColour (juce::ToggleButton::textColourId, kText);
    setColour (juce::ToggleButton::tickColourId, kAccent);
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2d36));
    setColour (juce::TextButton::textColourOffId, kText);
}

RhythmFXEditor::RhythmFXEditor (RhythmFXProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    setSize (1510, 1010);

    // --- knobs, combos, toggles bound to parameters
    for (auto [id, text] : std::initializer_list<std::pair<const char*, const char*>> {
            { "length", "Length" }, { "aLen", "Length" }, { "pLen", "Length" }, { "swing", "Swing" }, { "bpm", "Tempo" },
            { "pDepth", "Depth" }, { "pGlide", "Smooth" },
            { "fAtk", "Attack" }, { "fRel", "Release" }, { "trigSens", "Sens" }, { "eAtk", "Attack" }, { "eDec", "Decay" },
            { "eAmt", "Amount" }, { "pkRel", "Release" }, { "pkAmt", "Amount" }, { "lfoRate", "Rate" }, { "lfoAmt", "Amount" },
            { "fRand", "Random" }, { "mfRate", "Rate" }, { "mfDepth", "Depth" }, { "mfFb", "Feedback" }, { "mfMix", "Mix" },
            { "swAmt", "Depth" }, { "swAtk", "Rise" }, { "swRel", "Fall" },
            { "noteLo", "Low" }, { "noteHi", "High" }, { "nAmt", "Amount" }, { "rsNum", "Note / CC" }, { "fLow", "Low" }, { "fHigh", "High" }, { "fRes", "Reso" },
            { "fGlide", "Glide" }, { "fMix", "Mix" }, { "tDepth", "Depth" }, { "tGlide", "Smooth" }, { "drive", "Drive" },
            { "dMix", "Mix" }, { "dFb", "Feedback" }, { "rvMix", "Mix" }, { "rvSize", "Size" }, { "comp", "Amount" },
            { "sLevel", "Level" }, { "sRel", "Release" }, { "drLevel", "Level" }, { "master", "Master" },
            { "inGain", "Input" }, { "gate", "Gate" }, { "fEnv", "Amount" }, { "mix", "Dry/Wet" } })
        addKnob (id, text);
    for (auto id : { "rate", "aRate", "pRate", "fType", "fSlope", "dTime", "sWave",
                     "fEnvDst", "eDst", "pkDst", "lfoDst", "nDst", "trigSrc", "lfoShape", "lfoDiv", "rsMode",
                     "arpScale", "arpRoot", "mfType", "mfDiv" }) addCombo (id);
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
                                             [&pr] (int i, float v) { pr.filterSteps[(size_t) i].store (v); }, false, juce::Colour (0xff2fb4d8));
    ampLane    = std::make_unique<StepLane> ([&pr] (int i) { return pr.ampSteps[(size_t) i].load(); },
                                             [&pr] (int i, float v) { pr.ampSteps[(size_t) i].store (v); }, false, juce::Colour (0xffe0467d));
    panLane    = std::make_unique<StepLane> ([&pr] (int i) { return pr.panSteps[(size_t) i].load(); },
                                             [&pr] (int i, float v) { pr.panSteps[(size_t) i].store (v); }, false, juce::Colour (0xff7bd34a));
    panLane->bipolar = true;
    const juce::Colour dc[4] = { juce::Colour (0xfff0902f), juce::Colour (0xffe6c93b), juce::Colour (0xff3dbb4a), juce::Colour (0xff8a3fe0) };
    for (int k = 0; k < 4; ++k)
        drumLanes[k] = std::make_unique<StepLane> (
            [&pr, k] (int i) { return (float) ((pr.drumMask[(size_t) k].load() >> i) & 1); },
            [&pr, k] (int i, float v) { int m = pr.drumMask[(size_t) k].load(); m = v > 0.5f ? (m | (1 << i)) : (m & ~(1 << i)); pr.drumMask[(size_t) k].store (m); },
            true, dc[k]);
    for (auto* lane : { filterLane.get(), ampLane.get(), panLane.get() }) lane->numSteps = 32;
    for (auto& d : drumLanes) d->numSteps = 16;
    addAndMakeVisible (*filterLane);
    addAndMakeVisible (*ampLane);
    addAndMakeVisible (*panLane);
    for (auto& d : drumLanes) addAndMakeVisible (*d);

    // --- program menu
    for (int i = 0; i < proc.getNumPrograms(); ++i) programBox.addItem (proc.getProgramName (i), i + 1);
    programBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    programBox.onChange = [this] { proc.setCurrentProgram (programBox.getSelectedId() - 1); repaint(); };
    addAndMakeVisible (programBox);

    restartBtn.setTooltip ("Restart all sequences from step 1 (also: MIDI note / CC, or on pick - see RESTART panel)");
    restartBtn.onClick = [this] { proc.restartRequested.store (true); };
    addAndMakeVisible (restartBtn);

    startTimerHz (30);
}

RhythmFXEditor::~RhythmFXEditor() { setLookAndFeel (nullptr); }

RhythmFXEditor::Knob* RhythmFXEditor::addKnob (const juce::String& id, const juce::String& label)
{
    auto k = std::make_unique<Knob>();
    k->l.setText (label, juce::dontSendNotification);
    k->l.setJustificationType (juce::Justification::centred);
    k->l.setFont (juce::Font (juce::FontOptions (12.5f)));
    k->s.setPopupDisplayEnabled (true, true, this);
    addAndMakeVisible (k->s); addAndMakeVisible (k->l);
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
    addAndMakeVisible (c->c);
    c->att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, c->c);
    auto* raw = c.get();
    combos[id] = std::move (c);
    return raw;
}

RhythmFXEditor::Toggle* RhythmFXEditor::addToggle (const juce::String& id, const juce::String& text)
{
    auto t = std::make_unique<Toggle>();
    t->b.setButtonText (text);
    addAndMakeVisible (t->b);
    t->att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, t->b);
    auto* raw = t.get();
    toggles[id] = std::move (t);
    return raw;
}

void RhythmFXEditor::placeKnob (const juce::String& id, int x, int y)
{
    auto& k = *knobs[id];
    k.s.setBounds (x + 8, y, 64, 64);
    k.l.setBounds (x, y + 64, kKnobW, 18);
}

void RhythmFXEditor::addCaption (int x, int y, const juce::String& text) { captions.push_back ({ x, y, text }); }

void RhythmFXEditor::addPanel (int x, int y, int w, int h, const juce::String& title)
{
    panels.push_back ({ { x, y, w, h }, title });
}

void RhythmFXEditor::resized()
{
    panels.clear();
    captions.clear();
    programBox.setBounds (getWidth() - 270, 14, 256, 28);

    // Row 1: pattern + the three 32-step lane panels (each lane has its own rate and length) -------
    const int y1 = 58, h1 = 146, ky = y1 + 52, cy = y1 + 26;
    addPanel (14, y1, 180, h1, "PATTERN");
    toggles["hostSync"]->b.setBounds (24, cy, 150, 22);
    placeKnob ("swing", 14, ky); placeKnob ("bpm", 94, ky);

    addPanel (202, y1, 650, h1, "STEP FILTER");
    combos["fType"]->c.setBounds (212, cy, 100, 22);
    combos["fSlope"]->c.setBounds (318, cy, 80, 22);
    combos["rate"]->c.setBounds (404, cy, 80, 22);
    combos["arpScale"]->c.setBounds (500, cy, 100, 22);
    combos["arpRoot"]->c.setBounds (604, cy, 56, 22);
    addCaption (500, y1 + 9, "ARP SCALE / KEY");
    int x = 212;
    for (auto id : { "length", "fLow", "fHigh", "fRes", "fGlide", "fRand", "fMix" }) { placeKnob (id, x, ky); x += kKnobW; }

    addPanel (860, y1, 250, h1, "STEP AMP");
    combos["aRate"]->c.setBounds (870, cy, 80, 22);
    placeKnob ("aLen", 860, ky); placeKnob ("tDepth", 940, ky); placeKnob ("tGlide", 1020, ky);

    addPanel (1118, y1, 268, h1, "STEP PAN");
    combos["pRate"]->c.setBounds (1128, cy, 80, 22);
    placeKnob ("pLen", 1118, ky); placeKnob ("pDepth", 1204, ky); placeKnob ("pGlide", 1290, ky);

    // Lanes ------------------------------------------------------------
    const int lx = 100, lw = getWidth() - lx - 14;
    filterLane->setBounds (lx, 214, lw, 70);
    ampLane->setBounds (lx, 290, lw, 70);
    panLane->setBounds (lx, 366, lw, 70);
    for (int k = 0; k < 4; ++k) drumLanes[(size_t) k]->setBounds (lx, 446 + k * 32, lw, 28);

    // Row 2: modulation sources (each one: amount + destination) --------------------------------
    const int y2 = 582, h2 = 146, k2 = y2 + 52, c2 = y2 + 26;
    addPanel (14, y2, 250, h2, "ENVELOPE FOLLOWER");
    combos["fEnvDst"]->c.setBounds (24, c2, 90, 22);
    placeKnob ("fAtk", 14, k2); placeKnob ("fRel", 94, k2); placeKnob ("fEnv", 174, k2);

    addPanel (272, y2, 410, h2, "NOTE ENVELOPE (PLUCK / MIDI)");
    combos["trigSrc"]->c.setBounds (282, c2, 90, 22);
    combos["eDst"]->c.setBounds (378, c2, 90, 22);
    placeKnob ("trigSens", 272, k2); placeKnob ("eAtk", 352, k2); placeKnob ("eDec", 432, k2); placeKnob ("eAmt", 512, k2);

    addPanel (690, y2, 170, h2, "PEAK HOLD");
    combos["pkDst"]->c.setBounds (700, c2, 90, 22);
    placeKnob ("pkRel", 690, k2); placeKnob ("pkAmt", 770, k2);

    addPanel (868, y2, 340, h2, "LFO");
    combos["lfoShape"]->c.setBounds (878, c2, 84, 22);
    toggles["lfoSync"]->b.setBounds (966, c2, 62, 22);
    combos["lfoDiv"]->c.setBounds (1030, c2, 70, 22);
    combos["lfoDst"]->c.setBounds (1104, c2, 94, 22);
    placeKnob ("lfoRate", 868, k2); placeKnob ("lfoAmt", 948, k2);

    addPanel (1216, y2, 280, h2, "MIDI NOTE NUMBER");
    combos["nDst"]->c.setBounds (1226, c2, 90, 22);
    placeKnob ("noteLo", 1216, k2); placeKnob ("noteHi", 1296, k2); placeKnob ("nAmt", 1376, k2);

    // Row 3: modulation effect, swell, input, drive, delay, reverb, compressor -------------------
    const int y3 = 736, h3 = 128, k3 = y3 + 52;
    addPanel (14, y3, 360, h3, "MOD FX (CHORUS / FLANGER / VIBRATO / ROTARY)");
    combos["mfType"]->c.setBounds (24, y3 + 26, 100, 22);
    toggles["mfSync"]->b.setBounds (130, y3 + 26, 62, 22);
    combos["mfDiv"]->c.setBounds (194, y3 + 26, 70, 22);
    toggles["mfRandom"]->b.setBounds (270, y3 + 26, 90, 22);
    placeKnob ("mfRate", 14, k3); placeKnob ("mfDepth", 94, k3); placeKnob ("mfFb", 174, k3); placeKnob ("mfMix", 254, k3);

    addPanel (382, y3, 250, h3, "VOLUME SWELL (ON PLUCK)");
    placeKnob ("swAmt", 382, k3); placeKnob ("swAtk", 462, k3); placeKnob ("swRel", 542, k3);

    addPanel (640, y3, 180, h3, "INPUT (GUITAR)");
    placeKnob ("inGain", 640, k3); placeKnob ("gate", 720, k3);

    addPanel (828, y3, 100, h3, "DRIVE");
    placeKnob ("drive", 838, k3);

    addPanel (936, y3, 190, h3, "DELAY");
    combos["dTime"]->c.setBounds (946, y3 + 26, 92, 22);
    toggles["dPing"]->b.setBounds (1042, y3 + 26, 80, 22);
    placeKnob ("dMix", 936, k3); placeKnob ("dFb", 1026, k3);

    addPanel (1134, y3, 180, h3, "REVERB");
    placeKnob ("rvMix", 1134, k3); placeKnob ("rvSize", 1214, k3);

    addPanel (1322, y3, 100, h3, "COMPRESSOR");
    placeKnob ("comp", 1332, k3);

    // Row 4: restart, synth, drums, output ---------------------------------------------------------
    const int y4 = 872, k4 = y4 + 52;
    addPanel (14, y4, 190, h3, "SEQUENCE RESTART");
    combos["rsMode"]->c.setBounds (24, y4 + 26, 96, 22);
    toggles["rsPick"]->b.setBounds (124, y4 + 26, 76, 22);
    placeKnob ("rsNum", 14, k4);
    restartBtn.setBounds (104, k4 + 18, 90, 30);

    addPanel (212, y4, 190, h3, "SYNTH (MIDI)");
    combos["sWave"]->c.setBounds (222, y4 + 26, 100, 22);
    placeKnob ("sLevel", 212, k4); placeKnob ("sRel", 302, k4);

    addPanel (410, y4, 100, h3, "DRUMS");
    toggles["drOn"]->b.setBounds (418, y4 + 26, 88, 22);
    placeKnob ("drLevel", 420, k4);

    addPanel (1308, y4, 180, h3, "OUTPUT");
    placeKnob ("mix", 1308, k4); placeKnob ("master", 1388, k4);
}

void RhythmFXEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (26.f, juce::Font::bold)));
    g.drawText ("RHYTHMFX SYNC", 16, 8, 320, 36, juce::Justification::centredLeft);
    g.setColour (kAccent);
    g.fillRect (16, 46, 54, 3);

    for (auto& p : panels)
    {
        g.setColour (kPanel);
        g.fillRoundedRectangle (p.r.toFloat(), 6.f);
        g.setColour (kOutline);
        g.drawRoundedRectangle (p.r.toFloat().reduced (0.5f), 6.f, 1.f);
        g.setColour (kDim);
        g.setFont (juce::Font (juce::FontOptions (12.f, juce::Font::bold)));
        g.drawText (p.title, p.r.getX() + 10, p.r.getY() + 6, p.r.getWidth() - 20, 16, juce::Justification::centredLeft);
    }

    g.setColour (kDim);
    g.setFont (juce::Font (juce::FontOptions (11.f, juce::Font::bold)));
    for (auto& c : captions) g.drawText (c.text, c.x, c.y, 160, 14, juce::Justification::centredLeft);

    g.setFont (juce::Font (juce::FontOptions (13.f, juce::Font::bold)));
    const char* names[7] = { "Filter", "Amp", "Pan  (up = R)", "Kick", "Snare", "Hat", "Clap" };
    const int ys[7] = { 214 + 35, 290 + 35, 366 + 35, 446 + 14, 478 + 14, 510 + 14, 542 + 14 };
    for (int i = 0; i < 7; ++i)
    {
        g.setColour (i < 3 ? kText : kDim);
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
