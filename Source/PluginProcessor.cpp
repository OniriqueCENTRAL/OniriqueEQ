#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace onirique;

OniriqueEQAudioProcessor::OniriqueEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (int b = 0; b < 8; ++b)
    {
        const char* id[8] { ids::peqOn[b], ids::peqType[b], ids::peqFreq[b], ids::peqGain[b], ids::peqQ[b],
                            ids::peqSlope[b], ids::peqDyn[b], ids::peqThr[b] };
        for (int k = 0; k < 8; ++k) pPeq[b][k] = apvts.getRawParameterValue (id[k]);
    }
    pPower      = apvts.getRawParameterValue (ids::peqPower);
    pBypass     = apvts.getRawParameterValue (ids::bypass);
    pOutput     = apvts.getRawParameterValue (ids::output);
    pMode       = apvts.getRawParameterValue (ids::phaseMode);
    pListen     = apvts.getRawParameterValue (ids::peqListen);
    pListenBand = apvts.getRawParameterValue (ids::peqListenBand);
    apvts.addParameterListener (ids::phaseMode, this);
}

OniriqueEQAudioProcessor::~OniriqueEQAudioProcessor()
{
    apvts.removeParameterListener (ids::phaseMode, this);
    cancelPendingUpdate();
}

EqEngine::Params OniriqueEQAudioProcessor::readParams() const noexcept
{
    EqEngine::Params p;
    for (int b = 0; b < 8; ++b)
    {
        auto& x = p.bands[(size_t) b];
        x.on     = pPeq[b][0]->load() > 0.5f;
        x.type   = juce::roundToInt (pPeq[b][1]->load());
        x.freq   = pPeq[b][2]->load();
        x.gainDb = pPeq[b][3]->load();
        x.q      = pPeq[b][4]->load();
        x.slope  = juce::roundToInt (pPeq[b][5]->load());
        x.dyn    = pPeq[b][6]->load() > 0.5f;
        x.thrDb  = pPeq[b][7]->load();
    }
    p.eqOn = pPower->load() > 0.5f;
    p.bypass = pBypass->load() > 0.5f;
    p.outputDb = pOutput->load();
    // casque armé : on n'entend la bande que pendant qu'on la tient (clic sur son point, dans l'analyseur)
    p.listen = pListen->load() > 0.5f && peqListenHold.load (std::memory_order_relaxed);
    p.listenBand = juce::roundToInt (pListenBand->load());
    return p;
}

bool OniriqueEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void OniriqueEQAudioProcessor::configure()
{
    const int mode = juce::jlimit (0, 2, juce::roundToInt (pMode->load()));
    engine.prepare (mode, currentRate, currentBlock, juce::jmax (1, getTotalNumOutputChannels()), readParams());
    modeNow.store (mode);
    tapsNow.store (engine.getFirTaps());
    setLatencySamples (engine.getLatency());
}

void OniriqueEQAudioProcessor::handleAsyncUpdate()
{
    const int mode = juce::jlimit (0, 2, juce::roundToInt (pMode->load()));
    if (currentRate <= 0.0 || mode == modeNow.load()) { if (currentRate <= 0.0) modeNow.store (mode); return; }
    suspendProcessing (true);    // le thread audio attend la fin de la reconfiguration (allocation hors temps réel)
    configure();
    suspendProcessing (false);
}

void OniriqueEQAudioProcessor::prepareToPlay (double sr, int block)
{
    license.refresh();   // beaucoup de DAW rappellent prepareToPlay juste avant un export

    // Toute l'allocation se fait ici ; processBlock n'alloue rien.
    currentRate = sr;
    currentBlock = juce::jmax (1, block);
    configure();
}

void OniriqueEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int total = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, total);
    if (numCh < 1 || total < 1)
        return;

    // un hôte peut envoyer plus d'échantillons qu'annoncé : traité par morceaux
    const auto params = readParams();
    float* ch[2] { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };
    inputTap.push (ch[0], ch[1], total);    // analyseur : signal d'entrée
    for (int start = 0; start < total; start += currentBlock)
    {
        const int n = juce::jmin (currentBlock, total - start);
        float* part[2] { ch[0] + start, ch[1] != nullptr ? ch[1] + start : nullptr };
        juce::AudioBuffer<float> view (part, numCh, n);
        engine.process (view, params);
    }
    outputTap.push (ch[0], ch[1], total);   // analyseur : signal de sortie
    license.process (buffer, getSampleRate(), isNonRealtime());

    for (int c = 0; c < numCh; ++c)
    {
        const float pk = buffer.getMagnitude (c, 0, total);
        if (pk > outLevel[c].load (std::memory_order_relaxed)) outLevel[c].store (pk, std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* OniriqueEQAudioProcessor::createEditor()
{
    return new OniriqueEQAudioProcessorEditor (*this);
}

void OniriqueEQAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void OniriqueEQAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presets.takeSnapshot();   // l'état rechargé sert de référence pour l'indicateur "modifié"
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OniriqueEQAudioProcessor();
}
