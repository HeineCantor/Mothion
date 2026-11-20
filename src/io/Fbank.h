#pragma once

#include <cstddef>
#include <vector>

namespace mothion::io {

// Log mel filter banks, as Kaldi computes them with the options the ZIPA phone recognizer was trained on (lhotse's
// Fbank: 16 kHz, 25 ms frames every 10 ms, no snipped edges (the sound reflected at its ends), the DC offset removed,
// pre-emphasis 0.97, Povey window, 512-point FFT, power spectrum, 80 mel bins from 20 Hz to 7600 Hz, no dither).
inline constexpr int kFbankBins = 80;
inline constexpr int kFbankRate = 16000;
inline constexpr double kFbankShiftSeconds = 0.01;

// The features of `count` samples (16 kHz mono, -1..1): `frames` rows of kFbankBins, one every 10 ms
// ((count + 80) / 160 of them).
std::vector<float> kaldiFbank(const float *samples, std::size_t count, int *frames);

// `samples` (mono) at `fromRate` made `toRate`, with a windowed-sinc filter that keeps out what the new rate cannot
// hold (no aliasing when the rate is lowered).
std::vector<float> resampled(const std::vector<float> &samples, int fromRate, int toRate);

} // namespace mothion::io
