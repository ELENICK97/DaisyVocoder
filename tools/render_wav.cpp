/**
 * render_wav — run the firmware's audio Engine offline over a speech WAV.
 *
 *   render_wav <mic.wav> <out.wav> [options]
 *     --note N      MIDI note held as carrier pitch (repeat for a chord;
 *                   default 48 = C3)
 *     --mono        Mono voice mode (default Poly)
 *     --wave W      0 Saw, 1 Square, 2 Triangle, 3 Sine, 4 Noise
 *     --q Q  --atk MS  --rel MS  --noise L  --gain DB  --shift ST  --amount A
 *     --score       print STOI-style intelligibility scores (mic vs output)
 *
 * The input must be 48 kHz PCM16 or float32 (first channel used). The
 * output is mono PCM16. Uses exactly the same Engine code as the firmware,
 * so what you hear is what the Seed computes (except for analog I/O).
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../Engine.h"
#include "../VocoderConfig.h"
#include "Intelligibility.h"

static bool ReadWav(const char *path, std::vector<float> &out, uint32_t &sr)
{
    FILE *f = std::fopen(path, "rb");
    if(!f)
        return false;
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    size_t  n;
    while((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    if(d.size() < 12 || std::memcmp(&d[0], "RIFF", 4) || std::memcmp(&d[8], "WAVE", 4))
        return false;
    uint16_t fmt = 0, ch = 0, bits = 0;
    for(size_t p = 12; p + 8 <= d.size();)
    {
        uint32_t len;
        std::memcpy(&len, &d[p + 4], 4);
        if(!std::memcmp(&d[p], "fmt ", 4))
        {
            std::memcpy(&fmt, &d[p + 8], 2);
            std::memcpy(&ch, &d[p + 10], 2);
            std::memcpy(&sr, &d[p + 12], 4);
            std::memcpy(&bits, &d[p + 22], 2);
        }
        else if(!std::memcmp(&d[p], "data", 4))
        {
            const size_t frames = len / (ch * bits / 8);
            out.resize(frames);
            for(size_t i = 0; i < frames; i++)
            {
                const uint8_t *s = &d[p + 8 + i * ch * bits / 8];
                if(fmt == 1 && bits == 16)
                {
                    int16_t v;
                    std::memcpy(&v, s, 2);
                    out[i] = v / 32768.0f;
                }
                else if(fmt == 3 && bits == 32)
                    std::memcpy(&out[i], s, 4);
                else
                    return false;
            }
            return true;
        }
        p += 8 + len + (len & 1);
    }
    return false;
}

static bool WriteWav(const char *path, const std::vector<float> &x, uint32_t sr)
{
    FILE *f = std::fopen(path, "wb");
    if(!f)
        return false;
    const uint32_t data = static_cast<uint32_t>(x.size() * 2), riff = 36 + data, fmtlen = 16,
                   byterate = sr * 2;
    const uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmtlen, 4, 1, f);
    std::fwrite(&pcm, 2, 1, f);
    std::fwrite(&ch, 2, 1, f);
    std::fwrite(&sr, 4, 1, f);
    std::fwrite(&byterate, 4, 1, f);
    std::fwrite(&align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&data, 4, 1, f);
    for(float v : x)
    {
        const float   c = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
        const int16_t s = static_cast<int16_t>(c * 32767.0f);
        std::fwrite(&s, 2, 1, f);
    }
    std::fclose(f);
    return true;
}

int main(int argc, char **argv)
{
    if(argc < 3)
    {
        std::fprintf(stderr, "usage: render_wav <mic.wav> <out.wav> [options]\n");
        return 2;
    }
    std::vector<float> mic;
    uint32_t           sr = 0;
    if(!ReadWav(argv[1], mic, sr) || sr != 48000)
    {
        std::fprintf(stderr, "cannot read %s (need 48 kHz PCM16/float32)\n", argv[1]);
        return 2;
    }
    Params p     = DefaultParams();
    std::vector<int> notes;
    bool   score = false;
    for(int i = 3; i < argc; i++)
    {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? static_cast<float>(std::atof(argv[++i])) : 0.0f; };
        if(a == "--note") notes.push_back(static_cast<int>(next()));
        else if(a == "--mono") SetParam(p, PARAM_POLY, 0.0f);
        else if(a == "--wave") SetParam(p, PARAM_WAVEFORM, next());
        else if(a == "--q") SetParam(p, PARAM_FILTER_Q, next());
        else if(a == "--atk") SetParam(p, PARAM_VOC_ATTACK_MS, next());
        else if(a == "--rel") SetParam(p, PARAM_VOC_RELEASE_MS, next());
        else if(a == "--noise") SetParam(p, PARAM_NOISE_LEVEL, next());
        else if(a == "--gain") SetParam(p, PARAM_MIC_GAIN_DB, next());
        else if(a == "--shift") SetParam(p, PARAM_FORMANT_SHIFT_ST, next());
        else if(a == "--amount") SetParam(p, PARAM_ENV_AMOUNT, next());
        else if(a == "--score") score = true;
        else if(a == "--calibrate")
        {
            // Metric sanity: identical signal and unrelated noise.
            std::vector<float> noise(mic.size());
            uint32_t           s = 1;
            for(float &v : noise)
            {
                s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                v = static_cast<int32_t>(s) * (0.1f / 2147483648.0f);
            }
            std::printf("calibrate: self %.4f  noise %.4f\n",
                        intel::Stoi(mic, mic, sr), intel::Stoi(mic, noise, sr));
            return 0;
        }
    }

    if(notes.empty())
        notes.push_back(48);

    // Pad to whole blocks.
    const size_t blk = VOCODER_BLOCK_SIZE;
    mic.resize((mic.size() + blk - 1) / blk * blk, 0.0f);
    std::vector<float> ext(mic.size(), 0.0f), l(mic.size()), r(mic.size());

    static Engine e;
    e.Init(VOCODER_SAMPLE_RATE_HZ, blk);
    e.SetParams(p);
    for(int n : notes)
        e.NoteOn(static_cast<uint8_t>(n));
    for(size_t i = 0; i < mic.size(); i += blk)
        e.Process(&mic[i], &ext[i], &l[i], &r[i], blk);

    if(!WriteWav(argv[2], l, sr))
    {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 2;
    }
    if(score)
    {
        // Ripple: steady 120 Hz pulse-train vowel; how much of the applied
        // band envelopes fluctuates around its 50 ms average (voice pitch
        // leaking into the gains = roughness). Energy-weighted, in percent.
        std::vector<float> v(48000);
        dsp::Biquad        f1, f2, f3;
        f1.SetCoefs(dsp::BandpassCoefs(700.0f, 8.0f, 48000.0f));
        f2.SetCoefs(dsp::BandpassCoefs(1200.0f, 8.0f, 48000.0f));
        f3.SetCoefs(dsp::BandpassCoefs(2600.0f, 8.0f, 48000.0f));
        float ph = 0.0f;
        for(float &x : v)
        {
            ph += 120.0f / 48000.0f;
            float src = 0.0f;
            if(ph >= 1.0f)
            {
                ph -= 1.0f;
                src = 0.5f;
            }
            x = f1.Process(src) + 0.7f * f2.Process(src) + 0.4f * f3.Process(src);
        }
        e.Init(VOCODER_SAMPLE_RATE_HZ, blk);
        e.SetParams(p);
        for(int n : notes)
        e.NoteOn(static_cast<uint8_t>(n));
        const int nb = Vocoder::kNumBands;
        std::vector<std::vector<float>> envs(nb);
        std::vector<float> z(blk), ol(blk), orr(blk);
        for(size_t i = 0; i < v.size(); i += blk)
        {
            for(size_t k = 0; k < blk; k++)
            {
                e.Process(&v[i + k], &z[0], &ol[0], &orr[0], 1);
                if(i > 24000)
                    for(int b = 0; b < nb; b++)
                        envs[b].push_back(e.GetVocoder().AppliedEnvelope(b));
            }
        }
        double num = 0, den = 0;
        for(int b = 0; b < nb; b++)
        {
            const std::vector<float> &x = envs[b];
            const size_t w = 2400; // 50 ms moving average
            double mean = 0, dev = 0;
            for(float s : x)
                mean += s;
            mean /= x.size();
            double run = 0;
            for(size_t i = 0; i < x.size(); i++)
            {
                run += x[i];
                if(i >= w)
                    run -= x[i - w];
                if(i >= w)
                {
                    const double d = x[i] - run / w;
                    dev += d * d;
                }
            }
            dev = std::sqrt(dev / (x.size() - w));
            num += dev * mean;
            den += mean * mean;
        }
        std::printf("stoi15 %.4f  stoi18 %.4f  ripple %.1f%%\n",
                    intel::Stoi(mic, l, sr, 15),
                    intel::Stoi(mic, l, sr, 18),
                    100.0 * num / (den + 1e-20));
    }
    return 0;
}
