#pragma once
/**
 * Engine.h — the complete real-time audio chain, hardware independent:
 *
 *   mic --[Mic Gain]--+--------------------------------(dry)--+
 *                     |                                       |
 *                     +--> Vocoder analysis                   +--[Dry/Wet]--[Limiter]--[Output Level]--> L/R
 *   carrier = INTERNAL_SYNTH ? synth : external input         |
 *          --[Carrier Level]--> Vocoder synthesis ----(wet)---+
 *
 * Everything here runs in the audio callback: no allocation, no blocking,
 * bounded work per sample. Parameters arrive as a whole Params copy via
 * SetParams(); note events via the Note* methods (both audio context).
 */
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "DspUtil.h"
#include "Params.h"
#include "Synth.h"
#include "Vocoder.h"

class Engine
{
  public:
    void Init(float sample_rate, size_t block_size);

    /** Apply a new parameter set (audio context, start of block). */
    void SetParams(const Params &p);

    void NoteOn(uint8_t note) { synth_.NoteOn(note); }
    void NoteOff(uint8_t note) { synth_.NoteOff(note); }
    void AllNotesOff() { synth_.AllNotesOff(); }
    void SetPitchBend(float semitones) { synth_.SetPitchBend(semitones); }

    /** Process one block. mic/ext are mono inputs, out_l/out_r outputs. */
    void Process(const float *mic,
                 const float *ext,
                 float       *out_l,
                 float       *out_r,
                 size_t       size);

    /** Incremented (audio context) once per block that contained clipping;
     *  read from any context. */
    uint32_t ClipEvents() const { return clip_events_.load(std::memory_order_relaxed); }
    /** Incremented whenever a non-finite sample forced a DSP state reset. */
    uint32_t FaultEvents() const { return fault_events_.load(std::memory_order_relaxed); }

    const Params &CurrentParams() const { return params_; }
    const Synth  &GetSynth() const { return synth_; }
    const Vocoder &GetVocoder() const { return vocoder_; }

  private:
    Params           params_;
    Synth            synth_;
    Vocoder          vocoder_;
    dsp::PeakLimiter limiter_;

    float         mic_gain_target_ = 1.0f;
    dsp::Smoother mic_gain_, carrier_level_, carrier_sel_, dry_wet_, out_level_;

    std::atomic<uint32_t> clip_events_{0};
    std::atomic<uint32_t> fault_events_{0};
};
