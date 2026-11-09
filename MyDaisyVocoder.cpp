/**
 * MyDaisyVocoder.cpp — Daisy Seed 16-band vocoder V1: hardware init, audio
 * callback and main loop.
 *
 * Wiring (Daisy Seed pin numbers):
 *   Pin 16  AUDIO IN L   microphone (after external bias/preamp, line level)
 *   Pin 17  AUDIO IN R   external carrier (line level)
 *   Pin 18  AUDIO OUT L  vocoder output
 *   Pin 19  AUDIO OUT R  vocoder output (same mono signal)
 *   Pin 15  D14 / USART1 RX  DIN MIDI IN, via opto-isolated MIDI input circuit
 *   USB     power, firmware upload, optional USB MIDI (MIDI_ENABLE_USB)
 *   User LED (on board) lights briefly when an input clips.
 *
 * Contexts:
 *   Audio callback: ApplyBridgeToEngine() + Engine::Process() only.
 *   Main loop:      MIDI parsing, parameter mapping, settings save (QSPI).
 */
#include "daisy_seed.h"

#include "ControlBridge.h"
#include "Controller.h"
#include "Engine.h"
#include "MidiConfig.h"
#include "MidiParser.h"
#include "SettingsStorage.h"
#include "VocoderConfig.h"

using namespace daisy;

static_assert(!(MIDI_ENABLE_USB && VOCODER_CPU_LOG),
              "USB MIDI and the USB serial CPU log both need the USB port");

static DaisySeed     hw;
static Engine        engine;
static ControlBridge bridge;
static Controller    controller;
static CpuLoadMeter  cpu_meter;

/** One MIDI input: libDaisy transport (DMA/USB, interrupt context) ->
 *  lock-free byte queue -> our parser in the main loop. libDaisy's own
 *  MidiHandler/MidiParser is not used: it corrupts the channel of
 *  running-status messages after real-time bytes (Clock/Active Sensing). */
template <typename Transport>
struct MidiInput
{
    Transport                  transport;
    SpscQueue<uint8_t, 1024>   bytes;
    MidiByteParser                 parser;
    MidiMessage                pending{};
    bool                       has_pending = false;

    static void RxCallback(uint8_t *data, size_t size, void *ctx)
    {
        auto *self = static_cast<MidiInput *>(ctx);
        for(size_t i = 0; i < size; i++)
            self->bytes.Push(data[i]); // on overflow bytes drop; parser resyncs
    }
    void Start() { transport.StartRx(RxCallback, this); }

    /** Main loop: parse queued bytes into the central Controller path. If
     *  the note queue is momentarily full, retry the message next time. */
    void Poll(uint32_t now)
    {
        if(!transport.RxActive())
        {
            transport.FlushRx();
            Start();
        }
        if(has_pending)
        {
            if(!controller.HandleMidi(pending, now))
                return;
            has_pending = false;
        }
        uint8_t b;
        while(bytes.Pop(b))
        {
            MidiMessage msg;
            if(!parser.Parse(b, msg))
                continue;
            if(!controller.HandleMidi(msg, now))
            {
                pending     = msg;
                has_pending = true;
                return;
            }
        }
    }
};

static MidiInput<MidiUartTransport> midi_din;
#if MIDI_ENABLE_USB
static MidiInput<MidiUsbTransport> midi_usb;
#endif

/** Settings backend on QSPI flash via libDaisy PersistentStorage.
 *  Save() erases + writes synchronously: main loop only. */
class QspiSettingsBackend : public SettingsBackend
{
  public:
    explicit QspiSettingsBackend(QSPIHandle &qspi) : storage_(qspi) {}

    void Init()
    {
        // On a blank chip PersistentStorage writes these defaults once.
        storage_.Init(MakeStoredSettings(DefaultParams()), SETTINGS_QSPI_OFFSET);
    }
    bool Read(StoredSettings &out) override
    {
        out = storage_.GetSettings();
        return true;
    }
    bool Write(const StoredSettings &s) override
    {
        storage_.GetSettings() = s;
        storage_.Save();
        return true;
    }

  private:
    PersistentStorage<StoredSettings> storage_;
};

static QspiSettingsBackend settings_backend(hw.qspi);

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    cpu_meter.OnBlockStart();
    ApplyBridgeToEngine(bridge, engine);
    engine.Process(in[0], in[1], out[0], out[1], size);
    cpu_meter.OnBlockEnd();
}

int main(void)
{
    hw.Init();

    // Flush denormals to zero in thread mode and in every exception
    // handler (FPDSCR is loaded into FPSCR on exception entry).
    FPU->FPDSCR |= FPU_FPDSCR_FZ_Msk;
    __set_FPSCR(__get_FPSCR() | FPU_FPDSCR_FZ_Msk);

    hw.SetAudioBlockSize(VOCODER_BLOCK_SIZE);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    const float sr = hw.AudioSampleRate();

    engine.Init(sr, VOCODER_BLOCK_SIZE);
    cpu_meter.Init(sr, VOCODER_BLOCK_SIZE);

    // Load settings (fresh/invalid -> compile-time defaults, INTERNAL_SYNTH).
    settings_backend.Init();
    controller.Init(settings_backend, bridge);

    MidiUartTransport::Config din_cfg; // USART1 31250 baud, RX = PB7 (Seed D14, pin 15)
    midi_din.transport.Init(din_cfg);
#if MIDI_ENABLE_USB
    MidiUsbTransport::Config usb_cfg;
    usb_cfg.periph = MidiUsbTransport::Config::INTERNAL;
    midi_usb.transport.Init(usb_cfg);
#endif

#if VOCODER_CPU_LOG
    hw.StartLog(false);
    uint32_t last_log = System::GetNow();
#endif

    hw.StartAudio(AudioCallback);
    midi_din.Start();
#if MIDI_ENABLE_USB
    midi_usb.Start();
#endif

    uint32_t last_clips  = engine.ClipEvents();
    uint32_t led_off_at  = 0;
    bool     led_on      = false;

    for(;;)
    {
        const uint32_t now = System::GetNow();

        midi_din.Poll(now);
#if MIDI_ENABLE_USB
        midi_usb.Poll(now);
#endif
        controller.Update(now);

        // Clip indicator: Seed user LED on for CLIP_LED_HOLD_MS after a clip.
        const uint32_t clips = engine.ClipEvents();
        if(clips != last_clips)
        {
            last_clips = clips;
            led_off_at = now + CLIP_LED_HOLD_MS;
            led_on     = true;
            hw.SetLed(true);
        }
        else if(led_on && (int32_t)(now - led_off_at) >= 0)
        {
            led_on = false;
            hw.SetLed(false);
        }

#if VOCODER_CPU_LOG
        if(now - last_log >= 1000)
        {
            last_log = now;
            hw.PrintLine("cpu avg " FLT_FMT3 "%% max " FLT_FMT3 "%% faults %u",
                         FLT_VAR3(cpu_meter.GetAvgCpuLoad() * 100.0f),
                         FLT_VAR3(cpu_meter.GetMaxCpuLoad() * 100.0f),
                         static_cast<unsigned>(engine.FaultEvents()));
        }
#endif
    }
}
