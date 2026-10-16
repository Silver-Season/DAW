#pragma once

#include <complex>
#include <vector>

namespace daw::dsp
{

// Small in-place radix-2 FFT. Used offline (wavetable construction, tests), never on the audio thread.
void fft (std::vector<std::complex<double>>& data, bool inverse);

} // namespace daw::dsp
