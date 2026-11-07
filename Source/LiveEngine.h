// Modo "En vivo": cambio de tono de baja latencia con stftPitchShift (MIT,
// https://github.com/jurihock/stftPitchShift). Adaptado de su ejemplo oficial
// examples/realtime.cpp: ventana de analisis larga y ventana de sintesis corta,
// que es lo que permite un retraso bajo.
//
// No depende de JUCE: el mismo codigo se usa en el plugin y en la prueba fuera
// de linea (tools/offline_live.cpp).
#pragma once

#include <StftPitchShift/STFT.h>
#include <StftPitchShift/StftPitchShiftCore.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

namespace wmmy
{

class LiveChannel
{
public:
    // analysis/synthesis en muestras (potencias de dos). overlap >= 4.
    void prepare (double sampleRate, size_t analysis, size_t synthesis, size_t overlap)
    {
        A = analysis;
        S = synthesis;
        const auto framesize = std::make_tuple (A, S);
        const size_t hop = S / overlap;

        stft = std::make_unique<stftpitchshift::STFT<double>> (framesize, hop);
        core = std::make_unique<stftpitchshift::StftPitchShiftCore<double>> (framesize, hop, sampleRate);
        core->quefrency (0.0);
        core->distortion (1.0);
        core->normalization (false);
        currentFactor = 1.0;
        core->factors ({ currentFactor });

        bufIn.assign (A + S, 0.0);
        bufOut.assign (A + S, 0.0);
        chunkIn.assign (S, 0.0f);
        chunkOut.assign (S, 0.0f);
        pos = 0;
    }

    // Retraso total en muestras: bloque de entrada (S) + el propio del motor.
    // El valor del motor se midio con un impulso (ver tools/offline_live.cpp).
    size_t latencySamples() const { return S + engineDelay; }
    void setEngineDelay (size_t d) { engineDelay = d; }

    void setFactor (double f)
    {
        if (f != currentFactor)
        {
            currentFactor = f;
            core->factors ({ f });
        }
    }

    float processSample (float x)
    {
        chunkIn[pos] = x;
        const float y = chunkOut[pos];
        if (++pos == S)
        {
            processChunk();
            pos = 0;
        }
        return y;
    }

private:
    void processChunk()
    {
        std::copy (bufIn.begin() + (long) S, bufIn.end(), bufIn.begin());
        std::transform (chunkIn.begin(), chunkIn.end(), bufIn.begin() + (long) A,
                        [] (float v) { return (double) v; });

        (*stft) (bufIn, bufOut, [this] (std::span<std::complex<double>> dft) { core->shiftpitch (dft); });

        std::transform (bufOut.begin() - (long) S + (long) A, bufOut.end() - (long) S, chunkOut.begin(),
                        [] (double v) { return (float) v; });
        std::copy (bufOut.begin() + (long) S, bufOut.end(), bufOut.begin());
        std::fill (bufOut.begin() + (long) A, bufOut.end(), 0.0);
    }

    size_t A = 2048, S = 256, pos = 0, engineDelay = 0;
    double currentFactor = 1.0;
    std::unique_ptr<stftpitchshift::STFT<double>> stft;
    std::unique_ptr<stftpitchshift::StftPitchShiftCore<double>> core;
    std::vector<double> bufIn, bufOut;
    std::vector<float> chunkIn, chunkOut;
};

} // namespace wmmy
