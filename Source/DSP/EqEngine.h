#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "ParametricEQ.h"
#include "FirEQ.h"
#include "Common.h"

namespace onirique
{
/*  ONIRIQUE EQ : le chemin audio.

    entrée -> [ ZERO LATENCY : 8 bandes IIR appariées à l'analogique (ParametricEQ, le même EQ que la VOCALBOX)
              | NATURAL / LINEAR PHASE : FIR (bandes statiques) puis IIR (bandes dynamiques seulement) ]
           -> écoute d'une bande (casque) -> EQ ON / BYPASS en fondu (signal direct aligné sur la latence) -> OUTPUT

    Le mode change la latence : il est choisi dans prepare() (l'hôte arrête l'audio le temps du changement), puis le
    son revient en fondu (20 ms). Tout le reste se règle en temps réel, sans clic.
*/
class EqEngine
{
public:
    using Bands = FirEQ::Bands;

    struct Params
    {
        Bands bands {};
        bool eqOn = true, bypass = false;
        bool listen = false; int listenBand = 2;
        float outputDb = 0.0f;
    };

    void prepare (int phaseMode, double sampleRate, int maxBlock, int numChannels, const Params& p)
    {
        mode = juce::jlimit (0, 2, phaseMode);
        fs = sampleRate;
        iir.prepare (fs);
        iir.setBands (iirBands (p.bands), true);
        if (mode != 0)
            fir.prepare (mode == 2 ? FirEQ::linearPhase : FirEQ::naturalPhase, fs, maxBlock, numChannels, p.bands);
        else
            fir.release();
        latency = mode == 0 ? 0 : fir.getLatency();
        for (auto& d : dryDelay) d.prepare (latency);
        dryBuf.setSize (2, juce::jmax (1, maxBlock), false, true, true);

        wetMix.reset (fs, 0.03);
        wetMix.setCurrentAndTargetValue (p.eqOn && ! p.bypass ? 1.0f : 0.0f);
        out.reset (fs, 0.03);
        out.setCurrentAndTargetValue (dbToGain (p.outputDb));
        fadeIn.reset (fs, 0.02);
        fadeIn.setCurrentAndTargetValue (0.0f);
        fadeIn.setTargetValue (1.0f);
        listenMix.reset (fs, 0.03);
        listenMix.setCurrentAndTargetValue (0.0f);
        listenA.reset(); listenB.reset();
        listenF = -1.0f;
    }

    void release() { fir.release(); }
    int getLatency() const noexcept { return latency; }
    int getMode() const noexcept { return mode; }
    int getFirTaps() const noexcept { return mode == 0 ? 0 : fir.getTaps(); }
    float getBandGainDb (int b) const noexcept { return iir.getBandGainDb (b); }

    void process (juce::AudioBuffer<float>& buffer, const Params& p) noexcept
    {
        const int numCh = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();
        if (n <= 0 || numCh <= 0) return;

        iir.setBands (iirBands (p.bands), false);
        if (mode != 0) fir.setBands (p.bands);
        wetMix.setTargetValue (p.eqOn && ! p.bypass ? 1.0f : 0.0f);
        out.setTargetValue (dbToGain (p.outputDb));
        setListen (p);

        // signal direct, aligné sur la latence du mode (bypass, EQ coupé, écoute)
        for (int c = 0; c < numCh; ++c)
        {
            const float* x = buffer.getReadPointer (c);
            float* d = dryBuf.getWritePointer (c);
            for (int i = 0; i < n; ++i) d[i] = dryDelay[c].process (x[i]);
        }

        if (mode != 0)
        {
            juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
            fir.process (block);
        }
        iir.process (buffer.getArrayOfWritePointers(), numCh, n);

        const bool listening = listenMix.isSmoothing() || listenMix.getTargetValue() > 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float w = wetMix.getNextValue(), g = out.getNextValue() * fadeIn.getNextValue();
            const float lm = listening ? listenMix.getNextValue() : 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float dry = dryBuf.getSample (c, i);
                float y = dry + w * (buffer.getSample (c, i) - dry);
                if (listening) y += lm * (listenGain * listenB.process (listenA.process (dry, c), c) - y);
                buffer.setSample (c, i, sanitize (y * g));
            }
        }
    }

private:
    // IIR : toutes les bandes en ZERO LATENCY ; en NATURAL / LINEAR PHASE, seulement les dynamiques (le FIR fait le reste)
    Bands iirBands (const Bands& b) const noexcept
    {
        if (mode == 0) return b;
        Bands r = b;
        for (auto& x : r) x.on = x.on && x.dyn && ParametricEQ::canBeDynamic (x.type);
        return r;
    }

    // écoute (casque) : la zone de la bande tenue, prise sur le signal d'entrée
    void setListen (const Params& p) noexcept
    {
        listenMix.setTargetValue (p.listen ? 1.0f : 0.0f);
        const auto& b = p.bands[(size_t) juce::jlimit (0, ParametricEQ::kBands - 1, p.listenBand)];
        if (b.freq == listenF && b.q == listenQ && b.type == listenType) return;
        listenF = b.freq; listenQ = b.q; listenType = b.type;
        const double f = juce::jlimit (25.0, 0.45 * fs, (double) b.freq);
        listenGain = (b.type == ParametricEQ::bell || b.type == ParametricEQ::notch) ? 1.6f : 1.0f;   // une bande étroite paraît plus faible
        if (b.type == ParametricEQ::lowShelf || b.type == ParametricEQ::lowCut)
        { listenA.setCoeffs (BiquadCoeffs::lowpass (fs, f, 0.707)); listenB.setCoeffs (BiquadCoeffs::lowpass (fs, f, 0.707)); }
        else if (b.type == ParametricEQ::highShelf || b.type == ParametricEQ::highCut)
        { listenA.setCoeffs (BiquadCoeffs::highpass (fs, f, 0.707)); listenB.setCoeffs (BiquadCoeffs::highpass (fs, f, 0.707)); }
        else
        {
            const double q = juce::jlimit (0.5, 12.0, (double) b.q);
            listenA.setCoeffs (BiquadCoeffs::bandpass (fs, f, q)); listenB.setCoeffs (BiquadCoeffs::bandpass (fs, f, q));
        }
    }

    ParametricEQ iir;
    FirEQ fir;
    int mode = 0, latency = 0;
    double fs = 48000.0;
    DelayLine dryDelay[2];
    juce::AudioBuffer<float> dryBuf;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix, out, fadeIn, listenMix;
    Biquad listenA, listenB;
    float listenF = -1.0f, listenQ = -1.0f, listenGain = 1.0f;
    int listenType = -1;
};
} // namespace onirique
