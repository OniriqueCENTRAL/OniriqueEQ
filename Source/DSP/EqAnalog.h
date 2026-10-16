#pragma once

#include <complex>
#include "ParametricEQ.h"

namespace onirique
{
/*  Réponse ANALOGIQUE exacte des bandes de l'EQ (mêmes prototypes que ParametricEQ::design, mais sans passage au
    numérique) : H(s) évalué en s = j f / f0, jusqu'à Nyquist et au-delà, sans aucun resserrement.

    Sert à fabriquer les filtres FIR des modes NATURAL PHASE (réponse analogique complète : amplitude ET phase,
    celle d'un EQ analogique) et LINEAR PHASE (amplitude analogique seule, phase nulle), et à dessiner leur courbe.
*/
struct EqAnalog
{
    using C = std::complex<double>;

    // une section du second ordre : N(s) / D(s), coefficients [s^0, s^1, s^2], s en unités de 2 pi f0
    static C section (const double n[3], const double d[3], double hz, double f0) noexcept
    {
        const C s (0.0, hz / f0);
        return (n[0] + n[1] * s + n[2] * s * s) / (d[0] + d[1] * s + d[2] * s * s);
    }

    static C stage (int type, double f0, double gainDb, double Q, double hz) noexcept
    {
        Q = juce::jlimit (0.05, 30.0, Q);
        const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
        double n[3] { 1, 0, 0 }, d[3] { 1, 1.0 / Q, 1 };
        switch (type)
        {
            case ParametricEQ::bell:      n[0] = 1; n[1] = A / Q; n[2] = 1;  d[0] = 1; d[1] = 1.0 / (A * Q); d[2] = 1; break;
            case ParametricEQ::lowShelf:  n[0] = A * A; n[1] = A * sA / Q; n[2] = A;  d[0] = 1; d[1] = sA / Q; d[2] = A; break;
            case ParametricEQ::highShelf: n[0] = A; n[1] = A * sA / Q; n[2] = A * A;  d[0] = A; d[1] = sA / Q; d[2] = 1; break;
            case ParametricEQ::lowCut:    n[0] = 0; n[1] = 0; n[2] = 1; break;
            case ParametricEQ::highCut:   n[0] = 1; n[1] = 0; n[2] = 0; break;
            case ParametricEQ::notch:     n[0] = 1; n[1] = 0; n[2] = 1; break;
            default: break;
        }
        return section (n, d, hz, f0);
    }

    // une bande entière (les coupe-bas / coupe-haut : Butterworth en cascade, résonance réglable sur la dernière section)
    static C bandResponse (const ParametricEQ::Band& b, double hz, double gainDb) noexcept
    {
        const double f0 = juce::jlimit (10.0, 30000.0, (double) b.freq);
        if (! ParametricEQ::isCut (b.type))
            return stage (b.type, f0, gainDb, b.q, hz);

        const int N = ParametricEQ::numStages (b.type, b.slope);
        C h (1.0, 0.0);
        for (int k = 0; k < N; ++k)
        {
            double qk = 1.0 / (2.0 * std::cos ((2.0 * k + 1.0) * juce::MathConstants<double>::pi / (4.0 * N)));
            if (k == N - 1) qk *= b.q / 0.70710678118;
            h *= stage (b.type, f0, 0.0, qk, hz);
        }
        return h;
    }
};
} // namespace onirique
