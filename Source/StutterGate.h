#pragma once

#include <algorithm>
#include <cmath>

/**
    Stutter : gate rythmique à pas (aucune dépendance JUCE, testable seul).

    Chaque pas ouvre le son d'un coup puis le referme en dent de scie
    descendante. Le réglage de forme courbe cette descente :
      - 0   : le niveau tient presque jusqu'au bout du pas (quasi carré) ;
      - 0,5 : descente linéaire ;
      - 1   : le niveau chute tout de suite (pas très fermé, juste une pointe).

    Les pas suivent un motif de 16 cases : une case éteinte reste fermée.
    Le swing allonge les pas pairs et raccourcit les impairs. La profondeur
    règle jusqu'où le gate referme (1 = silence, 0 = sans effet).

    L'ouverture et la toute fin du pas sont adoucies sur 1 à 2 ms pour ne pas
    claquer. Éteint, le signal passe intact, à l'échantillon près.
*/
class StutterGate
{
public:
    static constexpr int numSteps = 16;

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        mixStep = (float) (1.0 / (0.003 * sr));                  // 3 ms pour entrer / sortir
        smoothing = 1.0 - std::exp (-1.0 / (0.02 * sr));         // ~20 ms
        reset();
    }

    void reset() noexcept
    {
        mix = 0.0f;
        position = 0.0;
        shapeNow = shapeTarget;
        depthNow = depthTarget;
        swingNow = swingTarget;
        wasActive = false;
    }

    void setActive (bool shouldGate) noexcept            { active = shouldGate; }

    /** Durée d'un pas en échantillons (avant swing). */
    void setStepLength (double lengthInSamples) noexcept
    {
        const double l = std::isnan (lengthInSamples) ? 4410.0 : lengthInSamples;
        stepLength = std::max (32.0, std::min (l, 1.0e9));
    }

    /** Forme de la descente, 0 (quasi carré) à 1 (très fermé). */
    void setShape (double shape) noexcept                { shapeTarget = unit (shape, 0.5); }

    /** Profondeur, 0 (le gate ne change rien) à 1 (il referme jusqu'au silence). */
    void setDepth (double depth) noexcept                { depthTarget = unit (depth, 1.0); }

    /** Swing, 0 (pas égaux) à 1 (le pas pair dure trois fois le pas impair). */
    void setSwing (double swing) noexcept                { swingTarget = unit (swing, 0.0); }

    /** Motif : le bit i allume le pas i (16 pas). */
    void setPattern (unsigned int stepMask) noexcept     { pattern = stepMask & 0xffffu; }

    /**
        Cale le gate sur une position donnée, comptée en pas (la partie entière donne
        le pas du motif, la partie décimale l'avancement dans le pas). À appeler au
        début de chaque bloc quand le tempo de l'hôte défile : les pas tombent alors
        sur sa grille. Sans cet appel, le gate tourne librement et repart du premier
        pas à l'activation.
    */
    void syncPosition (double positionInSteps) noexcept
    {
        if (std::isnan (positionInSteps))
            return;

        position = positionInSteps;
        hostLocked = true;
    }

    /** Niveau du gate à une position donnée du pas (pour l'affichage aussi). */
    static double shapeGain (double phase, double shape) noexcept
    {
        const double remaining = std::max (0.0, 1.0 - phase);
        return std::pow (remaining, std::pow (16.0, 2.0 * shape - 1.0));
    }

    struct Step
    {
        int index = 0;          // pas du motif, 0 à 15
        double phase = 0.0;     // avancement dans le pas, 0 à 1
        double length = 1.0;    // durée du pas, relative à un pas sans swing
    };

    /** Durée relative du premier pas de chaque paire (le second dure 2 moins cette valeur). */
    static double firstOfPair (double swing) noexcept    { return 1.0 + 0.5 * std::max (0.0, std::min (1.0, swing)); }

    /** Où se trouve une position (comptée en pas) : numéro du pas, avancement, durée du pas. */
    static Step locate (double positionInSteps, double swing) noexcept
    {
        const double pair = std::floor (positionInSteps * 0.5);
        const double inPair = positionInSteps - 2.0 * pair;      // 0 à 2
        const double first = firstOfPair (swing);
        const int pairInPattern = (int) (pair - 8.0 * std::floor (pair / 8.0));

        Step step;

        if (inPair < first)
        {
            step.index = 2 * pairInPattern;
            step.phase = inPair / first;
            step.length = first;
        }
        else
        {
            step.index = 2 * pairInPattern + 1;
            step.phase = (inPair - first) / (2.0 - first);
            step.length = 2.0 - first;
        }

        step.index = std::max (0, std::min (numSteps - 1, step.index));
        return step;
    }

    /** Traitement en place. */
    void process (float* const* io, int numChannels, int numSamples) noexcept
    {
        if (active && ! wasActive && mix <= 0.0f && ! hostLocked)
            position = 0.0;   // en roue libre, l'activation démarre sur le premier pas

        wasActive = active;

        const double increment = 1.0 / stepLength;

        for (int n = 0; n < numSamples; ++n)
        {
            mix = active ? std::min (1.0f, mix + mixStep) : std::max (0.0f, mix - mixStep);

            shapeNow = smooth (shapeNow, shapeTarget);
            depthNow = smooth (depthNow, depthTarget);
            swingNow = smooth (swingNow, swingTarget);

            if (mix > 0.0f)
            {
                const Step step = locate (position, swingNow);
                double gain = 0.0;

                if ((pattern >> step.index) & 1u)
                {
                    const double length = stepLength * step.length;
                    const double elapsed = step.phase * length;
                    const double remaining = length - elapsed;
                    const double attackLength = std::min (0.0015 * sr, length * 0.1);
                    const double releaseLength = std::min (0.002 * sr, length * 0.2);

                    gain = shapeGain (step.phase, shapeNow);
                    gain = std::min (gain, std::min (1.0, remaining / releaseLength));   // fin de pas : retombée d'au moins 2 ms
                    gain *= std::min (1.0, elapsed / attackLength);                      // ouverture adoucie
                }

                const float applied = 1.0f + mix * (float) (depthNow * (gain - 1.0));

                for (int ch = 0; ch < numChannels; ++ch)
                    io[ch][n] *= applied;
            }

            position += increment;

            if (position >= (double) numSteps)   // le motif fait 16 pas : on reste dans cette plage en roue libre
                position -= (double) numSteps * std::floor (position / (double) numSteps);
        }

        hostLocked = false;
    }

private:
    static double unit (double value, double fallback) noexcept
    {
        return std::isnan (value) ? fallback : std::max (0.0, std::min (1.0, value));
    }

    double smooth (double current, double target) const noexcept
    {
        const double delta = target - current;
        return std::abs (delta) < 1.0e-5 ? target : current + delta * smoothing;
    }

    double sr = 44100.0;
    double stepLength = 4410.0;
    double position = 0.0;      // en pas
    double shapeTarget = 0.5, shapeNow = 0.5;
    double depthTarget = 1.0, depthNow = 1.0;
    double swingTarget = 0.0, swingNow = 0.0;
    double smoothing = 1.0;
    unsigned int pattern = 0xffffu;
    float mix = 0.0f, mixStep = 1.0f;
    bool active = false, wasActive = false, hostLocked = false;
};
