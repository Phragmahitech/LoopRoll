#pragma once

#include "PluginProcessor.h"

/**
    Design clair : plaques à pans coupés, gris clair et blanc, accent en dégradé
    bleu électrique -> cyan. Police Chakra Petch embarquée.
*/
class LoopRollLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LoopRollLookAndFeel();

    // Palette
    static juce::Colour ground()    { return juce::Colour (0xffdce2e9); }
    static juce::Colour plate()     { return juce::Colour (0xfff5f7fa); }
    static juce::Colour bay()       { return juce::Colour (0xffebeff4); }
    static juce::Colour line()      { return juce::Colour (0xffb4becb); }
    static juce::Colour seam()      { return juce::Colour (0xffc5ced9); }
    static juce::Colour track()     { return juce::Colour (0xffcdd5df); }
    static juce::Colour ink()       { return juce::Colour (0xff101622); }
    static juce::Colour dimInk()    { return juce::Colour (0xff5b6676); }
    static juce::Colour blue()      { return juce::Colour (0xff1447ff); }
    static juce::Colour cyan()      { return juce::Colour (0xff1de4f5); }
    static juce::Colour standby()   { return juce::Colour (0xff93a3bf); }
    static juce::Colour unlit()     { return juce::Colour (0xffc3ccd8); }

    /** Dégradé d'accent, du bleu électrique au cyan. holdBlue garde le bleu sur les deux premiers tiers (texte blanc lisible). */
    static juce::ColourGradient accentGradient (juce::Point<float> from, juce::Point<float> to, bool holdBlue = false);
    static juce::Colour accentAt (float proportion);

    static juce::Path chamferedRect (juce::Rectangle<float> area, float cut);

    /** Trait en dégradé avec un halo lumineux autour. */
    static void strokeGlowing (juce::Graphics&, const juce::Path&, float thickness,
                               const juce::ColourGradient&, bool roundedEnds = true);

    juce::Font font (float pointHeight, bool bold, float tracking = 0.0f) const;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont (juce::Label&) override;

private:
    void drawMainKnob (juce::Graphics&, juce::Rectangle<float> bounds, float sliderPos,
                       float startAngle, float endAngle, juce::Slider&);
    void drawMiniKnob (juce::Graphics&, juce::Rectangle<float> bounds, float sliderPos,
                       float startAngle, float endAngle);

    juce::Typeface::Ptr semiBoldFace, boldFace;
};

//==============================================================================
class LoopRollAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit LoopRollAudioProcessorEditor (LoopRollAudioProcessor&);
    ~LoopRollAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    void configureMainKnob (juce::Slider&, const char* parameterId);
    void configureMiniKnob (juce::Slider&, const char* parameterId);
    void configureValueLabel (juce::Label&, bool isRefresh);
    void configureButton (juce::TextButton&, bool togglesOnClick, bool big);

    void modeChanged (bool sync);
    void updateValueLabels();
    void applyTypedValue (juce::Label&, juce::Slider&);

    juce::Slider& activeLengthKnob()   { return syncMode ? divisionKnob : timeKnob; }
    juce::Slider& activeRefreshKnob()  { return syncMode ? refreshDivisionKnob : refreshTimeKnob; }

    void paintPlates (juce::Graphics&);
    void paintLights (juce::Graphics&);
    void paintCentre (juce::Graphics&);
    void paintReadouts (juce::Graphics&);

    LoopRollAudioProcessor& audioProcessor;
    LoopRollLookAndFeel lookAndFeel;

    bool syncMode = true, loopOn = false, refreshOn = false;

    // Chaque grand potard est fait de deux sliders superposés, un par mode : ms en FREE, divisions en SYNC.
    juce::Slider timeKnob, divisionKnob, refreshTimeKnob, refreshDivisionKnob;
    juce::Slider fadeInKnob, fadeOutKnob;
    juce::Label lengthValue, refreshValue;
    juce::TextButton freeButton { "FREE" }, syncButton { "SYNC" }, loopButton { "LOOP" }, refreshButton { "REFRESH" };

    std::unique_ptr<APVTS::SliderAttachment> timeAttachment, divisionAttachment, refreshTimeAttachment,
                                             refreshDivisionAttachment, fadeInAttachment, fadeOutAttachment;
    std::unique_ptr<APVTS::ButtonAttachment> loopAttachment, refreshAttachment;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment, loopStateAttachment, refreshStateAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopRollAudioProcessorEditor)
};
