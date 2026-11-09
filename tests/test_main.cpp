/**
 * Desktop unit/integration tests for the hardware-independent vocoder code.
 * Build + run: tests\build_tests.bat (MSVC) or `make -C tests` (g++/clang).
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../ControlBridge.h"
#include "../Controller.h"
#include "../DspUtil.h"
#include "../Engine.h"
#include "../MidiConfig.h"
#include "../MidiMapper.h"
#include "../MidiParser.h"
#include "../Params.h"
#include "../SettingsStorage.h"
#include "../Synth.h"
#include "../SynthConfig.h"
#include "../Vocoder.h"
#include "../VocoderConfig.h"

/*------------------------------------------------------------------------*/
static int g_checks = 0, g_failures = 0;
static const char *g_test = "";

#define CHECK(cond)                                                          \
    do                                                                       \
    {                                                                        \
        g_checks++;                                                          \
        if(!(cond))                                                          \
        {                                                                    \
            g_failures++;                                                    \
            std::printf("  FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while(0)

#define CHECK_NEAR(a, b, tol)                                                \
    do                                                                       \
    {                                                                        \
        g_checks++;                                                          \
        const double _a = (a), _b = (b);                                     \
        if(!(std::fabs(_a - _b) <= (tol)))                                   \
        {                                                                    \
            g_failures++;                                                    \
            std::printf("  FAIL [%s] %s:%d: %s = %g, expected %g +/- %g\n",  \
                        g_test, __FILE__, __LINE__, #a, _a, _b, (double)(tol)); \
        }                                                                    \
    } while(0)

#define TEST(name) static void name()
#define RUN(name)               \
    do                          \
    {                           \
        g_test = #name;         \
        std::printf("%s\n", #name); \
        name();                 \
    } while(0)

static const float kSr    = VOCODER_SAMPLE_RATE_HZ;
static const size_t kBlock = VOCODER_BLOCK_SIZE;

/*------------------------------------------------------------------------*/
// Helpers

/** Magnitude response of a biquad at f. */
static double BiquadMag(const dsp::BiquadCoefs &c, double f, double sr)
{
    const double w = 2.0 * 3.141592653589793 * f / sr;
    // H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)
    const double cr1 = std::cos(w), ci1 = -std::sin(w);
    const double cr2 = std::cos(2 * w), ci2 = -std::sin(2 * w);
    const double nr = c.b0 + c.b1 * cr1 + c.b2 * cr2, ni = c.b1 * ci1 + c.b2 * ci2;
    const double dr = 1 + c.a1 * cr1 + c.a2 * cr2, di = c.a1 * ci1 + c.a2 * ci2;
    return std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
}

static bool BiquadStable(const dsp::BiquadCoefs &c)
{
    // Stability triangle for 1 + a1 z^-1 + a2 z^-2.
    return std::fabs(c.a2) < 1.0f && std::fabs(c.a1) < 1.0f + c.a2;
}

static bool Finite(const dsp::BiquadCoefs &c)
{
    return std::isfinite(c.b0) && std::isfinite(c.b1) && std::isfinite(c.b2)
           && std::isfinite(c.a1) && std::isfinite(c.a2);
}

/** Synthetic "vowel": 120 Hz pulse train through three formant resonators,
 *  normalised to the requested RMS. Stands in for a microphone signal. */
static std::vector<float> MakeVowel(size_t n, float rms_target)
{
    std::vector<float> out(n);
    dsp::Biquad f1, f2, f3;
    f1.SetCoefs(dsp::BandpassCoefs(700.0f, 8.0f, kSr));
    f2.SetCoefs(dsp::BandpassCoefs(1200.0f, 8.0f, kSr));
    f3.SetCoefs(dsp::BandpassCoefs(2600.0f, 8.0f, kSr));
    float phase = 0.0f;
    double acc  = 0.0;
    for(size_t i = 0; i < n; i++)
    {
        phase += 120.0f / kSr;
        float src = 0.0f;
        if(phase >= 1.0f)
        {
            phase -= 1.0f;
            src = 1.0f;
        }
        const float y = f1.Process(src) + 0.7f * f2.Process(src) + 0.4f * f3.Process(src);
        out[i] = y;
        acc += y * y;
    }
    const float rms = static_cast<float>(std::sqrt(acc / n));
    for(auto &v : out)
        v *= rms_target / rms;
    return out;
}

static double Rms(const float *x, size_t n)
{
    double a = 0.0;
    for(size_t i = 0; i < n; i++)
        a += x[i] * x[i];
    return std::sqrt(a / n);
}

static double Db(double lin)
{
    return 20.0 * std::log10(lin + 1e-12);
}

/** Run an engine over mic/ext buffers in blocks, collecting the left output. */
static std::vector<float> RunEngine(Engine &e,
                                    const std::vector<float> &mic,
                                    const std::vector<float> &ext,
                                    ControlBridge *bridge = nullptr)
{
    std::vector<float> l(mic.size()), r(mic.size());
    for(size_t i = 0; i + kBlock <= mic.size(); i += kBlock)
    {
        if(bridge)
            ApplyBridgeToEngine(*bridge, e);
        e.Process(&mic[i], &ext[i], &l[i], &r[i], kBlock);
    }
    return l;
}

/** RAM settings backend for tests. */
class FakeBackend : public SettingsBackend
{
  public:
    bool           has_data = false;
    StoredSettings data;
    int            writes = 0;
    bool Read(StoredSettings &out) override
    {
        if(!has_data)
            return false;
        out = data;
        return true;
    }
    bool Write(const StoredSettings &s) override
    {
        data     = s;
        has_data = true;
        writes++;
        return true;
    }
};

static MidiMessage Msg(MidiMessage::Type t, uint8_t d0, uint8_t d1, uint8_t ch = MIDI_CHANNEL - 1)
{
    MidiMessage m;
    m.type    = t;
    m.channel = ch;
    m.data0   = d0;
    m.data1   = d1;
    return m;
}

/*========================================================================*/
// Vocoder DSP

TEST(BandLayout)
{
    const int n = VOCODER_NUM_BANDS;
    CHECK(n == 16);
    Vocoder v;
    v.Init(kSr, kSr / kBlock);
    CHECK_NEAR(v.AnalysisCenterHz(0), VOCODER_MIN_FREQ_HZ, 1e-3);
    CHECK_NEAR(v.AnalysisCenterHz(n - 1), VOCODER_MAX_FREQ_HZ, 0.05);
    const double ratio = std::pow(VOCODER_MAX_FREQ_HZ / VOCODER_MIN_FREQ_HZ, 1.0 / (n - 1));
    for(int i = 1; i < n; i++)
    {
        CHECK(v.AnalysisCenterHz(i) > v.AnalysisCenterHz(i - 1));
        CHECK_NEAR(v.AnalysisCenterHz(i) / v.AnalysisCenterHz(i - 1), ratio, 1e-4);
    }
    // Neutral formant: synthesis == analysis.
    for(int i = 0; i < n; i++)
        CHECK_NEAR(v.SynthesisCenterHz(i), v.AnalysisCenterHz(i), 1e-3);
}

TEST(FilterCoefficientsAtExtremes)
{
    const float qs[]     = {VOCODER_FILTER_Q_MIN, VOCODER_FILTER_Q_DEFAULT, VOCODER_FILTER_Q_MAX};
    const float freqs[]  = {VOCODER_FILTER_FREQ_FLOOR_HZ, VOCODER_MIN_FREQ_HZ, 1000.0f,
                            VOCODER_MAX_FREQ_HZ, 0.45f * kSr, 0.0f, -5.0f, 1e9f};
    for(float q : qs)
        for(float f : freqs)
        {
            const dsp::BiquadCoefs c = dsp::BandpassCoefs(f, q, kSr);
            CHECK(Finite(c));
            CHECK(BiquadStable(c));
        }
    // Unity peak gain at centre.
    for(float q : qs)
    {
        const dsp::BiquadCoefs c = dsp::BandpassCoefs(1000.0f, q, kSr);
        CHECK_NEAR(BiquadMag(c, 1000.0, kSr), 1.0, 1e-3);
    }
    // Every band of a vocoder at the extremes of shift and Q.
    const float shifts[] = {VOCODER_FORMANT_SHIFT_MIN, 0.0f, VOCODER_FORMANT_SHIFT_MAX};
    for(float q : qs)
        for(float s : shifts)
        {
            Vocoder v;
            v.Init(kSr, kSr / kBlock);
            v.SetFilterQ(q);
            v.SetFormantShift(s);
            for(int k = 0; k < 2000; k++)
                v.UpdateControl();
            CHECK_NEAR(v.CurrentQ(), q, 1e-4);
            CHECK_NEAR(v.CurrentFormantShift(), s, 1e-4);
            for(int i = 0; i < Vocoder::kNumBands; i++)
            {
                CHECK(Finite(v.AnalysisCoefs(i)) && BiquadStable(v.AnalysisCoefs(i)));
                CHECK(Finite(v.SynthesisCoefs(i)) && BiquadStable(v.SynthesisCoefs(i)));
            }
        }
}

TEST(FormantShiftAndBounds)
{
    CHECK_NEAR(Vocoder::ShiftedCenterHz(1000.0f, 0.0f, kSr), 1000.0, 1e-3);
    CHECK_NEAR(Vocoder::ShiftedCenterHz(1000.0f, 12.0f, kSr), 2000.0, 0.01);
    CHECK_NEAR(Vocoder::ShiftedCenterHz(1000.0f, -12.0f, kSr), 500.0, 0.01);
    // Out-of-range shift is clamped to +/-12.
    CHECK_NEAR(Vocoder::ShiftedCenterHz(1000.0f, 48.0f, kSr), 2000.0, 0.01);
    // Frequency ceiling: min(CEIL, 0.45 fs) and floor.
    CHECK(Vocoder::ShiftedCenterHz(15000.0f, 12.0f, kSr) <= 0.45f * kSr);
    CHECK(Vocoder::ShiftedCenterHz(15000.0f, 12.0f, kSr) <= VOCODER_FILTER_FREQ_CEIL_HZ);
    CHECK(Vocoder::ShiftedCenterHz(5.0f, -12.0f, kSr) >= VOCODER_FILTER_FREQ_FLOOR_HZ);
    // Ceiling respects low sample rates too.
    CHECK(Vocoder::ShiftedCenterHz(8000.0f, 12.0f, 16000.0f) <= 0.45f * 16000.0f);

    Vocoder v;
    v.Init(kSr, kSr / kBlock);
    v.SetFormantShift(7.0f);
    // Smoothed: the first update moves only part of the way (no jump).
    v.UpdateControl();
    CHECK(v.CurrentFormantShift() > 0.0f && v.CurrentFormantShift() < 7.0f);
    float prev = v.CurrentFormantShift();
    float max_step = prev;
    for(int k = 0; k < 2000; k++)
    {
        v.UpdateControl();
        max_step = std::fmax(max_step, v.CurrentFormantShift() - prev);
        prev     = v.CurrentFormantShift();
    }
    CHECK(max_step < 0.5f); // < half a semitone per 1 ms block
    CHECK_NEAR(v.CurrentFormantShift(), 7.0, 1e-4);
    for(int i = 0; i < Vocoder::kNumBands; i++)
        CHECK_NEAR(v.SynthesisCenterHz(i),
                   Vocoder::ShiftedCenterHz(v.AnalysisCenterHz(i), 7.0f, kSr), 0.01);
    // NaN target is ignored.
    v.SetFormantShift(NAN);
    v.UpdateControl();
    CHECK_NEAR(v.CurrentFormantShift(), 7.0, 1e-4);
}

/** Envelope of band `band` for a tone burst at its centre frequency. */
static void EnvelopeTimes(float attack_ms, float release_ms, double &t_rise_ms, double &t_fall_ms)
{
    Vocoder v;
    v.Init(kSr, kSr / kBlock);
    v.SetAttackMs(attack_ms);
    v.SetReleaseMs(release_ms);
    const int band = 8;
    const float f  = v.AnalysisCenterHz(band);
    const int on   = static_cast<int>(0.5f * kSr), total = static_cast<int>(1.5f * kSr);
    float peak     = 0.0f;
    t_rise_ms = t_fall_ms = -1.0;
    std::vector<float> env(total);
    for(int i = 0; i < total; i++)
    {
        const float x = i < on ? 0.5f * std::sin(dsp::kTwoPi * f * i / kSr) : 0.0f;
        v.Process(x, 0.0f);
        env[i] = v.Envelope(band);
        if(i < on)
            peak = std::fmax(peak, env[i]);
    }
    for(int i = 0; i < on; i++)
        if(env[i] >= 0.63f * peak)
        {
            t_rise_ms = i * 1000.0 / kSr;
            break;
        }
    for(int i = on; i < total; i++)
        if(env[i] <= 0.37f * peak)
        {
            t_fall_ms = (i - on) * 1000.0 / kSr;
            break;
        }
}

TEST(EnvelopeAttackRelease)
{
    double r_fast, f_fast, r_slow, f_slow;
    EnvelopeTimes(2.0f, 50.0f, r_fast, f_fast);
    EnvelopeTimes(40.0f, 500.0f, r_slow, f_slow);
    std::printf("  rise(2ms)=%.1fms rise(40ms)=%.1fms fall(50ms)=%.1fms fall(500ms)=%.1fms\n",
                r_fast, r_slow, f_fast, f_slow);
    CHECK(r_fast > 0 && r_slow > 0 && f_fast > 0 && f_slow > 0);
    CHECK(r_slow > 4.0 * r_fast);
    CHECK(f_slow > 5.0 * f_fast);
    // Release time constant roughly as configured (filter ring-out adds a bit).
    CHECK(f_fast > 35.0 && f_fast < 80.0);
    CHECK(f_slow > 400.0 && f_slow < 650.0);
    // Envelope decays to (flushed) zero after long silence.
    Vocoder v;
    v.Init(kSr, kSr / kBlock);
    for(int i = 0; i < 4800; i++)
        v.Process(0.5f * std::sin(dsp::kTwoPi * 1000.0f * i / kSr), 0.0f);
    for(int i = 0; i < 10 * 48000; i++)
        v.Process(0.0f, 0.0f);
    for(int b = 0; b < Vocoder::kNumBands; b++)
        CHECK(v.Envelope(b) == 0.0f);
}

/*========================================================================*/
// Engine: mixing, limiter, clipping, robustness

static Engine *MakeEngine(Params p)
{
    static Engine e; // large-ish object; reuse storage
    e.Init(kSr, kBlock);
    e.SetParams(p);
    return &e;
}

TEST(DryWetEndpoints)
{
    const size_t n = 48000;
    std::vector<float> mic(n), ext(n);
    for(size_t i = 0; i < n; i++)
    {
        mic[i] = 0.2f * std::sin(dsp::kTwoPi * 300.0f * i / kSr);
        ext[i] = 0.3f * std::sin(dsp::kTwoPi * 110.0f * i / kSr);
    }
    // 0% wet: output == mic * mic_gain * output_level exactly (after smoothing settles).
    Params p      = DefaultParams();
    p.dry_wet     = 0.0f;
    p.mic_gain_db = 0.0f;
    p.output_level = 0.5f;
    p.carrier_mode = CARRIER_EXTERNAL_AUDIO; // carrier present, must not leak
    Engine *e      = MakeEngine(p);
    std::vector<float> out = RunEngine(*e, mic, ext);
    double max_err = 0.0;
    for(size_t i = n / 2; i < n; i++)
        max_err = std::fmax(max_err, std::fabs(out[i] - 0.5f * mic[i]));
    CHECK(max_err < 1e-4);

    // 100% wet, internal synth, no notes: silent (no dry leak).
    p              = DefaultParams();
    p.dry_wet      = 1.0f;
    e              = MakeEngine(p);
    out            = RunEngine(*e, mic, ext);
    CHECK(Rms(&out[n / 2], n / 2) < 1e-6);

    // 100% wet, external carrier present: wet signal audible, finite.
    p.carrier_mode = CARRIER_EXTERNAL_AUDIO;
    e              = MakeEngine(p);
    out            = RunEngine(*e, mic, ext);
    CHECK(Rms(&out[n / 2], n / 2) > 1e-3);

    // 50%: blend of both. External selected but silent -> output is half dry.
    std::vector<float> silent(n, 0.0f);
    p.dry_wet      = 0.5f;
    p.mic_gain_db  = 0.0f;
    p.output_level = 1.0f;
    e              = MakeEngine(p);
    out            = RunEngine(*e, mic, silent);
    max_err        = 0.0;
    for(size_t i = n / 2; i < n; i++)
        max_err = std::fmax(max_err, std::fabs(out[i] - 0.5f * mic[i]));
    CHECK(max_err < 1e-4);
}

TEST(ExternalCarrierNoSignalIsSilentWet)
{
    const size_t n = 48000;
    std::vector<float> mic = MakeVowel(n, 0.1f), silent(n, 0.0f);
    Params p       = DefaultParams();
    p.carrier_mode = CARRIER_EXTERNAL_AUDIO;
    p.dry_wet      = 1.0f;
    Engine *e      = MakeEngine(p);
    e->NoteOn(60); // synth notes must NOT fall back into the carrier
    std::vector<float> out = RunEngine(*e, mic, silent);
    CHECK(Rms(&out[n / 2], n / 2) < 1e-6);
}

TEST(LimiterPeakContainment)
{
    dsp::PeakLimiter lim;
    lim.Init(LIMITER_THRESHOLD, LIMITER_RELEASE_MS, kSr);
    // Transparent below threshold.
    double max_err = 0.0;
    for(int i = 0; i < 48000; i++)
    {
        const float x = 0.8f * std::sin(dsp::kTwoPi * 440.0f * i / kSr);
        max_err = std::fmax(max_err, std::fabs(lim.Process(x) - x));
    }
    CHECK(max_err == 0.0);
    // Huge peaks contained, including a single-sample spike.
    float peak = 0.0f;
    for(int i = 0; i < 48000; i++)
    {
        float x = 4.0f * std::sin(dsp::kTwoPi * 440.0f * i / kSr);
        if(i == 1000)
            x = 1000.0f;
        peak = std::fmax(peak, std::fabs(lim.Process(x)));
    }
    CHECK(peak <= LIMITER_THRESHOLD + 1e-6f);
    // Artifacts: steady 2x-over sine -> per-cycle peaks vary by < 3%.
    lim.Reset();
    const int period = 48000 / 400; // 400 Hz -> 120 samples
    std::vector<float> peaks;
    float cyc = 0.0f;
    double sxy = 0, sxx = 0, syy = 0;
    std::vector<float> xs, ys;
    for(int i = 0; i < 96000; i++)
    {
        const float x = 2.0f * std::sin(dsp::kTwoPi * 400.0f * i / kSr);
        const float y = lim.Process(x);
        if(i > 48000)
        {
            xs.push_back(x);
            ys.push_back(y);
            sxy += x * y;
            sxx += x * x;
        }
        cyc = std::fmax(cyc, std::fabs(y));
        if(i % period == period - 1)
        {
            if(i > 48000)
                peaks.push_back(cyc);
            cyc = 0.0f;
        }
    }
    float pmin = 10.0f, pmax = 0.0f;
    for(float v : peaks)
    {
        pmin = std::fmin(pmin, v);
        pmax = std::fmax(pmax, v);
    }
    // Distortion: residual after removing the best-fit scaled input.
    const double g = sxy / sxx;
    double err = 0;
    for(size_t i = 0; i < xs.size(); i++)
    {
        const double d = ys[i] - g * xs[i];
        err += d * d;
        syy += ys[i] * ys[i];
    }
    const double resid_db = 10.0 * std::log10(err / syy);
    std::printf("  steady-state cycle peaks %.4f..%.4f, residual %.1f dB (6 dB over)\n",
                pmin, pmax, resid_db);
    CHECK(pmax <= LIMITER_THRESHOLD + 1e-6f);
    CHECK((pmax - pmin) / pmax < 0.03f);
    CHECK(resid_db < -30.0); // gain ripple stays small in steady state

    // Whole engine: hot mic at 100% dry with max mic gain stays contained,
    // then Output Level scales after the limiter.
    const size_t n = 24000;
    std::vector<float> mic(n), ext(n, 0.0f);
    for(size_t i = 0; i < n; i++)
        mic[i] = 0.9f * std::sin(dsp::kTwoPi * 200.0f * i / kSr);
    Params p       = DefaultParams();
    p.dry_wet      = 0.0f;
    p.mic_gain_db  = VOCODER_MIC_GAIN_DB_MAX;
    p.output_level = 1.0f;
    Engine *e      = MakeEngine(p);
    std::vector<float> out = RunEngine(*e, mic, ext);
    float opk = 0.0f;
    for(float v : out)
        opk = std::fmax(opk, std::fabs(v));
    CHECK(opk <= LIMITER_THRESHOLD + 1e-6f);
    p.output_level = 0.5f;
    e              = MakeEngine(p);
    out            = RunEngine(*e, mic, ext);
    opk            = 0.0f;
    for(size_t i = n / 2; i < n; i++)
        opk = std::fmax(opk, std::fabs(out[i]));
    CHECK_NEAR(opk, 0.5 * LIMITER_THRESHOLD, 0.01);
}

TEST(ClippingDetection)
{
    const size_t n = 4800;
    std::vector<float> quiet(n, 0.0f), loud(n, 0.0f);
    for(size_t i = 0; i < n; i++)
        quiet[i] = 0.5f * std::sin(dsp::kTwoPi * 300.0f * i / kSr);
    loud = quiet;
    loud[100] = 1.0f;
    Engine *e = MakeEngine(DefaultParams());
    RunEngine(*e, quiet, quiet);
    CHECK(e->ClipEvents() == 0);
    RunEngine(*e, loud, quiet);
    CHECK(e->ClipEvents() == 1);
    RunEngine(*e, quiet, loud); // external carrier input clipping too
    CHECK(e->ClipEvents() == 2);
}

TEST(NanRecovery)
{
    const size_t n = 9600;
    std::vector<float> mic = MakeVowel(n, 0.1f), ext(n, 0.0f);
    mic[2000] = NAN;
    mic[2001] = INFINITY;
    Engine *e = MakeEngine(DefaultParams());
    e->NoteOn(57);
    std::vector<float> out = RunEngine(*e, mic, ext);
    bool all_finite = true;
    for(float v : out)
        all_finite &= std::isfinite(v) != 0;
    CHECK(all_finite);
    CHECK(e->FaultEvents() >= 1);
    // Audio resumes after the fault.
    CHECK(Rms(&out[n - 2400], 2400) > 1e-3);
}

/*========================================================================*/
// Synth

static double MeasureFreq(Synth &s, int samples)
{
    // Count rising zero crossings.
    float prev = s.Process();
    int   crossings = 0, first = -1, last = -1;
    for(int i = 1; i < samples; i++)
    {
        const float y = s.Process();
        if(prev < 0.0f && y >= 0.0f)
        {
            if(first < 0)
                first = i;
            last = i;
            crossings++;
        }
        prev = y;
    }
    return crossings > 1 ? (crossings - 1) * kSr / (last - first) : 0.0;
}

TEST(SynthWaveforms)
{
    for(uint32_t w = 0; w < SYNTH_WAVE_COUNT; w++)
    {
        Synth s;
        s.Init(kSr);
        s.SetWaveform(w);
        s.SetNoiseLevel(0.0f);
        s.SetAdsr(0.5f, 1.0f, 1.0f, 10.0f);
        s.NoteOn(69);
        for(int i = 0; i < 480; i++)
            s.Process();
        float pk = 0.0f;
        double acc = 0.0, mean = 0.0;
        const int n = 48000;
        bool fin = true;
        for(int i = 0; i < n; i++)
        {
            const float y = s.Process();
            fin &= std::isfinite(y) != 0;
            pk = std::fmax(pk, std::fabs(y));
            acc += y * y;
            mean += y;
        }
        CHECK(fin);
        CHECK(pk <= 1.2f);
        CHECK(std::sqrt(acc / n) > 0.2);
        CHECK(std::fabs(mean / n) < 0.02); // no significant DC
        if(w != SYNTH_WAVE_NOISE)
        {
            Synth t = s;
            CHECK_NEAR(MeasureFreq(t, 48000), 440.0, 1.0);
        }
    }
    // Pulse width keeps the pitch and removes DC.
    Synth s;
    s.Init(kSr);
    s.SetWaveform(SYNTH_WAVE_SQUARE);
    s.SetPulseWidth(0.2f);
    s.SetNoiseLevel(0.0f);
    s.SetAdsr(0.5f, 1.0f, 1.0f, 10.0f);
    s.NoteOn(69);
    double mean = 0.0;
    for(int i = 0; i < 48000; i++)
        mean += s.Process();
    CHECK(std::fabs(mean / 48000) < 0.02);
    // Invalid waveform index is ignored.
    s.SetWaveform(99);
    CHECK(std::isfinite(s.Process()));
    // Noise level adds energy to a sine.
    Synth a, b;
    a.Init(kSr);
    b.Init(kSr);
    a.SetWaveform(SYNTH_WAVE_SINE);
    b.SetWaveform(SYNTH_WAVE_SINE);
    a.SetNoiseLevel(0.0f);
    b.SetNoiseLevel(0.5f);
    a.NoteOn(69);
    b.NoteOn(69);
    double ea = 0, eb = 0;
    for(int i = 0; i < 48000; i++)
    {
        const float ya = a.Process(), yb = b.Process();
        ea += ya * ya;
        eb += yb * yb;
    }
    CHECK(eb > ea * 1.1);
}

TEST(SynthAdsr)
{
    Adsr env;
    env.Init(kSr);
    env.SetTimes(10.0f, 100.0f, 0.5f, 200.0f);
    env.Gate(true);
    int t_peak = -1;
    for(int i = 0; i < 48000 && t_peak < 0; i++)
        if(env.Process() >= 1.0f)
            t_peak = i;
    CHECK_NEAR(t_peak, 480, 2); // 10 ms linear attack
    // After decay time, within 1% of the distance to sustain.
    for(int i = 0; i < 4800; i++)
        env.Process();
    CHECK_NEAR(env.Level(), 0.5, 0.5 * 0.011);
    for(int i = 0; i < 48000; i++)
        env.Process();
    CHECK(env.GetStage() == Adsr::SUSTAIN);
    CHECK_NEAR(env.Level(), 0.5, 1e-3);
    env.Gate(false);
    for(int i = 0; i < 9600; i++) // 200 ms release -> -40 dB
        env.Process();
    CHECK_NEAR(env.Level(), 0.005, 0.0005);
    for(int i = 0; i < 48000; i++)
        env.Process();
    CHECK(env.GetStage() == Adsr::IDLE);
    CHECK(env.Level() == 0.0f);

    // Retrigger starts attack from the current level (no drop to zero).
    env.Gate(true);
    for(int i = 0; i < 2000; i++)
        env.Process();
    const float before = env.Level();
    env.Retrigger();
    const float after = env.Process();
    CHECK(after >= before);
}

TEST(NoteStackPriority)
{
    Synth s;
    s.Init(kSr);
    s.SetPolyphonic(false); // mono note-stack behaviour
    s.NoteOn(60);
    CHECK(s.GateOn());
    CHECK_NEAR(s.TargetPitch(), 60, 0);
    s.NoteOn(64);
    CHECK_NEAR(s.TargetPitch(), 64, 0); // last-note priority
    s.NoteOn(67);
    CHECK_NEAR(s.TargetPitch(), 67, 0);
    s.NoteOff(67); // return to most recent still held
    CHECK_NEAR(s.TargetPitch(), 64, 0);
    s.NoteOff(60); // releasing a non-sounding note changes nothing
    CHECK_NEAR(s.TargetPitch(), 64, 0);
    CHECK(s.GateOn());
    for(int i = 0; i < 4800; i++)
        s.Process();
    s.NoteOff(64);
    CHECK(!s.GateOn());
    for(int i = 0; i < 10; i++)
        s.Process();
    CHECK(s.Envelope().GetStage() == Adsr::RELEASE); // release when none held
    // Re-pressing a held note moves it to the top without duplicating it.
    s.NoteOn(60);
    s.NoteOn(62);
    s.NoteOn(60);
    CHECK(s.HeldCount() == 2);
    s.NoteOff(60);
    CHECK_NEAR(s.TargetPitch(), 62, 0);
    // Overflow drops the oldest note but keeps working.
    s.AllNotesOff();
    CHECK(!s.GateOn());
    for(int n = 0; n < SYNTH_NOTE_STACK_SIZE + 4; n++)
        s.NoteOn(static_cast<uint8_t>(40 + n));
    CHECK(s.HeldCount() == SYNTH_NOTE_STACK_SIZE);
    CHECK_NEAR(s.TargetPitch(), 40 + SYNTH_NOTE_STACK_SIZE + 3, 0);
    s.NoteOn(200); // invalid note ignored
    CHECK(s.HeldCount() == SYNTH_NOTE_STACK_SIZE);
}

TEST(LegatoVsRetrigger)
{
    for(int legato = 0; legato < 2; legato++)
    {
        Synth s;
        s.Init(kSr);
        s.SetPolyphonic(false);
        s.SetAdsr(1.0f, 10.0f, 0.5f, 50.0f);
        s.SetLegato(legato != 0);
        s.NoteOn(60);
        for(int i = 0; i < 4800; i++)
            s.Process();
        CHECK(s.Envelope().GetStage() == Adsr::SUSTAIN);
        s.NoteOn(62);
        s.Process();
        if(legato)
            CHECK(s.Envelope().GetStage() == Adsr::SUSTAIN);
        else
            CHECK(s.Envelope().GetStage() == Adsr::ATTACK);
        // Returning to a held note never retriggers.
        for(int i = 0; i < 4800; i++)
            s.Process();
        s.NoteOff(62);
        s.Process();
        CHECK(s.Envelope().GetStage() == Adsr::SUSTAIN);
        CHECK_NEAR(s.CurrentPitch(), 60, 1e-4); // glide off: immediate
    }
}

TEST(Glide)
{
    Synth s;
    s.Init(kSr);
    s.SetPolyphonic(false);
    // Default glide is off.
    CHECK(SYNTH_GLIDE_MS_DEFAULT == 0.0f);
    s.NoteOn(60);
    s.Process();
    s.NoteOn(72);
    CHECK_NEAR(s.CurrentPitch(), 72, 0);
    s.AllNotesOff();

    Synth g;
    g.Init(kSr);
    g.SetPolyphonic(false);
    g.SetGlideMs(100.0f);
    g.NoteOn(60);
    CHECK_NEAR(g.CurrentPitch(), 60, 0); // first note: no glide from silence
    for(int i = 0; i < 480; i++)
        g.Process();
    g.NoteOn(72);
    for(int i = 0; i < 2400; i++) // 50 ms
        g.Process();
    CHECK_NEAR(g.CurrentPitch(), 66, 0.05);
    for(int i = 0; i < 2400; i++)
        g.Process();
    CHECK_NEAR(g.CurrentPitch(), 72, 1e-4);
    // Glide is clamped to its maximum.
    g.SetGlideMs(1e9f);
    g.NoteOn(48);
    for(int i = 0; i < static_cast<int>(SYNTH_GLIDE_MS_MAX * 48.0f) + 10; i++)
        g.Process();
    CHECK_NEAR(g.CurrentPitch(), 48, 1e-3);
}

static std::vector<int> VoiceNotes(const Synth &s)
{
    std::vector<int> n;
    for(int v = 0; v < Synth::kNumVoices; v++)
        if(s.GetVoice(v).env.GateOn())
            n.push_back(s.GetVoice(v).note);
    std::sort(n.begin(), n.end());
    return n;
}

TEST(PolyVoiceAllocation)
{
    CHECK(Synth::kNumVoices == 4);
    CHECK(DefaultParams().poly == 1);
    Synth s;
    s.Init(kSr);
    CHECK(s.Polyphonic());

    // Four-note chord: one voice per note at the right pitch.
    for(uint8_t n : {60, 64, 67, 72})
        s.NoteOn(n);
    CHECK(s.GatedVoices() == 4);
    CHECK((VoiceNotes(s) == std::vector<int>{60, 64, 67, 72}));
    for(int v = 0; v < Synth::kNumVoices; v++)
        CHECK_NEAR(s.GetVoice(v).target_pitch, s.GetVoice(v).note, 0);
    for(int i = 0; i < 4800; i++)
        s.Process();

    // Fifth note steals the oldest held note (60).
    s.NoteOn(76);
    CHECK(s.GatedVoices() == 4);
    CHECK((VoiceNotes(s) == std::vector<int>{64, 67, 72, 76}));

    // Releasing 64 gives its voice back to the still-held, stolen 60,
    // without retriggering the envelope.
    for(int i = 0; i < 4800; i++)
        s.Process();
    s.NoteOff(64);
    CHECK((VoiceNotes(s) == std::vector<int>{60, 67, 72, 76}));
    for(int v = 0; v < Synth::kNumVoices; v++)
        if(s.GetVoice(v).note == 60)
            CHECK(s.GetVoice(v).env.GetStage() != Adsr::ATTACK);

    // Releasing a note with nothing waiting releases only that voice.
    s.NoteOff(67);
    CHECK(s.GatedVoices() == 3);
    CHECK((VoiceNotes(s) == std::vector<int>{60, 72, 76}));

    // Repeated Note On of a gated note does not take a second voice.
    s.NoteOn(72);
    CHECK(s.GatedVoices() == 3);

    // A note re-pressed while its voice is still releasing reuses that voice.
    s.NoteOn(67);
    CHECK(s.GatedVoices() == 4);
    int voices_on_67 = 0;
    for(int v = 0; v < Synth::kNumVoices; v++)
        voices_on_67 += s.GetVoice(v).note == 67 && s.GetVoice(v).Sounding();
    CHECK(voices_on_67 == 1);

    // All released -> every voice decays to idle.
    for(uint8_t n : {60, 67, 72, 76})
        s.NoteOff(n);
    CHECK(s.GatedVoices() == 0 && !s.GateOn());
    for(int i = 0; i < 48000; i++)
        s.Process();
    for(int v = 0; v < Synth::kNumVoices; v++)
        CHECK(!s.GetVoice(v).Sounding());

    // More keys than the note stack holds: no stuck voices afterwards.
    for(int n = 30; n < 30 + SYNTH_NOTE_STACK_SIZE + 6; n++)
        s.NoteOn(static_cast<uint8_t>(n));
    CHECK(s.GatedVoices() == 4);
    for(int n = 30; n < 30 + SYNTH_NOTE_STACK_SIZE + 6; n++)
        s.NoteOff(static_cast<uint8_t>(n));
    CHECK(s.GatedVoices() == 0);

    // Pitch bend moves every voice.
    s.NoteOn(57);
    s.NoteOn(69);
    s.SetPitchBend(2.0f);
    for(int v = 0; v < Synth::kNumVoices; v++)
        if(s.GetVoice(v).env.GateOn())
            CHECK_NEAR(s.GetVoice(v).FrequencyHz(2.0f),
                       440.0 * std::pow(2.0, (s.GetVoice(v).note + 2.0 - 69.0) / 12.0), 0.01);

    // Switching mode releases everything (clean hand-over).
    s.SetPolyphonic(false);
    CHECK(s.GatedVoices() == 0 && !s.Polyphonic());
}

TEST(PolyLevelNormalisation)
{
    auto rms_of = [](bool poly, std::vector<uint8_t> notes) {
        Synth s;
        s.Init(kSr);
        s.SetPolyphonic(poly);
        s.SetNoiseLevel(0.0f);
        for(uint8_t n : notes)
            s.NoteOn(n);
        for(int i = 0; i < 9600; i++)
            s.Process();
        double acc = 0;
        float  pk  = 0;
        for(int i = 0; i < 48000; i++)
        {
            const float y = s.Process();
            acc += y * y;
            pk = std::fmax(pk, std::fabs(y));
        }
        return std::make_pair(std::sqrt(acc / 48000), pk);
    };
    const auto mono  = rms_of(false, {60});
    const auto one   = rms_of(true, {60});
    const auto chord = rms_of(true, {60, 64, 67, 71});
    std::printf("  rms mono %.1f dB, poly 1 note %.1f dB, poly 4 notes %.1f dB (peak %.2f)\n",
                Db(mono.first), Db(one.first), Db(chord.first), chord.second);
    CHECK_NEAR(Db(one.first), Db(mono.first), 0.01); // single note identical
    CHECK(std::fabs(Db(chord.first) - Db(one.first)) < 2.0);
    CHECK(chord.second < 2.0f);

    // Through the vocoder: a chord is audible, finite and not much louder.
    const size_t n = 96000;
    std::vector<float> mic = MakeVowel(n, 0.1f), ext(n, 0.0f);
    Engine *e = MakeEngine(DefaultParams());
    e->NoteOn(48);
    const double single = Rms(&RunEngine(*e, mic, ext)[n / 2], n / 2);
    e = MakeEngine(DefaultParams());
    for(uint8_t k : {48, 52, 55, 59})
        e->NoteOn(k);
    std::vector<float> out = RunEngine(*e, mic, ext);
    bool fin = true;
    for(float v : out)
        fin &= std::isfinite(v) != 0;
    const double chord_rms = Rms(&out[n / 2], n / 2);
    std::printf("  vocoder: single %.1f dBFS, 4-note chord %.1f dBFS\n", Db(single), Db(chord_rms));
    CHECK(fin);
    CHECK(Db(chord_rms) > -30.0);
    CHECK(std::fabs(Db(chord_rms) - Db(single)) < 3.0);
}

TEST(PitchBend)
{
    CHECK(MIDI_PITCH_BEND_RANGE_ST == 2.0f);
    CHECK_NEAR(MidiMapper::PitchBendToSemitones(0x00, 0x40), 0.0, 1e-6);
    CHECK_NEAR(MidiMapper::PitchBendToSemitones(0x7F, 0x7F), 2.0, 1e-6);
    CHECK_NEAR(MidiMapper::PitchBendToSemitones(0x00, 0x00), -2.0, 1e-6);
    Synth s;
    s.Init(kSr);
    s.NoteOn(69);
    s.SetPitchBend(2.0f);
    s.Process();
    CHECK_NEAR(s.FrequencyHz(), 440.0 * std::pow(2.0, 2.0 / 12.0), 0.01);
    s.SetPitchBend(-2.0f);
    CHECK_NEAR(s.FrequencyHz(), 440.0 * std::pow(2.0, -2.0 / 12.0), 0.01);
    s.SetPitchBend(NAN); // ignored
    CHECK_NEAR(s.FrequencyHz(), 440.0 * std::pow(2.0, -2.0 / 12.0), 0.01);
}

/*========================================================================*/
// MIDI mapping

TEST(MidiChannelAndNotes)
{
    MidiAction a = MidiMapper::Map(Msg(MidiMessage::NOTE_ON, 60, 100));
    CHECK(a.kind == MidiAction::NOTE_ON && a.note == 60);
    a = MidiMapper::Map(Msg(MidiMessage::NOTE_ON, 60, 0)); // vel 0 == off
    CHECK(a.kind == MidiAction::NOTE_OFF);
    a = MidiMapper::Map(Msg(MidiMessage::NOTE_OFF, 60, 0));
    CHECK(a.kind == MidiAction::NOTE_OFF);
#if MIDI_CHANNEL != 0
    a = MidiMapper::Map(Msg(MidiMessage::NOTE_ON, 60, 100, (MIDI_CHANNEL) % 16));
    CHECK(a.kind == MidiAction::NONE); // other channel ignored
#endif
    CHECK(MIDI_CHANNEL == 1);
    a = MidiMapper::Map(Msg(MidiMessage::CHANNEL_MODE, 123, 0));
    CHECK(a.kind == MidiAction::ALL_NOTES_OFF);
    a = MidiMapper::Map(Msg(MidiMessage::OTHER, 1, 1));
    CHECK(a.kind == MidiAction::NONE);
}

static std::vector<MidiMessage> ParseAll(const std::vector<uint8_t> &bytes)
{
    MidiByteParser p;
    std::vector<MidiMessage> out;
    MidiMessage m;
    for(uint8_t b : bytes)
        if(p.Parse(b, m))
            out.push_back(m);
    return out;
}

TEST(MidiParserRunningStatusAndRealtime)
{
    // The exact stream that broke libDaisy's parser: Active Sensing between
    // running-status notes (Note Off sent as Note On velocity 0).
    std::vector<MidiMessage> m = ParseAll(
        {0x90, 60, 100, 0xFE, 62, 100, 60, 0, 0xFE, 62, 0, 64, 100});
    CHECK(m.size() == 5);
    for(const MidiMessage &x : m)
        CHECK(x.channel == 0 && x.type == MidiMessage::NOTE_ON);
    if(m.size() == 5)
    {
        CHECK(m[1].data0 == 62 && m[1].data1 == 100);
        CHECK(m[2].data0 == 60 && m[2].data1 == 0);
        CHECK(m[4].data0 == 64 && m[4].data1 == 100);
    }
    // Real-time bytes in the middle of a message (allowed by the spec).
    m = ParseAll({0x91, 0xF8, 67, 0xF8, 90, 0xFE});
    CHECK(m.size() == 1 && m[0].channel == 1 && m[0].data0 == 67 && m[0].data1 == 90);
    // SysEx and System Common cancel running status; their data is skipped.
    m = ParseAll({0x90, 60, 100, 0xF0, 1, 2, 3, 0xF7, 61, 100, 0xF2, 5, 6, 0x80, 61, 0});
    CHECK(m.size() == 2 && m[0].data0 == 60 && m[1].type == MidiMessage::NOTE_OFF);
    // CC, Channel Mode, Pitch Bend, Program Change (1 data byte).
    m = ParseAll({0xB0, 17, 64, 123, 0, 0xE0, 0x00, 0x40, 0xC0, 5, 0xB0, 18, 1});
    CHECK(m.size() == 5);
    if(m.size() == 5)
    {
        CHECK(m[0].type == MidiMessage::CONTROL_CHANGE && m[0].data0 == 17);
        CHECK(m[1].type == MidiMessage::CHANNEL_MODE && m[1].data0 == 123);
        CHECK(m[2].type == MidiMessage::PITCH_BEND && m[2].data1 == 0x40);
        CHECK(m[3].type == MidiMessage::OTHER);
        CHECK(m[4].type == MidiMessage::CONTROL_CHANGE && m[4].data0 == 18);
    }
    // Leading data bytes without status are ignored.
    CHECK(ParseAll({60, 100, 0x90, 60, 100}).size() == 1);

    // End to end: a played melody with Active Sensing reaches the synth.
    FakeBackend be;
    static ControlBridge br;
    static Engine eng;
    eng.Init(kSr, kBlock);
    Controller ctl;
    ctl.Init(be, br);
    MidiByteParser p;
    const uint8_t melody[] = {0x90, 60, 100, 0xFE, 60, 0, 62, 100, 0xFE, 0xF8,
                              62, 0, 64, 100, 0xFE, 64, 0, 65, 100};
    const int expect[] = {60, 62, 64, 65};
    int idx = 0;
    for(uint8_t b : melody)
    {
        MidiMessage msg;
        if(p.Parse(b, msg))
        {
            ctl.HandleMidi(msg, 0);
            ApplyBridgeToEngine(br, eng);
            if(msg.data1 > 0)
                CHECK_NEAR(eng.GetSynth().TargetPitch(), expect[idx++], 0);
        }
    }
    CHECK(idx == 4);
}

TEST(MidiCcMappingAndClamping)
{
    // Every mapped CC hits its parameter; endpoints land on the limits.
    struct Case
    {
        int     cc;
        ParamId id;
        float   at0, at127;
    } cases[] = {
        {MIDI_CC_CARRIER_MODE, PARAM_CARRIER_MODE, 0, 1},
        {MIDI_CC_CARRIER_LEVEL, PARAM_CARRIER_LEVEL, 0, VOCODER_CARRIER_LEVEL_MAX},
        {MIDI_CC_MIC_GAIN, PARAM_MIC_GAIN_DB, VOCODER_MIC_GAIN_DB_MIN, VOCODER_MIC_GAIN_DB_MAX},
        {MIDI_CC_DRY_WET, PARAM_DRY_WET, 0, 1},
        {MIDI_CC_OUTPUT_LEVEL, PARAM_OUTPUT_LEVEL, 0, VOCODER_OUTPUT_LEVEL_MAX},
        {MIDI_CC_VOC_ATTACK, PARAM_VOC_ATTACK_MS, VOCODER_ATTACK_MS_MIN, VOCODER_ATTACK_MS_MAX},
        {MIDI_CC_VOC_RELEASE, PARAM_VOC_RELEASE_MS, VOCODER_RELEASE_MS_MIN, VOCODER_RELEASE_MS_MAX},
        {MIDI_CC_ENV_AMOUNT, PARAM_ENV_AMOUNT, 0, 1},
        {MIDI_CC_FORMANT_SHIFT, PARAM_FORMANT_SHIFT_ST, VOCODER_FORMANT_SHIFT_MIN, VOCODER_FORMANT_SHIFT_MAX},
        {MIDI_CC_FILTER_Q, PARAM_FILTER_Q, VOCODER_FILTER_Q_MIN, VOCODER_FILTER_Q_MAX},
        {MIDI_CC_WAVEFORM, PARAM_WAVEFORM, 0, SYNTH_WAVE_COUNT - 1},
        {MIDI_CC_NOISE_LEVEL, PARAM_NOISE_LEVEL, 0, SYNTH_NOISE_LEVEL_MAX},
        {MIDI_CC_PULSE_WIDTH, PARAM_PULSE_WIDTH, SYNTH_PULSE_WIDTH_MIN, SYNTH_PULSE_WIDTH_MAX},
        {MIDI_CC_SYNTH_ATTACK, PARAM_SYNTH_ATTACK_MS, SYNTH_ATTACK_MS_MIN, SYNTH_ATTACK_MS_MAX},
        {MIDI_CC_SYNTH_DECAY, PARAM_SYNTH_DECAY_MS, SYNTH_DECAY_MS_MIN, SYNTH_DECAY_MS_MAX},
        {MIDI_CC_SYNTH_SUSTAIN, PARAM_SYNTH_SUSTAIN, 0, 1},
        {MIDI_CC_SYNTH_RELEASE, PARAM_SYNTH_RELEASE_MS, SYNTH_RELEASE_MS_MIN, SYNTH_RELEASE_MS_MAX},
        {MIDI_CC_GLIDE, PARAM_GLIDE_MS, 0, SYNTH_GLIDE_MS_MAX},
        {MIDI_CC_LEGATO, PARAM_LEGATO, 0, 1},
        {MIDI_CC_POLY, PARAM_POLY, 0, 1},
    };
    CHECK(sizeof(cases) / sizeof(cases[0]) == PARAM_COUNT);
    for(const Case &c : cases)
    {
        MidiAction a = MidiMapper::Map(Msg(MidiMessage::CONTROL_CHANGE, (uint8_t)c.cc, 0));
        CHECK(a.kind == MidiAction::SET_PARAM && a.param == c.id);
        CHECK_NEAR(a.value, c.at0, 1e-3 * (1 + std::fabs(c.at0)));
        a = MidiMapper::Map(Msg(MidiMessage::CONTROL_CHANGE, (uint8_t)c.cc, 127));
        CHECK_NEAR(a.value, c.at127, 1e-3 * (1 + std::fabs(c.at127)));
        // Out-of-range 7-bit values are clamped, never exceed limits.
        a = MidiMapper::Map(Msg(MidiMessage::CONTROL_CHANGE, (uint8_t)c.cc, 255));
        CHECK_NEAR(a.value, c.at127, 1e-3 * (1 + std::fabs(c.at127)));
    }
    CHECK_NEAR(MidiMapper::CcValueToParam(PARAM_FORMANT_SHIFT_ST, 64), 0.0, 0);
    CHECK_NEAR(MidiMapper::CcValueToParam(PARAM_CARRIER_MODE, 63), CARRIER_INTERNAL_SYNTH, 0);
    CHECK_NEAR(MidiMapper::CcValueToParam(PARAM_CARRIER_MODE, 64), CARRIER_EXTERNAL_AUDIO, 0);
    // All five waveform zones reachable, monotonic.
    int seen[SYNTH_WAVE_COUNT] = {0};
    for(int v = 0; v < 128; v++)
        seen[(int)MidiMapper::CcValueToParam(PARAM_WAVEFORM, (uint8_t)v)]++;
    for(int w = 0; w < SYNTH_WAVE_COUNT; w++)
        CHECK(seen[w] > 20);
    // Unmapped CC and INIT CC.
    CHECK(MidiMapper::Map(Msg(MidiMessage::CONTROL_CHANGE, 1, 64)).kind == MidiAction::NONE);
    MidiAction a = MidiMapper::Map(Msg(MidiMessage::CONTROL_CHANGE, MIDI_CC_INIT, 127));
    CHECK(a.kind == MidiAction::INIT_BUTTON && a.pressed);
    a = MidiMapper::Map(Msg(MidiMessage::CONTROL_CHANGE, MIDI_CC_INIT, 0));
    CHECK(a.kind == MidiAction::INIT_BUTTON && !a.pressed);

    // Params clamping directly.
    Params p = DefaultParams();
    SetParam(p, PARAM_FILTER_Q, 1e6f);
    CHECK(p.filter_q == VOCODER_FILTER_Q_MAX);
    SetParam(p, PARAM_DRY_WET, NAN);
    CHECK(p.dry_wet == VOCODER_DRY_WET_DEFAULT);
    SetParam(p, PARAM_WAVEFORM, 17.0f);
    CHECK(p.waveform == SYNTH_WAVEFORM_DEFAULT);
    SetParam(p, PARAM_CARRIER_MODE, -1.0f);
    CHECK(p.carrier_mode == CARRIER_INTERNAL_SYNTH);
}

/*========================================================================*/
// Settings

TEST(SettingsDefaults)
{
    const Params d = DefaultParams();
    CHECK(d.carrier_mode == CARRIER_INTERNAL_SYNTH);
    CHECK(d.glide_ms == 0.0f);
    Params c = d;
    CHECK(!SanitizeParams(c)); // defaults already within limits
    CHECK(d.output_level > 0.0f && d.carrier_level > 0.0f && d.dry_wet > 0.0f);
}

TEST(SettingsPersistenceAndValidation)
{
    // Empty -> defaults.
    FakeBackend be;
    SettingsManager sm;
    CHECK(sm.Init(be) == SettingsManager::LoadResult::DEFAULTS_EMPTY);
    CHECK(ParamsEqual(sm.Get(), DefaultParams()));

    // Valid saved record (external carrier) is loaded, not overridden.
    Params saved       = DefaultParams();
    saved.carrier_mode = CARRIER_EXTERNAL_AUDIO;
    saved.filter_q     = 9.0f;
    be.data            = MakeStoredSettings(saved);
    be.has_data        = true;
    CHECK(sm.Init(be) == SettingsManager::LoadResult::LOADED);
    CHECK(sm.Get().carrier_mode == CARRIER_EXTERNAL_AUDIO);
    CHECK(sm.Get().filter_q == 9.0f);

    // Corruptions -> defaults.
    StoredSettings bad = MakeStoredSettings(saved);
    bad.checksum ^= 1;
    be.data = bad;
    CHECK(sm.Init(be) == SettingsManager::LoadResult::DEFAULTS_INVALID);
    CHECK(sm.Get().carrier_mode == CARRIER_INTERNAL_SYNTH);

    bad         = MakeStoredSettings(saved);
    bad.version = kSettingsVersion + 1;
    bad.checksum = 0;
    be.data     = bad;
    CHECK(sm.Init(be) == SettingsManager::LoadResult::DEFAULTS_INVALID);

    bad       = MakeStoredSettings(saved);
    bad.magic = 0xFFFFFFFFu;
    be.data   = bad;
    CHECK(sm.Init(be) == SettingsManager::LoadResult::DEFAULTS_INVALID);

    std::memset(&bad, 0xFF, sizeof(bad)); // erased flash
    be.data = bad;
    CHECK(sm.Init(be) == SettingsManager::LoadResult::DEFAULTS_INVALID);
    CHECK(ParamsEqual(sm.Get(), DefaultParams()));

    // Checksum-valid but out-of-range values are clamped on load.
    Params wild       = DefaultParams();
    wild.filter_q     = 500.0f;
    wild.dry_wet      = NAN;
    wild.waveform     = 42;
    be.data           = MakeStoredSettings(wild);
    CHECK(sm.Init(be) == SettingsManager::LoadResult::LOADED);
    CHECK(sm.Get().filter_q == VOCODER_FILTER_Q_MAX);
    CHECK(sm.Get().dry_wet == VOCODER_DRY_WET_DEFAULT);
    CHECK(sm.Get().waveform == SYNTH_WAVEFORM_DEFAULT);

    // Round trip of saved data.
    StoredSettings s = MakeStoredSettings(saved);
    Params out;
    CHECK(ValidateStoredSettings(s, out) && ParamsEqual(out, saved));
}

TEST(SaveDebounce)
{
    FakeBackend be;
    SettingsManager sm;
    sm.Init(be);
    uint32_t t = 1000;
    // A knob sweep: 100 CC updates, 10 ms apart -> no write during the sweep.
    for(int i = 0; i < 100; i++, t += 10)
    {
        sm.SetParam(PARAM_DRY_WET, i / 100.0f, t);
        sm.Update(t);
    }
    CHECK(be.writes == 0);
    CHECK(sm.SavePending());
    sm.Update(t + SETTINGS_SAVE_DEBOUNCE_MS - 20);
    CHECK(be.writes == 0);
    sm.Update(t + SETTINGS_SAVE_DEBOUNCE_MS);
    CHECK(be.writes == 1);
    CHECK(!sm.SavePending());
    sm.Update(t + 10 * SETTINGS_SAVE_DEBOUNCE_MS);
    CHECK(be.writes == 1); // nothing new -> no write
    // Setting an identical value is not a change.
    sm.SetParam(PARAM_DRY_WET, sm.Get().dry_wet, t);
    CHECK(!sm.SavePending());
    // Persisted data matches.
    Params loaded;
    CHECK(ValidateStoredSettings(be.data, loaded) && ParamsEqual(loaded, sm.Get()));
    // Timer wraparound safe.
    sm.SetParam(PARAM_DRY_WET, 0.25f, 0xFFFFFF00u);
    sm.Update(0xFFFFFF00u + 100);
    CHECK(be.writes == 1);
    sm.Update(0xFFFFFF00u + SETTINGS_SAVE_DEBOUNCE_MS);
    CHECK(be.writes == 2);
}

TEST(InitHold)
{
    CHECK(MIDI_INIT_HOLD_MS == 5000);
    FakeBackend be;
    Params saved       = DefaultParams();
    saved.carrier_mode = CARRIER_EXTERNAL_AUDIO;
    saved.dry_wet      = 0.3f;
    be.data            = MakeStoredSettings(saved);
    be.has_data        = true;
    SettingsManager sm;
    sm.Init(be);
    CHECK(sm.Get().carrier_mode == CARRIER_EXTERNAL_AUDIO);

    // Short press: nothing.
    sm.OnInitButton(true, 0);
    for(uint32_t t = 0; t < MIDI_INIT_HOLD_MS - 1; t += 50)
        CHECK(!sm.Update(t));
    sm.OnInitButton(false, MIDI_INIT_HOLD_MS - 1);
    CHECK(!sm.Update(MIDI_INIT_HOLD_MS + 100));
    CHECK(sm.InitCount() == 0);
    CHECK(sm.Get().carrier_mode == CARRIER_EXTERNAL_AUDIO);

    // Repeated "pressed" messages do not restart the timer.
    const uint32_t t0 = 20000;
    sm.OnInitButton(true, t0);
    sm.OnInitButton(true, t0 + 3000);
    CHECK(!sm.Update(t0 + MIDI_INIT_HOLD_MS - 1));
    CHECK(sm.Update(t0 + MIDI_INIT_HOLD_MS));
    CHECK(sm.InitCount() == 1);
    CHECK(ParamsEqual(sm.Get(), DefaultParams()));
    CHECK(sm.Get().carrier_mode == CARRIER_INTERNAL_SYNTH);
    CHECK(be.writes == 1); // saved immediately
    Params stored;
    CHECK(ValidateStoredSettings(be.data, stored) && ParamsEqual(stored, DefaultParams()));
    // Keeping it held does not fire again.
    CHECK(!sm.Update(t0 + 3 * MIDI_INIT_HOLD_MS));
    CHECK(sm.InitCount() == 1);
    sm.OnInitButton(false, t0 + 3 * MIDI_INIT_HOLD_MS);
}

/*========================================================================*/
// Bridge

TEST(BridgeQueues)
{
    SpscQueue<int, 4> q;
    int v = 0;
    CHECK(!q.Pop(v));
    for(int i = 0; i < 4; i++)
        CHECK(q.Push(i));
    CHECK(!q.Push(99)); // full
    for(int i = 0; i < 4; i++)
        CHECK(q.Pop(v) && v == i);
    CHECK(!q.Pop(v));

    ParamMailbox mb;
    Params a = DefaultParams(), b = DefaultParams(), out;
    b.dry_wet = 0.1f;
    CHECK(mb.TryPublish(a));
    CHECK(!mb.TryPublish(b)); // previous not yet consumed
    CHECK(mb.TryConsume(out) && ParamsEqual(out, a));
    CHECK(!mb.TryConsume(out));
    CHECK(mb.TryPublish(b));
    CHECK(mb.TryConsume(out) && out.dry_wet == 0.1f);
}

/*========================================================================*/
// Level staging (informational + broad bounds)

TEST(DefaultLevelsAreAudible)
{
    const size_t n = 96000;
    std::vector<float> mic = MakeVowel(n, 0.1f), ext(n, 0.0f); // -20 dBFS RMS
    Engine *e = MakeEngine(DefaultParams());
    e->NoteOn(57);
    std::vector<float> out = RunEngine(*e, mic, ext);
    const double rms = Rms(&out[n / 2], n / 2);
    float pk = 0.0f;
    for(size_t i = n / 2; i < n; i++)
        pk = std::fmax(pk, std::fabs(out[i]));
    std::printf("  mic -20.0 dBFS RMS -> output %.1f dBFS RMS, peak %.1f dBFS\n", Db(rms), Db(pk));
    CHECK(Db(rms) > -30.0 && Db(rms) < -3.0);

    // Quieter (-35 dBFS) voice still produces clearly audible output.
    std::vector<float> quiet = MakeVowel(n, 0.0178f);
    e = MakeEngine(DefaultParams());
    e->NoteOn(57);
    out = RunEngine(*e, quiet, ext);
    const double rq = Rms(&out[n / 2], n / 2);
    std::printf("  mic -35.0 dBFS RMS -> output %.1f dBFS RMS\n", Db(rq));
    CHECK(Db(rq) > -45.0);

    // Q extremes keep loudness within a reasonable window of the default.
    for(float q : {VOCODER_FILTER_Q_MIN, VOCODER_FILTER_Q_MAX})
    {
        Params p   = DefaultParams();
        p.filter_q = q;
        e          = MakeEngine(p);
        e->NoteOn(57);
        out              = RunEngine(*e, mic, ext);
        const double rqq = Rms(&out[n / 2], n / 2);
        std::printf("  Q=%.1f -> output %.1f dBFS RMS\n", q, Db(rqq));
        CHECK(std::fabs(Db(rqq) - Db(rms)) < 12.0);
    }
}

/*========================================================================*/
// Intelligibility proxies: does the output carry the voice's spectral
// envelope (formants, consonants) rather than the carrier's own tilt?

/** Per-band energy (dB) of x, measured with an independent filter bank. */
static std::vector<double> BandEnergiesDb(const std::vector<float> &x, size_t from)
{
    std::vector<double> e(VOCODER_NUM_BANDS);
    for(int b = 0; b < VOCODER_NUM_BANDS; b++)
    {
        dsp::Biquad f;
        f.SetCoefs(dsp::BandpassCoefs(
            Vocoder::BandCenterHz(b, VOCODER_NUM_BANDS, VOCODER_MIN_FREQ_HZ, VOCODER_MAX_FREQ_HZ),
            VOCODER_FILTER_Q_DEFAULT, kSr));
        double acc = 0;
        for(size_t i = 0; i < x.size(); i++)
        {
            const float y = f.Process(x[i]);
            if(i >= from)
                acc += y * y;
        }
        e[b] = 10.0 * std::log10(acc / (x.size() - from) + 1e-20);
    }
    return e;
}

/** RMS (dB) deviation between output and modulator band profiles, after
 *  removing the overall level difference, over bands within 40 dB of the
 *  modulator's loudest band. 0 dB = perfect spectral-envelope transfer. */
static double EnvelopeMismatchDb(const std::vector<double> &mod, const std::vector<double> &out)
{
    double mx = -1e9;
    for(double v : mod)
        mx = std::fmax(mx, v);
    std::vector<double> d;
    for(size_t b = 0; b < mod.size(); b++)
        if(mod[b] > mx - 40.0)
            d.push_back(out[b] - mod[b]);
    double mean = 0;
    for(double v : d)
        mean += v;
    mean /= d.size();
    double var = 0;
    for(double v : d)
        var += (v - mean) * (v - mean);
    return std::sqrt(var / d.size());
}

TEST(SpectralEnvelopeTransfer)
{
    const size_t n = 96000;
    std::vector<float> ext(n, 0.0f);
    const uint32_t waves[] = {SYNTH_WAVE_SAW, SYNTH_WAVE_SQUARE};
    for(uint32_t w : waves)
    {
        // Vowel: formants 700 / 1200 / 2600 Hz.
        std::vector<float> mic = MakeVowel(n, 0.1f);
        Params p   = DefaultParams();
        p.waveform = w;
        Engine *e  = MakeEngine(p);
        e->NoteOn(48);
        std::vector<float> out = RunEngine(*e, mic, ext);
        const double mm = EnvelopeMismatchDb(BandEnergiesDb(mic, n / 2), BandEnergiesDb(out, n / 2));
        std::printf("  wave %u vowel: envelope mismatch %.1f dB\n", w, mm);
        CHECK(mm < (w == SYNTH_WAVE_SAW ? 4.0 : 6.5));
    }

    // Consonant "s": white noise high-passed around 5 kHz. The output must
    // put its energy in the high bands, not the low ones.
    std::vector<float> s(n);
    uint32_t rng = 1;
    dsp::Biquad hp1, hp2;
    hp1.SetCoefs(dsp::BandpassCoefs(6000.0f, 1.0f, kSr));
    hp2.SetCoefs(dsp::BandpassCoefs(6000.0f, 1.0f, kSr));
    for(size_t i = 0; i < n; i++)
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        s[i] = hp2.Process(hp1.Process(static_cast<int32_t>(rng) * (0.1f / 2147483648.0f)));
    }
    Engine *e = MakeEngine(DefaultParams());
    e->NoteOn(48);
    std::vector<float> out = RunEngine(*e, s, ext);
    std::vector<double> eb = BandEnergiesDb(out, n / 2);
    double lo = -1e9, hi = -1e9;
    for(int b = 0; b < VOCODER_NUM_BANDS; b++)
    {
        const float f = Vocoder::BandCenterHz(b, VOCODER_NUM_BANDS, VOCODER_MIN_FREQ_HZ, VOCODER_MAX_FREQ_HZ);
        if(f < 1000.0f)
            lo = std::fmax(lo, eb[b]);
        if(f > 3000.0f)
            hi = std::fmax(hi, eb[b]);
    }
    const double s_rms = Db(Rms(&out[n / 2], n / 2));
    std::printf("  's' consonant: high-vs-low band %.1f dB, output %.1f dBFS RMS\n", hi - lo, s_rms);
    CHECK(hi - lo > 15.0);
    CHECK(s_rms > -35.0); // consonants must be clearly audible
}

/*========================================================================*/
// Acceptance: erased settings + Note On/Off only (no CC) -> vocoder sound

TEST(AcceptanceNoteOnlyFromErasedSettings)
{
    // 1. Erased flash (all 0xFF) -> invalid -> compile-time defaults.
    FakeBackend be;
    std::memset(&be.data, 0xFF, sizeof(be.data));
    be.has_data = true;
    static ControlBridge bridge;
    static Engine        engine;
    Controller           ctl;
    engine.Init(kSr, kBlock);
    CHECK(ctl.Init(be, bridge) == SettingsManager::LoadResult::DEFAULTS_INVALID);
    CHECK(ctl.Settings().Get().carrier_mode == CARRIER_INTERNAL_SYNTH);

    // Timeline (n = 1 s): [0, .5) mic only | [.5, 2) Note On held |
    // [2, 3) mic muted, note held | [3, 3.5) mic back | [3.5, 4.5) Note Off.
    const size_t n = 48000;
    std::vector<float> mic = MakeVowel(9 * n / 2, 0.1f), ext(9 * n / 2, 0.0f);
    std::fill(mic.begin() + 2 * n, mic.begin() + 3 * n, 0.0f);
    std::vector<float> l(mic.size()), r(mic.size());
    uint32_t now = 0;

    auto run = [&](size_t from, size_t to) {
        for(size_t i = from; i + kBlock <= to; i += kBlock)
        {
            ctl.Update(now);
            ApplyBridgeToEngine(bridge, engine);
            engine.Process(&mic[i], &ext[i], &l[i], &r[i], kBlock);
            now++;
        }
    };

    // 2. Mic only, no notes: wet path silent (default 100% wet).
    run(0, n / 2);
    CHECK(Rms(&l[n / 4], n / 4) < 1e-6);

    // 3. Only a Note On (no CC ever sent).
    CHECK(ctl.HandleMidi(Msg(MidiMessage::NOTE_ON, 60, 100), now));
    run(n / 2, 2 * n);
    CHECK(engine.CurrentParams().carrier_mode == CARRIER_INTERNAL_SYNTH);
    const double on_rms = Rms(&l[n], n);
    bool finite = true;
    for(size_t i = 0; i < 2 * n; i++)
        finite &= std::isfinite(l[i]) && l[i] == r[i];
    std::printf("  note held: output %.1f dBFS RMS\n", Db(on_rms));
    CHECK(finite);
    CHECK(Db(on_rms) > -30.0);
    // Output follows the voice: muting the mic silences it (it is the
    // vocoder, not the raw synth, that is audible).
    run(2 * n, 3 * n);
    CHECK(Db(Rms(&l[3 * n - n / 4], n / 4)) < Db(on_rms) - 40.0);
    run(3 * n, 7 * n / 2);
    CHECK(Db(Rms(&l[7 * n / 2 - n / 4], n / 4)) > -30.0);

    // 4. Note Off -> envelope releases -> silence.
    CHECK(ctl.HandleMidi(Msg(MidiMessage::NOTE_OFF, 60, 0), now));
    run(7 * n / 2, 9 * n / 2);
    CHECK(Rms(&l[9 * n / 2 - n / 8], n / 8) < 1e-4);
    CHECK(be.writes == 0); // nothing was changed, nothing written
}

/*========================================================================*/
int main()
{
    RUN(BandLayout);
    RUN(FilterCoefficientsAtExtremes);
    RUN(FormantShiftAndBounds);
    RUN(EnvelopeAttackRelease);
    RUN(DryWetEndpoints);
    RUN(ExternalCarrierNoSignalIsSilentWet);
    RUN(LimiterPeakContainment);
    RUN(ClippingDetection);
    RUN(NanRecovery);
    RUN(SynthWaveforms);
    RUN(SynthAdsr);
    RUN(NoteStackPriority);
    RUN(LegatoVsRetrigger);
    RUN(Glide);
    RUN(PolyVoiceAllocation);
    RUN(PolyLevelNormalisation);
    RUN(PitchBend);
    RUN(MidiChannelAndNotes);
    RUN(MidiParserRunningStatusAndRealtime);
    RUN(MidiCcMappingAndClamping);
    RUN(SettingsDefaults);
    RUN(SettingsPersistenceAndValidation);
    RUN(SaveDebounce);
    RUN(InitHold);
    RUN(BridgeQueues);
    RUN(DefaultLevelsAreAudible);
    RUN(SpectralEnvelopeTransfer);
    RUN(AcceptanceNoteOnlyFromErasedSettings);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
