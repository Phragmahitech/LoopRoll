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
    setParam (*p, LoopRollAudioProcessor::divisionId, 2.0f);   // 1/16
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

    auto* division = apvts.getParameter (LoopRollAudioProcessor::divisionId);
    check (division->getCurrentValueAsText() == "1/4", "division par defaut : " + division->getCurrentValueAsText());
    check (apvts.getParameter (LoopRollAudioProcessor::modeId)->getCurrentValueAsText() == "Sync", "mode par defaut");

    setParam (*a, LoopRollAudioProcessor::modeId, 0.0f);
    setParam (*a, LoopRollAudioProcessor::timeId, 777.0f);
    setParam (*a, LoopRollAudioProcessor::divisionId, 6.0f);

    juce::MemoryBlock state;
    a->getStateInformation (state);

    std::unique_ptr<juce::AudioProcessor> b (createPluginFilter());
    b->setStateInformation (state.getData(), (int) state.getSize());
    auto& restored = dynamic_cast<LoopRollAudioProcessor&> (*b).apvts;

    check (restored.getParameter (LoopRollAudioProcessor::modeId)->getCurrentValueAsText() == "Free", "mode non restaure");
    check (restored.getParameter (LoopRollAudioProcessor::timeId)->getCurrentValueAsText() == "777 ms", "duree non restauree");
    check (restored.getParameter (LoopRollAudioProcessor::divisionId)->getCurrentValueAsText() == "1/1", "division non restauree");

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

    setParam (*p, LoopRollAudioProcessor::modeId, 0.0f);
    setParam (*p, LoopRollAudioProcessor::timeId, 1500.0f);
    setParam (*p, LoopRollAudioProcessor::loopId, 1.0f);
    snapshot ("ui_free_loop_on");

    // L'interface suit bien les paramètres : en mode free c'est le potard de durée qui est visible.
    int visibleSliders = 0;
    juce::String shownText;
    for (auto* child : editor->getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*> (child))
            if (slider->isVisible())
            {
                ++visibleSliders;
                shownText = slider->getTextFromValue (slider->getValue());
            }

    check (visibleSliders == 1, "un seul potard doit etre visible (" + juce::String (visibleSliders) + ")");
    check (shownText == "1.50 s", "valeur affichee en mode free : " + shownText);

    editor.reset();
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
        for (int blockSize : { 64, 512, 441 })
            testSyncAndFree (sampleRate, blockSize, 2);

    testSyncAndFree (48000.0, 256, 1);
    testParametersAndState();
    testEditor (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])) : juce::File());

    std::cout << (failures == 0 ? "TOUS LES TESTS PASSENT" : juce::String (failures) + " ECHEC(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
