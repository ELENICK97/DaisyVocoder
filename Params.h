#pragma once
/**
 * Params.h — the complete user-adjustable parameter set.
 *
 * Shared by the MIDI mapper (writes), settings storage (persists) and the
 * audio engine (reads a copy). Every field is 4 bytes so the struct has no
 * padding and can be compared/stored byte-wise.
 */
#include <cstdint>

enum CarrierMode : uint32_t
{
    CARRIER_INTERNAL_SYNTH = 0,
    CARRIER_EXTERNAL_AUDIO = 1,
};

/** Identifies one parameter; used by MIDI CC mapping. */
enum ParamId
{
    PARAM_CARRIER_MODE,
    PARAM_CARRIER_LEVEL,
    PARAM_MIC_GAIN_DB,
    PARAM_DRY_WET,
    PARAM_OUTPUT_LEVEL,
    PARAM_VOC_ATTACK_MS,
    PARAM_VOC_RELEASE_MS,
    PARAM_ENV_AMOUNT,
    PARAM_FORMANT_SHIFT_ST,
    PARAM_FILTER_Q,
    PARAM_WAVEFORM,
    PARAM_NOISE_LEVEL,
    PARAM_PULSE_WIDTH,
    PARAM_SYNTH_ATTACK_MS,
    PARAM_SYNTH_DECAY_MS,
    PARAM_SYNTH_SUSTAIN,
    PARAM_SYNTH_RELEASE_MS,
    PARAM_GLIDE_MS,
    PARAM_LEGATO,
    PARAM_POLY,
    PARAM_COUNT,
};

struct Params
{
    uint32_t carrier_mode;     /**< CarrierMode */
    float    carrier_level;    /**< linear gain */
    float    mic_gain_db;      /**< dB */
    float    dry_wet;          /**< 0 = dry .. 1 = wet */
    float    output_level;     /**< linear gain after limiter */
    float    voc_attack_ms;    /**< envelope follower attack, ms */
    float    voc_release_ms;   /**< envelope follower release, ms */
    float    env_amount;       /**< 0..1 */
    float    formant_shift_st; /**< semitone-equivalents */
    float    filter_q;         /**< band-pass Q */
    uint32_t waveform;         /**< SYNTH_WAVE_* */
    float    noise_level;      /**< linear */
    float    pulse_width;      /**< fraction of period */
    float    synth_attack_ms;
    float    synth_decay_ms;
    float    synth_sustain; /**< 0..1 */
    float    synth_release_ms;
    float    glide_ms; /**< 0 = off */
    uint32_t legato;   /**< 0/1 */
    uint32_t poly;     /**< 0 = Mono, 1 = Poly */
};

/** Compile-time defaults (fresh install and INIT). */
Params DefaultParams();

/** Clamp every field into its legal range; non-finite floats and out-of-range
 *  enums are replaced by the default. Returns true if anything changed. */
bool SanitizeParams(Params &p);

/** Set one parameter (value in the parameter's own units) and clamp it. */
void SetParam(Params &p, ParamId id, float value);

/** Read one parameter in its own units. */
float GetParam(const Params &p, ParamId id);

bool ParamsEqual(const Params &a, const Params &b);
