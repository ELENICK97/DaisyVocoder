#pragma once
/**
 * Vocoder.h — N-band channel vocoder (analysis filters + envelope followers +
 * synthesis filters). Produces the wet signal only; mixing happens in Engine.
 *
 * Formant Shift method: the analysis bands stay at their fixed logarithmic
 * centres; every synthesis band centre is multiplied by 2^(shift/12) and
 * clamped to [VOCODER_FILTER_FREQ_FLOOR_HZ, min(CEIL, 0.45 * fs)]. This moves
 * the spectral envelope imposed on the carrier up or down; it does not change
 * the carrier pitch.
 *
 * Coefficients are only recomputed in UpdateControl() (call once per audio
 * block), which glides Formant Shift and Q toward their targets so changes
 * are smooth and never redesign filters per sample.
 */
#include <cstddef>

#include "DspUtil.h"
#include "VocoderConfig.h"

/** One band: VOCODER_FILTER_SECTIONS identical biquads in cascade. */
class BandFilter
{
  public:
    static constexpr int kSections = VOCODER_FILTER_SECTIONS;
    static_assert(kSections >= 1 && kSections <= 4, "1..4 sections");

    void SetCoefs(const dsp::BiquadCoefs &c)
    {
        for(int s = 0; s < kSections; s++)
            sec_[s].SetCoefs(c);
    }
    const dsp::BiquadCoefs &Coefs() const { return sec_[0].Coefs(); }
    void                    Reset()
    {
        for(int s = 0; s < kSections; s++)
            sec_[s].Reset();
    }
    inline float Process(float x)
    {
        for(int s = 0; s < kSections; s++)
            x = sec_[s].Process(x);
        return x;
    }

  private:
    dsp::Biquad sec_[kSections];
};

class Vocoder
{
  public:
    static constexpr int kNumBands = VOCODER_NUM_BANDS;
    static_assert(kNumBands >= 2, "need at least 2 bands");

    /** Per-section Q for a cascade whose overall -3 dB bandwidth equals that
     *  of a single section with quality q. */
    static float SectionQ(float q);

    void Init(float sample_rate, float block_rate);
    /** Clear all filter and envelope state (e.g. after a NaN). */
    void Reset();

    void SetAttackMs(float ms);
    void SetReleaseMs(float ms);
    void SetEnvAmount(float amount);       /**< 0..1 */
    void SetFormantShift(float semitones); /**< target, smoothed */
    void SetFilterQ(float q);              /**< target, smoothed */

    /** Advance coefficient smoothing; recompute coefficients if needed, and
     *  refresh the carrier flattening gains.
     *  Bounded cost: at most 2 * kNumBands coefficient designs. */
    void UpdateControl();

    /** Process one sample: modulator (mic) and carrier in, wet sample out. */
    float Process(float modulator, float carrier);

    /** Log-spaced band centre i of n between fmin and fmax (inclusive). */
    static float BandCenterHz(int i, int n, float fmin, float fmax);
    /** Synthesis centre for an analysis centre after Formant Shift, clamped. */
    static float ShiftedCenterHz(float center_hz, float semitones, float fs);

    float AnalysisCenterHz(int i) const { return ana_hz_[i]; }
    float SynthesisCenterHz(int i) const { return syn_hz_[i]; }
    float Envelope(int i) const { return env_[i]; }
    /** Envelope actually applied to synthesis band i (after post-smoothing). */
    float AppliedEnvelope(int i) const { return env_s_[i]; }
    float CarrierNorm(int i) const { return car_norm_[i]; }
    const dsp::BiquadCoefs &AnalysisCoefs(int i) const
    {
        return ana_[i].Coefs();
    }
    const dsp::BiquadCoefs &SynthesisCoefs(int i) const
    {
        return syn_[i].Coefs();
    }
    float CurrentFormantShift() const { return shift_cur_; }
    float CurrentQ() const { return q_cur_; }

  private:
    void DesignAnalysis();
    void DesignSynthesis();

    float sr_ = VOCODER_SAMPLE_RATE_HZ;

    void UpdateCarrierNorm();

    BandFilter ana_[kNumBands];
    BandFilter syn_[kNumBands];
    float       env_[kNumBands];
    float       env_s_[kNumBands]; // post-smoothed envelope used for gain
    float       smooth_coef_ = 1.0f;
    float       car_env_[kNumBands]; // carrier level per synthesis band
    float       car_norm_[kNumBands]; // flattening gain, updated per block
    float       car_coef_ = 1.0f;
    float       ana_hz_[kNumBands];
    float       syn_hz_[kNumBands];

    float atk_coef_ = 1.0f, rel_coef_ = 1.0f;
    float env_amount_target_ = 1.0f;
    dsp::Smoother env_amount_;

    float shift_target_ = 0.0f, shift_cur_ = 0.0f;
    float q_target_ = VOCODER_FILTER_Q_DEFAULT, q_cur_ = VOCODER_FILTER_Q_DEFAULT;
    float ctrl_coef_ = 1.0f; // block-rate smoothing coefficient

    // Derived gain terms, refreshed when Q changes.
    float env_gain_  = VOCODER_ENV_SENSITIVITY;
    float wet_gain_  = VOCODER_WET_MAKEUP_GAIN;
    float flat_gain_ = 1.0f / VOCODER_WET_MAKEUP_GAIN;
};
