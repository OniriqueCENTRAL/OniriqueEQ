#include "UIKit.h"
#include <OniriqueLicenseData.h>
#include "OniriqueKnob.h"
#include "OniriqueWindowSize.h"
#include <BinaryData.h>
#include <array>
#include <map>

using namespace onirique;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;

juce::Point<float> onCircle (juce::Point<float> c, float r, float ang)
{
    return { c.x + std::sin (ang) * r, c.y - std::cos (ang) * r };
}

// pastille de valeur (boutons, curseurs, champs) : fond sombre, liseré, point braise
void drawValuePill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float glow, float u)
{
    const float rad = r.getHeight() * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::ember.withAlpha (0.14f * glow));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::line.interpolatedWith (ui::ember.withAlpha (0.8f), glow));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, std::max (1.0f, 1.2f * u));

    const float dot = r.getHeight() * 0.22f;
    const auto dotR = juce::Rectangle<float> (dot, dot).withCentre ({ r.getX() + rad * 0.95f, r.getCentreY() });
    g.setColour (ui::ember.withAlpha (0.25f + 0.35f * glow));
    g.fillEllipse (dotR.expanded (dot * 0.6f * glow));
    g.setColour (ui::ember.interpolatedWith (ui::bone, 0.35f * glow));
    g.fillEllipse (dotR);

    const auto textR = r.withTrimmedLeft (rad * 1.5f).withTrimmedRight (rad * 0.6f);
    g.setColour (ui::mist);
    g.setFont (ui::fitFont (ui::Face::mono, r.getHeight() * 0.56f, textR.getWidth(), text));
    g.drawText (text, textR, juce::Justification::centred, false);
}
} // namespace

//==============================================================================
namespace
{
// les polices de la charte, partagées par les interfaces ouvertes (thread de l'interface uniquement) : créées à
// l'ouverture de la première, détruites à la fermeture de la dernière. Seuls un compteur et un pointeur brut sont
// statiques : rien n'est détruit au déchargement du plugin.
struct Faces
{
    juce::Typeface::Ptr display, body, mono, monoBold;
};
Faces* faces = nullptr;
int faceUsers = 0;
} // namespace

ui::FontScope::FontScope()
{
    if (faceUsers++ == 0)
    {
        faces = new Faces();
        faces->display = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::Unbounded800_ttf, OniriqueLicenseData::Unbounded800_ttfSize);
        faces->body = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::HankenGrotesk600_ttf, OniriqueLicenseData::HankenGrotesk600_ttfSize);
        faces->mono = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::JetBrainsMono500_ttf, OniriqueLicenseData::JetBrainsMono500_ttfSize);
        faces->monoBold = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::JetBrainsMono600_ttf, OniriqueLicenseData::JetBrainsMono600_ttfSize);
    }
}

ui::FontScope::~FontScope()
{
    if (--faceUsers == 0) { delete faces; faces = nullptr; }
}

juce::Font ui::font (Face f, float h, bool bold, float kerning)
{
    h = std::max (4.0f, h);
    const juce::Typeface::Ptr tf = faces == nullptr ? nullptr
                                 : f == Face::display ? faces->display : f == Face::body ? faces->body : (bold ? faces->monoBold : faces->mono);
    // Unbounded est très large : un peu plus petit que la hauteur demandée, pour garder les mêmes encombrements
    juce::Font fnt = tf != nullptr ? juce::Font (juce::FontOptions (tf).withHeight (f == Face::display ? h * 0.86f : h))
                                   : juce::Font (juce::FontOptions (f == Face::display ? "Bahnschrift" : f == Face::body ? "Segoe UI" : "Cascadia Mono",
                                                                    h, bold ? juce::Font::bold : juce::Font::plain));
    if (kerning != 0.0f) fnt.setExtraKerningFactor (kerning);
    return fnt;
}

float ui::editorScale (const juce::Component& c)
{
    if (auto* ed = c.findParentComponentOfClass<juce::AudioProcessorEditor>()) return (float) ed->getWidth() / kEditorBaseW;
    if (auto* ed = dynamic_cast<const juce::AudioProcessorEditor*> (&c)) return (float) ed->getWidth() / kEditorBaseW;
    return 1.0f;
}

float ui::textWidth (const juce::Font& f, const juce::String& t)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, t, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

juce::Font ui::fitFont (Face face, float maxH, float maxW, const juce::String& text, bool bold, float kerning)
{
    auto f = font (face, maxH, bold, kerning);
    const float w = textWidth (f, text);
    if (w > maxW && w > 0.0f) f.setHeight (std::max (4.0f, f.getHeight() * maxW / w));
    return f;
}

void ui::strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float intensity)
{
    if (intensity <= 0.002f) return;
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, intensity * 0.17f / (float) i)));
        g.strokePath (p, juce::PathStrokeType (width * (1.0f + 1.15f * (float) i), juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}

void ui::drawCachedShadow (juce::Graphics& g, ShadowCache& shadowCache, const juce::DropShadow& ds, juce::Rectangle<float> shape, float corner)
{
    // clé : taille et position dans le pixel de la forme, coin, flou, décalage (au 1/64 de pixel près)
    using Key = std::array<int, 8>;
    auto& cache = shadowCache.images;   // thread de l'interface uniquement

    const auto ib = shape.getSmallestIntegerContainer();
    const auto q = [] (float v) { return juce::roundToInt (v * 64.0f); };
    const Key key { q (shape.getWidth()), q (shape.getHeight()), q (shape.getX() - (float) ib.getX()), q (shape.getY() - (float) ib.getY()),
                    corner < 0.0f ? -1 : q (corner), ds.radius, ds.offset.x, ds.offset.y };

    // zone de l'ombre relative au coin de la forme, comme DropShadow::drawForPath (sans le découpage par la zone à redessiner)
    const auto area = (ib.withZeroOrigin() + ds.offset).expanded (ds.radius + 1);
    auto it = cache.find (key);
    if (it == cache.end())
    {
        if (cache.size() > 512) cache.clear();
        // image en mémoire ordinaire : rien qui dépende des ressources graphiques du système (Direct2D, Core Graphics)
        juce::Image pathImage (juce::Image::SingleChannel, area.getWidth(), area.getHeight(), true, juce::SoftwareImageType());
        {
            juce::Graphics g2 (pathImage);
            g2.setColour (juce::Colours::white);
            const auto local = shape.translated ((float) (ds.offset.x - ib.getX() - area.getX()), (float) (ds.offset.y - ib.getY() - area.getY()));
            if (corner < 0.0f) g2.fillEllipse (local);
            else               g2.fillRoundedRectangle (local, corner);
        }
        juce::Image blurred;
        juce::ImageEffects::applySingleChannelBoxBlurEffect (ds.radius, pathImage, blurred);
        it = cache.emplace (key, blurred).first;
    }
    g.setColour (ds.colour);
    g.drawImageAt (it->second, ib.getX() + area.getX(), ib.getY() + area.getY(), true);
}

//==============================================================================
OniriqueLookAndFeel::OniriqueLookAndFeel()
{
    setDefaultSansSerifTypefaceName ("Segoe UI");
    setColour (juce::TooltipWindow::backgroundColourId, ui::deep2);
    setColour (juce::TooltipWindow::textColourId, ui::mist);
    setColour (juce::TooltipWindow::outlineColourId, ui::line);
    setColour (juce::PopupMenu::backgroundColourId, ui::deep2.withAlpha (0.99f));   // non opaque : coins arrondis transparents
    setColour (juce::PopupMenu::textColourId, ui::mist);
    setColour (juce::PopupMenu::headerTextColourId, ui::haze);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::ember.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, ui::bone);
}

void OniriqueLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float a0, float a1, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float D = juce::jmin (b.getWidth(), b.getHeight());
    if (D < 8.0f) return;

    const auto c = b.getCentre();
    const float R = D * 0.5f, u = D / 150.0f;
    const auto& props = s.getProperties();
    const float shown = (float) props.getWithDefault ("shown", pos);
    const float glow = (float) props.getWithDefault ("glow", 0.0f);
    const bool bipolar = (bool) props.getWithDefault ("bipolar", false);

    // ---- le potard ONIRIQUE, le même dans tous les plugins de la marque (OniriqueKnob.h) : graduations allumées
    //      jusqu'à la valeur (depuis le centre si bipolaire), corps biseauté, aiguille à l'étoile
    onirique::ui::drawKnob (g, b, shown, a0, a1, ui::ember, bipolar, s.isMouseOverOrDragging() || glow > 0.5f);

    // ---- bornes de l'échelle
    const auto minL = props["minLabel"].toString(), maxL = props["maxLabel"].toString();
    if (minL.isNotEmpty() || maxL.isNotEmpty())
    {
        g.setColour (ui::haze.withAlpha (0.8f));
        for (int side = 0; side < 2; ++side)
        {
            const auto& txt = side == 0 ? minL : maxL;
            const float es = ui::editorScale (s);   // plancher lisible même sur un petit bouton
            const auto f = ui::fitFont (ui::Face::mono, std::max (11.0f * u, 10.0f * es), std::max (0.34f * D, 30.0f * es), txt);
            const float tw = ui::textWidth (f, txt) + 2.0f;
            const auto pt = onCircle (c, R * 0.9f, side == 0 ? a0 : a1);
            const float cx = juce::jlimit (b.getX() + tw * 0.5f, b.getRight() - tw * 0.5f, pt.x);
            g.setFont (f);
            g.drawText (txt, juce::Rectangle<float> (tw, f.getHeight() * 1.3f).withCentre ({ cx, pt.y + 0.17f * R }),
                        juce::Justification::centred, false);
        }
    }
}

//==============================================================================
void OniriqueLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (ui::deep2, 0.0f, 0.0f, ui::deep, 0.0f, (float) height, false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.10f), (float) width * 0.8f, 0.0f,
                                             ui::ember.withAlpha (0.0f), (float) width * 0.8f, (float) height * 0.6f, true));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r, 12.0f, 1.0f);
}

void OniriqueLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& w, int& h)
{
    if (isSeparator) { w = 50; h = 11; return; }
    w = juce::roundToInt (ui::textWidth (getPopupMenuFont(), text)) + 56;
    h = 30;
}

void OniriqueLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    const auto r = area.toFloat().withTrimmedLeft (16.0f).withTrimmedTop (4.0f);
    const auto f = ui::font (ui::Face::mono, 10.5f, true, 0.18f);
    g.setFont (f);
    g.setColour (ui::haze);
    g.drawText (name, r, juce::Justification::centredLeft, false);
    const float tw = ui::textWidth (f, name);
    g.setColour (ui::line);
    g.fillRect (juce::Rectangle<float> (r.getX() + tw + 10.0f, r.getCentreY(), r.getWidth() - tw - 26.0f, 1.0f));
}

void OniriqueLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                             bool isHighlighted, bool isTicked, bool, const juce::String& text,
                                             const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (ui::line);
        g.fillRect (juce::Rectangle<float> (14.0f, (float) area.getCentreY(), (float) area.getWidth() - 28.0f, 1.0f));
        return;
    }

    const auto r = area.toFloat().reduced (5.0f, 2.0f);
    if (isHighlighted && isActive)
    {
        juce::Path p;
        p.addRoundedRectangle (r, r.getHeight() * 0.5f);
        ui::strokeGlow (g, p, ui::ember, 1.0f, 0.45f);
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.38f), r.getX(), r.getY(),
                                                 ui::blood.withAlpha (0.35f), r.getRight(), r.getBottom(), false));
        g.fillPath (p);
        g.setColour (ui::ember.withAlpha (0.7f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // élément actif (preset chargé) : point braise lumineux
    const juce::Point<float> dot { r.getX() + 14.0f, r.getCentreY() };
    if (isTicked)
    {
        g.setColour (ui::ember.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (dot));
        g.setColour (ui::ember.interpolatedWith (ui::bone, 0.25f));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
    }

    g.setColour (! isActive ? ui::haze.withAlpha (0.45f)
                            : (isHighlighted || isTicked ? ui::bone : ui::mist.withAlpha (0.88f)));
    g.setFont (getPopupMenuFont().withStyle (isTicked ? juce::Font::bold : juce::Font::plain));
    g.drawText (text, r.withTrimmedLeft (28.0f).withTrimmedRight (10.0f), juce::Justification::centredLeft, true);
}

// curseur vertical : la hauteur du curseur vaut D (même unité que les boutons rotatifs)
int OniriqueLookAndFeel::getSliderThumbRadius (juce::Slider& s)
{
    if (s.getSliderStyle() == juce::Slider::LinearVertical)
        return juce::roundToInt (s.getHeight() / 150.0f * 9.0f);
    if (s.getSliderStyle() == juce::Slider::LinearHorizontal)
        return juce::roundToInt (s.getHeight() * 0.32f);
    return LookAndFeel_V4::getSliderThumbRadius (s);
}

void OniriqueLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style == juce::Slider::LinearHorizontal)
    {
        // curseur horizontal : rail fin, remplissage braise lumineux depuis la gauche, pastille claire
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float u = b.getHeight() / 30.0f, cy = b.getCentreY();
        const float r = (float) getSliderThumbRadius (s);
        const float left = b.getX() + r, right = b.getRight() - r;
        const float shown = (float) s.getProperties().getWithDefault ("shown", s.valueToProportionOfLength (s.getValue()));
        const float glow = (float) s.getProperties().getWithDefault ("glow", 0.0f);
        const float tx = left + shown * (right - left);
        const float trackH = std::max (3.0f, 4.0f * u);

        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - trackH * 0.5f, right - left, trackH), trackH * 0.5f);
        if (tx - left > 0.5f)
        {
            juce::Path fill;
            fill.startNewSubPath (left, cy);
            fill.lineTo (tx, cy);
            ui::strokeGlow (g, fill, ui::ember, trackH, 0.5f + 0.5f * glow);
            g.setGradientFill (juce::ColourGradient (ui::ember, left, cy, ui::bone, tx, cy, false));
            g.strokePath (fill, juce::PathStrokeType (trackH, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const auto thumb = juce::Rectangle<float> (1.3f * r, 2.0f * r).withCentre ({ tx, cy });
        g.setColour (ui::ember.withAlpha (0.22f + 0.25f * glow));
        g.fillRoundedRectangle (thumb.expanded (3.0f * u), thumb.getWidth() * 0.5f + 3.0f * u);
        g.setGradientFill (juce::ColourGradient (ui::mist, thumb.getX(), thumb.getY(), juce::Colour (0xffd9c9cb), thumb.getX(), thumb.getBottom(), false));
        g.fillRoundedRectangle (thumb, thumb.getWidth() * 0.5f);
        g.setColour (ui::ember);
        g.drawLine (tx, thumb.getY() + 4.0f * u, tx, thumb.getBottom() - 4.0f * u, std::max (1.2f, 1.8f * u));
        return;
    }

    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }

    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float u = b.getHeight() / 150.0f, cx = b.getCentreX();
    const float thumbR = (float) getSliderThumbRadius (s);
    const float top = b.getY() + thumbR, bottom = b.getBottom() - thumbR;
    const auto& props = s.getProperties();
    const float shown = (float) props.getWithDefault ("shown", s.valueToProportionOfLength (s.getValue()));
    const float glow = (float) props.getWithDefault ("glow", 0.0f);
    const float ty = bottom - shown * (bottom - top);

    // ---- graduations : points discrets, ceux atteints s'allument
    for (int i = 0; i <= 10; ++i)
    {
        const float ti = (float) i / 10.0f;
        const float yy = bottom - ti * (bottom - top);
        const float d = (i % 5 == 0 ? 3.4f : 2.2f) * u;
        g.setColour (ti <= shown + 1.0e-3f ? ui::ember.withAlpha (0.75f) : juce::Colours::white.withAlpha (0.16f));
        for (float dx : { -13.0f * u, 13.0f * u })
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ cx + dx, yy }));
    }

    // ---- rail + remplissage lumineux du bas jusqu'au curseur
    const float trackW = std::max (3.0f, 5.0f * u);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRoundedRectangle (juce::Rectangle<float> (cx - trackW * 0.5f, top, trackW, bottom - top), trackW * 0.5f);
    if (bottom - ty > 0.5f)
    {
        juce::Path fill;
        fill.startNewSubPath (cx, bottom);
        fill.lineTo (cx, ty);
        ui::strokeGlow (g, fill, ui::ember, trackW, 0.5f + 0.5f * glow);
        g.setGradientFill (juce::ColourGradient (ui::ember, cx, bottom, ui::bone, cx, ty, false));
        g.strokePath (fill, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- curseur : pastille claire avec halo braise (comme les curseurs du site)
    const float tw = std::min (b.getWidth() * 0.56f, 34.0f * u), th = 1.5f * thumbR;
    const auto thumb = juce::Rectangle<float> (tw, th).withCentre ({ cx, ty });
    g.setColour (ui::ember.withAlpha (0.22f + 0.25f * glow));
    g.fillRoundedRectangle (thumb.expanded (4.0f * u), th * 0.5f + 4.0f * u);
    juce::Path tp;
    tp.addRoundedRectangle (thumb, th * 0.5f);
    ui::drawCachedShadow (g, shadows, juce::DropShadow (juce::Colours::black.withAlpha (0.5f), juce::roundToInt (8.0f * u), { 0, juce::roundToInt (3.0f * u) }),
                          thumb, th * 0.5f);
    g.setGradientFill (juce::ColourGradient (ui::mist, thumb.getX(), thumb.getY(), juce::Colour (0xffd9c9cb),
                                             thumb.getX(), thumb.getBottom(), false));
    g.fillPath (tp);
    g.setColour (ui::ember);
    g.drawLine (thumb.getX() + 7.0f * u, ty, thumb.getRight() - 7.0f * u, ty, std::max (1.5f, 2.4f * u));
}

//==============================================================================
KnobComponent::KnobComponent (juce::AudioProcessorValueTreeState& apvts, const char* paramId,
                              const juce::String& t, const juce::String& minLabel, const juce::String& maxLabel,
                              Layout l, const juce::String& tooltip, juce::LookAndFeel& lnf, bool bipolar)
    : title (t), layout (l)
{
    slider.setSliderStyle (l == Layout::fader ? juce::Slider::LinearVertical : l == Layout::horizontal ? juce::Slider::LinearHorizontal
                                              : juce::Slider::RotaryHorizontalVerticalDrag);   // glisser vertical OU horizontal
    slider.setRotaryParameters (1.25f * kPi, 2.75f * kPi, true);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setMouseDragSensitivity (220);
    slider.setLookAndFeel (&lnf);
    slider.setTooltip (tooltip);
    auto& props = slider.getProperties();
    props.set ("minLabel", minLabel);
    props.set ("maxLabel", maxLabel);
    props.set ("bipolar", bipolar);
    props.set ("shown", 0.0f);     // à l'ouverture, les arcs se déploient jusqu'à leur valeur
    props.set ("glow", 0.0f);

    if (auto* param = apvts.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));   // double-clic = reset

    slider.onDragStart = [this] { setFocus (hovering, true); };
    slider.onDragEnd   = [this] { setFocus (hovering, false); };
    slider.onValueChange = [this] { repaint (valueArea.getSmallestIntegerContainer().expanded (2)); };
    slider.addMouseListener (this, false);

    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, slider);
    setBufferedToImage (true);   // repos : une image prête (les photos des pages plein écran et les fondus ne le redessinent pas)
}

KnobComponent::~KnobComponent()
{
    slider.removeMouseListener (this);
    slider.setLookAndFeel (nullptr);
}

void KnobComponent::setFocus (bool hover, bool drag)
{
    const bool was = hovering || dragging;
    hovering = hover;
    dragging = drag;
    if ((hovering || dragging) != was && onFocusChange) onFocusChange (hovering || dragging);
}

void KnobComponent::mouseEnter (const juce::MouseEvent&) { setFocus (true, dragging); }
void KnobComponent::mouseExit (const juce::MouseEvent&)  { setFocus (false, dragging); }

void KnobComponent::tick()
{
    const float target = (float) slider.valueToProportionOfLength (slider.getValue());
    bool changed = ui::approach (shown, target, 0.30f);
    if (activity) changed |= ui::approach (act, activity() > 0.5f ? 1.0f : 0.0f, 0.3f);
    changed |= ui::approach (glow, dragging ? 1.0f : (hovering ? 0.6f : 0.0f), 0.18f);
    if (changed)
    {
        slider.getProperties().set ("shown", shown);
        slider.getProperties().set ("glow", glow);
        slider.repaint();
        repaint (nameArea.getUnion (valueArea).getSmallestIntegerContainer().expanded (2));
    }
}

void KnobComponent::resized()
{
    const auto area = getLocalBounds().toFloat();
    juce::Rectangle<float> knob;

    if (layout == Layout::horizontal)
    {
        // une seule ligne : nom (36 %) | curseur | pastille de valeur (24 %)
        const float h = area.getHeight();
        unit = h / 30.0f;
        nameArea = area.withWidth (area.getWidth() * 0.36f);
        valueArea = area.withLeft (area.getRight() - area.getWidth() * 0.24f).reduced (0.0f, h * 0.08f);
        knob = area.withLeft (nameArea.getRight() + 4.0f * unit).withRight (valueArea.getX() - 6.0f * unit);
    }
    else if (layout != Layout::textRight)
    {
        // nom + bouton ou curseur (D) + pastille de valeur. Le nom et la pastille ont une taille MINIMALE liée à
        // la fenêtre : sur les petits boutons (multibande, 1176...) les textes restent lisibles, c'est le bouton
        // qui rétrécit un peu pour leur laisser la place.
        const bool fader = (layout == Layout::fader);
        const float es = ui::editorScale (*this);
        float D = std::max (10.0f, juce::jmin (maxD, fader ? area.getHeight() / 1.47f
                                                           : juce::jmin (area.getWidth() - 2.0f, area.getHeight() / 1.47f)));
        // curseur vertical : textes calés sur la largeur (un grand curseur, comme celui du NOISE SUPPRESSOR, garde un nom et
        // une pastille de valeur à taille normale)
        const float Dt = fader ? std::min (D, area.getWidth() * 1.3f) : D;
        const float nameH = std::max (0.15f * Dt, 17.0f * es), pillH = std::max (0.21f * Dt, 23.0f * es);
        D = std::max (10.0f, std::min (D, (area.getHeight() - nameH - pillH) / 1.11f));
        unit = D / 150.0f;
        const float total = nameH + 0.05f * D + D + 0.06f * D + pillH;
        const float top = area.getY() + (area.getHeight() - total) * 0.5f;
        nameArea = { area.getX(), top, area.getWidth(), nameH };
        const float knobTop = top + nameH + 0.05f * D;
        knob = fader ? juce::Rectangle<float> (area.getX(), knobTop, area.getWidth(), D)
                     : juce::Rectangle<float> (D, D).withCentre ({ area.getCentreX(), knobTop + D * 0.5f });
        const float pillW = juce::jmin (area.getWidth() * (fader ? 0.98f : 0.92f), std::max (Dt * 0.95f, 92.0f * es));
        valueArea = juce::Rectangle<float> (pillW, pillH).withCentre ({ area.getCentreX(), knob.getBottom() + 0.06f * D + pillH * 0.5f });
    }
    else
    {
        const float D = std::max (10.0f, juce::jmin (area.getHeight() - 2.0f, area.getWidth() * 0.50f));
        unit = D / 150.0f;
        knob = juce::Rectangle<float> (D, D).withCentre ({ area.getX() + D * 0.5f + 2.0f, area.getCentreY() });
        const float tx = knob.getRight() + 0.12f * D;
        const float tw = std::max (10.0f, area.getRight() - tx - 2.0f);
        const float blockH = 0.17f * D + 0.08f * D + 0.28f * D;
        const float ty = area.getCentreY() - blockH * 0.5f;
        nameArea  = { tx, ty, tw, 0.17f * D };
        valueArea = { tx, ty + 0.25f * D, std::min (tw, D * 1.3f), 0.28f * D };
    }

    slider.setBounds (knob.toNearestInt());
}

void KnobComponent::paint (juce::Graphics& g)
{
    const float u = unit;
    auto nameR = nameArea;
    if (activity)
    {
        // LED d'activité devant le nom (s'allume quand le module agit, ex. plosive détectée)
        const float d = nameR.getHeight() * 0.36f;
        // LED (et son halo) entièrement dans le composant : le halo n'est plus coupé par le bord gauche
        const auto led = juce::Rectangle<float> (d, d).withCentre ({ nameR.getX() + d * 1.6f, nameR.getCentreY() });
        g.setColour (ui::ember.withAlpha (0.45f * act));
        g.fillEllipse (led.expanded (d * 0.8f));
        g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, act));
        g.fillEllipse (led);
        nameR.removeFromLeft (d * 3.0f);
    }
    g.setColour (ui::haze.interpolatedWith (ui::mist, glow));
    g.setFont (ui::fitFont (ui::Face::body, layout == Layout::horizontal ? nameArea.getHeight() * 0.48f
                                          : std::max ({ 8.0f, (layout == Layout::textRight ? 18.0f : 15.0f) * u, nameArea.getHeight() * 0.72f }),
                            nameR.getWidth() - 2.0f, title, true, 0.14f));
    g.drawText (title, nameR, (layout == Layout::textRight || layout == Layout::horizontal) ? juce::Justification::centredLeft
                                                                                           : juce::Justification::centred, false);

    drawValuePill (g, valueArea, slider.getTextFromValue (slider.getValue()), glow, u);
}

//==============================================================================
DragValue::DragValue (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& tooltip)
{
    setTooltip (tooltip);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    param = apvts.getParameter (paramId);
    if (param != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); });
        attachment->sendInitialUpdate();
    }
}

void DragValue::tick()
{
    if (ui::approach (glow, dragging ? 1.0f : (hovering ? 0.6f : 0.0f), 0.18f)) repaint();
}

void DragValue::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    if (! dragging && onFocusChange) onFocusChange (true);
}

void DragValue::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    if (! dragging && onFocusChange) onFocusChange (false);
}

void DragValue::mouseDown (const juce::MouseEvent&)
{
    if (param == nullptr) return;
    dragging = true;
    startNorm = param->getValue();
    attachment->beginGesture();
}

void DragValue::mouseDrag (const juce::MouseEvent& e)
{
    if (param == nullptr) return;
    const float range = e.mods.isShiftDown() ? 900.0f : 220.0f;   // Maj = réglage fin
    const float norm = juce::jlimit (0.0f, 1.0f, startNorm - (float) e.getDistanceFromDragStartY() / range);
    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (norm));
}

void DragValue::mouseUp (const juce::MouseEvent&)
{
    if (param == nullptr) return;
    dragging = false;
    attachment->endGesture();
    if (! hovering && onFocusChange) onFocusChange (false);
}

void DragValue::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param != nullptr) attachment->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

void DragValue::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (param == nullptr) return;
    const float norm = juce::jlimit (0.0f, 1.0f, param->getValue() + w.deltaY * 0.06f);
    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (norm));
}

void DragValue::paint (juce::Graphics& g)
{
    if (param == nullptr) return;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float u = r.getHeight() / 30.0f;
    drawValuePill (g, r, prefix + param->getCurrentValueAsText(), glow, u);
}

//==============================================================================
void PowerButton::tick()
{
    const bool st = getToggleState();
    if (st && ! was) ripple = 1.0f;   // allumé : une onde braise part du bouton
    was = st;
    bool changed = ui::approach (on, st ? 1.0f : 0.0f, 0.2f);
    if (ripple > 0.0f) { ripple = std::max (0.0f, ripple - 0.045f); changed = true; }
    if (changed) repaint();
}

void PowerButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const float d = std::min (b.getWidth(), b.getHeight()) * 0.72f;
    if (d < 6.0f) return;
    const auto c = b.getCentre();
    const auto disk = juce::Rectangle<float> (d, d).withCentre (c);
    const float u = d / 24.0f;

    juce::Path ring;
    ring.addEllipse (disk);
    if (ripple > 0.0f)
    {
        const float e = 1.0f - ripple * ripple;   // part vite, ralentit en s'élargissant
        g.setColour (ui::ember.withAlpha (0.55f * ripple));
        g.drawEllipse (disk.expanded (d * 0.18f * e), std::max (1.0f, 1.6f * u * ripple));
    }
    ui::strokeGlow (g, ring, ui::ember, 1.4f * u, on * (highlighted ? 1.0f : 0.7f));
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (disk);
    g.setColour (ui::ember.withAlpha (0.16f * on));
    g.fillEllipse (disk);
    g.setColour (ui::line.interpolatedWith (ui::ember, on).withMultipliedAlpha (highlighted ? 1.0f : 0.9f));
    g.drawEllipse (disk, std::max (1.0f, 1.4f * u));

    // pictogramme power : arc ouvert en haut + barre verticale, extrémités arrondies
    const float r = 0.26f * d, stroke = std::max (1.3f, 0.09f * d);
    juce::Path p;
    p.addCentredArc (c.x, c.y + 0.02f * d, r, r, 0.0f, 0.23f * kPi, 1.77f * kPi, true);
    p.startNewSubPath (c.x, c.y - 0.32f * d);
    p.lineTo (c.x, c.y - 0.04f * d);
    g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (on, highlighted ? 0.5f : 0.0f)));
    g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
void PillToggle::tick()
{
    const bool st = getToggleState();
    if (st && ! was) ripple = 1.0f;
    was = st;
    bool changed = ui::approach (on, st ? 1.0f : 0.0f, 0.16f);
    if (ripple > 0.0f) { ripple = std::max (0.0f, ripple - 0.05f); changed = true; }
    if (changed) repaint();
}

void PillToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const bool big = (style == Style::big);
    const float u = big ? std::min (b.getWidth() / 220.0f, b.getHeight() / 100.0f) : b.getHeight() / 36.0f;
    const auto r = b.reduced (big ? 10.0f * u : (style == Style::mini ? 2.0f * u : 6.0f * u));
    const float radius = big ? 18.0f * u : r.getHeight() * 0.5f;

    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    ui::strokeGlow (g, shape, ui::ember, 1.5f * u, on * 0.8f);
    g.setColour (juce::Colours::black.withAlpha (highlighted ? 0.28f : 0.36f));
    g.fillPath (shape);
    if (ripple > 0.0f)
    {
        // allumé : un reflet traverse la pastille de gauche à droite
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (shape);
        const float x = r.getX() + (1.0f - ripple) * (r.getWidth() + r.getHeight()) - r.getHeight();
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), x, 0.0f, juce::Colours::white.withAlpha (0.0f), x + r.getHeight() * 1.6f, 0.0f, false);
        sheen.addColour (0.5, juce::Colours::white.withAlpha (0.22f * std::sqrt (ripple)));
        g.setGradientFill (sheen);
        g.fillRect (juce::Rectangle<float> (x, r.getY(), r.getHeight() * 1.6f, r.getHeight()));
    }
    if (on > 0.001f)
    {
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.30f * on), r.getX(), r.getY(),
                                                 ui::blood.withAlpha (0.25f * on), r.getRight(), r.getBottom(), false));
        g.fillPath (shape);
    }
    g.setColour (ui::line.interpolatedWith (ui::ember, on));
    g.strokePath (shape, juce::PathStrokeType (std::max (1.0f, 1.4f * u)));

    if (big)
    {
        const bool hasCaption = caption.isNotEmpty() && on > 0.5f;
        auto top = r.reduced (12.0f * u, 8.0f * u);
        auto capArea = hasCaption ? top.removeFromBottom (top.getHeight() * 0.28f) : juce::Rectangle<float>();

        const juce::String main = getToggleState() ? "ON" : "OFF";
        g.setColour (ui::haze.interpolatedWith (ui::bone, on));
        g.setFont (ui::fitFont (ui::Face::display, top.getHeight() * 0.56f, top.getWidth(), main, true, 0.12f));
        g.drawText (main, top, juce::Justification::centred, false);

        if (hasCaption)
        {
            g.setColour (ui::mist.withAlpha (0.8f));
            g.setFont (ui::fitFont (ui::Face::mono, std::max (8.0f, 15.0f * u), capArea.getWidth(), caption));
            g.drawText (caption, capArea, juce::Justification::centred, false);
        }
    }
    else
    {
        const float d = r.getHeight() * 0.26f;
        const float pad = r.getHeight() * 0.40f;
        const auto ledR = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + pad + d * 0.5f, r.getCentreY() });
        g.setColour (ui::ember.withAlpha (0.35f * on));
        g.fillEllipse (ledR.expanded (d * 0.8f));
        g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, on));
        g.fillEllipse (ledR);

        const auto textR = r.withTrimmedLeft (pad * 2.0f + d).withTrimmedRight (pad * 0.8f);
        g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (on, highlighted ? 0.6f : 0.0f)));
        g.setFont (ui::fitFont (ui::Face::body, r.getHeight() * (style == Style::mini ? 0.52f : 0.40f), textR.getWidth(), getButtonText(), true, 0.10f));
        g.drawText (getButtonText(), textR, juce::Justification::centred, false);
    }
}



//==============================================================================
SpectrumAnalyzer::SpectrumAnalyzer (OniriqueEQAudioProcessor& p, bool fullScreen) : bubble (p.apvts, p.peqListenHold), processor (p), full (fullScreen)
{
    addChildComponent (bubble);
    bubble.onRemoved = [this] { if (onBackgroundClick) onBackgroundClick(); };
    bubble.xOfHz = [this] (float hz) { return xOf (hz); };
    bubble.hzOfX = [this] (float x) { return hzOf (x); };
    window.resize ((size_t) kN);
    double sum = 0.0;
    for (int i = 0; i < kN; ++i)
    {
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (6.283185307179586 * i / kN));
        sum += window[(size_t) i];
    }
    windowGain = (float) (2.0 / sum);          // un sinus d'amplitude A lit 20 log10 A (dBFS)
    histIn.assign ((size_t) kN, 0.0f);
    histOut.assign ((size_t) kN, 0.0f);
    scratch.assign ((size_t) onirique::SpectrumTap::kSize, 0.0f);
    fftBuf.assign ((size_t) (2 * kN), 0.0f);
    specIn.fill (-140.0f);
    specOut.fill (-140.0f);
    for (auto* c : { &curve, &maxCurve, &selCurve }) c->fill (0.0f);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setOpaque (full);
}

SpectrumAnalyzer::~SpectrumAnalyzer() = default;

// bulle de la bande sélectionnée : au-dessus de son point (dessous s'il n'y a pas la place), gardée dans l'analyseur
void SpectrumAnalyzer::placeBubble()
{
    const auto bd = band (selected);
    const bool show = bd.on && ! bubbleClosed;
    if (show != bubble.isVisible()) bubble.setVisible (show);
    if (! show) return;
    bubble.setBand (selected);
    const float u = unit();
    const float w = 168.0f * u, bh = 70.0f * u;
    const auto c = nodePos (selected);
    const auto area = getLocalBounds().toFloat();
    const bool down = c.y - 14.0f * u - bh >= area.getY() + 2.0f;   // la bulle pointe vers le bas (elle est au-dessus du point)
    bubble.setPointsDown (down);
    auto r = juce::Rectangle<float> (w, bh).withCentre ({ c.x, down ? c.y - 14.0f * u - bh * 0.5f : c.y + 14.0f * u + bh * 0.5f });
    r.setX (juce::jlimit (area.getX() + 2.0f, area.getRight() - w - 2.0f, r.getX()));
    if (r.toNearestInt() != bubble.getBounds()) bubble.setBounds (r.toNearestInt());
}

// chaque point de l'écran : moyenne gaussienne de 1/12 d'octave des bins voisins (voir SpectrumSmoother.h)
void SpectrumAnalyzer::buildMap (double fs)
{
    mapFs = fs;
    smoother.build (fs, kN, kPts);
}

bool SpectrumAnalyzer::analyse (onirique::SpectrumTap& tap, std::vector<float>& hist, int& writePos, std::array<float, kPts>& spec)
{
    bool moved = false;
    // récupère tout ce qui est arrivé depuis la dernière image, garde les kN derniers échantillons
    const int got = tap.pull (scratch.data(), (int) scratch.size());
    for (int i = std::max (0, got - kN); i < got; ++i)
    {
        hist[(size_t) writePos] = scratch[(size_t) i];
        writePos = (writePos + 1) & (kN - 1);
    }

    for (int i = 0; i < kN; ++i)
        fftBuf[(size_t) i] = hist[(size_t) ((writePos + i) & (kN - 1))] * window[(size_t) i];
    std::fill (fftBuf.begin() + kN, fftBuf.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data(), true);
    for (int k = 0; k <= kN / 2; ++k)
    {
        const float a = fftBuf[(size_t) k] * windowGain;
        fftBuf[(size_t) k] = a * a;                // puissance
    }

    std::array<float, kPts> level;
    smoother.apply (fftBuf.data(), level);
    for (int i = 0; i < kPts; ++i)
    {
        const float db = level[(size_t) i];
        auto& s = spec[(size_t) i];
        const float before = s;
        s += (db > s ? 0.70f : 0.13f) * (db - s);  // montée vive, retombée douce (~250 ms), à 30 analyses par seconde
        moved |= std::abs (s - before) > 0.05f && std::max (s, before) > -105.0f;   // sous -105 dB : hors de l'affichage
    }
    return moved;
}

void SpectrumAnalyzer::tick()
{
    if (! active || ! isShowing()) return;
    const double fs = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    if (std::abs (fs - mapFs) > 0.5) buildMap (fs);

    flushDrag();
    bubble.tick();
    placeBubble();
    // spectres : 30 analyses par seconde ; rien n'est redessiné tant que ni le spectre ni l'EQ ne bougent (silence,
    // lecture arrêtée). Pendant un réglage, la courbe et les points restent à 60 images par seconde.
    bool specChanged = false;
    if ((++tickCount & 1) == 0)
    {
        specChanged |= analyse (processor.inputTap, histIn, writeIn, specIn);
        specChanged |= analyse (processor.outputTap, histOut, writeOut, specOut);
    }
    bool eqChanged = updateCurves (fs);

    for (int b = 0; b < kPeq; ++b)
        eqChanged |= ui::approach (nodeGlow[b], (b == selected ? 0.6f : 0.0f) + (b == hoverNode || b == dragNode || b == qFlashBand ? 0.4f : 0.0f), 0.2f);
    if (qFlashFrames > 0)
    {
        eqChanged = true;
        if (--qFlashFrames == 0) qFlashBand = -1;
    }
    if (eqChanged) eqDirty = true;
    if (eqChanged || specChanged) repaint();
}

// courbes : valeurs animées vers les réglages, puis réponse exacte des filtres (recalculée seulement si quelque chose bouge)
bool SpectrumAnalyzer::updateCurves (double fs)
{
    using namespace onirique;
    const bool byp = param (ids::bypass) > 0.5f;
    bool moved = false;
    moved |= ui::approach (eqAmt, param (ids::peqPower) > 0.5f && ! byp ? 1.0f : 0.0f, 0.2f);

    std::array<ParametricEQ::Band, kPeq> bd;
    anyDyn = false;
    for (int b = 0; b < kPeq; ++b)
    {
        bd[(size_t) b] = band (b);
        const auto& x = bd[(size_t) b];
        const bool dyn = x.on && x.dyn && ParametricEQ::canBeDynamic (x.type);
        anyDyn |= dyn;
        moved |= ui::approach (liveGain[b], dyn ? processor.getPeqBandGainDb (b) : x.gainDb, dyn ? 0.35f : 1.0f);
    }

    // signature des réglages : rien à recalculer tant qu'elle ne change pas
    std::array<float, kPeq * 8 + 2> sig {};
    for (int b = 0; b < kPeq; ++b)
    {
        const auto& x = bd[(size_t) b];
        const float s[8] { x.on ? 1.0f : 0.0f, (float) x.type, x.freq, x.gainDb, x.q, (float) x.slope, x.dyn ? 1.0f : 0.0f, liveGain[b] };
        std::copy (s, s + 8, sig.begin() + b * 8);
    }
    sig[(size_t) kPeq * 8] = (float) selected;
    sig[(size_t) kPeq * 8 + 1] = (float) fs + 1.0e6f * (float) processor.getPhaseMode();
    if (! moved && sig == lastSig) return false;
    lastSig = sig;

    // coefficients une fois par bande (gain en cours et, pour les bandes dynamiques, gain réglé)
    BiquadCoeffs live[kPeq][ParametricEQ::kMaxStages], maxC[kPeq][ParametricEQ::kMaxStages];
    int ns[kPeq] {};
    for (int b = 0; b < kPeq; ++b)
    {
        const auto& x = bd[(size_t) b];
        if (! x.on) continue;
        ns[b] = ParametricEQ::numStages (x.type, x.slope);
        for (int k = 0; k < ns[b]; ++k)
        {
            live[b][k] = ParametricEQ::designStage (fs, x.type, x.freq, liveGain[b], x.q, x.slope, k);
            maxC[b][k] = ParametricEQ::designStage (fs, x.type, x.freq, x.gainDb, x.q, x.slope, k);
        }
    }
    const auto& sb = bd[(size_t) selected];
    const bool fir = processor.getPhaseMode() != 0;
    for (int i = 0; i < kPts; ++i)
    {
        const double f = std::min ((double) hzAt (i), 0.49 * fs);
        std::complex<double> hl (1.0, 0.0), hm (1.0, 0.0), hs (1.0, 0.0);
        for (int b = 0; b < kPeq; ++b)
        {
            // NATURAL / LINEAR PHASE : les bandes statiques passent par le FIR, dessiné d'après la courbe analogique exacte
            if (fir && ns[b] > 0 && ! (bd[(size_t) b].dyn && ParametricEQ::canBeDynamic (bd[(size_t) b].type)))
            {
                const auto ra = EqAnalog::bandResponse (bd[(size_t) b], f, bd[(size_t) b].gainDb);
                hl *= ra; hm *= ra;
                if (b == selected) hs *= ra;
                continue;
            }
            for (int k = 0; k < ns[b]; ++k)
            {
                const auto rl = live[b][k].response (fs, f);
                hl *= rl;
                hm *= anyDyn ? maxC[b][k].response (fs, f) : rl;
                if (b == selected) hs *= maxC[b][k].response (fs, f);
            }
        }
        auto db = [] (std::complex<double> z) { return (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (z)))); };
        curve[(size_t) i] = eqAmt * db (hl);
        maxCurve[(size_t) i] = eqAmt * db (hm);
        selCurve[(size_t) i] = sb.on ? db (hs) : 0.0f;
    }
    return true;
}

juce::String SpectrumAnalyzer::getTooltip()
{
    if (hoverNode >= 0)
        return "Band " + juce::String (hoverNode + 1) + ": drag = frequency / gain (Shift = fine), mouse wheel = Q (up = narrower), "
               "double-click = on/off, right-click = type, slope and DYNAMIC mode.";
    return "Parametric EQ + live analyzer: double-click an empty spot to add a band there, drag the numbered dots to shape it, "
           "right-click a dot for its menu. Grey = input, white = output, red = EQ curve.";
}

//------------------------------------------------------------------------------
float SpectrumAnalyzer::unit() const noexcept
{
    return ui::editorScale (*this) * (full ? 1.3f : 1.1f);
}

juce::Rectangle<float> SpectrumAnalyzer::plotArea() const noexcept
{
    const auto area = getLocalBounds().toFloat();
    const float u = unit();
    return area.reduced (12.0f * u, 10.0f * u).withTrimmedBottom (14.0f * u).withTrimmedRight (40.0f * u).withTrimmedTop (16.0f * u);
}

float SpectrumAnalyzer::xOf (float hz) const noexcept
{
    const auto p = plotArea();
    return p.getX() + std::log (hz / 20.0f) / std::log (1000.0f) * p.getWidth();
}

float SpectrumAnalyzer::hzOf (float x) const noexcept
{
    const auto p = plotArea();
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - p.getX()) / p.getWidth()));
}

float SpectrumAnalyzer::yEq (float db) const noexcept
{
    const auto p = plotArea();
    return p.getCentreY() - juce::jlimit (-kEqRange, kEqRange, db) / kEqRange * p.getHeight() * 0.5f;
}

float SpectrumAnalyzer::dbOf (float y) const noexcept
{
    const auto p = plotArea();
    return juce::jlimit (-18.0f, 18.0f, (p.getCentreY() - y) / (p.getHeight() * 0.5f) * kEqRange);
}

float SpectrumAnalyzer::ySpec (float db) const noexcept
{
    const auto p = plotArea();
    return p.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 90.0f) * p.getHeight();
}

onirique::ParametricEQ::Band SpectrumAnalyzer::band (int b) const
{
    using namespace onirique;
    ParametricEQ::Band bd;
    bd.on = param (ids::peqOn[b]) > 0.5f;
    bd.type = juce::roundToInt (param (ids::peqType[b]));
    bd.freq = param (ids::peqFreq[b]);
    bd.gainDb = param (ids::peqGain[b]);
    bd.q = param (ids::peqQ[b]);
    bd.slope = juce::roundToInt (param (ids::peqSlope[b]));
    bd.dyn = param (ids::peqDyn[b]) > 0.5f;
    bd.thrDb = param (ids::peqThr[b]);
    return bd;
}

juce::Point<float> SpectrumAnalyzer::nodePos (int b) const
{
    const auto bd = band (b);
    return { xOf (bd.freq), yEq (onirique::ParametricEQ::hasGain (bd.type) ? bd.gainDb : 0.0f) };
}

int SpectrumAnalyzer::nodeAt (juce::Point<float> p) const
{
    const float r = 11.0f * unit();
    int best = -1;
    float bestD = r;
    for (int b = 0; b < kPeq; ++b)
        if (const float d = nodePos (b).getDistanceFrom (p); d < bestD) { bestD = d; best = b; }
    return best;
}

void SpectrumAnalyzer::setParam (const char* id, float v, bool gesture)
{
    if (auto* p = processor.apvts.getParameter (id))
    {
        if (gesture) p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        if (gesture) p->endChangeGesture();
    }
}

void SpectrumAnalyzer::select (int b)
{
    selected = juce::jlimit (0, kPeq - 1, b);
    bubbleClosed = false;   // une bande choisie : sa bulle revient
    eqDirty = true;
    if (onSelect) onSelect (selected);
}

void SpectrumAnalyzer::mouseMove (const juce::MouseEvent& e)
{
    hoverNode = nodeAt (e.position);
    setMouseCursor (hoverNode >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
}

void SpectrumAnalyzer::mouseExit (const juce::MouseEvent&)
{
    if (dragNode < 0) hoverNode = -1;
}

void SpectrumAnalyzer::mouseDown (const juce::MouseEvent& e)
{
    using namespace onirique;
    const int n = nodeAt (e.position);
    if (n < 0) { closeBubble(); if (onBackgroundClick) onBackgroundClick(); return; }
    select (n);
    if (e.mods.isPopupMenu()) { showBandMenu (*this, processor.apvts, n, e.getScreenPosition()); return; }
    dragNode = n;
    processor.peqListenHold.store (true);    // casque armé : on entend cette bande tant qu'on la tient
    dragStart = e.position;
    dragF0 = band (n).freq;
    dragG0 = band (n).gainDb;
    for (auto* id : { ids::peqFreq[n], ids::peqGain[n] })
        if (auto* p = processor.apvts.getParameter (id)) p->beginChangeGesture();
}

void SpectrumAnalyzer::mouseDrag (const juce::MouseEvent& e)
{
    using namespace onirique;
    if (dragNode < 0) return;
    if (band (dragNode).on == false) setParam (ids::peqOn[dragNode], 1.0f);   // tirer une bande coupée l'allume
    // Maj : réglage fin (le point avance 4 fois moins vite que la souris)
    const float k = e.mods.isShiftDown() ? 0.25f : 1.0f;
    const auto p = plotArea();
    const float oct = (e.position.x - dragStart.x) * k / p.getWidth() * std::log2 (1000.0f);
    pendingF = juce::jlimit (20.0f, 20000.0f, dragF0 * std::pow (2.0f, oct));
    const float dg = -(e.position.y - dragStart.y) * k / (p.getHeight() * 0.5f) * kEqRange;
    pendingG = juce::jlimit (-18.0f, 18.0f, std::round ((dragG0 + dg) * 10.0f) / 10.0f);
    pending = true;
}

void SpectrumAnalyzer::flushDrag()
{
    using namespace onirique;
    if (! pending || dragNode < 0) return;
    pending = false;
    setParam (ids::peqFreq[dragNode], pendingF, false);
    if (ParametricEQ::hasGain (band (dragNode).type)) setParam (ids::peqGain[dragNode], pendingG, false);
}

void SpectrumAnalyzer::mouseUp (const juce::MouseEvent&)
{
    using namespace onirique;
    if (dragNode < 0) return;
    flushDrag();   // la dernière position, avant de clore le geste
    for (auto* id : { ids::peqFreq[dragNode], ids::peqGain[dragNode] })
        if (auto* p = processor.apvts.getParameter (id)) p->endChangeGesture();
    dragNode = -1;
    processor.peqListenHold.store (false);   // bande relâchée : on réentend toute la voix
}

void SpectrumAnalyzer::mouseDoubleClick (const juce::MouseEvent& e)
{
    using namespace onirique;
    if (const int n = nodeAt (e.position); n >= 0)
    {
        setParam (ids::peqOn[n], band (n).on ? 0.0f : 1.0f);   // double-clic sur un point : allume / coupe la bande
        select (n);
        return;
    }
    // double-clic dans le vide : la première bande libre devient une cloche à cet endroit
    for (int b = 0; b < kPeq; ++b)
        if (! band (b).on)
        {
            setParam (ids::peqType[b], (float) ParametricEQ::bell);
            setParam (ids::peqFreq[b], juce::jlimit (20.0f, 20000.0f, hzOf (e.position.x)));
            setParam (ids::peqGain[b], std::round (dbOf (e.position.y) * 10.0f) / 10.0f);
            setParam (ids::peqQ[b], 1.0f);
            setParam (ids::peqDyn[b], 0.0f);
            setParam (ids::peqOn[b], 1.0f);
            select (b);
            return;
        }
}

void SpectrumAnalyzer::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    using namespace onirique;
    const float delta = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (delta == 0.0f) return;

    // bande visée : le point sous la souris, sinon la bande active la plus proche en fréquence
    // (jusqu'à 3/4 d'octave autour de son centre), sinon la bande sélectionnée
    int n = nodeAt (e.position);
    if (n < 0)
    {
        const float hz = hzOf (e.position.x);
        float best = 0.75f;
        for (int b = 0; b < kPeq; ++b)
        {
            const auto bd = band (b);
            const float d = std::abs (std::log2 (hz / bd.freq));
            if (bd.on && d < best) { best = d; n = b; }
        }
    }
    if (n < 0) n = selected;

    // pas proportionnel au défilement (molette crantée ou trackpad) : environ x1.3 par cran, vers le haut = plus étroit
    const float q = band (n).q * std::pow (2.0f, delta * (e.mods.isShiftDown() ? 0.4f : 1.6f));
    setParam (ids::peqQ[n], juce::jlimit (0.1f, 10.0f, q));
    select (n);
    qFlashBand = n;
    qFlashFrames = 80;   // l'étiquette "Q" reste ~1.3 s après le dernier cran
}

void SpectrumAnalyzer::showBandMenu (juce::Component& target, juce::AudioProcessorValueTreeState& apvts, int b, juce::Point<int> screenPos)
{
    using namespace onirique;
    static const char* names[] { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" };
    static const char* slopes[] { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
    const int cur = juce::roundToInt (apvts.getRawParameterValue (ids::peqType[b])->load());
    const int slope = juce::roundToInt (apvts.getRawParameterValue (ids::peqSlope[b])->load());
    const bool on = apvts.getRawParameterValue (ids::peqOn[b])->load() > 0.5f;
    const bool dyn = apvts.getRawParameterValue (ids::peqDyn[b])->load() > 0.5f;
    juce::PopupMenu menu;
    menu.setLookAndFeel (&target.getLookAndFeel());
    menu.addSectionHeader ("BAND " + juce::String (b + 1));
    for (int t = 0; t < ParametricEQ::numTypes; ++t) menu.addItem (t + 1, names[t], true, t == cur);
    {
        juce::PopupMenu sm;
        for (int s = 0; s < 4; ++s) sm.addItem (50 + s, slopes[s], true, s == slope);
        menu.addSubMenu ("Slope", sm, ParametricEQ::isCut (cur));
    }
    menu.addSeparator();
    menu.addItem (102, ParametricEQ::canBeDynamic (cur) ? "Dynamic (acts only above its threshold)" : "Dynamic (bell and shelves only)",
                  ParametricEQ::canBeDynamic (cur), dyn && ParametricEQ::canBeDynamic (cur));
    menu.addSeparator();
    menu.addItem (100, on ? "Switch band off" : "Switch band on");
    menu.addItem (101, "Reset band");

    auto* ap = &apvts;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1))
                            .withParentComponent (target.findParentComponentOfClass<juce::AudioProcessorEditor>()),
                        [ap, b] (int r)
                        {
                            auto set = [ap] (const char* id, float v)
                            {
                                if (auto* p = ap->getParameter (id))
                                { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
                            };
                            auto get = [ap] (const char* id) { return ap->getRawParameterValue (id)->load(); };
                            if (r >= 1 && r <= ParametricEQ::numTypes) { set (ids::peqType[b], (float) (r - 1)); set (ids::peqOn[b], 1.0f); }
                            else if (r >= 50 && r < 54) set (ids::peqSlope[b], (float) (r - 50));
                            else if (r == 102)
                            {
                                const bool nowDyn = get (ids::peqDyn[b]) < 0.5f;
                                set (ids::peqDyn[b], nowDyn ? 1.0f : 0.0f);
                                // passage en dynamique d'une bande sans gain : une coupe de départ utile
                                if (nowDyn && std::abs (get (ids::peqGain[b])) < 0.5f) set (ids::peqGain[b], -4.0f);
                                set (ids::peqOn[b], 1.0f);
                            }
                            else if (r == 100) set (ids::peqOn[b], get (ids::peqOn[b]) > 0.5f ? 0.0f : 1.0f);
                            else if (r == 101)
                                for (auto* id : { ids::peqType[b], ids::peqFreq[b], ids::peqGain[b], ids::peqQ[b], ids::peqSlope[b],
                                                  ids::peqDyn[b], ids::peqThr[b], ids::peqOn[b] })
                                    if (auto* p = ap->getParameter (id))
                                    { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
                        });
}

//------------------------------------------------------------------------------
void PeqBandChip::tick()
{
    using namespace onirique;
    static const char* names[] { "BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT", "NOTCH" };
    const bool on = apvts.getRawParameterValue (ids::peqOn[band])->load() > 0.5f;
    const int type = juce::jlimit (0, 5, juce::roundToInt (apvts.getRawParameterValue (ids::peqType[band])->load()));
    juce::String t = names[type];
    if (ParametricEQ::isCut (type)) t << " " << 12 * (1 + juce::roundToInt (apvts.getRawParameterValue (ids::peqSlope[band])->load()));
    if (ParametricEQ::canBeDynamic (type) && apvts.getRawParameterValue (ids::peqDyn[band])->load() > 0.5f) t << " DYN";
    bool changed = t != shownType;
    shownType = t;
    changed |= ui::approach (lit, on ? 1.0f : 0.0f, 0.2f);
    if (changed) repaint();
}

void PeqBandChip::mouseDown (const juce::MouseEvent& e)
{
    using namespace onirique;
    if (onClick) onClick();
    if (e.position.x < (float) getHeight() * 1.1f)   // LED : allume / coupe
    {
        if (auto* p = apvts.getParameter (ids::peqOn[band]))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
        }
    }
    else
        SpectrumAnalyzer::showBandMenu (*this, apvts, band, e.getScreenPosition());
}

void PeqBandChip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 24.0f;
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillRoundedRectangle (r, h * 0.5f);
    if (highlighted)
    {
        g.setColour (ui::ember.withAlpha (0.16f));
        g.fillRoundedRectangle (r, h * 0.5f);
    }
    g.setColour (ui::line.interpolatedWith (ui::ember, juce::jmax (0.7f * lit, highlighted ? 0.9f : 0.0f)));
    g.drawRoundedRectangle (r.reduced (0.5f), h * 0.5f, std::max (1.0f, u));

    const float d = h * 0.32f;
    const auto led = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + h * 0.55f, r.getCentreY() });
    g.setColour (ui::ember.withAlpha (0.4f * lit));
    g.fillEllipse (led.expanded (d * 0.7f));
    g.setColour (ui::haze.withAlpha (0.4f).interpolatedWith (ui::ember, lit));
    g.fillEllipse (led);

    auto t = r.withTrimmedLeft (h * 1.0f).withTrimmedRight (h * 0.6f);
    const juce::String num = juce::String (band + 1);
    const auto f = ui::font (ui::Face::mono, h * 0.54f, true, 0.04f);
    const float nw = ui::textWidth (f, num) + 7.0f * u;
    g.setColour (ui::mist);
    g.setFont (f);
    g.drawText (num, t.removeFromLeft (nw), juce::Justification::centredLeft, false);
    g.setColour (ui::haze.interpolatedWith (ui::bone, lit));
    g.setFont (ui::fitFont (ui::Face::body, h * 0.50f, t.getWidth(), shownType, true, 0.05f));
    g.drawText (shownType, t, juce::Justification::centredLeft, false);

    const float tx = r.getRight() - h * 0.45f, ty = r.getCentreY();
    juce::Path tri;
    tri.addTriangle (tx - 3.5f * u, ty - 2.0f * u, tx + 3.5f * u, ty - 2.0f * u, tx, ty + 2.5f * u);
    g.setColour (ui::haze);
    g.fillPath (tri);
}

// courbe lisse (Catmull-Rom) à travers les points de l'écran
// step > 1 : un point sur step (le halo flou n'a pas besoin du détail, et chaque segment coûte à chaque trait)
juce::Path SpectrumAnalyzer::smoothPath (const std::array<float, kPts>& ys, bool closeToBottom, float bottom, int step) const
{
    std::array<juce::Point<float>, kPts> pt;
    int n = 0;
    for (int i = 0; i < kPts; i += step) pt[(size_t) n++] = { xOf (hzAt (i)), ys[(size_t) i] };
    if (pt[(size_t) n - 1].x < xOf (hzAt (kPts - 1))) pt[(size_t) n++] = { xOf (hzAt (kPts - 1)), ys[(size_t) kPts - 1] };
    juce::Path p;
    p.preallocateSpace (n * 7 + 8);
    if (closeToBottom) { p.startNewSubPath (pt[0].x, bottom); p.lineTo (pt[0]); }
    else p.startNewSubPath (pt[0]);
    for (int i = 0; i < n - 1; ++i)
    {
        const auto& p0 = pt[(size_t) std::max (0, i - 1)];
        const auto& p1 = pt[(size_t) i];
        const auto& p2 = pt[(size_t) i + 1];
        const auto& p3 = pt[(size_t) std::min (n - 1, i + 2)];
        p.cubicTo (p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
    }
    if (closeToBottom) { p.lineTo (pt[(size_t) n - 1].x, bottom); p.closeSubPath(); }
    return p;
}

// fond + grille (calque refait seulement au redimensionnement)
void SpectrumAnalyzer::paintGrid (juce::Graphics& g)
{
    using namespace onirique;
    const auto area = getLocalBounds().toFloat();
    const float u = unit(), es = ui::editorScale (*this);
    if (full)
    {
        // opaque : le dégradé de la vue plein écran (EqFullView::paint), recalé sur la position de l'analyseur
        const float parentH = getParentComponent() != nullptr ? (float) getParentHeight() : area.getHeight();
        g.setGradientFill (juce::ColourGradient (ui::deep2, 0.0f, (float) -getY(), ui::night, 0.0f, parentH - (float) getY(), false));
        g.fillAll();
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, area.getY(),
                                             ui::night.withAlpha (0.88f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, 12.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (area.reduced (0.5f), 12.0f * u, std::max (1.0f, u));

    const auto plot = plotArea();

    // ---- grille : décades nettes, divisions fines, dB tous les 6 (tous les 3 en plein écran)
    const float labelH = std::max (10.0f * u, 10.0f * es);
    g.setFont (ui::font (ui::Face::mono, labelH));
    for (float dec : { 10.0f, 100.0f, 1000.0f, 10000.0f })
        for (int k = 1; k <= 9; ++k)
        {
            const float hz = dec * (float) k;
            if (hz < 20.0f || hz > 20000.0f) continue;
            const float x = xOf (hz);
            const bool major = k == 1;
            g.setColour (ui::line.withMultipliedAlpha (major ? 0.9f : 0.32f));
            g.fillRect (juce::Rectangle<float> (x, plot.getY() - (major ? 6.0f * u : 0.0f), 1.0f, plot.getHeight() + (major ? 6.0f * u : 0.0f)));
            const bool label = major || k == 2 || k == 5 || (full && (k == 3 || k == 7));
            if (label)
            {
                const juce::String txt = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
                g.setColour (ui::haze.withAlpha (major ? 0.9f : 0.65f));
                g.drawText (txt, juce::Rectangle<float> (x + 3.0f * u, plot.getBottom() + 2.0f * u, 40.0f * u, 12.0f * u),
                            juce::Justification::centredLeft, false);
            }
        }
    for (int dbi = -18; dbi <= 18; dbi += full ? 3 : 6)
    {
        const float db = (float) dbi, y = yEq (db);
        g.setColour (dbi == 0 ? ui::line.withMultipliedAlpha (1.8f) : ui::line.withMultipliedAlpha (dbi % 6 == 0 ? 0.5f : 0.25f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), y, plot.getWidth(), 1.0f));
        if (dbi % 6 == 0)
        {
            g.setColour (ui::haze.withAlpha (0.6f));
            g.drawText ((db > 0.0f ? "+" : "") + juce::String (dbi) + " dB",
                        juce::Rectangle<float> (plot.getRight() + 4.0f * u, y - 6.0f * u, 40.0f * u, 12.0f * u), juce::Justification::centredLeft, false);
        }
    }

}

// courbes d'EQ, points, étiquettes, légende (calque refait seulement quand un réglage ou un survol change)
void SpectrumAnalyzer::paintEq (juce::Graphics& g)
{
    using namespace onirique;
    const auto area = getLocalBounds().toFloat();
    const float u = unit(), es = ui::editorScale (*this);
    const auto plot = plotArea();
    const float labelH = std::max (10.0f * u, 10.0f * es);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());

        // ---- bandes dynamiques : courbe au gain réglé (pointillés braise) ; la courbe pleine suit le gain en direct
        std::array<float, kPts> yEqC;
        for (int i = 0; i < kPts; ++i) yEqC[(size_t) i] = yEq (curve[(size_t) i]);
        if (anyDyn)
        {
            std::array<float, kPts> ym;
            for (int i = 0; i < kPts; ++i) ym[(size_t) i] = yEq (maxCurve[(size_t) i]);
            juce::Path dashed;
            const float dash[] { 3.0f * u, 3.0f * u };
            juce::PathStrokeType (std::max (1.0f, 1.2f * u)).createDashedStroke (dashed, smoothPath (ym, false, 0.0f), dash, 2);
            g.setColour (ui::ember.withAlpha (0.55f * eqAmt));
            g.fillPath (dashed);
        }

        // ---- courbe de l'EQ paramétrique : remplissage jusqu'à 0 dB + trait braise lumineux
        const auto eq = smoothPath (yEqC, false, 0.0f);
        juce::Path fill (eq);
        fill.lineTo (plot.getRight(), yEq (0.0f));
        fill.lineTo (plot.getX(), yEq (0.0f));
        fill.closeSubPath();
        g.setColour (ui::ember.withAlpha (0.12f * (0.3f + 0.7f * eqAmt)));
        g.fillPath (fill);
        ui::strokeGlow (g, smoothPath (yEqC, false, 0.0f, 3), ui::ember, 2.5f * u, 0.3f + 0.7f * eqAmt);
        g.setColour (ui::ember.withAlpha (0.45f + 0.55f * eqAmt));
        g.strokePath (eq, juce::PathStrokeType (std::max (1.5f, 2.4f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // ---- zone de la bande sélectionnée
        if (band (selected).on)
        {
            std::array<float, kPts> ys;
            for (int i = 0; i < kPts; ++i) ys[(size_t) i] = yEq (selCurve[(size_t) i]);
            juce::Path p = smoothPath (ys, false, 0.0f);
            p.lineTo (plot.getRight(), yEq (0.0f));
            p.lineTo (plot.getX(), yEq (0.0f));
            p.closeSubPath();
            g.setColour (ui::bone.withAlpha (0.10f * eqAmt));
            g.fillPath (p);
        }
    }

    // ---- points numérotés (glissables) ; bande dynamique : rail jusqu'au gain réglé + point qui suit le gain en direct
    for (int b = 0; b < kPeq; ++b)
    {
        const auto bd = band (b);
        const auto c = nodePos (b);
        const float gl = nodeGlow[b];
        const float r = (7.0f + 2.0f * gl) * u * (full ? 1.1f : 1.0f);
        const bool dyn = bd.on && bd.dyn && ParametricEQ::canBeDynamic (bd.type);
        if (dyn)
        {
            const float y0 = yEq (0.0f);
            g.setColour (ui::ember.withAlpha (0.35f));
            g.fillRoundedRectangle (juce::Rectangle<float> (c.x - 1.5f * u, std::min (y0, c.y), 3.0f * u, std::abs (c.y - y0)), 1.5f * u);
            const float yl = yEq (liveGain[b]);
            const float dd = 5.0f * u;
            g.setColour (ui::bone);
            g.fillEllipse (juce::Rectangle<float> (dd, dd).withCentre ({ c.x, yl }));
        }
        const auto dot = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c);
        juce::Path ring;
        ring.addEllipse (dot);
        if (bd.on)
        {
            ui::strokeGlow (g, ring, ui::ember, 1.4f * u, (0.4f + 0.6f * gl) * eqAmt);
            g.setColour (b == selected ? ui::ember : ui::night);
            g.fillPath (ring);
            g.setColour (b == selected ? ui::bone : ui::ember);
            g.strokePath (ring, juce::PathStrokeType (std::max (1.2f, 1.8f * u)));
            if (dyn)
            {
                const float dash[] { 2.0f * u, 2.0f * u };
                juce::Path halo;
                juce::PathStrokeType (std::max (1.0f, 1.0f * u)).createDashedStroke (halo, [&] { juce::Path e; e.addEllipse (dot.expanded (3.5f * u)); return e; }(), dash, 2);
                g.setColour (ui::ember.withAlpha (0.8f));
                g.fillPath (halo);
            }
            g.setColour (b == selected ? ui::night : ui::bone);
        }
        else
        {
            // bande coupée : point creux discret, prêt à être tiré ou double-cliqué
            g.setColour (ui::night.withAlpha (0.7f));
            g.fillPath (ring);
            g.setColour (ui::haze.withAlpha (0.35f + 0.4f * gl));
            g.strokePath (ring, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));
            g.setColour (ui::haze.withAlpha (0.5f + 0.4f * gl));
        }
        g.setFont (ui::font (ui::Face::mono, r * 1.15f, true));
        g.drawText (juce::String (b + 1), dot, juce::Justification::centred, false);
    }

    // ---- étiquette de la bande (Q à la molette ; en plein écran : fréquence / gain / Q de la bande sélectionnée)
    auto label = [&] (int b, const juce::String& txt, float a)
    {
        const auto c = nodePos (b);
        const auto f = ui::font (ui::Face::mono, std::max (8.0f, 11.0f * u), true);
        const float tw = ui::textWidth (f, txt) + 14.0f * u;
        auto box = juce::Rectangle<float> (tw, 18.0f * u).withCentre ({ c.x, c.y - 22.0f * u });
        if (box.getY() < area.getY() + 2.0f) box.setCentre (c.x, c.y + 22.0f * u);
        box = box.withX (juce::jlimit (area.getX() + 2.0f, area.getRight() - tw - 2.0f, box.getX()));
        g.setColour (ui::night.withAlpha (0.92f * a));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (ui::ember.withAlpha (a));
        g.drawRoundedRectangle (box, box.getHeight() * 0.5f, std::max (1.0f, u));
        g.setColour (ui::bone.withAlpha (a));
        g.setFont (f);
        g.drawText (txt, box, juce::Justification::centred, false);
    };
    // la bande sélectionnée a sa bulle : l'étiquette ne sert qu'aux autres bandes réglées à la molette
    if (qFlashBand >= 0 && ! (qFlashBand == selected && band (selected).on))
        label (qFlashBand, "Q " + juce::String (band (qFlashBand).q, 2), juce::jlimit (0.0f, 1.0f, (float) qFlashFrames / 20.0f));

    // ---- légende
    {
        float x = plot.getX();
        const float y = area.getY() + 7.0f * u;
        g.setFont (ui::font (ui::Face::mono, labelH));
        for (auto [c, txt] : { std::pair { ui::ember, "EQ" }, { ui::bone, "OUTPUT" }, { ui::haze, "INPUT" } })
        {
            g.setColour (c);
            g.fillRoundedRectangle (juce::Rectangle<float> (x, y + 4.5f * u, 14.0f * u, 3.0f * u), 1.5f * u);
            g.setColour (ui::haze);
            const float tw = ui::textWidth (g.getCurrentFont(), txt) + 4.0f * u;
            g.drawText (txt, juce::Rectangle<float> (x + 20.0f * u, y, tw, 12.0f * u), juce::Justification::centredLeft, false);
            x += 20.0f * u + tw + 16.0f * u;
        }
        if (anyDyn)
        {
            g.setColour (ui::ember.withAlpha (0.8f));
            g.drawText ("DYNAMIC: dotted = full cut, solid = live", juce::Rectangle<float> (x, y, 300.0f * u, 12.0f * u),
                        juce::Justification::centredLeft, false);
        }
    }
}

// chaque image : grille (calque) + spectres (seule partie redessinée à chaque fois) + courbes d'EQ (calque)
void SpectrumAnalyzer::paint (juce::Graphics& g)
{
    const float ps = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int w = juce::jmax (1, juce::roundToInt ((float) getWidth() * ps)), hh = juce::jmax (1, juce::roundToInt ((float) getHeight() * ps));
    auto render = [&] (juce::Image& img, void (SpectrumAnalyzer::*fn) (juce::Graphics&))
    {
        // même taille : le calque est vidé et réutilisé (en réallouer un de plusieurs Mo à chaque image faisait ramer le glisser)
        if (img.isValid() && img.getWidth() == w && img.getHeight() == hh) img.clear (img.getBounds());
        else img = juce::Image (full && fn == &SpectrumAnalyzer::paintGrid ? juce::Image::RGB : juce::Image::ARGB, w, hh, true);   // plein écran : la grille est opaque, plus rapide à recopier
        juce::Graphics lg (img);
        lg.addTransform (juce::AffineTransform::scale (ps));
        (this->*fn) (lg);
    };
    if (! gridLayer.isValid() || gridLayer.getWidth() != w || gridLayer.getHeight() != hh)
    {
        render (gridLayer, &SpectrumAnalyzer::paintGrid);
        eqDirty = true;
    }
   #if JUCE_WINDOWS
    // Windows : JUCE dessine par la carte graphique (Direct2D) ; dessiner dans une image puis la relire force une
    // synchronisation qui coûtait plus de 15 ms par image en grand. Les courbes sont donc dessinées directement.
    constexpr bool eqInLayer = false;
   #else
    constexpr bool eqInLayer = true;    // Mac : rendu par le processeur, le calque évite de tout redessiner à chaque image
   #endif
    if (eqInLayer && (eqDirty || ! eqLayer.isValid()))
    {
        render (eqLayer, &SpectrumAnalyzer::paintEq);
        eqDirty = false;
    }
    const auto toLogical = juce::AffineTransform::scale (1.0f / ps);
    g.setOpacity (1.0f);
    g.drawImageTransformed (gridLayer, toLogical);

    {
        // ---- spectres : entrée (trait discret), sortie (dégradé os + trait net)
        const auto plot = plotArea();
        const float u = unit();
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());
        std::array<float, kPts> yIn, yOut;
        for (int i = 0; i < kPts; ++i) { yIn[(size_t) i] = ySpec (specIn[(size_t) i]); yOut[(size_t) i] = ySpec (specOut[(size_t) i]); }
        // entrée : un simple trait discret, sans surface ; seule la sortie est remplie (deux surfaces l'une sur l'autre
        // brouillaient la lecture dès qu'une voix passait)
        g.setColour (ui::haze.withAlpha (0.30f));
        g.strokePath (smoothPath (yIn, false, 0.0f), juce::PathStrokeType (std::max (1.0f, 1.0f * u), juce::PathStrokeType::curved));

        juce::ColourGradient og (ui::bone.withAlpha (0.34f), 0.0f, plot.getY(), ui::bone.withAlpha (0.02f), 0.0f, plot.getBottom(), false);
        og.addColour (0.55, ui::bone.withAlpha (0.12f));
        g.setGradientFill (og);
        g.fillPath (smoothPath (yOut, true, plot.getBottom()));
        g.setColour (ui::bone.withAlpha (0.85f));
        g.strokePath (smoothPath (yOut, false, 0.0f), juce::PathStrokeType (std::max (1.0f, 1.3f * u), juce::PathStrokeType::curved));
    }

    g.setOpacity (1.0f);
    if (eqInLayer) g.drawImageTransformed (eqLayer, toLogical);
    else { paintEq (g); eqDirty = false; }
}



//==============================================================================
namespace
{
juce::String hzText (float f)
{
    if (f >= 1000.0f) return juce::String (f / 1000.0f, f >= 10000.0f ? 2 : 3) + " kHz";
    return juce::String (f, f < 100.0f ? 2 : 1) + " Hz";
}

// pictogrammes fins (bulle et panneau), dans un carré r
void iconPower (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float w)
{
    juce::Path p;
    const auto ctr = r.getCentre();
    const float rad = r.getWidth() * 0.36f;
    p.addCentredArc (ctr.x, ctr.y, rad, rad, 0.0f, 0.75f, 2.0f * juce::MathConstants<float>::pi - 0.75f, true);
    p.startNewSubPath (ctr.x, ctr.y - rad * 1.25f);
    p.lineTo (ctr.x, ctr.y - rad * 0.15f);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void iconHeadphones (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float w)
{
    juce::Path p;
    const auto ctr = r.getCentre();
    const float rad = r.getWidth() * 0.38f;
    p.addCentredArc (ctr.x, ctr.y + rad * 0.15f, rad, rad, 0.0f, -1.75f, 1.75f, true);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float cw = rad * 0.42f, ch = rad * 0.75f;
    g.fillRoundedRectangle (ctr.x - rad - cw * 0.35f, ctr.y + rad * 0.05f, cw, ch, cw * 0.3f);
    g.fillRoundedRectangle (ctr.x + rad - cw * 0.65f, ctr.y + rad * 0.05f, cw, ch, cw * 0.3f);
}

void iconCross (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float w)
{
    const auto q = r.reduced (r.getWidth() * 0.3f);
    juce::Path p;
    p.startNewSubPath (q.getTopLeft()); p.lineTo (q.getBottomRight());
    p.startNewSubPath (q.getTopRight()); p.lineTo (q.getBottomLeft());
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// petite courbe du type de bande (coupe-bas, cloche...)
void iconType (juce::Graphics& g, juce::Rectangle<float> r, int type, juce::Colour c, float w)
{
    const auto q = r.reduced (r.getWidth() * 0.12f, r.getHeight() * 0.25f);
    const float x0 = q.getX(), x1 = q.getRight(), yt = q.getY(), yb = q.getBottom(), ym = q.getCentreY(), xm = q.getCentreX();
    juce::Path p;
    switch (type)
    {
        case ParametricEQ::lowCut:    p.startNewSubPath (x0, yb); p.quadraticTo (xm - q.getWidth() * 0.1f, yt, x1, yt); break;
        case ParametricEQ::highCut:   p.startNewSubPath (x0, yt); p.quadraticTo (xm + q.getWidth() * 0.1f, yt, x1, yb); break;
        case ParametricEQ::lowShelf:  p.startNewSubPath (x0, yt); p.cubicTo (xm, yt, xm, yb, x1, yb); break;
        case ParametricEQ::highShelf: p.startNewSubPath (x0, yb); p.cubicTo (xm, yb, xm, yt, x1, yt); break;
        case ParametricEQ::notch:     p.startNewSubPath (x0, yt); p.lineTo (xm - 2.0f * w, yt); p.lineTo (xm, yb); p.lineTo (xm + 2.0f * w, yt); p.lineTo (x1, yt); break;
        default:                      p.startNewSubPath (x0, yb); p.cubicTo (xm - q.getWidth() * 0.15f, yb, xm - q.getWidth() * 0.12f, yt, xm, yt);
                                      p.cubicTo (xm + q.getWidth() * 0.12f, yt, xm + q.getWidth() * 0.15f, yb, x1, yb); break;
    }
    juce::ignoreUnused (ym);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void triangle (juce::Graphics& g, juce::Rectangle<float> r, bool up, juce::Colour c)
{
    juce::Path p;
    if (up) p.addTriangle (r.getX(), r.getBottom(), r.getRight(), r.getBottom(), r.getCentreX(), r.getY());
    else    p.addTriangle (r.getX(), r.getY(), r.getRight(), r.getY(), r.getCentreX(), r.getBottom());
    g.setColour (c);
    g.fillPath (p);
}

const char* const kTypeNames[] { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" };
const char* const kSlopeNames[] { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
} // namespace

//==============================================================================
BandBubble::BandBubble (juce::AudioProcessorValueTreeState& s, std::atomic<bool>& h) : apvts (s), hold (h)
{
    setBufferedToImage (true);   // redessinée seulement quand une valeur change (l'analyseur dessous bouge à chaque image)
    setRepaintsOnMouseActivity (false);
}

void BandBubble::set (const char* id, float v)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

void BandBubble::resized()
{
    const auto r = getLocalBounds().toFloat();
    const float u = r.getHeight() / 70.0f, tip = 7.0f * u;
    box = pointsDown ? r.withTrimmedBottom (tip) : r.withTrimmedTop (tip);
    box = box.reduced (1.0f);
    const float rowH = (box.getHeight() - 8.0f * u) / 3.0f, ic = rowH * 0.95f;
    const auto row = [&] (int i) { return juce::Rectangle<float> (box.getX(), box.getY() + 4.0f * u + rowH * (float) i, box.getWidth(), rowH); };
    zPower  = row (0).withWidth (ic + 8.0f * u).withTrimmedLeft (4.0f * u);
    zClose  = row (0).withLeft (box.getRight() - ic - 8.0f * u).withTrimmedRight (4.0f * u);
    zListen = zClose.translated (-zClose.getWidth() - 2.0f * u, 0.0f);
    const auto r3 = row (2);
    zUp   = r3.withLeft (box.getRight() - ic - 8.0f * u).withTrimmedRight (4.0f * u).withTrimmedBottom (rowH * 0.5f);
    zDown = zUp.translated (0.0f, rowH * 0.5f);
    zType = r3.withRight (zUp.getX());
}

BandBubble::Zone BandBubble::zoneAt (juce::Point<float> p) const noexcept
{
    const bool cut = ParametricEQ::isCut (juce::roundToInt (get (ids::peqType[band])));
    if (zPower.contains (p)) return power;
    if (zListen.contains (p)) return listen;
    if (zClose.contains (p)) return close;
    if (cut && zUp.contains (p)) return slopeUp;
    if (cut && zDown.contains (p)) return slopeDown;
    if (zType.contains (p)) return typeMenu;
    return none;
}

void BandBubble::tick()
{
    const int type = juce::roundToInt (get (ids::peqType[band]));
    juce::String t;
    t << get (ids::peqOn[band]) << "|" << type << "|" << get (ids::peqFreq[band]) << "|" << get (ids::peqGain[band]) << "|"
      << get (ids::peqQ[band]) << "|" << get (ids::peqSlope[band]) << "|" << (listening ? 1 : 0);
    if (t != shownText) { shownText = t; repaint(); }
}

void BandBubble::paint (juce::Graphics& g)
{
    const float u = (float) getHeight() / 70.0f;
    const int type = juce::jlimit (0, 5, juce::roundToInt (get (ids::peqType[band])));
    const bool on = get (ids::peqOn[band]) > 0.5f, cut = ParametricEQ::isCut (type);

    // bulle + pointe vers le point de la bande
    juce::Path shape;
    shape.addRoundedRectangle (box, 6.0f * u);
    const float tx = box.getCentreX(), tw = 6.0f * u;
    juce::Path tipP;
    if (pointsDown) tipP.addTriangle (tx - tw, box.getBottom() - 1.0f, tx + tw, box.getBottom() - 1.0f, tx, (float) getHeight() - 1.0f);
    else            tipP.addTriangle (tx - tw, box.getY() + 1.0f, tx + tw, box.getY() + 1.0f, tx, 1.0f);
    shape.addPath (tipP);
    g.setColour (ui::night.withAlpha (0.93f));
    g.fillPath (shape);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.45f));
    g.strokePath (shape, juce::PathStrokeType (std::max (1.0f, u)));

    const float sw = std::max (1.2f, 1.5f * u);
    auto hl = [&] (Zone z) { return hover == (int) z ? ui::bone : ui::haze; };
    iconPower (g, zPower, on ? ui::ember : hl (power), sw);
    iconHeadphones (g, zListen, listening ? ui::ember : hl (listen), sw);
    iconCross (g, zClose, hl (close), sw);

    const auto f = ui::font (ui::Face::mono, 12.0f * u, true);
    g.setFont (f);
    const auto textX = zPower.getRight() + 2.0f * u;
    const float rowH = (box.getHeight() - 8.0f * u) / 3.0f;
    auto row = [&] (int i) { return juce::Rectangle<float> (textX, box.getY() + 4.0f * u + rowH * (float) i, zListen.getX() - textX, rowH); };

    g.setColour (on ? ui::bone : ui::haze);
    g.drawText (hzText (get (ids::peqFreq[band])), row (0), juce::Justification::centredLeft, false);

    juce::String l2;
    if (ParametricEQ::hasGain (type))
    {
        const float gd = get (ids::peqGain[band]);
        l2 << (gd > 0.0f ? "+" : "") << juce::String (gd, 1) << " dB   ";
    }
    l2 << "Q: " << juce::String (get (ids::peqQ[band]), 3);
    g.setColour (ui::mist.withAlpha (on ? 0.95f : 0.6f));
    g.drawText (l2, row (1).withRight (box.getRight() - 6.0f * u), juce::Justification::centredLeft, false);

    // 3e ligne : type (menu) + pente des coupes (flèches)
    const auto r3 = zType;
    iconType (g, r3.withWidth (r3.getHeight() * 1.3f).withX (box.getX() + 5.0f * u), type, hover == typeMenu ? ui::bone : ui::ember, sw);
    const juce::String t3 = cut ? juce::String (kSlopeNames[juce::jlimit (0, 3, juce::roundToInt (get (ids::peqSlope[band])))]) : juce::String (kTypeNames[type]);
    g.setColour (hover == typeMenu ? ui::bone : ui::mist);
    g.drawText (t3, r3.withTrimmedLeft (r3.getHeight() * 1.3f + 8.0f * u), juce::Justification::centredLeft, false);
    if (cut)
    {
        triangle (g, zUp.withSizeKeepingCentre (zUp.getHeight() * 0.9f, zUp.getHeight() * 0.5f), true, hl (slopeUp));
        triangle (g, zDown.withSizeKeepingCentre (zDown.getHeight() * 0.9f, zDown.getHeight() * 0.5f), false, hl (slopeDown));
    }
    else
        triangle (g, juce::Rectangle<float> (zUp.getX(), r3.getY(), zUp.getWidth(), r3.getHeight()).withSizeKeepingCentre (7.0f * u, 4.0f * u), false, hl (typeMenu));
}

void BandBubble::mouseMove (const juce::MouseEvent& e)
{
    const int z = (int) zoneAt (e.position);
    if (z != hover) { hover = z; repaint(); }
    setMouseCursor (z >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void BandBubble::mouseDown (const juce::MouseEvent& e)
{
    switch (zoneAt (e.position))
    {
        case power:  set (ids::peqOn[band], get (ids::peqOn[band]) > 0.5f ? 0.0f : 1.0f); break;
        case listen:
            // casque : on entend la bande seule tant qu'on tient le bouton
            listenWas = get (ids::peqListen) > 0.5f;
            set (ids::peqListenBand, (float) band);
            set (ids::peqListen, 1.0f);
            hold.store (true);
            listening = true;
            repaint();
            break;
        case close:
            for (auto* id : { ids::peqType[band], ids::peqFreq[band], ids::peqGain[band], ids::peqQ[band], ids::peqSlope[band],
                              ids::peqDyn[band], ids::peqThr[band], ids::peqOn[band] })
                if (auto* p = apvts.getParameter (id))
                { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
            set (ids::peqOn[band], 0.0f);
            if (onRemoved) onRemoved();
            break;
        case slopeUp:   set (ids::peqSlope[band], (float) juce::jmin (3, juce::roundToInt (get (ids::peqSlope[band])) + 1)); break;
        case slopeDown: set (ids::peqSlope[band], (float) juce::jmax (0, juce::roundToInt (get (ids::peqSlope[band])) - 1)); break;
        case typeMenu:  SpectrumAnalyzer::showBandMenu (*this, apvts, band, e.getScreenPosition()); break;
        case none: default: break;
    }
}

void BandBubble::mouseDrag (const juce::MouseEvent& e)
{
    // casque tenu et souris déplacée : la bande suit la souris en fréquence, on l'entend seule pendant tout le geste
    if (! listening || xOfHz == nullptr || hzOfX == nullptr) return;
    auto* p = apvts.getParameter (ids::peqFreq[band]);
    if (p == nullptr) return;
    if (! sweeping)
    {
        if (e.getDistanceFromDragStart() < 3) return;
        sweeping = true;
        sweepStartX = xOfHz (get (ids::peqFreq[band]));
        p->beginChangeGesture();
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    }
    const float hz = hzOfX (sweepStartX + (float) e.getDistanceFromDragStartX());
    p->setValueNotifyingHost (p->convertTo0to1 (hz));
}

void BandBubble::mouseUp (const juce::MouseEvent&)
{
    if (! listening) return;
    if (sweeping)
    {
        if (auto* p = apvts.getParameter (ids::peqFreq[band])) p->endChangeGesture();
        sweeping = false;
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    hold.store (false);
    set (ids::peqListen, listenWas ? 1.0f : 0.0f);
    listening = false;
    repaint();
}

//==============================================================================
BandPanel::BandPanel (juce::AudioProcessorValueTreeState& s) : apvts (s)
{
    using Layout = KnobComponent::Layout;
    for (int b = 0; b < 8; ++b)
    {
        auto& x = *(sets[(size_t) b] = std::make_unique<Set>());
        const juce::String n = "Band " + juce::String (b + 1);
        x.freq = std::make_unique<KnobComponent> (apvts, ids::peqFreq[b], "FREQ", "20", "20k", Layout::textBelow,
                                                  n + " frequency. Double-click = reset.", lnf);
        x.gain = std::make_unique<KnobComponent> (apvts, ids::peqGain[b], "GAIN", "-18", "+18", Layout::textBelow,
                                                  n + " gain (bell and shelves). Double-click = 0 dB.", lnf, true);
        x.q    = std::make_unique<KnobComponent> (apvts, ids::peqQ[b], "Q", "0.1", "10", Layout::textBelow,
                                                  n + " Q: width of a bell, resonance of a cut. Double-click = reset.", lnf);
        x.thr  = std::make_unique<DragValue> (apvts, ids::peqThr[b], n + " dynamic threshold: the band acts only when its zone goes above it.");
        x.thr->setPrefix ("THR ");
        x.power.setTooltip (n + " on/off.");
        x.powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, ids::peqOn[b], x.power);
        x.dyn.setTooltip (n + " dynamic mode: the bell / shelf only acts when its zone goes above the threshold.");
        x.dynAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, ids::peqDyn[b], x.dyn);
        for (juce::Component* c : std::initializer_list<juce::Component*> { x.freq.get(), x.gain.get(), x.q.get(), x.thr.get(), &x.power, &x.dyn })
            addChildComponent (c);
    }
    setBufferedToImage (true);   // l'analyseur dessous bouge à chaque image : le panneau (ombre comprise) reste une image prête
    setInterceptsMouseClicks (true, true);
    setAlpha (0.0f);
    setVisible (false);
    setBand (2);
}

BandPanel::~BandPanel()
{
    for (auto& s : sets) { s->freq.reset(); s->gain.reset(); s->q.reset(); }   // avant leur LookAndFeel
}

void BandPanel::setBand (int b)
{
    band = juce::jlimit (0, 7, b);
    for (int i = 0; i < 8; ++i)
    {
        auto& x = *sets[(size_t) i];
        const bool v = i == band;
        for (juce::Component* c : std::initializer_list<juce::Component*> { x.freq.get(), x.gain.get(), x.q.get(), x.thr.get(), &x.power, &x.dyn })
            c->setVisible (v);
    }
    shownType = shownSlope = -1;
    repaint();
}

void BandPanel::setOpen (bool shouldOpen)
{
    open = shouldOpen;
    if (open) setVisible (true);
}

void BandPanel::resized()
{
    const auto r = getLocalBounds().toFloat();
    const float u = r.getHeight() / 200.0f;
    const float hump = 18.0f * u, m = 12.0f * u;   // marge : l'ombre portée reste dans le composant
    body = r.reduced (m).withTrimmedTop (hump);
    const float w = body.getWidth();
    // colonnes : gauche (marche, type, pente) | FREQ | GAIN (bosse) | Q | droite (bandes, fermer, DYN, seuil)
    const float sideW = w * 0.20f, knobW = (w - 2.0f * sideW) / 3.2f, gainW = knobW * 1.2f;
    auto left = body.withWidth (sideW).reduced (10.0f * u, 10.0f * u);
    auto right = body.withLeft (body.getRight() - sideW).reduced (10.0f * u, 10.0f * u);
    const float kx = body.getX() + sideW;
    const auto freqR = juce::Rectangle<float> (kx, body.getY() + 6.0f * u, knobW, body.getHeight() - 12.0f * u);
    const auto gainR = juce::Rectangle<float> (kx + knobW, r.getY() + m + 2.0f * u, gainW, body.getBottom() - r.getY() - m - 8.0f * u);
    const auto qR    = juce::Rectangle<float> (kx + knobW + gainW, freqR.getY(), knobW, freqR.getHeight());
    gainCentre = gainR;

    // forme : rectangle arrondi + bosse douce au-dessus du GAIN
    shape.clear();
    const float rad = 16.0f * u, hx0 = gainR.getX() - 26.0f * u, hx1 = gainR.getRight() + 26.0f * u, top = body.getY(), ht = r.getY() + m;
    shape.startNewSubPath (body.getX() + rad, top);
    shape.lineTo (hx0, top);
    shape.cubicTo (hx0 + 30.0f * u, top, hx0 + 22.0f * u, ht, hx0 + 56.0f * u, ht);
    shape.lineTo (hx1 - 56.0f * u, ht);
    shape.cubicTo (hx1 - 22.0f * u, ht, hx1 - 30.0f * u, top, hx1, top);
    shape.lineTo (body.getRight() - rad, top);
    shape.quadraticTo (body.getRight(), top, body.getRight(), top + rad);
    shape.lineTo (body.getRight(), body.getBottom() - rad);
    shape.quadraticTo (body.getRight(), body.getBottom(), body.getRight() - rad, body.getBottom());
    shape.lineTo (body.getX() + rad, body.getBottom());
    shape.quadraticTo (body.getX(), body.getBottom(), body.getX(), body.getBottom() - rad);
    shape.lineTo (body.getX(), top + rad);
    shape.quadraticTo (body.getX(), top, body.getX() + rad, top);
    shape.closeSubPath();

    const float rowH = 30.0f * u;
    zType = left.withTrimmedTop (rowH + 12.0f * u).withHeight (rowH);
    zSlope = zType.translated (0.0f, rowH + 6.0f * u);
    // de droite à gauche : x, puis < n > (jamais l'un sur l'autre)
    zClose = right.withHeight (rowH).withLeft (right.getRight() - rowH);
    zNext = zClose.translated (-rowH - 10.0f * u, 0.0f).withWidth (rowH * 0.8f).translated (rowH * 0.2f, 0.0f);
    zPrev = zNext.translated (-rowH * 1.7f, 0.0f);
    for (auto& s : sets)
    {
        auto& x = *s;
        x.power.setBounds (left.withHeight (rowH).withWidth (rowH).toNearestInt());
        x.freq->setBounds (freqR.toNearestInt());
        x.gain->setBounds (gainR.toNearestInt());
        x.q->setBounds (qR.toNearestInt());
        auto rr = right.withTrimmedTop (rowH + 12.0f * u);
        x.dyn.setBounds (rr.removeFromTop (rowH * 0.85f).toNearestInt());
        rr.removeFromTop (8.0f * u);
        x.thr->setBounds (rr.removeFromTop (rowH * 0.85f).toNearestInt());
    }
}

BandPanel::Zone BandPanel::zoneAt (juce::Point<float> p) const noexcept
{
    if (zPrev.contains (p)) return prev;
    if (zNext.contains (p)) return next;
    if (zClose.contains (p)) return close;
    if (zSlope.contains (p) && ParametricEQ::isCut (shownType)) return slopeCycle;
    if (zType.contains (p)) return typeMenu;
    return none;
}

void BandPanel::tick()
{
    // apparition / disparition : fondu + glissement
    const float target = open ? 1.0f : 0.0f;
    if (ui::approach (shown, target, 0.22f))
    {
        setAlpha (shown);
        setTransform (juce::AffineTransform::translation (0.0f, (1.0f - shown) * 14.0f * (float) getHeight() / 200.0f));
        if (! open && shown <= 0.0f) setVisible (false);
    }
    if (! isVisible()) return;
    auto& x = *sets[(size_t) band];
    x.freq->tick(); x.gain->tick(); x.q->tick(); x.thr->tick(); x.power.tick(); x.dyn.tick();
    const int type = juce::roundToInt (get (ids::peqType[band])), slope = juce::roundToInt (get (ids::peqSlope[band]));
    const bool can = ParametricEQ::canBeDynamic (type), dyn = can && get (ids::peqDyn[band]) > 0.5f;
    x.gain->setAlpha (ParametricEQ::hasGain (type) ? 1.0f : 0.3f);
    x.gain->setEnabled (ParametricEQ::hasGain (type));
    x.dyn.setEnabled (can);
    x.dyn.setAlpha (can ? 1.0f : 0.35f);
    x.thr->setAlpha (dyn ? 1.0f : 0.35f);
    if (type != shownType || slope != shownSlope) { shownType = type; shownSlope = slope; repaint(); }
}

void BandPanel::paint (juce::Graphics& g)
{
    const float u = (float) getHeight() / 200.0f;
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), juce::roundToInt (22.0f * u), { 0, juce::roundToInt (8.0f * u) }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (ui::deep2.withAlpha (0.97f), 0.0f, 0.0f, ui::night.withAlpha (0.97f), 0.0f, (float) getHeight(), false));
    g.fillPath (shape);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.35f));
    g.strokePath (shape, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));

    const int type = juce::jlimit (0, 5, shownType < 0 ? 0 : shownType);
    const bool cut = ParametricEQ::isCut (type);
    const float sw = std::max (1.2f, 1.6f * u);

    // type de la bande (menu) + pente
    {
        const auto r = zType;
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (hover == typeMenu ? ui::ember : ui::line);
        g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, std::max (1.0f, u));
        iconType (g, r.withWidth (r.getHeight() * 1.2f).translated (8.0f * u, 0.0f), type, ui::ember, sw);
        g.setColour (ui::mist);
        g.setFont (ui::fitFont (ui::Face::body, r.getHeight() * 0.48f, r.getWidth() - r.getHeight() * 1.9f, kTypeNames[type], true));
        g.drawText (kTypeNames[type], r.withTrimmedLeft (r.getHeight() * 1.2f + 12.0f * u), juce::Justification::centredLeft, false);
        if (cut)
        {
            g.setColour (hover == slopeCycle ? ui::bone : ui::haze);
            g.setFont (ui::font (ui::Face::mono, 13.0f * u, true));
            g.drawText (kSlopeNames[juce::jlimit (0, 3, shownSlope)], zSlope.withTrimmedLeft (10.0f * u), juce::Justification::centredLeft, false);
        }
    }

    // < n >   x
    {
        auto hc = [&] (Zone z) { return hover == (int) z ? ui::bone : ui::haze; };
        g.setColour (ui::mist);
        g.setFont (ui::font (ui::Face::mono, 14.0f * u, true));
        g.drawText (juce::String (band + 1), juce::Rectangle<float> (zPrev.getRight(), zPrev.getY(), zNext.getX() - zPrev.getRight(), zPrev.getHeight()),
                    juce::Justification::centred, false);
        iconCross (g, zClose, hc (close), sw);
        // flèches gauche / droite (triangles couchés)
        juce::Path pl, pr;
        const auto a = zPrev.withSizeKeepingCentre (zPrev.getHeight() * 0.25f, zPrev.getHeight() * 0.38f);
        const auto b = zNext.withSizeKeepingCentre (zNext.getHeight() * 0.25f, zNext.getHeight() * 0.38f);
        pl.addTriangle (a.getRight(), a.getY(), a.getRight(), a.getBottom(), a.getX(), a.getCentreY());
        pr.addTriangle (b.getX(), b.getY(), b.getX(), b.getBottom(), b.getRight(), b.getCentreY());
        g.setColour (hc (prev)); g.fillPath (pl);
        g.setColour (hc (next)); g.fillPath (pr);
    }
}

void BandPanel::mouseMove (const juce::MouseEvent& e)
{
    const int z = (int) zoneAt (e.position);
    if (z != hover) { hover = z; repaint(); }
    setMouseCursor (z >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void BandPanel::mouseDown (const juce::MouseEvent& e)
{
    switch (zoneAt (e.position))
    {
        case prev:     if (onBand) onBand ((band + 7) % 8); break;
        case next:     if (onBand) onBand ((band + 1) % 8); break;
        case close:    if (onClose) onClose(); break;
        case typeMenu: SpectrumAnalyzer::showBandMenu (*this, apvts, band, e.getScreenPosition()); break;
        case slopeCycle:   // 12 -> 24 -> 36 -> 48 -> 12 dB/oct
            if (auto* p = apvts.getParameter (ids::peqSlope[band]))
            { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) ((shownSlope + 1) % 4))); p->endChangeGesture(); }
            break;
        case none: default: break;
    }
}

//==============================================================================
void IconButton::tick()
{
    bool changed = ui::approach (glow, isDown() ? 1.0f : (isOver() ? 0.7f : 0.0f), 0.2f);
    changed |= ui::approach (lit, getToggleState() ? 1.0f : 0.0f, 0.2f);
    if (changed || lit > 0.01f) repaint();   // allumé : la pastille respire
}

void IconButton::paintButton (juce::Graphics& g, bool, bool)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float h = b.getHeight(), u = h / 28.0f;
    juce::Path shape;
    shape.addRoundedRectangle (b, h * 0.5f);
    ui::strokeGlow (g, shape, ui::ember, 1.2f * u, 0.6f * glow);
    g.setColour (juce::Colours::black.withAlpha (0.38f));
    g.fillPath (shape);
    g.setColour (ui::ember.withAlpha (0.14f * glow));
    g.fillPath (shape);
    if (lit > 0.001f)
    {
        // allumé (écoute) : pastille braise qui respire doucement
        const float breathe = 0.85f + 0.15f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.004f);
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.75f * lit * breathe), b.getX(), b.getY(),
                                                 ui::blood.withAlpha (0.8f * lit), b.getRight(), b.getBottom(), false));
        g.fillPath (shape);
    }
    g.setColour (ui::line.interpolatedWith (ui::ember, juce::jmax (lit, 0.25f + 0.75f * glow)));
    g.strokePath (shape, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));

    const float s = h * 0.42f;
    const float cx = text.isEmpty() ? b.getCentreX() : b.getX() + h * 0.62f, cy = b.getCentreY();
    if (icon == Icon::headphones)
    {
        // casque : arceau + deux écouteurs arrondis
        juce::Path hp;
        const float r = s * 0.62f;
        hp.addCentredArc (cx, cy + r * 0.15f, r, r, 0.0f, -kPi * 0.62f, kPi * 0.62f, true);
        g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (0.45f, juce::jmax (glow, lit))));
        g.strokePath (hp, juce::PathStrokeType (std::max (1.3f, 1.8f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (float sx : { -1.0f, 1.0f })
        {
            const auto cup = juce::Rectangle<float> (r * 0.5f, r * 0.82f).withCentre ({ cx + sx * r * 0.86f, cy + r * 0.62f });
            g.fillRoundedRectangle (cup, r * 0.2f);
        }
        if (text.isNotEmpty())
        {
            const auto tr = b.withTrimmedLeft (h * 1.15f).withTrimmedRight (h * 0.45f);
            g.setFont (ui::fitFont (ui::Face::body, h * 0.44f, tr.getWidth(), text, true, 0.10f));
            g.drawText (text, tr, juce::Justification::centred, false);
        }
        return;
    }

    // pictogramme : 4 coins vers l'extérieur (plein écran) ou vers l'intérieur (sortie)
    const float k = s * 0.36f;
    juce::Path ic;
    for (int q = 0; q < 4; ++q)
    {
        const float sx = (q & 1) ? 1.0f : -1.0f, sy = (q & 2) ? 1.0f : -1.0f;
        const float x = cx + sx * s * 0.5f, y = cy + sy * s * 0.5f;
        if (icon == Icon::expand)
        {
            ic.startNewSubPath (x - sx * k, y);
            ic.lineTo (x, y);
            ic.lineTo (x, y - sy * k);
        }
        else
        {
            const float ix = x - sx * k, iy = y - sy * k;
            ic.startNewSubPath (ix + sx * k, iy);
            ic.lineTo (ix, iy);
            ic.lineTo (ix, iy + sy * k);
        }
    }
    g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (0.4f, glow)));
    g.strokePath (ic, juce::PathStrokeType (std::max (1.3f, 1.9f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (text.isNotEmpty())
    {
        const auto tr = b.withTrimmedLeft (h * 1.15f).withTrimmedRight (h * 0.45f);
        g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (0.5f, glow)));
        g.setFont (ui::fitFont (ui::Face::body, h * 0.44f, tr.getWidth(), text, true, 0.10f));
        g.drawText (text, tr, juce::Justification::centred, false);
    }
}

//==============================================================================
PhaseModeBox::PhaseModeBox (juce::AudioProcessorValueTreeState& s) : apvts (s), param (s.getParameter (ids::phaseMode))
{
    setTooltip ("Processing mode. Zero Latency: analog-matched IIR filters, no latency. Natural Phase: FIR filters with the exact "
                "analog response (gain AND phase, like an analog EQ), no latency. Linear Phase: FIR filters with no phase shift at all "
                "(adds latency, compensated by your DAW).");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PhaseModeBox::tick()
{
    const int m = param != nullptr ? juce::roundToInt (param->convertFrom0to1 (param->getValue())) : 0;
    bool changed = m != shown;
    shown = m;
    changed |= ui::approach (glow, hovering || open ? 1.0f : 0.0f, 0.2f);
    if (changed) repaint();
}

void PhaseModeBox::mouseDown (const juce::MouseEvent&)
{
    if (param == nullptr) return;
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    const auto& names = phaseModeNames();
    for (int i = 0; i < names.size(); ++i) menu.addItem (i + 1, names[i], true, i == shown);
    open = true;
    juce::Component::SafePointer<PhaseModeBox> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                            .withParentComponent (findParentComponentOfClass<juce::AudioProcessorEditor>()),
                        [safe] (int r)
                        {
                            if (safe == nullptr) return;
                            safe->open = false;
                            if (r < 1) return;
                            auto* p = safe->param;
                            p->beginChangeGesture();
                            p->setValueNotifyingHost (p->convertTo0to1 ((float) (r - 1)));
                            p->endChangeGesture();
                        });
}

void PhaseModeBox::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 30.0f, rad = h * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::ember.withAlpha (0.10f + 0.10f * glow));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.45f + 0.45f * glow));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, std::max (1.0f, 1.2f * u));

    const float d = h * 0.24f;
    const auto led = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + rad, r.getCentreY() });
    g.setColour (ui::ember.withAlpha (0.4f));
    g.fillEllipse (led.expanded (d * 0.6f));
    g.setColour (ui::ember);
    g.fillEllipse (led);

    const juce::String t = phaseModeNames()[juce::jlimit (0, 2, shown)];
    auto tr = r.withTrimmedLeft (rad * 1.7f).withTrimmedRight (h * 0.9f);
    g.setColour (ui::mist);
    g.setFont (ui::fitFont (ui::Face::body, h * 0.46f, tr.getWidth(), t, true, 0.03f));
    g.drawText (t, tr, juce::Justification::centredLeft, false);

    const float tx = r.getRight() - h * 0.55f, ty = r.getCentreY();
    juce::Path chev;
    chev.startNewSubPath (tx - 4.5f * u, ty - 2.0f * u);
    chev.lineTo (tx, ty + 2.5f * u);
    chev.lineTo (tx + 4.5f * u, ty - 2.0f * u);
    g.setColour (ui::haze.interpolatedWith (ui::bone, glow));
    g.strokePath (chev, juce::PathStrokeType (std::max (1.2f, 1.6f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
EqFullView::EqFullView (OniriqueEQAudioProcessor& p) : analyzer (p, true), processor (p), modeBox (p.apvts), panel (p.apvts)
{
    setOpaque (false);   // coins arrondis : le fond du plugin se voit autour de la carte
    addAndMakeVisible (analyzer);
    children.push_back (&analyzer);
    addAndMakeVisible (modeBox);
    children.push_back (&modeBox);
    addAndMakeVisible (power);
    power.setTooltip ("EQ on/off (click-free): compare with the untouched sound, latency kept.");
    powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, ids::peqPower, power);
    addAndMakeVisible (listenButton);
    listenButton.setText ("LISTEN");
    listenButton.setTooltip ("Band listen: when lit, click and hold a band's dot to hear only that band (the frequencies it acts on); release it to hear the whole sound again.");
    listenAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, ids::peqListen, listenButton);
    children.push_back (&listenButton);
    children.push_back (&power);

    // panneau flottant de la bande : s'ouvre quand on choisit une bande, se ferme d'un clic dans le vide ou sur x
    addChildComponent (panel);
    children.push_back (&panel);
    panel.onBand = [this] (int b) { selectBand (b); if (onSelect) onSelect (b); };
    panel.onClose = [this] { panel.setOpen (false); };
    analyzer.onSelect = [this] (int b) { selectBand (b); panel.setOpen (true); if (onSelect) onSelect (b); };
    analyzer.onBackgroundClick = [this] { panel.setOpen (false); };
    selectBand (analyzer.getSelected());
}

EqFullView::~EqFullView() = default;

void EqFullView::selectBand (int b)
{
    selected = juce::jlimit (0, 7, b);
    analyzer.setSelectedSilently (selected);
    panel.setBand (selected);
    // l'écoute (casque) suit la bande sélectionnée
    if (auto* lb = processor.apvts.getParameter (ids::peqListenBand); lb != nullptr && juce::roundToInt (lb->convertFrom0to1 (lb->getValue())) != selected)
        lb->setValueNotifyingHost (lb->convertTo0to1 ((float) selected));
}

// les filtres qui tournent en ce moment (le mode réellement actif, pas seulement celui choisi)
juce::String EqFullView::processingText() const
{
    const juce::String dot (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
    const int mode = processor.getPhaseMode();
    bool anyDyn = false;
    for (int b = 0; b < 8; ++b)
        anyDyn |= processor.apvts.getRawParameterValue (ids::peqOn[b])->load() > 0.5f
               && processor.apvts.getRawParameterValue (ids::peqDyn[b])->load() > 0.5f
               && onirique::ParametricEQ::canBeDynamic (juce::roundToInt (processor.apvts.getRawParameterValue (ids::peqType[b])->load()));
    const double fs = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    if (mode == 0)
        return "IIR" + dot + "ANALOG-MATCHED BIQUADS" + dot + "64-BIT" + dot + "0 LATENCY";
    // avant le premier passage audio de l'hôte : les valeurs que ce mode aura à cette fréquence
    const auto kind = mode == 2 ? onirique::FirEQ::linearPhase : onirique::FirEQ::naturalPhase;
    const int taps = processor.getFirTaps() > 0 ? processor.getFirTaps() : onirique::FirEQ::tapsFor (kind, fs);
    juce::String t = "FIR" + dot + (mode == 2 ? "LINEAR PHASE" : "MINIMUM PHASE") + dot + juce::String (taps) + " TAPS" + dot;
    if (mode == 2)
    {
        const int lat = processor.getFirTaps() > 0 ? processor.getLatencySamples() : onirique::FirEQ::latencyFor (kind, fs);
        t << "LATENCY " << lat << " SAMPLES (" << juce::String (1000.0 * lat / fs, 1) << " ms)";
    }
    else
        t << "0 LATENCY";
    if (anyDyn) t << dot << "DYN BANDS: IIR";
    return t;
}

void EqFullView::tick()
{
    if (! isVisible()) return;
    for (auto* a : children) a->tick();
    if (const auto t = processingText(); t != info) { info = t; repaint (infoArea.getSmallestIntegerContainer().expanded (2)); }
}

void EqFullView::resized()
{
    const float u = ui::editorScale (*this);
    auto r = getLocalBounds().toFloat().reduced (16.0f * u, 12.0f * u);
    titleArea = r.removeFromTop (40.0f * u);
    power.setBounds (juce::Rectangle<float> (32.0f * u, 32.0f * u).withCentre ({ titleArea.getRight() - 16.0f * u, titleArea.getCentreY() }).toNearestInt());
    listenButton.setBounds (juce::Rectangle<float> (126.0f * u, 30.0f * u).withRightX ((float) power.getX() - 10.0f * u).withY (titleArea.getCentreY() - 15.0f * u).toNearestInt());
    modeBox.setBounds (juce::Rectangle<float> (196.0f * u, 30.0f * u).withRightX ((float) listenButton.getX() - 12.0f * u).withY (titleArea.getCentreY() - 15.0f * u).toNearestInt());
    const float infoX = titleArea.getX() + 24.0f * u + ui::textWidth (ui::font (ui::Face::display, 22.0f * u, true, 0.08f), "PARAMETRIC EQ") + 24.0f * u;
    infoArea = { infoX, titleArea.getY(), (float) modeBox.getX() - 16.0f * u - infoX, titleArea.getHeight() };
    r.removeFromTop (8.0f * u);
    analyzer.setBounds (r.toNearestInt());

    // panneau de bande : centré en bas de l'analyseur, au-dessus de l'échelle des fréquences
    const float pw = 760.0f * u, ph = 210.0f * u;
    panel.setBounds (juce::Rectangle<float> (pw, ph).withCentre ({ r.getCentreX() - 20.0f * u, r.getBottom() - 34.0f * u - ph * 0.5f }).toNearestInt());
}

void EqFullView::paint (juce::Graphics& g)
{
    const float u = ui::editorScale (*this);
    const auto r = getLocalBounds().toFloat();
    const float rad = 16.0f * u;
    g.setGradientFill (juce::ColourGradient (ui::deep2, 0.0f, r.getY(), ui::night, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.25f));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, std::max (1.0f, u));

    // titre + filtres utilisés
    const float dot = 7.0f * u;
    const auto dr = juce::Rectangle<float> (dot, dot).withCentre ({ titleArea.getX() + 8.0f * u, titleArea.getCentreY() });
    g.setColour (ui::ember.withAlpha (0.35f));
    g.fillEllipse (dr.expanded (dot * 0.7f));
    g.setColour (ui::ember);
    g.fillEllipse (dr);
    const auto f = ui::font (ui::Face::display, 22.0f * u, true, 0.08f);
    const juce::String t = "PARAMETRIC EQ";
    g.setFont (f);
    g.setColour (ui::mist);
    g.drawText (t, titleArea.withX (titleArea.getX() + 24.0f * u).withWidth (ui::textWidth (f, t) + 10.0f * u), juce::Justification::centredLeft, false);

    // pastille d'info : le type de filtre, sa taille et la latence
    const auto ff = ui::fitFont (ui::Face::mono, 12.5f * u, infoArea.getWidth() - 28.0f * u, info, true, 0.04f);
    const float w = std::min (infoArea.getWidth(), ui::textWidth (ff, info) + 30.0f * u);
    const auto pill = infoArea.withWidth (w).withSizeKeepingCentre (w, 26.0f * u).withX (infoArea.getX());
    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (ui::line);
    g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, std::max (1.0f, u));
    g.setColour (info.startsWith ("FIR") ? ui::bone : ui::haze.interpolatedWith (ui::bone, 0.5f));
    g.setFont (ff);
    g.drawText (info, pill.reduced (14.0f * u, 0.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
PresetBar::PresetBar (onirique::PresetManager& p) : pm (p)
{
    addChildComponent (editor);
    editor.setJustification (juce::Justification::centred);
    editor.setColour (juce::TextEditor::backgroundColourId, ui::night.withAlpha (0.9f));
    editor.setColour (juce::TextEditor::textColourId, ui::mist);
    editor.setColour (juce::TextEditor::outlineColourId, ui::ember);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, ui::ember);
    editor.setColour (juce::TextEditor::highlightColourId, ui::ember.withAlpha (0.4f));
    editor.setColour (juce::CaretComponent::caretColourId, ui::bone);
    editor.setTextToShowWhenEmpty ("Preset name", ui::haze);
    editor.onReturnKey = [this] { endSave (true); };
    editor.onEscapeKey = [this] { endSave (false); };
    editor.onFocusLost = [this] { endSave (false); };
    shownName = pm.getCurrentName();
}

void PresetBar::resized()
{
    const auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    auto rest = r;
    zones[save] = rest.removeFromRight (h * 2.1f);
    rest.removeFromRight (h * 0.2f);
    zones[prev] = rest.removeFromLeft (h);
    zones[next] = rest.removeFromRight (h);
    zones[name] = rest;
    editor.setBounds (zones[name].reduced (2.0f, h * 0.14f).toNearestInt());
    editor.setFont (ui::font (ui::Face::body, h * 0.40f, true));
}

PresetBar::Zone PresetBar::zoneAt (juce::Point<float> p) const noexcept
{
    for (int z = 0; z < 4; ++z) if (zones[z].contains (p)) return (Zone) z;
    return none;
}

void PresetBar::tick()
{
    bool changed = false;
    for (int z = 0; z < 4; ++z) changed |= ui::approach (glow[z], hover == z ? 1.0f : 0.0f, 0.2f);
    if ((++frame % 10) == 0)
    {
        const auto n = pm.getCurrentName();
        const bool mod = pm.isModified();
        if (n != shownName || mod != modified) { shownName = n; modified = mod; changed = true; }
    }
    if (changed) repaint();
}

void PresetBar::mouseMove (const juce::MouseEvent& e)
{
    const auto z = zoneAt (e.position);
    if (z != hover) { hover = z; repaint(); }
}

void PresetBar::mouseExit (const juce::MouseEvent&) { hover = none; }

void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    switch (zoneAt (e.position))
    {
        case prev: pm.step (-1); break;
        case next: pm.step (1); break;
        case name: showMenu(); break;
        case save: startSave(); break;
        default: break;
    }
    shownName = pm.getCurrentName();
    modified = pm.isModified();
    repaint();
}

void PresetBar::showMenu()
{
    const auto entries = pm.list();
    const auto current = pm.getCurrentName();
    juce::PopupMenu menu;
    bool userHeader = false;
    menu.addSectionHeader ("FACTORY");
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (! e.factory && ! userHeader) { menu.addSectionHeader ("USER"); userHeader = true; }
        menu.addItem (i + 1, e.name, true, e.name == current);
    }
    menu.addSeparator();
    menu.addItem (1000, "Save as new preset...");
    bool currentIsUser = false;
    for (const auto& e : entries) if (! e.factory && e.name == current) currentIsUser = true;
    menu.addItem (1001, "Delete \"" + current + "\"", currentIsUser);
    menu.addItem (1002, "Open presets folder");

    menu.setLookAndFeel (&getLookAndFeel());   // sinon JUCE juge la fenêtre opaque avec le style système (coins blancs)
    juce::Component::SafePointer<PresetBar> self (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                            .withParentComponent (findParentComponentOfClass<juce::AudioProcessorEditor>()),
                        [self, entries] (int r)
                        {
                            if (self == nullptr || r == 0) return;
                            auto& bar = *self;
                            if (r >= 1 && r <= (int) entries.size()) bar.pm.load (entries[(size_t) (r - 1)]);
                            else if (r == 1000) bar.startSave();
                            else if (r == 1001)
                            {
                                for (const auto& e : entries)
                                    if (! e.factory && e.name == bar.pm.getCurrentName()) bar.pm.remove (e);
                                bar.pm.load (entries.front());   // retour sur Init
                            }
                            else if (r == 1002) onirique::PresetManager::folder().startAsProcess();
                            bar.repaint();
                        });
}

void PresetBar::startSave()
{
    editor.setText (shownName == "Init" ? juce::String() : shownName, false);
    editor.setVisible (true);
    editor.selectAll();
    editor.grabKeyboardFocus();
    repaint();
}

void PresetBar::endSave (bool commit)
{
    if (! editor.isVisible()) return;
    const auto text = editor.getText();
    editor.setVisible (false);
    if (commit) pm.save (text);
    shownName = pm.getCurrentName();
    modified = pm.isModified();
    repaint();
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 40.0f;

    // conteneur : précédent | nom | suivant
    const auto box = zones[prev].getUnion (zones[next]).reduced (0.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.6f * glow[name]));
    g.drawRoundedRectangle (box.reduced (0.5f), box.getHeight() * 0.5f, std::max (1.0f, 1.2f * u));

    for (int z : { (int) prev, (int) next })
    {
        const auto c = zones[z].getCentre();
        const float d = (z == prev ? -1.0f : 1.0f) * 4.0f * u;
        juce::Path chev;
        chev.startNewSubPath (c.x - d, c.y - 6.0f * u);
        chev.lineTo (c.x + d, c.y);
        chev.lineTo (c.x - d, c.y + 6.0f * u);
        g.setColour (ui::haze.interpolatedWith (ui::bone, glow[z]));
        g.strokePath (chev, juce::PathStrokeType (std::max (1.3f, 2.0f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (! editor.isVisible())
    {
        auto nr = zones[name].reduced (4.0f * u, 0.0f);
        g.setColour (ui::haze.withAlpha (0.8f));
        g.setFont (ui::font (ui::Face::mono, 9.0f * u, false, 0.1f));
        g.drawText ("PRESET", nr.removeFromTop (h * 0.42f).withTrimmedTop (h * 0.10f), juce::Justification::centredBottom, false);
        const juce::String txt = shownName + (modified ? " *" : "");
        g.setColour (ui::mist.interpolatedWith (ui::bone, glow[name]));
        g.setFont (ui::fitFont (ui::Face::body, h * 0.36f, nr.getWidth() - 18.0f * u, txt, true, 0.04f));
        g.drawText (txt, nr.withTrimmedBottom (h * 0.08f), juce::Justification::centredTop, false);
        // petit triangle : la liste s'ouvre au clic
        const float tx = zones[name].getRight() - 8.0f * u, ty = zones[name].getCentreY();
        juce::Path tri;
        tri.addTriangle (tx - 4.0f * u, ty - 2.0f * u, tx + 4.0f * u, ty - 2.0f * u, tx, ty + 3.0f * u);
        g.setColour (ui::haze.interpolatedWith (ui::ember, glow[name]));
        g.fillPath (tri);
    }

    // SAVE
    const auto s = zones[save].reduced (0.0f, 1.0f);
    juce::Path sp;
    sp.addRoundedRectangle (s, s.getHeight() * 0.5f);
    ui::strokeGlow (g, sp, ui::ember, 1.2f * u, glow[save] * 0.8f);
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillPath (sp);
    g.setColour (ui::ember.withAlpha (0.18f * glow[save]));
    g.fillPath (sp);
    g.setColour (ui::line.interpolatedWith (ui::ember, glow[save]));
    g.strokePath (sp, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));
    g.setColour (ui::haze.interpolatedWith (ui::bone, glow[save]));
    g.setFont (ui::fitFont (ui::Face::body, h * 0.36f, s.getWidth() - 10.0f * u, "SAVE", true, 0.16f));
    g.drawText ("SAVE", s, juce::Justification::centred, false);
}


//==============================================================================
void LevelMeter::tick()
{
    float peakNow = -100.0f;
    for (int c = 0; c < 2; ++c)
    {
        const float pk = consume ? consume (c) : 0.0f;
        const float db = pk > 1.0e-5f ? 20.0f * std::log10 (pk) : -100.0f;
        peakNow = std::max (peakNow, db);
        levelDb[c] = db > levelDb[c] ? db : std::max (-100.0f, levelDb[c] - 0.33f);   // retombée ~20 dB/s
        if (db >= holdDb[c]) { holdDb[c] = db; holdFrames[c] = 90; }                // maintien 1.5 s
        else if (--holdFrames[c] <= 0) holdDb[c] = std::max (levelDb[c], holdDb[c] - 0.5f);
    }
    if (peakNow >= maxDb) { maxDb = peakNow; maxFrames = 180; }                      // valeur affichée : 3 s
    else if (--maxFrames <= 0) maxDb = std::max (holdDb[0], holdDb[1]);
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float u = area.getWidth() / 46.0f;
    auto r = area;
    const auto readout = r.removeFromBottom (18.0f * u);
    r.removeFromBottom (4.0f * u);
    const auto scale = r.removeFromRight (18.0f * u);
    const float barW = (r.getWidth() - 3.0f * u) * 0.5f;
    auto yOf = [&] (float db) { return r.getBottom() - norm (db) * r.getHeight(); };

    // graduations
    g.setFont (ui::font (ui::Face::mono, std::max (8.0f * u, 9.0f * ui::editorScale (*this))));
    for (int db : { 0, -6, -12, -24, -48 })
    {
        const float y = yOf ((float) db);
        g.setColour (db == 0 ? ui::ember.withAlpha (0.8f) : ui::haze.withAlpha (0.75f));
        g.fillRect (juce::Rectangle<float> (scale.getX() + 1.0f * u, y, 3.0f * u, 1.0f));
        if (db != -6)   // -6 : simple repère, sinon les chiffres du haut se chevauchent
            g.drawText (juce::String (db), juce::Rectangle<float> (scale.getX() + 4.0f * u, y - 6.0f * u, scale.getWidth() - 4.0f * u, 12.0f * u),
                    juce::Justification::centredLeft, false);
    }

    for (int c = 0; c < 2; ++c)
    {
        const auto bar = juce::Rectangle<float> (r.getX() + (float) c * (barW + 3.0f * u), r.getY(), barW, r.getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (bar, 2.5f * u);
        g.setColour (ui::line);
        g.drawRoundedRectangle (bar.reduced (0.5f), 2.5f * u, 1.0f);

        // remplissage : os -> braise près de 0 dBFS -> rouge au-delà
        const float top = yOf (levelDb[c]);
        if (top < bar.getBottom() - 0.5f)
        {
            juce::ColourGradient cg (ui::bone.withAlpha (0.30f), 0.0f, bar.getBottom(), juce::Colour (0xffff2a3a), 0.0f, yOf (2.0f), false);
            cg.addColour ((double) norm (-12.0f), ui::bone.withAlpha (0.85f));   // positions = niveau normalisé (bas = -60, haut = +2)
            cg.addColour ((double) norm (-3.0f), ui::ember);
            g.setGradientFill (cg);
            g.fillRoundedRectangle (bar.withTop (top).reduced (1.0f, 0.0f), 2.0f * u);
        }
        // maintien de crête
        if (holdDb[c] > -60.0f)
        {
            g.setColour (holdDb[c] > 0.0f ? juce::Colour (0xffff2a3a) : ui::bone);
            g.fillRect (juce::Rectangle<float> (bar.getX() + 1.0f, yOf (holdDb[c]) - 1.0f * u, bar.getWidth() - 2.0f, 2.0f * u));
        }
    }

    // valeur crête (3 s) en clair
    const juce::String txt = maxDb <= -99.0f ? juce::String ("-inf") : (maxDb > 0.0f ? "+" : "") + juce::String (maxDb, 1);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (readout, readout.getHeight() * 0.5f);
    g.setColour (maxDb > 0.0f ? juce::Colour (0xffff2a3a) : ui::line);
    g.drawRoundedRectangle (readout.reduced (0.5f), readout.getHeight() * 0.5f, 1.0f);
    g.setColour (maxDb > 0.0f ? ui::bone : ui::mist);
    g.setFont (ui::fitFont (ui::Face::mono, readout.getHeight() * 0.62f, readout.getWidth() - 6.0f * u, txt, true));
    g.drawText (txt, readout, juce::Justification::centred, false);
}
