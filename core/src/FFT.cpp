#include "daw/dsp/FFT.h"

#include <cassert>
#include <cmath>
#include <numbers>

namespace daw::dsp
{

void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    assert (n > 0 && (n & (n - 1)) == 0);

    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * std::numbers::pi / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wlen (std::cos (ang), std::sin (ang));

        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                auto u = a[i + k];
                auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse)
        for (auto& x : a)
            x /= (double) n;
}

} // namespace daw::dsp
