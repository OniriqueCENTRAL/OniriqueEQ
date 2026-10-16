#pragma once

#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"

#include <array>
#include <map>

namespace onirique::ui
{
// largeur de référence de la fenêtre : toutes les tailles de l'interface sont données pour elle, puis mises à l'échelle
inline constexpr float kEditorBaseW = 1700.0f, kEditorBaseH = 960.0f;

// ---- DA du site ONIRIQUE : nuit / sang, accent braise, lignes fines, lueurs
inline const juce::Colour night  { 0xff0D0809 };
inline const juce::Colour blood  { 0xff5C0008 };
inline const juce::Colour deep   { 0xff1A080A };
inline const juce::Colour deep2  { 0xff2A0A0E };
inline const juce::Colour mist   { 0xffF6EEEE };
inline const juce::Colour haze   { 0xffA88F92 };
inline const juce::Colour ember  { 0xffE23444 };
inline const juce::Colour bone   { 0xffFFECEE };
inline const juce::Colour line   { (juce::uint8) 255, (juce::uint8) 210, (juce::uint8) 214, 0.13f };

// Polices : display (titres), body (libellés), mono (valeurs). Le site utilise Unbounded / Hanken
// Grotesk / JetBrains Mono ; ici leurs équivalents système (tout passe par font()).
enum class Face { display, body, mono };
juce::Font font (Face, float height, bool bold = false, float kerning = 0.0f);
float textWidth (const juce::Font&, const juce::String&);
// police de hauteur maxH, réduite si nécessaire pour que le texte tienne dans maxW (jamais coupé)
juce::Font fitFont (Face, float maxH, float maxW, const juce::String&, bool bold = false, float kerning = 0.0f);

// échelle de la fenêtre (1 = 1480 px de large) : sert de PLANCHER aux tailles de texte, pour qu'un petit bouton
// garde des libellés lisibles
float editorScale (const juce::Component&);

// trait lumineux : plusieurs passes de plus en plus larges et transparentes sous le trait net
void strokeGlow (juce::Graphics&, const juce::Path&, juce::Colour, float width, float intensity);

// ombre portée d'un rectangle arrondi (corner >= 0) ou d'une ellipse (corner < 0) : même rendu que
// juce::DropShadow::drawForPath, mais le flou est calculé une seule fois par forme puis gardé en cache
// (sur Mac, Core Graphics redessine de grandes zones à chaque image : recalculer les flous y faisait ramer l'interface).
// Le cache appartient à l'interface (jamais statique) : ses images disparaissent avec elle, avant que l'hôte
// décharge le plugin (un cache statique libéré au déchargement de la DLL bloquait FL Studio à la suppression).
struct ShadowCache
{
    std::map<std::array<int, 8>, juce::Image> images;
};
void drawCachedShadow (juce::Graphics&, ShadowCache&, const juce::DropShadow&, juce::Rectangle<float> shape, float corner);

// approche exponentielle d'une valeur animée ; renvoie true tant qu'elle bouge
inline bool approach (float& v, float target, float k) noexcept
{
    if (std::abs (target - v) < 1.0e-3f) { const bool moved = v != target; v = target; return moved; }
    v += k * (target - v);
    return true;
}

// ressort légèrement sous-amorti (glissements de pastilles, d'onglets, de pastilles d'ordre) : part vite, dépasse à peine
// (~3 %), se pose sans à-coup. vel : vitesse gardée entre deux images (60 Hz).
inline bool spring (float& v, float& vel, float target, float stiffness = 0.16f, float damping = 0.70f) noexcept
{
    vel = vel * damping + (target - v) * stiffness;
    v += vel;
    if (std::abs (target - v) < 1.0e-3f && std::abs (vel) < 1.0e-3f) { const bool moved = v != target; v = target; vel = 0.0f; return moved; }
    return true;
}
} // namespace onirique::ui

// Tout ce qui s'anime reçoit tick() à 60 Hz depuis l'éditeur (une seule horloge pour toute l'interface).
struct Animated
{
    virtual ~Animated() = default;
    virtual void tick() = 0;
};

class OniriqueLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OniriqueLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;
    juce::Font getPopupMenuFont() override { return onirique::ui::font (onirique::ui::Face::body, 15.0f); }

    // menus (presets) : verre sombre arrondi, pastille braise au survol, point braise sur l'élément actif
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& name) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override { return 7; }

private:
    onirique::ui::ShadowCache shadows;   // ombres des boutons et des curseurs
};

// Bouton rotatif (ou curseur vertical, Layout::fader) complet : nom + contrôle + valeur dans une
// pastille. L'arc lumineux suit la valeur en glissant ; il s'illumine au survol et pendant le réglage.
class KnobComponent : public juce::Component, public Animated
{
public:
    enum class Layout { textBelow, textRight, fader, horizontal };   // horizontal : NOM  ━━●━━  [valeur]

    KnobComponent (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& title,
                   const juce::String& minLabel, const juce::String& maxLabel, Layout layout,
                   const juce::String& tooltip, juce::LookAndFeel& lnf, bool bipolar = false);
    ~KnobComponent() override;

    // taille de référence max (diamètre d'un bouton) : aligne un curseur sur les boutons voisins
    void setMaxDiameter (float d) { maxD = d; resized(); }
    // appelé quand le survol / réglage commence ou finit (surbrillance de la bande dans l'afficheur)
    std::function<void (bool)> onFocusChange;
    // activité (ex. réduction en dB) : une LED braise s'allume à côté du nom quand elle dépasse 0.5
    std::function<float()> activity;

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void setFocus (bool hover, bool drag);

    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String title;
    Layout layout;
    bool dragging = false, hovering = false;
    float shown = 0.0f, glow = 0.0f, act = 0.0f;
    juce::Rectangle<float> nameArea, valueArea;
    float unit = 1.0f, maxD = 1.0e6f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobComponent)
};

// Valeur réglable dans une pastille (glisser verticalement, molette, double-clic = défaut).
class DragValue : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    DragValue (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& tooltip);
    std::function<void (bool)> onFocusChange;
    void setPrefix (const juce::String& p) { prefix = p; repaint(); }   // ex. "GAIN " devant la valeur

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    float startNorm = 0.0f, glow = 0.0f;
    juce::String prefix;
    bool hovering = false, dragging = false;
};

// Bouton power d'un module : anneau braise lumineux quand le module est actif.
class PowerButton : public juce::ToggleButton, public Animated
{
public:
    PowerButton() { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    float on = 1.0f, ripple = 0.0f;   // ripple : onde qui part du bouton quand on l'allume
    bool was = true;
};

// Interrupteur : grand (AUTO GAIN, ON/OFF + correction en dB) ou petit (BYPASS avec LED)
class PillToggle : public juce::ToggleButton, public Animated
{
public:
    enum class Style { big, small, mini };   // mini : interrupteur compact dans une bande de sélecteurs
    PillToggle (const juce::String& label, Style s) : juce::ToggleButton (label), style (s) { setClickingTogglesState (true); }

    void setCaption (const juce::String& c) { if (c != caption) { caption = c; repaint(); } }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    Style style;
    juce::String caption;
    float on = 0.0f, ripple = 0.0f;
    bool was = true;
};

// Analyseur en temps réel + EQ paramétrique : spectres d'entrée et de sortie (FFT 8192, lissage 1/6 d'octave et
// retombée douce, courbes lissées), courbe EXACTE de l'EQ paramétrique (mêmes filtres que l'audio, gain dynamique en
// direct). Les points numérotés se tirent à la souris ; clic droit = menu
// de la bande (type, pente, mode dynamique). fullScreen : version plein écran (plus de repères, étiquettes de bande).
class SpectrumAnalyzer : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    SpectrumAnalyzer (OniriqueEQAudioProcessor&, bool fullScreen = false);

    // EQ paramétrique : bande sélectionnée (affichée dans la barre de réglage de la carte)
    std::function<void (int)> onSelect;
    int getSelected() const noexcept { return selected; }
    void select (int band);
    void setSelectedSilently (int band) { selected = juce::jlimit (0, kPeq - 1, band); eqDirty = true; }
    // un seul analyseur lit les prises à la fois (celui qui est affiché)
    void setActive (bool a) { active = a; }

    void paint (juce::Graphics&) override;
    void tick() override;
    juce::String getTooltip() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // menu de bande : type, pente (coupe-bas / coupe-haut), mode dynamique, allumer / couper, remise à zéro
    // le menu s'ouvre à l'endroit du clic (screenPos), plus jamais au bord de l'analyseur
    static void showBandMenu (juce::Component& target, juce::AudioProcessorValueTreeState&, int band, juce::Point<int> screenPos);

private:
    static constexpr int kOrder = 13, kN = 1 << kOrder, kPts = 320, kPeq = onirique::ParametricEQ::kBands;
    static constexpr float kEqRange = 20.0f;

    struct PointMap { int b0 = 1, b1 = 1; float pos = 1.0f, width = 1.0f; bool interp = true; };
    void buildMap (double fs);
    void analyse (onirique::SpectrumTap&, std::vector<float>& hist, int& writePos, std::array<float, kPts>& spec);
    bool updateCurves (double fs);
    void paintGrid (juce::Graphics&);
    void paintEq (juce::Graphics&);
    float hzAt (int i) const noexcept { return 20.0f * std::pow (1000.0f, (float) i / (float) (kPts - 1)); }
    float param (const char* id) const { return processor.apvts.getRawParameterValue (id)->load(); }
    float unit() const noexcept;
    juce::Rectangle<float> plotArea() const noexcept;
    float xOf (float hz) const noexcept;
    float hzOf (float x) const noexcept;
    float yEq (float db) const noexcept;
    float dbOf (float y) const noexcept;
    float ySpec (float db) const noexcept;
    onirique::ParametricEQ::Band band (int b) const;
    juce::Point<float> nodePos (int b) const;
    int nodeAt (juce::Point<float>) const;
    void setParam (const char* id, float v, bool gesture = true);
    juce::Path smoothPath (const std::array<float, kPts>& ys, bool closeToBottom, float bottom, int step = 1) const;
    void flushDrag();

    OniriqueEQAudioProcessor& processor;
    const bool full;
    bool active = true;
    juce::dsp::FFT fft { kOrder };
    std::vector<float> window, histIn, histOut, scratch, fftBuf;
    std::array<PointMap, kPts> pmap {};
    double mapFs = 0.0;
    int writeIn = 0, writeOut = 0;
    float windowGain = 1.0f;
    std::array<float, kPts> specIn {}, specOut {}, curve {}, maxCurve {}, selCurve {};
    std::array<float, kPeq * 8 + 2> lastSig {};
    juce::Image gridLayer, eqLayer;   // calques : grille (taille) et courbes d'EQ (réglages) ; seuls les spectres sont redessinés à chaque image
    bool eqDirty = true;
    float eqAmt = 1.0f;
    bool anyDyn = false;
    int selected = 2, hoverNode = -1, dragNode = -1;
    float nodeGlow[kPeq] {}, liveGain[kPeq] {};
    juce::Point<float> dragStart;
    float dragF0 = 1000.0f, dragG0 = 0.0f;
    // point tiré : la position voulue est retenue à chaque mouvement, et envoyée à l'hôte une fois par image
    // (une souris rapide envoie des centaines de mouvements par seconde : autant de notifications pour le DAW)
    float pendingF = 0.0f, pendingG = 0.0f;
    bool pending = false;
    int qFlashBand = -1, qFlashFrames = 0;   // étiquette "Q x.xx" affichée pendant le réglage à la molette
};

// Barre de réglage d'une bande de l'EQ paramétrique : n° + LED (on/off) + type (menu), "DYN" quand elle est dynamique.
class PeqBandChip : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit PeqBandChip (juce::AudioProcessorValueTreeState& s) : apvts (s)
    {
        setTooltip ("Parametric band: click the LED to switch it on/off, click the name for its menu "
                    "(type, slope, dynamic mode).");
    }
    void setBand (int b) { band = b; repaint(); }
    void setHighlighted (bool h) { if (h != highlighted) { highlighted = h; repaint(); } }
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::AudioProcessorValueTreeState& apvts;
    int band = 2;
    float lit = 0.0f;
    bool highlighted = false;
    juce::String shownType;
};

// Petit bouton à pictogramme (plein écran / sortie du plein écran), même verre sombre que les pastilles.
class IconButton : public juce::Button, public Animated
{
public:
    enum class Icon { expand, collapse, headphones };   // headphones : interrupteur (écoute de la bande), braise quand il est allumé
    explicit IconButton (Icon i) : juce::Button ("icon"), icon (i) { if (i == Icon::headphones) setClickingTogglesState (true); }
    void setText (const juce::String& t) { text = t; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    Icon icon;
    juce::String text;
    float glow = 0.0f, lit = 0.0f;
};

// Choix du traitement (menu déroulant) : ZERO LATENCY / NATURAL PHASE / LINEAR PHASE. Pastille avec le mode actif et un
// chevron ; le menu s'ouvre dessous, le mode actif coché.
class PhaseModeBox : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit PhaseModeBox (juce::AudioProcessorValueTreeState&);
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; }
    void mouseExit (const juce::MouseEvent&) override  { hovering = false; }
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::RangedAudioParameter* param = nullptr;
    int shown = -1;
    float glow = 0.0f;
    bool hovering = false, open = false;
};

// L'EQ (page principale) : titre + choix du traitement et filtres utilisés, grand analyseur, les 8 bandes réglables.
class EqFullView : public juce::Component, public Animated
{
public:
    explicit EqFullView (OniriqueEQAudioProcessor&);
    ~EqFullView() override;
    std::function<void (int)> onSelect;
    void selectBand (int b);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void parentHierarchyChanged() override { resized(); }   // l'échelle vient de la fenêtre du plugin : relayout une fois rattachée

    SpectrumAnalyzer analyzer;

private:
    juce::String processingText() const;   // ex. "FIR  ·  LINEAR PHASE  ·  8192 TAPS  ·  LATENCY 4096 SAMPLES (85.3 ms)"

    OniriqueEQAudioProcessor& processor;
    PhaseModeBox modeBox;
    PowerButton power;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt;
    IconButton listenButton { IconButton::Icon::headphones };   // écoute de la bande sélectionnée
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAtt;
    std::array<std::unique_ptr<PeqBandChip>, 8> chips;
    std::array<std::unique_ptr<DragValue>, 8> freqV, gainV, qV, thrV, slopeV;
    std::array<std::unique_ptr<PillToggle>, 8> dynB;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 8> dynAtt;
    std::vector<Animated*> children;
    juce::Rectangle<float> titleArea, bandsArea, infoArea;
    juce::String info;
    int selected = 2;
};


// Barre de presets (en-tête) : précédent / nom (menu) / suivant + SAVE (champ de nom intégré).
class PresetBar : public juce::Component, public Animated
{
public:
    explicit PresetBar (onirique::PresetManager&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    enum Zone { none = -1, prev, name, next, save };
    Zone zoneAt (juce::Point<float>) const noexcept;
    void showMenu();
    void startSave();
    void endSave (bool commit);

    onirique::PresetManager& pm;
    juce::TextEditor editor;
    juce::Rectangle<float> zones[4];
    juce::String shownName;
    bool modified = false;
    Zone hover = none;
    float glow[4] {};
    int frame = 0;
};


// Mètre de niveau vertical stéréo (L / R) : crête instantanée, retombée douce (~20 dB/s), maintien de crête 1.5 s,
// zone braise près de 0 dBFS, rouge au-delà ; valeur crête maintenue affichée sous les barres (clic = remise à zéro).
class LevelMeter : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit LevelMeter (std::function<float (int)> consumePeak) : consume (std::move (consumePeak))
    {
        setTooltip ("Peak level (dBFS), left / right. The number is the highest recent peak: click to reset it.");
    }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override { maxDb = -100.0f; repaint(); }

private:
    static float norm (float db) noexcept { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 62.0f); }   // -60 .. +2 dBFS

    std::function<float (int)> consume;
    float levelDb[2] { -100.0f, -100.0f }, holdDb[2] { -100.0f, -100.0f }, maxDb = -100.0f;
    int holdFrames[2] {};
    int maxFrames = 0;
};

