#include "PluginEditor.h"
#include "OniriqueWindowSize.h"
#include <BinaryData.h>

using namespace onirique;

namespace
{
constexpr float kBaseW = ui::kEditorBaseW, kBaseH = ui::kEditorBaseH;
constexpr int kMinW = 1100, kMaxW = 3200;   // 3200 : 200 % de la taille par défaut (menu de la barre du haut)
constexpr float kPi = juce::MathConstants<float>::pi;

// la barre du haut : en points, 1600 à la taille par défaut ; le haut de l'ancien en-tête (le titre, qui est
// maintenant dans la barre) passe sous elle
constexpr float kBarUnits = 1600.0f, kBarH = 50.0f;   // nuit::Bar::heightFor (1600)
constexpr float kHeaderCut = 40.0f;                    // en unités de l'interface
constexpr float kHeightPerWidth = kBarH / kBarUnits + (kBaseH - kHeaderCut) / kBaseW;

int heightFor (int width) { return juce::roundToInt ((float) width * kHeightPerWidth); }

// le titre, tel qu'il était en tête de l'interface, ramené à la hauteur de la barre
constexpr float kTitleScale = 0.6f, kOldCentreY = 42.0f, kOldLogoX = 22.0f;
const float kOldLogoW = 64.0f * 985.0f / 895.0f;
}

OniriqueEQAudioProcessorEditor::OniriqueEQAudioProcessorEditor (OniriqueEQAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      bar (p.presets, p.apvts, "EQ", juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize),
           (int) kBarUnits, (int) kBarUnits)
{
    jassert (nuit::Bar::heightFor ((int) kBarUnits) == (int) kBarH);
    tooltipWindow.setLookAndFeel (&lnf);
    setLookAndFeel (&lnf);   // menus et champs de texte dans le style du plugin
    setOpaque (true);

    addAndMakeVisible (eq);
    addAndMakeVisible (outputV);
    outputV.setPrefix ("OUT ");
    for (auto* a : std::initializer_list<Animated*> { &eq, &outputV })
        animated.push_back (a);
    addAndMakeVisible (bar);

    // le titre d'ONIRIQUE EQ tient la place de la marque de la barre : il n'est écrit qu'une fois
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);
    if (logo.isValid())
    {
        // à la résolution d'un zoom de 200 % sur un écran Retina, une fois pour toutes
        const int lh = 160, pad = lh / 4;
        logo = logo.rescaled (juce::roundToInt ((float) lh * 985.0f / 895.0f), lh, juce::Graphics::highResamplingQuality);
        juce::Image src (juce::Image::ARGB, logo.getWidth() + 2 * pad, logo.getHeight() + 2 * pad, true, juce::SoftwareImageType());
        { juce::Graphics lg (src); lg.drawImageAt (logo, pad, pad); }
        logoGlow = juce::Image (juce::Image::ARGB, src.getWidth(), src.getHeight(), true, juce::SoftwareImageType());
        juce::ImageConvolutionKernel k (15);
        k.createGaussianBlur ((float) lh / 22.0f);
        k.applyToImage (logoGlow, src, src.getBounds());
    }
    bar.setBrand (juce::roundToInt (titleWidth()), [this] (juce::Graphics& g, juce::Rectangle<float> area) { paintTitle (g, area); });

    // Licence : pastille (dans la barre) et page d'activation, par-dessus tout le reste.
    licenseOverlay = std::make_unique<onirique::license::Overlay> ("ONIRIQUE EQ", processor.license);
    addAndMakeVisible (*licenseOverlay);

    // redimensionnable, proportions verrouillées : aucune déformation, aucun texte coupé.
    // La taille mémorisée est lue AVANT tout appel qui redimensionne (setResizeLimits() déclenche un resized()).
    const int w = onirique::window::openingSize (processor.apvts.state, "ONIRIQUE EQ", { 1600, heightFor (1600) },
                                                 { kMinW, heightFor (kMinW) }, { kMaxW, heightFor (kMaxW) }).x;
    setResizable (true, true);
    setResizeLimits (kMinW, heightFor (kMinW), kMaxW, heightFor (kMaxW));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (1.0 / (double) kHeightPerWidth);
    setSize (w, heightFor (w));
    restoringSize = false;
    onirique::window::remember (processor.apvts.state, getWidth(), getHeight());
    resized();

   #if ONIRIQUE_UI_TEST
    // essais locaux uniquement (jamais dans les versions publiées) : captures et visuel de démo sans la page d'activation
    const auto test = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {});
    if (test.contains ("nolicense"))
        licenseOverlay->setVisible (false);
    // ONIRIQUE_UI_TEST=nolicense,demo : visuel de la boutique avec une vraie courbe (preset Vocal Clarity)
    if (test.contains ("demo"))
        processor.presets.load ("Vocal Clarity");
    if (test.contains ("panel"))
        eq.showBandPanel (0);
    // ONIRIQUE_UI_PNG=fichier.png : une image de l'interface (x2, moteur logiciel de JUCE), navigateur ou menu ouverts
    if (const auto png = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_PNG", {}); png.isNotEmpty())
    {
        juce::Component::SafePointer<OniriqueEQAudioProcessorEditor> safe (this);
        juce::Timer::callAfterDelay (1500, [safe, png, test]
        {
            if (safe == nullptr) return;
            if (test.contains ("browser")) safe->bar.openBrowser();
            if (test.contains ("menu")) safe->bar.openMenu();
            juce::Timer::callAfterDelay (400, [safe, png]
            {
                if (safe == nullptr) return;
                juce::Image img (juce::Image::ARGB, safe->getWidth() * 2, safe->getHeight() * 2, true, juce::SoftwareImageType());
                {
                    juce::Graphics g2 (img);
                    g2.addTransform (juce::AffineTransform::scale (2.0f));
                    safe->paintEntireComponent (g2, true);
                }
                juce::File f (png);
                f.deleteFile();
                if (auto os = f.createOutputStream()) juce::PNGImageFormat().writeImageToStream (img, *os);
            });
        });
    }
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

float OniriqueEQAudioProcessorEditor::top() const noexcept
{
    return kBarH * (float) getWidth() / kBarUnits - kHeaderCut * scale();
}

void OniriqueEQAudioProcessorEditor::timerCallback()
{
    clock += 1.0 / 60.0;
    ++frame;
    for (auto* a : animated) a->tick();

    // le logo du titre respire (30 images/s : il respire en 5 s), dans la barre
    if ((frame & 1) == 0)
        bar.repaint (bar.getBrandArea().withY (0).withHeight ((int) kBarH).expanded (10, 0));
    // le signal qui parcourt la ligne : seulement la zone de l'impulsion, pas toute la largeur
    {
        const float u = scale(), W = (float) getWidth(), len = 130.0f * u, span = W - 44.0f * u + len;
        const float px = 22.0f * u - len + std::fmod ((float) clock * 0.24f, 1.0f) * span;
        const auto streak = juce::Rectangle<float> (px, signalRect.getY(), len, signalRect.getHeight()).expanded (6.0f * u, 0.0f);
        repaint (streak.getUnion (lastStreak).getIntersection (signalRect).getSmallestIntegerContainer());
        lastStreak = streak;
    }
}

void OniriqueEQAudioProcessorEditor::resized()
{
    backdropLayer = {};   // fond redessiné à la nouvelle taille au prochain paint()

    const float u = scale(), b = (float) getWidth() / kBarUnits;
    const float W = (float) getWidth(), H = (float) getHeight();
    const float m = 22.0f * u, oy = top(), topY = oy + 96.0f * u;

    // ---- la barre du haut, la pastille de licence à sa place dans la barre
    bar.setPillWidth (licenseOverlay != nullptr ? licenseOverlay->pillWidth() : 0);
    bar.place (b, getLocalBounds());
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), b, bar.getPillArea(), juce::Justification::centredRight);

    // ---- dessous : ce que fait l'EQ à gauche, OUTPUT à droite, puis la ligne de signal
    signalRect = { 0.0f, oy + 80.0f * u, W, 12.0f * u };
    outputV.setBounds (juce::Rectangle<float> (150.0f * u, 32.0f * u).withRightX (W - m).withY (oy + 46.0f * u).toNearestInt());

    eq.setBounds (juce::Rectangle<float> (m, topY, W - 2.0f * m, H - topY - m).toNearestInt());

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

    // ce que fait l'EQ, sous la barre (la ligne qui suivait le titre)
    g.setColour (ui::haze.withAlpha (0.8f));
    g.setFont (ui::font (ui::Face::mono, 11.0f * u, false, 0.12f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("8-BAND PARAMETRIC  \xc2\xb7  ZERO LATENCY / NATURAL / LINEAR PHASE")),
                juce::Rectangle<float> (24.0f * u, top() + 53.0f * u, 620.0f * u, 18.0f * u), juce::Justification::centredLeft, false);
}

void OniriqueEQAudioProcessorEditor::paintHeader (juce::Graphics& g)
{
    const float u = scale();
    const float W = (float) getWidth();
    const float t = (float) clock;

    // ligne de signal : filet qui s'estompe aux bords + impulsion lumineuse qui la parcourt
    const float ly = top() + 86.0f * u;
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

float OniriqueEQAudioProcessorEditor::titleWidth() const
{
    const float k = kTitleScale;
    const auto f1 = ui::font (ui::Face::display, 30.0f * k, true, 0.10f);
    return (kOldLogoW + 14.0f) * k + ui::textWidth (f1, "ONIRIQUE") + 2.0f + 10.0f * k + ui::textWidth (f1, "EQ") + 8.0f;
}

// Le titre tel qu'il était en tête de l'interface (logo à la lueur braise qui respire, ONIRIQUE EQ) :
// x et y sont ceux de l'ancien en-tête, ramenés dans la zone de la marque de la barre.
void OniriqueEQAudioProcessorEditor::paintTitle (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const float k = kTitleScale;
    auto X = [&] (float oldX) { return area.getX() + (oldX - kOldLogoX) * k; };
    auto Y = [&] (float oldY) { return kBarH * 0.5f + (oldY - kOldCentreY) * k; };

    const auto logoRect = juce::Rectangle<float> (X (kOldLogoX), Y (14.0f), kOldLogoW * k, 64.0f * k);
    if (logo.isValid())
    {
        const float t = (float) clock;
        const float breathe = 0.5f + 0.5f * std::sin (t * 2.0f * kPi / 5.0f);
        const float pad = logoRect.getHeight() / 4.0f;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setColour (ui::ember.withAlpha (0.55f + 0.35f * breathe));
        g.drawImage (logoGlow, logoRect.expanded (pad), juce::RectanglePlacement::stretchToFit, true);
        g.setColour (juce::Colours::white.withAlpha (0.08f + 0.10f * breathe));
        g.drawImage (logoGlow, logoRect.expanded (pad), juce::RectanglePlacement::stretchToFit, true);
        g.setOpacity (1.0f);
        g.drawImage (logo, logoRect, juce::RectanglePlacement::stretchToFit);
    }

    const float x = logoRect.getRight() + 14.0f * k;
    const auto f1 = ui::font (ui::Face::display, 30.0f * k, true, 0.10f);
    const float w1 = ui::textWidth (f1, "ONIRIQUE") + 2.0f;
    const float w2 = ui::textWidth (f1, "EQ") + 2.0f;
    const float y = Y (20.0f), hh = 36.0f * k;
    g.setFont (f1);
    g.setColour (ui::mist);
    g.drawText ("ONIRIQUE", juce::Rectangle<float> (x, y, w1, hh), juce::Justification::centredLeft, false);
    g.setColour (ui::ember);
    g.drawText ("EQ", juce::Rectangle<float> (x + w1 + 10.0f * k, y, w2, hh), juce::Justification::centredLeft, false);
}
