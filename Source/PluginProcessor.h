#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "rubberband/RubberBandStretcher.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

// Procesador del plugin: envuelve Rubber Band (motor R3) en tiempo real.
//
// Latencia: Rubber Band tiene un retraso que depende del intervalo
// (mayor al bajar, menor al subir). El plugin lo informa a Logic con
// setLatencySamples() cada vez que cambia, para que Logic lo compense.
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

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    void buildEngine();           // crea Rubber Band segun el modo (no tiempo real)
    double currentPitchScale() const;
    float currentSemitones() const;
    int engineLatency() const;    // retraso real del motor en este momento
    int latencyForSemitones (float semitones) const; // desde la tabla precalculada
    void updateReportedLatency(); // avisa a Logic si el retraso cambio
    void pushToFifo (int numSamples);

    double sampleRate = 48000.0;
    int maxBlock = 512;
    int numChannels = 2;
    int activeMode = -1;

    // Margen extra para que la cola de salida nunca quede vacia.
    static constexpr int safetyMargin = 256;

    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher;
    double appliedScale = 1.0;
    std::atomic<int> reportedLatency { 0 };
    std::atomic<int> pendingLatency { -1 };

    // Retraso del motor para cada semitono entero de -24 a +24, calculado al
    // crear el motor. Permite avisar a Logic apenas se mueve la perilla, sin
    // esperar a que llegue audio (Logic no procesa pistas detenidas).
    std::array<int, 49> latencyTable {};

    // Cola circular con la senal procesada.
    juce::AudioBuffer<float> fifo;
    int fifoSize = 0, fifoRead = 0, fifoWrite = 0, fifoCount = 0;
    juce::AudioBuffer<float> retrieveBuffer;

    // Senal original retrasada, para mezclarla alineada (Mix < 100 %).
    juce::AudioBuffer<float> dryDelay;
    int dryWrite = 0, dryDelaySize = 0;

    std::vector<float*> outPtrs;
    std::vector<const float*> inPtrs;

    juce::SmoothedValue<float> mixSmoothed, gainSmoothed;

    std::atomic<float>* transposeParam = nullptr;
    std::atomic<float>* stepParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WmmyPitchShifterProcessor)
};
