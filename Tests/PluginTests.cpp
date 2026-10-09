// Tests du plugin complet (processeur JUCE + interface), sans DAW.
// Usage : LoopRollPluginTests [dossier où écrire les captures de l'interface]

#include "../Source/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

static int failures = 0;

static void check (bool condition, const juce::String& message)
{
    if (! condition)
    {
        ++failures;
        std::cout << "  ECHEC : " << message << std::endl;
    }
}

struct FixedTempoPlayHead final : juce::AudioPlayHead
{
    double bpm = 120.0;
    juce::Optional<double> beats;   // position en noires, si l'hôte la fournit

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (true);
        info.setPpqPosition (beats);
        return info;
    }
};

static void setParam (juce::AudioProcessor& p, const char* id, float realValue)
{
    auto* param = dynamic_cast<LoopRollAudioProcessor&> (p).apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

// Fait passer "in" dans le plugin, bloc par bloc ; "onBlock" est appelé avant chaque bloc.
static void process (juce::AudioProcessor& p, const juce::AudioBuffer<float>& in, juce::AudioBuffer<float>& out, int blockSize,
                     const std::function<void (int)>& onBlock)
{
    out.makeCopyOf (in);
    juce::MidiBuffer midi;

    for (int start = 0; start < in.getNumSamples(); start += blockSize)
    {
        const int n = juce::jmin (blockSize, in.getNumSamples() - start);
        onBlock (start);

        juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), out.getNumChannels(), start, n);
        p.processBlock (block, midi);
    }
}

// Même chose avec du bruit blanc en entrée.
static void render (juce::AudioProcessor& p, juce::AudioBuffer<float>& in, juce::AudioBuffer<float>& out, int blockSize,
                    const std::function<void (int)>& onBlock)
{
    juce::Random rng (42);
    for (int ch = 0; ch < in.getNumChannels(); ++ch)
        for (int i = 0; i < in.getNumSamples(); ++i)
            in.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);

    process (p, in, out, blockSize, onBlock);
}

static std::unique_ptr<juce::AudioProcessor> makePlugin (double sampleRate, int blockSize)
{
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    p->setRateAndBufferSizeDetails (sampleRate, blockSize);
    p->prepareToPlay (sampleRate, blockSize);
    return p;
}

// Cherche où commence la répétition la plus proche de "expected" ; renvoie l'écart en échantillons (ou 9999).
static int findRepeatOffset (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int loopStart, int expected)
{
    if (expected < 16 || expected + 700 > out.getNumSamples())
        return 9999;   // hors du rendu : erreur du test lui-même

    for (int d = 0; d <= 4; ++d)
        for (int sign : { 1, -1 })
        {
            bool match = true;

            for (int ch = 0; ch < in.getNumChannels() && match; ++ch)
                for (int i = 100; i < 600 && match; ++i)
                    match = juce::exactlyEqual (out.getSample (ch, expected + sign * d + i), in.getSample (ch, loopStart + i));

            if (match)
                return sign * d;
        }

    return 9999;
}

//==============================================================================
static void testSyncAndFree (double sampleRate, int blockSize, int numChannels)
{
    std::cout << "- " << sampleRate << " Hz, blocs de " << blockSize << ", " << numChannels << " canal(aux)" << std::endl;

    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    FixedTempoPlayHead playHead;
    p->setPlayHead (&playHead);

    const auto set = numChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    check (p->setBusesLayout (layout), "configuration de canaux refusee");

    p->setRateAndBufferSizeDetails (sampleRate, blockSize);
    p->prepareToPlay (sampleRate, blockSize);

    const int total = (int) (sampleRate * 6.0);
    const int loopStart = blockSize * 20;
    juce::AudioBuffer<float> in (numChannels, total), out;

    // --- Mode sync : 1/16 à 174 BPM, puis 1/8 ---
    playHead.bpm = 174.0;
    setParam (*p, LoopRollAudioProcessor::modeId, 1.0f);
    setParam (*p, LoopRollAudioProcessor::divisionId, 4.0f);   // 1/16
    setParam (*p, LoopRollAudioProcessor::refreshId, 0.0f);
    setParam (*p, LoopRollAudioProcessor::loopId, 0.0f);

    const double sixteenth = 60.0 / 174.0 / 4.0 * sampleRate;
    const int switchAt = loopStart + (int) (sixteenth * 40.3);

    render (*p, in, out, blockSize, [&] (int start)
    {
        if (start == loopStart)  setParam (*p, LoopRollAudioProcessor::loopId, 1.0f);
        if (start >= switchAt)   setParam (*p, LoopRollAudioProcessor::divisionId, 3.0f);   // 1/8
    });

    bool untouched = true;
    for (int i = 0; i < loopStart; ++i)
        untouched = untouched && juce::exactlyEqual (out.getSample (0, i), in.getSample (0, i));
    check (untouched, "audio modifie avant l'activation");

    for (int k : { 1, 2, 17, 39 })
    {
        const int offset = findRepeatOffset (in, out, loopStart, loopStart + (int) std::lround (k * sixteenth));
        check (std::abs (offset) <= 1, "sync 1/16 : repetition " + juce::String (k) + " hors grille (" + juce::String (offset) + ")");
    }

    const double eighth = sixteenth * 2.0;
    for (int k : { 22, 27, 33 })
    {
        const int offset = findRepeatOffset (in, out, loopStart, loopStart + (int) std::lround (k * eighth));
        check (std::abs (offset) <= 1, "sync 1/8 apres changement : repetition " + juce::String (k) + " hors grille (" + juce::String (offset) + ")");
    }

    // --- Refresh : boucle 1/16 recapturée toutes les noires ---
    setParam (*p, LoopRollAudioProcessor::loopId, 0.0f);
    setParam (*p, LoopRollAudioProcessor::divisionId, 4.0f);          // 1/16
    setParam (*p, LoopRollAudioProcessor::refreshDivisionId, 4.0f);   // 1/4
    setParam (*p, LoopRollAudioProcessor::refreshId, 1.0f);

    render (*p, in, out, blockSize, [&] (int start)
    {
        if (start == loopStart)  setParam (*p, LoopRollAudioProcessor::loopId, 1.0f);
    });

    const double quarter = sixteenth * 4.0;
    for (int k : { 1, 2, 9, 15 })
    {
        // La première répétition après le k-ième refresh rejoue l'audio capté à ce refresh.
        const int refreshPoint = loopStart + (int) std::lround (k * quarter);
        const int offset = findRepeatOffset (in, out, refreshPoint, refreshPoint + (int) std::lround (sixteenth));
        check (std::abs (offset) <= 2, "refresh 1/4 : capture " + juce::String (k) + " hors grille (" + juce::String (offset) + ")");

        // Et juste après le refresh, c'est le direct qui passe.
        bool live = true;
        for (int i = 0; i < 400; ++i)
            live = live && juce::exactlyEqual (out.getSample (0, refreshPoint + 4 + i), in.getSample (0, refreshPoint + 4 + i));
        check (live, "refresh 1/4 : pas de direct apres le refresh " + juce::String (k));
    }

    // --- Modes indépendants : boucle FREE de 50 ms, refresh SYNC toutes les noires ---
    setParam (*p, LoopRollAudioProcessor::loopId, 0.0f);
    setParam (*p, LoopRollAudioProcessor::modeId, 0.0f);
    setParam (*p, LoopRollAudioProcessor::timeId, 50.0f);

    render (*p, in, out, blockSize, [&] (int start)
    {
        if (start == loopStart)  setParam (*p, LoopRollAudioProcessor::loopId, 1.0f);
    });

    const int shortLoop = (int) std::lround (0.05 * sampleRate);
    for (int k : { 1, 4, 11 })
    {
        const int refreshPoint = loopStart + (int) std::lround (k * quarter);
        const int offset = findRepeatOffset (in, out, refreshPoint, refreshPoint + shortLoop);
        check (std::abs (offset) <= 2, "boucle free + refresh sync : capture " + juce::String (k) + " hors grille (" + juce::String (offset) + ")");
    }

    // --- Et l'inverse : boucle SYNC 1/16, refresh FREE toutes les 500 ms ---
    setParam (*p, LoopRollAudioProcessor::loopId, 0.0f);
    setParam (*p, LoopRollAudioProcessor::modeId, 1.0f);
    setParam (*p, LoopRollAudioProcessor::refreshModeId, 0.0f);
    setParam (*p, LoopRollAudioProcessor::refreshTimeId, 500.0f);

    render (*p, in, out, blockSize, [&] (int start)
    {
        if (start == loopStart)  setParam (*p, LoopRollAudioProcessor::loopId, 1.0f);
    });

    for (int k : { 1, 3, 8 })
    {
        const int refreshPoint = loopStart + (int) std::lround (k * 0.5 * sampleRate);
        const int offset = findRepeatOffset (in, out, refreshPoint, refreshPoint + (int) std::lround (sixteenth));
        check (std::abs (offset) <= 2, "boucle sync + refresh free : capture " + juce::String (k) + " hors grille (" + juce::String (offset) + ")");
    }

    setParam (*p, LoopRollAudioProcessor::refreshId, 0.0f);
    setParam (*p, LoopRollAudioProcessor::refreshModeId, 1.0f);

    // --- Arrêt puis mode free 100 ms ---
    setParam (*p, LoopRollAudioProcessor::loopId, 0.0f);
    setParam (*p, LoopRollAudioProcessor::modeId, 0.0f);
    setParam (*p, LoopRollAudioProcessor::timeId, 100.0f);

    render (*p, in, out, blockSize, [&] (int start)
    {
        if (start == loopStart)  setParam (*p, LoopRollAudioProcessor::loopId, 1.0f);
        if (start >= total - blockSize * 40)  setParam (*p, LoopRollAudioProcessor::loopId, 0.0f);
    });

    const int freeLength = (int) std::lround (0.1 * sampleRate);
    for (int k : { 1, 5, 20 })
    {
        const int offset = findRepeatOffset (in, out, loopStart, loopStart + k * freeLength);
        check (offset == 0, "free 100 ms : repetition " + juce::String (k) + " decalee (" + juce::String (offset) + ")");
    }

    bool dryAtEnd = true;
    for (int i = total - blockSize * 20; i < total; ++i)
        dryAtEnd = dryAtEnd && juce::exactlyEqual (out.getSample (numChannels - 1, i), in.getSample (numChannels - 1, i));
    check (dryAtEnd, "le signal direct ne revient pas apres l'arret de la boucle");

    p->releaseResources();
}

static void testFades()
{
    std::cout << "- fondus sur la boucle" << std::endl;

    const double sampleRate = 48000.0;
    const int blockSize = 256, total = 96000, loopStart = blockSize * 20, loopLength = 4800;   // 100 ms

    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    p->setRateAndBufferSizeDetails (sampleRate, blockSize);
    p->prepareToPlay (sampleRate, blockSize);

    setParam (*p, LoopRollAudioProcessor::modeId, 0.0f);
    setParam (*p, LoopRollAudioProcessor::timeId, 100.0f);
    setParam (*p, LoopRollAudioProcessor::refreshId, 0.0f);
    setParam (*p, LoopRollAudioProcessor::fadeInId, 50.0f);    // 2400 échantillons
    setParam (*p, LoopRollAudioProcessor::fadeOutId, 25.0f);   // 1200 échantillons

    juce::AudioBuffer<float> in (2, total), out;
    render (*p, in, out, blockSize, [&] (int start)
    {
        if (start == loopStart)  setParam (*p, LoopRollAudioProcessor::loopId, 1.0f);
    });

    // Sur chaque passage : à 25 % le fondu d'entrée est à mi-course, à 60 % plein niveau, à 87,5 % le fondu de sortie est à mi-course.
    struct Point { int position; float gain; };

    for (int repeat : { 0, 3, 11 })
        for (const auto point : { Point { 1200, 0.5f }, Point { 2880, 1.0f }, Point { 4200, 0.5f } })
            for (int ch = 0; ch < 2; ++ch)
            {
                const float expected = in.getSample (ch, loopStart + point.position) * point.gain;
                const float actual = out.getSample (ch, loopStart + repeat * loopLength + point.position);
                check (juce::exactlyEqual (actual, expected), "fondu : passage " + juce::String (repeat) + ", position " + juce::String (point.position)
                                                                + " : " + juce::String (actual) + " au lieu de " + juce::String (expected));
            }

    // Début de passage : silence (fondu d'entrée).
    check (std::abs (out.getSample (0, loopStart + 5 * loopLength + 1)) < 0.001f, "le passage devrait repartir du silence");

    p->releaseResources();
}

static void testStutter()
{
    std::cout << "- stutter : gate, forme, calage sur l'hote, ordre de chaine" << std::endl;

    const double sampleRate = 48000.0;
    const int blockSize = 256, total = 96000, start = blockSize * 20;
    using Ids = LoopRollAudioProcessor;

    juce::AudioBuffer<float> in (2, total), out;
    const auto ratio = [&] (int index, int channel = 0) { return out.getSample (channel, index) / in.getSample (channel, index); };

    // --- Seul, en FREE : pas de 100 ms, mesure au milieu du pas pour trois formes ---
    struct Shape { float percent; double expected; };

    for (const auto shape : { Shape { 50.0f, 0.5 }, Shape { 0.0f, std::pow (0.5, 1.0 / 16.0) }, Shape { 100.0f, std::pow (0.5, 16.0) } })
    {
        auto p = makePlugin (sampleRate, blockSize);
        setParam (*p, Ids::stutterModeId, 0.0f);
        setParam (*p, Ids::stutterTimeId, 100.0f);
        setParam (*p, Ids::stutterShapeId, shape.percent);

        render (*p, in, out, blockSize, [&] (int position)
        {
            if (position == start)  setParam (*p, Ids::stutterId, 1.0f);
            if (position >= total - blockSize * 40)  setParam (*p, Ids::stutterId, 0.0f);
        });

        bool untouched = true;
        for (int i = 0; i < start; ++i)
            untouched = untouched && juce::exactlyEqual (out.getSample (1, i), in.getSample (1, i));
        check (untouched, "stutter : audio modifie avant l'activation");

        for (int step : { 2, 5, 9 })
            for (int ch = 0; ch < 2; ++ch)
            {
                const double gain = ratio (start + step * 4800 + 2400, ch);
                check (std::abs (gain - shape.expected) < 0.005, "stutter forme " + juce::String (shape.percent) + " % : niveau a mi-pas "
                                                                   + juce::String (gain, 4) + " au lieu de " + juce::String (shape.expected, 4));
            }

        // Juste après l'ouverture du pas le son est à plein niveau, juste avant la fin il est refermé.
        check (ratio (start + 5 * 4800 + 100) > (shape.percent > 99.0f ? 0.6 : 0.95), "stutter : le pas ne s'ouvre pas a plein niveau");
        check (std::abs (ratio (start + 6 * 4800 - 2)) < 0.05, "stutter : le pas ne se referme pas");

        bool dryAtEnd = true;
        for (int i = total - blockSize * 20; i < total; ++i)
            dryAtEnd = dryAtEnd && juce::exactlyEqual (out.getSample (0, i), in.getSample (0, i));
        check (dryAtEnd, "stutter : le signal direct ne revient pas apres l'arret");
    }

    // --- En SYNC : les pas tombent sur la grille de l'hôte, d'où que parte la lecture ---
    for (const double startBeats : { 0.0, 0.1, 37.6 })
    {
        auto p = makePlugin (sampleRate, blockSize);
        FixedTempoPlayHead playHead;
        playHead.bpm = 120.0;                      // une noire = 24000 échantillons, une double croche = 6000
        p->setPlayHead (&playHead);

        setParam (*p, Ids::stutterModeId, 1.0f);
        setParam (*p, Ids::stutterDivisionId, 4.0f);   // 1/16
        setParam (*p, Ids::stutterId, 1.0f);

        render (*p, in, out, blockSize, [&] (int position) { playHead.beats = startBeats + position / 24000.0; });

        // Position du premier début de pas dans le rendu
        const double phaseAtStart = std::fmod (startBeats / 0.25, 1.0);
        const int firstStep = (int) std::lround ((1.0 - phaseAtStart) * 6000.0) % 6000;

        for (int step : { 2, 7, 13 })
        {
            const double gain = ratio (firstStep + step * 6000 + 3000);
            check (std::abs (gain - 0.5) < 0.005, "stutter sync, depart a " + juce::String (startBeats) + " noires : niveau a mi-pas " + juce::String (gain, 4));
        }

        p->setPlayHead (nullptr);
    }

    // --- Les deux ensemble : l'ordre de la chaîne change le résultat ---
    for (const bool stutterFirst : { true, false })
    {
        auto p = makePlugin (sampleRate, blockSize);
        setParam (*p, Ids::modeId, 0.0f);
        setParam (*p, Ids::timeId, 100.0f);          // boucle de 4800 échantillons
        setParam (*p, Ids::stutterModeId, 0.0f);
        setParam (*p, Ids::stutterTimeId, 70.0f);    // pas de 3360 échantillons
        setParam (*p, Ids::orderId, stutterFirst ? 0.0f : 1.0f);

        render (*p, in, out, blockSize, [&] (int position)
        {
            if (position == start)
            {
                setParam (*p, Ids::loopId, 1.0f);
                setParam (*p, Ids::stutterId, 1.0f);
            }
        });

        if (stutterFirst)
        {
            // La boucle capture le son déjà haché : chaque passage est la copie exacte du premier.
            bool identical = true, gated = false;

            for (int pass : { 1, 4, 9 })
                for (int i = 300; i < 4400; ++i)
                    identical = identical && juce::exactlyEqual (out.getSample (0, start + pass * 4800 + i), out.getSample (0, start + i));

            for (int i = 300; i < 4400; ++i)
                gated = gated || std::abs (out.getSample (0, start + i)) < 0.5f * std::abs (in.getSample (0, start + i));

            check (identical, "stutter > loop : la boucle ne repete pas le son hache a l'identique");
            check (gated, "stutter > loop : le premier passage n'est pas hache");
        }
        else
        {
            // La boucle répète le son brut, et le gate continue de tourner par-dessus à son propre rythme.
            // Milieu d'un pas de gate : (position depuis l'activation) modulo 3360 = 1680.
            struct Point { int pass, offset; };

            for (const auto point : { Point { 1, 240 }, Point { 2, 2160 }, Point { 5, 1200 } })
            {
                const float expected = in.getSample (0, start + point.offset) * 0.5f;
                const float actual = out.getSample (0, start + point.pass * 4800 + point.offset);
                check (std::abs (actual - expected) < 0.003f, "loop > stutter : passage " + juce::String (point.pass) + " : "
                                                                + juce::String (actual, 4) + " au lieu de " + juce::String (expected, 4));
            }
        }
    }
}

static void testFilter()
{
    std::cout << "- filtre de fin de chaine" << std::endl;

    const double sampleRate = 48000.0;
    const int blockSize = 256, total = 96000;
    using Ids = LoopRollAudioProcessor;

    const auto sine = [&] (double frequency)
    {
        juce::AudioBuffer<float> buffer (2, total);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < total; ++i)
                buffer.setSample (ch, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / sampleRate));
        return buffer;
    };

    const auto level = [&] (const juce::AudioBuffer<float>& buffer) { return (double) buffer.getRMSLevel (1, total / 2, total / 2); };

    struct Case { bool bandPass; double frequency, minimum, maximum; const char* name; };

    for (const auto test : { Case { false, 100.0, 0.005, 0.02, "passe-haut 1 kHz, sinus 100 Hz (-40 dB)" },
                             Case { false, 8000.0, 0.95, 1.02, "passe-haut 1 kHz, sinus 8 kHz (intact)" },
                             Case { true, 1000.0, 0.80, 0.88, "passe-bande 1 kHz, sinus 1 kHz" },
                             Case { true, 100.0, 0.05, 0.14, "passe-bande 1 kHz, sinus 100 Hz" } })
    {
        auto p = makePlugin (sampleRate, blockSize);
        setParam (*p, Ids::filterId, 1.0f);
        setParam (*p, Ids::filterTypeId, test.bandPass ? 1.0f : 0.0f);
        setParam (*p, Ids::filterCutoffId, 1000.0f);
        setParam (*p, Ids::filterResonanceId, 0.0f);

        const auto in = sine (test.frequency);
        juce::AudioBuffer<float> out;
        process (*p, in, out, blockSize, [] (int) {});

        const double gain = level (out) / level (in);
        check (gain > test.minimum && gain < test.maximum, juce::String (test.name) + " : gain " + juce::String (gain, 4));
    }

    // La résonance crée un pic à la coupure ; le filtre éteint ne touche à rien.
    {
        auto p = makePlugin (sampleRate, blockSize);
        setParam (*p, Ids::filterId, 1.0f);
        setParam (*p, Ids::filterCutoffId, 1000.0f);
        setParam (*p, Ids::filterResonanceId, 100.0f);

        const auto in = sine (1000.0);
        juce::AudioBuffer<float> out;
        process (*p, in, out, blockSize, [] (int) {});
        const double gain = level (out) / level (in);
        check (gain > 9.0 && gain < 11.0, "resonance a fond : pic de " + juce::String (gain, 2) + " au lieu de 10");

        setParam (*p, Ids::filterId, 0.0f);
        process (*p, in, out, blockSize, [] (int) {});

        bool untouched = true;
        for (int i = total / 2; i < total; ++i)
            untouched = untouched && juce::exactlyEqual (out.getSample (0, i), in.getSample (0, i));
        check (untouched, "filtre eteint : le signal n'est pas intact");
    }

    // Fin de chaîne : le filtre s'applique aussi aux répétitions de la boucle (bruit blanc, passe-haut à 6 kHz : il en reste 86 %).
    {
        auto p = makePlugin (sampleRate, blockSize);
        setParam (*p, Ids::modeId, 0.0f);
        setParam (*p, Ids::timeId, 100.0f);
        setParam (*p, Ids::loopId, 1.0f);

        juce::AudioBuffer<float> in (2, total), looped, filtered;
        render (*p, in, looped, blockSize, [] (int) {});

        auto q = makePlugin (sampleRate, blockSize);
        setParam (*q, Ids::modeId, 0.0f);
        setParam (*q, Ids::timeId, 100.0f);
        setParam (*q, Ids::loopId, 1.0f);
        setParam (*q, Ids::filterId, 1.0f);
        setParam (*q, Ids::filterCutoffId, 6000.0f);
        setParam (*q, Ids::filterResonanceId, 0.0f);
        render (*q, in, filtered, blockSize, [] (int) {});

        const double gain = level (filtered) / level (looped);
        check (gain > 0.82 && gain < 0.90, "filtre sur la boucle : gain " + juce::String (gain, 3) + " au lieu de 0.86");
    }
}

static void testParametersAndState()
{
    std::cout << "- parametres et sauvegarde d'etat" << std::endl;

    std::unique_ptr<juce::AudioProcessor> a (createPluginFilter());
    auto& apvts = dynamic_cast<LoopRollAudioProcessor&> (*a).apvts;

    // Le plugin s'appelle Fragment depuis la v1.0 ; l'étiquette de ses états sauvegardés garde l'ancien nom (compatibilité).
    check (a->getName() == "Fragment", "nom du plugin : " + a->getName());
    check (apvts.state.getType().toString() == "LoopRoll", "etiquette d'etat : " + apvts.state.getType().toString());

    auto* time = apvts.getParameter (LoopRollAudioProcessor::timeId);
    check (time->getText (time->convertTo0to1 (250.0f), 64) == "250 ms", "texte 250 ms : " + time->getText (time->convertTo0to1 (250.0f), 64));
    check (time->getText (time->convertTo0to1 (1500.0f), 64) == "1.50 s", "texte 1.50 s : " + time->getText (time->convertTo0to1 (1500.0f), 64));
    check (std::abs (time->convertFrom0to1 (time->getValueForText ("1.5 s")) - 1500.0f) < 1.0f, "saisie '1.5 s'");
    check (std::abs (time->convertFrom0to1 (time->getValueForText ("80")) - 80.0f) < 1.0f, "saisie '80'");
    check (std::abs (time->convertFrom0to1 (time->getValueForText ("120 ms")) - 120.0f) < 1.0f, "saisie '120 ms'");

    // Sens des potards : valeur haute (potard à droite) = plus court.
    check (time->convertTo0to1 (LoopRollAudioProcessor::minTimeMs) > 0.999f, "duree min doit etre a droite");
    check (time->convertTo0to1 (LoopRollAudioProcessor::maxTimeMs) < 0.001f, "duree max doit etre a gauche");
    check (time->convertFrom0to1 (0.75f) < time->convertFrom0to1 (0.25f), "le potard de duree n'est pas inverse");
    check (std::abs (time->convertFrom0to1 (time->convertTo0to1 (123.0f)) - 123.0f) < 0.5f, "aller-retour 123 ms");
    check (time->getText (time->convertTo0to1 (12.5f), 64) == "12.5 ms", "texte 12.5 ms : " + time->getText (time->convertTo0to1 (12.5f), 64));

    auto* division = apvts.getParameter (LoopRollAudioProcessor::divisionId);
    check (division->getText (0.0f, 64) == "1/1" && division->getText (1.0f, 64) == "1/64", "sens du potard de division");
    check (LoopRollAudioProcessor::getDivisionNames().size() == 7 && LoopRollAudioProcessor::getRefreshDivisionNames().size() == 8, "listes de divisions");
    check (juce::approximatelyEqual (LoopRollAudioProcessor::getDivisionInBeats (4), 0.25), "1/16 = 0.25 noire");
    check (juce::approximatelyEqual (LoopRollAudioProcessor::getRefreshDivisionInBeats (0), 16.0), "4/1 = 16 noires");

    auto* refreshDivision = apvts.getParameter (LoopRollAudioProcessor::refreshDivisionId);
    check (refreshDivision->getCurrentValueAsText() == "1/1", "intervalle de refresh par defaut : " + refreshDivision->getCurrentValueAsText());
    check (refreshDivision->getText (0.0f, 64) == "4/1" && refreshDivision->getText (1.0f, 64) == "1/32", "sens du potard de refresh");
    check (apvts.getParameter (LoopRollAudioProcessor::refreshId)->getValue() < 0.5f, "refresh eteint par defaut");
    check (apvts.getParameter (LoopRollAudioProcessor::refreshTimeId)->getCurrentValueAsText() == "1.00 s", "intervalle free par defaut");
    check (apvts.getParameter (LoopRollAudioProcessor::fadeInId)->getCurrentValueAsText() == "0 %", "fondu d'entree par defaut");
    check (apvts.getParameter (LoopRollAudioProcessor::fadeOutId)->getCurrentValueAsText() == "0 %", "fondu de sortie par defaut");

    check (division->getCurrentValueAsText() == "1/4", "division par defaut : " + division->getCurrentValueAsText());
    check (apvts.getParameter (LoopRollAudioProcessor::modeId)->getCurrentValueAsText() == "Sync", "mode par defaut");

    // Nouveautés de la v0.4
    const auto text = [&apvts] (const char* id) { return apvts.getParameter (id)->getCurrentValueAsText(); };
    check (text (LoopRollAudioProcessor::refreshModeId) == "Sync" && text (LoopRollAudioProcessor::stutterModeId) == "Sync", "modes par defaut");
    check (text (LoopRollAudioProcessor::stutterDivisionId) == "1/16", "pas de stutter par defaut : " + text (LoopRollAudioProcessor::stutterDivisionId));
    check (text (LoopRollAudioProcessor::stutterTimeId) == "125 ms", "pas free par defaut : " + text (LoopRollAudioProcessor::stutterTimeId));
    check (text (LoopRollAudioProcessor::stutterShapeId) == "50 %", "forme par defaut : " + text (LoopRollAudioProcessor::stutterShapeId));
    check (text (LoopRollAudioProcessor::orderId) == "Loop > Stutter", "ordre par defaut : " + text (LoopRollAudioProcessor::orderId));
    check (text (LoopRollAudioProcessor::filterTypeId) == "High Pass", "type de filtre par defaut : " + text (LoopRollAudioProcessor::filterTypeId));
    check (text (LoopRollAudioProcessor::filterCutoffId) == "20 Hz", "coupure par defaut : " + text (LoopRollAudioProcessor::filterCutoffId));
    check (apvts.getParameter (LoopRollAudioProcessor::stutterId)->getValue() < 0.5f
             && apvts.getParameter (LoopRollAudioProcessor::filterId)->getValue() < 0.5f, "stutter et filtre eteints par defaut");

    auto* cutoff = apvts.getParameter (LoopRollAudioProcessor::filterCutoffId);
    check (cutoff->getText (cutoff->convertTo0to1 (1200.0f), 64) == "1.20 kHz", "texte 1.20 kHz : " + cutoff->getText (cutoff->convertTo0to1 (1200.0f), 64));
    check (std::abs (cutoff->convertFrom0to1 (cutoff->getValueForText ("2.5k")) - 2500.0f) < 5.0f, "saisie '2.5k'");
    check (std::abs (cutoff->convertFrom0to1 (cutoff->getValueForText ("440 Hz")) - 440.0f) < 1.0f, "saisie '440 Hz'");
    check (std::abs (cutoff->convertFrom0to1 (0.5f) - 632.0f) < 2.0f, "milieu de course de la coupure : " + juce::String (cutoff->convertFrom0to1 (0.5f)));

    auto* stutterTime = apvts.getParameter (LoopRollAudioProcessor::stutterTimeId);
    check (stutterTime->convertFrom0to1 (0.75f) < stutterTime->convertFrom0to1 (0.25f), "le potard de stutter n'est pas inverse");

    setParam (*a, LoopRollAudioProcessor::modeId, 0.0f);
    setParam (*a, LoopRollAudioProcessor::timeId, 777.0f);
    setParam (*a, LoopRollAudioProcessor::divisionId, 6.0f);
    setParam (*a, LoopRollAudioProcessor::refreshId, 1.0f);
    setParam (*a, LoopRollAudioProcessor::refreshTimeId, 2500.0f);
    setParam (*a, LoopRollAudioProcessor::refreshDivisionId, 5.0f);
    setParam (*a, LoopRollAudioProcessor::fadeInId, 12.0f);
    setParam (*a, LoopRollAudioProcessor::fadeOutId, 80.0f);
    setParam (*a, LoopRollAudioProcessor::stutterId, 1.0f);
    setParam (*a, LoopRollAudioProcessor::stutterModeId, 0.0f);
    setParam (*a, LoopRollAudioProcessor::stutterTimeId, 33.0f);
    setParam (*a, LoopRollAudioProcessor::stutterDivisionId, 5.0f);
    setParam (*a, LoopRollAudioProcessor::stutterShapeId, 85.0f);
    setParam (*a, LoopRollAudioProcessor::orderId, 0.0f);
    setParam (*a, LoopRollAudioProcessor::filterId, 1.0f);
    setParam (*a, LoopRollAudioProcessor::filterTypeId, 1.0f);
    setParam (*a, LoopRollAudioProcessor::filterCutoffId, 2500.0f);
    setParam (*a, LoopRollAudioProcessor::filterResonanceId, 64.0f);

    juce::MemoryBlock state;
    a->getStateInformation (state);

    std::unique_ptr<juce::AudioProcessor> b (createPluginFilter());
    b->setStateInformation (state.getData(), (int) state.getSize());
    auto& restored = dynamic_cast<LoopRollAudioProcessor&> (*b).apvts;

    check (restored.getParameter (LoopRollAudioProcessor::modeId)->getCurrentValueAsText() == "Free", "mode non restaure");
    check (restored.getParameter (LoopRollAudioProcessor::timeId)->getCurrentValueAsText() == "777 ms", "duree non restauree");
    check (restored.getParameter (LoopRollAudioProcessor::divisionId)->getCurrentValueAsText() == "1/64", "division non restauree");
    check (restored.getParameter (LoopRollAudioProcessor::refreshId)->getValue() > 0.5f, "refresh non restaure");
    check (restored.getParameter (LoopRollAudioProcessor::refreshTimeId)->getCurrentValueAsText() == "2.50 s", "intervalle free non restaure");
    check (restored.getParameter (LoopRollAudioProcessor::refreshDivisionId)->getCurrentValueAsText() == "1/8", "intervalle sync non restaure");
    check (restored.getParameter (LoopRollAudioProcessor::fadeInId)->getCurrentValueAsText() == "12 %", "fondu d'entree non restaure");
    check (restored.getParameter (LoopRollAudioProcessor::fadeOutId)->getCurrentValueAsText() == "80 %", "fondu de sortie non restaure");

    const auto restoredText = [&restored] (const char* id) { return restored.getParameter (id)->getCurrentValueAsText(); };
    check (restored.getParameter (LoopRollAudioProcessor::stutterId)->getValue() > 0.5f, "stutter non restaure");
    check (restoredText (LoopRollAudioProcessor::refreshModeId) == "Sync", "le mode du refresh doit rester independant de celui de la boucle");
    check (restoredText (LoopRollAudioProcessor::stutterModeId) == "Free", "mode du stutter non restaure");
    check (restoredText (LoopRollAudioProcessor::stutterTimeId) == "33.0 ms", "pas de stutter non restaure : " + restoredText (LoopRollAudioProcessor::stutterTimeId));
    check (restoredText (LoopRollAudioProcessor::stutterDivisionId) == "1/32", "division de stutter non restauree");
    check (restoredText (LoopRollAudioProcessor::stutterShapeId) == "85 %", "forme non restauree");
    check (restoredText (LoopRollAudioProcessor::orderId) == "Stutter > Loop", "ordre non restaure");
    check (restored.getParameter (LoopRollAudioProcessor::filterId)->getValue() > 0.5f, "filtre non restaure");
    check (restoredText (LoopRollAudioProcessor::filterTypeId) == "Band Pass", "type de filtre non restaure");
    check (restoredText (LoopRollAudioProcessor::filterCutoffId) == "2.50 kHz", "coupure non restauree : " + restoredText (LoopRollAudioProcessor::filterCutoffId));
    check (restoredText (LoopRollAudioProcessor::filterResonanceId) == "64 %", "resonance non restauree");

    // Projet sauvegardé avec la v0.3 : un seul mode pour la boucle et le refresh, aucun des nouveaux réglages.
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        check (xml != nullptr, "etat illisible");

        if (xml != nullptr)
        {
            const juce::StringArray known { "loop", "mode", "time", "division", "refresh", "refreshTime", "refreshDivision", "fadeIn", "fadeOut" };

            for (int i = xml->getNumChildElements(); --i >= 0;)
                if (! known.contains (xml->getChildElement (i)->getStringAttribute ("id")))
                    xml->removeChildElement (xml->getChildElement (i), true);

            check (xml->getNumChildElements() == known.size(), "etat v0.3 mal reconstitue");

            juce::MemoryBlock oldState;
            juce::AudioProcessor::copyXmlToBinary (*xml, oldState);

            std::unique_ptr<juce::AudioProcessor> c (createPluginFilter());
            c->setStateInformation (oldState.getData(), (int) oldState.getSize());
            auto& old = dynamic_cast<LoopRollAudioProcessor&> (*c).apvts;

            check (old.getParameter (LoopRollAudioProcessor::modeId)->getCurrentValueAsText() == "Free", "etat v0.3 : mode de boucle");
            check (old.getParameter (LoopRollAudioProcessor::refreshModeId)->getCurrentValueAsText() == "Free", "etat v0.3 : le refresh doit reprendre le mode de la boucle");
            check (old.getParameter (LoopRollAudioProcessor::refreshTimeId)->getCurrentValueAsText() == "2.50 s", "etat v0.3 : intervalle de refresh");
            check (old.getParameter (LoopRollAudioProcessor::stutterId)->getValue() < 0.5f
                     && old.getParameter (LoopRollAudioProcessor::filterId)->getValue() < 0.5f, "etat v0.3 : stutter et filtre doivent rester eteints");
        }
    }

    // Un état corrompu ne doit rien casser.
    const char garbage[] = "pas un etat valide";
    b->setStateInformation (garbage, (int) sizeof (garbage));
    b->setStateInformation (nullptr, 0);
}

static void testEditor (const juce::File& outputDir)
{
    std::cout << "- interface" << std::endl;

    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    check (editor != nullptr, "pas d'interface");

    if (editor == nullptr)
        return;

    check (editor->getWidth() > 100 && editor->getHeight() > 100, "taille d'interface invalide");

    auto snapshot = [&] (const juce::String& name)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

        if (outputDir == juce::File())
            return;

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto file = outputDir.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        check (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream), "ecriture de " + file.getFullPathName());
    };

    using Ids = LoopRollAudioProcessor;
    check (editor->getWidth() == 1104 && editor->getHeight() == 568, "taille d'interface : " + juce::String (editor->getWidth()) + " x " + juce::String (editor->getHeight()));

    const auto knob = [&editor] (const char* id) { return dynamic_cast<juce::Slider*> (editor->findChildWithID (id)); };
    const auto button = [&editor] (const juce::String& id) { return dynamic_cast<juce::TextButton*> (editor->findChildWithID (id)); };

    const auto readouts = [&editor]
    {
        juce::StringArray texts;
        for (auto* child : editor->getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child))
                texts.add (label->getText());
        return texts.joinIntoString (" | ");
    };

    snapshot ("ui_default");
    check (readouts() == "1/4 | 1/1 | 1/16 | 20 Hz", "afficheurs par defaut : " + readouts());

    bool allFound = true;
    for (const char* id : { Ids::timeId, Ids::divisionId, Ids::refreshTimeId, Ids::refreshDivisionId, Ids::stutterTimeId, Ids::stutterDivisionId,
                            Ids::filterCutoffId, Ids::fadeInId, Ids::fadeOutId, Ids::stutterShapeId, Ids::filterResonanceId })
        allFound = allFound && knob (id) != nullptr;

    for (const char* id : { Ids::modeId, Ids::refreshModeId, Ids::stutterModeId, Ids::filterTypeId, Ids::orderId })
        allFound = allFound && button (juce::String (id) + "/0") != nullptr && button (juce::String (id) + "/1") != nullptr;

    check (allFound, "controles introuvables dans l'interface");

    if (allFound)
    {
        // Tous les contrôles tiennent dans la façade et aucun ne chevauche un autre (hors potards superposés d'un même module).
        juce::Array<juce::Component*> controls;
        for (auto* child : editor->getChildren())
            if (child->isVisible())
                controls.add (child);

        for (auto* control : controls)
            check (editor->getLocalBounds().reduced (12).contains (control->getBounds()), "controle hors de la facade : " + control->getComponentID());

        for (int i = 0; i < controls.size(); ++i)
            for (int j = i + 1; j < controls.size(); ++j)
                check (! controls[i]->getBounds().intersects (controls[j]->getBounds()),
                       "controles qui se chevauchent : " + controls[i]->getComponentID() + " / " + controls[j]->getComponentID());

        // Tout allumé, en sync
        setParam (*p, Ids::divisionId, 4.0f);          // 1/16
        setParam (*p, Ids::refreshDivisionId, 4.0f);   // 1/4
        setParam (*p, Ids::refreshId, 1.0f);
        setParam (*p, Ids::loopId, 1.0f);
        setParam (*p, Ids::fadeInId, 20.0f);
        setParam (*p, Ids::fadeOutId, 35.0f);
        setParam (*p, Ids::stutterId, 1.0f);
        setParam (*p, Ids::stutterDivisionId, 5.0f);   // 1/32
        setParam (*p, Ids::stutterShapeId, 30.0f);
        setParam (*p, Ids::filterId, 1.0f);
        setParam (*p, Ids::filterCutoffId, 320.0f);
        setParam (*p, Ids::filterResonanceId, 60.0f);
        snapshot ("ui_all_on");
        check (readouts() == "1/16 | 1/4 | 1/32 | 320 Hz", "afficheurs en sync : " + readouts());

        // Chaque module a son propre mode : on ne passe que la boucle en FREE, par un clic sur son interrupteur.
        button (juce::String (Ids::modeId) + "/0")->onClick();
        setParam (*p, Ids::timeId, 1500.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        check (p->getParameters().size() == 20, "nombre de parametres : " + juce::String (p->getParameters().size()));
        check (dynamic_cast<LoopRollAudioProcessor&> (*p).apvts.getParameter (Ids::modeId)->getValue() < 0.5f, "le clic sur FREE ne change pas le parametre");
        check (knob (Ids::timeId)->isVisible() && ! knob (Ids::divisionId)->isVisible(), "boucle en free : mauvais potard visible");
        check (knob (Ids::refreshDivisionId)->isVisible() && ! knob (Ids::refreshTimeId)->isVisible(), "le refresh aurait du rester en sync");
        check (knob (Ids::stutterDivisionId)->isVisible() && ! knob (Ids::stutterTimeId)->isVisible(), "le stutter aurait du rester en sync");
        check (button (juce::String (Ids::modeId) + "/0")->getToggleState() && ! button (juce::String (Ids::modeId) + "/1")->getToggleState(),
               "interrupteur de la boucle : mauvais cote allume");
        check (button (juce::String (Ids::refreshModeId) + "/1")->getToggleState(), "interrupteur du refresh : mauvais cote allume");
        check (readouts() == "1.50 s | 1/4 | 1/32 | 320 Hz", "afficheurs, boucle en free : " + readouts());

        // 1,5 s sur une plage 10 ms - 4 s : le potard doit être nettement à gauche (long = gauche).
        const double position = knob (Ids::timeId)->valueToProportionOfLength (knob (Ids::timeId)->getValue());
        check (position > 0.05 && position < 0.3, "position du potard de duree : " + juce::String (position));

        // Puis tout en FREE, filtre en passe-bande, stutter avant la boucle.
        setParam (*p, Ids::refreshModeId, 0.0f);
        setParam (*p, Ids::refreshTimeId, 4000.0f);
        setParam (*p, Ids::stutterModeId, 0.0f);
        setParam (*p, Ids::stutterTimeId, 62.5f);
        setParam (*p, Ids::stutterShapeId, 90.0f);
        button (juce::String (Ids::filterTypeId) + "/1")->onClick();
        button (juce::String (Ids::orderId) + "/0")->onClick();
        setParam (*p, Ids::filterCutoffId, 2500.0f);
        setParam (*p, Ids::refreshId, 0.0f);
        snapshot ("ui_free_bandpass");

        auto& apvts = dynamic_cast<LoopRollAudioProcessor&> (*p).apvts;
        check (apvts.getParameter (Ids::filterTypeId)->getCurrentValueAsText() == "Band Pass", "le clic sur BP ne change pas le parametre");
        check (apvts.getParameter (Ids::orderId)->getCurrentValueAsText() == "Stutter > Loop", "le clic sur l'ordre ne change pas le parametre");
        check (knob (Ids::refreshTimeId)->isVisible() && knob (Ids::stutterTimeId)->isVisible(), "tout en free : mauvais potards visibles");
        check (knob (Ids::fadeInId)->getTextFromValue (knob (Ids::fadeInId)->getValue()) == "20 %", "valeur de fondu");
        check (readouts() == "1.50 s | 4.00 s | 62.5 ms | 2.50 kHz", "afficheurs en free : " + readouts());

        // Saisie au clavier dans un afficheur
        for (auto* child : editor->getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child))
                if (label->getText() == "2.50 kHz")
                {
                    label->setText ("800", juce::sendNotificationSync);
                    break;
                }

        check (apvts.getParameter (Ids::filterCutoffId)->getCurrentValueAsText() == "800 Hz",
               "saisie de la coupure : " + apvts.getParameter (Ids::filterCutoffId)->getCurrentValueAsText());
    }

    p->editorBeingDeleted (editor.get());   // ce que fait un hôte avant de détruire l'interface
    editor.reset();
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
        for (int blockSize : { 64, 512, 441 })
            testSyncAndFree (sampleRate, blockSize, 2);

    testSyncAndFree (48000.0, 256, 1);
    testFades();
    testStutter();
    testFilter();
    testParametersAndState();
    testEditor (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])) : juce::File());

    std::cout << (failures == 0 ? "TOUS LES TESTS PASSENT" : juce::String (failures) + " ECHEC(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
