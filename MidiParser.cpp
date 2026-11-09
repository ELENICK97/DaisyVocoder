#include "MidiParser.h"

bool MidiByteParser::Parse(uint8_t byte, MidiMessage &out)
{
    if(byte >= 0xF8) // real-time: transparent
        return false;
    if(byte & 0x80)
    {
        // 0x80-0xEF channel voice sets running status; 0xF0-0xF7 clears it.
        status_ = byte < 0xF0 ? byte : 0;
        count_  = 0;
        return false;
    }
    if(status_ == 0)
        return false; // data without status (or SysEx / System Common data)

    data_[count_++] = byte;
    const uint8_t kind   = status_ & 0xF0;
    const uint8_t needed = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
    if(count_ < needed)
        return false;
    count_ = 0; // running status: stay ready for the next data bytes

    out.channel = status_ & 0x0F;
    out.data0   = data_[0];
    out.data1   = needed == 2 ? data_[1] : 0;
    switch(kind)
    {
        case 0x80: out.type = MidiMessage::NOTE_OFF; break;
        case 0x90: out.type = MidiMessage::NOTE_ON; break;
        case 0xB0:
            out.type = data_[0] >= 120 ? MidiMessage::CHANNEL_MODE
                                       : MidiMessage::CONTROL_CHANGE;
            break;
        case 0xE0: out.type = MidiMessage::PITCH_BEND; break;
        default: out.type = MidiMessage::OTHER; break;
    }
    return true;
}
