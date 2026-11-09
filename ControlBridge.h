#pragma once
/**
 * ControlBridge.h — lock-free, wait-free hand-off from the main loop
 * (producer) to the audio callback (consumer). Single producer, single
 * consumer; neither side ever blocks.
 */
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "Params.h"

/** Bounded SPSC ring buffer. N must be a power of two. */
template <typename T, size_t N>
class SpscQueue
{
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

  public:
    /** Producer side. Returns false (event dropped) if full. */
    bool Push(const T &v)
    {
        const uint32_t h = head_.load(std::memory_order_relaxed);
        const uint32_t t = tail_.load(std::memory_order_acquire);
        if(h - t >= N)
            return false;
        buf_[h & (N - 1)] = v;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
    /** Consumer side. */
    bool Pop(T &out)
    {
        const uint32_t t = tail_.load(std::memory_order_relaxed);
        const uint32_t h = head_.load(std::memory_order_acquire);
        if(h == t)
            return false;
        out = buf_[t & (N - 1)];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

  private:
    T                     buf_[N];
    std::atomic<uint32_t> head_{0};
    std::atomic<uint32_t> tail_{0};
};

/** Single-slot mailbox for a whole Params snapshot. The producer may only
 *  write while the slot is empty; the consumer only reads while it is full,
 *  so the two never touch the slot concurrently. */
class ParamMailbox
{
  public:
    bool TryPublish(const Params &p)
    {
        if(full_.load(std::memory_order_acquire))
            return false; // audio has not taken the previous one yet
        slot_ = p;
        full_.store(true, std::memory_order_release);
        return true;
    }
    bool TryConsume(Params &out)
    {
        if(!full_.load(std::memory_order_acquire))
            return false;
        out = slot_;
        full_.store(false, std::memory_order_release);
        return true;
    }

  private:
    Params            slot_;
    std::atomic<bool> full_{false};
};

/** Note / pitch-bend event for the synth. */
struct NoteEvent
{
    enum Type : uint8_t
    {
        NOTE_ON,
        NOTE_OFF,
        ALL_NOTES_OFF,
        PITCH_BEND,
    };
    Type    type;
    uint8_t note;
    float   bend_semitones;
};

/** Everything the audio callback needs from the control side. */
struct ControlBridge
{
    SpscQueue<NoteEvent, 64> notes;
    ParamMailbox             params;
};
