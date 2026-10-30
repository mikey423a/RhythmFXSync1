# RhythmFX Sync - handoff to the next engineer (written for Claude Opus 5.5)

## STATUS UPDATE (v0.13) - read this first; it supersedes the "never compiled" rows in section 1
Done in a Linux sandbox with JUCE 8.0.4 (g++ 13, Release):
* **Compiles with zero errors**, both `RFX_NEW_GUI=OFF` and `ON` (one fix needed: `juce::juce_wchar` in EditorWidgets.h).
* **pluginval 1.x, strictness 10, VST3: SUCCESS** on both builds (repeated runs) - open cold/warm, editor, audio processing,
  state save/restore, automation, bus layouts, parameter fuzzing, thread safety.
* Found and fixed by pluginval: on/off parameters kept a fractional host value and did not restore from saved state. They are now
  two-state choice parameters (same IDs, snap to 0/1).
* Fixed the audio-thread allocation flagged in section 7 (scratch buffers are sized to 65536 samples in `prepareToPlay`).
* Editor was **rendered on screen** (Standalone under Xvfb) - all pages draw, nothing clipped. The new-GUI widgets had never been
  laid out; `PluginEditor.cpp` now places them (spectrum on EFFECTS, STUDIO and PERFORM pages).
* CI: `build` now waits for `preflight` (which runs all unit tests) and uploads only the VST3 + Standalone folders.
Still NOT verified: Windows/MSVC compile (JUCE 8 has no MinGW support and the MSVC SDK was unreachable from the sandbox, so
the Windows build must come from GitHub Actions), sound quality by ear, behaviour inside a real DAW, tap tempo / pluck light /
spectrum with real audio, scenes are still not saved in plugin state, and no listening pass of the 42 programs.


You are taking over a half-finished JUCE audio plugin. Read this whole file first. It tells you what exists, what has
actually been **verified**, what has **never been run**, and what to do next, in priority order.

## 0. The one-paragraph version
A guitarist wants a **VST3 insert effect** (plus Standalone app) in the style of the Rogerlinn / Strymon-like
"AdrenaLinn Sync": tempo-synced step-sequenced filter / tremolo / pan, envelope + LFO modulation, and a big set of
"extremely high quality, 3D, thick, hi-def" effects (oversampled drive, EQ, pitch/thicken, phaser, ring mod, bit crusher,
modulation FX, shimmer reverb, delay with effects inside the feedback loop, 3D stereo imager, soft limiter).
All DSP is written and **unit-tested**. The JUCE layer (processor, editor, CMake) is written but **has never been
compiled by anyone**, because the authoring environment had no JUCE and no network. **Job #1 is to get it to compile and
run.** Job #2 is to make sure it sounds as good as the DSP tests suggest.

## 1. What is verified and what is not
| Area | Status |
|---|---|
| `Source/RhythmDSP.h`, `Source/RhythmDSP2.h` (all DSP, JUCE-free) | **Compiled with g++ -std=c++17 -Wall -Wextra; all three test programs pass** |
| `Source/Theme.h` + the restyled `PluginEditor.cpp` (soft palette, custom knobs / drop-downs / tabs / check boxes, rounded gradient panels) | **Never compiled.** Preview pictures were drawn with `Tools/preview_gui.py` (section 13) |
| `Source/EditorLogic.h` (JUCE-free logic for the new GUI ideas) | **Compiled; `Tests/editor_logic_test.cpp` passes (52 checks)** |
| `Source/EditorWidgets.h` (the JUCE widgets for those ideas) | **Never compiled.** Switched off by default (`RFX_NEW_GUI`), so it cannot break the normal build. See section 12 |
| `Tests/*.cpp` | Pass (commands in section 6) |
| `Source/PluginProcessor.cpp/.h` | **Never compiled.** Written carefully; parameter IDs cross-checked by script (105 IDs, layout == X-macro list == uses). The preset table was compile-checked in isolation (std only). `processBlock` has never executed |
| `Source/PluginEditor.cpp/.h` | **Never compiled or shown on screen.** Layout geometry was checked by script (26 panels / 98 controls, no overlaps, all inside window 1510x872). Nothing has been rendered |
| `CMakeLists.txt`, `.github/workflows/build.yml` | Never run. `GIT_TAG 8.0.4` - confirm that tag exists. Artifact path in the workflow (`build/RhythmFXSync_artefacts/Release/`) is a best guess |
| **Sound quality** | **Nobody has listened to any of it.** Tests measure behaviour (notch frequencies, alias level, RT60, stability), not tone |

Do not tell the user anything "works" until you have built it and exercised it. They will be told honestly what was
and was not verified; keep it that way.

## 2. The user
A guitarist, not a developer. Uses **GitHub Desktop** (no command line), Windows most likely, wants an installable
`.vst3` for their DAW. Asked for click-by-click instructions with pictures. Wants hi-def, thick, wide ("3D") tone.
Earlier they got an `.exe` from a different Claude session; they expect something they can just drop in a folder.
Plain-language explanations, short steps. Deliverables as a zip. Be upfront about anything unverified.

## 3. Repository map
```
CMakeLists.txt                 JUCE 8.0.4 via FetchContent; VST3 + Standalone; effect build by default
.github/workflows/build.yml    Windows + macOS build, uploads artifacts (-DRFX_COPY_AFTER_BUILD=OFF)
README.md                      user-facing feature docs (keep in sync when you change features)
HANDOFF.md                     this file
Source/RhythmDSP.h     (667 l) sequencer + modulation DSP, JUCE-free
Source/RhythmDSP2.h    (532 l) "hi-def" effects, JUCE-free
Source/PluginProcessor.h/.cpp  (117 / 835 l) parameters, presets, processBlock, state
Source/PluginEditor.h/.cpp     UI: 4 pages (+ 2 more when RFX_NEW_GUI=ON)
Source/Theme.h                 the whole colour palette and corner radii (edit only this file to retheme)
Source/EditorLogic.h           JUCE-free logic for the new GUI ideas (pattern tools, orbit geometry, scene morph, tap tempo ...)
Source/EditorWidgets.h         JUCE widgets using it; compiled only with -DRFX_NEW_GUI=ON (see section 12)
Tests/dsp_smoke_test.cpp   Tests/dsp2_test.cpp   Tests/dsp_fx_test.cpp   Tests/editor_logic_test.cpp
```
**DSP modules** - `RhythmDSP.h` (namespace `rfx`): OnePole, Svf (TPT), StepClock (swing, per-lane rate/length,
restart-able), panGains, driveProcess (legacy, unused now), FbFxParams + StereoDelay (effect inside feedback loop:
tremolo/chorus/vibrato/ring/filter), Compressor, NoiseGate, EnvFollower, AdEnv, NoteTrigger (pluck detector), PeakHold,
Lfo, Vowel (formant filter), quantizeFreq (arpeggio scale snap), Swell, ModFx (chorus/flanger/vibrato/rotary),
DrumMachine, Synth. `RhythmDSP2.h`: Biquad, Eq3, Halfband/Oversampler4 (latency 35 samples), LatencyLine, HiDrive,
PitchShifter, PitchFx (Thicken/octaves/fifth), Allpass, FdnReverb (16 lines, shimmer), Spatial (3D imager), Phaser,
RingMod, BitCrush, softLimit.

## 4. Signal chain (per sample, in `RhythmFXProcessor::processBlock`)
Input trim -> noise gate -> (dry tap, delayed 35 samples by `dryLine`) -> volume swell -> + MIDI synth ->
`HiDrive` (4x oversampled, always runs so latency is constant) -> step filter (LP/BP/HP/Vowel, 12/24 dB, scale-quantised
for arpeggio) -> step amp -> step pan -> EQ -> Pitch/Thicken -> Phaser -> ModFx -> RingMod -> BitCrush -> + drums ->
compressor -> StereoDelay (+ in-loop FX) -> FdnReverb -> Spatial -> (outside the loop) Dry/Wet -> master ->
`softLimit` -> hard clip safety. Mono input is copied to both channels; mono output sums L+R.

Modulation: five sources (envelope follower, note-triggered AD envelope, peak hold, LFO, MIDI note number), each with
Amount (-1..1) and Destination (Cutoff, Reso, Amp, Pan, Drive). Triggers: pluck detector and/or MIDI note-on.
Sequence restart: UI button, MIDI note, MIDI CC (rising through 64), or on each detected pluck; implemented by
shifting `restartPpq`, so all lane clocks, drums and the synced LFO realign.

## 5. Conventions you must follow
* **Adding a parameter = four edits:** (1) `X(name)` in `RFX_PARAM_LIST` in `PluginProcessor.h` (the ID *is* the member
  name of the cached `std::atomic<float>* p.name`), (2) the `F/C/B/I(...)` line in `createLayout()`, (3) a knob/combo/
  toggle in the `PluginEditor.cpp` constructor lists **and** a `placeKnob/combo/toggle` call in `resized()`, (4) if
  programs should control it, a field in `PresetMod` + `applyPreset()` (and add it to the reset list there).
  A script cross-check (list vs layout vs editor) is easy to re-create; do it after every change.
* Parameter IDs are stable (`ParameterID { id, 1 }`). Do not rename shipped IDs.
* Audio thread: no allocation, no locks. Pattern data is `std::atomic<float>` arrays shared with the editor.
* `PresetMod` defaults must equal the parameter defaults - loading a program resets modulation/effect params first.
* Choice-parameter order must match the DSP enums (`kDrive` = `HiDrive::Soft,Tube,Fuzz,Fold`; `kPitch` = `PitchFx`
  enum order). Keep them in sync.
* DSP stays JUCE-free and header-only so it can be unit-tested with plain g++. Add a test for every new DSP block.
* Editor pages: panels/controls are tagged with the page being laid out (`curPage`); `-1` = always visible.

## 6. Build and test
```
# ONE command that runs everything below plus the consistency checks (no JUCE needed). Run it after EVERY edit:
python3 Tools/preflight.py       # parameter IDs agree, editor layout fits, braces balance, all unit tests pass; exit 0 = good
#   (CI runs it too, as the 'preflight' job.) If you change the editor layout or the parameter list, it checks you did it consistently.

# DSP tests (no JUCE needed), from the project folder:
g++ -std=c++17 -O2 -Wall -Wextra Tests/dsp_smoke_test.cpp -ISource -o t1 && ./t1   # prints ... dfxBad=0 and exits 0
g++ -std=c++17 -O2 -Wall -Wextra Tests/dsp2_test.cpp      -ISource -o t2 && ./t2   # ends "ALL OK"
g++ -std=c++17 -O2 -Wall -Wextra Tests/dsp_fx_test.cpp    -ISource -o t3 && ./t3   # ends "ALL OK"
g++ -std=c++17 -O2 -Wall -Wextra Tests/editor_logic_test.cpp -ISource -o t4 && ./t4  # ends "ALL OK"
g++ -std=c++17 -O2 Tests/dsp_bench.cpp -ISource -o bench && ./bench                     # CPU benchmark (information, not pass/fail)

# Plugin (needs git + network for JUCE FetchContent):
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRFX_COPY_AFTER_BUILD=OFF
cmake --build build --config Release --parallel
# with the new editor widgets (not yet compiled - expect to fix errors):  add  -DRFX_NEW_GUI=ON  to the first command
# outputs under build/RhythmFXSync_artefacts/Release/{VST3,Standalone}
```
Windows install: copy the whole `RhythmFX Sync.vst3` (may be a folder) to `C:\Program Files\Common Files\VST3\`.
The user builds through GitHub Actions; any red CI log lines will be pasted back to whoever helps them.

## 7. TASKS - in priority order
### P0 - make it build and run
1. Do a real build (CI or local). Fix every compile/link error. Likely trouble spots (JUCE 8 API drift, my guesses):
   `juce::Font (juce::FontOptions (...))` usage, `juce::ParameterID`, `getPlayHead()->getPosition()` optionals,
   `AudioParameterInt` + `SliderAttachment`, structured bindings in `applyPage()`, `Preset` aggregate initialisation
   with default member initialisers and `M([](PresetMod&){...})` lambdas, `static constexpr` arrays in `FdnReverb`
   (C++17 inline), `-Wall` noise from `juce_recommended_warning_flags`.
2. Add the three DSP test programs as a step in `.github/workflows/build.yml` (they need only g++/clang).
3. Launch the Standalone build; open every page; confirm nothing overlaps or is clipped, combos show the right items,
   knobs move parameters, `getRawParameterValue` pointers are non-null (a typo'd ID would crash on load).
4. Load it in a DAW as an insert on a guitar track: mono-in/stereo-out, stereo-in/stereo-out, mono-in/mono-out.
   Check: no clicks on preset change, state save/reload restores patterns (`fSteps/aSteps/pSteps` strings),
   host tempo sync + transport start realign, MIDI restart note/CC, "Host sync" off uses the Tempo knob,
   latency reported (35 samples) and Dry/Wet is phase-aligned (null test at drive 0 / mix 0.5 against a copy).
5. Standalone note: desktop JUCE standalone may mute audio input by default - confirm and keep the README note accurate.

### P1 - sound-quality review (by ear, then with measurements)
Gain staging was set by calculation, not listening. Review and trim: reverb wet level vs `rvMix`, Thicken level,
shimmer amount (loop gain capped at 0.09), drive output level across types (Fuzz/Fold are louder), pitch-shifter
artefacts (two-tap + waveform alignment, 55 ms window: exact on single notes, approximate on dense or inharmonic chords), phaser depth range (250 Hz-4 kHz),
delay in-loop FX levels (tremolo/ring are quieter), limiter ceiling. Check for zipper noise when knobs move
(some params are read once per block, e.g. `fdn.setParams`, `eq.setup`) and add smoothing where audible.
CPU: a DSP-only benchmark (`Tests/dsp_bench.cpp`, one stereo instance at 48 kHz, one core, -O2, the sandbox's CPU) measured
1.5 % with only the always-on oversampled drive stage, 4.8 % for a typical patch (drive, EQ, chorus, reverb, 3D imager), 8.6 % for a
rich one (+ thicken, 12-stage phaser, tape delay with wow and ducking, repeat FX, 24 dB filter) and 9.2 % with everything on (+ shimmer reverb, ring mod, crusher,
octaves). That is comfortable, but it excludes JUCE, the GUI and the host, so still profile the REAL plugin at 44.1/48/96 kHz and small
buffers; the 4x oversampler + FDN + cubic reads are the heavy parts. Re-run the benchmark after DSP changes to catch regressions.

### P1 - known limitations / loose ends (documented, not hidden)
- **Audio-thread safety scan** (searched `processBlock` and the DSP `process()` bodies for allocation, locks, string building, exceptions): one hit.
  `processBlock` calls `scratchR/dryL/dryR.resize()` if the host delivers a block larger than the one announced in `prepareToPlay` - rare, but
  that allocates on the audio thread. Fix: size the three buffers to `max(samplesPerBlock, 8192)` in `prepareToPlay`, and when a block is still
  larger, process it in slices instead of resizing. Everything else the scan looked for is absent.
* Reverb is skipped when `rvMix` <= 0.001, so its tail is cut (not decayed) if the mix is pulled to zero.
* Delay "repeat FX" rate is in Hz (not tempo-synced) and only one repeat effect runs at a time.
* "Arpeggio" = filter cutoff quantised to a scale (resonant band-pass rings the notes); it is not a note generator.
* Drums are a fixed 16-step bar; patterns are 16 values tiled to 32 when a program loads.
* No cabinet / amp impulse-response loader. No guitar pitch tracking (the MIDI-note modulation source needs MIDI).
* No user-preset save/load or browser; 42 factory programs only. Editor is a fixed 1510x872 (not resizable, no scaling).
* Formats: VST3 + Standalone only (no AU/AAX/CLAP). `COMPANY_NAME "YourName"` and manufacturer/plugin codes in
  `CMakeLists.txt` are placeholders. No installer, code signing, or notarisation.
* `Eq3` is bypassed when all gains are ~0; `Oversampler`/`HiDrive` always run (intentional: constant latency).

### P2 - feature backlog (user's stated wishlist that is not done)
Amp + cab simulation with IR loader; pitch tracking so audio can drive the note-number source; preset browser and user
presets; resizable / hi-DPI editor; metering and modulation-activity display; tempo-synced delay-FX rate and a second
simultaneous delay-FX slot; step accents for drums or sample-based drums; per-lane swing; pattern copy/paste/randomise;
AU/CLAP builds; installer.

## 8. Design decisions - do not undo without a reason
* Pan is a **balance** law (louder side stays at unity) so enabling pan never changes level.
* Each step lane (filter/amp/pan) has its own Rate and Length; swing is shared; clocks are `StepClock` over `ppq - restartPpq`.
* Drive is always oversampled and the dry tap is delayed by the same 35 samples, rather than bypassing at drive 0
  (switching latency would click and break phase alignment).
* In the delay, the effect sits **on the read tap**, so every repeat (and what is fed back) passes through it: it
  accumulates round the loop, and the first repeat already carries it.
* The shimmer loop gain is capped (0.09) because higher values measured as unstable at maximum reverb size.
* Test failures were fixed by fixing the *test* only where the test was wrong (e.g. wet-only measurement); otherwise the
  DSP was fixed. Keep that discipline.

## 9. Definition of done
Builds clean on Windows (and macOS) via CI; the three DSP tests run in CI; Standalone opens and every control works;
VST3 loads in at least one DAW, saves/restores state, stays phase-aligned, and sits within a sane CPU budget; a
by-ear pass of all 42 programs finds no clicks, runaway feedback or level jumps; README matches reality; the user can
download a working `.vst3` from the Actions page and drop it into their VST3 folder.

## 10. Ready-to-paste prompt
> You are finishing a JUCE 8 audio plugin ("RhythmFX Sync", guitar insert effect). Read HANDOFF.md fully, then work through
> section 7 in priority order. First get the project compiling (fix errors from a real CMake build or the CI log), add the
> DSP tests to CI, then verify behaviour in the Standalone app and a DAW. Never claim something works unless you ran it;
> report what you verified and what you could not. Keep the conventions in section 5. The end user is a guitarist who
> builds with GitHub Actions and installs a `.vst3` - give them click-by-click, plain-language steps.

## 11. Visual reference and layout fixes (added after the GUI mock)
`RhythmFXSync_GUI_mock.gif` is an animated mock of the four editor pages (Sequencer, Modulation, Effects, Space). It is a
drawing made from the real layout numbers in `PluginEditor.cpp` and the parameter defaults in `PluginProcessor.cpp`,
**not a screenshot of the compiled plugin**: fonts, knob look and the animated values (playheads, drifting knobs, LFO scope,
level meter) are illustrative. Use it as the target for what the editor should look like.
Measuring the mock against the layout found eight overflow problems, now fixed in `PluginEditor.cpp`: titles "DRIVE (4x OS)",
"COMP" and "OUTPUT" shortened; DELAY panel widened (230 px) so the Ping-pong toggle fits, with the rest of the SPACE page
shifted right; Pitch and Delay-time dropdowns and the Drums toggle widened; lane label shortened to "Pan (up=R)".
Still open (design, not bugs): the Modulation, Effects and Space pages leave a large empty area above the always-visible
Input/Output strip. Options: shrink the window per page (check host support for resizing editors first) or move the strip
up. The real JUCE font is not DejaVu, so re-check text widths in the running plugin.

## 12. GUI work package: six editing ideas (added after the animated GUI mock)
**Reference pictures:** `RhythmFXSync_GUI_ideas.gif` (the six ideas, one scene each) and `RhythmFXSync_GUI_mock.gif` (the four
existing pages). They are drawings, not screenshots: use them as the target look and behaviour.

**How it is organised.** Each idea has (a) its logic in `Source/EditorLogic.h` - JUCE-free, unit-tested, 52 checks - and
(b) a widget in `Source/EditorWidgets.h` (namespace `rfxw`) that only draws that logic and forwards mouse events. The widgets are
compiled **only** with `-DRFX_NEW_GUI=ON` (CMake option, default OFF; it also links `juce_dsp` for the FFT). With it OFF nothing changes
except two harmless atomics in the processor (`pluckCount`, `inputLevel`). CI builds both: the `gui=ON` job has
`continue-on-error: true`, so a red ON build never blocks the normal plugin. Once ON compiles cleanly, make it the default and
remove `continue-on-error`.

When ON, the editor gains two tabs (STUDIO, PERFORM) and a spectrum panel on the EFFECTS page; the processor also fills two
8192-sample ring buffers (`specIn`, `specOut`, published through `specPos`) for the spectrum.

| # | Idea | Logic (tested) | Widget | State |
|---|---|---|---|---|
| 1 | **Pattern tools** (ramp, sine, random, mirror, shift, invert, clear, copy/paste; per lane or all lanes) | `ui::tool*` | `PatternToolBar` (STUDIO tab) | Written, uncompiled. Not done: undo (add a `juce::UndoManager`), wiring `ui::toolLine` into `StepLane`'s mouse handling for Shift+drag straight lines, right-click tool menu |
| 2 | **Drag-to-modulate rings** (drag a source chip onto a knob; a coloured ring shows range and live position) | `ui::modDestForParam`, `modRingRange`, `modRingPosition` | **No widget yet** | Logic only. See "Idea 2 design" below |
| 3 | **Live spectrum + draggable filter curve** | (filter response is computed in the widget) | `SpectrumView` (EFFECTS tab, lower half) | Written, uncompiled. Drag = cutoff (slides `fLow`/`fHigh` together) and resonance. Not done: vowel-filter curve (hidden for that type), log-frequency smoothing, CPU check |
| 4 | **Orbit view** (lanes as concentric rings; polyrhythm at a glance) | `ui::orbit*` | `OrbitView` (STUDIO tab) | Written, uncompiled. Click/drag edits a step, wheel rotates a lane |
| 5 | **XY morph pad + scenes A/B/C + dice + parameter locks** | `ui::SceneMorph`, `ui::randomise` | `MorphPad` (STUDIO tab) | Written, uncompiled. Not done: scenes are **not saved in the plugin state** (add the three vectors and the lock mask to `getStateInformation` / `setStateInformation`); the parameter list is fixed in the editor constructor |
| 6 | **Performance mode** (big Restart, tap tempo, pluck light, level meter with the detector floor) | `ui::TapTempo`, `ui::PluckLight` | `PerformPanel` (PERFORM tab) | Written, uncompiled. Tap tempo writes the `bpm` parameter; with Host sync ON the DAW tempo still wins (the panel says so) |

**Idea 2 design (to build).** The backend has five modulation sources, each with ONE destination (Cutoff, Reso, Amp, Pan, Drive)
and one amount. That is enough for a first version: (1) a `ModChips` component with five draggable chips (`juce::DragAndDropContainer` on the
editor, chips as `DragAndDropTarget` sources); (2) knobs whose id maps through `ui::modDestForParam` accept a drop - set that source's
destination parameter (`fEnvDst`, `eDst`, `pkDst`, `lfoDst`, `nDst`) and give it a default amount; (3) a ring overlay drawn around the knob
using `ui::modRingRange(base, amount)` with a live dot from `ui::modRingPosition` (feed it the source value: expose the five source values
from the processor as atomics, like `inputLevel`); (4) dragging the ring changes the amount parameter. The mock shows several sources on one knob
and modulation of any knob (reverb mix etc.). That needs the matrix to become a real route list (source, destination id, amount) with more
destinations; do it as a second step, keeping parameter IDs stable for saved sessions.

**Acceptance checks for this work package** (run by you, not assumed): build with `-DRFX_NEW_GUI=ON` and fix compile errors; each
widget draws without asserting in Standalone; painting a lane in STUDIO changes the sound immediately; orbit view and the lanes stay
in sync; morph pad moves the seven parameters, locks hold, dice respects locks, hosts record the moves as automation (gestures);
tap tempo converges on a steady 120 BPM within four taps; the pluck light flashes on every audible note and not on silence; spectrum
shows the input dim and the output bright and does not add audible CPU load (profile it); saved sessions reload with scenes intact.

**Known risks in the new code.** (a) `PluginEditor.cpp` hands the widgets lambdas that capture `this` and references into the processor:
fine while the editor lives, but check teardown order. (b) `SpectrumView` reads the ring buffers while the audio thread writes them
(no lock: a torn frame only means one glitchy display frame) - acceptable for drawing, never for audio. (c) JUCE API names were written
from memory against JUCE 8.0.4 (e.g. `juce::FontOptions`, `Path::addPieSegment`, `dsp::WindowingFunction` constructor) - the first ON build
will show any that differ.

## 13. Visual restyle ("more modern, easier on the eyes")
**What changed.** `Source/Theme.h` now holds every colour: a calm dark-slate palette (background gradient `#1a1e29 -> #12151d`,
panels `#232937 -> #1d222e`), softened lane colours, and one warm amber accent (`#f3a65c`). `PluginEditor.cpp` gained a custom
`Look` (LookAndFeel_V4 subclass) that draws: rotary knobs as a thin rounded arc plus a soft disc and a short pointer (no hard
outline); rounded drop-downs with a chevron; buttons with hover states; the four page tabs as pills (radio group 4711 = tab); check
boxes as rounded squares with a tick. Panels are rounded (radius 10) gradient cards with a hairline edge, a soft shadow (all shadows
are drawn before any panel so they never dim a neighbour) and a small accent marker before the title. The title is now mixed case with a
three-bar logo mark. `EditorWidgets.h` reads the same palette. No layout positions changed.

**Pictures without building:** `python3 Tools/preview_gui.py` (needs `pip install pillow`) draws all four pages from the real layout,
`Theme.h` and the parameter defaults; `--style classic` draws the old look for comparison. It is a Pillow drawing, not JUCE's renderer,
so fonts and anti-aliasing differ slightly; use it to judge colour, spacing and balance, then confirm in the real plugin.

**Still to verify in a real build (JUCE API names written from memory, 8.0.4):** `juce::ColourGradient::vertical`,
`juce::DropShadow::drawForRectangle`, `juce::Path::addCentredArc` with `PathStrokeType::rounded`, the four `Look` overrides (signatures must
match `LookAndFeel_V4` exactly), `getTextButtonFont`, `Slider::isMouseOverOrDragging`, `juce::Colour::withMultipliedBrightness`.
Check legibility of the small grey text (`dim`, `#7b8499`, 11.5 px panel titles) on a real monitor; if it is too faint, raise `dim` in
`Theme.h`. Possible next steps: a light theme (second palette selected by a setting), a UI-scale option for high-DPI screens
(`setScaleFactor` + resizable editor with `setResizeLimits`), and value read-outs on the knobs (the hover popup already exists).

## 14. Pro-quality DSP upgrade ("Strymon / Meris class")
**Honest framing.** Strymon and Meris pedals sound the way they do because of years of tuning by ear and proprietary algorithms. Nothing
here claims to match them, and nobody has listened to this plugin yet. What this pass did is measure the DSP against objective quality
criteria, fix what measured badly, and lock the results into `Tests/quality_test.cpp` so later edits cannot quietly undo them.
**A by-ear A/B against real hardware is still the real test** (checklist below).

| Area | What measured badly | What changed | Result (all asserted in `quality_test.cpp`) |
|---|---|---|---|
| Pitch shifter (octaves, fifth, shimmer) | Out of tune by up to **52 cents** (half a semitone) depending on the note: each tap's phase jumps when it wraps, and the cross-fade drags the pitch away | SOLA-style waveform alignment: when a tap wraps, its delay is nudged by the lag that best matches the other tap (normalised cross-correlation over the last 20 ms, coarse-to-fine, one longest-period range) | Worst error **0.56 cents** over 8 notes (82-880 Hz) x 4 intervals; level ripple 0.09 dB; equal-tempered chord within 5.3 cents; silence stays silent |
| Reverb | Dense only after 150-230 ms; tail ringing 2.3-3 dB above an ideal noise tail (resonances 2.3 Hz apart but 1.2 Hz wide ring as separate peaks) | 16 lines (was 8), 4 input all-passes per side (was 2), asymmetric input weights, faster and shallower modulation; output level recalibrated to the original | Dense in **50-95 ms**; ringing **+0.8 to +1.2 dB** over ideal; decay times and wet level unchanged |
| Delay | Clean digital only | Voicings: **Tape** (5.5 kHz roll-off per repeat, lows trimmed, tanh saturation), **Analog/BBD** (two 3 kHz poles, lighter saturation); **Wow** (0.55 Hz drift + 7.3 Hz flutter of the read position); **Ducking** (input-driven, 4 ms attack / 150 ms release, acts on what you hear, not on the feedback) | 8 kHz repeat: digital 0 dB, tape -4.2, analog -16.9; tape 3rd harmonic 10 %, analog 5.5 %, digital 0; wow = 17 cents peak-to-peak at full; ducking removes the repeat while playing and restores 87 % within 0.4 s of stopping; stable at maximum feedback |
| Reverb ducking | n/a | `rvDuck`: the wet level follows an input envelope (4 ms / 250 ms) | processor-level (needs the plugin build to test) |
| Parameter smoothing | Mix/level knobs stepped once per block (zipper noise, clicks on automation) | `rfx::Smoothed` one-pole (20 ms) on drive, filter / amp / pan depth, delay / reverb / pitch / phaser / ring / mod-FX mix, 3D imager, compressor, Dry/Wet and Master; the first value is adopted instantly | A gain step at a waveform peak: unsmoothed jump 1.0, smoothed 0.0525 (a plain 440 Hz sine moves 0.0576 per sample) |
| CPU | New work doubled the cost | Reverb modulation evaluated every 16 samples; shifter not run when shimmer is 0; alignment search on contiguous copies | Reverb 5.5 % -> 1.8 %, shifter 3.3 % -> 0.8 %; behaviour bit-for-bit the same in the metrics |

**New controls:** Delay Voice, Wow, Duck, and Reverb Duck (Space page, 109 parameters now); four new programs: *Tape Echo*, *Analog Echo (BBD)*,
*Ducked Tape + Shimmer*, *Cathedral Swell*.

**Not done / known limits (be straight about these):**
- The shifter is exact for periodic single notes and approximate on dense or inharmonic chords (the best-matching lag is used). A phase-vocoder
  or formant-preserving shifter would be the next step for studio-grade polyphonic shimmer.
- Reverb is one algorithm family. Strymon-style Room / Hall / Plate / Spring / Bloom / Cloud are not modelled; the Size / Damping / Pre-delay /
  Shimmer / Duck controls and the programs approximate some of them. Spring and plate need their own models.
- Not implemented: reverse delay, multi-tap / rhythmic delay patterns, freeze/infinite hold, tap-tempo sub-divisions beyond the delay-time list,
  analog-style input compander noise, ring-mod oversampling (high carriers alias).
- The processor changes (`PluginProcessor.cpp`: smoothers that shadow the per-block values inside the sample loop, delay voice, reverb ducking)
  are plain C++ but have **not been compiled**; `-Wshadow` will warn about the deliberate shadowing (it can be silenced by renaming).

**By-ear checklist (needs a person):** play low E, A, D strings into Octave Up/Down and Shimmer and check they hold their tuning against a tuner; compare
Tape Echo and Analog Echo to a reference tape / BBD echo (darkening per repeat, wow, saturation level); sweep Wow and Duck for clicks; check the reverb
tail on a sustained chord at Size 0.3 / 0.6 / 0.9 for metallic ringing, and a staccato part for tail density; automate Dry/Wet and Master and listen for zipper
noise; compare Shimmer and Cathedral Swell against hardware to decide whether the reverb needs types / a second algorithm.

## 15. Scalable window and retina / high-DPI
**What it does.** The editor is laid out once at a fixed design size (`kBaseW x kBaseH` = 1510 x 872) inside a `content` component, and `content` is
scaled to the window with `setTransform(AffineTransform::scale(...))` (the standard JUCE pattern for fixed-layout plugin UIs). Everything is vector drawing (paths,
gradients, text; no bitmaps except the `DropShadow` blur), so it is rendered at the display's real resolution and stays sharp on retina and fractional-scale
screens. The window is resizable from 50 % to 200 % with a **fixed aspect ratio** (`ComponentBoundsConstrainer`), by dragging the corner or from the new **UI %** menu in
the header (60 / 75 / 90 / 100 / 125 / 150 / 200 %). It **opens at the largest size that fits the screen** (never above 100 %: a 13-inch laptop gets 90 %) or at the size saved
with the session (`uiScale` in the plugin state). The size rules are `rfx::ui::fitScale / initialScale / snapScale` in `EditorLogic.h` (tested: 11 checks).

**A latent crash was found and fixed on the way.** The old constructor called `setSize()` first; `setSize()` calls `resized()` immediately, and `resized()` held the whole
layout and dereferenced `knobs[id]` / `combos[id]` / `toggles[id]` before any of them existed (a null dereference the first time the editor opened). Now `resized()` only scales;
the layout runs once, at the end of the constructor (`layoutContent()`).

**Rules this creates (enforced by `Tools/preflight.py`, section 2b):** every control must be added with `content.addAndMakeVisible` (not the editor's), the layout and
panel-drawing code must use `kBaseW / kBaseH` and never the live `getWidth() / getHeight()`, and the constructor must not call the editor's own `setSize` before
`layoutContent()`. `python3 Tools/preview_gui.py --scale 0.9` (laptop) or `--scale 2` (retina) draws the pages at those sizes.

**To verify in a real build / on real displays (not testable here):**
- Resizing in real hosts: VST3 hosts that honour the aspect-ratio constraint, hosts that ignore it, and hosts that cannot resize at all (the UI % menu then
  does nothing visible; confirm it fails gracefully). AU and AAX resizing support varies; Pro Tools windows are fixed.
- Windows at 100 / 125 / 150 / 200 % display scaling, per-monitor DPI and moving the window between monitors; macOS retina and non-retina. A host that applies its own
  scale composes with ours; check nothing double-scales.
- Popup menus, tooltips and the slider value bubble are separate OS windows (or parented outside `content`), so they do **not** scale with the UI. Consider scaling the
  popup font / item height from the current `uiScale` in `Look`.
- `DropShadow` renders a bitmap at logical resolution, so shadows are slightly soft on retina (fine for shadows); cache a pre-blurred image if it shows in profiling.
- Hairlines (1 px borders) at fractional scales such as 90 % are anti-aliased rather than pixel-snapped; if they look fuzzy on a 1x screen, snap them to device pixels.
- Keyboard shortcuts for size (Ctrl/Cmd + and -) are not implemented.
