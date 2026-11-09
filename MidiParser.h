#pragma once
/**
 * MidiParser.h — byte-stream MIDI parser for channel voice messages.
 *
 * Replaces libDaisy's MidiParser, which overwrites the stored channel with
 * the low nibble of real-time bytes (0xF8 Clock, 0xFE Active Sensing) so that
 * following running-status notes arrive on the wrong channel and are dropped.
 *
 * Rules implemented (MIDI 1.0):
 *  - Real-time bytes (0xF8-0xFF) may appear anywhere and are ignored without
 *    disturbing running status or a partially received message.
 *  - System Common / SysEx (0xF0-0xF7) cancel running status; their data
 *    bytes are skipped.
 *  - Channel voice status bytes set running status; data bytes without a
 *    valid running status are ignored.
 *  - Note On with velocity 0 is reported as NOTE_ON with data1 = 0 (the
 *    mapper treats it as Note Off).
 */
#include <cstdint>

#include "MidiMapper.h"

class MidiByteParser
{
  public:
    /** Feed one byte. Returns true and fills out when a message completes. */
    bool Parse(uint8_t byte, MidiMessage &out);
    void Reset() { status_ = 0; count_ = 0; }

  private:
    uint8_t status_ = 0; // running status (0 = none)
    uint8_t data_[2] = {0, 0};
    uint8_t count_   = 0;
};
