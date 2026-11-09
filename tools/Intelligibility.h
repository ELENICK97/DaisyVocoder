#pragma once
/**
 * Intelligibility.h — STOI-style objective intelligibility score (desktop
 * only). Follows Taal et al. 2011 (STOI): 1/3-octave band envelopes from a
 * short-time spectrum, silent-frame removal, 384 ms segments, normalisation
 * and clipping of the processed envelope, then the mean correlation between
 * clean and processed envelopes. Range ~0..1, higher = more intelligible.
 *
 * Differences from reference STOI: runs at 48 kHz instead of resampling to
 * 10 kHz (25 ms frames, 50% overlap), and optionally extends the bands up to
 * 8 kHz (the reference stops at ~4.3 kHz) to also cover sibilants.
 */
#include <cmath>
#include <complex>
#include <vector>

namespace intel
{
inline void Fft(std::vector<std::complex<double>> &a)
{
    const size_t n = a.size();
    for(size_t i = 1, j = 0; i < n; i++)
    {
        size_t bit = n >> 1;
        for(; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if(i < j)
            std::swap(a[i], a[j]);
    }
    for(size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for(size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0);
            for(size_t k = 0; k < len / 2; k++)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k]           = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** 1/3-octave band magnitudes per frame: result[frame][band]. */
inline std::vector<std::vector<double>> BandFrames(const std::vector<float> &x,
                                                   double sr,
                                                   int    num_bands,
                                                   std::vector<double> &frame_db)
{
    const size_t frame = static_cast<size_t>(0.025 * sr), hop = frame / 2, nfft = 2048;
    std::vector<double> win(frame);
    for(size_t i = 0; i < frame; i++)
        win[i] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * (i + 1) / (frame + 1));
    std::vector<std::vector<double>> out;
    frame_db.clear();
    for(size_t start = 0; start + frame <= x.size(); start += hop)
    {
        std::vector<std::complex<double>> a(nfft);
        double e = 0;
        for(size_t i = 0; i < frame; i++)
        {
            const double v = x[start + i] * win[i];
            a[i] = v;
            e += v * v;
        }
        frame_db.push_back(10.0 * std::log10(e + 1e-20));
        Fft(a);
        std::vector<double> bands(num_bands);
        for(int b = 0; b < num_bands; b++)
        {
            const double fc = 150.0 * std::pow(2.0, b / 3.0);
            const double lo = fc * std::pow(2.0, -1.0 / 6.0), hi = fc * std::pow(2.0, 1.0 / 6.0);
            double       acc = 0;
            for(size_t k = 0; k <= nfft / 2; k++)
            {
                const double f = k * sr / nfft;
                if(f >= lo && f < hi)
                    acc += std::norm(a[k]);
            }
            bands[b] = std::sqrt(acc);
        }
        out.push_back(bands);
    }
    return out;
}

/** num_bands = 15 is standard STOI (150 Hz - 4.3 kHz); 18 extends to ~8.6 kHz. */
inline double Stoi(const std::vector<float> &clean,
                   const std::vector<float> &proc,
                   double                    sr,
                   int                       num_bands = 15)
{
    std::vector<double> cdb, pdb;
    auto X = BandFrames(clean, sr, num_bands, cdb);
    auto Y = BandFrames(proc, sr, num_bands, pdb);
    const size_t nf = std::min(X.size(), Y.size());
    // Remove frames more than 40 dB below the loudest clean frame.
    double mx = -1e9;
    for(size_t i = 0; i < nf; i++)
        mx = std::fmax(mx, cdb[i]);
    std::vector<std::vector<double>> Xs, Ys;
    for(size_t i = 0; i < nf; i++)
        if(cdb[i] > mx - 40.0)
        {
            Xs.push_back(X[i]);
            Ys.push_back(Y[i]);
        }
    const size_t N    = 30; // 384 ms at 10 kHz STOI framing; ~375 ms here
    const double clip = std::pow(10.0, 15.0 / 20.0);
    double       sum  = 0;
    size_t       cnt  = 0;
    for(size_t m = N; m <= Xs.size(); m++)
        for(int b = 0; b < num_bands; b++)
        {
            double ex = 0, ey = 0;
            for(size_t k = m - N; k < m; k++)
            {
                ex += Xs[k][b] * Xs[k][b];
                ey += Ys[k][b] * Ys[k][b];
            }
            const double alpha = std::sqrt(ex / (ey + 1e-20));
            std::vector<double> xv(N), yv(N);
            double mxv = 0, myv = 0;
            for(size_t k = 0; k < N; k++)
            {
                xv[k] = Xs[m - N + k][b];
                yv[k] = std::fmin(alpha * Ys[m - N + k][b], (1.0 + clip) * xv[k]);
                mxv += xv[k];
                myv += yv[k];
            }
            mxv /= N;
            myv /= N;
            double sxy = 0, sxx = 0, syy = 0;
            for(size_t k = 0; k < N; k++)
            {
                sxy += (xv[k] - mxv) * (yv[k] - myv);
                sxx += (xv[k] - mxv) * (xv[k] - mxv);
                syy += (yv[k] - myv) * (yv[k] - myv);
            }
            sum += sxy / (std::sqrt(sxx * syy) + 1e-20);
            cnt++;
        }
    return cnt ? sum / cnt : 0.0;
}
} // namespace intel
