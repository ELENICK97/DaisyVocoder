#include "Engine.h"

#include <cmath>

#include "VocoderConfig.h"

using namespace dsp;

void Engine::Init(float sample_rate, size_t block_size)
{
    const float block_rate = sample_rate / static_cast<float>(block_size);
    synth_.Init(sample_rate);
    vocoder_.Init(sample_rate, block_rate);
    limiter_.Init(LIMITER_THRESHOLD, LIMITER_RELEASE_MS, sample_rate);

    params_          = DefaultParams();
    mic_gain_target_ = DbToLin(params_.mic_gain_db);
    mic_gain_.Init(VOCODER_LEVEL_SMOOTH_MS, sample_rate, mic_gain_target_);
    carrier_level_.Init(
        VOCODER_LEVEL_SMOOTH_MS, sample_rate, params_.carrier_level);
    carrier_sel_.Init(VOCODER_LEVEL_SMOOTH_MS,
                      sample_rate,
                      params_.carrier_mode == CARRIER_EXTERNAL_AUDIO ? 1.0f
                                                                     : 0.0f);
    dry_wet_.Init(VOCODER_LEVEL_SMOOTH_MS, sample_rate, params_.dry_wet);
    out_level_.Init(VOCODER_LEVEL_SMOOTH_MS, sample_rate, params_.output_level);
    SetParams(params_);
}

void Engine::SetParams(const Params &in)
{
    Params p = in;
    SanitizeParams(p); // never trust a cross-context copy blindly
    params_ = p;

    mic_gain_target_ = DbToLin(p.mic_gain_db);
    vocoder_.SetAttackMs(p.voc_attack_ms);
    vocoder_.SetReleaseMs(p.voc_release_ms);
    vocoder_.SetEnvAmount(p.env_amount);
    vocoder_.SetFormantShift(p.formant_shift_st);
    vocoder_.SetFilterQ(p.filter_q);

    synth_.SetWaveform(p.waveform);
    synth_.SetNoiseLevel(p.noise_level);
    synth_.SetPulseWidth(p.pulse_width);
    synth_.SetAdsr(p.synth_attack_ms,
                   p.synth_decay_ms,
                   p.synth_sustain,
                   p.synth_release_ms);
    synth_.SetGlideMs(p.glide_ms);
    synth_.SetLegato(p.legato != 0);
    synth_.SetPolyphonic(p.poly != 0);
}

void Engine::Process(const float *mic,
                     const float *ext,
                     float       *out_l,
                     float       *out_r,
                     size_t       size)
{
    vocoder_.UpdateControl();

    const float carrier_sel_target
        = params_.carrier_mode == CARRIER_EXTERNAL_AUDIO ? 1.0f : 0.0f;
    bool clipped = false;
    bool fault   = false;

    for(size_t i = 0; i < size; i++)
    {
        const float raw_mic = mic[i];
        const float raw_ext = ext[i];
        // Lightweight clip detection on the raw ADC values.
        if(std::fabs(raw_mic) >= CLIP_DETECT_THRESHOLD
           || std::fabs(raw_ext) >= CLIP_DETECT_THRESHOLD)
            clipped = true;

        const float m = raw_mic * mic_gain_.Process(mic_gain_target_);

        // Synth always runs so its envelopes keep time in either mode; the
        // carrier switch is crossfaded to avoid a click. No fallback: in
        // EXTERNAL mode a silent input gives a silent wet signal.
        const float synth = synth_.Process();
        const float sel   = carrier_sel_.Process(carrier_sel_target);
        const float carrier
            = (synth * (1.0f - sel) + raw_ext * sel)
              * carrier_level_.Process(params_.carrier_level);

        const float wet = vocoder_.Process(m, carrier);
        const float w   = dry_wet_.Process(params_.dry_wet);
        float       mix = m * (1.0f - w) + wet * w;

        if(!std::isfinite(mix))
        {
            mix   = 0.0f;
            fault = true;
        }
        const float y = limiter_.Process(mix) * out_level_.Process(params_.output_level);
        out_l[i]      = y;
        out_r[i]      = y;
    }

    if(clipped)
        clip_events_.fetch_add(1, std::memory_order_relaxed);
    if(fault)
    {
        // Recover from NaN/Inf: clear all recursive state.
        vocoder_.Reset();
        limiter_.Reset();
        fault_events_.fetch_add(1, std::memory_order_relaxed);
    }
}
