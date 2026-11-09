#pragma once
/**
 * VocoderConfig.h — compile-time configuration for the vocoder DSP,
 * mixing/limiter and settings persistence.
 *
 * Units are stated on every define. Values marked "(chosen)" were not fixed
 * by the V1 spec and were picked as sensible, audible out-of-box defaults.
 * All runtime values are clamped to the MIN/MAX limits below.
 */

/*---------------------------------------------------------------------------
 * Audio engine
 *-------------------------------------------------------------------------*/
/** Sample rate in Hz. The Seed codec is configured for 48 kHz. */
#define VOCODER_SAMPLE_RATE_HZ 48000.0f
/** Samples per audio callback (chosen: 48 = 1 ms per block). */
#define VOCODER_BLOCK_SIZE 48
/** 1 = print average/max audio-callback CPU load once per second over the
 *  USB serial port (for hardware measurement). Uses the USB port, so it
 *  cannot be combined with MIDI_ENABLE_USB. */
#ifndef VOCODER_CPU_LOG
#define VOCODER_CPU_LOG 0
#endif

/*---------------------------------------------------------------------------
 * Filter bank layout
 *-------------------------------------------------------------------------*/
/** Number of analysis bands (= number of synthesis bands). */
#define VOCODER_NUM_BANDS 16
/** Centre frequency of the lowest band, Hz. */
#define VOCODER_MIN_FREQ_HZ 100.0f
/** Centre frequency of the highest band, Hz. Bands are spaced logarithmically
 *  between MIN and MAX (inclusive). */
#define VOCODER_MAX_FREQ_HZ 8000.0f
/** Biquad sections cascaded per band (analysis and synthesis). 3 = 6th-order
 *  band-pass (best STOI in tools/render_wav sweeps vs 1/2 sections): steeper skirts keep the carrier's strong low harmonics out of
 *  the high bands, which is essential for consonant intelligibility. The
 *  per-section Q is scaled so the band's -3 dB bandwidth matches Filter Q. */
#ifndef VOCODER_FILTER_SECTIONS
#define VOCODER_FILTER_SECTIONS 3
#endif
/** Synthesis band Q = Filter Q * this ratio (analysis uses Filter Q). */
#ifndef VOCODER_SYNTH_Q_RATIO
#define VOCODER_SYNTH_Q_RATIO 1.0f
#endif
/** Fixed one-pole smoothing after the Attack/Release follower, ms
 *  (0 = off). Removes rectifier and voice-pitch ripple. */
#ifndef VOCODER_ENV_SMOOTH_MS
#define VOCODER_ENV_SMOOTH_MS 1.5f
#endif
/** Safety limits for any filter centre frequency after Formant Shift, Hz.
 *  The upper limit is additionally clamped to 0.45 * sample rate. */
#define VOCODER_FILTER_FREQ_FLOOR_HZ 20.0f
#define VOCODER_FILTER_FREQ_CEIL_HZ 20000.0f

/*---------------------------------------------------------------------------
 * Vocoder parameters: defaults and limits
 *-------------------------------------------------------------------------*/
/** Envelope follower attack time, ms (time constant). Tuned for speech
 *  intelligibility with tools/render_wav (STOI). */
#define VOCODER_ATTACK_MS_DEFAULT 1.0f
#define VOCODER_ATTACK_MS_MIN 0.5f
#define VOCODER_ATTACK_MS_MAX 200.0f

/** Envelope follower release time, ms (time constant). Speech articulation
 *  needs envelope bandwidth well above 16 Hz: 60 ms scored STOI 0.59, 8 ms
 *  scores ~0.79. Longer = smoother/less rough, shorter = crisper. */
#define VOCODER_RELEASE_MS_DEFAULT 8.0f
#define VOCODER_RELEASE_MS_MIN 5.0f
#define VOCODER_RELEASE_MS_MAX 2000.0f

/** Envelope Amount, 0..1. 1 = bands fully follow the microphone envelopes,
 *  0 = flat filter bank (carrier passes unmodulated at ~unity). (chosen) */
#define VOCODER_ENV_AMOUNT_DEFAULT 1.0f

/** Formant Shift in semitone-equivalents, neutral = 0. Shifts the synthesis
 *  band centres by 2^(shift/12) relative to the analysis bands. */
#define VOCODER_FORMANT_SHIFT_DEFAULT 0.0f
#define VOCODER_FORMANT_SHIFT_MIN -12.0f
#define VOCODER_FORMANT_SHIFT_MAX 12.0f

/** Band-pass filter Q (dimensionless), shared by analysis and synthesis.
 *  6 scored best in STOI sweeps (4..8 tested). */
#define VOCODER_FILTER_Q_DEFAULT 6.0f
#define VOCODER_FILTER_Q_MIN 1.0f
#define VOCODER_FILTER_Q_MAX 20.0f

/** Dry/Wet, 0..1 (0 = microphone only, 1 = vocoder only). (chosen: 1) */
#define VOCODER_DRY_WET_DEFAULT 1.0f

/** Carrier Level, linear gain applied to the selected carrier. (chosen) */
#define VOCODER_CARRIER_LEVEL_DEFAULT 1.0f
#define VOCODER_CARRIER_LEVEL_MAX 2.0f

/** Digital microphone gain, dB, applied before analysis and dry path. (chosen) */
#define VOCODER_MIC_GAIN_DB_DEFAULT 6.0f
#define VOCODER_MIC_GAIN_DB_MIN -12.0f
#define VOCODER_MIC_GAIN_DB_MAX 36.0f

/** Output Level, linear gain applied after the limiter, 0..MAX. (chosen) */
#define VOCODER_OUTPUT_LEVEL_DEFAULT 0.8f
#define VOCODER_OUTPUT_LEVEL_MAX 1.0f

/** Carrier mode at fresh install / INIT: 0 = INTERNAL_SYNTH, 1 = EXTERNAL_AUDIO.
 *  Must stay INTERNAL_SYNTH (spec: sound with Note On/Off only). */
#define VOCODER_CARRIER_MODE_DEFAULT 0

/*---------------------------------------------------------------------------
 * Internal gain staging (tuned in desktop simulation, see tests)
 *-------------------------------------------------------------------------*/
/** Multiplier from band envelope (linear amplitude) to synthesis band gain at
 *  the default Q. Higher = more sensitive to quiet voices. */
#ifndef VOCODER_ENV_SENSITIVITY
#define VOCODER_ENV_SENSITIVITY 12.0f
#endif
/** Gain applied to the summed synthesis bands. */
#ifndef VOCODER_WET_MAKEUP_GAIN
#define VOCODER_WET_MAKEUP_GAIN 2.0f
#endif
/** Upper clamp for any single synthesis band gain (protects against blowups). */
#define VOCODER_BAND_GAIN_MAX 16.0f

/** Carrier spectral flattening. A saw/pulse carrier has ~30-40 dB more energy
 *  in the low bands than in the high ones, which buries formants and
 *  consonants. Each synthesis band's carrier level is tracked and normalised
 *  toward the mean band level (overall carrier level is preserved).
 *  1 = on, 0 = off. */
#ifndef VOCODER_CARRIER_NORMALIZE
#define VOCODER_CARRIER_NORMALIZE 1
#endif
/** Time constant of the per-band carrier level trackers, ms. */
#ifndef VOCODER_CARRIER_ENV_MS
#define VOCODER_CARRIER_ENV_MS 20.0f
#endif
/** Maximum boost a carrier band may receive from flattening, linear
 *  (31.6 = +30 dB). Limits amplification of near-empty bands. */
#ifndef VOCODER_CARRIER_NORM_MAX
#define VOCODER_CARRIER_NORM_MAX 31.6f
#endif

/** Smoothing time for Formant Shift / Q coefficient sweeps, ms. */
#define VOCODER_COEF_SMOOTH_MS 20.0f
/** Smoothing time for level parameters (gains, dry/wet, carrier mode), ms. */
#define VOCODER_LEVEL_SMOOTH_MS 10.0f

/*---------------------------------------------------------------------------
 * Limiter and clip detection
 *-------------------------------------------------------------------------*/
/** Limiter ceiling, linear full scale (0.891 = -1 dBFS). */
#define LIMITER_THRESHOLD 0.891f
/** Limiter gain recovery time constant, ms. Attack is instantaneous. */
#define LIMITER_RELEASE_MS 100.0f
/** |sample| at or above this on a raw input counts as ADC clipping. */
#define CLIP_DETECT_THRESHOLD 0.99f
/** How long the Seed user LED stays lit after a clip event, ms. */
#define CLIP_LED_HOLD_MS 200

/*---------------------------------------------------------------------------
 * Settings persistence
 *-------------------------------------------------------------------------*/
/** Save changed settings once no change has occurred for this long, ms. */
#define SETTINGS_SAVE_DEBOUNCE_MS 2000
/** QSPI byte offset of the settings record. Must be 4 KiB sector aligned and
 *  outside any program image stored in QSPI (last 64 KiB block of 8 MiB). */
#define SETTINGS_QSPI_OFFSET 0x007F0000u
