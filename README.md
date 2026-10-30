# RhythmFX Sync (JUCE / VST3)

A tempo-synced rhythmic multi-effect for guitar (insert effect) with an optional built-in synth and drum machine, inspired by the
*idea* of beat-synced filter/tremolo plugins like AdrenaLinn Sync. Original code and
name, no affiliation with Roger Linn Design.

## Signal flow
Guitar in -> Input trim -> Noise gate -> **Volume swell** -> [+ MIDI synth] -> **Drive (4x oversampled)** -> **Step Filter**
-> **Step Amp** -> **Step Pan** -> **EQ** -> **Pitch / Thicken** -> **Phaser** -> **Mod FX** (chorus / flanger / vibrato / rotary)
-> **Ring mod** -> **Bit crusher** -> + Drum machine -> Compressor -> Ping-pong Delay (with repeat FX in the loop)
-> **FDN reverb (shimmer)** -> **3D stereo imager** -> Dry/Wet -> Master -> **soft limiter**

The modulation sources (below) are summed onto the destinations at audio rate, on top of the step lanes.
The editor has four pages (tabs along the top): **Sequencer**, **Modulation**, **Effects**, **Space**. Input and Output
are always visible at the bottom.

## Hi-def / 3D effects
| Effect | What it is | Controls |
|---|---|---|
| Drive | Waveshaper run at **4x oversampling** (steep half-band filters) so high notes don't alias into fizz. Soft (tanh), Tube (asymmetric, even harmonics, DC-blocked), Fuzz, Fold (wavefolder) | Type, Drive, Tone |
| EQ | Low shelf 110 Hz, mid bell (Q 0.9, sweepable 200 Hz-5 kHz), high "air" shelf 7 kHz; +/-12 dB | Low, Mid, Mid Hz, Air |
| Pitch / Thicken | **Thicken** = automatic double-tracking (two voices, one per side, ~17/~23 ms with a slow random wander so they never lock into a comb filter). Also Octave Up/Down, Octaves, Fifth Up (two-tap shifter with waveform alignment: within about 0.6 cents on single notes) | Mode, Mix, Detune |
| Phaser | 4/6/8/12 all-pass stages, exponential sweep, feedback, left/right LFOs 90 degrees apart | Stages, Mix, Rate, Depth, Feedback |
| Ring mod | Quadrature carriers left/right (sidebands differ in each ear) | Ring Mix, Ring Hz |
| Lo-fi crusher | Bit depth 2-16 and sample-rate divide 1-32 (16 bits / 1 = off, bit-exact) | Bits, Rate Div |
| Reverb | 16-line feedback-delay network, modulated lines, 4-stage input diffusion, damping, pre-delay, decorrelated L/R tail, **shimmer** (octave-up fed back into the tank) and **ducking** (the tail drops while you play and blooms back when you stop). Size sets the decay (about 0.25 s to 9 s) | Mix, Size, Damping, Pre-delay, Shimmer, Duck |
| Delay | Ping-pong delay, tempo synced, with three **voicings**: Digital (clean), Tape (darker, saturated, lows trimmed on every repeat) and Analog / BBD (much darker). **Wow** adds tape-style speed drift and flutter; **Duck** pulls the repeats down while you play | Voice, Wow, Duck (plus the repeat FX below) |
| 3D imager | All-pass decorrelation (widens even a mono guitar), mid/side width, early-reflection "room" | Width, Space, Room |
| Output limiter | Soft-knee: transparent below 0.8, never reaches full scale | - |

Notes: the oversampled drive adds 35 samples of latency (reported to the host; the Dry/Wet dry path is delayed to match).
Mod FX, Pitch, Phaser etc. each run one at a time per slot; the slots stack.

## Sequencer
* **32-step lanes** for Filter, Amp and Pan. Click/drag to paint; the bright outline is the playing step, dimmed
  cells are beyond the lane's Length.
* **Per-lane Rate and Length** (1/4, 1/8, 1/16, 1/8T, 1/16T; 1-32 steps) -> polyrhythms, e.g. 32 steps of
  filter against a 12-step amp. Swing is shared.
* **Step Filter**: lane drives the cutoff between Low and High (exponential), with Glide. Lowpass / Bandpass /
  Highpass or **Vowel**, 12 or 24 dB slope, Resonance, **Random**, Mix (raise **Mix** or you won't hear the filter at all).
* **Step Amp**: tremolo / gate chop, Depth + Smooth. **Step Pan**: auto-pan (up = right), Depth + Smooth.
* **Drums**: Kick / Snare / Hat / Clap, one 16-step bar. Synthesised.
* **Tempo / Host sync**: with Host sync on, tempo and position come from the DAW; off (or in the Standalone app)
  the Tempo knob sets the BPM.

## Effect types - where to find each one
| Effect | How |
|---|---|
| Tremolo (smooth pulse to hard chop) | LFO -> **Amp** (Sine ... Square, synced), or the Step Amp lane. Programs: *Tremolo (Smooth)*, *Tremolo (Hard Chop)* |
| Filter tremolo | LFO -> **Cutoff** with Filter Mix up. Program: *Filter Tremolo* |
| Tremolo / Filter sequences | The 32-step Amp and Filter lanes (programs *16th Gate Chop*, *Guitar Stutter*, *8th Wah* ...) |
| Flanger, Chorus, Vibrato, Rotary speaker | **Mod FX** panel (one at a time): Rate in Hz or tempo-synced, Depth, Feedback (flanger), Mix. Rotary: Rate = horn speed (drum runs at 0.88x), crossover 800 Hz. Programs: *Chorus Shimmer*, *Flanger Jet*, *Rotary Speaker (+Fast)*, *Vibrato* |
| Random filter | Filter lane **Random** knob: each step is replaced by a fresh random value (0 = your pattern, 1 = fully random). Program: *Random Filter* |
| Random flanger | Mod FX with **Random** ticked: the sweep jumps to a new random position every cycle/division. Program: *Random Flanger* |
| Arpeggio sequences | Filter lane + **Arp Scale / Key**: the cutoff snaps to notes of the scale (chromatic, major, minor, major/minor pentatonic). Use Bandpass, high Reso, short Glide. Program: *Arpeggio (A Minor Pent)* |
| Auto-pan | Step Pan lane or LFO -> **Pan**. Program: *Auto-Pan 8th* |
| Auto-wah / envelope filter | Envelope follower -> Cutoff, or the Note Envelope. Programs: *Auto-Wah (Envelope)*, *Pluck Wah (Envelope)* |
| Talk-box / voice-box | Filter type **Vowel**: three formant filters morph A-E-I-O-U as the filter lane / envelope / LFO moves the cutoff. Programs: *Vowel Talk Box*, *Vowel Auto-Talk (Envelope)* |
| Volume swells | **Volume Swell** panel: each pluck drops the level, swells up over Rise, then fades over Fall. Program: *Volume Swell* |

Swell note: the pluck is detected ~1 ms into the note, so the very start of the pick can slip through at the old
level. Set Fall short enough that the level has dropped before the next pluck, and it is effectively unnoticeable.

## Modulation sources
Each source has an **Amount** (-1..+1, negative inverts) and a **Destination**: Cutoff, Reso, Amp, Pan or Drive.
Several sources can feed the same destination.
| Source | What it does | Controls |
|---|---|---|
| Envelope follower | Follows your picking loudness | Attack, Release, Amount |
| Note envelope | A pluck (or MIDI note-on) fires an attack/decay sweep | Trigger (Audio / MIDI / Both), Sens, Attack, Decay, Amount |
| Peak hold | Captures the loudest level of the note's first 30 ms (or MIDI velocity) and holds it until the next note | Release (10 s = hold until next note), Amount |
| LFO | Sine / Triangle / Saw / Square / Random; free-running in Hz or synced to the tempo (2 bars .. 1/16) | Shape, Rate or Division, Sync, Amount |
| MIDI note number | The last MIDI note played, mapped across Low..High note | Low, High, Amount |

All shapes start at their low point and rise, so a restart always begins a sweep from the bottom.
The note-number source needs MIDI notes sent to the plugin (a keyboard, a MIDI track, or a guitar-to-MIDI
converter); plain guitar audio has no note number.

## Delay repeat effects (effect inside the feedback loop)
The DELAY REPEAT FX panel puts an effect *inside* the delay's feedback path. Every repeat is processed before you hear
it and before it is fed back, so the effect builds up with each trip round the loop (the 4th echo has been through it
four times). Delay Mix, Time, Feedback and Ping-pong work as usual; the first repeat already carries the effect.
| Mode | What each repeat gets | Rate | Depth | Tone |
|---|---|---|---|---|
| Tremolo | Volume pulsing; left/right in opposite phase for a stereo pulse | speed | how deep (1 = full chop) | - |
| Chorus | Doubled/shimmering, widening as repeats pile up | speed | width | - |
| Vibrato | Wobbling pitch on the repeats | speed | pitch swing | - |
| Ring Mod | Metallic, bell-like, increasingly inharmonic | - | dry <-> ring blend | carrier 40 Hz - 2.5 kHz |
| Filter | Low-pass that darkens every repeat, optionally wobbling | wobble speed | wobble amount | cutoff 300 Hz - 12 kHz |
Programs: *Tremolo Echoes*, *Chorus Echoes*, *Vibrato Echoes*, *Ring Mod Echoes*, *Dark Wobble Echoes*.
Only one repeat effect runs at a time. The rate is in Hz (not tempo-synced yet).

## Sequence restart
Restarts every lane, the drums and the synced LFO from step 1, immediately. Ways to trigger it:
* the **RESTART** button,
* a **MIDI note** (default C1 = note 24; that note is consumed, not played) or a **MIDI CC** (rising through 64),
  chosen in the Mode menu with the Note / CC knob,
* **On pick**: every detected pluck restarts the pattern, so the rhythm always starts on your downbeat.
When the host transport starts, the pattern realigns to the host grid.

* Follows host tempo and bar position while playing; free-runs at the last known tempo when stopped.
* 42 factory programs (pattern + settings), patterns saved with the project/preset state.

## Using it as a guitar insert (default build)
The default build is an **audio effect** (VST3 + Standalone). Insert it on a guitar audio track.
* Mono or stereo tracks both work (mono guitar is fed to both sides; on a mono track the result is summed).
* **Input** trims the guitar level, **Gate** is a simple noise gate (helps with high drive/resonance).
* **Dry/Wet** (Output panel) blends your untouched guitar with the processed signal. Defaults are transparent
  (filter, amp, drive, delay, reverb, compressor all off) until you pick a program or turn things up.
* Zero latency. Rhythms follow the host tempo and bar position while the transport plays and free-run at the
  last tempo when it is stopped, so you can tweak while jamming.
* Good starting programs: *Pluck Wah (Envelope)*, *LFO Wah (1/4 sync)*, *Peak-Hold Brightness*, *Pick Restart Chop*, *Auto-Wah (Envelope)*, *Wah Chase (24dB)*, *Auto-Pan 8th*, *Polyrhythm 16 vs 12*.
  Programs are 16 steps and are tiled across all 32.
* The synth/drum sections are still there: MIDI to the plugin plays the synth, "Drums on" runs the drum grid.
  They sit inside the wet path, so Dry/Wet affects them too.
* Standalone app: pick your audio interface and, in the Audio/MIDI settings, make sure **Mute audio input** is
  unticked (desktop builds mute the input by default to prevent feedback). Use headphones (delay/reverb can feed back through speakers).

## Build
Requires CMake 3.22+, a C++17 compiler, and internet access (JUCE is fetched with FetchContent).

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

VST3 and a Standalone app are produced under `build/RhythmFXSync_artefacts/`
(`COPY_PLUGIN_AFTER_BUILD` also installs the VST3 to your system VST3 folder).

* Instrument version (MIDI synth + drums, no audio input): `cmake -B build -DRFX_AS_EFFECT=OFF`
* Windows: Visual Studio 2022. macOS: Xcode (add `AU` to FORMATS in CMakeLists.txt if wanted).
* Change `COMPANY_NAME` / manufacturer code in CMakeLists.txt before distributing.

## Building without a local toolchain
Push the folder to GitHub: `.github/workflows/build.yml` builds Windows and macOS and uploads the VST3/Standalone
as downloadable artifacts. Any compile errors show up in the Actions log.

## Optional new editor widgets (not yet compiled)
Six editing ideas are written but switched off by default: pattern tools, orbit view, XY morph pad with scenes / locks / dice,
performance mode (restart, tap tempo, pluck light), and a live spectrum with a draggable filter curve. Configure with
`-DRFX_NEW_GUI=ON` to try them (adds STUDIO and PERFORM tabs). GitHub Actions builds both variants; the ON build is allowed to fail
and never blocks the normal plugin. Details: HANDOFF.md section 12.

## Look and feel
The window is resizable (50-200 %, fixed proportions): drag the bottom-right corner or use the **UI %** menu in the header. It opens at the largest size that fits your screen (about 90 % on a 13-inch laptop) and remembers the size with your session. Everything is vector drawing, so it stays sharp on retina and high-DPI displays.

The editor uses a calm dark-slate theme with an amber accent. All colours live in `Source/Theme.h`. `python3 Tools/preview_gui.py` draws pictures of every page without building the plugin (needs Pillow); `--style classic` shows the old look, `--scale 0.9` a laptop size and `--scale 2` a retina rendering.

## Quick check (no JUCE needed)
`python3 Tools/preflight.py` verifies that parameter IDs, the editor layout and the unit tests are consistent. Run it after any edit. `Tests/dsp_bench.cpp` measures how much CPU the effect chain needs (about 2-9 % of one core on the test machine).

## Status
* `Source/RhythmDSP.h` (sequencer, filters, modulation) and `Source/RhythmDSP2.h` (hi-def effects) are JUCE-free and
  unit-tested. Build and run each from the project folder:
  `g++ -std=c++17 -O2 Tests/dsp_smoke_test.cpp -ISource -o t1 && ./t1`   (clock, envelopes, pluck detector, LFO, delay-loop FX ...)
  `g++ -std=c++17 -O2 Tests/dsp2_test.cpp -ISource -o t2 && ./t2`        (EQ, oversampled drive, pitch, reverb, imager, limiter)
  `g++ -std=c++17 -O2 Tests/dsp_fx_test.cpp -ISource -o t3 && ./t3`      (phaser, ring mod, bit crusher)
  `g++ -std=c++17 -O2 Tests/editor_logic_test.cpp -ISource -o t4 && ./t4`  (pattern tools, orbit geometry, morph pad, tap tempo)
  `g++ -std=c++17 -O2 Tests/quality_test.cpp -ISource -o t5 && ./t5`        (measured quality: tuning accuracy, reverb smoothness, delay voicing, ducking, parameter smoothing)
  All five pass. These check measurable behaviour (notch frequencies, aliasing level, RT60, stability), **not how it
  sounds** - judge the tone by ear and adjust.
* The JUCE layer (processor, editor, CMake) targets JUCE 8.0.4 but has **not been compiled yet** (no JUCE
  available where it was written) - expect to fix a few compile errors on first build and send them back.

## Ideas for next steps
Guitar pitch tracking (to drive the note-number source from audio), step accents for drums, sample-based drums,
amp / cabinet impulse-response loader, preset browser + user presets, per-lane swing, pattern randomise / copy-paste.
