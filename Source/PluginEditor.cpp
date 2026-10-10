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
    constexpr int editorWidth = 1104, editorHeight = 642;
    constexpr int numColumns = 4, columnWidth = 250, columnGap = 16, firstColumnX = 28;
    constexpr int panelInset = 14, panelInnerWidth = columnWidth - 2 * panelInset;
    constexpr int footerY = 610;

    constexpr int columnX (int column)                  { return firstColumnX + column * (columnWidth + columnGap); }

    juce::Rectangle<int> bayArea (int column)           { return { columnX (column), 84, columnWidth, 268 }; }
    juce::Rectangle<int> knobArea (int column)          { return { columnX (column) + 40, 98, 170, 170 }; }
    juce::Rectangle<int> readoutArea (int column)       { return { columnX (column) + 20, 276, 210, 34 }; }
    juce::Rectangle<int> switchArea (int column, int offset, int width)   { return { columnX (column) + offset, 318, width, 24 }; }
    juce::Rectangle<int> buttonArea (int column)        { return { columnX (column), 364, columnWidth, 52 }; }
    juce::Rectangle<int> panelArea (int column)         { return { columnX (column), 430, columnWidth, 166 }; }
    juce::Rectangle<int> displayArea (int column, int width = panelInnerWidth)   { return { columnX (column) + panelInset, 458, width, 44 }; }

    /** Petit potard numéro "slot" parmi "numSlots" répartis sur la largeur du panneau. */
    juce::Rectangle<int> miniKnobArea (int column, int slot, int numSlots)
    {
        const int centre = columnX (column) + panelInset + (panelInnerWidth * (2 * slot + 1)) / (2 * numSlots);
        return { centre - 20, 514, 40, 40 };
    }

    /** Petits interrupteurs du panneau LOOP : deux colonnes, deux rangées, à droite de l'afficheur. */
    juce::Rectangle<int> smallToggleArea (int column, int gridColumn, int gridRow)
    {
        return { columnX (column) + 118 + gridColumn * 61, 458 + gridRow * 24, 57, 20 };
    }

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

    juce::Path circle (Pt centre, float radius)
    {
        juce::Path path;
        path.addEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
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

    /** Trace une courbe en dégradé d'accent, remplie en dessous jusqu'au bas de la zone. */
    void plotCurve (juce::Graphics& g, juce::Rectangle<float> plot, const juce::Path& curve, float thickness = 2.0f)
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
        g.strokePath (curve, juce::PathStrokeType (thickness, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
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
        gradient.addColour (0.62, juce::Colour (0xff1a8dff));

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

void LoopRollLookAndFeel::dropShadow (juce::Graphics& g, const juce::Path& path, float radius, float offsetY, float alpha)
{
    juce::DropShadow (shadow().withAlpha (alpha), juce::roundToInt (radius), { 0, juce::roundToInt (offsetY) }).drawForPath (g, path);
}

void LoopRollLookAndFeel::innerShadow (juce::Graphics& g, const juce::Path& shape, float radius, float offsetY, float alpha)
{
    // L'ombre de tout ce qui entoure la forme, vue à travers la forme : sombre le long du bord haut, puis estompée.
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (shape);

    juce::Path surround;
    surround.setUsingNonZeroWinding (false);
    surround.addRectangle (shape.getBounds().expanded (radius * 3.0f + std::abs (offsetY) + 4.0f));
    surround.addPath (shape);

    juce::DropShadow (shadow().withAlpha (alpha), juce::roundToInt (radius), { 0, juce::roundToInt (offsetY) }).drawForPath (g, surround);
}

void LoopRollLookAndFeel::fillVertical (juce::Graphics& g, const juce::Path& path, juce::Rectangle<float> area,
                                        juce::Colour top, juce::Colour bottom)
{
    g.setGradientFill (juce::ColourGradient (top, area.getX(), area.getY(), bottom, area.getX(), area.getBottom(), false));
    g.fillPath (path);
}

void LoopRollLookAndFeel::drawScreen (juce::Graphics& g, juce::Rectangle<float> area, float cut)
{
    const auto shape = chamferedRect (area, cut);

    // Lèvre claire sous l'écran, puis l'écran lui-même, en creux.
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.strokePath (shape, juce::PathStrokeType (1.2f), juce::AffineTransform::translation (0.0f, 1.2f));

    fillVertical (g, shape, area, juce::Colour (0xfff2f5f8), juce::Colours::white);
    innerShadow (g, shape, 4.0f, 2.0f, 0.30f);

    g.setColour (juce::Colour (0xffa7b3c2));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
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
    const float arcRadius = 76.0f * scale, tickRadius = 89.0f * scale, capRadius = 44.0f * scale;
    const float bodyRadius = mainKnobBodyRadius (bounds);
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const bool isStandby = (bool) slider.getProperties().getWithDefault ("standby", false);

    // Le dégradé court de gauche (bleu) à droite (cyan) : il suit la progression de l'arc.
    const float left = centre.x - arcRadius;
    const auto gradient = accentGradient ({ left, centre.y }, { centre.x + arcRadius, centre.y });
    const auto accentAtX = [left, arcRadius] (float px) { return accentAt ((px - left) / (2.0f * arcRadius)); };
    const juce::PathStrokeType arcStroke (6.0f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // Piste en creux : un bord sombre en haut, un bord clair en bas.
    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.strokePath (trackArc, arcStroke, juce::AffineTransform::translation (0.0f, 1.0f));
    g.setColour (juce::Colour (0xffafbac8));
    g.strokePath (trackArc, arcStroke, juce::AffineTransform::translation (0.0f, -0.7f));
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

    // Corps du potard (son ombre portée est dessinée une fois pour toutes dans le fond)
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, centre.x, body.getY(),
                                             juce::Colour (0xffd3dae4), centre.x, body.getBottom(), false));
    g.fillEllipse (body);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, centre.x, body.getY(),
                                             juce::Colour (0xff9eaaba), centre.x, body.getBottom(), false));
    g.drawEllipse (body, 1.5f * scale);

    // Calotte : légèrement creusée, avec un reflet en haut
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe3e8ef), centre.x, cap.getY(),
                                             juce::Colour (0xfffafbfd), centre.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffb4bfcc), centre.x, cap.getY(),
                                             juce::Colours::white, centre.x, cap.getBottom(), false));
    g.drawEllipse (cap, 1.2f * scale);

    // Repère
    const auto tip = centre.getPointOnCircumference (50.0f * scale, angle);
    juce::Path pointer;
    pointer.startNewSubPath (centre.getPointOnCircumference (24.0f * scale, angle));
    pointer.lineTo (tip);
    const juce::PathStrokeType pointerStroke (4.5f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (shadow().withAlpha (0.18f));
    g.strokePath (pointer, pointerStroke, juce::AffineTransform::translation (0.0f, 1.5f * scale));
    g.setColour (isStandby ? juce::Colour (0xff7c8ba6) : accentAtX (tip.x));
    g.strokePath (pointer, pointerStroke);

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
    const float arcRadius = radius - 3.0f, bodyRadius = miniKnobBodyRadius (bounds);
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const juce::PathStrokeType arcStroke (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.strokePath (trackArc, arcStroke, juce::AffineTransform::translation (0.0f, 0.8f));
    g.setColour (juce::Colour (0xffc2cbd7));
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
                                             juce::Colour (0xffd5dce6), centre.x, body.getBottom(), false));
    g.fillEllipse (body);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, centre.x, body.getY(),
                                             juce::Colour (0xff9eaaba), centre.x, body.getBottom(), false));
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
    const auto inner = chamferedRect (area.reduced (1.5f), cut - 1.0f);

    if (! button.isEnabled())
    {
        // Hors service (variante rythmique en mode free) : à plat, sans relief.
        g.setColour (juce::Colour (0xffcfd6df));
        g.fillPath (shape);
        g.setColour (juce::Colour (0xffe9edf2));
        g.fillPath (inner);
        return;
    }

    if (button.getToggleState())
    {
        // Allumé : le dégradé d'accent, bombé (reflet en haut, ombre en bas).
        g.setGradientFill (accentGradient (area.getTopLeft(), area.getTopRight(), true));
        g.fillPath (shape);

        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.30f), area.getX(), area.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), area.getX(), area.getCentreY(), false));
        g.fillPath (shape);
        g.setGradientFill (juce::ColourGradient (shadow().withAlpha (0.0f), area.getX(), area.getCentreY(),
                                                 shadow().withAlpha (0.22f), area.getX(), area.getBottom(), false));
        g.fillPath (shape);

        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.strokePath (inner, juce::PathStrokeType (1.0f));
    }
    else
    {
        // Éteint : une touche claire en relief, bord sombre, liseré blanc.
        g.setColour (juce::Colour (0xffa3afbe));
        g.fillPath (shape);
        fillVertical (g, chamferedRect (area.reduced (1.0f), cut - 0.7f), area, juce::Colours::white, juce::Colour (0xffe0e5ec));
        g.setColour (juce::Colours::white);
        g.strokePath (inner, juce::PathStrokeType (1.0f));
    }

    if (down)
    {
        g.setColour (shadow().withAlpha (0.14f));
        g.fillPath (shape);
    }
    else if (highlighted)
    {
        g.setColour (juce::Colours::white.withAlpha (button.getToggleState() ? 0.10f : 0.45f));
        g.fillPath (shape);
    }
}

void LoopRollLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool down)
{
    const bool on = button.getToggleState();
    const auto& properties = button.getProperties();
    const bool big = properties.contains ("big"), small = properties.contains ("small"), tiny = properties.contains ("tiny");
    const auto text = button.getButtonText();
    const auto textFont = font (big ? 18.0f : small ? 10.5f : tiny ? 9.0f : 13.0f, true, big ? 0.2f : tiny ? 0.08f : 0.12f);
    const auto textColour = ! button.isEnabled() ? juce::Colour (0xffb4bdc9) : on ? juce::Colours::white : juce::Colour (0xff2a3342);
    const auto area = button.getLocalBounds().toFloat().translated (0.0f, down ? 1.0f : 0.0f);   // la touche s'enfonce

    g.setFont (textFont);

    if (! big)
    {
        if (on)   // ombre du texte : il se détache du dégradé
        {
            g.setColour (juce::Colour (0xff0a3a8a).withAlpha (0.45f));
            g.drawText (text, area.translated (0.0f, 1.0f), juce::Justification::centred, false);
        }

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

    const auto labelArea = juce::Rectangle<float> (left + iconWidth + gap, area.getY(), labelWidth + 12.0f, area.getHeight());

    if (on)
    {
        g.setColour (juce::Colour (0xff0a3a8a).withAlpha (0.45f));
        g.strokePath (bars, juce::PathStrokeType (3.0f), juce::AffineTransform::translation (0.0f, 1.5f));
        g.drawText (text, labelArea.translated (0.0f, 1.5f), juce::Justification::centredLeft, false);
    }

    g.setColour (on ? juce::Colours::white : juce::Colour (0xffa9b6c8));
    g.strokePath (bars, juce::PathStrokeType (3.0f));

    g.setColour (textColour);
    g.drawText (text, labelArea, juce::Justification::centredLeft, false);
}

//==============================================================================
StutterStepGrid::StutterStepGrid (juce::AudioProcessorValueTreeState& apvts)
{
    using Ids = LoopRollAudioProcessor;

    shape = apvts.getRawParameterValue (Ids::stutterShapeId);
    depth = apvts.getRawParameterValue (Ids::stutterDepthId);
    swing = apvts.getRawParameterValue (Ids::stutterSwingId);

    for (int i = 0; i < numSteps; ++i)
    {
        on[(size_t) i] = true;
        attachments[(size_t) i] = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (Ids::getStepId (i)),
                                                                              [this, i] (float value)
                                                                              {
                                                                                  on[(size_t) i] = value > 0.5f;
                                                                                  repaint();
                                                                              });
        attachments[(size_t) i]->sendInitialUpdate();
    }

    setComponentID ("stutterSteps");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Range<float> StutterStepGrid::getStepRange (int index) const
{
    const auto plot = plotArea();
    const float pairWidth = plot.getWidth() / (float) (numSteps / 2);
    const float firstWidth = 0.5f * pairWidth * (float) StutterGate::firstOfPair (0.01 * (double) swing->load());
    const float pairStart = plot.getX() + pairWidth * (float) (index / 2);

    return index % 2 == 0 ? juce::Range<float> (pairStart, pairStart + firstWidth)
                          : juce::Range<float> (pairStart + firstWidth, pairStart + pairWidth);
}

int StutterStepGrid::stepAt (float x) const
{
    for (int i = 0; i < numSteps; ++i)
        if (x < getStepRange (i).getEnd())
            return i;

    return numSteps - 1;
}

void StutterStepGrid::setStep (int index, bool shouldBeOn)
{
    if (on[(size_t) index] != shouldBeOn)
        attachments[(size_t) index]->setValueAsCompleteGesture (shouldBeOn ? 1.0f : 0.0f);
}

void StutterStepGrid::mouseDown (const juce::MouseEvent& event)
{
    const int index = stepAt (event.position.x);
    paintValue = ! on[(size_t) index];
    setStep (index, paintValue);
}

void StutterStepGrid::mouseDrag (const juce::MouseEvent& event)
{
    setStep (stepAt (event.position.x), paintValue);   // on peint les pas survolés dans l'état choisi au clic
}

void StutterStepGrid::paint (juce::Graphics& g)
{
    // L'écran en creux est dessiné par le fond de l'interface ; ici, seulement le motif.
    const auto plot = plotArea();
    const double shapeValue = 0.01 * (double) shape->load();
    const double depthValue = 0.01 * (double) depth->load();

    const auto yFor = [&] (double gain)
    {
        const double level = 1.0 - depthValue * (1.0 - gain);
        return plot.getBottom() - (float) level * plot.getHeight();
    };

    // Séparateurs : un trait plus marqué tous les quatre pas.
    for (int i = 1; i < numSteps; ++i)
    {
        const float x = getStepRange (i).getStart();
        g.setColour (LF::dimInk().withAlpha (i % 4 == 0 ? 0.38f : 0.13f));
        g.drawLine (x, plot.getY() - 3.0f, x, plot.getBottom() + 3.0f, 1.0f);
    }

    juce::Path curve;
    curve.startNewSubPath (plot.getX(), yFor (0.0));

    for (int i = 0; i < numSteps; ++i)
    {
        const auto range = getStepRange (i);

        if (on[(size_t) i])
        {
            constexpr int numPoints = 10;

            for (int j = 0; j <= numPoints; ++j)
            {
                const double phase = (double) j / numPoints;
                const double gain = j == numPoints ? 0.0 : StutterGate::shapeGain (phase, shapeValue) * juce::jmin (1.0, phase / 0.1);
                curve.lineTo (range.getStart() + (float) phase * range.getLength(), yFor (gain));
            }
        }
        else
        {
            curve.lineTo (range.getStart(), yFor (0.0));
            curve.lineTo (range.getEnd(), yFor (0.0));

            // Pas éteint : un point discret, pour le retrouver même quand la profondeur est à zéro.
            g.setColour (LF::standby().withAlpha (0.7f));
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ range.getStart() + range.getLength() * 0.5f, plot.getCentreY() }));
        }
    }

    plotCurve (g, plot, curve, 1.6f);
}

//==============================================================================
LoopRollAudioProcessorEditor::LoopRollAudioProcessorEditor (LoopRollAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), stepGrid (p.apvts)
{
    setLookAndFeel (&lookAndFeel);

    using Ids = LoopRollAudioProcessor;
    auto& apvts = audioProcessor.apvts;

    // Grands potards : un par module, deux sliders superposés quand le module a un mode FREE / SYNC.
    configureMainKnob (timeKnob, Ids::timeId, 0);
    configureMainKnob (divisionKnob, Ids::divisionId, 0);
    configureMainKnob (refreshTimeKnob, Ids::refreshTimeId, 1);
    configureMainKnob (refreshDivisionKnob, Ids::refreshDivisionId, 1);
    configureMainKnob (stutterTimeKnob, Ids::stutterTimeId, 2);
    configureMainKnob (stutterDivisionKnob, Ids::stutterDivisionId, 2);
    configureMainKnob (cutoffKnob, Ids::filterCutoffId, 3);
    cutoffKnob.setVisible (true);

    // Petits potards des panneaux du bas
    fadeInKnob.setName ("IN");
    fadeOutKnob.setName ("OUT");
    pitchKnob.setName ("PITCH");
    decayKnob.setName ("DECAY");
    mixKnob.setName ("MIX");
    shapeKnob.setName ("SHAPE");
    depthKnob.setName ("DEPTH");
    swingKnob.setName ("SWING");
    resonanceKnob.setName ("RESO");

    configureMiniKnob (fadeInKnob, Ids::fadeInId, 0, 0, 4);
    configureMiniKnob (fadeOutKnob, Ids::fadeOutId, 0, 1, 4);
    configureMiniKnob (pitchKnob, Ids::pitchId, 0, 2, 4);
    configureMiniKnob (decayKnob, Ids::decayId, 0, 3, 4);
    configureMiniKnob (mixKnob, Ids::mixId, 1, 0, 4);
    configureMiniKnob (shapeKnob, Ids::stutterShapeId, 2, 0, 3);
    configureMiniKnob (depthKnob, Ids::stutterDepthId, 2, 1, 3);
    configureMiniKnob (swingKnob, Ids::stutterSwingId, 2, 2, 3);
    configureMiniKnob (resonanceKnob, Ids::filterResonanceId, 3, 0, 3);

    for (int column = 0; column < numColumns; ++column)
        configureValueLabel (valueLabels[column], column);

    // LOOP : bascule, ou appui maintenu en mode momentané.
    configureBigButton (loopButton, 0);
    loopButton.setComponentID (Ids::loopId);
    loopAttachment = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (Ids::loopId), [this] (float value)
    {
        loopOn = value > 0.5f;
        loopButton.setToggleState (loopOn, juce::dontSendNotification);
        repaint();
    });

    loopButton.onClick = [this]
    {
        if (! momentary)
            loopAttachment->setValueAsCompleteGesture (loopOn ? 0.0f : 1.0f);
    };

    loopButton.onStateChange = [this]
    {
        const bool down = loopButton.isDown();

        if (down == momentaryDown)
            return;

        momentaryDown = down;

        if (momentary)   // la boucle tourne tant que le bouton est enfoncé
            loopAttachment->setValueAsCompleteGesture (down ? 1.0f : 0.0f);
    };

    // Les autres grands boutons sont des bascules. Les lumières de chaque module suivent son état.
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

    // Interrupteurs des baies
    const juce::StringArray freeOrSync { "FREE", "SYNC" };
    configureSwitch (loopMode, Ids::modeId, freeOrSync, switchArea (0, 20, 120), false, false, [this] (int choice) { loopSync = choice == 1; modesChanged(); });
    configureSwitch (refreshMode, Ids::refreshModeId, freeOrSync, switchArea (1, 20, 120), false, false, [this] (int choice) { refreshSync = choice == 1; modesChanged(); });
    configureSwitch (stutterMode, Ids::stutterModeId, freeOrSync, switchArea (2, 20, 120), false, false, [this] (int choice) { stutterSync = choice == 1; modesChanged(); });
    configureSwitch (filterTypeSwitch, Ids::filterTypeId, { "HP", "BP", "LP" }, switchArea (3, 20, 120), false, false, [this] (int choice) { filterType = choice; repaint(); });
    configureSwitch (filterSlope, Ids::filterSlopeId, { "12 dB", "24 dB" }, switchArea (3, 152, 78), false, true, [this] (int choice) { steepFilter = choice == 1; repaint(); });

    configureFeel (loopFeel, Ids::divisionFeelId, 0);
    configureFeel (refreshFeel, Ids::refreshFeelId, 1);
    configureFeel (stutterFeel, Ids::stutterFeelId, 2);

    // Panneaux du bas
    configureSmallToggle (reverseButton, Ids::reverseId, smallToggleArea (0, 0, 0));
    configureSmallToggle (preButton, Ids::preId, smallToggleArea (0, 1, 0));
    configureSmallToggle (snapButton, Ids::snapId, smallToggleArea (0, 0, 1));
    configureSmallToggle (momentaryButton, Ids::momentaryId, smallToggleArea (0, 1, 1));
    configureSmallToggle (limiterButton, Ids::limiterId, { columnX (3) + 100, 522, 136, 24 });

    configureSwitch (chainOrder, Ids::orderId, { "STUTTER > LOOP", "LOOP > STUTTER" }, displayArea (1), true, false, [this] (int choice) { loopFirstInChain = choice == 1; repaint(); });

    addAndMakeVisible (stepGrid);
    stepGrid.setBounds (displayArea (2));

    stateWatchers.push_back (std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (Ids::momentaryId),
                                                                          [this] (float value) { momentary = value > 0.5f; }));

    // Tout est en place : on aligne l'interface sur les paramètres.
    loopAttachment->sendInitialUpdate();

    for (auto& watcher : stateWatchers)
        watcher->sendInitialUpdate();

    for (auto* selector : { &loopMode, &refreshMode, &stutterMode, &filterTypeSwitch, &filterSlope, &chainOrder })
        selector->attachment->sendInitialUpdate();

    for (auto* feel : { &loopFeel, &refreshFeel, &stutterFeel })
        feel->attachment->sendInitialUpdate();

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

void LoopRollAudioProcessorEditor::configureMiniKnob (juce::Slider& knob, const char* parameterId, int column, int slot, int numSlots)
{
    addAndMakeVisible (knob);
    knob.setComponentID (parameterId);
    knob.getProperties().set ("mini", true);
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    knob.setWantsKeyboardFocus (false);
    knob.setBounds (miniKnobArea (column, slot, numSlots));
    knob.onValueChange = [this] { repaint(); };   // afficheurs et valeurs

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

void LoopRollAudioProcessorEditor::configureBigButton (juce::TextButton& button, int column)
{
    addAndMakeVisible (button);
    button.setWantsKeyboardFocus (false);
    button.getProperties().set ("cut", 16.0);
    button.getProperties().set ("big", true);
    button.setBounds (buttonArea (column));
}

void LoopRollAudioProcessorEditor::configureToggle (juce::TextButton& button, const char* parameterId, int column,
                                                    std::function<void (bool)> onChange)
{
    configureBigButton (button, column);
    button.setClickingTogglesState (true);

    buttonAttachments.push_back (std::make_unique<APVTS::ButtonAttachment> (audioProcessor.apvts, parameterId, button));

    stateWatchers.push_back (std::make_unique<juce::ParameterAttachment> (*audioProcessor.apvts.getParameter (parameterId),
                                                                          [this, onChange] (float value)
                                                                          {
                                                                              onChange (value > 0.5f);
                                                                              repaint();
                                                                          }));
}

void LoopRollAudioProcessorEditor::configureSmallToggle (juce::TextButton& button, const char* parameterId, juce::Rectangle<int> area)
{
    addAndMakeVisible (button);
    button.setComponentID (parameterId);
    button.setClickingTogglesState (true);
    button.setWantsKeyboardFocus (false);
    button.getProperties().set ("cut", 5.0);
    button.getProperties().set ("tiny", true);
    button.setBounds (area);

    buttonAttachments.push_back (std::make_unique<APVTS::ButtonAttachment> (audioProcessor.apvts, parameterId, button));
}

void LoopRollAudioProcessorEditor::configureSwitch (ChoiceSwitch& selector, const char* parameterId, const juce::StringArray& texts,
                                                    juce::Rectangle<int> area, bool stacked, bool tiny, std::function<void (int)> onChange)
{
    const int count = texts.size();
    const int gap = 4;
    const int stackedHeight = (area.getHeight() - gap * (count - 1)) / count;

    for (int i = 0; i < count; ++i)
    {
        auto* button = selector.buttons.add (new juce::TextButton (texts[i]));
        addAndMakeVisible (*button);

        // Identifiants "paramètre/0", "paramètre/1"… : pour retrouver chaque position (tests, accessibilité).
        button->setComponentID (juce::String (parameterId) + "/" + juce::String (i));
        button->setClickingTogglesState (false);
        button->setWantsKeyboardFocus (false);
        button->getProperties().set ("cut", 6.0);
        button->getProperties().set (tiny ? "tiny" : "small", true);

        if (stacked)
        {
            button->setBounds (area.getX(), area.getY() + i * (stackedHeight + gap), area.getWidth(), stackedHeight);
        }
        else
        {
            const int left = area.getX() + (area.getWidth() * i) / count;
            const int right = area.getX() + (area.getWidth() * (i + 1)) / count;
            button->setBounds (left, area.getY(), right - left, area.getHeight());
            button->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < count - 1 ? juce::Button::ConnectedOnRight : 0));
        }
    }

    auto* buttons = &selector.buttons;

    selector.attachment = std::make_unique<juce::ParameterAttachment> (*audioProcessor.apvts.getParameter (parameterId),
                                                                       [buttons, onChange] (float value)
                                                                       {
                                                                           const int chosen = juce::roundToInt (value);

                                                                           for (int i = 0; i < buttons->size(); ++i)
                                                                               buttons->getUnchecked (i)->setToggleState (i == chosen, juce::dontSendNotification);

                                                                           onChange (chosen);
                                                                       });

    auto* attachment = selector.attachment.get();

    for (int i = 0; i < count; ++i)
        selector.buttons.getUnchecked (i)->onClick = [attachment, i] { attachment->setValueAsCompleteGesture ((float) i); };
}

void LoopRollAudioProcessorEditor::configureFeel (FeelSwitch& feel, const char* parameterId, int column)
{
    for (auto* button : { &feel.triplet, &feel.dotted })
    {
        addAndMakeVisible (*button);
        button->setClickingTogglesState (false);
        button->setWantsKeyboardFocus (false);
        button->getProperties().set ("cut", 6.0);
        button->getProperties().set ("small", true);
    }

    feel.triplet.setComponentID (juce::String (parameterId) + "/1");
    feel.dotted.setComponentID (juce::String (parameterId) + "/2");
    feel.triplet.setConnectedEdges (juce::Button::ConnectedOnRight);
    feel.dotted.setConnectedEdges (juce::Button::ConnectedOnLeft);
    feel.triplet.setBounds (switchArea (column, 152, 39));
    feel.dotted.setBounds (switchArea (column, 191, 39));

    auto* state = &feel;

    feel.attachment = std::make_unique<juce::ParameterAttachment> (*audioProcessor.apvts.getParameter (parameterId),
                                                                   [this, state] (float value)
                                                                   {
                                                                       state->value = juce::roundToInt (value);
                                                                       state->triplet.setToggleState (state->value == 1, juce::dontSendNotification);
                                                                       state->dotted.setToggleState (state->value == 2, juce::dontSendNotification);
                                                                       updateValueLabels();
                                                                   });

    // Un clic sur la variante déjà allumée revient à la division normale.
    feel.triplet.onClick = [state] { state->attachment->setValueAsCompleteGesture (state->value == 1 ? 0.0f : 1.0f); };
    feel.dotted.onClick = [state] { state->attachment->setValueAsCompleteGesture (state->value == 2 ? 0.0f : 2.0f); };
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

bool LoopRollAudioProcessorEditor::isSynced (int column) const
{
    return column == 0 ? loopSync : column == 1 ? refreshSync : column == 2 && stutterSync;
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

    // Triolet / pointé n'ont de sens que sur une division.
    FeelSwitch* const feels[] = { &loopFeel, &refreshFeel, &stutterFeel };

    for (int column = 0; column < 3; ++column)
    {
        feels[column]->triplet.setEnabled (isSynced (column));
        feels[column]->dotted.setEnabled (isSynced (column));
    }

    updateValueLabels();
    repaint();
}

void LoopRollAudioProcessorEditor::updateValueLabels()
{
    const FeelSwitch* const feels[] = { &loopFeel, &refreshFeel, &stutterFeel };

    for (int column = 0; column < numColumns; ++column)
    {
        auto& knob = activeKnob (column);
        auto text = knob.getTextFromValue (knob.getValue());

        if (column < 3 && isSynced (column))   // "1/16 T" = triolet, "1/16 D" = pointée
            text += feels[column]->value == 1 ? " T" : feels[column]->value == 2 ? " D" : "";

        valueLabels[column].setText (text, juce::dontSendNotification);
    }
}

void LoopRollAudioProcessorEditor::resized() {}

//==============================================================================
void LoopRollAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Le fond (plaques, ombres, textes fixes) est coûteux à dessiner : on le garde en image,
    // à la résolution réelle de l'écran, et on ne redessine à chaque fois que ce qui bouge.
    const float scale = juce::jlimit (1.0f, 4.0f, (float) g.getInternalContext().getPhysicalPixelScaleFactor());

    if (! background.isValid() || std::abs (scale - backgroundScale) > 0.01f)
    {
        backgroundScale = scale;
        background = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) editorWidth * scale),
                                  juce::roundToInt ((float) editorHeight * scale), true);

        juce::Graphics layer (background);
        layer.addTransform (juce::AffineTransform::scale (scale));
        paintBackground (layer);
    }

    g.drawImage (background, getLocalBounds().toFloat());

    paintLights (g);
    paintModules (g);
}

void LoopRollAudioProcessorEditor::paintBackground (juce::Graphics& g)
{
    const auto full = juce::Rectangle<float> (0.0f, 0.0f, (float) editorWidth, (float) editorHeight);
    const float w = (float) editorWidth, mid = w * 0.5f;

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2e7ee), 0.0f, 0.0f, juce::Colour (0xffcbd3dd), 0.0f, (float) editorHeight, false));
    g.fillRect (full);

    // Plaque principale : posée sur le fond, avec un liseré clair le long du bord.
    const auto plateArea = full.reduced (10.0f);
    const auto plate = LF::chamferedRect (plateArea, 18.0f);
    LF::dropShadow (g, plate, 12.0f, 5.0f, 0.38f);
    LF::fillVertical (g, plate, plateArea, juce::Colour (0xfffcfdfe), juce::Colour (0xffeceff3));
    g.setColour (juce::Colour (0xffa9b4c2));
    g.strokePath (plate, juce::PathStrokeType (1.3f));
    g.setColour (juce::Colours::white);
    g.strokePath (LF::chamferedRect (plateArea.reduced (1.8f), 17.0f), juce::PathStrokeType (1.2f));
    g.setColour (juce::Colour (0xffdde2e9));
    g.strokePath (LF::chamferedRect (plateArea.reduced (6.0f), 15.0f), juce::PathStrokeType (1.0f));

    // Joints de l'en-tête et du pied : une rainure (trait sombre, reflet clair dessous)
    const auto headerSeam = polyline ({ { 16.0f, 64.0f }, { mid - 80.0f, 64.0f }, { mid - 68.0f, 76.0f },
                                        { mid + 68.0f, 76.0f }, { mid + 80.0f, 64.0f }, { w - 16.0f, 64.0f } });
    const auto footerSeam = polyline ({ { 16.0f, (float) footerY }, { w - 16.0f, (float) footerY } });

    for (const auto* seam : { &headerSeam, &footerSeam })
    {
        g.setColour (juce::Colours::white);
        g.strokePath (*seam, juce::PathStrokeType (1.2f), juce::AffineTransform::translation (0.0f, 1.2f));
        g.setColour (juce::Colour (0xffb9c3d0));
        g.strokePath (*seam, juce::PathStrokeType (1.2f));
    }

    static const char* const readoutNames[numColumns] = { "LENGTH", "EVERY", "RATE", "CUTOFF" };
    static const char* const panelTitles[numColumns] = { "LOOP REPEATS", "CHAIN", "STUTTER PATTERN", "FILTER RESPONSE" };

    for (int column = 0; column < numColumns; ++column)
    {
        // Baie du potard : en creux dans la plaque.
        const auto bay = bayArea (column).toFloat();
        const auto bayShape = LF::chamferedRect (bay, 22.0f);
        g.setColour (juce::Colours::white);
        g.strokePath (bayShape, juce::PathStrokeType (1.4f), juce::AffineTransform::translation (0.0f, 1.4f));
        LF::fillVertical (g, bayShape, bay, juce::Colour (0xffdde2e9), juce::Colour (0xffebeef3));
        LF::innerShadow (g, bayShape, 10.0f, 4.0f, 0.34f);
        g.setColour (juce::Colour (0xffa9b4c2));
        g.strokePath (bayShape, juce::PathStrokeType (1.2f));

        // Ombre portée du potard : une large et douce, une courte au contact.
        const auto knob = knobArea (column).toFloat();
        const auto body = circle (knob.getCentre(), LF::mainKnobBodyRadius (knob));
        LF::dropShadow (g, body, 13.0f, 9.0f, 0.30f);
        LF::dropShadow (g, body, 3.0f, 2.0f, 0.30f);

        // Afficheur sous le potard
        const auto readout = readoutArea (column);
        LF::drawScreen (g, readout.toFloat(), 8.0f);
        g.setColour (LF::dimInk());
        g.setFont (lookAndFeel.font (10.0f, false, 0.12f));
        g.drawText (readoutNames[column], readout.withTrimmedLeft (14), juce::Justification::centredLeft, false);

        // Panneau du bas : posé sur la plaque.
        const auto panel = panelArea (column).toFloat();
        const auto panelShape = LF::chamferedRect (panel, 14.0f);
        LF::dropShadow (g, panelShape, 8.0f, 4.0f, 0.22f);
        LF::fillVertical (g, panelShape, panel, juce::Colour (0xfffbfcfd), juce::Colour (0xffe8ecf1));
        g.setColour (juce::Colour (0xffb1bcc9));
        g.strokePath (panelShape, juce::PathStrokeType (1.1f));
        g.setColour (juce::Colours::white);
        g.strokePath (LF::chamferedRect (panel.reduced (1.5f), 13.0f), juce::PathStrokeType (1.1f));

        g.setColour (LF::dimInk());
        g.setFont (lookAndFeel.font (9.0f, false, 0.12f));
        g.drawText (panelTitles[column], panelArea (column).withTrimmedLeft (panelInset).withHeight (28), juce::Justification::centredLeft, false);
    }

    // Écrans des panneaux : enveloppe de la boucle, motif du stutter, réponse du filtre
    LF::drawScreen (g, displayArea (0, 96).toFloat(), 6.0f);
    LF::drawScreen (g, stepGrid.getBounds().toFloat(), 6.0f);
    LF::drawScreen (g, displayArea (3).toFloat(), 6.0f);

    // Ombres portées des boutons et des petits potards, noms des petits potards
    for (auto* child : getChildren())
    {
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            const bool big = button->getProperties().contains ("big");
            const float cut = (float) (double) button->getProperties().getWithDefault ("cut", 8.0);
            const auto shape = LF::chamferedRect (button->getBounds().toFloat(), cut);

            if (big)
                LF::dropShadow (g, shape, 9.0f, 5.0f, 0.30f);

            LF::dropShadow (g, shape, big ? 3.0f : 2.0f, big ? 2.0f : 1.6f, big ? 0.30f : 0.34f);
        }
        else if (auto* knob = dynamic_cast<juce::Slider*> (child))
        {
            if (! knob->getProperties().contains ("mini"))
                continue;

            const auto bounds = knob->getBounds().toFloat();
            LF::dropShadow (g, circle (bounds.getCentre(), LF::miniKnobBodyRadius (bounds)), 4.0f, 3.0f, 0.34f);

            g.setColour (LF::dimInk());
            g.setFont (lookAndFeel.font (9.0f, false, 0.1f));
            g.drawText (knob->getName(), juce::Rectangle<int> (60, 12).withCentre ({ knob->getBounds().getCentreX(), knob->getBottom() + 8 }),
                        juce::Justification::centred, false);
        }
    }

    // En-tête : nom du plugin, signature, et numéro de version à droite
    const auto titleFont = lookAndFeel.font (20.0f, true, 0.24f);
    const auto titleArea = juce::Rectangle<float> (36.0f, 16.0f, 420.0f, 48.0f);

    g.setColour (LF::ink());
    g.setFont (titleFont);
    g.drawText ("PHRAGMENTS", titleArea, juce::Justification::centredLeft, false);

    // La signature est posée sur la même ligne de base que le nom (d'où le décalage vers le bas).
    g.setColour (LF::dimInk());
    g.setFont (lookAndFeel.font (11.0f, false, 0.26f));
    g.drawText ("BY PHRAGMA", titleArea.withTrimmedLeft (textWidth (titleFont, "PHRAGMENTS") + 18.0f).translated (0.0f, 1.5f),
                juce::Justification::centredLeft, false);

    // Numéro de version : pour vérifier d'un coup d'œil quelle version le DAW a chargée
    g.setFont (lookAndFeel.font (11.0f, false, 0.2f));
    g.drawText ("V" LOOPROLL_VERSION, juce::Rectangle<float> ((float) editorWidth - 336.0f, 16.0f, 300.0f, 48.0f),
                juce::Justification::centredRight, false);
}

void LoopRollAudioProcessorEditor::paintLights (juce::Graphics& g)
{
    const bool moduleOn[numColumns] = { loopOn, refreshOn, stutterOn, filterOn };
    const float pipY = (float) footerY + 11.0f;

    // Trait éteint : une rainure (reflet clair dessous).
    const auto strokeUnlit = [&g] (const juce::Path& path, const juce::PathStrokeType& stroke)
    {
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.strokePath (path, stroke, juce::AffineTransform::translation (0.0f, 1.2f));
        g.setColour (LF::unlit());
        g.strokePath (path, stroke);
    };

    for (int column = 0; column < numColumns; ++column)
    {
        const float x = (float) columnX (column);
        const float right = x + (float) columnWidth;

        // Néons d'angle de la baie, et voyants du pied
        const Pt leftStart { x + 13.0f, 150.0f }, leftEnd { x + 80.0f, 97.0f };
        const Pt rightStart { right - 13.0f, 150.0f }, rightEnd { right - 80.0f, 97.0f };
        const auto leftStrip = polyline ({ leftStart, { x + 13.0f, 112.0f }, { x + 28.0f, 97.0f }, leftEnd });
        const auto rightStrip = polyline ({ rightStart, { right - 13.0f, 112.0f }, { right - 28.0f, 97.0f }, rightEnd });

        juce::Path pips;
        pips.startNewSubPath (x + 12.0f, pipY);
        pips.lineTo (x + 24.0f, pipY);
        pips.startNewSubPath (x + 32.0f, pipY);
        pips.lineTo (x + 44.0f, pipY);

        const juce::PathStrokeType pipStroke (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const juce::PathStrokeType stripStroke (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        if (moduleOn[column])
        {
            LF::strokeGlowing (g, leftStrip, 3.0f, LF::accentGradient (leftStart, leftEnd));
            LF::strokeGlowing (g, rightStrip, 3.0f, LF::accentGradient (rightStart, rightEnd));
            LF::strokeGlowing (g, pips, 2.5f, LF::accentGradient ({ x + 12.0f, pipY }, { x + 44.0f, pipY }));

            const auto button = buttonArea (column).toFloat();
            drawHalo (g, LF::chamferedRect (button, 16.0f), LF::accentGradient (button.getTopLeft(), button.getTopRight(), true));
        }
        else
        {
            strokeUnlit (leftStrip, stripStroke);
            strokeUnlit (rightStrip, stripStroke);
            strokeUnlit (pips, pipStroke);
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
        strokeUnlit (leftBars, juce::PathStrokeType (4.0f));
        strokeUnlit (rightBars, juce::PathStrokeType (4.0f));
    }
}

void LoopRollAudioProcessorEditor::paintModules (juce::Graphics& g)
{
    // --- Fondus de la boucle : forme de l'enveloppe sur un passage ---
    {
        const auto plot = displayArea (0, 96).toFloat().reduced (9.0f, 8.0f);
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

        plotCurve (g, plot, curve);
    }

    // --- Filtre : courbe de réponse, de 20 Hz à 20 kHz ---
    {
        const auto plot = displayArea (3).toFloat().reduced (9.0f, 8.0f);
        const double cutoff = cutoffKnob.getValue();
        const double quality = OutputFilter::qualityFor (resonanceKnob.getValue() * 0.01);
        const int numPoints = 110;
        juce::Path curve;

        for (int i = 0; i <= numPoints; ++i)
        {
            const double position = (double) i / numPoints;
            const double frequency = OutputFilter::minFrequency * std::pow (OutputFilter::maxFrequency / OutputFilter::minFrequency, position);
            const double decibels = 20.0 * std::log10 (juce::jmax (1.0e-5, OutputFilter::magnitudeAt (frequency, cutoff, quality, filterType, steepFilter)));
            const double level = juce::jlimit (0.0, 1.0, (decibels + 36.0) / 56.0);   // de -36 dB (bas) à +20 dB (haut)
            const Pt point { plot.getX() + (float) position * plot.getWidth(), plot.getBottom() - (float) level * plot.getHeight() };

            if (i == 0)
                curve.startNewSubPath (point);
            else
                curve.lineTo (point);
        }

        plotCurve (g, plot, curve);
    }

    // --- Chaîne : rappel du trajet du son, à côté du potard de mélange ---
    {
        const auto area = juce::Rectangle<int> (columnX (1) + 74, 516, 162, 40);
        g.setColour (LF::dimInk());
        g.setFont (lookAndFeel.font (9.0f, false, 0.1f));
        g.drawText (loopFirstInChain ? "LOOP  >  STUTTER" : "STUTTER  >  LOOP", area.withHeight (20), juce::Justification::centredLeft, false);
        g.drawText (">  FILTER  >  MIX", area.withTrimmedTop (20), juce::Justification::centredLeft, false);
    }

    // Valeurs des petits potards (leurs noms sont dans le fond)
    g.setColour (LF::ink());
    g.setFont (lookAndFeel.font (10.5f, true));

    for (auto* knob : { &fadeInKnob, &fadeOutKnob, &pitchKnob, &decayKnob, &mixKnob, &shapeKnob, &depthKnob, &swingKnob, &resonanceKnob })
        g.drawText (knob->getTextFromValue (knob->getValue()),
                    juce::Rectangle<int> (60, 12).withCentre ({ knob->getBounds().getCentreX(), knob->getBottom() + 20 }),
                    juce::Justification::centred, false);
}
