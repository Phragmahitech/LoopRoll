#include "PluginEditor.h"

//==============================================================================
LoopRollLookAndFeel::LoopRollLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, background());

    setColour (juce::Slider::textBoxTextColourId, text());
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accent().withAlpha (0.35f));

    setColour (juce::Label::textColourId, text());
    setColour (juce::Label::textWhenEditingColourId, text());
    setColour (juce::Label::backgroundWhenEditingColourId, panel());
    setColour (juce::Label::outlineWhenEditingColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::textColourId, text());
    setColour (juce::TextEditor::highlightColourId, accent().withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, text());
    setColour (juce::CaretComponent::caretColourId, accent());

    setColour (juce::TextButton::buttonColourId, panel());
    setColour (juce::TextButton::buttonOnColourId, accent());
    setColour (juce::TextButton::textColourOffId, dimText());
    setColour (juce::TextButton::textColourOnId, background());
}

void LoopRollLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                            float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (10.0f);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float thickness = 6.0f;
    const float arcRadius = radius - thickness * 0.5f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const juce::PathStrokeType stroke (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (panel());
    g.strokePath (track, stroke);

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, angle, true);
    g.setColour (accent());
    g.strokePath (value, stroke);

    // Corps du potard + repère
    const float bodyRadius = arcRadius - 14.0f;
    g.setColour (panel());
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre));

    juce::Path pointer;
    pointer.addRoundedRectangle (-2.0f, -bodyRadius + 6.0f, 4.0f, bodyRadius * 0.42f, 2.0f);
    g.setColour (text());
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

    // Crans pour les sliders à pas (divisions du mode sync)
    const int numSteps = slider.getInterval() > 0.0 ? juce::roundToInt ((slider.getMaximum() - slider.getMinimum()) / slider.getInterval()) : 0;

    if (numSteps > 0 && numSteps <= 16)
    {
        for (int i = 0; i <= numSteps; ++i)
        {
            const float a = rotaryStartAngle + ((float) i / (float) numSteps) * (rotaryEndAngle - rotaryStartAngle);
            const auto p = centre.getPointOnCircumference (bodyRadius + 7.0f, a);
            g.setColour (a <= angle + 0.001f ? accent() : dimText().withAlpha (0.6f));
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (p));
        }
    }
}

void LoopRollLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                                bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = backgroundColour;

    if (down)
        colour = colour.brighter (0.15f);
    else if (highlighted)
        colour = colour.brighter (0.07f);

    const float corner = 6.0f;
    juce::Path shape;
    shape.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), corner, corner,
                               ! button.isConnectedOnLeft(), ! button.isConnectedOnRight(),
                               ! button.isConnectedOnLeft(), ! button.isConnectedOnRight());
    g.setColour (colour);
    g.fillPath (shape);
}

juce::Font LoopRollLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions().withHeight (juce::jmin (17.0f, (float) buttonHeight * 0.42f)).withStyle ("Bold"));
}

juce::Font LoopRollLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font (juce::FontOptions().withHeight (20.0f).withStyle ("Bold"));
}

//==============================================================================
LoopRollAudioProcessorEditor::LoopRollAudioProcessorEditor (LoopRollAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);

    configureKnob (timeKnob);
    configureKnob (divisionKnob);
    divisionKnob.setTextBoxIsEditable (false);

    timeAttachment     = std::make_unique<APVTS::SliderAttachment> (audioProcessor.apvts, LoopRollAudioProcessor::timeId, timeKnob);
    divisionAttachment = std::make_unique<APVTS::SliderAttachment> (audioProcessor.apvts, LoopRollAudioProcessor::divisionId, divisionKnob);

    // Sélecteur FREE | SYNC
    for (auto* b : { &freeButton, &syncButton })
    {
        b->setClickingTogglesState (false);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    freeButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    syncButton.setConnectedEdges (juce::Button::ConnectedOnLeft);

    modeAttachment = std::make_unique<juce::ParameterAttachment> (*audioProcessor.apvts.getParameter (LoopRollAudioProcessor::modeId),
                                                                  [this] (float value) { showMode (value > 0.5f); });
    freeButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture (0.0f); };
    syncButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture (1.0f); };
    modeAttachment->sendInitialUpdate();

    // LOOP : bouton à bascule
    loopButton.setClickingTogglesState (true);
    loopButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (loopButton);
    loopAttachment = std::make_unique<APVTS::ButtonAttachment> (audioProcessor.apvts, LoopRollAudioProcessor::loopId, loopButton);

    setSize (280, 400);
}

LoopRollAudioProcessorEditor::~LoopRollAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void LoopRollAudioProcessorEditor::configureKnob (juce::Slider& knob)
{
    addChildComponent (knob);   // d'abord : la zone de texte prend ses couleurs du look du parent
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 120, 28);
    knob.setWantsKeyboardFocus (false);
}

void LoopRollAudioProcessorEditor::showMode (bool sync)
{
    freeButton.setToggleState (! sync, juce::dontSendNotification);
    syncButton.setToggleState (sync, juce::dontSendNotification);
    timeKnob.setVisible (! sync);
    divisionKnob.setVisible (sync);
}

void LoopRollAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (LoopRollLookAndFeel::background());

    g.setColour (LoopRollLookAndFeel::dimText());
    g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f).withStyle ("Bold")).withExtraKerningFactor (0.25f));
    g.drawText ("LOOPROLL", getLocalBounds().removeFromTop (44), juce::Justification::centred);
}

void LoopRollAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (24, 0);
    area.removeFromTop (44);
    area.removeFromBottom (24);

    loopButton.setBounds (area.removeFromBottom (56));
    area.removeFromBottom (16);

    auto modeRow = area.removeFromBottom (32);
    freeButton.setBounds (modeRow.removeFromLeft (modeRow.getWidth() / 2));
    syncButton.setBounds (modeRow);
    area.removeFromBottom (12);

    timeKnob.setBounds (area);
    divisionKnob.setBounds (area);
}
