#pragma once
/**
 * Controller.h — main-loop glue: MIDI actions -> settings + audio bridge.
 * Hardware independent so the full control path can be tested on desktop.
 *
 *   main loop:   Controller::HandleMidi() / Controller::Update()
 *   audio ISR:   ApplyBridgeToEngine() then Engine::Process()
 */
#include <cstdint>

#include "ControlBridge.h"
#include "Engine.h"
#include "MidiMapper.h"
#include "SettingsStorage.h"

class Controller
{
  public:
    /** Loads settings (or defaults) and queues them for the audio side. */
    SettingsManager::LoadResult Init(SettingsBackend &backend, ControlBridge &bridge);

    /** Process one incoming MIDI message (main loop). Returns false if the
     *  note queue was full; the caller should retry the same message. */
    bool HandleMidi(const MidiMessage &msg, uint32_t now_ms);

    /** Periodic main-loop work: INIT hold, debounced save, param publish. */
    void Update(uint32_t now_ms);

    const SettingsManager &Settings() const { return settings_; }

  private:
    void PublishParams();

    SettingsManager settings_;
    ControlBridge  *bridge_        = nullptr;
    bool            params_unsent_ = false;
};

/** Audio context: apply pending parameter snapshot and note events. */
void ApplyBridgeToEngine(ControlBridge &bridge, Engine &engine);
