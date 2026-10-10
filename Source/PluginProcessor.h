#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LooperEngine.h"
#include "OutputFilter.h"
#include "StutterGate.h"

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
    static constexpr const char* fadeInId          = "fadeIn";
    static constexpr const char* fadeOutId         = "fadeOut";
    static constexpr const char* refreshModeId     = "refreshMode";
    static constexpr const char* stutterId         = "stutter";
    static constexpr const char* stutterModeId     = "stutterMode";
    static constexpr const char* stutterTimeId     = "stutterTime";
    static constexpr const char* stutterDivisionId = "stutterDivision";
    static constexpr const char* stutterShapeId    = "stutterShape";
    static constexpr const char* orderId           = "order";
    static constexpr const char* filterId          = "filter";
    static constexpr const char* filterTypeId      = "filterType";
    static constexpr const char* filterCutoffId    = "filterCutoff";
    static constexpr const char* filterResonanceId = "filterResonance";

    // Depuis la v1.1
    static constexpr const char* divisionFeelId    = "divisionFeel";
    static constexpr const char* refreshFeelId     = "refreshFeel";
    static constexpr const char* stutterFeelId     = "stutterFeel";
    static constexpr const char* pitchId           = "pitch";
    static constexpr const char* reverseId         = "reverse";
    static constexpr const char* preId             = "pre";
    static constexpr const char* decayId           = "decay";
    static constexpr const char* snapId            = "snap";
    static constexpr const char* momentaryId       = "momentary";
    static constexpr const char* mixId             = "mix";
    static constexpr const char* stutterDepthId    = "stutterDepth";
    static constexpr const char* stutterSwingId    = "stutterSwing";
    static constexpr const char* filterSlopeId     = "filterSlope";
    static constexpr const char* limiterId         = "limiter";

    /** Identifiant d'un pas du motif du stutter : "stutterStep1" à "stutterStep16". */
    static juce::String getStepId (int index)       { return "stutterStep" + juce::String (index + 1); }
    static constexpr int numStutterSteps = StutterGate::numSteps;

    // Plages du mode free (ms)
    static constexpr float minTimeMs = 10.0f;
    static constexpr float maxTimeMs = 4000.0f;
    static constexpr float minRefreshMs = 50.0f;
    static constexpr float maxRefreshMs = 16000.0f;
    static constexpr float minStutterMs = 5.0f;
    static constexpr float maxStutterMs = 1000.0f;

    // Les potards de durée vont du plus long (à gauche) au plus court (à droite).
    // Chaque module (boucle, refresh, stutter) a son propre mode FREE / SYNC,
    // et en SYNC sa propre variante : normale, triolet (x 2/3) ou pointée (x 3/2).
    static const juce::StringArray& getFeelNames();
    static double getFeelFactor (int index);

    /** Divisions de la boucle et du stutter en mode sync, de la plus longue à la plus courte. */
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

    const juce::String getName() const override             { return "Phragments"; }
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

    // Chaîne : [stutter et looper, dans l'ordre choisi], filtre (et son limiteur), puis mélange dry / wet.
    LooperEngine engine;
    StutterGate gate;
    OutputFilter filter;

    std::atomic<float>* loopParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* timeParam = nullptr;
    std::atomic<float>* divisionParam = nullptr;
    std::atomic<float>* refreshParam = nullptr;
    std::atomic<float>* refreshTimeParam = nullptr;
    std::atomic<float>* refreshDivisionParam = nullptr;
    std::atomic<float>* fadeInParam = nullptr;
    std::atomic<float>* fadeOutParam = nullptr;
    std::atomic<float>* refreshModeParam = nullptr;
    std::atomic<float>* stutterParam = nullptr;
    std::atomic<float>* stutterModeParam = nullptr;
    std::atomic<float>* stutterTimeParam = nullptr;
    std::atomic<float>* stutterDivisionParam = nullptr;
    std::atomic<float>* stutterShapeParam = nullptr;
    std::atomic<float>* orderParam = nullptr;
    std::atomic<float>* filterParam = nullptr;
    std::atomic<float>* filterTypeParam = nullptr;
    std::atomic<float>* filterCutoffParam = nullptr;
    std::atomic<float>* filterResonanceParam = nullptr;
    std::atomic<float>* divisionFeelParam = nullptr;
    std::atomic<float>* refreshFeelParam = nullptr;
    std::atomic<float>* stutterFeelParam = nullptr;
    std::atomic<float>* pitchParam = nullptr;
    std::atomic<float>* reverseParam = nullptr;
    std::atomic<float>* preParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* snapParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* stutterDepthParam = nullptr;
    std::atomic<float>* stutterSwingParam = nullptr;
    std::atomic<float>* filterSlopeParam = nullptr;
    std::atomic<float>* limiterParam = nullptr;
    std::atomic<float>* stepParams[numStutterSteps] = {};

    double currentSampleRate = 44100.0;
    double lastBpm = 120.0;
    int lastDivision = -1, lastDivisionFeel = -1;
    int lastRefreshDivision = -1, lastRefreshFeel = -1;
    bool lastLoopSync = false, lastRefreshSync = false;

    // Dry / wet : copie de l'entrée et mélange lissé
    juce::AudioBuffer<float> dryBuffer;
    float mixNow = 1.0f, mixSmoothing = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopRollAudioProcessor)
};
