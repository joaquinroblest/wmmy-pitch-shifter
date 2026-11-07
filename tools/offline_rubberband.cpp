// Prueba fuera de linea con Rubber Band en modo tiempo real.
// Uso: offline_rubberband in.f32 out.f32 <semitonos> <motor: r2|r3> <ventana: short|std>
#include "rubberband/RubberBandStretcher.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using RubberBand::RubberBandStretcher;

int main (int argc, char** argv)
{
    if (argc < 6)
        return 1;
    const double sr = 48000.0;
    const double semis = std::atof (argv[3]);
    const bool r3 = std::strcmp (argv[4], "r3") == 0;
    const bool shortWin = std::strcmp (argv[5], "short") == 0;

    std::FILE* f = std::fopen (argv[1], "rb");
    std::vector<float> in;
    float tmp[4096];
    size_t n;
    while ((n = std::fread (tmp, sizeof (float), 4096, f)) > 0)
        in.insert (in.end(), tmp, tmp + n);
    std::fclose (f);

    int opts = RubberBandStretcher::OptionProcessRealTime | RubberBandStretcher::OptionPitchHighConsistency;
    opts |= r3 ? RubberBandStretcher::OptionEngineFiner : RubberBandStretcher::OptionEngineFaster;
    if (shortWin)
        opts |= RubberBandStretcher::OptionWindowShort;

    RubberBandStretcher rb ((size_t) sr, 1, opts, 1.0, std::pow (2.0, semis / 12.0));
    const int chunk = 256;
    rb.setMaxProcessSize (chunk);

    // Relleno inicial recomendado por la libreria, como haria el plugin.
    const int pad = (int) rb.getPreferredStartPad();
    std::vector<float> zeros ((size_t) pad, 0.0f);
    if (pad > 0)
    {
        const float* zp = zeros.data();
        rb.process (&zp, (size_t) pad, false);
    }

    // Igual que en un plugin: por cada bloque de entrada se entrega un bloque
    // de salida del mismo tamano, tomado de una cola. Mientras la cola no
    // tenga suficiente, sale silencio. Asi se mide el retraso real.
    std::vector<float> out;
    std::vector<float> fifo;
    size_t fifoRead = 0;
    std::vector<float> obuf (8192);
    // Descartar el relleno inicial que la libreria indica.
    int toDiscard = (int) rb.getStartDelay();
    for (size_t pos = 0; pos < in.size(); pos += chunk)
    {
        const int len = (int) std::min<size_t> (chunk, in.size() - pos);
        const float* ip = in.data() + pos;
        rb.process (&ip, (size_t) len, false);
        int avail;
        while ((avail = rb.available()) > 0)
        {
            float* op = obuf.data();
            const int got = (int) rb.retrieve (&op, (size_t) std::min (avail, 8192));
            int start = 0;
            if (toDiscard > 0)
            {
                start = std::min (toDiscard, got);
                toDiscard -= start;
            }
            fifo.insert (fifo.end(), obuf.begin() + start, obuf.begin() + got);
        }
        for (int i = 0; i < len; ++i)
            out.push_back (fifoRead < fifo.size() ? fifo[fifoRead++] : 0.0f);
    }
    std::fprintf (stderr, "pad=%d startDelay=%d\n", pad, (int) rb.getStartDelay());
    f = std::fopen (argv[2], "wb");
    std::fwrite (out.data(), sizeof (float), out.size(), f);
    std::fclose (f);
    return 0;
}
