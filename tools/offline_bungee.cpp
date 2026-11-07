// Prueba fuera de linea con Bungee en modo "streaming", emulando un plugin:
// bloques fijos de entrada, cola de salida y silencio mientras la cola no
// tenga suficiente (asi se mide el retraso real).
// Uso: offline_bungee in.f32 out.f32 <semitonos> <ajuste_granos: -1|0|1>
#include <bungee/Bungee.h>
#include <bungee/Stream.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 5)
        return 1;
    const int sr = 48000;
    const double pitch = std::pow (2.0, std::atof (argv[3]) / 12.0);
    const int hopAdjust = std::atoi (argv[4]);

    std::FILE* f = std::fopen (argv[1], "rb");
    std::vector<float> in;
    float tmp[4096];
    size_t n;
    while ((n = std::fread (tmp, sizeof (float), 4096, f)) > 0)
        in.insert (in.end(), tmp, tmp + n);
    std::fclose (f);

    const int chunk = 256;
    Bungee::Stretcher<Bungee::Basic> stretcher ({ sr, sr }, 1, hopAdjust);
    Bungee::Stream<Bungee::Basic> stream (stretcher, chunk, 1);

    std::vector<float> obuf (chunk * 4);
    std::deque<float> fifo;
    std::vector<float> out;
    out.reserve (in.size());
    double maxLatency = 0.0;

    for (size_t pos = 0; pos < in.size(); pos += chunk)
    {
        const int len = (int) std::min<size_t> (chunk, in.size() - pos);
        const float* ip = in.data() + pos;
        float* op = obuf.data();
        const int got = stream.process (&ip, &op, len, (double) len, pitch);
        for (int i = 0; i < got; ++i)
            fifo.push_back (obuf[(size_t) i]);
        for (int i = 0; i < len; ++i)
        {
            if (! fifo.empty())
            {
                out.push_back (fifo.front());
                fifo.pop_front();
            }
            else
                out.push_back (0.0f);
        }
        if (pos > (size_t) sr)
            maxLatency = std::max (maxLatency, stream.latency());
    }
    std::fprintf (stderr, "latencia interna de Bungee: %.1f ms\n", 1000.0 * maxLatency / sr);

    f = std::fopen (argv[2], "wb");
    std::fwrite (out.data(), sizeof (float), out.size(), f);
    std::fclose (f);
    return 0;
}
