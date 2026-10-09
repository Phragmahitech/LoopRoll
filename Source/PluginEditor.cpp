#include "PluginEditor.h"
#include "BinaryData.h"

#ifndef LOOPROLL_VERSION
 #define LOOPROLL_VERSION "dev"
#endif

namespace
{
    using LF = LoopRollLookAndFeel;
    using Pt = juce::Point<float>;

    // Façade : quatre modules côte à côte (LOOP, REFRESH, STUTTER, FILTER), tous bâtis sur la même grille.
    constexpr int editorWidth = 1104, editorHeight = 568;
    constexpr int numColumns = 4, columnWidth = 250, columnGap = 16, firstColumnX = 28;

    constexpr int columnX (int column)                  { return firstColumnX + column * (columnWidth + columnGap); }

    juce::Rectangle<int> bayArea (int column)           { return { columnX (column), 84, columnWidth, 268 }; }
    juce::Rectangle<int> knobArea (int column)          { return { columnX (column) + 40, 98, 170, 170 }; }
    juce::Rectangle<int> readoutArea (int column)       { return { columnX (column) + 20, 276, 210, 34 }; }
    juce::Rectangle<int> switchArea (int column)        { return { columnX (column) + 50, 318, 150, 24 }; }
    juce::Rectangle<int> buttonArea (int column)        { return { columnX (column), 364, columnWidth, 52 }; }
    juce::Rectangle<int> moduleArea (int column)        { return { columnX (column), 430, columnWidth, 92 }; }
    juce::Rectangle<int> displayArea (int column, int width)   { return { columnX (column) + 14, 458, width, 44 }; }
    juce::Rectangle<int> miniKnobArea (int column, int slot)   { return { columnX (column) + (slot == 0 ? 112 : 176), 452, 40, 40 }; }

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
    const bool small = button.getProperties().contains ("small");
    const auto text = button.getButtonText();
    const auto textFont = font (big ? 18.0f : small ? 10.5f : 13.0f, true, big ? 0.2f : small ? 0.12f : 0.14f);
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

    using Ids = LoopRollAudioProcessor;

    // Grands potards : un par module, deux sliders superposés quand le module a un mode FREE / SYNC.
    configureMainKnob (timeKnob, Ids::timeId, 0);
    configureMainKnob (divisionKnob, Ids::divisionId, 0);
    configureMainKnob (refreshTimeKnob, Ids::refreshTimeId, 1);
    configureMainKnob (refreshDivisionKnob, Ids::refreshDivisionId, 1);
    configureMainKnob (stutterTimeKnob, Ids::stutterTimeId, 2);
    configureMainKnob (stutterDivisionKnob, Ids::stutterDivisionId, 2);
    configureMainKnob (cutoffKnob, Ids::filterCutoffId, 3);
    cutoffKnob.setVisible (true);

    configureMiniKnob (fadeInKnob, Ids::fadeInId, miniKnobArea (0, 0));
    configureMiniKnob (fadeOutKnob, Ids::fadeOutId, miniKnobArea (0, 1));
    configureMiniKnob (shapeKnob, Ids::stutterShapeId, miniKnobArea (2, 1));
    configureMiniKnob (resonanceKnob, Ids::filterResonanceId, miniKnobArea (3, 1));

    for (int column = 0; column < numColumns; ++column)
        configureValueLabel (valueLabels[column], column);

    // Grands boutons à bascule. Les lumières de chaque module suivent son état.
    configureToggle (loopButton, Ids::loopId, 0, [this] (bool on) { loopOn = on; });
    configureToggle (refreshButton, Ids::refreshId, 1, [this] (bool on)
    {
        refreshOn = on;
        setStandby (1, ! on);
    });
    configureToggle (stutterButton, Ids::stutterId, 2, [this] (bool on)
    {
        stutterOn = on;
        setStandby (2, ! on);
    });
    configureToggle (filterButton, Ids::filterId, 3, [this] (bool on)
    {
        filterOn = on;
        setStandby (3, ! on);
    });

    // Interrupteurs à deux positions
    configureSwitch (loopMode, Ids::modeId, "FREE", "SYNC", switchArea (0), [this] (bool sync) { loopSync = sync; modesChanged(); });
    configureSwitch (refreshMode, Ids::refreshModeId, "FREE", "SYNC", switchArea (1), [this] (bool sync) { refreshSync = sync; modesChanged(); });
    configureSwitch (stutterMode, Ids::stutterModeId, "FREE", "SYNC", switchArea (2), [this] (bool sync) { stutterSync = sync; modesChanged(); });
    configureSwitch (filterType, Ids::filterTypeId, "HP", "BP", switchArea (3), [this] (bool isBandPass) { bandPass = isBandPass; repaint(); });
    configureSwitch (chainOrder, Ids::orderId, "STUTTER > LOOP", "LOOP > STUTTER", {}, [] (bool) {});

    const auto chain = moduleArea (1);
    chainOrder.first.setBounds (chain.getX() + 14, chain.getY() + 26, chain.getWidth() - 28, 26);
    chainOrder.second.setBounds (chain.getX() + 14, chain.getY() + 56, chain.getWidth() - 28, 26);
    chainOrder.first.setConnectedEdges (0);
    chainOrder.second.setConnectedEdges (0);

    // Tout est en place : on aligne l'interface sur les paramètres.
    for (auto& watcher : stateWatchers)
        watcher->sendInitialUpdate();

    for (auto* selector : { &loopMode, &refreshMode, &stutterMode, &filterType, &chainOrder })
        selector->attachment->sendInitialUpdate();

    setSize (editorWidth, editorHeight);
}

LoopRollAudioProcessorEditor::~LoopRollAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void LoopRollAudioProcessorEditor::configureMainKnob (juce::Slider& knob, const char* parameterId, int column)
{
    addChildComponent (knob);
    knob.setComponentID (parameterId);
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    knob.setWantsKeyboardFocus (false);
    knob.setBounds (knobArea (column));
    knob.onValueChange = [this] { updateValueLabels(); repaint(); };

    sliderAttachments.push_back (std::make_unique<APVTS::SliderAttachment> (audioProcessor.apvts, parameterId, knob));
}

void LoopRollAudioProcessorEditor::configureMiniKnob (juce::Slider& knob, const char* parameterId, juce::Rectangle<int> area)
{
    addAndMakeVisible (knob);
    knob.setComponentID (parameterId);
    knob.getProperties().set ("mini", true);
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    knob.setWantsKeyboardFocus (false);
    knob.setBounds (area);
    knob.onValueChange = [this] { repaint(); };   // afficheurs et valeurs en %

    sliderAttachments.push_back (std::make_unique<APVTS::SliderAttachment> (audioProcessor.apvts, parameterId, knob));
}

void LoopRollAudioProcessorEditor::configureValueLabel (juce::Label& label, int column)
{
    addAndMakeVisible (label);
    label.setJustificationType (juce::Justification::centredRight);
    label.setMinimumHorizontalScale (0.7f);
    label.setBounds (readoutArea (column).withTrimmedLeft (78).withTrimmedRight (9));

    // Saisie au clavier : la valeur tapée est appliquée au potard affiché dans ce module.
    label.onTextChange = [this, &label, column]
    {
        auto& knob = activeKnob (column);
        knob.setValue (knob.getValueFromText (label.getText()), juce::sendNotificationSync);
        updateValueLabels();
    };
}

void LoopRollAudioProcessorEditor::configureToggle (juce::TextButton& button, const char* parameterId, int column,
                                                    std::function<void (bool)> onChange)
{
    addAndMakeVisible (button);
    button.setClickingTogglesState (true);
    button.setWantsKeyboardFocus (false);
    button.getProperties().set ("cut", 16.0);
    button.getProperties().set ("big", true);
    button.setBounds (buttonArea (column));

    buttonAttachments.push_back (std::make_unique<APVTS::ButtonAttachment> (audioProcessor.apvts, parameterId, button));

    stateWatchers.push_back (std::make_unique<juce::ParameterAttachment> (*audioProcessor.apvts.getParameter (parameterId),
                                                                          [this, onChange] (float value)
                                                                          {
                                                                              onChange (value > 0.5f);
                                                                              repaint();
                                                                          }));
}

void LoopRollAudioProcessorEditor::configureSwitch (TwoWaySwitch& selector, const char* parameterId,
                                                    const juce::String& firstText, const juce::String& secondText,
                                                    juce::Rectangle<int> area, std::function<void (bool)> onChange)
{
    selector.first.setButtonText (firstText);
    selector.second.setButtonText (secondText);

    // Identifiants "paramètre/0" et "paramètre/1" : pour retrouver les deux positions (tests, accessibilité).
    selector.first.setComponentID (juce::String (parameterId) + "/0");
    selector.second.setComponentID (juce::String (parameterId) + "/1");

    for (auto* button : { &selector.first, &selector.second })
    {
        addAndMakeVisible (*button);
        button->setClickingTogglesState (false);
        button->setWantsKeyboardFocus (false);
        button->getProperties().set ("cut", 6.0);
        button->getProperties().set ("small", true);
    }

    selector.first.setConnectedEdges (juce::Button::ConnectedOnRight);
    selector.second.setConnectedEdges (juce::Button::ConnectedOnLeft);
    selector.first.setBounds (area.withWidth (area.getWidth() / 2));
    selector.second.setBounds (area.withTrimmedLeft (area.getWidth() / 2));

    auto* first = &selector.first;
    auto* second = &selector.second;

    selector.attachment = std::make_unique<juce::ParameterAttachment> (*audioProcessor.apvts.getParameter (parameterId),
                                                                       [first, second, onChange] (float value)
                                                                       {
                                                                           const bool secondChosen = value > 0.5f;
                                                                           first->setToggleState (! secondChosen, juce::dontSendNotification);
                                                                           second->setToggleState (secondChosen, juce::dontSendNotification);
                                                                           onChange (secondChosen);
                                                                       });

    auto* attachment = selector.attachment.get();
    selector.first.onClick = [attachment] { attachment->setValueAsCompleteGesture (0.0f); };
    selector.second.onClick = [attachment] { attachment->setValueAsCompleteGesture (1.0f); };
}

juce::Slider& LoopRollAudioProcessorEditor::activeKnob (int column)
{
    switch (column)
    {
        case 0:   return loopSync ? divisionKnob : timeKnob;
        case 1:   return refreshSync ? refreshDivisionKnob : refreshTimeKnob;
        case 2:   return stutterSync ? stutterDivisionKnob : stutterTimeKnob;
        default:  return cutoffKnob;
    }
}

void LoopRollAudioProcessorEditor::setStandby (int column, bool standby)
{
    // Module éteint : son potard passe en veille (il reste réglable) et sa valeur s'estompe.
    for (auto* knob : { &refreshTimeKnob, &refreshDivisionKnob, &stutterTimeKnob, &stutterDivisionKnob, &cutoffKnob })
        if (knob->getBounds() == knobArea (column))
            knob->getProperties().set ("standby", standby);

    valueLabels[column].setColour (juce::Label::textColourId, standby ? LF::dimInk() : LF::ink());
}

void LoopRollAudioProcessorEditor::modesChanged()
{
    timeKnob.setVisible (! loopSync);
    divisionKnob.setVisible (loopSync);
    refreshTimeKnob.setVisible (! refreshSync);
    refreshDivisionKnob.setVisible (refreshSync);
    stutterTimeKnob.setVisible (! stutterSync);
    stutterDivisionKnob.setVisible (stutterSync);

    // Les durées libres et la fréquence se tapent au clavier ; les divisions se choisissent au potard.
    valueLabels[0].setEditable (! loopSync, false, false);
    valueLabels[1].setEditable (! refreshSync, false, false);
    valueLabels[2].setEditable (! stutterSync, false, false);
    valueLabels[3].setEditable (true, false, false);

    updateValueLabels();
    repaint();
}

void LoopRollAudioProcessorEditor::updateValueLabels()
{
    for (int column = 0; column < numColumns; ++column)
    {
        auto& knob = activeKnob (column);
        valueLabels[column].setText (knob.getTextFromValue (knob.getValue()), juce::dontSendNotification);
    }
}

void LoopRollAudioProcessorEditor::resized() {}

//==============================================================================
void LoopRollAudioProcessorEditor::paint (juce::Graphics& g)
{
    paintPlates (g);
    paintLights (g);
    paintModules (g);
    paintReadouts (g);
}

void LoopRollAudioProcessorEditor::paintPlates (juce::Graphics& g)
{
    g.fillAll (LF::ground());

    // Plaque principale et son liseré intérieur
    const auto plateArea = getLocalBounds().toFloat().reduced (10.0f);
    const auto plate = LF::chamferedRect (plateArea, 18.0f);
    g.setColour (LF::plate());
    g.fillPath (plate);
    g.setColour (LF::line());
    g.strokePath (plate, juce::PathStrokeType (1.5f));

    g.setColour (juce::Colour (0xffe1e6ec));
    g.strokePath (LF::chamferedRect (plateArea.reduced (6.0f), 15.0f), juce::PathStrokeType (1.0f));

    // Joints de l'en-tête et du pied
    const float w = (float) editorWidth, mid = w * 0.5f;
    g.setColour (LF::seam());
    g.strokePath (polyline ({ { 16.0f, 64.0f }, { mid - 80.0f, 64.0f }, { mid - 68.0f, 76.0f },
                              { mid + 68.0f, 76.0f }, { mid + 80.0f, 64.0f }, { w - 16.0f, 64.0f } }), juce::PathStrokeType (1.2f));
    g.drawLine (16.0f, 536.0f, w - 16.0f, 536.0f, 1.2f);

    // Baies des potards et panneaux du bas
    for (int column = 0; column < numColumns; ++column)
    {
        const auto bay = bayArea (column).toFloat();
        g.setColour (LF::bay());
        g.fillPath (LF::chamferedRect (bay, 22.0f));
        g.setColour (juce::Colour (0xffc0cad6));
        g.strokePath (LF::chamferedRect (bay, 22.0f), juce::PathStrokeType (1.2f));
        g.setColour (juce::Colours::white);
        g.strokePath (LF::chamferedRect (bay.reduced (7.0f), 19.0f), juce::PathStrokeType (1.5f));

        const auto module = moduleArea (column).toFloat();
        g.setColour (LF::bay());
        g.fillPath (LF::chamferedRect (module, 14.0f));
        g.setColour (juce::Colour (0xffc0cad6));
        g.strokePath (LF::chamferedRect (module, 14.0f), juce::PathStrokeType (1.2f));
    }
}

void LoopRollAudioProcessorEditor::paintLights (juce::Graphics& g)
{
    const bool moduleOn[numColumns] = { loopOn, refreshOn, stutterOn, filterOn };

    for (int column = 0; column < numColumns; ++column)
    {
        const float x = (float) columnX (column);
        const float right = x + (float) columnWidth;
        const bool lit = moduleOn[column];

        // Néons d'angle de la baie, et voyants du pied
        const Pt leftStart { x + 13.0f, 150.0f }, leftEnd { x + 80.0f, 97.0f };
        const Pt rightStart { right - 13.0f, 150.0f }, rightEnd { right - 80.0f, 97.0f };
        const auto leftStrip = polyline ({ leftStart, { x + 13.0f, 112.0f }, { x + 28.0f, 97.0f }, leftEnd });
        const auto rightStrip = polyline ({ rightStart, { right - 13.0f, 112.0f }, { right - 28.0f, 97.0f }, rightEnd });

        juce::Path pips;
        pips.startNewSubPath (x + 12.0f, 547.0f);
        pips.lineTo (x + 24.0f, 547.0f);
        pips.startNewSubPath (x + 32.0f, 547.0f);
        pips.lineTo (x + 44.0f, 547.0f);

        const juce::PathStrokeType pipStroke (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        if (lit)
        {
            LF::strokeGlowing (g, leftStrip, 3.0f, LF::accentGradient (leftStart, leftEnd));
            LF::strokeGlowing (g, rightStrip, 3.0f, LF::accentGradient (rightStart, rightEnd));
            g.setGradientFill (LF::accentGradient ({ x + 12.0f, 547.0f }, { x + 44.0f, 547.0f }));
            g.strokePath (pips, pipStroke);

            const auto button = buttonArea (column).toFloat();
            drawHalo (g, LF::chamferedRect (button, 16.0f), LF::accentGradient (button.getTopLeft(), button.getTopRight(), true));
        }
        else
        {
            const juce::PathStrokeType stroke (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            g.setColour (LF::unlit());
            g.strokePath (leftStrip, stroke);
            g.strokePath (rightStrip, stroke);
            g.setColour (LF::line());
            g.strokePath (pips, pipStroke);
        }
    }

    // Barres obliques du haut : allumées dès qu'un effet tourne
    const float mid = (float) editorWidth * 0.5f;
    juce::Path leftBars, rightBars;

    for (const float offset : { 46.0f, 34.0f, 22.0f })
    {
        leftBars.startNewSubPath (mid - offset, 30.0f);
        leftBars.lineTo (mid - offset + 9.0f, 58.0f);
        rightBars.startNewSubPath (mid + offset, 30.0f);
        rightBars.lineTo (mid + offset - 9.0f, 58.0f);
    }

    if (loopOn || stutterOn)
    {
        LF::strokeGlowing (g, leftBars, 4.0f, LF::accentGradient ({ mid - 46.0f, 44.0f }, { mid - 13.0f, 44.0f }), false);
        LF::strokeGlowing (g, rightBars, 4.0f, LF::accentGradient ({ mid + 46.0f, 44.0f }, { mid + 13.0f, 44.0f }), false);
    }
    else
    {
        g.setColour (LF::unlit());
        g.strokePath (leftBars, juce::PathStrokeType (4.0f));
        g.strokePath (rightBars, juce::PathStrokeType (4.0f));
    }
}

void LoopRollAudioProcessorEditor::paintModules (juce::Graphics& g)
{
    // Titres des panneaux du bas
    static const char* const titles[numColumns] = { "LOOP FADES", "CHAIN ORDER", "STUTTER SHAPE", "FILTER RESPONSE" };

    g.setColour (LF::dimInk());
    g.setFont (lookAndFeel.font (9.0f, false, 0.12f));

    for (int column = 0; column < numColumns; ++column)
        g.drawText (titles[column], moduleArea (column).withTrimmedLeft (14).withHeight (24), juce::Justification::centredLeft, false);

    // Cadre d'un petit afficheur ; renvoie la zone de tracé.
    const auto frame = [&g] (juce::Rectangle<int> area)
    {
        const auto bounds = area.toFloat();
        g.setColour (juce::Colour (0xffb9c3d0));
        g.fillPath (LF::chamferedRect (bounds, 6.0f));
        g.setColour (juce::Colours::white);
        g.fillPath (LF::chamferedRect (bounds.reduced (1.0f), 5.0f));
        return bounds.reduced (9.0f, 8.0f);
    };

    // Trace une courbe en dégradé, remplie en dessous.
    const auto plotCurve = [&g] (juce::Rectangle<float> plot, const juce::Path& curve)
    {
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
    };

    // --- Fondus de la boucle : forme de l'enveloppe sur un passage ---
    {
        const auto plot = frame (displayArea (0, 84));
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

        plotCurve (plot, curve);
    }

    // --- Stutter : deux pas du gate ---
    {
        const auto plot = frame (displayArea (2, 150));
        const double shape = shapeKnob.getValue() * 0.01;
        const int numPoints = 120;
        juce::Path curve;

        for (int i = 0; i <= numPoints; ++i)
        {
            const double position = (double) i / numPoints;
            const double phase = std::fmod (position * 2.0, 1.0);
            const double level = position >= 1.0 ? 0.0 : StutterGate::shapeGain (phase, shape) * juce::jmin (1.0, phase / 0.03);
            const Pt point { plot.getX() + (float) position * plot.getWidth(), plot.getBottom() - (float) level * plot.getHeight() };

            if (i == 0)
                curve.startNewSubPath (point);
            else
                curve.lineTo (point);
        }

        plotCurve (plot, curve);
    }

    // --- Filtre : courbe de réponse, de 20 Hz à 20 kHz ---
    {
        const auto plot = frame (displayArea (3, 150));
        const double cutoff = cutoffKnob.getValue();
        const double quality = OutputFilter::qualityFor (resonanceKnob.getValue() * 0.01);
        const int numPoints = 80;
        juce::Path curve;

        for (int i = 0; i <= numPoints; ++i)
        {
            const double position = (double) i / numPoints;
            const double frequency = OutputFilter::minFrequency * std::pow (OutputFilter::maxFrequency / OutputFilter::minFrequency, position);
            const double decibels = 20.0 * std::log10 (juce::jmax (1.0e-4, OutputFilter::magnitudeAt (frequency, cutoff, quality, bandPass)));
            const double level = juce::jlimit (0.0, 1.0, (decibels + 36.0) / 56.0);   // de -36 dB (bas) à +20 dB (haut)
            const Pt point { plot.getX() + (float) position * plot.getWidth(), plot.getBottom() - (float) level * plot.getHeight() };

            if (i == 0)
                curve.startNewSubPath (point);
            else
                curve.lineTo (point);
        }

        plotCurve (plot, curve);
    }

    // Libellés et valeurs des petits potards
    const auto caption = [this, &g] (juce::Slider& knob, const juce::String& name)
    {
        const auto column = juce::Rectangle<int> (60, 12).withCentre ({ knob.getBounds().getCentreX(), knob.getBottom() + 8 });

        g.setColour (LF::dimInk());
        g.setFont (lookAndFeel.font (9.0f, false, 0.1f));
        g.drawText (name, column, juce::Justification::centred, false);

        g.setColour (LF::ink());
        g.setFont (lookAndFeel.font (10.5f, true));
        g.drawText (knob.getTextFromValue (knob.getValue()), column.translated (0, 12), juce::Justification::centred, false);
    };

    caption (fadeInKnob, "IN");
    caption (fadeOutKnob, "OUT");
    caption (shapeKnob, "SHAPE");
    caption (resonanceKnob, "RESO");
}

void LoopRollAudioProcessorEditor::paintReadouts (juce::Graphics& g)
{
    // En-tête
    g.setColour (LF::ink());
    g.setFont (lookAndFeel.font (20.0f, true, 0.24f));
    g.drawText ("LOOPROLL", juce::Rectangle<int> (36, 16, 300, 48), juce::Justification::centredLeft, false);

    const auto brandFont = lookAndFeel.font (11.0f, false, 0.26f);
    const float brandWidth = textWidth (brandFont, "PHRAGMA");
    const auto brandArea = juce::Rectangle<float> ((float) editorWidth - 336.0f, 16.0f, 300.0f, 48.0f);

    g.setColour (LF::dimInk());
    g.setFont (brandFont);
    g.drawText ("PHRAGMA", brandArea, juce::Justification::centredRight, false);

    // Numéro de version : pour vérifier d'un coup d'œil quelle version le DAW a chargée
    g.setFont (lookAndFeel.font (10.0f, false, 0.08f));
    g.drawText ("V" LOOPROLL_VERSION, brandArea.withTrimmedRight (brandWidth + 16.0f), juce::Justification::centredRight, false);

    // Afficheurs sous les grands potards
    static const char* const names[numColumns] = { "LENGTH", "EVERY", "RATE", "CUTOFF" };

    for (int column = 0; column < numColumns; ++column)
    {
        const auto area = readoutArea (column);
        const auto bounds = area.toFloat();
        g.setColour (juce::Colour (0xffb9c3d0));
        g.fillPath (LF::chamferedRect (bounds, 8.0f));
        g.setColour (juce::Colours::white);
        g.fillPath (LF::chamferedRect (bounds.reduced (1.0f), 7.0f));

        g.setColour (LF::dimInk());
        g.setFont (lookAndFeel.font (10.0f, false, 0.12f));
        g.drawText (names[column], area.withTrimmedLeft (14), juce::Justification::centredLeft, false);
    }
}
