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

        return juce::String (juce::roundToInt (ms)) + " ms";
    }

    float textToTime (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const auto value = t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();

        // "1.5 s" -> 1500 ms ; "250" ou "250 ms" -> 250 ms
        return (t.endsWithChar ('s') && ! t.endsWith ("ms")) ? value * 1000.0f : value;
    }
}

//==============================================================================
const juce::StringArray& LoopRollAudioProcessor::getDivisionNames()
{
    static const juce::StringArray names { "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" };
    return names;
}

double LoopRollAudioProcessor::getDivisionInBeats (int index)
{
    static constexpr double beats[] = { 0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };
    return beats[juce::jlimit (0, (int) std::size (beats) - 1, index)];
}

juce::AudioProcessorValueTreeState::ParameterLayout LoopRollAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { loopId, 1 }, "Loop", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { modeId, 1 }, "Mode",
                                                              juce::StringArray { "Free", "Sync" }, 1));

    juce::NormalisableRange<float> timeRange (minTimeMs, maxTimeMs, 1.0f);
    timeRange.setSkewForCentre (300.0f);

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { timeId, 1 }, "Time", timeRange, 250.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction (timeToText)
                                                                 .withValueFromStringFunction (textToTime)));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { divisionId, 1 }, "Division",
                                                              getDivisionNames(), 4));   // 1/4

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
    const int division = juce::roundToInt (divisionParam->load (std::memory_order_relaxed));

    if (sync)
    {
        const double lengthInSamples = getDivisionInBeats (division) * 60.0 / lastBpm * currentSampleRate;

        // Changement de division (ou passage en sync) : on recale la boucle sur sa grille de départ.
        // Une simple variation de tempo, elle, ne provoque pas de saut.
        engine.setLoopLength (lengthInSamples, division != lastDivision || ! lastSync);
    }
    else
    {
        const double ms = (double) timeParam->load (std::memory_order_relaxed);
        engine.setLoopLength (ms * 0.001 * currentSampleRate, false);
    }

    lastSync = sync;
    lastDivision = division;

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
