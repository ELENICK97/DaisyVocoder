#include "Controller.h"

SettingsManager::LoadResult Controller::Init(SettingsBackend &backend,
                                             ControlBridge   &bridge)
{
    bridge_                                = &bridge;
    const SettingsManager::LoadResult res  = settings_.Init(backend);
    params_unsent_                         = true;
    PublishParams();
    return res;
}

bool Controller::HandleMidi(const MidiMessage &msg, uint32_t now_ms)
{
    const MidiAction a = MidiMapper::Map(msg);
    NoteEvent        ev{};
    switch(a.kind)
    {
        case MidiAction::NOTE_ON:
            ev.type = NoteEvent::NOTE_ON;
            ev.note = a.note;
            return bridge_->notes.Push(ev);
        case MidiAction::NOTE_OFF:
            ev.type = NoteEvent::NOTE_OFF;
            ev.note = a.note;
            return bridge_->notes.Push(ev);
        case MidiAction::ALL_NOTES_OFF:
            ev.type = NoteEvent::ALL_NOTES_OFF;
            return bridge_->notes.Push(ev);
        case MidiAction::PITCH_BEND:
            ev.type           = NoteEvent::PITCH_BEND;
            ev.bend_semitones = a.value;
            return bridge_->notes.Push(ev);
        case MidiAction::SET_PARAM:
            settings_.SetParam(a.param, a.value, now_ms);
            params_unsent_ = true;
            PublishParams();
            return true;
        case MidiAction::INIT_BUTTON:
            settings_.OnInitButton(a.pressed, now_ms);
            return true;
        default: return true;
    }
}

void Controller::Update(uint32_t now_ms)
{
    if(settings_.Update(now_ms))
        params_unsent_ = true;
    PublishParams();
}

void Controller::PublishParams()
{
    // If the audio side has not consumed the previous snapshot yet, keep the
    // flag and try again on the next call; the newest values win.
    if(params_unsent_ && bridge_->params.TryPublish(settings_.Get()))
        params_unsent_ = false;
}

void ApplyBridgeToEngine(ControlBridge &bridge, Engine &engine)
{
    Params p;
    if(bridge.params.TryConsume(p))
        engine.SetParams(p);

    NoteEvent ev;
    while(bridge.notes.Pop(ev)) // bounded by queue capacity
    {
        switch(ev.type)
        {
            case NoteEvent::NOTE_ON: engine.NoteOn(ev.note); break;
            case NoteEvent::NOTE_OFF: engine.NoteOff(ev.note); break;
            case NoteEvent::ALL_NOTES_OFF: engine.AllNotesOff(); break;
            case NoteEvent::PITCH_BEND: engine.SetPitchBend(ev.bend_semitones); break;
        }
    }
}
