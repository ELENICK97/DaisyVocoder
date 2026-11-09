#include "MidiMapper.h"

#include <cmath>

#include "MidiConfig.h"
#include "SynthConfig.h"
#include "VocoderConfig.h"

static_assert(MIDI_CHANNEL >= 0 && MIDI_CHANNEL <= 16,
              "MIDI_CHANNEL must be 1..16, or 0 for omni");

namespace
{
struct CcAssignment
{
    int     cc;
    ParamId param;
};

constexpr CcAssignment kCcMap[] = {
    {MIDI_CC_CARRIER_MODE, PARAM_CARRIER_MODE},
    {MIDI_CC_CARRIER_LEVEL, PARAM_CARRIER_LEVEL},
    {MIDI_CC_MIC_GAIN, PARAM_MIC_GAIN_DB},
    {MIDI_CC_DRY_WET, PARAM_DRY_WET},
    {MIDI_CC_OUTPUT_LEVEL, PARAM_OUTPUT_LEVEL},
    {MIDI_CC_VOC_ATTACK, PARAM_VOC_ATTACK_MS},
    {MIDI_CC_VOC_RELEASE, PARAM_VOC_RELEASE_MS},
    {MIDI_CC_ENV_AMOUNT, PARAM_ENV_AMOUNT},
    {MIDI_CC_FORMANT_SHIFT, PARAM_FORMANT_SHIFT_ST},
    {MIDI_CC_FILTER_Q, PARAM_FILTER_Q},
    {MIDI_CC_WAVEFORM, PARAM_WAVEFORM},
    {MIDI_CC_NOISE_LEVEL, PARAM_NOISE_LEVEL},
    {MIDI_CC_PULSE_WIDTH, PARAM_PULSE_WIDTH},
    {MIDI_CC_SYNTH_ATTACK, PARAM_SYNTH_ATTACK_MS},
    {MIDI_CC_SYNTH_DECAY, PARAM_SYNTH_DECAY_MS},
    {MIDI_CC_SYNTH_SUSTAIN, PARAM_SYNTH_SUSTAIN},
    {MIDI_CC_SYNTH_RELEASE, PARAM_SYNTH_RELEASE_MS},
    {MIDI_CC_GLIDE, PARAM_GLIDE_MS},
    {MIDI_CC_LEGATO, PARAM_LEGATO},
    {MIDI_CC_POLY, PARAM_POLY},
};
constexpr int kCcMapSize = sizeof(kCcMap) / sizeof(kCcMap[0]);
static_assert(kCcMapSize == PARAM_COUNT, "every parameter needs a CC");

// Compile-time collision / range checks (C++14 constexpr loops).
constexpr bool CcNumbersValid()
{
    for(int i = 0; i < kCcMapSize; i++)
    {
        if(kCcMap[i].cc < 0 || kCcMap[i].cc >= 120)
            return false;
        if(kCcMap[i].cc == MIDI_CC_INIT)
            return false;
        for(int j = i + 1; j < kCcMapSize; j++)
            if(kCcMap[i].cc == kCcMap[j].cc)
                return false;
    }
    return MIDI_CC_INIT >= 0 && MIDI_CC_INIT < 120;
}
static_assert(CcNumbersValid(),
              "MidiConfig.h: CC numbers must be unique and in 0..119");

inline float Norm(uint8_t v)
{
    return static_cast<float>(v > 127 ? 127 : v) / 127.0f;
}
inline float Lin(float t, float lo, float hi)
{
    return lo + t * (hi - lo);
}
inline float Expo(float t, float lo, float hi)
{
    return lo * std::pow(hi / lo, t);
}
} // namespace

namespace MidiMapper
{
bool ChannelMatches(uint8_t channel)
{
#if MIDI_CHANNEL == 0
    (void)channel;
    return true;
#else
    return channel == (MIDI_CHANNEL - 1);
#endif
}

ParamId CcToParam(uint8_t cc)
{
    for(int i = 0; i < kCcMapSize; i++)
        if(kCcMap[i].cc == cc)
            return kCcMap[i].param;
    return PARAM_COUNT;
}

float CcValueToParam(ParamId id, uint8_t value)
{
    const float t      = Norm(value);
    const bool  switch_on = value >= MIDI_SWITCH_THRESHOLD;
    float       r      = 0.0f;
    switch(id)
    {
        case PARAM_CARRIER_MODE:
            r = static_cast<float>(switch_on ? CARRIER_EXTERNAL_AUDIO
                                             : CARRIER_INTERNAL_SYNTH);
            break;
        case PARAM_CARRIER_LEVEL: r = VOCODER_CARRIER_LEVEL_MAX * t * t; break;
        case PARAM_MIC_GAIN_DB:
            r = Lin(t, VOCODER_MIC_GAIN_DB_MIN, VOCODER_MIC_GAIN_DB_MAX);
            break;
        case PARAM_DRY_WET: r = t; break;
        case PARAM_OUTPUT_LEVEL: r = VOCODER_OUTPUT_LEVEL_MAX * t * t; break;
        case PARAM_VOC_ATTACK_MS:
            r = Expo(t, VOCODER_ATTACK_MS_MIN, VOCODER_ATTACK_MS_MAX);
            break;
        case PARAM_VOC_RELEASE_MS:
            r = Expo(t, VOCODER_RELEASE_MS_MIN, VOCODER_RELEASE_MS_MAX);
            break;
        case PARAM_ENV_AMOUNT: r = t; break;
        case PARAM_FORMANT_SHIFT_ST:
        {
            // 64 is exactly neutral; 0 -> MIN, 127 -> MAX.
            const int v = value > 127 ? 127 : value;
            r = v <= 64 ? VOCODER_FORMANT_SHIFT_MIN * (64 - v) / 64.0f
                        : VOCODER_FORMANT_SHIFT_MAX * (v - 64) / 63.0f;
            break;
        }
        case PARAM_FILTER_Q:
            r = Expo(t, VOCODER_FILTER_Q_MIN, VOCODER_FILTER_Q_MAX);
            break;
        case PARAM_WAVEFORM:
        {
            int w = (value > 127 ? 127 : value) * SYNTH_WAVE_COUNT / 128;
            r     = static_cast<float>(w);
            break;
        }
        case PARAM_NOISE_LEVEL: r = SYNTH_NOISE_LEVEL_MAX * t * t; break;
        case PARAM_PULSE_WIDTH:
            r = Lin(t, SYNTH_PULSE_WIDTH_MIN, SYNTH_PULSE_WIDTH_MAX);
            break;
        case PARAM_SYNTH_ATTACK_MS:
            r = Expo(t, SYNTH_ATTACK_MS_MIN, SYNTH_ATTACK_MS_MAX);
            break;
        case PARAM_SYNTH_DECAY_MS:
            r = Expo(t, SYNTH_DECAY_MS_MIN, SYNTH_DECAY_MS_MAX);
            break;
        case PARAM_SYNTH_SUSTAIN: r = t; break;
        case PARAM_SYNTH_RELEASE_MS:
            r = Expo(t, SYNTH_RELEASE_MS_MIN, SYNTH_RELEASE_MS_MAX);
            break;
        case PARAM_GLIDE_MS: r = value == 0 ? 0.0f : SYNTH_GLIDE_MS_MAX * t * t; break;
        case PARAM_LEGATO:
        case PARAM_POLY: r = switch_on ? 1.0f : 0.0f; break;
        default: break;
    }
    // Final clamp through the shared parameter limits.
    Params p = DefaultParams();
    SetParam(p, id, r);
    return GetParam(p, id);
}

float PitchBendToSemitones(uint8_t lsb, uint8_t msb)
{
    const int raw = ((msb & 0x7F) << 7) | (lsb & 0x7F); // 0..16383
    const int centered = raw - 8192;                    // -8192..8191
    const float norm = centered >= 0 ? centered / 8191.0f : centered / 8192.0f;
    return norm * MIDI_PITCH_BEND_RANGE_ST;
}

MidiAction Map(const MidiMessage &msg)
{
    MidiAction a;
    if(!ChannelMatches(msg.channel))
        return a;
    switch(msg.type)
    {
        case MidiMessage::NOTE_ON:
            if(msg.data0 > 127)
                break;
            a.kind = msg.data1 == 0 ? MidiAction::NOTE_OFF : MidiAction::NOTE_ON;
            a.note = msg.data0;
            break;
        case MidiMessage::NOTE_OFF:
            if(msg.data0 > 127)
                break;
            a.kind = MidiAction::NOTE_OFF;
            a.note = msg.data0;
            break;
        case MidiMessage::PITCH_BEND:
            a.kind  = MidiAction::PITCH_BEND;
            a.value = PitchBendToSemitones(msg.data0, msg.data1);
            break;
        case MidiMessage::CONTROL_CHANGE:
            if(msg.data0 == MIDI_CC_INIT)
            {
                a.kind    = MidiAction::INIT_BUTTON;
                a.pressed = msg.data1 >= MIDI_SWITCH_THRESHOLD;
            }
            else
            {
                const ParamId id = CcToParam(msg.data0);
                if(id != PARAM_COUNT)
                {
                    a.kind  = MidiAction::SET_PARAM;
                    a.param = id;
                    a.value = CcValueToParam(id, msg.data1);
                }
            }
            break;
        case MidiMessage::CHANNEL_MODE:
            // 120 All Sound Off, 123 All Notes Off (and 124-127 imply it).
            if(msg.data0 == 120 || msg.data0 >= 123)
                a.kind = MidiAction::ALL_NOTES_OFF;
            break;
        default: break;
    }
    return a;
}
} // namespace MidiMapper
