#pragma once

#include "PluginProcessor.h"

/** Look minimal : fond sombre, potards à arc, boutons plats. */
class LoopRollLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LoopRollLookAndFeel();

    static juce::Colour background()   { return juce::Colour (0xff121316); }
    static juce::Colour panel()        { return juce::Colour (0xff22242a); }
    static juce::Colour text()         { return juce::Colour (0xffe8e9ec); }
    static juce::Colour dimText()      { return juce::Colour (0xff8a8d96); }
    static juce::Colour accent()       { return juce::Colour (0xff35e0a1); }

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
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

    void configureKnob (juce::Slider&, bool editableText);
    void configureToggle (juce::TextButton&);
    void showMode (bool sync);
    void showRefreshState (bool enabled);

    LoopRollAudioProcessor& audioProcessor;
    LoopRollLookAndFeel lookAndFeel;

    // Deux potards à l'écran (durée de boucle, intervalle de refresh). Chacun est fait de
    // deux sliders superposés, un par mode : ms en FREE, divisions en SYNC.
    juce::Slider timeKnob, divisionKnob, refreshTimeKnob, refreshDivisionKnob;
    juce::TextButton freeButton { "FREE" }, syncButton { "SYNC" }, loopButton { "LOOP" }, refreshButton { "REFRESH" };
    juce::Rectangle<int> lengthLabelArea, refreshLabelArea;

    std::unique_ptr<APVTS::SliderAttachment> timeAttachment, divisionAttachment, refreshTimeAttachment, refreshDivisionAttachment;
    std::unique_ptr<APVTS::ButtonAttachment> loopAttachment, refreshAttachment;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment, refreshStateAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopRollAudioProcessorEditor)
};
