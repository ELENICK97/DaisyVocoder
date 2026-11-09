#include "Synth.h"

#include <cmath>

#include "DspUtil.h"

using namespace dsp;

/*--------------------------------------------------------------------------*/
void NoteStack::Push(uint8_t note)
{
    Remove(note);
    if(count_ == SYNTH_NOTE_STACK_SIZE)
    {
        // Full: drop the oldest note.
        for(int i = 1; i < count_; i++)
            notes_[i - 1] = notes_[i];
        count_--;
    }
    notes_[count_++] = note;
}

void NoteStack::Remove(uint8_t note)
{
    const int i = IndexOf(note);
    if(i < 0)
        return;
    for(int j = i + 1; j < count_; j++)
        notes_[j - 1] = notes_[j];
    count_--;
}

int NoteStack::IndexOf(uint8_t note) const
{
    for(int i = 0; i < count_; i++)
        if(notes_[i] == note)
            return i;
    return -1;
}

/*--------------------------------------------------------------------------*/
// Exponential segments cover 99% of their distance in the configured time.
static float ExpCoef(float ms, float sr)
{
    const float samples = ms * 0.001f * sr;
    return samples <= 1.0f ? 0.0f : std::exp(-4.60517f / samples);
}

void Adsr::Init(float sample_rate)
{
    sr_ = sample_rate;
    SetTimes(SYNTH_ATTACK_MS_DEFAULT,
             SYNTH_DECAY_MS_DEFAULT,
             SYNTH_SUSTAIN_DEFAULT,
             SYNTH_RELEASE_MS_DEFAULT);
    Reset();
}

void Adsr::SetTimes(float attack_ms, float decay_ms, float sustain, float release_ms)
{
    const float atk_samples = attack_ms * 0.001f * sr_;
    atk_inc_  = atk_samples <= 1.0f ? 1.0f : 1.0f / atk_samples;
    dec_coef_ = ExpCoef(decay_ms, sr_);
    rel_coef_ = ExpCoef(release_ms, sr_);
    sustain_  = Clamp(sustain, 0.0f, 1.0f);
}

void Adsr::Reset()
{
    stage_ = IDLE;
    level_ = 0.0f;
    gate_  = false;
}

void Adsr::Gate(bool on)
{
    if(on && !gate_)
        stage_ = ATTACK;
    else if(!on && gate_)
        stage_ = RELEASE;
    gate_ = on;
}

void Adsr::Retrigger()
{
    if(gate_)
        stage_ = ATTACK;
}

float Adsr::Process()
{
    switch(stage_)
    {
        case IDLE: level_ = 0.0f; break;
        case ATTACK:
            level_ += atk_inc_;
            if(level_ >= 1.0f)
            {
                level_ = 1.0f;
                stage_ = DECAY;
            }
            break;
        case DECAY:
        case SUSTAIN:
            // Sustain also tracks the target so a live Sustain change glides.
            level_ = sustain_ + (level_ - sustain_) * dec_coef_;
            if(stage_ == DECAY && std::fabs(level_ - sustain_) < 1e-4f)
                stage_ = SUSTAIN;
            break;
        case RELEASE:
            level_ *= rel_coef_;
            if(level_ < 1e-5f)
            {
                level_ = 0.0f;
                stage_ = IDLE;
            }
            break;
    }
    return level_;
}

/*--------------------------------------------------------------------------*/
void Voice::Init(float sample_rate)
{
    sr_ = sample_rate;
    env.Init(sample_rate);
    note      = -1;
    phase_    = 0.0f;
    dt_dirty_ = true;
}

void Voice::StartPitch(float new_note, float glide_ms)
{
    target_pitch              = new_note;
    const float glide_samples = glide_ms * 0.001f * sr_;
    if(Sounding() && glide_samples >= 1.0f)
        glide_step_ = std::fabs(target_pitch - pitch) / glide_samples;
    else
    {
        pitch       = target_pitch;
        glide_step_ = 0.0f;
    }
    dt_dirty_ = true;
}

float Voice::FrequencyHz(float bend) const
{
    return 440.0f * std::exp2((pitch + bend - 69.0f) / 12.0f);
}

// PolyBLEP residual for a discontinuity at phase t (0..1), increment dt.
static inline float PolyBlep(float t, float dt)
{
    if(t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if(t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

float Voice::Oscillator(uint32_t waveform, float pw, float dt) const
{
    float y;
    switch(waveform)
    {
        case SYNTH_WAVE_SAW:
            y = 2.0f * phase_ - 1.0f;
            y -= PolyBlep(phase_, dt);
            break;
        case SYNTH_WAVE_SQUARE:
        {
            y = phase_ < pw ? 1.0f : -1.0f;
            y += PolyBlep(phase_, dt);
            float t2 = phase_ - pw;
            if(t2 < 0.0f)
                t2 += 1.0f;
            y -= PolyBlep(t2, dt);
            // Remove the DC offset of an asymmetric pulse.
            y -= 2.0f * pw - 1.0f;
            break;
        }
        case SYNTH_WAVE_TRIANGLE: y = 4.0f * std::fabs(phase_ - 0.5f) - 1.0f; break;
        case SYNTH_WAVE_SINE: y = std::sin(kTwoPi * phase_); break;
        default: y = 0.0f; break; // NOISE: handled by caller
    }
    return y;
}

static inline float NextNoise(uint32_t &rng)
{
    // xorshift32 -> uniform [-1, 1)
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return static_cast<float>(static_cast<int32_t>(rng)) * (1.0f / 2147483648.0f);
}

float Voice::Process(uint32_t waveform, float pw, float noise_level, float bend, uint32_t &rng)
{
    if(glide_step_ > 0.0f)
    {
        const float diff = target_pitch - pitch;
        if(std::fabs(diff) <= glide_step_)
        {
            pitch       = target_pitch;
            glide_step_ = 0.0f;
        }
        else
            pitch += diff > 0.0f ? glide_step_ : -glide_step_;
        dt_dirty_ = true;
    }
    if(dt_dirty_ || bend != last_bend_)
    {
        last_bend_ = bend;
        dt_        = Clamp(FrequencyHz(bend) / sr_, 0.0f, 0.45f);
        dt_dirty_  = false;
    }

    const float level = env.Process();
    if(level == 0.0f)
        return 0.0f; // idle voices cost almost nothing
    float y;
    if(waveform == SYNTH_WAVE_NOISE)
        y = SYNTH_OSC_LEVEL * NextNoise(rng);
    else
    {
        y = SYNTH_OSC_LEVEL * Oscillator(waveform, pw, dt_) + noise_level * NextNoise(rng);
        phase_ += dt_;
        if(phase_ >= 1.0f)
            phase_ -= 1.0f;
    }
    return y * level;
}

/*--------------------------------------------------------------------------*/
void Synth::Init(float sample_rate)
{
    sr_ = sample_rate;
    for(int v = 0; v < kNumVoices; v++)
        voices_[v].Init(sample_rate);
    stack_.Clear();
    lead_ = 0;
}

void Synth::SetWaveform(uint32_t waveform)
{
    if(waveform < SYNTH_WAVE_COUNT)
        waveform_ = waveform;
}

void Synth::SetNoiseLevel(float level)
{
    noise_level_ = Clamp(level, 0.0f, SYNTH_NOISE_LEVEL_MAX);
}

void Synth::SetPulseWidth(float pw)
{
    pw_ = Clamp(pw, SYNTH_PULSE_WIDTH_MIN, SYNTH_PULSE_WIDTH_MAX);
}

void Synth::SetAdsr(float a, float d, float s, float r)
{
    a = Clamp(a, SYNTH_ATTACK_MS_MIN, SYNTH_ATTACK_MS_MAX);
    d = Clamp(d, SYNTH_DECAY_MS_MIN, SYNTH_DECAY_MS_MAX);
    r = Clamp(r, SYNTH_RELEASE_MS_MIN, SYNTH_RELEASE_MS_MAX);
    for(int v = 0; v < kNumVoices; v++)
        voices_[v].env.SetTimes(a, d, s, r);
}

void Synth::SetGlideMs(float ms)
{
    glide_ms_ = Clamp(ms, 0.0f, SYNTH_GLIDE_MS_MAX);
}

void Synth::SetLegato(bool on)
{
    legato_ = on;
}

void Synth::SetPitchBend(float semitones)
{
    if(!std::isfinite(semitones))
        return;
    bend_ = semitones;
}

void Synth::SetPolyphonic(bool on)
{
    if(on == poly_)
        return;
    AllNotesOff(); // voice/note bookkeeping differs between modes
    poly_ = on;
    lead_ = 0;
}

int Synth::GatedVoices() const
{
    int n = 0;
    for(int v = 0; v < kNumVoices; v++)
        n += voices_[v].env.GateOn() ? 1 : 0;
    return n;
}

void Synth::NoteOn(uint8_t note)
{
    if(note > 127)
        return;
    if(poly_)
        NoteOnPoly(note);
    else
        NoteOnMono(note);
}

void Synth::NoteOff(uint8_t note)
{
    // Poly always checks the voices: with more than SYNTH_NOTE_STACK_SIZE
    // keys down, a sounding note may have been dropped from the stack.
    if(poly_)
        NoteOffPoly(note);
    else if(stack_.IndexOf(note) >= 0)
        NoteOffMono(note);
}

void Synth::AllNotesOff()
{
    stack_.Clear();
    for(int v = 0; v < kNumVoices; v++)
        voices_[v].env.Gate(false);
}

/*---------------------------------- Mono ----------------------------------*/
void Synth::NoteOnMono(uint8_t note)
{
    Voice     &v        = voices_[0];
    const bool was_held = !stack_.Empty();
    stack_.Push(note);
    v.note = note;
    v.StartPitch(static_cast<float>(note), glide_ms_);
    if(!was_held)
        v.env.Gate(true); // first held note: start attack
    else if(!legato_)
        v.env.Retrigger();
}

void Synth::NoteOffMono(uint8_t note)
{
    const uint8_t top = stack_.Top();
    stack_.Remove(note);
    Voice &v = voices_[0];
    if(stack_.Empty())
        v.env.Gate(false);
    else if(note == top)
    {
        v.note = stack_.Top();
        v.StartPitch(static_cast<float>(stack_.Top()), glide_ms_); // no retrigger
    }
}

/*---------------------------------- Poly ----------------------------------*/
int Synth::FindVoiceForNewNote(uint8_t note) const
{
    // 1. A voice already on this note (e.g. still releasing): reuse it.
    for(int v = 0; v < kNumVoices; v++)
        if(voices_[v].note == note && voices_[v].Sounding())
            return v;
    // 2. An idle voice.
    for(int v = 0; v < kNumVoices; v++)
        if(!voices_[v].Sounding())
            return v;
    // 3. The quietest voice in release.
    int   best  = -1;
    float level = 2.0f;
    for(int v = 0; v < kNumVoices; v++)
        if(!voices_[v].env.GateOn() && voices_[v].env.Level() < level)
        {
            level = voices_[v].env.Level();
            best  = v;
        }
    if(best >= 0)
        return best;
    // 4. Steal the voice holding the oldest held note.
    int oldest_idx = SYNTH_NOTE_STACK_SIZE + 1;
    best           = 0;
    for(int v = 0; v < kNumVoices; v++)
    {
        const int idx = voices_[v].note < 0 ? -1 : stack_.IndexOf(static_cast<uint8_t>(voices_[v].note));
        if(idx < oldest_idx)
        {
            oldest_idx = idx;
            best       = v;
        }
    }
    return best;
}

void Synth::AssignVoice(int v, uint8_t note, bool retrigger)
{
    Voice &vc = voices_[v];
    vc.note   = note;
    vc.StartPitch(static_cast<float>(note), glide_ms_);
    if(!vc.env.GateOn())
        vc.env.Gate(true); // attack from current level (no click)
    else if(retrigger)
        vc.env.Retrigger();
    lead_ = v;
}

void Synth::NoteOnPoly(uint8_t note)
{
    stack_.Push(note);
    // Already gated on this note (repeated Note On without Note Off)?
    for(int v = 0; v < kNumVoices; v++)
        if(voices_[v].note == note && voices_[v].env.GateOn())
        {
            voices_[v].env.Retrigger();
            lead_ = v;
            return;
        }
    AssignVoice(FindVoiceForNewNote(note), note, true);
}

void Synth::NoteOffPoly(uint8_t note)
{
    stack_.Remove(note);
    for(int v = 0; v < kNumVoices; v++)
    {
        Voice &vc = voices_[v];
        if(vc.note != note || !vc.env.GateOn())
            continue;
        // Give the voice to the most recent held note that has no voice
        // (it was stolen earlier), otherwise release it.
        for(int i = stack_.Count() - 1; i >= 0; i--)
        {
            const uint8_t n      = stack_.At(i);
            bool          voiced = false;
            for(int w = 0; w < kNumVoices; w++)
                voiced |= voices_[w].note == n && voices_[w].env.GateOn();
            if(!voiced)
            {
                AssignVoice(v, n, false); // pitch change, no retrigger
                return;
            }
        }
        vc.env.Gate(false);
        return;
    }
}

/*--------------------------------------------------------------------------*/
float Synth::Process()
{
    float sum = 0.0f, power = 0.0f;
    for(int v = 0; v < kNumVoices; v++)
    {
        sum += voices_[v].Process(waveform_, pw_, noise_level_, bend_, rng_);
        const float l = voices_[v].env.Level();
        power += l * l;
    }
    // Uncorrelated voices add in power: keep a chord as loud as one note.
    return power > 1.0f ? sum / std::sqrt(power) : sum;
}
