// Prueba fuera de linea del modo "En vivo" usando exactamente LiveEngine.h.
// Uso: offline_live in.f32 out.f32 <semitonos> [analisis] [sintesis] [overlap]
//      offline_live --impulse   (mide el retraso propio del motor)
#include "LiveEngineV1.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main (int argc, char** argv)
{
    const double sr = 48000.0;
    if (argc > 1 && std::strcmp (argv[1], "--impulse") == 0)
    {
        for (int st : { -24, -12, -5, 0, 7, 12, 24 })
        {
            wmmy::LiveChannel ch;
            ch.prepare (sr, 2048, 256, 8);
            ch.setFactor (std::pow (2.0, st / 12.0));
            const int n = (int) sr * 2, imp = (int) sr;
            std::vector<float> out ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                float x = 0.0f;
                if (i >= imp && i < imp + 48)
                    x = 0.8f * (float) std::sin (M_PI * (i - imp) / 48.0); // pulso de 1 ms
                out[(size_t) i] = ch.processSample (x);
            }
            int best = imp;
            float bv = 0.0f;
            for (int i = imp; i < n; ++i)
                if (std::fabs (out[(size_t) i]) > bv)
                {
                    bv = std::fabs (out[(size_t) i]);
                    best = i;
                }
            std::printf ("st=%+d  retraso total=%d muestras (%.1f ms); motor=%d\n", st, best - imp - 24,
                         1000.0 * (best - imp - 24) / sr, best - imp - 24 - 256);
        }
        return 0;
    }
    if (argc < 4)
        return 1;
    const double st = std::atof (argv[3]);
    const size_t A = argc > 4 ? (size_t) std::atoi (argv[4]) : 2048;
    const size_t S = argc > 5 ? (size_t) std::atoi (argv[5]) : 256;
    const size_t ov = argc > 6 ? (size_t) std::atoi (argv[6]) : 8;

    std::FILE* f = std::fopen (argv[1], "rb");
    std::vector<float> x;
    float tmp[4096];
    size_t n;
    while ((n = std::fread (tmp, sizeof (float), 4096, f)) > 0)
        x.insert (x.end(), tmp, tmp + n);
    std::fclose (f);

    wmmy::LiveChannel ch;
    ch.prepare (sr, A, S, ov);
    ch.setFactor (std::pow (2.0, st / 12.0));
    for (auto& v : x)
        v = ch.processSample (v);

    f = std::fopen (argv[2], "wb");
    std::fwrite (x.data(), sizeof (float), x.size(), f);
    std::fclose (f);
    return 0;
}
