#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LooperEngine.h"

class LoopRollAudioProcessor final : public juce::AudioProcessor
{
public:
    //==========================================================================
    // Identifiants des paramètres (ne pas les renommer : les projets DAW sauvegardés en dépendent)
    static constexpr const char* loopId     = "loop";
    static constexpr const char* modeId     = "mode";
    static constexpr const char* timeId     = "time";
    static constexpr const char* divisionId = "division";
    static constexpr const char* refreshId         = "refresh";
    static constexpr const char* refreshTimeId     = "refreshTime";
    static constexpr const char* refreshDivisionId = "refreshDivision";

    // Plages du mode free (ms)
    static constexpr float minTimeMs = 10.0f;
    static constexpr float maxTimeMs = 4000.0f;
    static constexpr float minRefreshMs = 50.0f;
    static constexpr float maxRefreshMs = 16000.0f;

    // Les deux potards vont du plus long (à gauche) au plus court (à droite).

    /** Divisions de la boucle en mode sync, de la plus longue à la plus courte. */
    static const juce::StringArray& getDivisionNames();
    /** Durée d'une division en noires (1/4 = 1). */
    static double getDivisionInBeats (int index);

    /** Intervalles de refresh en mode sync, du plus long au plus court. */
    static const juce::StringArray& getRefreshDivisionNames();
    static double getRefreshDivisionInBeats (int index);

    //==========================================================================
    LoopRollAudioProcessor();
    ~LoopRollAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "LoopRoll"; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 0.0; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    LooperEngine engine;

    std::atomic<float>* loopParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* timeParam = nullptr;
    std::atomic<float>* divisionParam = nullptr;
    std::atomic<float>* refreshParam = nullptr;
    std::atomic<float>* refreshTimeParam = nullptr;
    std::atomic<float>* refreshDivisionParam = nullptr;

    double currentSampleRate = 44100.0;
    double lastBpm = 120.0;
    int lastDivision = -1;
    int lastRefreshDivision = -1;
    bool lastSync = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopRollAudioProcessor)
};
