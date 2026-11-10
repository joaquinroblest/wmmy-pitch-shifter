// Prueba fuera de linea del motor "En vivo" v2 usando exactamente Source/PeakEngine.h.
// Uso: offline_peak in.f32 out.f32 <semitonos> [N] [S] [R] [falda 0|1] [remuestreo 0|1]
#include "../Source/PeakEngine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 4)
        return 1;
    wmmy::PeakChannel::Config c;
    if (argc > 4) c.N = std::atoi (argv[4]);
    if (argc > 5) c.S = std::atoi (argv[5]);
    if (argc > 6) c.R = std::atoi (argv[6]);
    if (argc > 7) c.skirt = std::atoi (argv[7]) != 0;
    if (argc > 8) c.transientResample = std::atoi (argv[8]) != 0;

    std::FILE* f = std::fopen (argv[1], "rb");
    if (! f)
        return 1;
    std::vector<float> x;
    float tmp[4096];
    size_t n;
    while ((n = std::fread (tmp, sizeof (float), 4096, f)) > 0)
        x.insert (x.end(), tmp, tmp + n);
    std::fclose (f);

    wmmy::PeakChannel ch;
    ch.prepare (48000.0, c);
    ch.setRatio (std::pow (2.0, std::atof (argv[3]) / 12.0));
    const auto t0 = std::chrono::steady_clock::now();
    for (auto& v : x)
        v = ch.processSample (v);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::fprintf (stderr, "retraso %d muestras (%.1f ms); uso de procesador %.1f %% de tiempo real\n",
                  ch.latencySamples(), 1000.0 * ch.latencySamples() / 48000.0, 100.0 * secs / (x.size() / 48000.0));

    f = std::fopen (argv[2], "wb");
    std::fwrite (x.data(), sizeof (float), x.size(), f);
    std::fclose (f);
    return 0;
}
