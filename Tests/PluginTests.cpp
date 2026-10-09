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

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (true);
        return info;
    }
};

static void setParam (juce::AudioProcessor& p, const char* id, float realValue)
{
    auto* param = dynamic_cast<LoopRollAudioProcessor&> (p).apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

// Fait passer du bruit dans le plugin ; "onBlock" est appelé avant chaque bloc.
static void render (juce::AudioProcessor& p, juce::AudioBuffer<float>& in, juce::AudioBuffer<float>& out, int blockSize,
                    const std::function<void (int)>& onBlock)
{
    juce::Random rng (42);
    for (int ch = 0; ch < in.getNumChannels(); ++ch)
        for (int i = 0; i < in.getNumSamples(); ++i)
            in.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);

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

    setParam (*p, LoopRollAudioProcessor::refreshId, 0.0f);

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

static void testParametersAndState()
{
    std::cout << "- parametres et sauvegarde d'etat" << std::endl;

    std::unique_ptr<juce::AudioProcessor> a (createPluginFilter());
    auto& apvts = dynamic_cast<LoopRollAudioProcessor&> (*a).apvts;

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

    setParam (*a, LoopRollAudioProcessor::modeId, 0.0f);
    setParam (*a, LoopRollAudioProcessor::timeId, 777.0f);
    setParam (*a, LoopRollAudioProcessor::divisionId, 6.0f);
    setParam (*a, LoopRollAudioProcessor::refreshId, 1.0f);
    setParam (*a, LoopRollAudioProcessor::refreshTimeId, 2500.0f);
    setParam (*a, LoopRollAudioProcessor::refreshDivisionId, 5.0f);
    setParam (*a, LoopRollAudioProcessor::fadeInId, 12.0f);
    setParam (*a, LoopRollAudioProcessor::fadeOutId, 80.0f);

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

    snapshot ("ui_sync");

    setParam (*p, LoopRollAudioProcessor::divisionId, 4.0f);          // 1/16
    setParam (*p, LoopRollAudioProcessor::refreshDivisionId, 4.0f);   // 1/4
    setParam (*p, LoopRollAudioProcessor::refreshId, 1.0f);
    setParam (*p, LoopRollAudioProcessor::loopId, 1.0f);
    setParam (*p, LoopRollAudioProcessor::fadeInId, 20.0f);
    setParam (*p, LoopRollAudioProcessor::fadeOutId, 35.0f);
    snapshot ("ui_sync_refresh_on");

    setParam (*p, LoopRollAudioProcessor::modeId, 0.0f);
    setParam (*p, LoopRollAudioProcessor::timeId, 1500.0f);
    setParam (*p, LoopRollAudioProcessor::refreshTimeId, 4000.0f);
    snapshot ("ui_free");

    // L'interface suit bien les paramètres : en mode free ce sont les potards en ms qui sont visibles.
    const auto knob = [&editor] (const char* id) { return dynamic_cast<juce::Slider*> (editor->findChildWithID (id)); };
    auto* time = knob (LoopRollAudioProcessor::timeId);
    auto* division = knob (LoopRollAudioProcessor::divisionId);
    auto* refreshTime = knob (LoopRollAudioProcessor::refreshTimeId);
    auto* refreshDivision = knob (LoopRollAudioProcessor::refreshDivisionId);
    auto* fadeIn = knob (LoopRollAudioProcessor::fadeInId);
    auto* fadeOut = knob (LoopRollAudioProcessor::fadeOutId);

    check (time != nullptr && division != nullptr && refreshTime != nullptr && refreshDivision != nullptr
             && fadeIn != nullptr && fadeOut != nullptr, "potards introuvables dans l'interface");

    if (time != nullptr && division != nullptr && refreshTime != nullptr && refreshDivision != nullptr && fadeIn != nullptr && fadeOut != nullptr)
    {
        check (time->isVisible() && refreshTime->isVisible() && ! division->isVisible() && ! refreshDivision->isVisible(),
               "mode free : mauvais potards visibles");
        check (fadeIn->isVisible() && fadeOut->isVisible(), "potards de fondu masques");
        check (time->getTextFromValue (time->getValue()) == "1.50 s", "valeur de duree : " + time->getTextFromValue (time->getValue()));
        check (refreshTime->getTextFromValue (refreshTime->getValue()) == "4.00 s", "valeur de refresh : " + refreshTime->getTextFromValue (refreshTime->getValue()));
        check (fadeIn->getTextFromValue (fadeIn->getValue()) == "20 %", "valeur de fondu : " + fadeIn->getTextFromValue (fadeIn->getValue()));

        // 1,5 s sur une plage 10 ms - 4 s : le potard doit être nettement à gauche (long = gauche).
        const double position = time->valueToProportionOfLength (time->getValue());
        check (position > 0.05 && position < 0.3, "position du potard de duree : " + juce::String (position));

        // Les afficheurs sous les potards montrent les mêmes valeurs.
        juce::StringArray readouts;
        for (auto* child : editor->getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child))
                readouts.add (label->getText());
        check (readouts.joinIntoString (" | ") == "1.50 s | 4.00 s", "afficheurs : " + readouts.joinIntoString (" | "));
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
    testParametersAndState();
    testEditor (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])) : juce::File());

    std::cout << (failures == 0 ? "TOUS LES TESTS PASSENT" : juce::String (failures) + " ECHEC(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
