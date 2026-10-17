#pragma once

#include "UIKit.h"

// ONIRIQUE EQ : en-tête (logo, presets, OUTPUT, licence, BYPASS) + l'EQ paramétrique de la VOCALBOX en grand.
class OniriqueEQAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OniriqueEQAudioProcessorEditor (OniriqueEQAudioProcessor&);
    ~OniriqueEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    float scale() const noexcept { return (float) getWidth() / onirique::ui::kEditorBaseW; }
    void paintBackdrop (juce::Graphics&);
    void paintHeader (juce::Graphics&);
    void paintStar (juce::Graphics&, juce::Point<float> c, float r, float alpha);

    OniriqueEQAudioProcessor& processor;
    OniriqueLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 };

    PresetBar presetBar { processor.presets };
    DragValue outputV { processor.apvts, onirique::ids::output, "Output level (after the EQ): drag up/down, double-click = 0 dB." };
    PillToggle bypassButton { "BYPASS", PillToggle::Style::small };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
    EqFullView eq { processor };

    std::vector<Animated*> animated;
    juce::Image logo, logoScaled, logoGlow;
    juce::Image backdropLayer;   // vidée au redimensionnement
    float layersScale = 0.0f;
    juce::Rectangle<float> logoRect, signalRect;
    juce::Rectangle<float> lastStreak;   // impulsion de la ligne de signal à l'image précédente (zone à effacer)
    std::vector<std::pair<juce::Point<float>, float>> stars;   // position, taille
    double clock = 0.0;
    int frame = 0;
    bool restoringSize = true;   // pendant la construction : ne pas écraser la taille mémorisée

    std::unique_ptr<onirique::license::Overlay> licenseOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueEQAudioProcessorEditor)
};
