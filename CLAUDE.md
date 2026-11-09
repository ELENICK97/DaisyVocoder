# CLAUDE.md — Daisy Seed Vocoder V1

## Claude Code workflow
Treat this file as the source of truth for settled V1 decisions. Inspect repository state and relevant files before editing. Check `git status` when available and preserve unrelated user changes. Make small, coherent changes and build/test after each step. Verify libDaisy/DaisySP APIs against the checked-out versions. In summaries, report files changed, commands actually run, real outcomes, and outstanding hardware verification. Never claim unrun tests passed.

## Mission and workflow
Implement a reliable real-time 16-band vocoder for Electrosmith Daisy Seed in C++, using the repository's existing supported libDaisy/DaisySP setup. Inspect the repository, target board, library versions, build system, and hardware assumptions before coding. Verify APIs against the checked-out source/documentation; never invent capabilities or test results. Work incrementally, build often, and report changed files, commands run, actual results, and hardware checks still outstanding. Do not re-ask decisions recorded here or overwrite unrelated user changes.

## Critical out-of-box behavior: sound immediately with MIDI notes
**The default firmware must produce a working vocoder sound without the user sending or configuring any MIDI CC messages.** On a fresh install/reset, the default carrier mode must be `INTERNAL_SYNTH`. With a valid MIDI Note On/Off stream on the configured MIDI channel, the internal synth must generate the carrier and the vocoder must process the live microphone input. A user should be able to connect the microphone and MIDI controller, play notes, and hear the vocoder without first moving a knob, sending a CC, or changing a setting.

- Choose useful, documented compile-time defaults for synth waveform, synth ADSR, oscillator/noise levels, vocoder envelope settings, Carrier Level, Mic Gain, Dry/Wet, and Output Level so the signal is audible and usable immediately.
- The fresh-device default settings and the INIT defaults must select `INTERNAL_SYNTH`.
- If a previously saved settings record exists, load it as specified in persistence requirements; do not silently override a user's saved carrier selection at every boot. INIT restores the out-of-box defaults, including internal carrier.
- The external carrier remains selectable by MIDI CC, but is not the default.
- Do not make any CC message a prerequisite for enabling the oscillator or hearing the first note. Note On/Off must work with defaults alone.
- Add a test or documented acceptance test that starts from erased/invalid settings, sends only MIDI Note On/Off (no CC), provides a microphone signal, and verifies audible/valid vocoder output with the internal carrier selected. If hardware is unavailable, state that hardware audio verification remains outstanding.

## Audio hardware and I/O
- Target: Daisy Seed (STM32H7), 48 kHz.
- Use onboard supported audio I/O; do not add an external codec without evidence it is necessary.
- Mono electret microphone input through a 3.5 mm connection, with appropriate external bias/preamp circuit. The analog PCB design is outside firmware scope; do not assume a bare electret capsule can connect directly without bias/preamp.
- Mono external audio carrier input.
- Stereo line-level output. V1 may duplicate the mono vocoder output to left/right.
- Carrier modes are exclusive: `INTERNAL_SYNTH` or `EXTERNAL_AUDIO`, selected by MIDI CC. No automatic fallback to synth if external mode is selected but no carrier is present. In that case wet output is silent; dry mic remains audible according to Dry/Wet.
- USB provides power and firmware loading. USB MIDI from a computer is optional only if supported without compromising audio or DIN MIDI. USB Audio is out of scope.
- No extra encoders, display, dedicated INIT button, or INIT visual feedback.

## Vocoder DSP
- 16 analysis bands and 16 synthesis bands using IIR biquad band-pass filters.
- Initial logarithmic range: 100 Hz–8 kHz. Make band count and frequency limits easy to change via compile-time definitions. Calculate coefficients safely; avoid hard-coded per-band implementations.
- Each analysis band has its own envelope follower; global Attack and Release controls are shared across bands.
- Global controls: Envelope Amount, Formant Shift, Filter Q, Dry/Wet, Carrier Level, digital Mic Gain, Output Level.
- Formant Shift range: nominally -12 to +12 semitone-equivalents, neutral at center. This shifts vocoder filter bands; it is not a pitch shifter. Clamp to safe frequency limits and document the method.
- Dry/Wet: 0% wet = microphone only; 100% wet = vocoder only; intermediate values blend both.
- Output Level is applied after Dry/Wet mixing and the final limiter.
- A conservative always-on limiter follows Dry/Wet mixing. Test peak containment and artifacts. It cannot prevent analog/ADC clipping that already occurred.
- Include lightweight digital input-clipping detection only. No input gate, low-input-level indicator, or separate sibilance path.
- No effects in V1.

## Internal synth and MIDI notes
- Internal synth waveforms: Saw, Square/Pulse, Triangle, Sine, Noise. Waveform and Noise Level have separate CCs.
- **Spec revision (during development):** the original V1 spec called for a monophonic synth. Polyphony was later added as an extension: Poly mode with `SYNTH_NUM_VOICES` voices (default 4, 1–8) and a Mono/Poly Voice mode CC. Poly is the default. Mono mode keeps all of the monophonic behaviour below. Out-of-box behaviour is unchanged: Note On/Off alone must produce sound with no CC.
- ADSR controls are available via MIDI CC and have independent compile-time defaults.
- Mono mode: support Note On/Off, last-note priority, return to the previous still-held note when the current note is released, and release envelope when no notes remain held.
- Poly mode: voice allocation and stealing must be deterministic and bounded. Summed voices must be level-normalised so chords do not overload the vocoder or limiter.
- Legato is distinct from Glide. Glide default is 0 ms/off, adjusted by CC, with maximum configurable at compile time.
- Pitch Bend supported, default range ±2 semitones configurable in `MidiConfig.h`.
- Keep synth ADSR separate from the vocoder band envelope followers.

## MIDI
- Required primary input: 5-pin DIN MIDI IN from the external controller, through an appropriate hardware MIDI input interface to a compatible UART/serial input.
- Support Note On, Note Off, Control Change, Pitch Bend.
- Configurable MIDI channel, default channel 1.
- USB MIDI is optional. If both DIN and USB MIDI are enabled, feed both into one central MIDI event/parameter path.
- Centralize CC mapping in `MidiConfig.h` using clearly commented `#define`s; changing mappings and rebuilding must not require editing DSP logic. Avoid CC collisions.
- Include CCs for carrier mode/level, vocoder Attack/Release, Envelope Amount, Formant Shift, Filter Q, Dry/Wet, Mic Gain, Output Level, waveform, Noise Level, synth ADSR, Glide, and other exposed controls.
- Validate and clamp all incoming parameter values.

## Configuration defaults
Suggested headers:
- `MidiConfig.h`: MIDI channel, CC assignments, Pitch Bend range, INIT hold duration.
- `VocoderConfig.h`: band count/range, default vocoder parameters and limits.
- `SynthConfig.h`: waveform, Noise Level, ADSR, Glide defaults/limits.
- Additional files only if useful and consistent with the repository.

At minimum make configurable: `VOCODER_NUM_BANDS` (16), `VOCODER_MIN_FREQ_HZ` (100), `VOCODER_MAX_FREQ_HZ` (8000), defaults for vocoder Attack/Release/Envelope Amount/Filter Q/Formant Shift/Dry-Wet/Carrier Level/Mic Gain/Output Level, synth ADSR and Noise Level, maximum Glide and default 0 ms, Pitch Bend range ±2 semitones, MIDI channel 1, INIT hold duration 5 seconds, and save debounce interval. Clearly document units and any chosen numeric defaults that have not been explicitly specified.

## Persistence and INIT
- Use a supported libDaisy/non-volatile storage mechanism only after verifying the API and memory behavior for the actual library/board version.
- At boot, load the last valid saved settings. If empty, corrupted, incompatible, or invalid, use compile-time defaults.
- Automatically save changed parameters after a debounce period; never write on every CC/slider update.
- Storage writes must happen outside the real-time audio callback.
- Use a versioned settings structure and validate/clamp loaded values.
- INIT is a dedicated MIDI CC assigned to an existing momentary controller button. Require a continuous hold for 5 seconds by default, configurable by `#define`. Short presses do nothing; do not block or busy-wait.
- INIT restores all compile-time defaults, including `INTERNAL_SYNTH` carrier mode, and saves them to non-volatile storage.
- No visual feedback for INIT in V1.

## Real-time safety and architecture
- Audio callback must be bounded, allocation-free, and free of blocking calls, flash/file operations, logging, and dynamic allocation.
- Initialize DSP and coefficients outside the callback where possible. Parameter changes must not cause invalid coefficients or audible discontinuities.
- Do not redesign filters per sample. Apply safe coefficient updates at a controlled rate/context.
- Avoid NaN/Inf propagation and relevant denormal problems. Use safe cross-context parameter exchange without blocking the audio callback.
- Measure and document actual CPU load/timing on hardware; never invent measurements.

## Suggested modules
Adapt to the existing project template rather than forcing a restructure:
- `main.cpp`: hardware/DSP initialization, callback and main loop.
- `Vocoder.h/.cpp`: filters, envelopes, synthesis, Formant Shift, wet output.
- `Synth.h/.cpp`: oscillator, ADSR, note stack, Legato, Glide.
- `MidiHandler.h/.cpp`: parsing, CC mapping and unified DIN/optional USB events.
- `SettingsStorage.h/.cpp`: versioned persistence, validation, debounce and INIT.
- `MidiConfig.h`, `VocoderConfig.h`, `SynthConfig.h`: compile-time configuration.

## Build and test requirements
- Use the project's supported Daisy build system; do not introduce a new one just by preference.
- Desktop tests where practical:
  - band layout and valid filter coefficients at extremes;
  - envelope Attack/Release;
  - Formant Shift and safe bounds;
  - Dry/Wet endpoints;
  - synth waveforms, ADSR, note stack, Legato, Glide, Pitch Bend;
  - MIDI channel/CC mapping and clamping;
  - settings defaults, persistence, invalid data/version, INIT hold, save debounce;
  - clipping detection.
- Hardware tests:
  - 48 kHz stable audio and real-time deadline/CPU load;
  - DIN MIDI, and USB MIDI only if implemented;
  - internal carrier works from Note On/Off without any CC;
  - external carrier selection and no-signal behavior;
  - sound quality, stability, limiter behavior, no NaN/Inf.
- Never claim a test passed unless actually run. Distinguish desktop tests from hardware checks that remain.

## Out of scope
The following are explicitly excluded from V1:
- Analog PCB / hardware design (mic bias and preamp, MIDI input circuit, I/O board). These are developed separately.
- External audio codec. The onboard Daisy Seed codec is used.
- USB Audio. USB MIDI is optional only.
- Automatic fallback from `EXTERNAL_AUDIO` to the internal synth when no carrier is present.
- Effects of any kind (reverb, delay, chorus, etc.).
- Input gate, low-input-level indicator, and a separate sibilance/unvoiced path.
- Pitch shifting. Formant Shift moves filter bands only.
- Extra encoders, a display, a dedicated INIT button, and visual feedback for INIT.
- Velocity sensitivity. The firmware ignores velocity.
