#include "Params.h"

#include <cmath>
#include <cstring>

#include "SynthConfig.h"
#include "VocoderConfig.h"

static_assert(sizeof(Params) == PARAM_COUNT * 4, "Params must be unpadded");

Params DefaultParams()
{
    Params p;
    p.carrier_mode     = VOCODER_CARRIER_MODE_DEFAULT;
    p.carrier_level    = VOCODER_CARRIER_LEVEL_DEFAULT;
    p.mic_gain_db      = VOCODER_MIC_GAIN_DB_DEFAULT;
    p.dry_wet          = VOCODER_DRY_WET_DEFAULT;
    p.output_level     = VOCODER_OUTPUT_LEVEL_DEFAULT;
    p.voc_attack_ms    = VOCODER_ATTACK_MS_DEFAULT;
    p.voc_release_ms   = VOCODER_RELEASE_MS_DEFAULT;
    p.env_amount       = VOCODER_ENV_AMOUNT_DEFAULT;
    p.formant_shift_st = VOCODER_FORMANT_SHIFT_DEFAULT;
    p.filter_q         = VOCODER_FILTER_Q_DEFAULT;
    p.waveform         = SYNTH_WAVEFORM_DEFAULT;
    p.noise_level      = SYNTH_NOISE_LEVEL_DEFAULT;
    p.pulse_width      = SYNTH_PULSE_WIDTH_DEFAULT;
    p.synth_attack_ms  = SYNTH_ATTACK_MS_DEFAULT;
    p.synth_decay_ms   = SYNTH_DECAY_MS_DEFAULT;
    p.synth_sustain    = SYNTH_SUSTAIN_DEFAULT;
    p.synth_release_ms = SYNTH_RELEASE_MS_DEFAULT;
    p.glide_ms         = SYNTH_GLIDE_MS_DEFAULT;
    p.legato           = SYNTH_LEGATO_DEFAULT;
    p.poly             = SYNTH_POLY_DEFAULT;
    return p;
}

namespace
{
struct FloatLimits
{
    float min, max;
};

/** Limits per ParamId for float parameters; enums are handled separately. */
FloatLimits Limits(ParamId id)
{
    switch(id)
    {
        case PARAM_CARRIER_LEVEL: return {0.0f, VOCODER_CARRIER_LEVEL_MAX};
        case PARAM_MIC_GAIN_DB:
            return {VOCODER_MIC_GAIN_DB_MIN, VOCODER_MIC_GAIN_DB_MAX};
        case PARAM_DRY_WET: return {0.0f, 1.0f};
        case PARAM_OUTPUT_LEVEL: return {0.0f, VOCODER_OUTPUT_LEVEL_MAX};
        case PARAM_VOC_ATTACK_MS:
            return {VOCODER_ATTACK_MS_MIN, VOCODER_ATTACK_MS_MAX};
        case PARAM_VOC_RELEASE_MS:
            return {VOCODER_RELEASE_MS_MIN, VOCODER_RELEASE_MS_MAX};
        case PARAM_ENV_AMOUNT: return {0.0f, 1.0f};
        case PARAM_FORMANT_SHIFT_ST:
            return {VOCODER_FORMANT_SHIFT_MIN, VOCODER_FORMANT_SHIFT_MAX};
        case PARAM_FILTER_Q:
            return {VOCODER_FILTER_Q_MIN, VOCODER_FILTER_Q_MAX};
        case PARAM_NOISE_LEVEL: return {0.0f, SYNTH_NOISE_LEVEL_MAX};
        case PARAM_PULSE_WIDTH:
            return {SYNTH_PULSE_WIDTH_MIN, SYNTH_PULSE_WIDTH_MAX};
        case PARAM_SYNTH_ATTACK_MS:
            return {SYNTH_ATTACK_MS_MIN, SYNTH_ATTACK_MS_MAX};
        case PARAM_SYNTH_DECAY_MS:
            return {SYNTH_DECAY_MS_MIN, SYNTH_DECAY_MS_MAX};
        case PARAM_SYNTH_SUSTAIN: return {0.0f, 1.0f};
        case PARAM_SYNTH_RELEASE_MS:
            return {SYNTH_RELEASE_MS_MIN, SYNTH_RELEASE_MS_MAX};
        case PARAM_GLIDE_MS: return {0.0f, SYNTH_GLIDE_MS_MAX};
        default: return {0.0f, 0.0f};
    }
}

float *FloatField(Params &p, ParamId id)
{
    switch(id)
    {
        case PARAM_CARRIER_LEVEL: return &p.carrier_level;
        case PARAM_MIC_GAIN_DB: return &p.mic_gain_db;
        case PARAM_DRY_WET: return &p.dry_wet;
        case PARAM_OUTPUT_LEVEL: return &p.output_level;
        case PARAM_VOC_ATTACK_MS: return &p.voc_attack_ms;
        case PARAM_VOC_RELEASE_MS: return &p.voc_release_ms;
        case PARAM_ENV_AMOUNT: return &p.env_amount;
        case PARAM_FORMANT_SHIFT_ST: return &p.formant_shift_st;
        case PARAM_FILTER_Q: return &p.filter_q;
        case PARAM_NOISE_LEVEL: return &p.noise_level;
        case PARAM_PULSE_WIDTH: return &p.pulse_width;
        case PARAM_SYNTH_ATTACK_MS: return &p.synth_attack_ms;
        case PARAM_SYNTH_DECAY_MS: return &p.synth_decay_ms;
        case PARAM_SYNTH_SUSTAIN: return &p.synth_sustain;
        case PARAM_SYNTH_RELEASE_MS: return &p.synth_release_ms;
        case PARAM_GLIDE_MS: return &p.glide_ms;
        default: return nullptr;
    }
}

uint32_t *EnumField(Params &p, ParamId id, uint32_t &count)
{
    switch(id)
    {
        case PARAM_CARRIER_MODE: count = 2; return &p.carrier_mode;
        case PARAM_WAVEFORM: count = SYNTH_WAVE_COUNT; return &p.waveform;
        case PARAM_LEGATO: count = 2; return &p.legato;
        case PARAM_POLY: count = 2; return &p.poly;
        default: count = 0; return nullptr;
    }
}

/** Clamp one field; returns true if it was modified. */
bool SanitizeOne(Params &p, ParamId id, const Params &defaults)
{
    Params d = defaults;
    if(float *f = FloatField(p, id))
    {
        const FloatLimits lim = Limits(id);
        const float       old = *f;
        if(!std::isfinite(*f))
            *f = *FloatField(d, id);
        else if(*f < lim.min)
            *f = lim.min;
        else if(*f > lim.max)
            *f = lim.max;
        return std::memcmp(&old, f, sizeof(float)) != 0;
    }
    uint32_t count = 0;
    if(uint32_t *e = EnumField(p, id, count))
    {
        if(*e >= count)
        {
            *e = *EnumField(d, id, count);
            return true;
        }
    }
    return false;
}
} // namespace

bool SanitizeParams(Params &p)
{
    const Params d       = DefaultParams();
    bool         changed = false;
    for(int i = 0; i < PARAM_COUNT; i++)
        changed |= SanitizeOne(p, static_cast<ParamId>(i), d);
    return changed;
}

void SetParam(Params &p, ParamId id, float value)
{
    if(float *f = FloatField(p, id))
    {
        *f = value;
    }
    else
    {
        uint32_t count = 0;
        if(uint32_t *e = EnumField(p, id, count))
        {
            // Negative / NaN become an out-of-range value, which is reset
            // to the default by SanitizeOne below.
            *e = (value >= 0.0f) ? static_cast<uint32_t>(value) : count;
        }
    }
    SanitizeOne(p, id, DefaultParams());
}

float GetParam(const Params &p, ParamId id)
{
    Params c = p;
    if(float *f = FloatField(c, id))
        return *f;
    uint32_t count = 0;
    if(uint32_t *e = EnumField(c, id, count))
        return static_cast<float>(*e);
    return 0.0f;
}

bool ParamsEqual(const Params &a, const Params &b)
{
    return std::memcmp(&a, &b, sizeof(Params)) == 0;
}
