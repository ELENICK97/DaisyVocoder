#pragma once
/**
 * Synth.h — internal carrier synth, SYNTH_NUM_VOICES-voice polyphonic or
 * monophonic (Poly/Mono parameter).
 *
 * Mono mode: last-note priority. Releasing the sounding note returns to the
 * most recent note still held (pitch change only, no envelope retrigger).
 * The ADSR releases when no notes remain held.
 *   Legato on:  a new note while another is held changes pitch only.
 *   Legato off: every new Note On retriggers the ADSR attack from its current
 *               level (no reset to zero, so no click).
 *
 * Poly mode: each held note gets its own voice (oscillator + ADSR).
 *   Allocation: a voice already playing/releasing that note, else an idle
 *   voice, else the quietest releasing voice, else the voice holding the
 *   oldest held note is stolen (newest notes win, like last-note priority).
 *   Releasing a note while more notes are held than there are voices gives
 *   its voice to the most recent held-but-silent note (pitch change, no
 *   retrigger). Legato has no effect in Poly mode.
 *   The voice sum is normalised by 1/sqrt(sum of voice levels^2), so one
 *   note is exactly as loud as in Mono and chords are not louder.
 *
 * Glide (both modes, independent of Legato): when > 0 ms and the voice is
 * already sounding, its pitch slides linearly (in semitones) to the new note
 * in exactly the glide time. Velocity is ignored (V1). Pitch Bend applies to
 * all voices.
 */
#include <cstdint>

#include "SynthConfig.h"

/** Fixed-size note stack, most recent note on top. */
class NoteStack
{
  public:
    void Clear() { count_ = 0; }
    void Push(uint8_t note);   /**< moves note to top if already held */
    void Remove(uint8_t note); /**< no-op if not held */
    bool Empty() const { return count_ == 0; }
    int  Count() const { return count_; }
    uint8_t Top() const { return notes_[count_ - 1]; }
    /** i = 0 is the oldest held note. */
    uint8_t At(int i) const { return notes_[i]; }
    /** Press order index of note (0 = oldest), or -1 if not held. */
    int IndexOf(uint8_t note) const;

  private:
    uint8_t notes_[SYNTH_NOTE_STACK_SIZE];
    int     count_ = 0;
};

class Adsr
{
  public:
    enum Stage
    {
        IDLE,
        ATTACK,
        DECAY,
        SUSTAIN,
        RELEASE,
    };

    void Init(float sample_rate);
    void SetTimes(float attack_ms, float decay_ms, float sustain, float release_ms);
    void Gate(bool on); /**< rising gate (re)starts ATTACK from current level */
    void Retrigger();   /**< restart ATTACK from current level */
    void Reset();
    float Process();
    float Level() const { return level_; }
    Stage GetStage() const { return stage_; }
    bool  GateOn() const { return gate_; }

  private:
    float sr_ = 48000.0f;
    Stage stage_ = IDLE;
    float level_ = 0.0f;
    float atk_inc_ = 1.0f, dec_coef_ = 1.0f, rel_coef_ = 1.0f, sustain_ = 1.0f;
    bool  gate_ = false;
};

/** One oscillator + ADSR + glide state. */
class Voice
{
  public:
    void Init(float sample_rate);
    /** Move to note; glides if glide_ms > 0 and the voice is sounding. */
    void StartPitch(float note, float glide_ms);
    /** One sample: oscillator (+noise) times envelope. */
    float Process(uint32_t waveform, float pw, float noise_level, float bend, uint32_t &rng);
    void  MarkDirty() { dt_dirty_ = true; }

    Adsr  env;
    int   note = -1; /**< assigned MIDI note, -1 = none */
    float pitch = 60.0f, target_pitch = 60.0f;

    bool Sounding() const { return env.GetStage() != Adsr::IDLE; }
    float FrequencyHz(float bend) const;

  private:
    float Oscillator(uint32_t waveform, float pw, float dt) const;

    float sr_ = 48000.0f;
    float glide_step_ = 0.0f;
    float phase_      = 0.0f;
    float dt_         = 0.0f; // phase increment, cycles/sample
    float last_bend_  = 0.0f;
    bool  dt_dirty_   = true;
};

class Synth
{
  public:
    static constexpr int kNumVoices = SYNTH_NUM_VOICES;
    static_assert(kNumVoices >= 1 && kNumVoices <= 8, "1..8 voices");

    void Init(float sample_rate);

    void SetWaveform(uint32_t waveform);
    void SetNoiseLevel(float level);
    void SetPulseWidth(float pw);
    void SetAdsr(float attack_ms, float decay_ms, float sustain, float release_ms);
    void SetGlideMs(float ms);
    void SetLegato(bool on);
    void SetPitchBend(float semitones);
    /** Switching mode releases all voices and clears held notes. */
    void SetPolyphonic(bool on);
    bool Polyphonic() const { return poly_; }

    void NoteOn(uint8_t note);
    void NoteOff(uint8_t note);
    void AllNotesOff();

    float Process();

    // Introspection for tests. "Lead" = voice of the most recently started
    // note (always voice 0 in Mono mode).
    float CurrentPitch() const { return voices_[lead_].pitch; } /**< excl. bend */
    float TargetPitch() const { return voices_[lead_].target_pitch; }
    float FrequencyHz() const { return voices_[lead_].FrequencyHz(bend_); }
    bool  GateOn() const { return !stack_.Empty(); }
    int   HeldCount() const { return stack_.Count(); }
    const Adsr  &Envelope() const { return voices_[lead_].env; }
    const Voice &GetVoice(int i) const { return voices_[i]; }
    /** Number of voices whose gate is on. */
    int GatedVoices() const;

  private:
    void NoteOnMono(uint8_t note);
    void NoteOffMono(uint8_t note);
    void NoteOnPoly(uint8_t note);
    void NoteOffPoly(uint8_t note);
    int  FindVoiceForNewNote(uint8_t note) const;
    void AssignVoice(int v, uint8_t note, bool retrigger);

    float     sr_ = 48000.0f;
    NoteStack stack_;
    Voice     voices_[kNumVoices];
    int       lead_ = 0;

    uint32_t waveform_    = SYNTH_WAVEFORM_DEFAULT;
    float    noise_level_ = SYNTH_NOISE_LEVEL_DEFAULT;
    float    pw_          = SYNTH_PULSE_WIDTH_DEFAULT;
    float    glide_ms_    = SYNTH_GLIDE_MS_DEFAULT;
    bool     legato_      = SYNTH_LEGATO_DEFAULT != 0;
    bool     poly_        = SYNTH_POLY_DEFAULT != 0;
    float    bend_        = 0.0f;
    uint32_t rng_         = 0x12345678u;
};
