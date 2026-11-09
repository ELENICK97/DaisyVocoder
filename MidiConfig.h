#pragma once
/**
 * MidiConfig.h — MIDI channel, CC assignments, Pitch Bend range, INIT hold.
 *
 * Change a CC number here and rebuild; no DSP code needs editing.
 * All CC numbers must be unique and below 120 (120-127 are Channel Mode
 * messages). MidiMapper.cpp static_asserts both rules.
 *
 * The chosen numbers sit in MIDI's "undefined" ranges (14-31, 102-119),
 * except Legato which uses the standard Legato Footswitch CC 68.
 */

/** Receive channel, 1..16. 0 = omni (respond on all channels). */
#define MIDI_CHANNEL 1

/** Pitch Bend range, +/- semitones. */
#define MIDI_PITCH_BEND_RANGE_ST 2.0f

/** INIT: CC value >= MIDI_SWITCH_THRESHOLD counts as "pressed". The button must
 *  be held continuously this long (ms) before defaults are restored. */
#define MIDI_INIT_HOLD_MS 5000

/** CC value at/above which on/off and two-state controls read as "on". */
#define MIDI_SWITCH_THRESHOLD 64

/** 1 = also accept MIDI from a computer over the Seed's USB port (USB MIDI
 *  device), merged with DIN MIDI. Off by default: not verified on hardware. */
#ifndef MIDI_ENABLE_USB
#define MIDI_ENABLE_USB 0
#endif

/*---------------------------------------------------------------------------
 * Control Change assignments
 *-------------------------------------------------------------------------*/
/* Carrier / levels */
#define MIDI_CC_CARRIER_MODE 14  /**< 0-63 INTERNAL_SYNTH, 64-127 EXTERNAL_AUDIO */
#define MIDI_CC_CARRIER_LEVEL 15 /**< 0..VOCODER_CARRIER_LEVEL_MAX, squared curve */
#define MIDI_CC_MIC_GAIN 16      /**< VOCODER_MIC_GAIN_DB_MIN..MAX dB, linear */
#define MIDI_CC_DRY_WET 17       /**< 0 = mic only .. 127 = vocoder only */
#define MIDI_CC_OUTPUT_LEVEL 18  /**< 0..VOCODER_OUTPUT_LEVEL_MAX, squared curve */

/* Vocoder */
#define MIDI_CC_VOC_ATTACK 19    /**< VOCODER_ATTACK_MS_MIN..MAX, exponential */
#define MIDI_CC_VOC_RELEASE 20   /**< VOCODER_RELEASE_MS_MIN..MAX, exponential */
#define MIDI_CC_ENV_AMOUNT 21    /**< 0..1 */
#define MIDI_CC_FORMANT_SHIFT 22 /**< -12..+12 st, 64 = neutral */
#define MIDI_CC_FILTER_Q 23      /**< VOCODER_FILTER_Q_MIN..MAX, exponential */

/* Synth */
#define MIDI_CC_WAVEFORM 24      /**< 5 zones: Saw, Square, Triangle, Sine, Noise */
#define MIDI_CC_NOISE_LEVEL 25   /**< 0..SYNTH_NOISE_LEVEL_MAX, squared curve */
#define MIDI_CC_PULSE_WIDTH 26   /**< SYNTH_PULSE_WIDTH_MIN..MAX, linear */
#define MIDI_CC_SYNTH_ATTACK 27  /**< SYNTH_ATTACK_MS_MIN..MAX, exponential */
#define MIDI_CC_SYNTH_DECAY 28   /**< SYNTH_DECAY_MS_MIN..MAX, exponential */
#define MIDI_CC_SYNTH_SUSTAIN 29 /**< 0..1 */
#define MIDI_CC_SYNTH_RELEASE 30 /**< SYNTH_RELEASE_MS_MIN..MAX, exponential */
#define MIDI_CC_GLIDE 31         /**< 0 = off, else up to SYNTH_GLIDE_MS_MAX, squared */
#define MIDI_CC_LEGATO 68        /**< 0-63 off, 64-127 on (Mono mode only) */
#define MIDI_CC_POLY 103         /**< 0-63 Mono, 64-127 Poly (SYNTH_NUM_VOICES) */

/* System */
#define MIDI_CC_INIT 102 /**< momentary button; hold MIDI_INIT_HOLD_MS to INIT */
