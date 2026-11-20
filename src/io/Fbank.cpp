#include "io/Fbank.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numeric>
#include <numbers>

namespace mothion::io {

namespace {

constexpr int kLength = 400; // 25 ms
constexpr int kShift = 160;  // 10 ms
constexpr int kFft = 512;
constexpr int kSpectrum = kFft / 2 + 1;
constexpr double kPreemphasis = 0.97;
constexpr double kLowFreq = 20.0;
constexpr double kHighFreq = kFbankRate / 2.0 - 400.0;

double lin2mel(double f) { return 1127.0 * std::log(1.0 + f / 700.0); }

// What every frame shares: the window and the mel banks (kFbankBins rows of the power spectrum's bins).
struct Tables
{
    std::array<double, kLength> window{};
    std::vector<std::array<double, kSpectrum>> banks;

    Tables()
    {
        for (int i = 0; i < kLength; ++i) // Povey's: a Hann window (symmetric) to the power 0.85
            window[std::size_t(i)] = std::pow(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (kLength - 1)), 0.85);
        const double low = lin2mel(kLowFreq);
        const double delta = (lin2mel(kHighFreq) - low) / (kFbankBins + 1);
        const double binWidth = double(kFbankRate) / kFft;
        banks.resize(kFbankBins);
        for (int b = 0; b < kFbankBins; ++b) {
            const double left = low + b * delta, center = low + (b + 1) * delta, right = low + (b + 2) * delta;
            banks[std::size_t(b)].fill(0.0);
            for (int k = 0; k < kFft / 2; ++k) { // the Nyquist bin stays out, as in Kaldi
                const double mel = lin2mel(binWidth * k);
                const double weight = std::min((mel - left) / (center - left), (right - mel) / (right - center));
                banks[std::size_t(b)][std::size_t(k)] = std::max(0.0, weight);
            }
        }
    }
};

const Tables &tables()
{
    static const Tables t;
    return t;
}

// In-place radix-2 FFT of kFft points.
void fft(std::array<std::complex<double>, kFft> &a)
{
    for (int i = 1, j = 0; i < kFft; ++i) {
        int bit = kFft >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[std::size_t(i)], a[std::size_t(j)]);
    }
    for (int len = 2; len <= kFft; len <<= 1) {
        const double angle = -2.0 * std::numbers::pi / len;
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (int i = 0; i < kFft; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int k = 0; k < len / 2; ++k) {
                const auto u = a[std::size_t(i + k)];
                const auto v = a[std::size_t(i + k + len / 2)] * w;
                a[std::size_t(i + k)] = u + v;
                a[std::size_t(i + k + len / 2)] = u - v;
                w *= step;
            }
        }
    }
}

} // namespace

std::vector<float> kaldiFbank(const float *samples, std::size_t count, int *frames)
{
    const int n = int(count);
    const int frameCount = (n + kShift / 2) / kShift;
    if (frames)
        *frames = std::max(0, frameCount);
    std::vector<float> out;
    if (frameCount <= 0 || !samples)
        return out;
    // The sound reflected at its ends (each end's samples mirrored, the end one included), so that frame t is
    // centred on sample t * kShift + kShift / 2.
    const int padLeft = (kLength - kShift) / 2;
    auto sample = [&](int i) -> double {
        i -= padLeft;
        if (i < 0)
            i = -i - 1;
        if (i >= n)
            i = 2 * n - 1 - i;
        return samples[std::clamp(i, 0, n - 1)];
    };
    const Tables &t = tables();
    const double floor = std::numeric_limits<float>::epsilon();
    out.resize(std::size_t(frameCount) * kFbankBins);
    std::array<double, kLength> frame{};
    std::array<std::complex<double>, kFft> spectrum{};
    std::array<double, kSpectrum> power{};
    for (int f = 0; f < frameCount; ++f) {
        double mean = 0.0;
        for (int i = 0; i < kLength; ++i)
            mean += frame[std::size_t(i)] = sample(f * kShift + i);
        mean /= kLength;
        for (double &v : frame)
            v -= mean;
        // Pre-emphasis (the first sample against itself), then the window, zero-padded to the FFT's length.
        for (int i = kLength - 1; i >= 0; --i) {
            const double previous = frame[std::size_t(std::max(0, i - 1))];
            spectrum[std::size_t(i)] = (frame[std::size_t(i)] - kPreemphasis * previous) * t.window[std::size_t(i)];
        }
        std::fill(spectrum.begin() + kLength, spectrum.end(), 0.0);
        fft(spectrum);
        for (int k = 0; k < kSpectrum; ++k)
            power[std::size_t(k)] = std::norm(spectrum[std::size_t(k)]);
        float *row = out.data() + std::size_t(f) * kFbankBins;
        for (int b = 0; b < kFbankBins; ++b) {
            double energy = 0.0;
            const auto &bank = t.banks[std::size_t(b)];
            for (int k = 0; k < kSpectrum; ++k)
                energy += bank[std::size_t(k)] * power[std::size_t(k)];
            row[b] = float(std::log(std::max(energy, floor)));
        }
    }
    return out;
}

std::vector<float> resampled(const std::vector<float> &samples, int fromRate, int toRate)
{
    if (fromRate <= 0 || toRate <= 0 || fromRate == toRate || samples.empty())
        return samples;
    constexpr int kZeros = 16; // zero crossings of the sinc on each side
    const double ratio = double(fromRate) / toRate; // input samples per output sample
    const double cutoff = 0.95 * std::min(1.0, 1.0 / ratio); // of the input's Nyquist frequency, a margin below the lower one
    const double halfWidth = kZeros / cutoff;                // in input samples
    // The filter's weight for an input sample `d` samples from where the output one falls: a Hann-windowed sinc.
    auto weight = [&](double d) {
        if (std::abs(d) >= halfWidth)
            return 0.0;
        const double x = d * cutoff;
        const double sinc = std::abs(x) < 1e-9 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
        return sinc * (0.5 + 0.5 * std::cos(std::numbers::pi * d / halfWidth));
    };
    // Output sample i falls at input sample i * M / L: between two of them by one of L fractions (phases), each with a
    // filter of its own, computed once.
    const int g = std::gcd(fromRate, toRate);
    const std::int64_t up = toRate / g, down = fromRate / g;
    const auto n = std::int64_t(samples.size());
    std::vector<float> out(std::size_t(n * up / down));
    const int reach = int(std::ceil(halfWidth));
    const int taps = 2 * reach + 1;
    const bool tables = up <= 4096; // (rates sharing no large factor: the filter is made for each sample instead)
    std::vector<double> filters(tables ? std::size_t(up * taps) : 0);
    for (std::int64_t p = 0; tables && p < up; ++p)
        for (int j = 0; j < taps; ++j)
            filters[std::size_t(p * taps + j)] = weight(double(j - reach) - double(p) / double(up));
    std::vector<double> own(tables ? 0 : std::size_t(taps));
    for (std::size_t i = 0; i < out.size(); ++i) {
        const std::int64_t position = std::int64_t(i) * down;
        const std::int64_t base = position / up, phase = position % up;
        const double *filter = nullptr;
        if (tables) {
            filter = filters.data() + std::size_t(phase * taps);
        } else {
            for (int j = 0; j < taps; ++j)
                own[std::size_t(j)] = weight(double(j - reach) - double(phase) / double(up));
            filter = own.data();
        }
        const int first = int(std::max<std::int64_t>(0, reach - base));
        const int last = int(std::min<std::int64_t>(taps - 1, n - 1 - base + reach));
        double sum = 0.0, weights = 0.0; // (dividing by the weights used keeps the level at the ends, where some are missing)
        for (int j = first; j <= last; ++j) {
            sum += filter[j] * samples[std::size_t(base - reach + j)];
            weights += filter[j];
        }
        out[i] = float(weights != 0.0 ? sum / weights : 0.0);
    }
    return out;
}

} // namespace mothion::io
