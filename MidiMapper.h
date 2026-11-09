#pragma once
/**
 * MidiMapper.h — translates raw MIDI channel messages (from DIN and, if
 * enabled, USB) into vocoder actions. Single central path: every transport
 * converts its events to MidiMessage and calls Map(). All CC numbers come
 * from MidiConfig.h; all values are validated and clamped here.
 */
#include <cstdint>

#include "Params.h"

struct MidiMessage
{
    enum Type : uint8_t
    {
        NOTE_OFF,
        NOTE_ON,
        CONTROL_CHANGE,
        PITCH_BEND,
        CHANNEL_MODE, /**< CC 120..127; data0 = controller number */
        OTHER,
    };
    Type    type;
    uint8_t channel; /**< 0-based, 0..15 */
    uint8_t data0;   /**< note / controller / pitch-bend LSB */
    uint8_t data1;   /**< velocity / value / pitch-bend MSB */
};

struct MidiAction
{
    enum Kind : uint8_t
    {
        NONE,
        NOTE_ON,
        NOTE_OFF,
        ALL_NOTES_OFF,
        PITCH_BEND,
        SET_PARAM,
        INIT_BUTTON,
    };
    Kind    kind     = NONE;
    uint8_t note     = 0;
    ParamId param    = PARAM_COUNT;
    float   value    = 0.0f; /**< param value (own units) or bend semitones */
    bool    pressed  = false; /**< INIT_BUTTON state */
};

namespace MidiMapper
{
/** True if the message's channel matches MIDI_CHANNEL (or omni). */
bool ChannelMatches(uint8_t channel);

/** Map a message to an action. Messages on other channels map to NONE. */
MidiAction Map(const MidiMessage &msg);

/** Parameter assigned to a CC number, or PARAM_COUNT if none. */
ParamId CcToParam(uint8_t cc);

/** Convert a 7-bit CC value into the parameter's units (clamped). */
float CcValueToParam(ParamId id, uint8_t value);

/** Pitch-bend 14-bit data to semitones within +/-MIDI_PITCH_BEND_RANGE_ST. */
float PitchBendToSemitones(uint8_t lsb, uint8_t msb);
} // namespace MidiMapper
