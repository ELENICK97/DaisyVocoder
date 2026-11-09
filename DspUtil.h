#pragma once
/**
 * DspUtil.h — small, allocation-free DSP building blocks shared by the
 * vocoder, synth and output stage. No hardware dependencies.
 */
#include <cmath>

namespace dsp
{
constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float Clamp(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

inline float DbToLin(float db)
{
    return std::pow(10.0f, db * 0.05f);
}

/** One-pole coefficient so that y approaches x with time constant tau_ms. */
inline float OnePoleCoef(float tau_ms, float rate_hz)
{
    const float samples = tau_ms * 0.001f * rate_hz;
    return samples <= 1.0f ? 1.0f : 1.0f - std::exp(-1.0f / samples);
}

/** Flush values that would decay into the denormal range. */
inline float FlushTiny(float x)
{
    return (std::fabs(x) < 1e-15f) ? 0.0f : x;
}

/** Normalised biquad coefficients (a0 == 1). */
struct BiquadCoefs
{
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

/** RBJ cookbook band-pass, constant 0 dB peak gain at the centre frequency.
 *  freq_hz and q are clamped so the result is always stable and finite in
 *  single precision: freq in [10 Hz, 0.49 * rate], q in [0.1, 100]. */
inline BiquadCoefs BandpassCoefs(float freq_hz, float q, float rate_hz)
{
    if(!(freq_hz == freq_hz)) // NaN
        freq_hz = 1000.0f;
    if(!(q == q))
        q = 1.0f;
    freq_hz            = Clamp(freq_hz, 10.0f, 0.49f * rate_hz);
    q                  = Clamp(q, 0.1f, 100.0f);
    const float w0     = kTwoPi * freq_hz / rate_hz;
    const float cw     = std::cos(w0);
    const float alpha  = std::sin(w0) / (2.0f * q);
    const float inv_a0 = 1.0f / (1.0f + alpha);
    BiquadCoefs c;
    c.b0 = alpha * inv_a0;
    c.b1 = 0.0f;
    c.b2 = -alpha * inv_a0;
    c.a1 = -2.0f * cw * inv_a0;
    c.a2 = (1.0f - alpha) * inv_a0;
    return c;
}

/** Transposed direct form II biquad. Coefficients may be swapped at any time
 *  (state is preserved), which keeps small sweeps click-free. */
class Biquad
{
  public:
    void SetCoefs(const BiquadCoefs &c) { c_ = c; }
    const BiquadCoefs &Coefs() const { return c_; }
    void               Reset() { z1_ = z2_ = 0.0f; }

    inline float Process(float x)
    {
        const float y = c_.b0 * x + z1_;
        z1_           = c_.b1 * x - c_.a1 * y + z2_;
        z2_           = c_.b2 * x - c_.a2 * y;
        return y;
    }

  private:
    BiquadCoefs c_;
    float       z1_ = 0.0f, z2_ = 0.0f;
};

/** Per-sample one-pole parameter smoother. Snaps exactly onto the target
 *  once close, so endpoints (e.g. 0 % / 100 % wet) are exact. */
class Smoother
{
  public:
    void Init(float tau_ms, float rate_hz, float initial)
    {
        coef_ = OnePoleCoef(tau_ms, rate_hz);
        y_    = initial;
    }
    void         Reset(float v) { y_ = v; }
    inline float Process(float target)
    {
        y_ += coef_ * (target - y_);
        // In float the approach stalls once coef * diff < 0.5 ulp(y), which
        // for slow smoothers is ~1e-5; snap before that (-80 dB step).
        if(std::fabs(target - y_) < 1e-4f)
            y_ = target;
        return y_;
    }
    float Value() const { return y_; }

  private:
    float coef_ = 1.0f;
    float y_    = 0.0f;
};

/** Conservative peak limiter: instantaneous attack (output never exceeds the
 *  threshold), exponential gain recovery. Final hard clamp guards against any
 *  rounding overshoot. */
class PeakLimiter
{
  public:
    void Init(float threshold, float release_ms, float rate_hz)
    {
        thr_  = threshold;
        rel_  = OnePoleCoef(release_ms, rate_hz);
        gain_ = 1.0f;
    }
    void  Reset() { gain_ = 1.0f; }
    float Gain() const { return gain_; }

    inline float Process(float x)
    {
        const float a      = std::fabs(x);
        const float target = (a > thr_) ? thr_ / a : 1.0f;
        if(target < gain_)
            gain_ = target;
        else
            gain_ += rel_ * (target - gain_);
        return Clamp(x * gain_, -thr_, thr_);
    }

  private:
    float thr_  = 1.0f;
    float rel_  = 1.0f;
    float gain_ = 1.0f;
};

} // namespace dsp
