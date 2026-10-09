#include "PluginEditor.h"
#include "BinaryData.h"

#ifndef LOOPROLL_VERSION
 #define LOOPROLL_VERSION "dev"
#endif

namespace
{
    using LF = LoopRollLookAndFeel;
    using Pt = juce::Point<float>;

    constexpr int editorWidth = 760, editorHeight = 480;

    // Zones de la façade, en pixels
    const juce::Rectangle<int> lengthKnobArea    { 85, 98, 190, 190 };
    const juce::Rectangle<int> refreshKnobArea   { 485, 98, 190, 190 };
    const juce::Rectangle<int> lengthReadout     { 78, 296, 204, 36 };
    const juce::Rectangle<int> refreshReadout    { 478, 296, 204, 36 };
    const juce::Rectangle<int> freeButtonArea    { 342, 116, 76, 44 };
    const juce::Rectangle<int> syncButtonArea    { 342, 168, 76, 44 };
    const juce::Rectangle<int> envelopeArea      { 342, 224, 76, 30 };
    const juce::Rectangle<int> fadeInKnobArea    { 338, 260, 40, 40 };
    const juce::Rectangle<int> fadeOutKnobArea   { 382, 260, 40, 40 };
    const juce::Rectangle<int> loopButtonArea    { 34, 362, 292, 60 };
    const juce::Rectangle<int> refreshButtonArea { 434, 362, 292, 60 };

    juce::Path polyline (std::initializer_list<Pt> points, bool closed = false)
    {
        juce::Path path;
        bool first = true;

        for (const auto point : points)
        {
            if (first)
                path.startNewSubPath (point);
            else
                path.lineTo (point);

            first = false;
        }

        if (closed)
            path.closeSubPath();

        return path;
    }

    float mirrorX (float x)                         { return (float) editorWidth - x; }
    Pt mirror (Pt point)                            { return { mirrorX (point.x), point.y }; }

    /** Symétrie gauche / droite de la façade. */
    juce::Path mirrored (const juce::Path& path)
    {
        auto copy = path;
        copy.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f).translated ((float) editorWidth, 0.0f));
        return copy;
    }

    /** "4.00 s" -> "4 s", "10.0 ms" -> "10 ms" : pour les repères de début et de fin des potards. */
    juce::String compactValue (const juce::String& text)
    {
        return text.replace (".00 ", " ").replace (".0 ", " ");
    }

    float textWidth (const juce::Font& font, const juce::String& text)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, 0.0f, 0.0f);
        return glyphs.getBoundingBox (0, -1, true).getWidth();
    }

    /** Halo lumineux autour d'une forme allumée. */
    void drawHalo (juce::Graphics& g, const juce::Path& outline, const juce::ColourGradient& gradient)
    {
        struct Layer { float width, alpha; };

        for (const auto layer : { Layer { 16.0f, 0.06f }, Layer { 10.0f, 0.09f }, Layer { 5.0f, 0.13f } })
        {
            g.setGradientFill (gradient);
            g.setOpacity (layer.alpha);
            g.strokePath (outline, juce::PathStrokeType (layer.width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setOpacity (1.0f);
    }
}

//==============================================================================
LoopRollLookAndFeel::LoopRollLookAndFeel()
{
    semiBoldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchSemiBold_ttf,
                                                            (size_t) BinaryData::ChakraPetchSemiBold_ttfSize);
    boldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchBold_ttf,
                                                        (size_t) BinaryData::ChakraPetchBold_ttfSize);

    setColour (juce::ResizableWindow::backgroundColourId, ground());

    setColour (juce::Label::textColourId, ink());
    setColour (juce::Label::textWhenEditingColourId, ink());
    setColour (juce::Label::backgroundWhenEditingColourId, juce::Colours::white);
    setColour (juce::Label::outlineWhenEditingColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::textColourId, ink());
    setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::highlightColourId, cyan().withAlpha (0.45f));
    setColour (juce::TextEditor::highlightedTextColourId, ink());
    setColour (juce::CaretComponent::caretColourId, blue());
}

juce::ColourGradient LoopRollLookAndFeel::accentGradient (juce::Point<float> from, juce::Point<float> to, bool holdBlue)
{
    juce::ColourGradient gradient (blue(), from, cyan(), to, false);

    if (holdBlue)
        gradient.addColour (0.62, juce::Colour (0xff1668ff));

    return gradient;
}

juce::Colour LoopRollLookAndFeel::accentAt (float proportion)
{
    return blue().interpolatedWith (cyan(), juce::jlimit (0.0f, 1.0f, proportion));
}

juce::Path LoopRollLookAndFeel::chamferedRect (juce::Rectangle<float> area, float cut)
{
    const float c = juce::jmax (0.0f, juce::jmin (cut, area.getWidth() * 0.5f, area.getHeight() * 0.5f));
    const float x = area.getX(), y = area.getY(), r = area.getRight(), b = area.getBottom();

    juce::Path path;
    path.startNewSubPath (x + c, y);
    path.lineTo (r - c, y);
    path.lineTo (r, y + c);
    path.lineTo (r, b - c);
    path.lineTo (r - c, b);
    path.lineTo (x + c, b);
    path.lineTo (x, b - c);
    path.lineTo (x, y + c);
    path.closeSubPath();
    return path;
}

void LoopRollLookAndFeel::strokeGlowing (juce::Graphics& g, const juce::Path& path, float thickness,
                                         const juce::ColourGradient& gradient, bool roundedEnds)
{
    struct Layer { float extra, alpha; };

    for (const auto layer : { Layer { 12.0f, 0.07f }, Layer { 8.0f, 0.10f }, Layer { 4.0f, 0.16f } })
    {
        g.setGradientFill (gradient);
        g.setOpacity (layer.alpha);
        g.strokePath (path, juce::PathStrokeType (thickness + layer.extra, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setGradientFill (gradient);
    g.setOpacity (1.0f);
    g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                              roundedEnds ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
}

juce::Font LoopRollLookAndFeel::font (float pointHeight, bool bold, float tracking) const
{
    const juce::Font base (juce::FontOptions (bold ? boldFace : semiBoldFace).withPointHeight (pointHeight));
    return tracking > 0.0f ? base.withExtraKerningFactor (tracking) : base;
}

juce::Font LoopRollLookAndFeel::getLabelFont (juce::Label&)
{
    return font (20.0f, true);
}

//==============================================================================
void LoopRollLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                            float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();

    if (slider.getProperties().contains ("mini"))
        drawMiniKnob (g, bounds, sliderPos, rotaryStartAngle, rotaryEndAngle);
    else
        drawMainKnob (g, bounds, sliderPos, rotaryStartAngle, rotaryEndAngle, slider);
}

void LoopRollLookAndFeel::drawMainKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float sliderPos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    const auto centre = bounds.getCentre();
    const float scale = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 190.0f;
    const float arcRadius = 76.0f * scale, tickRadius = 89.0f * scale, bodyRadius = 58.0f * scale, capRadius = 44.0f * scale;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const bool isStandby = (bool) slider.getProperties().getWithDefault ("standby", false);

    // Le dégradé court de gauche (bleu) à droite (cyan) : il suit la progression de l'arc.
    const float left = centre.x - arcRadius;
    const auto gradient = accentGradient ({ left, centre.y }, { centre.x + arcRadius, centre.y });
    const auto accentAtX = [left, arcRadius] (float px) { return accentAt ((px - left) / (2.0f * arcRadius)); };
    const juce::PathStrokeType arcStroke (6.0f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (track());
    g.strokePath (trackArc, arcStroke);

    if (angle > startAngle + 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);

        if (isStandby)
        {
            g.setColour (standby());
            g.strokePath (valueArc, arcStroke);
        }
        else
        {
            strokeGlowing (g, valueArc, 6.0f * scale, gradient);
        }
    }

    // Crans pour les potards à pas (divisions du mode sync)
    const int numSteps = slider.getInterval() > 0.0 ? juce::roundToInt ((slider.getMaximum() - slider.getMinimum()) / slider.getInterval()) : 0;

    if (numSteps > 0 && numSteps <= 16)
    {
        for (int i = 0; i <= numSteps; ++i)
        {
            const float a = startAngle + ((float) i / (float) numSteps) * (endAngle - startAngle);
            const auto p = centre.getPointOnCircumference (tickRadius, a);
            const bool lit = a <= angle + 0.001f;

            g.setColour (! lit ? juce::Colour (0xffaeb9c8) : isStandby ? standby() : accentAtX (p.x));
            g.fillEllipse (juce::Rectangle<float> (4.8f * scale, 4.8f * scale).withCentre (p));
        }
    }

    // Corps du potard
    juce::Path body;
    body.addEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre));
    juce::DropShadow (juce::Colour (0x381b2b4b), juce::roundToInt (14.0f * scale), { 0, juce::roundToInt (8.0f * scale) }).drawForPath (g, body);

    g.setGradientFill (juce::ColourGradient (juce::Colours::white, centre.x, centre.y - bodyRadius * 0.35f,
                                             juce::Colour (0xffe1e6ee), centre.x, centre.y + bodyRadius, true));
    g.fillPath (body);
    g.setColour (juce::Colour (0xffb9c2cf));
    g.strokePath (body, juce::PathStrokeType (1.5f * scale));

    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f6fa), centre.x, cap.getY(),
                                             juce::Colour (0xffdae0e9), centre.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (juce::Colour (0xffc9d1dc));
    g.drawEllipse (cap, 1.0f * scale);

    // Repère
    const auto tip = centre.getPointOnCircumference (50.0f * scale, angle);
    juce::Path pointer;
    pointer.startNewSubPath (centre.getPointOnCircumference (24.0f * scale, angle));
    pointer.lineTo (tip);
    g.setColour (isStandby ? juce::Colour (0xff7c8ba6) : accentAtX (tip.x));
    g.strokePath (pointer, juce::PathStrokeType (4.5f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Valeurs de début et de fin de course
    const auto endText = [&slider] (double proportion)
    {
        return compactValue (slider.getTextFromValue (slider.proportionOfLengthToValue (proportion)));
    };

    g.setColour (dimInk());
    g.setFont (font (10.0f * scale, false));
    g.drawText (endText (0.0), juce::Rectangle<float> (64.0f, 14.0f).withCentre ({ centre.x - 65.0f * scale, centre.y + 82.0f * scale }),
                juce::Justification::centred, false);
    g.drawText (endText (1.0), juce::Rectangle<float> (64.0f, 14.0f).withCentre ({ centre.x + 65.0f * scale, centre.y + 82.0f * scale }),
                juce::Justification::centred, false);
}

void LoopRollLookAndFeel::drawMiniKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float sliderPos,
                                        float startAngle, float endAngle)
{
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float arcRadius = radius - 3.0f, bodyRadius = radius - 8.5f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const juce::PathStrokeType arcStroke (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (track());
    g.strokePath (trackArc, arcStroke);

    if (angle > startAngle + 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
        g.setGradientFill (accentGradient ({ centre.x - arcRadius, centre.y }, { centre.x + arcRadius, centre.y }));
        g.strokePath (valueArc, arcStroke);
    }

    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, centre.x, body.getY(),
                                             juce::Colour (0xffdfe5ed), centre.x, body.getBottom(), false));
    g.fillEllipse (body);
    g.setColour (juce::Colour (0xffb9c2cf));
    g.drawEllipse (body, 1.2f);

    juce::Path pointer;
    pointer.startNewSubPath (centre.getPointOnCircumference (bodyRadius * 0.3f, angle));
    pointer.lineTo (centre.getPointOnCircumference (bodyRadius - 2.0f, angle));
    g.setColour (blue());
    g.strokePath (pointer, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LoopRollLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                bool highlighted, bool down)
{
    const float cut = (float) (double) button.getProperties().getWithDefault ("cut", 8.0);
    const auto area = button.getLocalBounds().toFloat();
    const auto shape = chamferedRect (area, cut);

    if (button.getToggleState())
    {
        g.setGradientFill (accentGradient (area.getTopLeft(), area.getTopRight(), true));
        g.fillPath (shape);

        if (down || highlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.2f : 0.08f));
            g.fillPath (shape);
        }
    }
    else
    {
        g.setColour (line());
        g.fillPath (shape);
        g.setColour (down ? juce::Colour (0xffe1e9f7) : highlighted ? juce::Colour (0xffeef3fb) : juce::Colour (0xfff7f9fc));
        g.fillPath (chamferedRect (area.reduced (1.5f), cut - 1.0f));
    }
}

void LoopRollLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const bool on = button.getToggleState();
    const bool big = button.getProperties().contains ("big");
    const auto text = button.getButtonText();
    const auto textFont = font (big ? 18.0f : 13.0f, true, big ? 0.2f : 0.14f);
    const auto textColour = on ? juce::Colours::white : juce::Colour (0xff2a3342);
    const auto area = button.getLocalBounds().toFloat();

    g.setFont (textFont);

    if (! big)
    {
        g.setColour (textColour);
        g.drawText (text, area, juce::Justification::centred, false);
        return;
    }

    // Grand bouton : deux barres obliques (témoin) puis le libellé, le tout centré.
    const float iconWidth = 14.0f, gap = 14.0f;
    const float labelWidth = textWidth (textFont, text);
    const float left = area.getCentreX() - (iconWidth + gap + labelWidth) * 0.5f;
    const float midY = area.getCentreY();

    juce::Path bars;
    bars.startNewSubPath (left + 1.0f, midY - 7.0f);
    bars.lineTo (left + 6.0f, midY + 7.0f);
    bars.startNewSubPath (left + 9.0f, midY - 7.0f);
    bars.lineTo (left + 14.0f, midY + 7.0f);

    g.setColour (on ? juce::Colours::white : juce::Colour (0xffa9b6c8));
    g.strokePath (bars, juce::PathStrokeType (3.0f));

    g.setColour (textColour);
    g.drawText (text, juce::Rectangle<float> (left + iconWidth + gap, area.getY(), labelWidth + 12.0f, area.getHeight()),
                juce::Justification::centredLeft, false);
}

//==============================================================================
LoopRollAudioProcessorEditor::LoopRollAudioProcessorEditor (LoopRollAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& apvts = audioProcessor.apvts;

    configureMainKnob (timeKnob, LoopRollAudioProcessor::timeId);
    configureMainKnob (divisionKnob, LoopRollAudioProcessor::divisionId);
    configureMainKnob (refreshTimeKnob, LoopRollAudioProcessor::refreshTimeId);
    configureMainKnob (refreshDivisionKnob, LoopRollAudioProcessor::refreshDivisionId);
    configureMiniKnob (fadeInKnob, LoopRollAudioProcessor::fadeInId);
    configureMiniKnob (fadeOutKnob, LoopRollAudioProcessor::fadeOutId);

    configureValueLabel (lengthValue, false);
    configureValueLabel (refreshValue, true);

    timeAttachment            = std::make_unique<APVTS::SliderAttachment> (apvts, LoopRollAudioProcessor::timeId, timeKnob);
    divisionAttachment        = std::make_unique<APVTS::SliderAttachment> (apvts, LoopRollAudioProcessor::divisionId, divisionKnob);
    refreshTimeAttachment     = std::make_unique<APVTS::SliderAttachment> (apvts, LoopRollAudioProcessor::refreshTimeId, refreshTimeKnob);
    refreshDivisionAttachment = std::make_unique<APVTS::SliderAttachment> (apvts, LoopRollAudioProcessor::refreshDivisionId, refreshDivisionKnob);
    fadeInAttachment          = std::make_unique<APVTS::SliderAttachment> (apvts, LoopRollAudioProcessor::fadeInId, fadeInKnob);
    fadeOutAttachment         = std::make_unique<APVTS::SliderAttachment> (apvts, LoopRollAudioProcessor::fadeOutId, fadeOutKnob);

    for (auto* knob : { &timeKnob, &divisionKnob, &refreshTimeKnob, &refreshDivisionKnob })
        knob->onValueChange = [this] { updateValueLabels(); };

    for (auto* knob : { &fadeInKnob, &fadeOutKnob })
        knob->onValueChange = [this] { repaint(); };   // dessin de l'enveloppe et valeurs en %

    // Sélecteur FREE | SYNC
    configureButton (freeButton, false, false);
    configureButton (syncButton, false, false);

    modeAttachment = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (LoopRollAudioProcessor::modeId),
                                                                  [this] (float value) { modeChanged (value > 0.5f); });
    freeButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture (0.0f); };
    syncButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture (1.0f); };

    // LOOP et REFRESH : boutons à bascule
    configureButton (loopButton, true, true);
    configureButton (refreshButton, true, true);
    loopAttachment    = std::make_unique<APVTS::ButtonAttachment> (apvts, LoopRollAudioProcessor::loopId, loopButton);
    refreshAttachment = std::make_unique<APVTS::ButtonAttachment> (apvts, LoopRollAudioProcessor::refreshId, refreshButton);

    // Les lumières de la façade suivent l'état de LOOP (baie gauche) et de REFRESH (baie droite).
    loopStateAttachment = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (LoopRollAudioProcessor::loopId),
                                                                       [this] (float value)
                                                                       {
                                                                           loopOn = value > 0.5f;
                                                                           repaint();
                                                                       });

    refreshStateAttachment = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (LoopRollAudioProcessor::refreshId),
                                                                          [this] (float value)
                                                                          {
                                                                              refreshOn = value > 0.5f;

                                                                              // Refresh éteint : son potard passe en veille (il reste réglable).
                                                                              for (auto* knob : { &refreshTimeKnob, &refreshDivisionKnob })
                                                                                  knob->getProperties().set ("standby", ! refreshOn);

                                                                              refreshValue.setColour (juce::Label::textColourId, refreshOn ? LF::ink() : LF::dimInk());
                                                                              repaint();
                                                                          });

    modeAttachment->sendInitialUpdate();
    loopStateAttachment->sendInitialUpdate();
    refreshStateAttachment->sendInitialUpdate();

    setSize (editorWidth, editorHeight);
}

LoopRollAudioProcessorEditor::~LoopRollAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void LoopRollAudioProcessorEditor::configureMainKnob (juce::Slider& knob, const char* parameterId)
{
    addChildComponent (knob);
    knob.setComponentID (parameterId);
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    knob.setWantsKeyboardFocus (false);
}

void LoopRollAudioProcessorEditor::configureMiniKnob (juce::Slider& knob, const char* parameterId)
{
    addAndMakeVisible (knob);
    knob.setComponentID (parameterId);
    knob.getProperties().set ("mini", true);
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    knob.setWantsKeyboardFocus (false);
}

void LoopRollAudioProcessorEditor::configureValueLabel (juce::Label& label, bool isRefresh)
{
    addAndMakeVisible (label);
    label.setJustificationType (juce::Justification::centredRight);
    label.setMinimumHorizontalScale (0.8f);

    // Saisie au clavier (mode FREE) : la valeur tapée est appliquée au potard affiché.
    label.onTextChange = [this, &label, isRefresh]
    {
        applyTypedValue (label, isRefresh ? activeRefreshKnob() : activeLengthKnob());
    };
}

void LoopRollAudioProcessorEditor::configureButton (juce::TextButton& button, bool togglesOnClick, bool big)
{
    addAndMakeVisible (button);
    button.setClickingTogglesState (togglesOnClick);
    button.setWantsKeyboardFocus (false);
    button.getProperties().set ("cut", big ? 16.0 : 8.0);

    if (big)
        button.getProperties().set ("big", true);
}

void LoopRollAudioProcessorEditor::modeChanged (bool sync)
{
    syncMode = sync;

    freeButton.setToggleState (! sync, juce::dontSendNotification);
    syncButton.setToggleState (sync, juce::dontSendNotification);

    timeKnob.setVisible (! sync);
    divisionKnob.setVisible (sync);
    refreshTimeKnob.setVisible (! sync);
    refreshDivisionKnob.setVisible (sync);

    // Les durées libres se tapent au clavier ; les divisions se choisissent au potard.
    lengthValue.setEditable (! sync, false, false);
    refreshValue.setEditable (! sync, false, false);

    updateValueLabels();
    repaint();
}

void LoopRollAudioProcessorEditor::updateValueLabels()
{
    auto& length = activeLengthKnob();
    auto& refresh = activeRefreshKnob();

    lengthValue.setText (length.getTextFromValue (length.getValue()), juce::dontSendNotification);
    refreshValue.setText (refresh.getTextFromValue (refresh.getValue()), juce::dontSendNotification);
}

void LoopRollAudioProcessorEditor::applyTypedValue (juce::Label& label, juce::Slider& knob)
{
    knob.setValue (knob.getValueFromText (label.getText()), juce::sendNotificationSync);
    updateValueLabels();
}

void LoopRollAudioProcessorEditor::resized()
{
    timeKnob.setBounds (lengthKnobArea);
    divisionKnob.setBounds (lengthKnobArea);
    refreshTimeKnob.setBounds (refreshKnobArea);
    refreshDivisionKnob.setBounds (refreshKnobArea);

    lengthValue.setBounds (lengthReadout.withTrimmedLeft (98).withTrimmedRight (9));
    refreshValue.setBounds (refreshReadout.withTrimmedLeft (98).withTrimmedRight (9));

    freeButton.setBounds (freeButtonArea);
    syncButton.setBounds (syncButtonArea);
    fadeInKnob.setBounds (fadeInKnobArea);
    fadeOutKnob.setBounds (fadeOutKnobArea);

    loopButton.setBounds (loopButtonArea);
    refreshButton.setBounds (refreshButtonArea);
}

//==============================================================================
void LoopRollAudioProcessorEditor::paint (juce::Graphics& g)
{
    paintPlates (g);
    paintLights (g);
    paintCentre (g);
    paintReadouts (g);
}

void LoopRollAudioProcessorEditor::paintPlates (juce::Graphics& g)
{
    g.fillAll (LF::ground());

    // Plaque principale et son liseré intérieur
    const auto plate = polyline ({ { 28, 10 }, { 732, 10 }, { 750, 28 }, { 750, 452 }, { 732, 470 }, { 28, 470 }, { 10, 452 }, { 10, 28 } }, true);
    g.setColour (LF::plate());
    g.fillPath (plate);
    g.setColour (LF::line());
    g.strokePath (plate, juce::PathStrokeType (1.5f));

    g.setColour (juce::Colour (0xffe1e6ec));
    g.strokePath (polyline ({ { 31, 16 }, { 729, 16 }, { 744, 31 }, { 744, 449 }, { 729, 464 }, { 31, 464 }, { 16, 449 }, { 16, 31 } }, true),
                  juce::PathStrokeType (1.0f));

    // Joints de l'en-tête et du pied
    g.setColour (LF::seam());
    g.strokePath (polyline ({ { 16, 64 }, { 300, 64 }, { 312, 76 }, { 448, 76 }, { 460, 64 }, { 744, 64 } }), juce::PathStrokeType (1.2f));
    g.strokePath (polyline ({ { 16, 440 }, { 300, 440 }, { 312, 428 }, { 448, 428 }, { 460, 440 }, { 744, 440 } }), juce::PathStrokeType (1.2f));
    g.drawLine (150.0f, 440.0f, 150.0f, 464.0f, 1.2f);
    g.drawLine (610.0f, 440.0f, 610.0f, 464.0f, 1.2f);

    // Baies des deux potards
    const auto bayShape = polyline ({ { 58, 88 }, { 302, 88 }, { 326, 112 }, { 326, 324 }, { 302, 348 }, { 58, 348 }, { 34, 324 }, { 34, 112 } }, true);
    const auto bayInner = polyline ({ { 61, 95 }, { 299, 95 }, { 319, 115 }, { 319, 321 }, { 299, 341 }, { 61, 341 }, { 41, 321 }, { 41, 115 } }, true);

    for (const bool right : { false, true })
    {
        g.setColour (LF::bay());
        g.fillPath (right ? mirrored (bayShape) : bayShape);
        g.setColour (juce::Colour (0xffc0cad6));
        g.strokePath (right ? mirrored (bayShape) : bayShape, juce::PathStrokeType (1.2f));
        g.setColour (juce::Colours::white);
        g.strokePath (right ? mirrored (bayInner) : bayInner, juce::PathStrokeType (1.5f));
    }

    // Barres obliques entre les deux grands boutons
    juce::Path dashes;
    for (const float x : { 358.0f, 368.0f })
    {
        dashes.startNewSubPath (x, 381.0f);
        dashes.lineTo (x + 6.0f, 403.0f);
        dashes.startNewSubPath (mirrorX (x), 381.0f);
        dashes.lineTo (mirrorX (x + 6.0f), 403.0f);
    }

    g.setColour (juce::Colour (0xffa9b6c8));
    g.strokePath (dashes, juce::PathStrokeType (3.0f));
}

void LoopRollAudioProcessorEditor::paintLights (juce::Graphics& g)
{
    // Néons d'angle des baies : gauche = LOOP, droite = REFRESH
    const Pt topStart { 48, 170 }, topEnd { 128, 102 }, bottomStart { 48, 286 }, bottomEnd { 64, 334 };
    const auto topStrip = polyline ({ topStart, { 48, 118 }, { 64, 102 }, topEnd });
    const auto bottomStrip = polyline ({ bottomStart, { 48, 318 }, bottomEnd });

    for (const bool right : { false, true })
    {
        const bool lit = right ? refreshOn : loopOn;
        const auto top = right ? mirrored (topStrip) : topStrip;
        const auto bottom = right ? mirrored (bottomStrip) : bottomStrip;

        if (lit)
        {
            LF::strokeGlowing (g, top, 3.0f, LF::accentGradient (right ? mirror (topStart) : topStart, right ? mirror (topEnd) : topEnd));
            LF::strokeGlowing (g, bottom, 3.0f, LF::accentGradient (right ? mirror (bottomStart) : bottomStart, right ? mirror (bottomEnd) : bottomEnd));
        }
        else
        {
            const juce::PathStrokeType stroke (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            g.setColour (LF::unlit());
            g.strokePath (top, stroke);
            g.strokePath (bottom, stroke);
        }

        // Voyants du pied
        juce::Path pips;
        pips.startNewSubPath (40.0f, 452.0f);
        pips.lineTo (52.0f, 452.0f);
        pips.startNewSubPath (60.0f, 452.0f);
        pips.lineTo (72.0f, 452.0f);

        if (lit)
            g.setGradientFill (LF::accentGradient (right ? mirror ({ 40, 452 }) : Pt { 40, 452 }, right ? mirror ({ 72, 452 }) : Pt { 72, 452 }));
        else
            g.setColour (LF::line());

        g.strokePath (right ? mirrored (pips) : pips, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Barres obliques du haut : témoin de boucle active
    juce::Path leftBars;
    for (const float x : { 334.0f, 346.0f, 358.0f })
    {
        leftBars.startNewSubPath (x, 30.0f);
        leftBars.lineTo (x + 9.0f, 58.0f);
    }

    if (loopOn)
    {
        LF::strokeGlowing (g, leftBars, 4.0f, LF::accentGradient ({ 334, 44 }, { 367, 44 }), false);
        LF::strokeGlowing (g, mirrored (leftBars), 4.0f, LF::accentGradient (mirror ({ 334, 44 }), mirror ({ 367, 44 })), false);
    }
    else
    {
        g.setColour (LF::unlit());
        g.strokePath (leftBars, juce::PathStrokeType (4.0f));
        g.strokePath (mirrored (leftBars), juce::PathStrokeType (4.0f));
    }

    // Halo des boutons allumés
    const auto halo = [&g] (juce::Rectangle<int> area, float cut)
    {
        const auto bounds = area.toFloat();
        drawHalo (g, LF::chamferedRect (bounds, cut), LF::accentGradient (bounds.getTopLeft(), bounds.getTopRight(), true));
    };

    halo (syncMode ? syncButtonArea : freeButtonArea, 8.0f);

    if (loopOn)
        halo (loopButtonArea, 16.0f);

    if (refreshOn)
        halo (refreshButtonArea, 16.0f);
}

void LoopRollAudioProcessorEditor::paintCentre (juce::Graphics& g)
{
    // Pistes de circuit autour de l'afficheur d'enveloppe
    g.setColour (juce::Colour (0xff9fb0c6));
    g.drawLine (380.0f, 212.0f, 380.0f, 224.0f, 1.2f);
    g.drawLine (335.0f, 239.0f, 342.0f, 239.0f, 1.2f);
    g.drawLine (418.0f, 239.0f, 425.0f, 239.0f, 1.2f);
    g.drawLine (352.0f, 338.0f, 408.0f, 338.0f, 1.2f);

    for (const float x : { 332.0f, 428.0f })
    {
        const auto node = juce::Rectangle<float> (6.4f, 6.4f).withCentre ({ x, 239.0f });
        g.setColour (juce::Colours::white);
        g.fillEllipse (node);
        g.setColour (LF::blue());
        g.drawEllipse (node, 1.5f);
    }

    g.setColour (LF::plate());
    g.fillPath (polyline ({ { 380, 332 }, { 386, 338 }, { 380, 344 }, { 374, 338 } }, true));
    g.setGradientFill (LF::accentGradient ({ 374, 338 }, { 386, 338 }));
    g.fillPath (polyline ({ { 380, 333 }, { 385, 338 }, { 380, 343 }, { 375, 338 } }, true));

    // Afficheur d'enveloppe : la forme des fondus sur un passage de boucle
    const auto frame = envelopeArea.toFloat();
    g.setColour (juce::Colour (0xffb9c3d0));
    g.fillPath (LF::chamferedRect (frame, 6.0f));
    g.setColour (juce::Colours::white);
    g.fillPath (LF::chamferedRect (frame.reduced (1.0f), 5.0f));

    const auto plot = frame.reduced (9.0f, 7.0f);
    const float fadeIn = (float) fadeInKnob.getValue() * 0.01f;
    const float fadeOut = (float) fadeOutKnob.getValue() * 0.01f;
    const auto at = [plot] (float position, float level) { return Pt { plot.getX() + position * plot.getWidth(), plot.getBottom() - level * plot.getHeight() }; };

    juce::Path curve;

    if (fadeIn + fadeOut > 1.0f)   // les deux fondus se chevauchent : triangle
    {
        curve.startNewSubPath (at (0.0f, 0.0f));
        curve.lineTo (at (fadeIn / (fadeIn + fadeOut), 1.0f / (fadeIn + fadeOut)));
        curve.lineTo (at (1.0f, 0.0f));
    }
    else
    {
        curve.startNewSubPath (at (0.0f, fadeIn > 0.0f ? 0.0f : 1.0f));
        curve.lineTo (at (fadeIn, 1.0f));
        curve.lineTo (at (1.0f - fadeOut, 1.0f));
        curve.lineTo (at (1.0f, fadeOut > 0.0f ? 0.0f : 1.0f));
    }

    auto area = curve;
    area.lineTo (plot.getBottomRight());
    area.lineTo (plot.getBottomLeft());
    area.closeSubPath();

    const auto gradient = LF::accentGradient (plot.getBottomLeft(), plot.getBottomRight());
    g.setGradientFill (gradient);
    g.setOpacity (0.16f);
    g.fillPath (area);
    g.setGradientFill (gradient);
    g.setOpacity (1.0f);
    g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));

    // Libellés et valeurs des deux fondus
    const auto caption = [this, &g] (juce::Rectangle<int> knobArea, const juce::String& name, juce::Slider& knob)
    {
        const auto column = juce::Rectangle<int> (52, 12).withCentre ({ knobArea.getCentreX(), knobArea.getBottom() + 8 });

        g.setColour (LF::dimInk());
        g.setFont (lookAndFeel.font (9.0f, false, 0.1f));
        g.drawText (name, column, juce::Justification::centred, false);

        g.setColour (LF::ink());
        g.setFont (lookAndFeel.font (10.5f, true));
        g.drawText (knob.getTextFromValue (knob.getValue()), column.translated (0, 12), juce::Justification::centred, false);
    };

    caption (fadeInKnobArea, "IN", fadeInKnob);
    caption (fadeOutKnobArea, "OUT", fadeOutKnob);
}

void LoopRollAudioProcessorEditor::paintReadouts (juce::Graphics& g)
{
    // En-tête
    g.setColour (LF::ink());
    g.setFont (lookAndFeel.font (20.0f, true, 0.24f));
    g.drawText ("LOOPROLL", juce::Rectangle<int> (36, 16, 300, 48), juce::Justification::centredLeft, false);

    const auto brandFont = lookAndFeel.font (11.0f, false, 0.26f);
    const float brandWidth = textWidth (brandFont, "PHRAGMA");

    g.setColour (LF::dimInk());
    g.setFont (brandFont);
    g.drawText ("PHRAGMA", juce::Rectangle<float> (424.0f, 16.0f, 300.0f, 48.0f), juce::Justification::centredRight, false);

    // Numéro de version : pour vérifier d'un coup d'œil quelle version le DAW a chargée
    g.setFont (lookAndFeel.font (10.0f, false, 0.08f));
    g.drawText ("V" LOOPROLL_VERSION, juce::Rectangle<float> (424.0f, 16.0f, 300.0f - brandWidth - 16.0f, 48.0f),
                juce::Justification::centredRight, false);

    // Afficheurs sous les potards
    const auto readout = [this, &g] (juce::Rectangle<int> area, const juce::String& name)
    {
        const auto frame = area.toFloat();
        g.setColour (juce::Colour (0xffb9c3d0));
        g.fillPath (LF::chamferedRect (frame, 8.0f));
        g.setColour (juce::Colours::white);
        g.fillPath (LF::chamferedRect (frame.reduced (1.0f), 7.0f));

        g.setColour (LF::dimInk());
        g.setFont (lookAndFeel.font (10.0f, false, 0.12f));
        g.drawText (name, area.withTrimmedLeft (14), juce::Justification::centredLeft, false);
    };

    readout (lengthReadout, "LENGTH");
    readout (refreshReadout, "REFRESH EVERY");
}
