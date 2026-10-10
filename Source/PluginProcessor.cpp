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

    juce::String frequencyToText (float hertz, int)
    {
        if (hertz >= 1000.0f)
            return juce::String (hertz / 1000.0f, 2) + " kHz";

        return juce::String (juce::roundToInt (hertz)) + " Hz";
    }

    float textToFrequency (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const auto value = t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();

        // "1.2k" ou "1.2 kHz" -> 1200 Hz ; "250" ou "250 Hz" -> 250 Hz
        return t.containsChar ('k') ? value * 1000.0f : value;
    }

    // Fréquence de coupure sur une échelle logarithmique, de 20 Hz à 20 kHz.
    juce::NormalisableRange<float> makeFrequencyRange()
    {
        return { (float) OutputFilter::minFrequency, (float) OutputFilter::maxFrequency,
                 [] (float start, float end, float normalised)
                 {
                     return start * std::pow (end / start, juce::jlimit (0.0f, 1.0f, normalised));
                 },
                 [] (float start, float end, float value)
                 {
                     return juce::jlimit (0.0f, 1.0f, std::log (juce::jlimit (start, end, value) / start) / std::log (end / start));
                 },
                 [] (float start, float end, float value)
                 {
                     const float step = value < 1000.0f ? 1.0f : 10.0f;
                     return juce::jlimit (start, end, std::round (value / step) * step);
                 } };
    }

    // Réglage en pourcentage (fondus, forme du stutter, résonance).
    std::unique_ptr<juce::AudioParameterFloat> makePercentParameter (const char* id, const char* name, float defaultPercent)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                            juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), defaultPercent,
                                                            juce::AudioParameterFloatAttributes()
                                                                .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value)) + " %"; })
                                                                .withValueFromStringFunction ([] (const juce::String& text) { return text.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue(); }));
    }

    std::unique_ptr<juce::AudioParameterBool> makeSwitch (const juce::String& id, const juce::String& name, bool defaultValue)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, defaultValue);
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
juce::String LoopRollAudioProcessor::pitchToText (float semitones)
{
    const float rounded = std::round (semitones);
    const bool whole = std::abs (semitones - rounded) < 0.005f;
    const auto number = whole ? juce::String (juce::roundToInt (rounded)) : juce::String (semitones, 2);

    return ((whole ? rounded : semitones) > 0.0f ? "+" : "") + number + " st";
}

const juce::StringArray& LoopRollAudioProcessor::getDivisionNames()
{
    static const juce::StringArray names { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/128" };
    return names;
}

double LoopRollAudioProcessor::getDivisionInBeats (int index)
{
    static constexpr double beats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625, 0.03125 };
    return beats[juce::jlimit (0, (int) std::size (beats) - 1, index)];
}

const juce::StringArray& LoopRollAudioProcessor::getFeelNames()
{
    static const juce::StringArray names { "Straight", "Triplet", "Dotted" };
    return names;
}

double LoopRollAudioProcessor::getFeelFactor (int index)
{
    static constexpr double factors[] = { 1.0, 2.0 / 3.0, 1.5 };
    return factors[juce::jlimit (0, (int) std::size (factors) - 1, index)];
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

    const juce::StringArray freeOrSync { "Free", "Sync" };

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { modeId, 1 }, "Loop Mode", freeOrSync, 1));

    layout.add (makeTimeParameter (timeId, "Time", minTimeMs, maxTimeMs, 250.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { divisionId, 1 }, "Division",
                                                              getDivisionNames(), 2));   // 1/4

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { refreshId, 1 }, "Refresh", false));

    layout.add (makeTimeParameter (refreshTimeId, "Refresh Time", minRefreshMs, maxRefreshMs, 1000.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { refreshDivisionId, 1 }, "Refresh Division",
                                                              getRefreshDivisionNames(), 2));   // 1/1

    layout.add (makePercentParameter (fadeInId, "Fade In", 0.0f));
    layout.add (makePercentParameter (fadeOutId, "Fade Out", 0.0f));

    // Depuis la v0.4 : le refresh et le stutter ont leur propre mode FREE / SYNC.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { refreshModeId, 1 }, "Refresh Mode", freeOrSync, 1));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { stutterId, 1 }, "Stutter", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { stutterModeId, 1 }, "Stutter Mode", freeOrSync, 1));
    layout.add (makeTimeParameter (stutterTimeId, "Stutter Time", minStutterMs, maxStutterMs, 125.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { stutterDivisionId, 1 }, "Stutter Division",
                                                              getDivisionNames(), 4));   // 1/16
    layout.add (makePercentParameter (stutterShapeId, "Stutter Shape", 50.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { orderId, 1 }, "Chain Order",
                                                              juce::StringArray { "Stutter > Loop", "Loop > Stutter" }, 1));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { filterId, 1 }, "Filter", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { filterTypeId, 1 }, "Filter Type",
                                                              juce::StringArray { "High Pass", "Band Pass", "Low Pass" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { filterCutoffId, 1 }, "Filter Cutoff",
                                                             makeFrequencyRange(), 20.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction (frequencyToText)
                                                                 .withValueFromStringFunction (textToFrequency)));
    layout.add (makePercentParameter (filterResonanceId, "Filter Resonance", 20.0f));

    // Depuis la v1.1. Les nouveaux réglages sont ajoutés à la fin : l'ordre des anciens ne bouge pas.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { divisionFeelId, 1 }, "Loop Feel", getFeelNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { refreshFeelId, 1 }, "Refresh Feel", getFeelNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { stutterFeelId, 1 }, "Stutter Feel", getFeelNames(), 0));

    // Pitch : en demi-tons, au centième près. En mode "Semitones" (voir pitchMode) la valeur est arrondie au demi-ton.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pitchId, 1 }, "Pitch",
                                                             juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction ([] (float value, int) { return pitchToText (value); })
                                                                 .withValueFromStringFunction ([] (const juce::String& text) { return text.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getFloatValue(); })));
    layout.add (makeSwitch (reverseId, "Loop Reverse", false));
    layout.add (makePercentParameter (mixId, "Mix", 100.0f));

    layout.add (makePercentParameter (stutterDepthId, "Stutter Depth", 100.0f));
    layout.add (makePercentParameter (stutterSwingId, "Stutter Swing", 0.0f));

    for (int i = 0; i < numStutterSteps; ++i)
        layout.add (makeSwitch (getStepId (i), "Stutter Step " + juce::String (i + 1), true));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { filterSlopeId, 1 }, "Filter Slope",
                                                              juce::StringArray { "12 dB", "24 dB" }, 0));
    layout.add (makeSwitch (limiterId, "Limiter", true));

    // Depuis la v1.2
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pitchModeId, 1 }, "Pitch Mode",
                                                              juce::StringArray { "Semitones", "Free" }, 0));

    return layout;
}

//==============================================================================
LoopRollAudioProcessor::LoopRollAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LoopRoll", createParameterLayout())   // ancien nom du plugin : à garder, les états sauvegardés portent cette étiquette
{
    loopParam     = apvts.getRawParameterValue (loopId);
    modeParam     = apvts.getRawParameterValue (modeId);
    timeParam     = apvts.getRawParameterValue (timeId);
    divisionParam = apvts.getRawParameterValue (divisionId);
    refreshParam         = apvts.getRawParameterValue (refreshId);
    refreshTimeParam     = apvts.getRawParameterValue (refreshTimeId);
    refreshDivisionParam = apvts.getRawParameterValue (refreshDivisionId);
    fadeInParam          = apvts.getRawParameterValue (fadeInId);
    fadeOutParam         = apvts.getRawParameterValue (fadeOutId);
    refreshModeParam     = apvts.getRawParameterValue (refreshModeId);
    stutterParam         = apvts.getRawParameterValue (stutterId);
    stutterModeParam     = apvts.getRawParameterValue (stutterModeId);
    stutterTimeParam     = apvts.getRawParameterValue (stutterTimeId);
    stutterDivisionParam = apvts.getRawParameterValue (stutterDivisionId);
    stutterShapeParam    = apvts.getRawParameterValue (stutterShapeId);
    orderParam           = apvts.getRawParameterValue (orderId);
    filterParam          = apvts.getRawParameterValue (filterId);
    filterTypeParam      = apvts.getRawParameterValue (filterTypeId);
    filterCutoffParam    = apvts.getRawParameterValue (filterCutoffId);
    filterResonanceParam = apvts.getRawParameterValue (filterResonanceId);
    divisionFeelParam    = apvts.getRawParameterValue (divisionFeelId);
    refreshFeelParam     = apvts.getRawParameterValue (refreshFeelId);
    stutterFeelParam     = apvts.getRawParameterValue (stutterFeelId);
    pitchParam           = apvts.getRawParameterValue (pitchId);
    reverseParam         = apvts.getRawParameterValue (reverseId);
    mixParam             = apvts.getRawParameterValue (mixId);
    stutterDepthParam    = apvts.getRawParameterValue (stutterDepthId);
    stutterSwingParam    = apvts.getRawParameterValue (stutterSwingId);
    filterSlopeParam     = apvts.getRawParameterValue (filterSlopeId);
    limiterParam         = apvts.getRawParameterValue (limiterId);
    pitchModeParam       = apvts.getRawParameterValue (pitchModeId);

    for (int i = 0; i < numStutterSteps; ++i)
        stepParams[i] = apvts.getRawParameterValue (getStepId (i));
}

bool LoopRollAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && layouts.getMainInputChannelSet() == out;
}

void LoopRollAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    engine.prepare (currentSampleRate, juce::jmax (1, getTotalNumOutputChannels()), maxLoopSeconds);
    gate.prepare (currentSampleRate);
    shifter.prepare (currentSampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    filter.prepare (currentSampleRate);
    lastDivision = -1;
    lastRefreshDivision = -1;
    lastDivisionFeel = -1;
    lastRefreshFeel = -1;

    dryBuffer.setSize (juce::jmax (1, getTotalNumOutputChannels()), juce::jmax (4096, samplesPerBlock), false, true, false);
    mixSmoothing = (float) (1.0 - std::exp (-1.0 / (0.01 * currentSampleRate)));   // ~10 ms
    mixNow = 0.01f * mixParam->load (std::memory_order_relaxed);
}

void LoopRollAudioProcessor::reset()
{
    engine.reset();
    gate.reset();
    shifter.reset();
    filter.reset();
}

void LoopRollAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    // Tempo et position de l'hôte (on garde le dernier tempo connu si l'hôte n'en fournit pas).
    double hostBeats = 0.0;
    bool hostIsMoving = false;

    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto position = hostPlayHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
                if (*bpm > 1.0)
                    lastBpm = *bpm;

            if (const auto beats = position->getPpqPosition())
            {
                hostBeats = *beats;
                hostIsMoving = position->getIsPlaying();
            }
        }
    }

    const auto value = [] (const std::atomic<float>* parameter) { return parameter->load (std::memory_order_relaxed); };
    const auto isOn = [&value] (const std::atomic<float>* parameter) { return value (parameter) > 0.5f; };
    const double samplesPerBeat = 60.0 / lastBpm * currentSampleRate;
    const double samplesPerMs = 0.001 * currentSampleRate;

    // --- Dry / wet : on garde l'entrée de côté tant que le mélange n'est pas à 100 % ---
    const int numChannels = juce::jmin (numIn, numOut, buffer.getNumChannels());
    const float mixTarget = juce::jlimit (0.0f, 1.0f, 0.01f * value (mixParam));
    const bool mixing = mixTarget < 1.0f || mixNow < 1.0f;

    if (mixing)
    {
        dryBuffer.setSize (juce::jmax (dryBuffer.getNumChannels(), numChannels), juce::jmax (dryBuffer.getNumSamples(), numSamples), true, false, true);

        for (int ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);
    }

    // --- Boucle ---
    const bool loopSync = isOn (modeParam);
    const int division = juce::roundToInt (value (divisionParam));
    const int divisionFeel = juce::roundToInt (value (divisionFeelParam));

    // Changement de division (ou passage en sync) : on recale sur la grille de départ.
    // Une simple variation de tempo, elle, ne provoque pas de saut.
    if (loopSync)
        engine.setLoopLength (getDivisionInBeats (division) * getFeelFactor (divisionFeel) * samplesPerBeat,
                              division != lastDivision || divisionFeel != lastDivisionFeel || ! lastLoopSync);
    else
        engine.setLoopLength ((double) value (timeParam) * samplesPerMs, false);

    // --- Refresh ---
    const bool refresh = isOn (refreshParam);
    const bool refreshSync = isOn (refreshModeParam);
    const int refreshDivision = juce::roundToInt (value (refreshDivisionParam));
    const int refreshFeel = juce::roundToInt (value (refreshFeelParam));

    if (refreshSync)
        engine.setRefresh (refresh, getRefreshDivisionInBeats (refreshDivision) * getFeelFactor (refreshFeel) * samplesPerBeat,
                           refreshDivision != lastRefreshDivision || refreshFeel != lastRefreshFeel || ! lastRefreshSync);
    else
        engine.setRefresh (refresh, (double) value (refreshTimeParam) * samplesPerMs, false);

    lastLoopSync = loopSync;
    lastRefreshSync = refreshSync;
    lastDivision = division;
    lastDivisionFeel = divisionFeel;
    lastRefreshDivision = refreshDivision;
    lastRefreshFeel = refreshFeel;

    engine.setFades (0.01 * (double) value (fadeInParam), 0.01 * (double) value (fadeOutParam));
    engine.setReverse (isOn (reverseParam));
    engine.setActive (isOn (loopParam));

    // --- Stutter ---
    if (isOn (stutterModeParam))
    {
        const double stepInBeats = getDivisionInBeats (juce::roundToInt (value (stutterDivisionParam)))
                                     * getFeelFactor (juce::roundToInt (value (stutterFeelParam)));
        gate.setStepLength (stepInBeats * samplesPerBeat);

        // Quand le transport défile, les pas du gate (et son motif) tombent sur la grille de l'hôte.
        if (hostIsMoving)
            gate.syncPosition (hostBeats / stepInBeats);
    }
    else
    {
        gate.setStepLength ((double) value (stutterTimeParam) * samplesPerMs);
    }

    unsigned int pattern = 0;

    for (int i = 0; i < numStutterSteps; ++i)
        if (isOn (stepParams[i]))
            pattern |= 1u << i;

    gate.setPattern (pattern);
    gate.setShape (0.01 * (double) value (stutterShapeParam));
    gate.setDepth (0.01 * (double) value (stutterDepthParam));
    gate.setSwing (0.01 * (double) value (stutterSwingParam));
    gate.setActive (isOn (stutterParam));

    // --- Pitch : en demi-tons entiers, ou libre ---
    const double semitones = (double) value (pitchParam);
    shifter.setSemitones (isOn (pitchModeParam) ? semitones : std::round (semitones));

    // --- Filtre ---
    filter.setFrequency ((double) value (filterCutoffParam));
    filter.setResonance (0.01 * (double) value (filterResonanceParam));
    filter.setType (juce::roundToInt (value (filterTypeParam)));
    filter.setSteep (isOn (filterSlopeParam));
    filter.setLimiter (isOn (limiterParam));
    filter.setActive (isOn (filterParam));

    // --- Chaîne : stutter et looper dans l'ordre choisi, puis pitch, puis filtre ---
    const auto channels = buffer.getArrayOfWritePointers();

    if (value (orderParam) < 0.5f)
    {
        gate.process (channels, numChannels, numSamples);
        engine.process (channels, numChannels, numSamples);
    }
    else
    {
        engine.process (channels, numChannels, numSamples);
        gate.process (channels, numChannels, numSamples);
    }

    shifter.process (channels, numChannels, numSamples);
    filter.process (channels, numChannels, numSamples);

    // --- Mélange avec l'entrée ---
    if (mixing)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float delta = mixTarget - mixNow;
            mixNow = std::abs (delta) < 1.0e-4f ? mixTarget : mixNow + delta * mixSmoothing;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float dry = dryBuffer.getSample (ch, n);
                channels[ch][n] = dry + mixNow * (channels[ch][n] - dry);
            }
        }
    }
}

void LoopRollAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // En bypass on repart d'un état propre : pas de vieille boucle rejouée à la réactivation.
    engine.reset();
    gate.reset();
    shifter.reset();
    filter.reset();
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
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    // Jusqu'à la v0.3, un seul mode FREE / SYNC servait à la boucle et au refresh :
    // un état de cette époque applique donc son mode aux deux.
    const auto loopMode = state.getChildWithProperty ("id", modeId);

    if (loopMode.isValid() && ! state.getChildWithProperty ("id", refreshModeId).isValid())
    {
        auto refreshMode = loopMode.createCopy();
        refreshMode.setProperty ("id", refreshModeId, nullptr);
        state.appendChild (refreshMode, nullptr);
    }

    apvts.replaceState (state);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LoopRollAudioProcessor();
}
