#pragma once

#include <algorithm>
#include <cmath>

/**
    Filtre de fin de chaîne : passe-haut ou passe-bande résonant
    (aucune dépendance JUCE, testable seul).

    Filtre à variable d'état en forme TPT : il reste stable quand la fréquence
    est balayée vite. La fréquence et la résonance sont lissées pour éviter les
    bruits de potard ; l'allumage, l'extinction et le changement de type passent
    par un fondu court. Éteint, le signal passe intact, à l'échantillon près.
*/
class OutputFilter
{
public:
    static constexpr double minFrequency = 20.0;
    static constexpr double maxFrequency = 20000.0;

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        mixStep = (float) (1.0 / (0.005 * sr));                 // 5 ms
        smoothing = 1.0 - std::exp (-1.0 / (0.015 * sr));       // ~15 ms
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : state)
            s = {};

        mix = 0.0f;
        typeMix = bandPass ? 1.0f : 0.0f;
        logFrequencyNow = logFrequencyTarget;
        dampingNow = dampingTarget;
    }

    void setActive (bool shouldFilter) noexcept          { active = shouldFilter; }
    void setBandPass (bool isBandPass) noexcept          { bandPass = isBandPass; }

    void setFrequency (double hertz) noexcept
    {
        const double f = std::isnan (hertz) ? 1000.0 : hertz;
        logFrequencyTarget = std::log (std::max (minFrequency, std::min (f, maxFrequency)));
    }

    /** Résonance de 0 (aucune, Q = 0,7) à 1 (forte, Q = 10). */
    void setResonance (double amount) noexcept
    {
        dampingTarget = 1.0 / qualityFor (amount);
    }

    static double qualityFor (double amount) noexcept
    {
        const double a = std::isnan (amount) ? 0.0 : std::max (0.0, std::min (1.0, amount));
        return 0.7071 * std::pow (10.0 / 0.7071, a);
    }

    /** Gain du filtre à une fréquence donnée (pour dessiner sa courbe). */
    static double magnitudeAt (double hertz, double cutoff, double quality, bool isBandPass) noexcept
    {
        const double w = hertz / cutoff;
        const double denominator = std::sqrt ((1.0 - w * w) * (1.0 - w * w) + (w / quality) * (w / quality));

        // Passe-bande : le pic vaut racine(Q), pour que la résonance s'entende sans exploser le niveau.
        return isBandPass ? (w / std::sqrt (quality)) / denominator
                          : (w * w) / denominator;
    }

    /** Traitement en place. */
    void process (float* const* io, int numChannels, int numSamples) noexcept
    {
        const int numCh = std::min (numChannels, maxChannels);
        const double maxCutoff = std::min (maxFrequency, sr * 0.45);

        for (int n = 0; n < numSamples; ++n)
        {
            mix = active ? std::min (1.0f, mix + mixStep) : std::max (0.0f, mix - mixStep);
            typeMix = bandPass ? std::min (1.0f, typeMix + mixStep) : std::max (0.0f, typeMix - mixStep);

            logFrequencyNow += (logFrequencyTarget - logFrequencyNow) * smoothing;
            dampingNow += (dampingTarget - dampingNow) * smoothing;

            const double g = std::tan (pi * std::min (std::exp (logFrequencyNow), maxCutoff) / sr);
            const double k = dampingNow;
            const double a1 = 1.0 / (1.0 + g * (g + k));
            const double a2 = g * a1;
            const double a3 = g * a2;
            const double bandGain = std::sqrt (k);

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& s = state[ch];
                const double x = (double) io[ch][n];

                const double v3 = x - s.ic2;
                const double v1 = a1 * s.ic1 + a2 * v3;
                const double v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
                s.ic1 = 2.0 * v1 - s.ic1;
                s.ic2 = 2.0 * v2 - s.ic2;

                if (mix > 0.0f)
                {
                    const double highPass = x - k * v1 - v2;
                    const double filtered = highPass + (double) typeMix * (v1 * bandGain - highPass);

                    io[ch][n] = mix >= 1.0f ? (float) filtered
                                            : (float) (x + (double) mix * (filtered - x));
                }
            }
        }
    }

private:
    static constexpr int maxChannels = 2;
    static constexpr double pi = 3.14159265358979323846;

    struct State { double ic1 = 0.0, ic2 = 0.0; };
    State state[maxChannels];

    double sr = 44100.0;
    double logFrequencyTarget = 6.907755, logFrequencyNow = 6.907755;   // ln(1000)
    double dampingTarget = 1.0 / 0.7071, dampingNow = 1.0 / 0.7071;
    double smoothing = 1.0;
    float mix = 0.0f, typeMix = 0.0f, mixStep = 1.0f;
    bool active = false, bandPass = false;
};
