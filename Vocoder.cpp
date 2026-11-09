#include "Vocoder.h"

#include <cmath>

using namespace dsp;

float Vocoder::BandCenterHz(int i, int n, float fmin, float fmax)
{
    if(n < 2)
        return fmin;
    const float t = static_cast<float>(i) / static_cast<float>(n - 1);
    return fmin * std::pow(fmax / fmin, t);
}

float Vocoder::SectionQ(float q)
{
    // n identical 2nd-order sections: bandwidth shrinks by sqrt(2^(1/n) - 1).
    const float n = static_cast<float>(BandFilter::kSections);
    return q * std::sqrt(std::pow(2.0f, 1.0f / n) - 1.0f);
}

float Vocoder::ShiftedCenterHz(float center_hz, float semitones, float fs)
{
    semitones = Clamp(semitones,
                      VOCODER_FORMANT_SHIFT_MIN,
                      VOCODER_FORMANT_SHIFT_MAX);
    float ceil = VOCODER_FILTER_FREQ_CEIL_HZ;
    if(ceil > 0.45f * fs)
        ceil = 0.45f * fs;
    const float f = center_hz * std::exp2(semitones / 12.0f);
    return Clamp(f, VOCODER_FILTER_FREQ_FLOOR_HZ, ceil);
}

void Vocoder::Init(float sample_rate, float block_rate)
{
    sr_        = sample_rate;
    ctrl_coef_ = OnePoleCoef(VOCODER_COEF_SMOOTH_MS, block_rate);
    car_coef_  = OnePoleCoef(VOCODER_CARRIER_ENV_MS, sample_rate);
    smooth_coef_ = OnePoleCoef(VOCODER_ENV_SMOOTH_MS, sample_rate);
    for(int i = 0; i < kNumBands; i++)
        ana_hz_[i] = BandCenterHz(
            i, kNumBands, VOCODER_MIN_FREQ_HZ, VOCODER_MAX_FREQ_HZ);
    SetAttackMs(VOCODER_ATTACK_MS_DEFAULT);
    SetReleaseMs(VOCODER_RELEASE_MS_DEFAULT);
    env_amount_target_ = VOCODER_ENV_AMOUNT_DEFAULT;
    env_amount_.Init(VOCODER_LEVEL_SMOOTH_MS, sample_rate, env_amount_target_);
    shift_target_ = shift_cur_ = VOCODER_FORMANT_SHIFT_DEFAULT;
    q_target_ = q_cur_ = VOCODER_FILTER_Q_DEFAULT;
    DesignAnalysis();
    DesignSynthesis();
    Reset();
}

void Vocoder::Reset()
{
    for(int i = 0; i < kNumBands; i++)
    {
        ana_[i].Reset();
        syn_[i].Reset();
        env_[i]      = 0.0f;
        env_s_[i]    = 0.0f;
        car_env_[i]  = 0.0f;
        car_norm_[i] = 1.0f;
    }
}

void Vocoder::UpdateCarrierNorm()
{
#if VOCODER_CARRIER_NORMALIZE
    // Gain that brings each band's carrier level to the mean band level.
    // eps keeps the ratio at ~1 when the carrier is silent.
    constexpr float eps  = 1e-5f;
    float           mean = 0.0f;
    for(int i = 0; i < kNumBands; i++)
        mean += car_env_[i];
    mean = mean / kNumBands + eps;
    for(int i = 0; i < kNumBands; i++)
    {
        const float g = mean / (car_env_[i] + eps);
        car_norm_[i]  = g > VOCODER_CARRIER_NORM_MAX ? VOCODER_CARRIER_NORM_MAX : g;
    }
#endif
}

void Vocoder::SetAttackMs(float ms)
{
    atk_coef_ = OnePoleCoef(
        Clamp(ms, VOCODER_ATTACK_MS_MIN, VOCODER_ATTACK_MS_MAX), sr_);
}

void Vocoder::SetReleaseMs(float ms)
{
    rel_coef_ = OnePoleCoef(
        Clamp(ms, VOCODER_RELEASE_MS_MIN, VOCODER_RELEASE_MS_MAX), sr_);
}

void Vocoder::SetEnvAmount(float amount)
{
    env_amount_target_ = Clamp(amount, 0.0f, 1.0f);
}

void Vocoder::SetFormantShift(float semitones)
{
    if(!std::isfinite(semitones))
        return;
    shift_target_
        = Clamp(semitones, VOCODER_FORMANT_SHIFT_MIN, VOCODER_FORMANT_SHIFT_MAX);
}

void Vocoder::SetFilterQ(float q)
{
    if(!std::isfinite(q))
        return;
    q_target_ = Clamp(q, VOCODER_FILTER_Q_MIN, VOCODER_FILTER_Q_MAX);
}

void Vocoder::DesignAnalysis()
{
    const float sq = SectionQ(q_cur_);
    for(int i = 0; i < kNumBands; i++)
        ana_[i].SetCoefs(BandpassCoefs(ana_hz_[i], sq, sr_));

    // A narrower band passes less energy of a broadband signal; amplitude
    // scales roughly with 1/sqrt(Q). Compensate on both the envelopes and the
    // synthesis sum so loudness stays similar across the Q range.
    const float comp = std::sqrt(q_cur_ / VOCODER_FILTER_Q_DEFAULT);
    env_gain_        = VOCODER_ENV_SENSITIVITY * comp;
    wet_gain_        = VOCODER_WET_MAKEUP_GAIN * comp;
    flat_gain_       = 1.0f / VOCODER_WET_MAKEUP_GAIN;
}

void Vocoder::DesignSynthesis()
{
    const float sq = SectionQ(q_cur_ * VOCODER_SYNTH_Q_RATIO);
    for(int i = 0; i < kNumBands; i++)
    {
        syn_hz_[i] = ShiftedCenterHz(ana_hz_[i], shift_cur_, sr_);
        syn_[i].SetCoefs(BandpassCoefs(syn_hz_[i], sq, sr_));
    }
}

void Vocoder::UpdateControl()
{
    bool q_changed = false, shift_changed = false;

    if(q_cur_ != q_target_)
    {
        // Smooth in the log domain so the sweep feels even across the range.
        const float lc = std::log(q_cur_), lt = std::log(q_target_);
        const float ln = lc + ctrl_coef_ * (lt - lc);
        q_cur_ = (std::fabs(lt - ln) < 1e-3f) ? q_target_ : std::exp(ln);
        q_changed = true;
    }
    if(shift_cur_ != shift_target_)
    {
        shift_cur_ += ctrl_coef_ * (shift_target_ - shift_cur_);
        if(std::fabs(shift_target_ - shift_cur_) < 1e-3f)
            shift_cur_ = shift_target_;
        shift_changed = true;
    }
    if(q_changed)
        DesignAnalysis();
    if(q_changed || shift_changed)
        DesignSynthesis();
    UpdateCarrierNorm();
}

float Vocoder::Process(float modulator, float carrier)
{
    const float amount = env_amount_.Process(env_amount_target_);
    const float flat   = (1.0f - amount) * flat_gain_;
    const float depth  = amount * env_gain_;
    float       sum    = 0.0f;
    for(int i = 0; i < kNumBands; i++)
    {
        const float a    = std::fabs(ana_[i].Process(modulator));
        const float coef = (a > env_[i]) ? atk_coef_ : rel_coef_;
        env_[i]          = FlushTiny(env_[i] + coef * (a - env_[i]));
        // Fixed post-smoothing removes rectifier/pitch ripple without
        // adding the lag a longer Release would.
        env_s_[i] = FlushTiny(env_s_[i] + smooth_coef_ * (env_[i] - env_s_[i]));

        float g = flat + depth * env_s_[i];
        if(g > VOCODER_BAND_GAIN_MAX)
            g = VOCODER_BAND_GAIN_MAX;
        const float c = syn_[i].Process(carrier);
        car_env_[i]   = FlushTiny(car_env_[i] + car_coef_ * (std::fabs(c) - car_env_[i]));
        sum += g * car_norm_[i] * c;
    }
    return sum * wet_gain_;
}
