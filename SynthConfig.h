#pragma once
/**
 * SynthConfig.h — compile-time configuration for the internal monophonic
 * carrier synth. Values marked "(chosen)" were not fixed by the V1 spec.
 */

/** Waveform indices (used by the Waveform CC and saved settings). */
#define SYNTH_WAVE_SAW 0
#define SYNTH_WAVE_SQUARE 1
#define SYNTH_WAVE_TRIANGLE 2
#define SYNTH_WAVE_SINE 3
#define SYNTH_WAVE_NOISE 4
#define SYNTH_WAVE_COUNT 5

/** Default waveform. Saw is harmonically rich, the classic vocoder carrier. */
#define SYNTH_WAVEFORM_DEFAULT SYNTH_WAVE_SAW

/** Fixed oscillator amplitude before the ADSR, linear. (chosen) */
#define SYNTH_OSC_LEVEL 0.8f

/** Noise added to the oscillator, linear 0..MAX. With carrier flattening the
 *  noise mostly fills the high bands, so sibilants ("s", "f", "sh") sound
 *  hissy instead of buzzy while vowels stay pitched. (chosen) */
#define SYNTH_NOISE_LEVEL_DEFAULT 0.15f
#define SYNTH_NOISE_LEVEL_MAX 1.0f

/** Pulse width for the Square/Pulse waveform, fraction of period. (chosen) */
#define SYNTH_PULSE_WIDTH_DEFAULT 0.5f
#define SYNTH_PULSE_WIDTH_MIN 0.05f
#define SYNTH_PULSE_WIDTH_MAX 0.95f

/** ADSR. Attack is linear 0 -> 1 over the attack time. Decay and Release are
 *  exponential; the time given is the time to cover 99% of the distance
 *  (i.e. reach -40 dB for Release). Sustain is a linear level 0..1. (chosen) */
#define SYNTH_ATTACK_MS_DEFAULT 5.0f
#define SYNTH_ATTACK_MS_MIN 0.5f
#define SYNTH_ATTACK_MS_MAX 5000.0f

#define SYNTH_DECAY_MS_DEFAULT 150.0f
#define SYNTH_DECAY_MS_MIN 1.0f
#define SYNTH_DECAY_MS_MAX 5000.0f

#define SYNTH_SUSTAIN_DEFAULT 0.8f

#define SYNTH_RELEASE_MS_DEFAULT 200.0f
#define SYNTH_RELEASE_MS_MIN 1.0f
#define SYNTH_RELEASE_MS_MAX 8000.0f

/** Glide (portamento): constant-time linear pitch slide in ms.
 *  0 = off (spec default). */
#define SYNTH_GLIDE_MS_DEFAULT 0.0f
#define SYNTH_GLIDE_MS_MAX 2000.0f

/** Legato default: 0 = every new note retriggers the ADSR (from its current
 *  level, no click), 1 = overlapping notes change pitch without retrigger.
 *  (chosen: 0) */
#define SYNTH_LEGATO_DEFAULT 0

/** Maximum number of simultaneously held notes tracked by the note stack. */
#define SYNTH_NOTE_STACK_SIZE 16

/** Number of synth voices used in Poly mode (1..8). */
#define SYNTH_NUM_VOICES 4
/** Default voice mode: 1 = Poly (SYNTH_NUM_VOICES voices), 0 = Mono
 *  (last-note priority, Legato). */
#define SYNTH_POLY_DEFAULT 1
