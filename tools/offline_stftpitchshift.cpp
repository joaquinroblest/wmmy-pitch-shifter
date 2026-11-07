// Prueba fuera de linea con stftPitchShift (MIT), basada en su ejemplo
// oficial examples/realtime.cpp. Procesa el archivo en bloques del tamano de
// la ventana de sintesis, como lo haria la tarjeta de audio.
// Uso: offline_stftpitchshift in.f32 out.f32 <semitonos> <ventana_analisis> <ventana_sintesis> [overlap]
#include <StftPitchShift/STFT.h>
#include <StftPitchShift/StftPitchShiftCore.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

using namespace stftpitchshift;

int main (int argc, char** argv)
{
    if (argc < 6)
        return 1;
    const double samplerate = 48000;
    const double factor = std::pow (2.0, std::atof (argv[3]) / 12.0);
    const size_t A = (size_t) std::atoi (argv[4]);
    const size_t S = (size_t) std::atoi (argv[5]);
    const size_t overlap = argc > 6 ? (size_t) std::atoi (argv[6]) : 4;
    const std::tuple<size_t, size_t> framesize = { A, S };
    const size_t hopsize = S / overlap;

    std::FILE* f = std::fopen (argv[1], "rb");
    std::vector<float> x;
    float tmp[4096];
    size_t n;
    while ((n = std::fread (tmp, sizeof (float), 4096, f)) > 0)
        x.insert (x.end(), tmp, tmp + n);
    std::fclose (f);

    std::vector<double> bufIn (A + S, 0.0), bufOut (A + S, 0.0);
    auto stft = std::make_shared<STFT<double>> (framesize, hopsize);
    auto core = std::make_shared<StftPitchShiftCore<double>> (framesize, hopsize, samplerate);
    core->factors ({ factor });
    core->quefrency (0.0);
    core->distortion (1);
    core->normalization (false);

    std::vector<float> y (x.size(), 0.0f);
    for (size_t i = 0; i + S <= x.size(); i += S)
    {
        std::copy (bufIn.begin() + (long) S, bufIn.end(), bufIn.begin());
        std::transform (x.begin() + (long) i, x.begin() + (long) (i + S), bufIn.begin() + (long) A,
                        [] (float v) { return (double) v; });

        (*stft) (bufIn, bufOut, [&] (std::span<std::complex<double>> dft) { core->shiftpitch (dft); });

        std::transform (bufOut.begin() - (long) S + (long) A, bufOut.end() - (long) S, y.begin() + (long) i,
                        [] (double v) { return (float) v; });
        std::copy (bufOut.begin() + (long) S, bufOut.end(), bufOut.begin());
        std::fill (bufOut.begin() + (long) A, bufOut.end(), 0.0);
    }

    f = std::fopen (argv[2], "wb");
    std::fwrite (y.data(), sizeof (float), y.size(), f);
    std::fclose (f);
    return 0;
}
