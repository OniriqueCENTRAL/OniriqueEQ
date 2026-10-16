#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace onirique
{
namespace ids
{
    inline constexpr const char* output    = "output";
    inline constexpr const char* bypass    = "bypass";
    inline constexpr const char* phaseMode = "phaseMode";         // Zero Latency / Natural Phase / Linear Phase
    inline constexpr const char* peqListen     = "peqListen";
    inline constexpr const char* peqListenBand = "peqListenBand";
    inline constexpr const char* peqPower = "peqPower";
    inline constexpr const char* peqOn[8]    { "peq1On", "peq2On", "peq3On", "peq4On", "peq5On", "peq6On", "peq7On", "peq8On" };
    inline constexpr const char* peqType[8]  { "peq1Type", "peq2Type", "peq3Type", "peq4Type", "peq5Type", "peq6Type", "peq7Type", "peq8Type" };
    inline constexpr const char* peqFreq[8]  { "peq1Freq", "peq2Freq", "peq3Freq", "peq4Freq", "peq5Freq", "peq6Freq", "peq7Freq", "peq8Freq" };
    inline constexpr const char* peqGain[8]  { "peq1Gain", "peq2Gain", "peq3Gain", "peq4Gain", "peq5Gain", "peq6Gain", "peq7Gain", "peq8Gain" };
    inline constexpr const char* peqQ[8]     { "peq1Q", "peq2Q", "peq3Q", "peq4Q", "peq5Q", "peq6Q", "peq7Q", "peq8Q" };
    inline constexpr const char* peqSlope[8] { "peq1Slope", "peq2Slope", "peq3Slope", "peq4Slope", "peq5Slope", "peq6Slope", "peq7Slope", "peq8Slope" };
    inline constexpr const char* peqDyn[8]   { "peq1Dyn", "peq2Dyn", "peq3Dyn", "peq4Dyn", "peq5Dyn", "peq6Dyn", "peq7Dyn", "peq8Dyn" };
    inline constexpr const char* peqThr[8]   { "peq1Thresh", "peq2Thresh", "peq3Thresh", "peq4Thresh", "peq5Thresh", "peq6Thresh", "peq7Thresh", "peq8Thresh" };
}

inline const juce::StringArray& phaseModeNames()
{
    static const juce::StringArray n { "Zero Latency", "Natural Phase", "Linear Phase" };
    return n;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using Attr = AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto dbText = [] (float v, int)
    {
        v = std::round (v * 10.0f) / 10.0f + 0.0f;   // évite "-0.0"
        return String (v > 0.0f ? "+" : "") + String (v, 1) + " dB";
    };

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, Attr().withLabel ("dB").withStringFromValueFunction (dbText)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));
    // change la latence : réglage de session, non automatisable
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::phaseMode, 1 }, "Phase Mode", phaseModeNames(), 0,
                                                         AudioParameterChoiceAttributes().withAutomatable (false)));
    // écoute d'une bande (session : ni presets, ni automation)
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::peqListen, 1 }, "Listen", false,
                                                       AudioParameterBoolAttributes().withAutomatable (false)));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { ids::peqListenBand, 1 }, "Listen Band", 0, 7, 2,
                                                      AudioParameterIntAttributes().withAutomatable (false)));

    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::peqPower, 1 }, "EQ On", true));
    auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (juce::roundToInt (v)) + " Hz"; };
    auto qText = [] (float v, int) { return String (v, 2); };
    int defType[8] { 3, 1, 0, 0, 0, 2, 0, 4 };      // LOW CUT, LOW SHELF, BELL x3, HIGH SHELF, BELL, HIGH CUT
    float defFreq[8] { 30.0f, 100.0f, 300.0f, 1000.0f, 3000.0f, 10000.0f, 6000.0f, 18000.0f };
    float defQ[8] { 0.71f, 0.71f, 1.0f, 1.0f, 1.0f, 0.71f, 1.0f, 0.71f };
    float defGain[8] {};
    bool defOn[8] {};
   #if ONIRIQUE_UI_TEST
    // essais locaux uniquement : visuel de démo de la boutique (ONIRIQUE_UI_TEST=...demo) avec une vraie courbe au repos
    // (preset Vocal Clarity) : l'outil de démo ramène chaque réglage à sa valeur par défaut
    if (SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("demo"))
    {
        const int t[4] { 3, 0, 0, 2 };
        const float f[4] { 80.0f, 250.0f, 3200.0f, 11000.0f }, g[4] { 0.0f, -2.5f, 2.0f, 3.0f }, q[4] { 0.71f, 1.2f, 0.8f, 0.71f };
        for (int i = 0; i < 4; ++i) { defOn[i] = true; defType[i] = t[i]; defFreq[i] = f[i]; defGain[i] = g[i]; defQ[i] = q[i]; }
    }
   #endif
    for (int b = 0; b < 8; ++b)
    {
        const String n = "Band " + String (b + 1) + " ";
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::peqOn[b], 1 }, n + "On", defOn[b]));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::peqType[b], 1 }, n + "Type",
            StringArray { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" }, defType[b]));
        NormalisableRange<float> fr (20.0f, 20000.0f, 0.1f);
        fr.setSkewForCentre (1000.0f);
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::peqFreq[b], 1 }, n + "Freq", fr, defFreq[b],
                                                            Attr().withLabel ("Hz").withStringFromValueFunction (hz)));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::peqGain[b], 1 }, n + "Gain",
            NormalisableRange<float> (-18.0f, 18.0f, 0.1f), defGain[b], Attr().withLabel ("dB").withStringFromValueFunction (dbText)));
        NormalisableRange<float> qr (0.1f, 10.0f, 0.01f);
        qr.setSkewForCentre (1.0f);
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::peqQ[b], 1 }, n + "Q", qr, defQ[b],
                                                            Attr().withStringFromValueFunction (qText)));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::peqSlope[b], 1 }, n + "Slope",
            StringArray { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 1));
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::peqDyn[b], 1 }, n + "Dynamic", false));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::peqThr[b], 1 }, n + "Threshold",
            NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -30.0f,
            Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    }
    return { p.begin(), p.end() };
}
} // namespace onirique
