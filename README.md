# MyDaisyVocoder — real-time 16-band vocoder firmware for Daisy Seed

> **🚧 Work in progress.** The firmware builds and passes its desktop test suite. The **custom
> hardware (mic preamp, MIDI input, audio I/O board) is currently being developed**, so on-device
> verification is still outstanding. See [Project status](#project-status).

A real-time 16-band channel vocoder for the [Electrosmith Daisy Seed](https://electro-smith.com/products/daisy-seed)
(STM32H750, Cortex-M7 @ 480 MHz), written in C++ on top of libDaisy. It runs at 48 kHz on the Seed's
onboard codec and is played from a MIDI keyboard. With no setup, the internal synth is the carrier:
plug in a mic and a MIDI keyboard, play notes, and you hear the vocoder.

**Author:** NicDox

### Built on the Electrosmith Daisy platform

This project is built on the open-source **Electrosmith Daisy** ecosystem:

- **Hardware:** [Daisy Seed](https://electro-smith.com/products/daisy-seed) embedded audio module by
  [Electrosmith](https://electro-smith.com/) (STM32H750 MCU, onboard audio codec, QSPI flash).
- **[libDaisy](https://github.com/electro-smith/libDaisy):** the hardware abstraction layer. This
  project uses it for audio I/O (SAI/codec), UART and USB MIDI transports, QSPI `PersistentStorage`,
  `CpuLoadMeter`, logging and the core build system.
- **[DaisySP](https://github.com/electro-smith/DaisySP):** the DSP library, linked as part of the standard Daisy build.
- The project template and folder layout follow [DaisyExamples](https://github.com/electro-smith/DaisyExamples).

The vocoder DSP, synth, MIDI parser, parameter/settings system and tests are specific to this project.
This is an independent project, not affiliated with or endorsed by Electrosmith.

---

## How this project was built: AI-developed, human-supervised

The code in this repository was **written by an AI coding agent ([Claude Code](https://claude.com/claude-code))**.
I supervised the whole process. I did not hand off a one-line prompt and accept what came back.

- **The specification came first.** [`CLAUDE.md`](CLAUDE.md) is the engineering spec for V1. I
  discussed it and refined it with the agent before any code was written. It sets the constraints the
  agent had to work within. Among them:
  - **Hard real-time rules.** The audio callback must be bounded. It does no allocation, no locking,
    no flash access and no logging. Filters are never redesigned per sample. NaN/Inf and denormals
    are handled. Data passes between contexts without blocking.
  - **Out-of-box behaviour.** A fresh or erased device must make sound from MIDI Note On/Off alone,
    with no CC setup needed. An automated acceptance test covers this.
  - **Persistence rules.** Settings records are versioned and validated. Saves are debounced and
    never happen in the audio callback. INIT needs a 5 s hold on a momentary button.
  - **Configurability.** Band count, frequency range, CC map and every default are compile-time
    `#define`s. Remapping a CC never means touching DSP code.
  - **Engineering honesty.** Library APIs are checked against the checked-out source. No test result
    or CPU figure is reported unless it was actually measured. Desktop tests are kept separate from
    hardware checks that are still outstanding.
- **A living spec.** When requirements changed, the spec was updated before the code. For example,
  the original V1 called for a monophonic synth. Polyphony was added later as a recorded spec
  revision, with its own requirements (bounded voice allocation, level normalisation), and the
  original out-of-box behaviour stayed intact.
- **Iterative supervision.** I directed the work step by step: reviewing changes, listening to
  offline renders, asking for measurements, and pushing back where the result fell short. One example
  is the intelligibility tuning below, which is backed by an objective metric.
- **My own work.** I'm developing the hardware (analog front end, MIDI input, board) myself, and I own
  the system requirements and the verification plan.

This is how I believe AI coding tools should be used in embedded work. The human owns the
requirements, the constraints and the acceptance criteria. The agent produces code that has to meet
them, and claims are only accepted when there is a test to back them.

---

## Engineering highlights

| Area | What's in the firmware |
|---|---|
| **Real-time safety** | Allocation-free and lock-free audio callback. Notes cross from the main loop through an SPSC queue, and parameters arrive as whole snapshots through a wait-free mailbox. Flush-to-zero is set in both FPSCR and FPDSCR, so it also applies inside ISRs. Any NaN/Inf triggers a DSP state reset. |
| **Architecture** | The audio `Engine` is hardware-independent, so the full signal chain compiles and runs on a desktop. Hardware glue (`MyDaisyVocoder.cpp`) is kept thin. MIDI parsing, CC mapping and settings run in the main loop. |
| **DSP** | 16 + 16 log-spaced bands, each a 6th-order band-pass (3 cascaded RBJ biquads, TDF-II). Each band has its own envelope follower. Carrier spectrum flattening, Formant Shift with safe clamping, coefficient updates rate-limited to 1 ms blocks, and an always-on peak limiter. |
| **MIDI** | Custom running-status byte parser. It replaces libDaisy's parser, which assigns a channel to System Real-Time bytes (Clock, Active Sensing); after one of those bytes, running-status notes get dropped. DMA/USB callbacks only queue raw bytes. DIN and optional USB MIDI feed a single event path. |
| **Persistence** | Versioned QSPI settings record (magic, size, FNV-1a checksum, per-field clamping), debounced writes from the main loop only, and a non-blocking 5 s INIT hold. |
| **Synth** | 4-voice poly / mono modes with voice stealing, PolyBLEP oscillators, ADSR, last-note priority, legato, glide and pitch bend. |
| **Verification** | 29 desktop test cases with **885 checks**, including an acceptance test that starts from erased flash. An offline WAV renderer runs the real `Engine` and gives a STOI-style intelligibility score. |
| **Footprint** | 86.4 KB of 128 KB internal flash (65.9 %), 17.7 KB SRAM + 16.6 KB RAM_D2 (`arm-none-eabi-gcc` 10.2, `-O2`). |

---

## Project status

| Item | Status |
|---|---|
| Firmware builds (libDaisy core Makefile, internal flash) | ✅ Builds with no compiler errors |
| Desktop test suite (MSVC) | ✅ 885 checks, 0 failures |
| Custom hardware: mic bias/preamp, opto-isolated DIN MIDI in, line I/O | 🚧 **In development** |
| On-device verification (real-time deadline / CPU load, DIN MIDI, sound quality) | ⏳ Not yet run. See [Hardware checklist](#hardware-checklist-not-yet-run-on-a-device) |
| CPU load measurement | ⏳ Instrumentation is in place (`VOCODER_CPU_LOG`), but no hardware figure has been measured yet |

No hardware performance numbers are published here until they have been measured on a real device.

---

## Hardware

| Seed pin | Function | Connect to |
|---|---|---|
| 16 – AUDIO IN L | Microphone | Electret capsule **through an external bias + preamp** (line level). A bare capsule will not work. |
| 17 – AUDIO IN R | External carrier | Line-level synth or other source (only used in EXTERNAL_AUDIO mode) |
| 18 – AUDIO OUT L | Output L | Line out |
| 19 – AUDIO OUT R | Output R | Line out (same mono signal as L) |
| 15 – D14 (USART1 RX) | DIN MIDI IN | Output of a standard opto-isolated MIDI input circuit (6N138/H11L1 or similar) |
| USB | Power, firmware upload | Optional USB MIDI (`MIDI_ENABLE_USB`) |
| On-board user LED | Input clip indicator | Lights for 200 ms when either raw input reaches ≥ 0.99 FS |

The analog front end and the MIDI input interface are part of the hardware still being developed.

---

## Build / flash

Tested with libDaisy `v5.4.0-22-g85172e2b`, DaisySP `V1.0.0` and GNU Arm Embedded Toolchain 10 2020-q4-major.

By default the Makefile expects to sit two levels below libDaisy and DaisySP, as in
[DaisyExamples](https://github.com/electro-smith/DaisyExamples) (`DaisyExamples/MyProjects/MyDaisyVocoder`).
To build somewhere else, point it at your copies of the libraries (build those first with `make` in each one):

```
make LIBDAISY_DIR=/path/to/libDaisy DAISYSP_DIR=/path/to/DaisySP
```

```
make                 # firmware (libDaisy core Makefile, internal flash, BOOT_NONE)
make program-dfu     # flash over USB DFU (hold BOOT, tap RESET)
```

Optional builds:

- `MIDI_ENABLE_USB=1` adds USB MIDI. It is merged into the same path as DIN MIDI.
- `VOCODER_CPU_LOG=1` prints CPU load over USB serial once per second.

Both options use the USB port, so you can only turn on one at a time. A `static_assert` enforces this.
Set the define in `MidiConfig.h` / `VocoderConfig.h`, or pass it in `C_DEFS`.

---

## MIDI implementation (channel 1 by default, `MIDI_CHANNEL`)

- Note On / Note Off are handled. A Note On with velocity 0 counts as Note Off. Velocity is ignored.
- Pitch Bend: ±2 semitones (`MIDI_PITCH_BEND_RANGE_ST`).
- All Notes Off (123) and All Sound Off (120) clear the note stack.

| CC | Parameter | Range / curve | Default |
|---|---|---|---|
| 14 | Carrier mode | 0–63 INTERNAL_SYNTH, 64–127 EXTERNAL_AUDIO | INTERNAL_SYNTH |
| 15 | Carrier Level | 0 – 2.0 linear, squared curve | 1.0 |
| 16 | Mic Gain (digital) | −12 – +36 dB, linear | +6 dB |
| 17 | Dry/Wet | 0 = mic only … 127 = vocoder only | 100 % wet |
| 18 | Output Level (after limiter) | 0 – 1.0, squared curve | 0.8 |
| 19 | Vocoder Attack | 0.5 – 200 ms, exponential | 1 ms |
| 20 | Vocoder Release | 5 – 2000 ms, exponential | 8 ms |
| 21 | Envelope Amount | 0 – 1 | 1.0 |
| 22 | Formant Shift | −12 – +12 st, 64 = neutral | 0 |
| 23 | Filter Q | 1 – 20, exponential | 6 |
| 24 | Synth waveform | 5 zones: Saw, Square/Pulse, Triangle, Sine, Noise | Saw |
| 25 | Noise Level | 0 – 1.0, squared curve | 0.15 |
| 26 | Pulse Width | 5 – 95 % | 50 % |
| 27 | Synth Attack | 0.5 – 5000 ms, exponential | 5 ms |
| 28 | Synth Decay | 1 – 5000 ms, exponential | 150 ms |
| 29 | Synth Sustain | 0 – 1 | 0.8 |
| 30 | Synth Release | 1 – 8000 ms, exponential | 200 ms |
| 31 | Glide | 0 = off, else up to 2000 ms, squared | 0 (off) |
| 68 | Legato (Mono mode only) | 0–63 off (retrigger), 64–127 on | off |
| 103 | Voice mode | 0–63 Mono, 64–127 Poly (4 voices) | Poly |
| 102 | **INIT** (momentary button) | hold ≥ 64 for 5 s | – |

All CC numbers live in `MidiConfig.h`. To change one, edit it there and rebuild; you don't need
to touch any DSP code. A `static_assert` rejects duplicate CC numbers and CCs ≥ 120.

---

## Settings, persistence and INIT

- Settings are saved to QSPI flash at offset `0x7F0000` using libDaisy `PersistentStorage`.
  - The record carries a version, a magic number, a size and an FNV-1a checksum.
  - Every value is range-clamped when loaded.
  - If the record is empty, erased, corrupt or from another version, the compile-time defaults are used.
- A save happens once 2 s pass with no further parameter change (`SETTINGS_SAVE_DEBOUNCE_MS`).
  - Values that don't actually change never cause a write.
  - Saves run in the main loop only, never in the audio callback.
- To INIT, hold CC 102 at a value ≥ 64 for 5 continuous seconds (`MIDI_INIT_HOLD_MS`).
  - This restores every default, including INTERNAL_SYNTH, and saves immediately.
  - A shorter press does nothing. INIT fires once per hold, with no busy-waiting and no visual feedback.
- A saved carrier selection is kept across reboots. Only INIT resets it.
- The settings format is now version 4 (it adds Voice mode). Settings saved by earlier firmware
  are discarded once at the first boot, and the defaults load.

---

## Design notes

- **Signal chain** (`Engine.cpp`): mic → Mic Gain → {dry path, vocoder analysis}. The carrier
  (synth or the external input, crossfaded over 10 ms when you switch) → Carrier Level →
  vocoder synthesis. Then Dry/Wet → limiter → Output Level → L and R.
- **Filter bank** (`Vocoder.cpp`):
  - 16 analysis + 16 synthesis bands, log-spaced from 100 Hz to 8 kHz.
    Set by `VOCODER_NUM_BANDS`, `VOCODER_MIN_FREQ_HZ` and `VOCODER_MAX_FREQ_HZ`.
  - Each band is 3 cascaded RBJ constant-peak band-pass biquads (TDF-II), i.e. 6th order
    (`VOCODER_FILTER_SECTIONS`). Per-section Q is scaled so the band's −3 dB width matches Filter Q.
    The steeper skirts keep the carrier's strong low harmonics out of the high bands.
  - Each band has a peak envelope follower with the shared Attack/Release settings, followed by
    a fixed 1.5 ms smoother (`VOCODER_ENV_SMOOTH_MS`) that removes rectifier/voice-pitch ripple.
  - Band gain = `(1−Amount)·flat + Amount·env·sensitivity`.
  - Wet output is compensated by √Q.
- **Carrier flattening** (`VOCODER_CARRIER_NORMALIZE`):
  - A saw carrier has 30–40 dB more energy in the low bands than in the high ones. Without
    correction that buries formants and consonants.
  - Each synthesis band's carrier level is tracked (20 ms). Once per block, a gain pulls each band
    toward the mean band level, with a maximum boost of +30 dB.
  - Overall carrier level is preserved, so Carrier Level still works.
- **Intelligibility tuning** (`tools/render_wav`, STOI-style score on Windows TTS speech,
  Italian/English, held C3, saw carrier). Self-score is 1.00 and unrelated noise scores 0.38.

  | Version | IT | EN | Envelope ripple |
  |---|---|---|---|
  | Previous (2 sections, Q 5, attack 5 / release 60 ms) | 0.59 | 0.66 | 2.9 % |
  | Now (3 sections, Q 6, attack 1 / release 8 / smooth 1.5 ms) | 0.78 | 0.81 | 9.9 % |

  - Envelope speed mattered most. A release of 60 ms smooths the envelope at about 2.6 Hz, but
    speech articulation needs well above 16 Hz.
  - Longer releases sound smoother but less clear; shorter ones sound crisper but rougher.
  - A broader synthesis Q than analysis Q scored worse (`VOCODER_SYNTH_Q_RATIO` stays at 1).
  - Desktop test `SpectralEnvelopeTransfer`: vowel envelope mismatch is 1.5 dB (saw) and 2.0 dB
    (square), and an "s" puts 27.9 dB more energy in the high bands than in the low ones.
  - Sine and Triangle carriers have few harmonics, so they will always be less intelligible.
- **Formant Shift**: the analysis bands stay fixed. Each synthesis centre is multiplied by
  2^(shift/12) and clamped to [20 Hz, min(20 kHz, 0.45·fs)]. This moves the spectral envelope;
  it is not a pitch shifter.
- **Coefficient updates**:
  - Filters are only redesigned in `Vocoder::UpdateControl()`, once per 1 ms block.
  - Shift and Q glide toward their targets (20 ms), and filters are never redesigned per sample.
  - Level parameters are smoothed per sample (10 ms).
- **Limiter**: a peak limiter with a −1 dBFS ceiling, instantaneous attack and 100 ms release,
  plus a hard clamp. It can't undo clipping that already happened in the ADC.
- **Real-time safety**:
  - MIDI is parsed in the main loop.
  - Notes reach the audio callback through a lock-free SPSC queue.
  - Parameters arrive as a whole snapshot through a wait-free mailbox.
  - The callback does no allocation, locking, flash access or logging.
  - The FPU flushes denormals to zero (FPSCR and FPDSCR FZ bit).
  - A NaN/Inf sample is replaced with 0, and the DSP state is reset.
- **Synth**:
  - Four voices (`SYNTH_NUM_VOICES`, 1–8) in Poly mode, or a single voice in Mono mode (CC 103).
    Switching mode releases all held notes.
  - Poly voice allocation, in order of preference:
    1. the voice already playing or releasing that note
    2. an idle voice
    3. the quietest voice in release
    4. steal the voice holding the oldest held note
  - When you release a note while more keys are held than there are voices, its voice moves to
    the most recent held note that had been stolen (pitch change only, no retrigger).
  - Voices are summed with 1/√(Σ level²) normalisation. A single note is exactly as loud as in
    Mono, and a 4-note chord measured +1.9 dB on desktop.
  - Speech STOI with a 4-note chord: 0.77 IT / 0.79 EN (a single note scores 0.78 / 0.81).
  - Glide is per voice. Pitch Bend applies to all voices. Legato only affects Mono mode.
  - PolyBLEP saw and pulse oscillators (pulse is DC-corrected), plus triangle, sine and xorshift noise.
  - Linear attack. Decay and release are exponential: each reaches 99 % of its distance in the set time.
  - Mono mode: last-note priority. Releasing a note returns to the previous held note without retriggering.
  - Legato off: each new note retriggers the attack from the current level, so there's no click.
  - Glide is constant-time and linear in semitones. It only applies while a note is already sounding.
- **MIDI parsing** (`MidiParser.cpp`): the project uses its own parser, not libDaisy's
  `MidiHandler`/`MidiParser`.
  - libDaisy's parser takes the channel from real-time bytes too (Clock `0xF8`, Active Sensing `0xFE`).
    So after one Active Sensing byte, every following running-status note arrives on channel 15
    and is dropped. Notes stop changing and Note Offs get lost.
  - Here, the libDaisy transport only delivers raw bytes from the DMA/USB callback into a
    lock-free queue. The parser runs in the main loop.
  - Real-time bytes are transparent, and SysEx/System Common cancel running status.

---

## Files

| File | Role |
|---|---|
| [`CLAUDE.md`](CLAUDE.md) | **V1 specification and constraints** the AI agent worked under |
| `MyDaisyVocoder.cpp` | Hardware init, audio callback, main loop, QSPI settings backend |
| `Engine.*` | Complete audio chain (hardware independent) |
| `Vocoder.*` | Filter banks, envelopes, Formant Shift |
| `Synth.*` | Voices (oscillator, ADSR, glide), Poly/Mono allocation, note stack, Legato, Pitch Bend |
| `MidiParser.*` | Byte-stream MIDI parser (running status, real-time bytes) |
| `MidiMapper.*` | Channel filtering, CC mapping, value scaling and clamping |
| `SettingsStorage.*` | Versioned record, validation, debounce, INIT |
| `Controller.*`, `ControlBridge.h` | Main-loop glue and lock-free hand-off to the audio callback |
| `Params.*` | Parameter set, defaults, limits |
| `DspUtil.h` | Biquad, smoother, limiter |
| `MidiConfig.h`, `VocoderConfig.h`, `SynthConfig.h` | Compile-time configuration |
| `tests/` | Desktop tests |
| `tools/render_wav.cpp` | Offline render of a WAV through the firmware Engine, with STOI/ripple scoring |

---

## Tests

Desktop tests (no hardware needed):

```
tests\build_tests.bat     # Windows, MSVC 2019/2022
make -C tests             # Linux/macOS with g++/clang++
```

Last run: 29 test cases, **885 checks, 0 failures** (MSVC, Windows). They cover:

- band layout, and coefficient stability/finiteness at the Q, shift and frequency extremes
- Formant Shift bounds and smoothing
- envelope attack/release
- Dry/Wet endpoints, and an external carrier with no signal
- limiter peak containment and distortion
- clip detection and NaN recovery
- synth waveforms (pitch, DC), ADSR timing, note stack, legato/retrigger, glide, pitch bend
- poly voice allocation and level normalisation
- MIDI channel, running status / real-time bytes, CC mapping and clamping
- settings defaults, persistence, invalid data and versions, save debounce, INIT hold
- the SPSC bridge
- default gain staging and spectral envelope transfer

Offline listening / tuning (Windows, MSVC):

```
tools\build_render.bat
tools\build\render_wav.exe my_voice_48k.wav out.wav --score [--note 48 [--note 52 ...] --rel 8 --q 6 --noise 0.15 ...]
```

The input must be 48 kHz mono or stereo, PCM16 or float32. For the most realistic result,
record your own mic through the real preamp.

**Acceptance test** (`AcceptanceNoteOnlyFromErasedSettings`):

1. Start from erased flash (all 0xFF).
2. Check that the defaults are loaded with INTERNAL_SYNTH selected.
3. Feed a synthetic vowel as the mic signal and send **only** Note On/Off (no CC).
4. Check that the output is finite and audible (≈ −14 dBFS RMS for a −20 dBFS voice).
5. Check that the output follows the voice, goes silent after Note Off, and that nothing was written to flash.

### Hardware checklist (not yet run on a device)

These checks wait for the custom hardware currently in development.

1. Erase the settings (or flash a fresh board). Connect the mic preamp and a DIN MIDI keyboard on channel 1.
   Play notes without sending any CC: you should hear the vocoded voice.
2. Build with `VOCODER_CPU_LOG=1`. Read the average/max CPU load over USB serial. Check for no dropouts at 48 kHz.
3. Exercise every CC from the table. Confirm smooth Formant Shift and Q sweeps with no clicks.
4. Select EXTERNAL_AUDIO (CC 14 ≥ 64) and feed a carrier into IN R.
   With no carrier present, wet output should be silent and the dry signal should still follow Dry/Wet.
5. Change settings, wait more than 2 s, power-cycle: the settings should be retained.
   Hold CC 102 for 5 s, power-cycle: the defaults should be back.
6. Overdrive the inputs: the LED should light and the output should stay ≤ −1 dBFS before Output Level.
   Listen for limiter artifacts and for any NaN/glitch recovery.
7. If USB MIDI is enabled: check that it works alongside DIN MIDI without audio glitches.

---

## Roadmap

- [ ] Finish and bring up the custom hardware (mic preamp, DIN MIDI input, line I/O)
- [ ] Run the hardware checklist and publish measured CPU load / deadline margin
- [ ] Hardware audio demos
- [ ] Continuous integration for the firmware build and desktop tests

---

## License

MIT. See [LICENSE](LICENSE). libDaisy and DaisySP are © Electrosmith and are distributed under their own (MIT) licenses.
