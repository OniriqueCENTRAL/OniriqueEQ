#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "License/LicenseGuard.h"
#include "Parameters.h"
#include "DSP/EqEngine.h"
#include "DSP/SpectrumTap.h"
#include "StockPresets.h"
#include "Instances.h"

class OniriqueEQAudioProcessor : public juce::AudioProcessor,
                                 private juce::AudioProcessorValueTreeState::Listener,
                                 private juce::AsyncUpdater
{
public:
    OniriqueEQAudioProcessor();
    ~OniriqueEQAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { engine.release(); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Le bypass du DAW pilote directement le paramètre "Bypass" (fondu propre + latence alignée)
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (onirique::ids::bypass); }

    const juce::String getName() const override { return "ONIRIQUE EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    std::atomic<float>* pRange[8] {};   // RANGE des bandes dynamiques

    // ---- les autres ONIRIQUE EQ de la session (fenêtre INSTANCES)
    juce::SharedResourcePointer<onirique::EqInstances> instances;
    int getInstanceId() const noexcept { return instanceId; }
    void updateTrackProperties (const TrackProperties& p) override
    {
        const juce::ScopedLock sl (trackLock);
        trackName = p.name.value_or (juce::String());
        trackColour = p.colour.value_or (juce::Colour());
        hasTrackColour = p.colour.has_value();
    }
    juce::String getTrackName() const { const juce::ScopedLock sl (trackLock); return trackName; }
    bool getTrackColour (juce::Colour& c) const { const juce::ScopedLock sl (trackLock); c = trackColour; return hasTrackColour; }
    onirique::SpectrumTap shareTap;   // le spectre de sortie, pour la fenêtre INSTANCES d'un autre EQ

    juce::AudioProcessorValueTreeState apvts;
    /** Les presets d'usine et ceux de l'utilisateur, la comparaison A/B : ce que montre la barre du haut. */
    onirique::PresetBank presets { apvts, "EQ", onirique::eqpresets::stockPresets(), onirique::eqpresets::tagNames(),
                                   onirique::eqpresets::notStored() };
    onirique::SpectrumTap inputTap, outputTap;   // analyseur : entrée et sortie du plugin (mono)

    // lus par l'interface
    float getPeqBandGainDb (int b) const noexcept { return engine.getBandGainDb (b); }
    int getPhaseMode() const noexcept { return modeNow.load (std::memory_order_relaxed); }      // mode qui tourne
    int getFirTaps() const noexcept { return tapsNow.load (std::memory_order_relaxed); }
    float consumeOutputLevel (int c) noexcept { return outLevel[juce::jlimit (0, 1, c)].exchange (0.0f); }

    // écoute d'une bande : vraie seulement pendant qu'on tient une bande (casque armé ou non)
    std::atomic<bool> peqListenHold { false };
    // le DAW envoie un sidechain avec du signal (ces dernières secondes) : l'interface le montre à côté du bouton SIDECHAIN
    std::atomic<bool> sidechainLive { false };

private:
    onirique::EqEngine::Params readParams() const noexcept;

    std::atomic<float>* pPeq[8][8] {};   // on, type, freq, gain, q, pente, dynamique, seuil
    std::atomic<float>* pSc[8] {};       // sidechain de chaque bande : INT / EXT
    int scSilentSamples = 0;             // depuis quand le sidechain est muet (sidechainLive)
    std::atomic<float> *pPower = nullptr, *pBypass = nullptr, *pOutput = nullptr, *pMode = nullptr,
                       *pListen = nullptr, *pListenBand = nullptr;

    // le mode de phase change la latence : la chaîne est reconfigurée sur le thread de l'interface (audio suspendu le
    // temps du changement), puis la nouvelle latence est annoncée au DAW
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    void configure();
    double currentRate = 0.0;
    int currentBlock = 512;

    onirique::EqEngine engine;
    std::atomic<int> modeNow { 0 }, tapsNow { 0 };
    int instanceId = 0;
    juce::CriticalSection trackLock;
    juce::String trackName;
    juce::Colour trackColour;
    bool hasTrackColour = false;
    std::atomic<float> outLevel[2] { { 0.0f }, { 0.0f } };

public:
    /** Licence ONIRIQUE (acheté, essai, démo) : voir License/LicenseGuard.h. */
    onirique::license::Guard license { "onirique-eq" };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueEQAudioProcessor)
};
