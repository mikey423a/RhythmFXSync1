# RhythmFX Sync (JUCE / VST3)

A tempo-synced rhythmic multi-effect for guitar (insert effect) with an optional built-in synth and drum machine, inspired by the
*idea* of beat-synced filter/tremolo plugins like AdrenaLinn Sync. Original code and
name, no affiliation with Roger Linn Design.

## Signal flow
Guitar in -> Input trim -> Noise gate -> **Volume swell** -> [+ MIDI synth] -> Drive -> **Step Filter** -> **Step Amp** -> **Step Pan** ->
+ **Mod FX** (chorus / flanger / vibrato / rotary) + Drum machine -> Compressor -> Ping-pong Delay (tempo synced) -> Reverb -> Dry/Wet -> Master

The modulation sources (below) are summed onto the destinations at audio rate, on top of the step lanes.

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

## Sequence restart
Restarts every lane, the drums and the synced LFO from step 1, immediately. Ways to trigger it:
* the **RESTART** button,
* a **MIDI note** (default C1 = note 24; that note is consumed, not played) or a **MIDI CC** (rising through 64),
  chosen in the Mode menu with the Note / CC knob,
* **On pick**: every detected pluck restarts the pattern, so the rhythm always starts on your downbeat.
When the host transport starts, the pattern realigns to the host grid.

* Follows host tempo and bar position while playing; free-runs at the last known tempo when stopped.
* 31 factory programs (pattern + settings), patterns saved with the project/preset state.

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

## Status
* `Source/RhythmDSP.h` is JUCE-free and has a smoke test (32-step clock, pan law, 24 dB slope, attack/decay envelope,
  pluck detector, peak hold, LFO shapes, vowel formants, scale quantizer, swell, chorus/flanger/vibrato/rotary):
  `g++ -std=c++17 -O2 Tests/dsp_smoke_test.cpp -ISource -o dsp_test && ./dsp_test` (passes).
* The JUCE layer (processor, editor, CMake) targets JUCE 8.0.4 but has **not been compiled yet** (no JUCE
  available where it was written) - expect to fix a few compile errors on first build and send them back.

## Ideas for next steps
Phaser / ring-mod, guitar pitch tracking (to drive the note-number source from audio), step accents for drums,
sample-based drums, preset browser + user presets, oversampled drive, per-lane swing, pattern randomise / copy-paste.
