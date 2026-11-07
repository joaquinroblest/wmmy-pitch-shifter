#include "PluginProcessor.h"

#include <cmath>

using RubberBand::RubberBandStretcher;

namespace
{
const juce::String idTranspose { "transpose" };
const juce::String idStep { "step" };
const juce::String idMode { "mode" };
const juce::String idMix { "mix" };
const juce::String idOutput { "output" };

// Margen anti-cortes para Rubber Band, en muestras a 48 kHz. Se obtuvo
// simulando la cola del plugin con buferes de 32 a 2048 muestras
// (tools/: prueba "exact"); es el minimo sin cortes en todos los casos.
constexpr int marginRapido48k = 256;
constexpr int marginCalidad48k = 256;

// Modo En vivo (a 48 kHz): ventana de analisis 2048, de sintesis 256,
// solapamiento 8. Retraso medido alineando la envolvente: ~720 muestras (15 ms).
constexpr int liveAnalysis48k = 2048;
constexpr int liveSynthesis48k = 256;
constexpr int liveOverlap = 8;
constexpr int liveLatency48k = 720;
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout WmmyPitchShifterProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { idTranspose, 1 }, "Transpose",
        NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("st")));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { idStep, 1 }, "Step", true));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { idMode, 1 }, "Modo",
        StringArray { String::fromUTF8 ("R\xc3\xa1pido"), "Calidad", "En vivo" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { idMix, 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { idOutput, 1 }, "Salida",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}

WmmyPitchShifterProcessor::WmmyPitchShifterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    transposeParam = apvts.getRawParameterValue (idTranspose);
    stepParam = apvts.getRawParameterValue (idStep);
    modeParam = apvts.getRawParameterValue (idMode);
    mixParam = apvts.getRawParameterValue (idMix);
    outputParam = apvts.getRawParameterValue (idOutput);

    apvts.addParameterListener (idMode, this);
    apvts.addParameterListener (idTranspose, this);
    apvts.addParameterListener (idStep, this);
}

WmmyPitchShifterProcessor::~WmmyPitchShifterProcessor()
{
    apvts.removeParameterListener (idMode, this);
    apvts.removeParameterListener (idTranspose, this);
    apvts.removeParameterListener (idStep, this);
    cancelPendingUpdate();
}

bool WmmyPitchShifterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

float WmmyPitchShifterProcessor::currentSemitones() const
{
    float st = transposeParam->load();
    if (stepParam->load() > 0.5f)
        st = std::round (st);
    return juce::jlimit (-24.0f, 24.0f, st);
}

double WmmyPitchShifterProcessor::currentPitchScale() const
{
    return std::pow (2.0, (double) currentSemitones() / 12.0);
}

int WmmyPitchShifterProcessor::actualLatency() const
{
    if (activeMode == modeEnVivo)
        return liveLatency;
    if (stretcher == nullptr)
        return 0;
    return padSamples + (int) stretcher->getStartDelay() + margin;
}

int WmmyPitchShifterProcessor::latencyForSemitones (float semitones) const
{
    if (activeMode == modeEnVivo)
        return liveLatency;
    const float pos = juce::jlimit (0.0f, 48.0f, semitones + 24.0f);
    const int i = juce::jmin (47, (int) std::floor (pos));
    const float frac = pos - (float) i;
    return (int) std::lround ((1.0f - frac) * (float) latencyTable[(size_t) i]
                              + frac * (float) latencyTable[(size_t) i + 1]);
}

void WmmyPitchShifterProcessor::updateReportedLatency()
{
    const int lat = latencyForSemitones (currentSemitones());
    if (reportedLatency.exchange (lat) != lat)
    {
        pendingLatency.store (lat);
        triggerAsyncUpdate();
    }
}

void WmmyPitchShifterProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    maxBlock = juce::jmax (64, samplesPerBlock);
    numChannels = juce::jmax (1, getTotalNumOutputChannels());

    inPtrs.assign ((size_t) numChannels, nullptr);
    outPtrs.assign ((size_t) numChannels, nullptr);
    retrieveBuffer.setSize (numChannels, 8192);

    // Espacio de sobra (1,5 s) para la cola y la linea de retraso.
    rawSize = (int) (sampleRate * 1.5) + 4 * maxBlock;
    raw.setSize (numChannels, rawSize);
    dryDelaySize = rawSize;
    dryDelay.setSize (numChannels, dryDelaySize);

    mixSmoothed.reset (sampleRate, 0.05);
    mixSmoothed.setCurrentAndTargetValue (mixParam->load() / 100.0f);
    gainSmoothed.reset (sampleRate, 0.05);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));

    buildEngine();
}

void WmmyPitchShifterProcessor::buildEngine()
{
    const int mode = (int) modeParam->load();
    activeMode = mode;
    dryDelay.clear();
    dryWrite = 0;

    // Factor para frecuencias de muestreo altas (88,2 / 96 kHz y mas).
    const int k = sampleRate > 60000.0 ? (sampleRate > 120000.0 ? 4 : 2) : 1;

    if (mode == modeEnVivo)
    {
        stretcher.reset();
        live.clear();
        live.resize ((size_t) numChannels);
        for (auto& ch : live)
        {
            ch.prepare (sampleRate, (size_t) (liveAnalysis48k * k), (size_t) (liveSynthesis48k * k),
                        (size_t) liveOverlap);
            ch.setFactor (currentPitchScale());
        }
        liveLatency = liveLatency48k * k;
    }
    else
    {
        live.clear();
        int options = RubberBandStretcher::OptionProcessRealTime
                    | RubberBandStretcher::OptionEngineFiner
                    | RubberBandStretcher::OptionPitchHighConsistency
                    | RubberBandStretcher::OptionChannelsTogether;
        if (mode == modeRapido)
            options |= RubberBandStretcher::OptionWindowShort;

        margin = (mode == modeRapido ? marginRapido48k : marginCalidad48k) * k;

        // Tabla de retrasos, medida con un motor de prueba que nunca procesa audio.
        {
            RubberBandStretcher probe ((size_t) sampleRate, (size_t) numChannels, options, 1.0, 1.0);
            probe.setMaxProcessSize ((size_t) maxBlock);
            for (int i = 0; i <= 48; ++i)
            {
                probe.setPitchScale (std::pow (2.0, (double) (i - 24) / 12.0));
                latencyTable[(size_t) i] = (int) probe.getPreferredStartPad()
                                         + (int) probe.getStartDelay() + margin;
            }
        }

        appliedScale = currentPitchScale();
        stretcher = std::make_unique<RubberBandStretcher> ((size_t) sampleRate, (size_t) numChannels,
                                                           options, 1.0, appliedScale);
        stretcher->setMaxProcessSize ((size_t) maxBlock);
        padSamples = (int) stretcher->getPreferredStartPad();

        raw.clear();
        rawWritten = 0;
        outTime = 0;

        // Relleno inicial que recomienda Rubber Band.
        juce::AudioBuffer<float> zeros (numChannels, maxBlock);
        zeros.clear();
        int remaining = padSamples;
        while (remaining > 0)
        {
            const int len = juce::jmin (remaining, maxBlock);
            for (int c = 0; c < numChannels; ++c)
                inPtrs[(size_t) c] = zeros.getReadPointer (c);
            stretcher->process (inPtrs.data(), (size_t) len, false);
            pullFromRubberBand();
            remaining -= len;
        }
    }

    const int lat = latencyForSemitones (currentSemitones());
    reportedLatency.store (lat);
    pendingLatency.store (-1);
    setLatencySamples (lat);
}

void WmmyPitchShifterProcessor::pullFromRubberBand()
{
    int avail;
    while ((avail = stretcher->available()) > 0)
    {
        const int want = juce::jmin (avail, retrieveBuffer.getNumSamples());
        for (int c = 0; c < numChannels; ++c)
            outPtrs[(size_t) c] = retrieveBuffer.getWritePointer (c);
        const int got = (int) stretcher->retrieve (outPtrs.data(), (size_t) want);
        if (got <= 0)
            break;
        for (int i = 0; i < got; ++i)
        {
            const int w = (int) ((rawWritten + i) % rawSize);
            for (int c = 0; c < numChannels; ++c)
                raw.setSample (c, w, retrieveBuffer.getSample (c, i));
        }
        rawWritten += got;
    }
}

void WmmyPitchShifterProcessor::processRubberBand (juce::AudioBuffer<float>& buffer, int n, int chans)
{
    const double scale = currentPitchScale();
    if (scale != appliedScale)
    {
        stretcher->setPitchScale (scale);
        appliedScale = scale;
    }

    for (int offset = 0; offset < n; offset += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - offset);
        for (int c = 0; c < numChannels; ++c)
            inPtrs[(size_t) c] = buffer.getReadPointer (juce::jmin (c, chans - 1)) + offset;
        stretcher->process (inPtrs.data(), (size_t) len, false);
        pullFromRubberBand();
    }

    // Salida numero t = muestra cruda numero t - pad - margen (retraso exacto).
    for (int i = 0; i < n; ++i, ++outTime)
    {
        const juce::int64 r = outTime - padSamples - margin;
        const bool ok = r >= 0 && r < rawWritten && r >= rawWritten - rawSize;
        const int ri = ok ? (int) (r % rawSize) : 0;
        for (int c = 0; c < chans; ++c)
            buffer.setSample (c, i, ok ? raw.getSample (c, ri) : 0.0f);
    }
}

void WmmyPitchShifterProcessor::processLive (juce::AudioBuffer<float>& buffer, int n, int chans)
{
    const double scale = currentPitchScale();
    for (int c = 0; c < chans; ++c)
    {
        auto& ch = live[(size_t) c];
        ch.setFactor (scale);
        float* data = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            data[i] = ch.processSample (data[i]);
    }
}

void WmmyPitchShifterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());

    for (int c = chans; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    if (n == 0 || (activeMode == modeEnVivo ? live.empty() : stretcher == nullptr))
        return;

    // 1. Guardar la senal original para la mezcla.
    const int dryStart = dryWrite;
    for (int c = 0; c < chans; ++c)
    {
        const float* src = buffer.getReadPointer (c);
        int w = dryStart;
        for (int i = 0; i < n; ++i)
        {
            dryDelay.setSample (c, w, src[i]);
            w = (w + 1) % dryDelaySize;
        }
    }
    dryWrite = (dryStart + n) % dryDelaySize;

    // 2. Cambio de tono (deja la senal procesada en 'buffer').
    if (activeMode == modeEnVivo)
        processLive (buffer, n, chans);
    else
        processRubberBand (buffer, n, chans);

    // 3. Mezcla con la original alineada y volumen de salida.
    mixSmoothed.setTargetValue (mixParam->load() / 100.0f);
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    const int lat = actualLatency();

    for (int i = 0; i < n; ++i)
    {
        const float mix = mixSmoothed.getNextValue();
        const float gain = gainSmoothed.getNextValue();
        int dryIndex = (dryStart + i - lat) % dryDelaySize;
        if (dryIndex < 0)
            dryIndex += dryDelaySize;
        for (int c = 0; c < chans; ++c)
        {
            const float wet = buffer.getSample (c, i);
            const float dry = dryDelay.getSample (c, dryIndex);
            buffer.setSample (c, i, gain * (wet * mix + dry * (1.0f - mix)));
        }
    }
}

void WmmyPitchShifterProcessor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == idMode)
        triggerAsyncUpdate();
    else
        updateReportedLatency(); // Transpose o Step: avisar a Logic de inmediato
}

void WmmyPitchShifterProcessor::handleAsyncUpdate()
{
    // Cambio de modo: se recrea el motor fuera del hilo de audio.
    const bool engineReady = stretcher != nullptr || ! live.empty();
    if (engineReady && (int) modeParam->load() != activeMode)
    {
        suspendProcessing (true);
        buildEngine();
        suspendProcessing (false);
    }

    const int lat = pendingLatency.exchange (-1);
    if (lat >= 0)
        setLatencySamples (lat);
}

juce::AudioProcessorEditor* WmmyPitchShifterProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void WmmyPitchShifterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void WmmyPitchShifterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WmmyPitchShifterProcessor();
}
