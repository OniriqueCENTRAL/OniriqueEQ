#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "EqAnalog.h"

namespace onirique
{
/*  EQ en FIR (modes NATURAL PHASE et LINEAR PHASE).

    Le filtre n'est pas une approximation numérique de l'EQ : il est calculé directement à partir de la réponse
    ANALOGIQUE exacte des bandes (EqAnalog), échantillonnée finement jusqu'à Nyquist puis ramenée en temps par FFT.

    - NATURAL PHASE : amplitude analogique exacte, avec la phase minimale qui va avec (méthode du cepstre) : la
      phase d'un EQ analogique : aucune pré-résonance, aucune latence, et aucun resserrement des cloches et des shelfs dans l'extrême
      aigu (ce que la transformée bilinéaire ne sait pas éviter). FIR de 16384 coefficients à 44.1 / 48 kHz.
    - LINEAR PHASE : amplitude analogique seule, phase nulle : toutes les fréquences ressortent alignées, le signal
      n'est pas déphasé. FIR symétrique de 32768 coefficients à 44.1 / 48 kHz (fenêtre de Tukey), latence N / 2
      (341 ms à 48 kHz, compensée par le DAW) : assez long pour suivre exactement une cloche étroite ou une coupe raide
      jusqu'à 25 Hz.
    (Les tailles doublent à 88.2 / 96 kHz, quadruplent à 176.4 / 192 kHz : même résolution en Hz.)

    Convolution sans latence ajoutée (juce::dsp::Convolution, partitions non uniformes). Chaque nouveau réglage
    fabrique un nouveau filtre sur un thread à part ; la convolution passe de l'ancien au nouveau en fondu (50 ms),
    sans clic. Le thread audio ne fait que déposer les réglages (verrou tenté, jamais attendu).

    Les bandes DYNAMIQUES ne sont pas dans le FIR (leur gain bouge à chaque instant) : elles restent en IIR, après lui.
*/
class FirEQ : private juce::Thread
{
public:
    enum Kind { naturalPhase = 1, linearPhase = 2 };
    using Bands = std::array<ParametricEQ::Band, ParametricEQ::kBands>;

    FirEQ() : juce::Thread ("ONIRIQUE EQ FIR") {}
    ~FirEQ() override { stopThread (2000); }

    static int rateFactor (double fs) noexcept { return fs > 120000.0 ? 4 : (fs > 60000.0 ? 2 : 1); }
    static int tapsFor (Kind k, double fs) noexcept { return (k == linearPhase ? 32768 : 16384) * rateFactor (fs); }
    static int latencyFor (Kind k, double fs) noexcept { return k == linearPhase ? tapsFor (k, fs) / 2 : 0; }

    // thread de configuration (audio arrêté) : premier filtre calculé tout de suite, actif dès le premier bloc
    void prepare (Kind k, double sampleRate, int maxBlock, int numChannels, const Bands& bands)
    {
        stopThread (2000);
        kind = k; fs = sampleRate;
        taps = tapsFor (k, fs);
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            pending = bands;
        }
        sent = bands;
        dirty.store (false);
        conv.loadImpulseResponse (design (bands), fs, juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        conv.prepare ({ fs, (juce::uint32) juce::jmax (1, maxBlock), (juce::uint32) juce::jmax (1, numChannels) });
        conv.reset();
        startThread (juce::Thread::Priority::normal);
    }

    void release() { stopThread (2000); }

    int getTaps() const noexcept { return taps; }
    int getLatency() const noexcept { return latencyFor (kind, fs); }

    // thread audio : nouveaux réglages (seules les bandes statiques comptent)
    void setBands (const Bands& b) noexcept
    {
        if (sameStatic (b, sent)) return;
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (! sl.isLocked()) return;   // le thread de calcul lit : on redéposera au prochain bloc
        pending = b;
        sent = b;
        dirty.store (true, std::memory_order_release);
    }

    void process (juce::dsp::AudioBlock<float>& block) noexcept
    {
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        conv.process (ctx);
    }

    // une bande statique (dans le FIR) : allumée et pas dynamique
    static bool inFir (const ParametricEQ::Band& b) noexcept { return b.on && ! (b.dyn && ParametricEQ::canBeDynamic (b.type)); }

    // le filtre de ces réglages (public : sert aux tests)
    juce::AudioBuffer<float> design (const Bands& bands) const
    {
        const int N = taps;
        // grille de fréquence bien plus fine que le filtre : 4 x en LINEAR, 8 x en NATURAL (le cepstre se replie sinon)
        const int M = N * (kind == linearPhase ? 4 : 8);
        const int order = juce::roundToInt (std::log2 ((double) M));
        juce::dsp::FFT fft (order);
        std::vector<std::complex<double>> H ((size_t) M);

        // amplitude analogique exacte sur 0..Nyquist, en double précision
        for (int k = 0; k <= M / 2; ++k)
        {
            const double hz = fs * (double) k / (double) M;
            std::complex<double> h (1.0, 0.0);
            for (const auto& b : bands)
                if (inFir (b)) h *= EqAnalog::bandResponse (b, std::max (hz, 1.0e-3), b.gainDb);
            H[(size_t) k] = std::abs (h);
        }

        juce::AudioBuffer<float> ir (1, N);
        auto* out = ir.getWritePointer (0);
        if (kind == linearPhase)
        {
            // phase nulle : h = IFFT (|H|), centré sur 0, décalé de N / 2. Fenêtre de Tukey : plate sur les 70 %
            // du milieu (la résonance d'une bande étroite dans le grave est gardée entière), arrondie aux deux bouts
            const auto h = inverseReal (fft, H, M);
            const double flat = 0.70;
            for (int n = 0; n < N; ++n)
            {
                const int m = n - N / 2;                                          // -N/2 .. N/2-1
                const double r = std::abs ((double) m) / (double) (N / 2);        // 0 au centre, 1 aux bords
                const double w = r <= flat ? 1.0 : 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * (r - flat) / (1.0 - flat));
                out[n] = (float) (h[(size_t) ((m + M) % M)] * w);
            }
            return ir;
        }

        // phase minimale (méthode du cepstre) : la phase qui va avec cette amplitude, celle d'un EQ analogique.
        // log |H| -> cepstre -> repli causal -> exp -> réponse à phase minimale
        std::vector<std::complex<double>> L ((size_t) M);
        for (int k = 0; k <= M / 2; ++k) L[(size_t) k] = std::log (std::max (1.0e-9, H[(size_t) k].real()));   // plancher -180 dB (fonds de coupe)
        const auto c = inverseReal (fft, L, M);
        std::vector<std::complex<double>> F ((size_t) M);
        {
            std::vector<std::complex<float>> in ((size_t) M), spec ((size_t) M);
            in[0] = (float) c[0];
            for (int n = 1; n < M / 2; ++n) in[(size_t) n] = (float) (2.0 * c[(size_t) n]);
            in[(size_t) M / 2] = (float) c[(size_t) M / 2];
            fft.perform (in.data(), spec.data(), false);
            for (int k = 0; k <= M / 2; ++k) F[(size_t) k] = std::exp (std::complex<double> (spec[(size_t) k]));
        }
        const auto h = inverseRealComplex (fft, F, M);
        const int fade = N / 8;   // la queue (déjà éteinte) finit en douceur sur le dernier huitième
        for (int n = 0; n < N; ++n)
        {
            double w = 1.0;
            if (n >= N - fade) w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * (double) (n - (N - fade)) / (double) fade);
            out[n] = (float) (h[(size_t) n] * w);
        }
        return ir;
    }

private:
    static bool sameStatic (const Bands& a, const Bands& b) noexcept
    {
        for (size_t i = 0; i < a.size(); ++i)
        {
            const bool ia = inFir (a[i]), ib = inFir (b[i]);
            if (ia != ib) return false;
            if (! ia) continue;
            if (a[i].type != b[i].type || a[i].freq != b[i].freq || a[i].gainDb != b[i].gainDb || a[i].q != b[i].q
                || (ParametricEQ::isCut (a[i].type) && a[i].slope != b[i].slope)) return false;
        }
        return true;
    }

    // spectre d'un signal réel (points 0..M/2 remplis) -> signal (M points) ; FFT inverse normalisée de JUCE
    static std::vector<double> inverseReal (juce::dsp::FFT& fft, const std::vector<std::complex<double>>& half, int M)
    {
        std::vector<std::complex<float>> in ((size_t) M), outv ((size_t) M);
        for (int k = 0; k <= M / 2; ++k) in[(size_t) k] = std::complex<float> (half[(size_t) k]);
        for (int k = 1; k < M / 2; ++k) in[(size_t) (M - k)] = std::conj (in[(size_t) k]);   // symétrie hermitienne
        fft.perform (in.data(), outv.data(), true);
        std::vector<double> r ((size_t) M);
        for (int n = 0; n < M; ++n) r[(size_t) n] = outv[(size_t) n].real();
        return r;
    }
    static std::vector<double> inverseRealComplex (juce::dsp::FFT& fft, const std::vector<std::complex<double>>& half, int M)
    {
        return inverseReal (fft, half, M);
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            wait (8);
            if (! dirty.exchange (false, std::memory_order_acquire)) continue;
            Bands b;
            {
                const juce::SpinLock::ScopedLockType sl (lock);
                b = pending;
            }
            auto ir = design (b);
            if (threadShouldExit()) break;
            conv.loadImpulseResponse (std::move (ir), fs, juce::dsp::Convolution::Stereo::no,
                                      juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        }
    }

    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 512 } };
    Kind kind = naturalPhase;
    double fs = 48000.0;
    int taps = 16384;
    juce::SpinLock lock;
    Bands pending {}, sent {};
    std::atomic<bool> dirty { false };
};
} // namespace onirique
