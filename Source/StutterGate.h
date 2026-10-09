#pragma once

#include <algorithm>
#include <cmath>

/**
    Stutter : gate rythmique (aucune dépendance JUCE, testable seul).

    Chaque pas ouvre le son d'un coup puis le referme en dent de scie
    descendante. Le réglage de forme courbe cette descente :
      - 0   : le niveau tient presque jusqu'au bout du pas (quasi carré) ;
      - 0,5 : descente linéaire ;
      - 1   : le niveau chute tout de suite (pas très fermé, juste une pointe).

    L'ouverture et la toute fin du pas sont adoucies sur 1 à 2 ms pour ne pas
    claquer. Éteint, le signal passe intact, à l'échantillon près.
*/
class StutterGate
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        mixStep = (float) (1.0 / (0.003 * sr));                       // 3 ms pour entrer / sortir
        shapeSmoothing = 1.0 - std::exp (-1.0 / (0.02 * sr));         // ~20 ms
        reset();
    }

    void reset() noexcept
    {
        mix = 0.0f;
        phase = 0.0;
        shapeNow = shapeTarget;
        wasActive = false;
    }

    void setActive (bool shouldGate) noexcept            { active = shouldGate; }

    /** Durée d'un pas en échantillons. */
    void setStepLength (double lengthInSamples) noexcept
    {
        const double l = std::isnan (lengthInSamples) ? 4410.0 : lengthInSamples;
        stepLength = std::max (32.0, std::min (l, 1.0e9));
    }

    /** Forme de la descente, 0 (quasi carré) à 1 (très fermé). */
    void setShape (double shape) noexcept
    {
        shapeTarget = std::isnan (shape) ? 0.5 : std::max (0.0, std::min (1.0, shape));
    }

    /**
        Cale le pas en cours sur une position donnée (0 à 1). À appeler au début de
        chaque bloc quand le tempo de l'hôte défile : les pas tombent alors sur sa grille.
        Sans cet appel, le gate tourne librement et repart d'un pas plein à l'activation.
    */
    void syncPhase (double phase01) noexcept
    {
        phase = phase01 - std::floor (phase01);
        hostLocked = true;
    }

    /** Niveau du gate à une position donnée du pas (pour l'affichage aussi). */
    static double shapeGain (double position, double shape) noexcept
    {
        const double remaining = std::max (0.0, 1.0 - position);
        return std::pow (remaining, std::pow (16.0, 2.0 * shape - 1.0));
    }

    /** Traitement en place. */
    void process (float* const* io, int numChannels, int numSamples) noexcept
    {
        if (active && ! wasActive && mix <= 0.0f && ! hostLocked)
            phase = 0.0;   // en roue libre, l'activation démarre sur un pas plein

        wasActive = active;

        const double increment = 1.0 / stepLength;
        const double attackLength = std::min (0.0015 * sr, stepLength * 0.1);
        const double releaseLength = std::min (0.002 * sr, stepLength * 0.2);

        for (int n = 0; n < numSamples; ++n)
        {
            mix = active ? std::min (1.0f, mix + mixStep) : std::max (0.0f, mix - mixStep);

            const double delta = shapeTarget - shapeNow;
            shapeNow = std::abs (delta) < 1.0e-5 ? shapeTarget : shapeNow + delta * shapeSmoothing;

            if (mix > 0.0f)
            {
                const double position = phase * stepLength;
                const double remaining = stepLength - position;

                double gain = shapeGain (phase, shapeNow);
                gain = std::min (gain, std::min (1.0, remaining / releaseLength));   // fin de pas : retombée d'au moins 2 ms
                gain *= std::min (1.0, position / attackLength);                     // ouverture adoucie

                const float applied = 1.0f + mix * ((float) gain - 1.0f);

                for (int ch = 0; ch < numChannels; ++ch)
                    io[ch][n] *= applied;
            }

            phase += increment;

            if (phase >= 1.0)
                phase -= std::floor (phase);
        }

        hostLocked = false;
    }

private:
    double sr = 44100.0;
    double stepLength = 4410.0;
    double phase = 0.0;
    double shapeTarget = 0.5, shapeNow = 0.5, shapeSmoothing = 1.0;
    float mix = 0.0f, mixStep = 1.0f;
    bool active = false, wasActive = false, hostLocked = false;
};
