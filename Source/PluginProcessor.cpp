#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Capacité d'enregistrement : couvre 1/1 jusqu'à 24 BPM.
    constexpr double maxLoopSeconds = 10.0;

    juce::String timeToText (float ms, int)
    {
        if (ms >= 1000.0f)
            return juce::String (ms / 1000.0f, 2) + " s";

        if (ms < 99.95f)
            return juce::String (ms, 1) + " ms";

        return juce::String (juce::roundToInt (ms)) + " ms";
    }

    float textToTime (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const auto value = t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();

        // "1.5 s" -> 1500 ms ; "250" ou "250 ms" -> 250 ms
        return (t.endsWithChar ('s') && ! t.endsWith ("ms")) ? value * 1000.0f : value;
    }

    // Plage de temps exponentielle et INVERSÉE : 0 = le plus long, 1 = le plus court.
    // Tourner le potard à droite (ou monter la valeur en automation / MIDI) raccourcit.
    juce::NormalisableRange<float> makeReversedTimeRange (float shortestMs, float longestMs)
    {
        return { shortestMs, longestMs,
                 [] (float start, float end, float normalised)
                 {
                     return end * std::pow (start / end, juce::jlimit (0.0f, 1.0f, normalised));
                 },
                 [] (float start, float end, float value)
                 {
                     return juce::jlimit (0.0f, 1.0f, std::log (juce::jlimit (start, end, value) / end) / std::log (start / end));
                 },
                 [] (float start, float end, float value)
                 {
                     const float step = value < 100.0f ? 0.1f : 1.0f;   // plus fin sur les durées très courtes
                     return juce::jlimit (start, end, std::round (value / step) * step);
                 } };
    }

    std::unique_ptr<juce::AudioParameterFloat> makeTimeParameter (const char* id, const char* name,
                                                                  float shortestMs, float longestMs, float defaultMs)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                            makeReversedTimeRange (shortestMs, longestMs), defaultMs,
                                                            juce::AudioParameterFloatAttributes()
                                                                .withStringFromValueFunction (timeToText)
                                                                .withValueFromStringFunction (textToTime));
    }
}

//==============================================================================
const juce::StringArray& LoopRollAudioProcessor::getDivisionNames()
{
    static const juce::StringArray names { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
    return names;
}

double LoopRollAudioProcessor::getDivisionInBeats (int index)
{
    static constexpr double beats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };
    return beats[juce::jlimit (0, (int) std::size (beats) - 1, index)];
}

const juce::StringArray& LoopRollAudioProcessor::getRefreshDivisionNames()
{
    // 4/1 = 4 mesures, 1/1 = 1 mesure
    static const juce::StringArray names { "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
    return names;
}

double LoopRollAudioProcessor::getRefreshDivisionInBeats (int index)
{
    static constexpr double beats[] = { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
    return beats[juce::jlimit (0, (int) std::size (beats) - 1, index)];
}

juce::AudioProcessorValueTreeState::ParameterLayout LoopRollAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { loopId, 1 }, "Loop", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { modeId, 1 }, "Mode",
                                                              juce::StringArray { "Free", "Sync" }, 1));

    layout.add (makeTimeParameter (timeId, "Time", minTimeMs, maxTimeMs, 250.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { divisionId, 1 }, "Division",
                                                              getDivisionNames(), 2));   // 1/4

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { refreshId, 1 }, "Refresh", false));

    layout.add (makeTimeParameter (refreshTimeId, "Refresh Time", minRefreshMs, maxRefreshMs, 1000.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { refreshDivisionId, 1 }, "Refresh Division",
                                                              getRefreshDivisionNames(), 2));   // 1/1

    return layout;
}

//==============================================================================
LoopRollAudioProcessor::LoopRollAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LoopRoll", createParameterLayout())
{
    loopParam     = apvts.getRawParameterValue (loopId);
    modeParam     = apvts.getRawParameterValue (modeId);
    timeParam     = apvts.getRawParameterValue (timeId);
    divisionParam = apvts.getRawParameterValue (divisionId);
    refreshParam         = apvts.getRawParameterValue (refreshId);
    refreshTimeParam     = apvts.getRawParameterValue (refreshTimeId);
    refreshDivisionParam = apvts.getRawParameterValue (refreshDivisionId);
}

bool LoopRollAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && layouts.getMainInputChannelSet() == out;
}

void LoopRollAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    engine.prepare (currentSampleRate, juce::jmax (1, getTotalNumOutputChannels()), maxLoopSeconds);
    lastDivision = -1;
}

void LoopRollAudioProcessor::reset()
{
    engine.reset();
}

void LoopRollAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    // Tempo de l'hôte (on garde le dernier connu si l'hôte n'en fournit pas).
    if (auto* hostPlayHead = getPlayHead())
        if (const auto position = hostPlayHead->getPosition())
            if (const auto bpm = position->getBpm())
                if (*bpm > 1.0)
                    lastBpm = *bpm;

    const bool sync = modeParam->load (std::memory_order_relaxed) > 0.5f;
    const bool refresh = refreshParam->load (std::memory_order_relaxed) > 0.5f;
    const int division = juce::roundToInt (divisionParam->load (std::memory_order_relaxed));
    const int refreshDivision = juce::roundToInt (refreshDivisionParam->load (std::memory_order_relaxed));

    if (sync)
    {
        const double samplesPerBeat = 60.0 / lastBpm * currentSampleRate;

        // Changement de division (ou passage en sync) : on recale sur la grille de départ.
        // Une simple variation de tempo, elle, ne provoque pas de saut.
        engine.setLoopLength (getDivisionInBeats (division) * samplesPerBeat,
                              division != lastDivision || ! lastSync);
        engine.setRefresh (refresh, getRefreshDivisionInBeats (refreshDivision) * samplesPerBeat,
                           refreshDivision != lastRefreshDivision || ! lastSync);
    }
    else
    {
        const double samplesPerMs = 0.001 * currentSampleRate;

        engine.setLoopLength ((double) timeParam->load (std::memory_order_relaxed) * samplesPerMs, false);
        engine.setRefresh (refresh, (double) refreshTimeParam->load (std::memory_order_relaxed) * samplesPerMs, false);
    }

    lastSync = sync;
    lastDivision = division;
    lastRefreshDivision = refreshDivision;

    engine.setActive (loopParam->load (std::memory_order_relaxed) > 0.5f);
    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (numIn, numOut, buffer.getNumChannels()), numSamples);
}

void LoopRollAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // En bypass on repart d'un état propre : pas de vieille boucle rejouée à la réactivation.
    engine.reset();
    AudioProcessor::processBlockBypassed (buffer, midi);
}

//==============================================================================
juce::AudioProcessorEditor* LoopRollAudioProcessor::createEditor()
{
    return new LoopRollAudioProcessorEditor (*this);
}

void LoopRollAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LoopRollAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LoopRollAudioProcessor();
}
