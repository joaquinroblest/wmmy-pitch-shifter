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
        StringArray { String::fromUTF8 ("R\xc3\xa1pido"), "Calidad" }, 0));

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
}

WmmyPitchShifterProcessor::~WmmyPitchShifterProcessor()
{
    apvts.removeParameterListener (idMode, this);
    cancelPendingUpdate();
}

bool WmmyPitchShifterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

double WmmyPitchShifterProcessor::currentPitchScale() const
{
    float st = transposeParam->load();
    if (stepParam->load() > 0.5f)
        st = std::round (st);
    return std::pow (2.0, (double) st / 12.0);
}

int WmmyPitchShifterProcessor::engineLatency() const
{
    if (stretcher == nullptr)
        return 0;
    return (int) stretcher->getPreferredStartPad() + (int) stretcher->getStartDelay() + safetyMargin;
}

void WmmyPitchShifterProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    maxBlock = juce::jmax (64, samplesPerBlock);
    numChannels = juce::jmax (1, getTotalNumOutputChannels());

    inPtrs.assign ((size_t) numChannels, nullptr);
    outPtrs.assign ((size_t) numChannels, nullptr);
    retrieveBuffer.setSize (numChannels, 8192);

    // Cola y linea de retraso con espacio de sobra (1,5 s).
    fifoSize = (int) (sampleRate * 1.5) + 4 * maxBlock;
    fifo.setSize (numChannels, fifoSize);
    dryDelaySize = fifoSize;
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

    int options = RubberBandStretcher::OptionProcessRealTime
                | RubberBandStretcher::OptionEngineFiner
                | RubberBandStretcher::OptionPitchHighConsistency
                | RubberBandStretcher::OptionChannelsTogether;
    if (mode == 0)
        options |= RubberBandStretcher::OptionWindowShort; // modo Rapido

    appliedScale = currentPitchScale();
    stretcher = std::make_unique<RubberBandStretcher> ((size_t) sampleRate, (size_t) numChannels,
                                                       options, 1.0, appliedScale);
    stretcher->setMaxProcessSize ((size_t) maxBlock);
    activeMode = mode;

    fifo.clear();
    dryDelay.clear();
    dryWrite = 0;

    // Margen inicial de silencio en la cola.
    fifoRead = 0;
    fifoWrite = safetyMargin;
    fifoCount = safetyMargin;

    // Relleno inicial que recomienda Rubber Band.
    juce::AudioBuffer<float> zeros (numChannels, maxBlock);
    zeros.clear();
    int remaining = (int) stretcher->getPreferredStartPad();
    while (remaining > 0)
    {
        const int len = juce::jmin (remaining, maxBlock);
        for (int c = 0; c < numChannels; ++c)
            inPtrs[(size_t) c] = zeros.getReadPointer (c);
        stretcher->process (inPtrs.data(), (size_t) len, false);
        pushToFifo (0);
        remaining -= len;
    }

    reportedLatency = engineLatency();
    setLatencySamples (reportedLatency);
}

void WmmyPitchShifterProcessor::pushToFifo (int)
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
            if (fifoCount >= fifoSize)
                break; // no deberia pasar: la cola es muy grande
            for (int c = 0; c < numChannels; ++c)
                fifo.setSample (c, fifoWrite, retrieveBuffer.getSample (c, i));
            fifoWrite = (fifoWrite + 1) % fifoSize;
            ++fifoCount;
        }
    }
}

void WmmyPitchShifterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());

    for (int c = chans; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    if (stretcher == nullptr || n == 0)
        return;

    // 1. Cambio de tono (Rubber Band lo acepta en tiempo real).
    const double scale = currentPitchScale();
    if (scale != appliedScale)
    {
        stretcher->setPitchScale (scale);
        appliedScale = scale;
        const int lat = engineLatency();
        if (lat != reportedLatency)
        {
            reportedLatency = lat;
            pendingLatency.store (lat);
            triggerAsyncUpdate(); // Logic se entera desde el hilo principal
        }
    }

    // 2. Guardar la senal original (para la mezcla) y alimentar el motor.
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

    for (int offset = 0; offset < n; offset += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - offset);
        for (int c = 0; c < numChannels; ++c)
            inPtrs[(size_t) c] = buffer.getReadPointer (juce::jmin (c, chans - 1)) + offset;
        stretcher->process (inPtrs.data(), (size_t) len, false);
        pushToFifo (len);
    }

    // 3. Salida: senal procesada desde la cola + original alineada.
    mixSmoothed.setTargetValue (mixParam->load() / 100.0f);
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    const int lat = reportedLatency;

    for (int i = 0; i < n; ++i)
    {
        const float mix = mixSmoothed.getNextValue();
        const float gain = gainSmoothed.getNextValue();
        const bool haveWet = fifoCount > 0;
        int dryIndex = (dryStart + i - lat) % dryDelaySize;
        if (dryIndex < 0)
            dryIndex += dryDelaySize;

        for (int c = 0; c < chans; ++c)
        {
            const float wet = haveWet ? fifo.getSample (c, fifoRead) : 0.0f;
            const float dry = dryDelay.getSample (c, dryIndex);
            buffer.setSample (c, i, gain * (wet * mix + dry * (1.0f - mix)));
        }

        if (haveWet)
        {
            fifoRead = (fifoRead + 1) % fifoSize;
            --fifoCount;
        }
    }
}

void WmmyPitchShifterProcessor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == idMode)
        triggerAsyncUpdate();
}

void WmmyPitchShifterProcessor::handleAsyncUpdate()
{
    // Cambio de modo: hay que recrear el motor (no se puede hacer en el hilo de audio).
    if (stretcher != nullptr && (int) modeParam->load() != activeMode)
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
