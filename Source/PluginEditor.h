#pragma once

#include "PluginProcessor.h"
#include "PresetBar.h"

/**
    Design clair : plaques à pans coupés, gris clair et blanc, accent en dégradé
    bleu -> cyan. Le relief vient d'ombres portées (ce qui dépasse) et d'ombres
    intérieures (ce qui est en creux). Police Chakra Petch embarquée.
*/
class LoopRollLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LoopRollLookAndFeel();

    // Palette
    static juce::Colour ground()    { return juce::Colour (0xffd9dfe7); }
    static juce::Colour plate()     { return juce::Colour (0xfff5f7fa); }
    static juce::Colour bay()       { return juce::Colour (0xffe6eaf0); }
    static juce::Colour line()      { return juce::Colour (0xffb4becb); }
    static juce::Colour seam()      { return juce::Colour (0xffc5ced9); }
    static juce::Colour track()     { return juce::Colour (0xffcdd5df); }
    static juce::Colour ink()       { return juce::Colour (0xff101622); }
    static juce::Colour dimInk()    { return juce::Colour (0xff5b6676); }
    static juce::Colour blue()      { return juce::Colour (0xff1785ff); }
    static juce::Colour cyan()      { return juce::Colour (0xff1de4f5); }
    static juce::Colour standby()   { return juce::Colour (0xff93a3bf); }
    static juce::Colour unlit()     { return juce::Colour (0xffc3ccd8); }
    static juce::Colour shadow()    { return juce::Colour (0xff1b2b4b); }

    /** Dégradé d'accent, du bleu au cyan. holdBlue garde le bleu sur les deux premiers tiers (texte blanc lisible). */
    static juce::ColourGradient accentGradient (juce::Point<float> from, juce::Point<float> to, bool holdBlue = false);
    static juce::Colour accentAt (float proportion);

    static juce::Path chamferedRect (juce::Rectangle<float> area, float cut);

    /** Trait en dégradé avec un halo lumineux autour. */
    static void strokeGlowing (juce::Graphics&, const juce::Path&, float thickness,
                               const juce::ColourGradient&, bool roundedEnds = true);

    /** Ombre portée sous une forme : elle la décolle du fond. Coûteuse : à réserver au fond, dessiné une seule fois. */
    static void dropShadow (juce::Graphics&, const juce::Path&, float radius, float offsetY, float alpha);

    /** Ombre intérieure en haut d'une forme : elle la creuse. Même remarque. */
    static void innerShadow (juce::Graphics&, const juce::Path&, float radius, float offsetY, float alpha);

    /** Remplissage en dégradé vertical. */
    static void fillVertical (juce::Graphics&, const juce::Path&, juce::Rectangle<float> area, juce::Colour top, juce::Colour bottom);

    /** Écran blanc en creux (afficheurs). */
    static void drawScreen (juce::Graphics&, juce::Rectangle<float> area, float cut);

    juce::Font font (float pointHeight, bool bold, float tracking = 0.0f) const;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont (juce::Label&) override;

    /** Rayon du corps d'un grand / petit potard pour une zone donnée (le fond dessine son ombre). */
    static float mainKnobBodyRadius (juce::Rectangle<float> bounds)   { return 58.0f * juce::jmin (bounds.getWidth(), bounds.getHeight()) / 190.0f; }
    static float miniKnobBodyRadius (juce::Rectangle<float> bounds)   { return juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 8.5f; }

private:
    void drawMainKnob (juce::Graphics&, juce::Rectangle<float> bounds, float sliderPos,
                       float startAngle, float endAngle, juce::Slider&);
    void drawMiniKnob (juce::Graphics&, juce::Rectangle<float> bounds, float sliderPos,
                       float startAngle, float endAngle);

    juce::Typeface::Ptr semiBoldFace, boldFace;
};

//==============================================================================
/**
    Les 16 pas du stutter : chaque case dessine la forme du gate sur ce pas
    (avec la profondeur et le swing). Un clic allume ou éteint le pas ; en
    glissant, on peint plusieurs pas d'un coup.
*/
class StutterStepGrid final : public juce::Component
{
public:
    explicit StutterStepGrid (juce::AudioProcessorValueTreeState&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    bool isStepOn (int index) const     { return on[(size_t) juce::jlimit (0, numSteps - 1, index)]; }

    /** Bornes horizontales d'un pas dans le composant (le swing décale un pas sur deux). */
    juce::Range<float> getStepRange (int index) const;

private:
    static constexpr int numSteps = LoopRollAudioProcessor::numStutterSteps;

    juce::Rectangle<float> plotArea() const     { return getLocalBounds().toFloat().reduced (7.0f, 7.0f); }
    int stepAt (float x) const;
    void setStep (int index, bool shouldBeOn);

    std::array<bool, (size_t) numSteps> on {};
    std::array<std::unique_ptr<juce::ParameterAttachment>, (size_t) numSteps> attachments;
    std::atomic<float>* shape = nullptr;
    std::atomic<float>* depth = nullptr;
    std::atomic<float>* swing = nullptr;
    bool paintValue = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StutterStepGrid)
};

//==============================================================================
/**
    Quatre modules côte à côte : LOOP, REFRESH, STUTTER, FILTER.
    Chacun a son grand potard, son afficheur, ses interrupteurs, son bouton
    marche / arrêt et un panneau de réglages en dessous.
*/
class LoopRollFacade final : public juce::Component
{
public:
    static constexpr int width = 1104, height = 642;

    explicit LoopRollFacade (LoopRollAudioProcessor&);
    ~LoopRollFacade() override;

    phragma::PresetBar& getPresetBar() noexcept     { return presetBar; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    /** Interrupteur à plusieurs positions relié à un paramètre à choix. */
    struct ChoiceSwitch
    {
        juce::OwnedArray<juce::TextButton> buttons;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    /** Variante rythmique d'un module en SYNC : T (triolet) et D (pointé), tous deux éteints = normal. */
    struct FeelSwitch
    {
        juce::TextButton triplet { "T" }, dotted { "D" };
        std::unique_ptr<juce::ParameterAttachment> attachment;
        int value = 0;
    };

    void configureMainKnob (juce::Slider&, const char* parameterId, int column);
    void configureMiniKnob (juce::Slider&, const char* parameterId, int column, int slot, int numSlots);
    void configureValueLabel (juce::Label&, int column);
    void configureBigButton (juce::TextButton&, int column);
    void configureToggle (juce::TextButton&, const char* parameterId, int column, std::function<void (bool)> onChange);
    void configureSmallToggle (juce::TextButton&, const char* parameterId, juce::Rectangle<int> area);
    void configureSwitch (ChoiceSwitch&, const char* parameterId, const juce::StringArray& texts,
                          juce::Rectangle<int> area, bool stacked, bool tiny, std::function<void (int)> onChange);
    void configureFeel (FeelSwitch&, const char* parameterId, int column);

    /** Le potard affiché dans un module (celui en ms ou celui en divisions, selon son mode). */
    juce::Slider& activeKnob (int column);
    bool isSynced (int column) const;
    void setStandby (int column, bool standby);
    void modesChanged();
    void updateValueLabels();

    void paintBackground (juce::Graphics&);
    void paintLights (juce::Graphics&);
    void paintModules (juce::Graphics&);

    LoopRollAudioProcessor& audioProcessor;
    LoopRollLookAndFeel lookAndFeel;

    // Fond (plaques, ombres, textes fixes) : dessiné une fois, à la résolution de l'écran.
    juce::Image background;
    float backgroundScale = 0.0f;

    bool loopOn = false, refreshOn = false, stutterOn = false, filterOn = false;
    bool loopSync = true, refreshSync = true, stutterSync = true;
    bool pitchInSemitones = true;
    int filterType = 0;
    bool steepFilter = false;

    // Les potards de durée sont faits de deux sliders superposés, un par mode : ms en FREE, divisions en SYNC.
    juce::Slider timeKnob, divisionKnob, refreshTimeKnob, refreshDivisionKnob, stutterTimeKnob, stutterDivisionKnob, cutoffKnob;
    juce::Slider fadeInKnob, fadeOutKnob, mixKnob, pitchKnob, shapeKnob, depthKnob, swingKnob, resonanceKnob;
    juce::Label valueLabels[4];
    juce::TextButton loopButton { "LOOP" }, refreshButton { "REFRESH" }, stutterButton { "STUTTER" }, filterButton { "FILTER" };
    juce::TextButton reverseButton { "REVERSE" }, limiterButton { "LIMITER" };
    ChoiceSwitch loopMode, refreshMode, stutterMode, filterTypeSwitch, filterSlope, chainOrder, pitchMode;
    FeelSwitch loopFeel, refreshFeel, stutterFeel;
    StutterStepGrid stepGrid;

    // Déclarés après les contrôles : détruits avant eux.
    std::vector<std::unique_ptr<APVTS::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> stateWatchers;
    std::unique_ptr<juce::ParameterAttachment> loopAttachment, pitchAttachment;

    // Barre des presets et de la taille (en-tête) : déclarée en dernier, détruite en premier.
    phragma::PresetBar presetBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopRollFacade)
};

//==============================================================================
/** L'éditeur : la façade, mise à l'échelle de la fenêtre (redimensionnable de 60 à 150 %, proportions gardées). */
class LoopRollAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit LoopRollAudioProcessorEditor (LoopRollAudioProcessor&);
    ~LoopRollAudioProcessorEditor() override;

    void resized() override;

    LoopRollFacade& getFacade() noexcept                 { return facade; }

    /** Taille de la fenêtre (1 = 100 %). */
    float getUiScale() const                  { return (float) getWidth() / (float) LoopRollFacade::width; }
    void setUiScale (float scale);

private:
    LoopRollAudioProcessor& audioProcessor;
    LoopRollFacade facade;
    bool sized = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopRollAudioProcessorEditor)
};
