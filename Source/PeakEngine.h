// Motor "En vivo" v2 del WMMY Pitch Shifter.
//
// Es el traslado a C++ del prototipo lab/pv.py, afinado midiendo con guitarra
// real (lab/regla.py). Tecnicas publicas:
//   - Vocoder de fase con desplazamiento rigido de regiones alrededor de picos
//     (J. Laroche y M. Dolson, "New phase-vocoder techniques for pitch-shifting,
//     harmonizing and other exotic effects", WASPAA 1999).
//   - Ventana de analisis asimetrica (larga hacia el pasado, corta hacia el
//     futuro): el retraso es solo el largo de la ventana de sintesis (S).
//   - En los ataques: reinicio de fases anclado en el instante del ataque y
//     remuestreo directo de la entrada durante los primeros milisegundos.
//
// La transformada de Fourier es la de stftPitchShift (MIT, J. Hock).
// No depende de JUCE: el mismo codigo se usa en el plugin y en tools/offline_peak.cpp.
#pragma once

#include <StftPitchShift/RFFT.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <span>
#include <vector>

namespace wmmy
{

class PeakChannel
{
public:
    struct Config
    {
        int N = 4096;          // ventana de analisis
        int S = 512;           // ventana de sintesis = retraso en muestras
        int R = 64;            // salto entre cuadros
        bool skirt = true;     // descartar picos que son derrame de otro mas fuerte
        float onsetDb = 9.0f;  // sensibilidad del detector de ataques
        bool transientResample = true;
    };

    void prepare (double sampleRate, const Config& c)
    {
        cfg = c;
        sr = sampleRate;
        N = c.N;
        S = c.S;
        R = c.R;
        K = N / 2 + 1;

        const float pi = 3.14159265358979323846f;
        wa.resize ((size_t) N);
        const int rise = N - S / 2;
        for (int n = 0; n < rise; ++n)
        {
            const float s = std::sin (0.5f * pi * ((float) n + 0.5f) / (float) rise);
            wa[(size_t) n] = s * s;
        }
        for (int n = 0; n < S / 2; ++n)
        {
            const float c2 = std::cos (0.5f * pi * ((float) n + 0.5f) / (float) (S / 2));
            wa[(size_t) (rise + n)] = c2 * c2;
        }
        prod.resize ((size_t) S);
        ws.resize ((size_t) S);
        for (int i = 0; i < S; ++i)
        {
            const float s = std::sin (pi * ((float) i + 0.5f) / (float) S);
            prod[(size_t) i] = s * s;
            ws[(size_t) i] = prod[(size_t) i] / std::max (wa[(size_t) (N - S + i)], 1e-9f);
        }
        gain = 2.0f * (float) R / (float) S;

        // Falda de la ventana: cuanto "derrama" un pico a d bins de distancia.
        {
            std::vector<float> tmp (wa);
            std::vector<std::complex<float>> W ((size_t) K);
            fft.fft (std::span<const float> (tmp), std::span<std::complex<float>> (W));
            skirtTab.resize ((size_t) K);
            const float w0 = std::abs (W[0]);
            float running = 0.0f;
            for (int d = K - 1; d >= 0; --d)
            {
                running = std::max (running, std::abs (W[(size_t) d]) / w0);
                skirtTab[(size_t) d] = std::min (1.0f, 2.0f * running); // +6 dB de margen
            }
        }

        center.resize ((size_t) K);
        omegaBin.resize ((size_t) K);
        for (int k = 0; k < K; ++k)
        {
            omegaBin[(size_t) k] = 2.0f * pi * (float) k / (float) N;
            // exp(j*2*pi*k*nc/N) con nc = N - S/2  ==  exp(-j*pi*k*S/N)
            const double a = -3.14159265358979323846 * (double) k * (double) S / (double) N;
            center[(size_t) k] = { (float) std::cos (a), (float) std::sin (a) };
        }

        hist.assign ((size_t) N, 0.0f);
        frame.assign ((size_t) N, 0.0f);
        X.assign ((size_t) K, {});
        Y.assign ((size_t) K, {});
        mag.assign ((size_t) K, 0.0f);
        prevPhase.assign ((size_t) K, 0.0f);
        omega.assign ((size_t) K, 0.0f);
        phiBin.assign ((size_t) K, 0.0f);
        skirtLevel.assign ((size_t) K, 0.0f);
        region.assign ((size_t) K, 0);
        peaks.clear();
        peaks.reserve ((size_t) K);
        order.reserve ((size_t) K);
        pkDw.reserve ((size_t) K);
        pkPhi.reserve ((size_t) K);
        pkRot.reserve ((size_t) K);
        pkDk.reserve ((size_t) K);
        pkFrac.reserve ((size_t) K);
        acc.assign ((size_t) S, 0.0f);
        outFrame.assign ((size_t) S, 0.0f);
        pending.assign ((size_t) R, 0.0f);
        outQ.assign ((size_t) R, 0.0f);
        pos = 0;
        hold = 0;
        refractory = 0;
        framesSinceOnset = 0;
        past = std::min (N - R, (int) (0.02 * sr));
        holdFrames = S / R;
        refractoryFrames = (int) (0.05 * sr / (double) R);
        onsetThresh = std::pow (10.0f, c.onsetDb / 10.0f);
    }

    int latencySamples() const { return S; }

    void setRatio (double r) { beta = (float) r; }

    float processSample (float x)
    {
        pending[(size_t) pos] = x;
        const float y = outQ[(size_t) pos];
        if (++pos == R)
        {
            pos = 0;
            std::copy (hist.begin() + R, hist.end(), hist.begin());
            std::copy (pending.begin(), pending.end(), hist.end() - R);
            processFrame();
            std::copy (acc.begin(), acc.begin() + R, outQ.begin());
            std::copy (acc.begin() + R, acc.end(), acc.begin());
            std::fill (acc.end() - R, acc.end(), 0.0f);
        }
        return y;
    }

private:
    static float princarg (float p)
    {
        const float twoPi = 6.28318530717958647692f;
        return p - twoPi * std::round (p / twoPi);
    }

    void processFrame()
    {
        const float pi = 3.14159265358979323846f;
        const float twoPi = 2.0f * pi;

        // ---- detector de ataques: energia del salto nuevo contra los ~20 ms previos ----
        bool doReset = false;
        {
            float eNew = 0.0f, eOld = 0.0f;
            for (int n = N - R; n < N; ++n)
                eNew += hist[(size_t) n] * hist[(size_t) n];
            for (int n = N - R - past; n < N - R; ++n)
                eOld += hist[(size_t) n] * hist[(size_t) n];
            eNew /= (float) R;
            eOld /= (float) past;
            if (refractory > 0)
                --refractory;
            else if (eNew > onsetThresh * eOld + 1e-7f)
            {
                doReset = true;
                hold = holdFrames;
                refractory = refractoryFrames;
                framesSinceOnset = 0;
            }
        }
        const bool inHold = hold > 0;

        // ---- analisis ----
        for (int n = 0; n < N; ++n)
            frame[(size_t) n] = hist[(size_t) n] * wa[(size_t) n];
        fft.fft (std::span<const float> (frame), std::span<std::complex<float>> (X));

        float maxMag = 0.0f;
        for (int k = 0; k < K; ++k)
        {
            const float re = X[(size_t) k].real(), im = X[(size_t) k].imag();
            const float m = std::sqrt (re * re + im * im);
            mag[(size_t) k] = m;
            maxMag = std::max (maxMag, m);
            const float ph = std::atan2 (im, re);
            const float d = princarg (ph - prevPhase[(size_t) k] - omegaBin[(size_t) k] * (float) R);
            omega[(size_t) k] = omegaBin[(size_t) k] + d / (float) R;
            prevPhase[(size_t) k] = ph;
        }

        std::fill (Y.begin(), Y.end(), std::complex<float> {});
        const float floorAbs = 3.1622776e-4f * maxMag + 1e-12f; // -70 dB bajo el maximo

        // ---- picos: maximos locales sobre +-2 bins ----
        peaks.clear();
        for (int k = 1; k < K; ++k)
        {
            const float m = mag[(size_t) k];
            if (m <= floorAbs)
                continue;
            bool isPeak = true;
            for (int s = 1; s <= 2 && isPeak; ++s)
            {
                if (k - s >= 0 && mag[(size_t) (k - s)] > m)
                    isPeak = false;
                if (k + s < K && mag[(size_t) (k + s)] > m)
                    isPeak = false;
            }
            if (isPeak)
                peaks.push_back (k);
        }

        if (cfg.skirt && peaks.size() > 1)
        {
            // de mayor a menor: un pico se acepta solo si supera la falda de los ya aceptados
            order.assign (peaks.begin(), peaks.end());
            std::sort (order.begin(), order.end(), [this] (int a, int b) { return mag[(size_t) a] > mag[(size_t) b]; });
            std::fill (skirtLevel.begin(), skirtLevel.end(), 0.0f);
            peaks.clear();
            for (int q : order)
            {
                const float mq = mag[(size_t) q];
                if (mq <= skirtLevel[(size_t) q])
                    continue;
                peaks.push_back (q);
                for (int d = 0; d < K; ++d)
                {
                    const float lvl = mq * skirtTab[(size_t) d];
                    if (lvl < floorAbs)
                        break;
                    if (q + d < K)
                        skirtLevel[(size_t) (q + d)] = std::max (skirtLevel[(size_t) (q + d)], lvl);
                    if (q - d >= 0)
                        skirtLevel[(size_t) (q - d)] = std::max (skirtLevel[(size_t) (q - d)], lvl);
                }
            }
            std::sort (peaks.begin(), peaks.end());
        }

        const int P = (int) peaks.size();
        if (P > 0)
        {
            // ---- regiones: cada bin pertenece al pico mas cercano ----
            {
                int p = 0;
                for (int k = 0; k < K; ++k)
                {
                    while (p + 1 < P && k >= (peaks[(size_t) p] + peaks[(size_t) (p + 1)] + 1) / 2)
                        ++p;
                    region[(size_t) k] = p;
                }
            }

            // ---- desplazamiento y fase de rotacion de cada pico ----
            // tiempo desde el ataque hasta el centro de la ventana de sintesis de este cuadro
            const float sinceOnset = (float) framesSinceOnset * (float) R + 0.5f * (float) R - 0.5f * (float) S;
            pkDw.resize ((size_t) P);
            pkPhi.resize ((size_t) P);
            pkRot.resize ((size_t) P);
            pkDk.resize ((size_t) P);
            pkFrac.resize ((size_t) P);
            for (int p = 0; p < P; ++p)
            {
                const int kp = peaks[(size_t) p];
                const float w = inHold ? omegaBin[(size_t) kp] : omega[(size_t) kp];
                const float dw = (beta - 1.0f) * w;
                float phi;
                if (doReset || inHold)
                    phi = dw * sinceOnset;                       // rotacion nula en el instante del ataque
                else
                    phi = phiBin[(size_t) kp] + dw * (float) R;  // continua la de la region anterior
                phi = princarg (phi);
                const float dkf = dw * (float) N / twoPi;
                const float fl = std::floor (dkf);
                pkDw[(size_t) p] = dw;
                pkPhi[(size_t) p] = phi;
                pkRot[(size_t) p] = { std::cos (phi), std::sin (phi) };
                pkDk[(size_t) p] = (int) fl;
                pkFrac[(size_t) p] = dkf - fl;
            }

            // ---- mover cada region completa (interpolacion lineal entre dos bins) ----
            for (int k = 0; k < K; ++k)
            {
                const int p = region[(size_t) k];
                phiBin[(size_t) k] = pkPhi[(size_t) p];
                const int tgt = k + pkDk[(size_t) p];
                if (tgt < 1 || tgt >= K - 2)
                    continue;
                const std::complex<float> z = X[(size_t) k] * center[(size_t) k] * pkRot[(size_t) p];
                const float a = pkFrac[(size_t) p];
                Y[(size_t) tgt] += z * (1.0f - a);
                Y[(size_t) (tgt + 1)] += z * a;
            }
            for (int k = 0; k < K; ++k)
                Y[(size_t) k] *= std::conj (center[(size_t) k]);
        }

        // ---- sintesis ----
        fft.ifft (std::span<const std::complex<float>> (Y), std::span<float> (frame));
        for (int i = 0; i < S; ++i)
            outFrame[(size_t) i] = frame[(size_t) (N - S + i)] * ws[(size_t) i] * gain;

        if (cfg.transientResample && inHold && beta != 1.0f)
        {
            // Durante el ataque: remuestreo directo de la entrada alrededor del instante del
            // ataque (coherente con el vocoder, que esta anclado en ese mismo instante).
            const float t0 = (float) N - 0.5f * (float) R - (float) framesSinceOnset * (float) R; // en el cuadro
            const float last = (float) (N - 2);
            int firstMissing = S;
            for (int i = 0; i < S; ++i)
                if (t0 + beta * ((float) (N - S + i) - t0) > last)
                {
                    firstMissing = i;
                    break;
                }
            const int xf = 32;
            for (int i = 0; i < firstMissing; ++i)
            {
                float sp = t0 + beta * ((float) (N - S + i) - t0);
                sp = std::clamp (sp, 1.0f, last);
                const int i0 = (int) sp;
                const float fr = sp - (float) i0;
                const float xm1 = hist[(size_t) (i0 - 1)], x0 = hist[(size_t) i0], x1 = hist[(size_t) (i0 + 1)];
                const float x2 = hist[(size_t) std::min (i0 + 2, N - 1)];
                const float c1 = 0.5f * (x1 - xm1);
                const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
                const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                const float res = ((c3 * fr + c2) * fr + c1) * fr + x0;
                float w = 1.0f;
                if (firstMissing < S && i >= firstMissing - xf)
                    w = (float) (firstMissing - i) / (float) xf;
                outFrame[(size_t) i] = w * res * prod[(size_t) i] * gain + (1.0f - w) * outFrame[(size_t) i];
            }
        }

        for (int i = 0; i < S; ++i)
            acc[(size_t) i] += outFrame[(size_t) i];

        if (hold > 0)
            --hold;
        ++framesSinceOnset;
    }

    Config cfg;
    double sr = 48000.0;
    int N = 4096, S = 512, R = 64, K = 2049;
    float beta = 1.0f, gain = 1.0f, onsetThresh = 8.0f;
    int pos = 0, hold = 0, refractory = 0, framesSinceOnset = 0, past = 960, holdFrames = 8, refractoryFrames = 37;

    stftpitchshift::TRFFT<float> fft;
    std::vector<float> wa, ws, prod, skirtTab, omegaBin;
    std::vector<std::complex<float>> center, X, Y, pkRot;
    std::vector<float> hist, frame, mag, prevPhase, omega, phiBin, skirtLevel, acc, outFrame, pending, outQ;
    std::vector<float> pkDw, pkPhi, pkFrac;
    std::vector<int> region, peaks, order, pkDk;
};

} // namespace wmmy
