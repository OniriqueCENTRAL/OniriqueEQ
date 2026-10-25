#pragma once

#include "UIKit.h"
#include "ui/OniriqueBar.h"

// ONIRIQUE EQ : la barre du haut des plugins ONIRIQUE (le titre, les presets, IN, A/B, copier / coller, la licence, la
// taille de la fenêtre), une ligne (ce que fait l'EQ, OUTPUT) puis l'EQ paramétrique de la VOCALBOX en grand.
class OniriqueEQAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OniriqueEQAudioProcessorEditor (OniriqueEQAudioProcessor&);
    ~OniriqueEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override { eq.analyzer.closeBubble(); }   // clic dans le vide : la bulle se ferme

private:
    void timerCallback() override;
    float scale() const noexcept { return (float) getWidth() / onirique::ui::kEditorBaseW; }
    float top() const noexcept;   // où commence l'ancien en-tête (en pixels) : son haut, vide, passe sous la barre
    void paintBackdrop (juce::Graphics&);
    void paintHeader (juce::Graphics&);
    float titleWidth() const;
    void paintTitle (juce::Graphics&, juce::Rectangle<float> area) const;   // le titre, dans la barre (en points)

    OniriqueEQAudioProcessor& processor;
    onirique::ui::FontScope fontScope;   // polices de la charte, le temps que l'interface est ouverte
    OniriqueLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 };

    DragValue outputV { processor.apvts, onirique::ids::output, "Output level (after the EQ): drag up/down, double-click = 0 dB." };
    EqFullView eq { processor };
    onirique::nuit::Bar bar;

    std::vector<Animated*> animated;
    juce::Image logo, logoGlow;   // le logo du titre et sa lueur, qui respire
    juce::Image backdropLayer;    // vidée au redimensionnement
    float layersScale = 0.0f;
    juce::Rectangle<float> signalRect;
    juce::Rectangle<float> lastStreak;   // impulsion de la ligne de signal à l'image précédente (zone à effacer)
    double clock = 0.0;
    int frame = 0;
    bool restoringSize = true;   // pendant la construction : ne pas écraser la taille mémorisée

    std::unique_ptr<onirique::license::Overlay> licenseOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueEQAudioProcessorEditor)
};
