#include "SettingsStorage.h"

#include <cstddef>
#include <cstring>

#include "MidiConfig.h"
#include "VocoderConfig.h"

static_assert(sizeof(StoredSettings) == 4 * 4 + sizeof(Params),
              "StoredSettings must be unpadded");

namespace
{
uint32_t Fnv1a(const uint8_t *data, size_t len)
{
    uint32_t h = 2166136261u;
    for(size_t i = 0; i < len; i++)
    {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

uint32_t Checksum(const StoredSettings &s)
{
    return Fnv1a(reinterpret_cast<const uint8_t *>(&s),
                 offsetof(StoredSettings, checksum));
}
} // namespace

bool StoredSettings::operator!=(const StoredSettings &o) const
{
    return std::memcmp(this, &o, sizeof(StoredSettings)) != 0;
}

StoredSettings MakeStoredSettings(const Params &p)
{
    StoredSettings s;
    std::memset(&s, 0, sizeof(s));
    s.magic    = kSettingsMagic;
    s.version  = kSettingsVersion;
    s.size     = sizeof(Params);
    s.params   = p;
    s.checksum = Checksum(s);
    return s;
}

bool ValidateStoredSettings(const StoredSettings &s, Params &out)
{
    if(s.magic != kSettingsMagic || s.version != kSettingsVersion
       || s.size != sizeof(Params) || s.checksum != Checksum(s))
        return false;
    out = s.params;
    SanitizeParams(out); // range-clamp even a checksum-valid record
    return true;
}

SettingsManager::LoadResult SettingsManager::Init(SettingsBackend &backend)
{
    backend_ = &backend;
    params_  = DefaultParams();
    dirty_   = false;

    StoredSettings s;
    if(!backend.Read(s))
        return LoadResult::DEFAULTS_EMPTY;
    Params loaded;
    if(!ValidateStoredSettings(s, loaded))
        return LoadResult::DEFAULTS_INVALID;
    params_ = loaded;
    return LoadResult::LOADED;
}

void SettingsManager::SetParam(ParamId id, float value, uint32_t now_ms)
{
    Params p = params_;
    ::SetParam(p, id, value);
    if(ParamsEqual(p, params_))
        return;
    params_         = p;
    dirty_          = true;
    last_change_ms_ = now_ms;
}

void SettingsManager::OnInitButton(bool pressed, uint32_t now_ms)
{
    if(pressed && !init_pressed_)
    {
        init_start_ms_ = now_ms;
        init_fired_    = false;
    }
    init_pressed_ = pressed;
}

bool SettingsManager::Update(uint32_t now_ms)
{
    bool init_now = false;
    if(init_pressed_ && !init_fired_
       && (uint32_t)(now_ms - init_start_ms_) >= MIDI_INIT_HOLD_MS)
    {
        init_fired_ = true; // once per hold
        params_     = DefaultParams();
        init_count_++;
        Save(); // INIT saves immediately
        init_now = true;
    }
    if(dirty_ && (uint32_t)(now_ms - last_change_ms_) >= SETTINGS_SAVE_DEBOUNCE_MS)
        Save();
    return init_now;
}

void SettingsManager::Save()
{
    dirty_ = false;
    if(backend_ && backend_->Write(MakeStoredSettings(params_)))
        save_count_++;
}
