#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "PeakEngine.h"
#include "rubberband/RubberBandStretcher.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

// Procesador del plugin. Tres modos:
//  - Rapido  (Rubber Band R3, ventana corta)
//  - Calidad (Rubber Band R3, ventana estandar)
//  - En vivo / En vivo Ultra (motor de picos propio, 10,7 ms / 8 ms; ver PeakEngine.h)
//
// Latencia: el plugin le informa a Logic el retraso real con
// setLatencySamples(). En Rapido/Calidad depende del intervalo; en En vivo es fijo.
class WmmyPitchShifterProcessor : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener,
                                  private juce::AsyncUpdater
{
public:
    WmmyPitchShifterProcessor();
    ~WmmyPitchShifterProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    enum Mode { modeRapido = 0, modeCalidad = 1, modeEnVivo = 2, modeEnVivoUltra = 3 };
    static bool isLiveMode (int m) { return m == modeEnVivo || m == modeEnVivoUltra; }

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    void buildEngine();             // crea el motor del modo actual (no tiempo real)
    float currentSemitones() const;
    double currentPitchScale() const;
    int actualLatency() const;      // retraso real en este momento
    int latencyForSemitones (float semitones) const;
    void updateReportedLatency();
    void pullFromRubberBand();

    void processRubberBand (juce::AudioBuffer<float>& buffer, int numSamples, int chans);
    void processLive (juce::AudioBuffer<float>& buffer, int numSamples, int chans);

    double sampleRate = 48000.0;
    int maxBlock = 512;
    int numChannels = 2;
    int activeMode = -1;

    // ---- Rubber Band (Rapido / Calidad) ----
    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher;
    double appliedScale = 1.0;
    int padSamples = 0;
    int margin = 0;                 // margen anti-cortes del modo actual
    std::array<int, 49> latencyTable {}; // retraso por semitono entero (-24..+24)

    // Cola de salida con indices absolutos: la muestra de salida numero t es
    // la muestra cruda numero t - pad - margen. Asi el retraso es exacto.
    juce::AudioBuffer<float> raw;
    int rawSize = 0;
    juce::int64 rawWritten = 0;
    juce::int64 outTime = 0;
    juce::AudioBuffer<float> retrieveBuffer;
    std::vector<float*> outPtrs;
    std::vector<const float*> inPtrs;

    // ---- En vivo ----
    std::vector<wmmy::PeakChannel> live;
    int liveLatency = 0;

    // ---- Comun ----
    std::atomic<int> reportedLatency { 0 };
    std::atomic<int> pendingLatency { -1 };

    juce::AudioBuffer<float> dryDelay;  // senal original retrasada (Mix < 100 %)
    int dryWrite = 0, dryDelaySize = 0;

    juce::SmoothedValue<float> mixSmoothed, gainSmoothed;

    std::atomic<float>* transposeParam = nullptr;
    std::atomic<float>* stepParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WmmyPitchShifterProcessor)
};
