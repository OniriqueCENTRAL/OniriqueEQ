#include "PluginEditor.h"
#include "OniriqueWindowSize.h"
#include <BinaryData.h>

using namespace onirique;

namespace
{
constexpr float kBaseW = ui::kEditorBaseW, kBaseH = ui::kEditorBaseH;
constexpr int kMinW = 1100, kMaxW = 3000;
constexpr float kPi = juce::MathConstants<float>::pi;
}

OniriqueEQAudioProcessorEditor::OniriqueEQAudioProcessorEditor (OniriqueEQAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    tooltipWindow.setLookAndFeel (&lnf);
    setLookAndFeel (&lnf);   // menus et champs de texte dans le style du plugin
    setOpaque (true);

    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);

    addAndMakeVisible (eq);
    addAndMakeVisible (presetBar);
    addAndMakeVisible (outputV);
    outputV.setPrefix ("OUT ");
    addAndMakeVisible (bypassButton);
    bypassButton.setTooltip ("Bypass: the untouched sound, aligned with the EQ latency (click-free).");
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, ids::bypass, bypassButton);
    for (auto* a : std::initializer_list<Animated*> { &eq, &presetBar, &outputV, &bypassButton })
        animated.push_back (a);

    // redimensionnable, proportions verrouillées : aucune déformation, aucun texte coupé.
    // La taille mémorisée est lue AVANT tout appel qui redimensionne (setResizeLimits() déclenche un resized()).
    const int w = onirique::window::openingSize (processor.apvts.state, "ONIRIQUE EQ",
                                                 { 1600, juce::roundToInt (1600.0f * kBaseH / kBaseW) },
                                                 { kMinW, juce::roundToInt ((float) kMinW * kBaseH / kBaseW) },
                                                 { kMaxW, juce::roundToInt ((float) kMaxW * kBaseH / kBaseW) }).x;
    setResizable (true, true);
    setResizeLimits (kMinW, juce::roundToInt (kMinW * kBaseH / kBaseW), kMaxW, juce::roundToInt (kMaxW * kBaseH / kBaseW));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) (kBaseW / kBaseH));
    setSize (w, juce::roundToInt ((float) w * kBaseH / kBaseW));
    restoringSize = false;
    onirique::window::remember (processor.apvts.state, getWidth(), getHeight());

    // Licence : pastille et page d'activation, par-dessus tout le reste.
    licenseOverlay = std::make_unique<onirique::license::Overlay> ("ONIRIQUE EQ", processor.license);
    addAndMakeVisible (*licenseOverlay);
    resized();

   #if ONIRIQUE_UI_TEST
    // essais locaux uniquement (jamais dans les versions publiées) : captures et visuel de démo sans la page d'activation
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("nolicense"))
        licenseOverlay->setVisible (false);
    // ONIRIQUE_UI_TEST=nolicense,demo : visuel de la boutique avec une vraie courbe (preset Vocal Clarity)
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("demo"))
        for (const auto& e : processor.presets.list())
            if (e.name == "Vocal Clarity") processor.presets.load (e);
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("panel"))
        eq.showBandPanel (0);
   #endif

    startTimerHz (60);
}

OniriqueEQAudioProcessorEditor::~OniriqueEQAudioProcessorEditor()
{
    stopTimer();
    onirique::window::rememberOnThisComputer ("ONIRIQUE EQ", getWidth(), getHeight());
    tooltipWindow.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void OniriqueEQAudioProcessorEditor::timerCallback()
{
    clock += 1.0 / 60.0;
    ++frame;
    for (auto* a : animated) a->tick();

    // en-tête vivant : lueur du logo, signal qui parcourt la ligne, étoiles qui scintillent
    // (lueur du logo à 30 images/s : elle respire en 5 s ; le signal : seulement la zone de l'impulsion, pas toute la largeur)
    if ((frame & 1) == 0)
        repaint (logoRect.expanded (logoRect.getHeight() * 0.4f).getSmallestIntegerContainer());
    {
        const float u = scale(), W = (float) getWidth(), len = 130.0f * u, span = W - 44.0f * u + len;
        const float px = 22.0f * u - len + std::fmod ((float) clock * 0.24f, 1.0f) * span;
        const auto streak = juce::Rectangle<float> (px, signalRect.getY(), len, signalRect.getHeight()).expanded (6.0f * u, 0.0f);
        repaint (streak.getUnion (lastStreak).getIntersection (signalRect).getSmallestIntegerContainer());
        lastStreak = streak;
    }
    if ((frame & 1) == 0)
        for (const auto& st : stars)
            repaint (juce::Rectangle<float> (st.second * 2.4f, st.second * 2.4f).withCentre (st.first).getSmallestIntegerContainer());
}

void OniriqueEQAudioProcessorEditor::resized()
{
    backdropLayer = {};   // fond redessiné à la nouvelle taille au prochain paint()

    const float u = scale();
    const float W = (float) getWidth(), H = (float) getHeight();
    const float m = 22.0f * u, topY = 96.0f * u;

    // ---- en-tête : OUTPUT, presets, licence, BYPASS à droite
    logoRect = { 22.0f * u, 14.0f * u, 64.0f * u * 985.0f / 895.0f, 64.0f * u };
    signalRect = { 0.0f, 80.0f * u, W, 12.0f * u };
    bypassButton.setBounds (juce::Rectangle<float> (136.0f * u, 40.0f * u).withRightX (W - m).withY (24.0f * u).toNearestInt());
    // la pastille de licence : un bouton de l'en-tête, entre les presets et BYPASS, à leur hauteur ; sa place suit son texte
    const float badgeW = licenseOverlay != nullptr ? (float) licenseOverlay->pillWidth() : 0.0f;
    const float badgeRight = (float) bypassButton.getX() / u - 10.0f;
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), u, juce::Rectangle<float> (badgeRight - badgeW, 24.0f, badgeW, 40.0f).toNearestInt());
    presetBar.setBounds (juce::Rectangle<float> (336.0f * u, 40.0f * u).withRightX ((badgeRight - badgeW - 12.0f) * u).withY (24.0f * u).toNearestInt());
    outputV.setBounds (juce::Rectangle<float> (150.0f * u, 32.0f * u).withRightX ((float) presetBar.getX() - 14.0f * u).withY (28.0f * u).toNearestInt());

    eq.setBounds (juce::Rectangle<float> (m, topY, W - 2.0f * m, H - topY - m).toNearestInt());

    if (logo.isValid())
    {
        // logo pré-réduit (net) + version floutée pour la lueur, recalculés seulement au redimensionnement
        const int lh = juce::jlimit (16, 600, juce::roundToInt (logoRect.getHeight() * 2.0f));
        logoScaled = logo.rescaled (juce::roundToInt ((float) lh * 985.0f / 895.0f), lh, juce::Graphics::highResamplingQuality);
        const int pad = lh / 4;
        juce::Image src (juce::Image::ARGB, logoScaled.getWidth() + 2 * pad, logoScaled.getHeight() + 2 * pad, true);
        { juce::Graphics lg (src); lg.drawImageAt (logoScaled, pad, pad); }
        logoGlow = juce::Image (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
        juce::ImageConvolutionKernel k (15);
        k.createGaussianBlur ((float) lh / 22.0f);
        k.applyToImage (logoGlow, src, src.getBounds());
    }

    stars.clear();
    juce::Random rnd (11);
    const float titleRight = logoRect.getRight() + 14.0f * u
                           + ui::textWidth (ui::font (ui::Face::display, 30.0f * u, true, 0.10f), "ONIRIQUE") + 12.0f * u
                           + ui::textWidth (ui::font (ui::Face::display, 30.0f * u, false, 0.10f), "EQ") + 30.0f * u;
    for (int i = 0; i < 60 && stars.size() < 12; ++i)
    {
        const float x = titleRight + ((float) outputV.getX() - 16.0f * u - titleRight) * rnd.nextFloat();   // entre le titre et les réglages
        const float y = (10.0f + 62.0f * rnd.nextFloat()) * u;
        stars.push_back ({ { x, y }, (2.5f + 4.5f * rnd.nextFloat() * rnd.nextFloat()) * u });
    }

    if (! restoringSize)
        onirique::window::remember (processor.apvts.state, getWidth(), getHeight());   // taille mémorisée avec le projet
}

void OniriqueEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    // fond : image prête, à la résolution réelle de l'écran (Retina : x2)
    const float ps = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! backdropLayer.isValid() || std::abs (ps - layersScale) > 1.0e-3f)
    {
        layersScale = ps;
        backdropLayer = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * ps)),
                                     juce::jmax (1, juce::roundToInt ((float) getHeight() * ps)), false);
        juce::Graphics bg (backdropLayer);
        bg.addTransform (juce::AffineTransform::scale (ps));
        paintBackdrop (bg);
    }
    g.setOpacity (1.0f);
    g.drawImageTransformed (backdropLayer, juce::AffineTransform::scale (1.0f / layersScale));

    juce::Graphics::ScopedSaveState ss (g);
    paintHeader (g);
}

// ---- fond : sang -> nuit, halo braise en haut à droite
void OniriqueEQAudioProcessorEditor::paintBackdrop (juce::Graphics& g)
{
    const float u = scale();
    const float W = (float) getWidth(), H = (float) getHeight();
    // DA PRO SERIES : noir profond, lueurs rouges très discrètes, grain fin
    g.fillAll (ui::night);
    g.setGradientFill (juce::ColourGradient (ui::blood.withAlpha (0.55f), W * 0.22f, H * 0.45f, ui::night.withAlpha (0.0f), W * 0.22f + W * 0.6f, H * 0.45f, true));
    g.fillRect (getLocalBounds());
    g.setGradientFill (juce::ColourGradient (ui::blood.withAlpha (0.22f), W * 0.92f, H * 1.05f, ui::night.withAlpha (0.0f), W * 0.92f - W * 0.45f, H * 1.05f, true));
    g.fillRect (getLocalBounds());
    juce::Random rnd (21);
    for (int i = 0; i < 120; ++i)
    {
        const float x = rnd.nextFloat() * W, y = rnd.nextFloat() * H, s = (0.5f + 1.2f * rnd.nextFloat() * rnd.nextFloat()) * u;
        g.setColour (ui::mist.withAlpha (0.03f + 0.07f * rnd.nextFloat()));
        g.fillEllipse (x, y, s, s);
    }
}

void OniriqueEQAudioProcessorEditor::paintStar (juce::Graphics& g, juce::Point<float> c, float r, float alpha)
{
    // étoile à 4 branches, reprise du logo
    juce::Path s;
    const float k = 0.16f * r;
    s.startNewSubPath (c.x, c.y - r);
    s.lineTo (c.x + k, c.y - k); s.lineTo (c.x + r, c.y); s.lineTo (c.x + k, c.y + k);
    s.lineTo (c.x, c.y + r);     s.lineTo (c.x - k, c.y + k); s.lineTo (c.x - r, c.y); s.lineTo (c.x - k, c.y - k);
    s.closeSubPath();
    g.setColour (ui::ember.withAlpha (0.25f * alpha));
    g.fillEllipse (juce::Rectangle<float> (r * 1.1f, r * 1.1f).withCentre (c));
    g.setColour (ui::mist.withAlpha (alpha));
    g.fillPath (s);
}

void OniriqueEQAudioProcessorEditor::paintHeader (juce::Graphics& g)
{
    const float u = scale();
    const float W = (float) getWidth();
    const float t = (float) clock;

    for (size_t i = 0; i < stars.size(); ++i)
    {
        const float a = 0.15f + 0.75f * (0.5f + 0.5f * std::sin (t * (0.9f + 0.37f * (float) (i % 5)) + (float) i * 1.7f));
        paintStar (g, stars[i].first, stars[i].second, a);
    }

    // logo : lueur braise qui respire + sigil net
    if (logoScaled.isValid())
    {
        const float breathe = 0.5f + 0.5f * std::sin (t * 2.0f * kPi / 5.0f);
        const float pad = logoRect.getHeight() / 4.0f;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setColour (ui::ember.withAlpha (0.55f + 0.35f * breathe));
        g.drawImage (logoGlow, logoRect.expanded (pad), juce::RectanglePlacement::stretchToFit, true);
        g.setColour (juce::Colours::white.withAlpha (0.08f + 0.10f * breathe));
        g.drawImage (logoGlow, logoRect.expanded (pad), juce::RectanglePlacement::stretchToFit, true);
        g.setOpacity (1.0f);
        g.drawImage (logoScaled, logoRect, juce::RectanglePlacement::stretchToFit);
    }

    // titre
    {
        const float x = logoRect.getRight() + 14.0f * u;
        auto f1 = ui::font (ui::Face::display, 30.0f * u, true, 0.10f);
        auto f2 = ui::font (ui::Face::display, 30.0f * u, false, 0.10f);
        const float w1 = ui::textWidth (f1, "ONIRIQUE") + 2.0f;
        const float w2 = ui::textWidth (f2, "EQ") + 2.0f;
        const float y = 20.0f * u, hh = 36.0f * u;
        g.setFont (f1);
        g.setColour (ui::mist);
        g.drawText ("ONIRIQUE", juce::Rectangle<float> (x, y, w1, hh), juce::Justification::centredLeft, false);
        g.setFont (f1);
        g.setColour (ui::ember);
        g.drawText ("EQ", juce::Rectangle<float> (x + w1 + 10.0f * u, y, w2, hh), juce::Justification::centredLeft, false);
        g.setColour (ui::haze.withAlpha (0.8f));
        g.setFont (ui::font (ui::Face::mono, 11.0f * u, false, 0.12f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("8-BAND PARAMETRIC  \xc2\xb7  ZERO LATENCY / NATURAL / LINEAR PHASE")), juce::Rectangle<float> (x + 2.0f * u, y + hh, 520.0f * u, 18.0f * u),
                    juce::Justification::centredLeft, false);
    }

    // ligne de signal : filet qui s'estompe aux bords + impulsion lumineuse qui la parcourt
    {
        const float ly = 86.0f * u;
        juce::ColourGradient cg (juce::Colours::transparentBlack, 22.0f * u, ly, juce::Colours::transparentBlack, W - 22.0f * u, ly, false);
        cg.addColour (0.08, ui::line);
        cg.addColour (0.92, ui::line);
        g.setGradientFill (cg);
        g.fillRect (juce::Rectangle<float> (22.0f * u, ly, W - 44.0f * u, std::max (1.0f, u)));

        const float len = 130.0f * u;
        const float span = W - 44.0f * u + len;
        const float px = 22.0f * u - len + std::fmod (t * 0.24f, 1.0f) * span;
        const auto streak = juce::Rectangle<float> (px, ly - 1.5f * u, len, 3.5f * u);
        juce::ColourGradient sg (ui::bone.withAlpha (0.0f), streak.getX(), ly, ui::ember, streak.getRight(), ly, false);
        sg.addColour (0.6, ui::bone);
        g.reduceClipRegion (juce::Rectangle<float> (22.0f * u, signalRect.getY(), W - 44.0f * u, signalRect.getHeight()).toNearestInt());
        g.setColour (ui::ember.withAlpha (0.25f));
        g.fillRoundedRectangle (streak.expanded (0.0f, 2.5f * u).withTrimmedLeft (len * 0.5f), 3.0f * u);
        g.setGradientFill (sg);
        g.fillRoundedRectangle (streak, streak.getHeight() * 0.5f);
    }
}
