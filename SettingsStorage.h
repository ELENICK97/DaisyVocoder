#pragma once
/**
 * SettingsStorage.h — versioned settings record, validation, debounced
 * auto-save and the INIT (hold-to-reset) logic.
 *
 * Main-loop only: Update() may call the backend's blocking Write(). It must
 * never be called from the audio callback.
 */
#include <cstdint>

#include "Params.h"

/** On-flash record. Fixed-width fields only, no padding. Bump
 *  kSettingsVersion whenever Params changes layout or meaning. */
struct StoredSettings
{
    uint32_t magic;
    uint32_t version;
    uint32_t size; /**< sizeof(Params) when written */
    Params   params;
    uint32_t checksum; /**< FNV-1a over magic..params */

    bool operator!=(const StoredSettings &o) const;
    bool operator==(const StoredSettings &o) const { return !(*this != o); }
};

constexpr uint32_t kSettingsMagic   = 0x564F4331u; // "VOC1"
// v2: carrier flattening + 4th-order bands. v3: intelligibility-tuned
// envelope/Q/noise defaults. Old records are discarded on upgrade so users
// start from the tuned defaults instead of a stale saved Release/Q.
// v4: added Poly/Mono parameter.
constexpr uint32_t kSettingsVersion = 4;

/** Build a valid record (magic/version/checksum filled) from params. */
StoredSettings MakeStoredSettings(const Params &p);

/** Validate a record read from storage. On success writes the (clamped)
 *  params to out and returns true; otherwise returns false. */
bool ValidateStoredSettings(const StoredSettings &s, Params &out);

/** Non-volatile backend. The hardware version wraps libDaisy's
 *  PersistentStorage; tests use a RAM fake. */
class SettingsBackend
{
  public:
    virtual ~SettingsBackend() {}
    /** Return false if nothing could be read. */
    virtual bool Read(StoredSettings &out) = 0;
    /** Blocking write; return false on failure. */
    virtual bool Write(const StoredSettings &s) = 0;
};

class SettingsManager
{
  public:
    enum class LoadResult
    {
        LOADED,           /**< valid saved settings */
        DEFAULTS_EMPTY,   /**< nothing readable -> defaults */
        DEFAULTS_INVALID, /**< corrupt / wrong version -> defaults */
    };

    LoadResult Init(SettingsBackend &backend);

    const Params &Get() const { return params_; }

    /** Apply a parameter change (already in parameter units). Starts/extends
     *  the save debounce window. */
    void SetParam(ParamId id, float value, uint32_t now_ms);

    /** INIT button state from the MIDI CC. */
    void OnInitButton(bool pressed, uint32_t now_ms);

    /** Call regularly from the main loop. Handles the INIT hold and the
     *  debounced save. Returns true if params changed due to INIT. */
    bool Update(uint32_t now_ms);

    bool     SavePending() const { return dirty_; }
    uint32_t SaveCount() const { return save_count_; }
    uint32_t InitCount() const { return init_count_; }

  private:
    void Save();

    SettingsBackend *backend_ = nullptr;
    Params           params_;
    bool             dirty_          = false;
    uint32_t         last_change_ms_ = 0;
    bool             init_pressed_   = false;
    bool             init_fired_     = false;
    uint32_t         init_start_ms_  = 0;
    uint32_t         save_count_     = 0;
    uint32_t         init_count_     = 0;
};
