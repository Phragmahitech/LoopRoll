#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

/**
    Moteur du looper (aucune dépendance JUCE, testable seul).

    Principe : à l'activation, l'entrée est enregistrée dans un buffer et
    relue en boucle sur une durée donnée. Le premier passage laisse passer
    l'audio en direct (la sortie EST l'enregistrement), les suivants le
    répètent.

    Anti-clic sans ramollir les transitoires :
      - le raccord de fin de boucle est un fondu de la queue de boucle vers
        les quelques millisecondes d'audio qui PRÉCÉDAIENT le point de départ
        (pré-roll). Le premier échantillon de la boucle est donc toujours
        rejoué à plein niveau : un kick capté sur le temps garde son attaque ;
      - les sauts de position (changement de division) et l'arrêt de la
        boucle passent par un fondu court.

    Refresh : à intervalle régulier, la boucle est recapturée. Juste avant le
    point de refresh, la boucle en cours est fondue vers l'entrée en direct ;
    la nouvelle capture démarre donc elle aussi sur un échantillon à plein
    niveau, et le premier passage est de nouveau l'audio en direct.

    Fondus : une enveloppe (fondu d'entrée / de sortie, en fraction de la durée
    de boucle) est appliquée à chaque passage, y compris le premier. Elle est
    calculée par tête de lecture, donc elle reste continue à travers les
    raccords et les refresh.

    Tout est alloué dans prepare() : process() ne fait ni allocation ni verrou.
*/
class LooperEngine
{
public:
    void prepare (double sampleRate, int numChannels, double maxLoopSeconds)
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        channels = std::max (1, numChannels);
        maxFade  = std::max (16, (int) std::lround (0.003 * sr));   // 3 ms
        preRoll  = maxFade;
        maxLoop  = std::max (minLoop * 2, (int) std::ceil (maxLoopSeconds * sr));
        capacity = maxLoop + maxFade + 8;
        gateStep = 1.0f / (float) maxFade;
        shapeSmoothing = 1.0 - std::exp (-1.0 / (0.02 * sr));   // ~20 ms

        buffer.assign ((std::size_t) channels, std::vector<float> ((std::size_t) (preRoll + capacity), 0.0f));
        history.assign ((std::size_t) channels, std::vector<float> ((std::size_t) preRoll, 0.0f));

        requestedLength = clampLength (requestedLength);
        reset();
    }

    /** Coupe toute boucle en cours et vide l'historique. */
    void reset()
    {
        state = State::idle;
        gate = 0.0f;
        fading = false;
        pendingReanchor = false;
        refreshFading = false;
        historyPos = 0;
        captured = 0;
        elapsed = 0;
        totalElapsed = 0;
        pos = 0;
        iterPos = 0.0;
        refreshPos = 0.0;
        fadeInNow = fadeInTarget;
        fadeOutNow = fadeOutTarget;

        for (auto& h : history)
            std::fill (h.begin(), h.end(), 0.0f);
    }

    /** true = capture puis boucle ; false = retour au signal direct. */
    void setActive (bool shouldLoop) noexcept        { wantActive = shouldLoop; }

    /**
        Durée de la boucle en échantillons (peut être fractionnaire : la grille
        reste juste sur la durée, sans dérive).

        reanchor = true  : la position est recalculée comme si cette durée avait
                           été utilisée depuis l'activation (mode sync : la
                           grille rythmique reste calée sur le point de départ).
        reanchor = false : la lecture continue là où elle est ; seule la fin de
                           boucle bouge (mode free, dérive de tempo).
    */
    void setLoopLength (double lengthInSamples, bool reanchor) noexcept
    {
        const double newLength = clampLength (lengthInSamples);
        const bool changed = std::abs (newLength - requestedLength) > 1.0e-9;
        requestedLength = newLength;

        if (state == State::idle)
            return;

        length = newLength;

        if (reanchor && changed)
        {
            if (fading)
                pendingReanchor = true;
            else
                applyReanchor();
        }
    }

    /**
        Refresh périodique : tous les "intervalInSamples", la boucle est recapturée
        avec l'audio du moment. Les points de refresh sont comptés depuis l'activation.

        reanchor = true  : après un changement d'intervalle, les points de refresh
                           restent calés sur la grille de l'activation (mode sync).
        reanchor = false : seul le prochain point de refresh bouge (mode free).

        Un intervalle plus court que la boucle revient à laisser passer le direct.
    */
    void setRefresh (bool enabled, double intervalInSamples, bool reanchor) noexcept
    {
        double newInterval = std::isnan (intervalInSamples) ? 1.0e12 : intervalInSamples;
        newInterval = std::max ((double) (minLoop * 2), std::min (newInterval, 1.0e12));

        const bool changed = std::abs (newInterval - refreshInterval) > 1.0e-9;
        const bool turnedOn = enabled && ! refreshEnabled;

        refreshEnabled = enabled;
        refreshInterval = newInterval;

        // À l'allumage (ou sur changement de division), on se recale sur la grille de l'activation.
        if (state == State::looping && ! refreshFading && (turnedOn || (reanchor && changed)))
            refreshPos = std::fmod ((double) totalElapsed, refreshInterval);
    }

    /**
        Fondus appliqués à chaque passage de la boucle, en fraction de sa durée (0 à 1).
        0 / 0 = boucle brute. Si les deux se chevauchent, l'enveloppe forme un triangle.
    */
    void setFades (double fadeInFraction, double fadeOutFraction) noexcept
    {
        fadeInTarget  = std::isnan (fadeInFraction)  ? 0.0 : std::max (0.0, std::min (1.0, fadeInFraction));
        fadeOutTarget = std::isnan (fadeOutFraction) ? 0.0 : std::max (0.0, std::min (1.0, fadeOutFraction));

        if (state == State::idle)
        {
            fadeInNow = fadeInTarget;
            fadeOutNow = fadeOutTarget;
        }
    }

    bool isLooping() const noexcept                  { return state == State::looping; }
    double getLoopLength() const noexcept            { return requestedLength; }
    int getMaxLoopLength() const noexcept            { return maxLoop; }

    /** Traitement en place. */
    void process (float* const* io, int numChannels, int numSamples) noexcept
    {
        const int numCh = std::min (numChannels, channels);

        if (numCh <= 0 || buffer.empty())
            return;

        for (int n = 0; n < numSamples; ++n)
        {
            if (state == State::idle)
            {
                if (! wantActive)
                {
                    for (int ch = 0; ch < numCh; ++ch)
                        history[(std::size_t) ch][(std::size_t) historyPos] = io[ch][n];

                    advanceHistory();
                    continue;
                }

                startLoop();
            }

            const bool canCapture = captured < capacity;
            float fadeGain = 0.0f;

            if (fading)
            {
                const float t = (float) (fadeCount + 1) / (float) (fadeLength + 1);
                fadeGain = t * t * (3.0f - 2.0f * t);
            }

            float refreshGain = 0.0f;

            if (refreshFading)
            {
                const float t = (float) (refreshFadeCount + 1) / (float) (refreshFadeLength + 1);
                refreshGain = t * t * (3.0f - 2.0f * t);
            }

            // Enveloppe de fondu : lissée pour qu'un mouvement de potard ne claque pas.
            fadeInNow  = smoothTowards (fadeInNow, fadeInTarget);
            fadeOutNow = smoothTowards (fadeOutNow, fadeOutTarget);

            const bool shaping = fadeInNow > 0.0 || fadeOutNow > 0.0;
            float envA = 1.0f, envB = 1.0f, envPreRoll = 1.0f;

            if (shaping)
            {
                envA = envelopeGain (pos);
                envB = fading ? envelopeGain (posB) : envA;
                envPreRoll = envelopeGain (-1);   // niveau juste avant un début de boucle
            }

            const std::size_t indexA = bufferIndex (pos, canCapture);
            const std::size_t indexB = fading ? bufferIndex (posB, canCapture) : indexA;

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float in = io[ch][n];
                auto& buf = buffer[(std::size_t) ch];

                history[(std::size_t) ch][(std::size_t) historyPos] = in;

                if (canCapture)
                    buf[(std::size_t) (preRoll + captured)] = in;

                float wet = buf[indexA];

                if (shaping)
                    wet *= envA;

                if (fading)
                    wet += fadeGain * ((shaping ? buf[indexB] * envB : buf[indexB]) - wet);

                if (refreshFading)   // la boucle sortante s'efface vers le début de la capture suivante
                    wet += refreshGain * ((shaping ? in * envPreRoll : in) - wet);

                // gate == 1 : copie exacte de l'enregistrement (pas d'arrondi flottant)
                io[ch][n] = gate >= 1.0f ? wet : in + gate * (wet - in);
            }

            advanceHistory();

            if (canCapture)
                ++captured;

            ++pos;
            ++elapsed;
            ++totalElapsed;
            iterPos += 1.0;

            if (fading)
            {
                ++posB;

                if (++fadeCount >= fadeLength)
                    finishFade();
            }
            else if (length - iterPos <= (double) fadeFor (length))
            {
                startWrapFade (length - iterPos);
            }

            if (state == State::looping)
            {
                refreshPos += 1.0;

                if (refreshFading)
                {
                    if (++refreshFadeCount >= refreshFadeLength)
                        finishRefresh();
                }
                else if (refreshEnabled && refreshInterval - refreshPos <= (double) fadeFor (refreshInterval))
                {
                    startRefreshFade (refreshInterval - refreshPos);
                }

                if (! wantActive)
                    state = State::releasing;
                else
                    gate = std::min (1.0f, gate + gateStep);
            }

            if (state == State::releasing)
            {
                gate -= gateStep;

                if (gate <= 0.0f)
                {
                    gate = 0.0f;
                    fading = false;
                    pendingReanchor = false;
                    refreshFading = false;
                    state = State::idle;     // si wantActive est revenu, une nouvelle capture démarre à l'échantillon suivant
                }
            }
        }
    }

private:
    enum class State { idle, looping, releasing };

    static constexpr int minLoop = 64;

    double clampLength (double l) const noexcept
    {
        if (std::isnan (l))
            l = (double) minLoop;

        return std::max ((double) minLoop, std::min (l, (double) std::max (minLoop, maxLoop)));
    }

    int fadeFor (double l) const noexcept
    {
        return std::max (8, std::min (maxFade, (int) (l * 0.25)));
    }

    double smoothTowards (double current, double target) const noexcept
    {
        const double delta = target - current;
        return std::abs (delta) < 1.0e-5 ? target : current + delta * shapeSmoothing;
    }

    /**
        Gain de l'enveloppe pour une tête à la position p (négatif = pré-roll, avant le début de boucle).

        Un fondu plus court que le raccord de boucle (3 ms) ne descend pas jusqu'au silence : il part
        d'un niveau plancher qui tend vers 1 quand le fondu tend vers 0. La pente reste ainsi bornée,
        et l'enveloppe rejoint sans saut le cas « pas de fondu ».
    */
    float envelopeGain (int p) const noexcept
    {
        const double minRamp = (double) fadeFor (length);
        double g = 1.0;

        if (fadeInNow > 0.0)
        {
            const double ramp = fadeInNow * length;
            const double rise = p <= 0 ? 0.0 : std::min (1.0, (double) p / ramp);
            const double floorLevel = 1.0 - ramp / minRamp;

            g = floorLevel > 0.0 ? floorLevel + (1.0 - floorLevel) * rise : rise;
        }

        if (fadeOutNow > 0.0)
        {
            const double ramp = fadeOutNow * length;
            const double fall = std::max (0.0, std::min (1.0, (length - (double) p) / ramp));
            const double floorLevel = 1.0 - ramp / minRamp;

            g = std::min (g, floorLevel > 0.0 ? floorLevel + (1.0 - floorLevel) * fall : fall);
        }

        return (float) g;
    }

    void advanceHistory() noexcept
    {
        if (++historyPos >= preRoll)
            historyPos = 0;
    }

    /** Position de lecture (relative au début de boucle, négative = pré-roll) -> index dans le buffer. */
    std::size_t bufferIndex (int p, bool capturingNow) const noexcept
    {
        const int lastValid = capturingNow ? captured : captured - 1;
        return (std::size_t) (preRoll + std::max (-preRoll, std::min (p, lastValid)));
    }

    /** Démarre une capture à l'échantillon suivant (activation ou refresh). */
    void beginCapture() noexcept
    {
        // Le pré-roll = les dernières millisecondes d'entrée, dans l'ordre chronologique.
        for (std::size_t ch = 0; ch < buffer.size(); ++ch)
        {
            const auto& h = history[ch];
            auto& buf = buffer[ch];

            for (int i = 0; i < preRoll; ++i)
                buf[(std::size_t) i] = h[(std::size_t) ((historyPos + i) % preRoll)];
        }

        length = requestedLength;
        captured = 0;
        elapsed = 0;
        pos = 0;
        iterPos = 0.0;
        fading = false;
        pendingReanchor = false;
    }

    void startLoop() noexcept
    {
        beginCapture();
        totalElapsed = 0;
        refreshPos = 0.0;
        refreshFading = false;
        fadeInNow = fadeInTarget;
        fadeOutNow = fadeOutTarget;
        gate = 0.0f;
        state = State::looping;
    }

    /** Fondu de la boucle en cours vers le direct, qui se termine pile sur le point de refresh. */
    void startRefreshFade (double remaining) noexcept
    {
        const int maxLen = fadeFor (refreshInterval);
        const int minLen = std::min (maxLen, std::max (16, maxFade / 4));
        int n = maxLen;
        double carry = 0.0;

        if (remaining >= 0.0)
        {
            n = std::max (minLen, std::min (maxLen, (int) std::lround (remaining)));
            carry = (double) n - remaining;   // fraction d'échantillon reportée -> pas de dérive
        }

        refreshFadeLength = n;
        refreshFadeCount = 0;
        refreshCarry = carry;
        refreshFading = true;
    }

    void finishRefresh() noexcept
    {
        refreshFading = false;
        refreshPos = refreshCarry;
        beginCapture();   // le gate reste ouvert : le premier passage est le direct, sans transition
    }

    /** Fondu de la lecture en cours vers le pré-roll, qui débouche pile sur le début de boucle. */
    void startWrapFade (double remaining) noexcept
    {
        const int maxLen = fadeFor (length);
        const int minLen = std::min (maxLen, std::max (16, maxFade / 4));
        int n = maxLen;
        double carry = 0.0;

        if (remaining >= 0.0)
        {
            // Cas normal : le fondu se termine pile sur la fin de boucle. La fraction
            // d'échantillon restante est reportée sur l'itération suivante -> pas de dérive.
            n = std::max (minLen, std::min (maxLen, (int) std::lround (remaining)));
            carry = (double) n - remaining;
        }
        // Sinon la fin de boucle est déjà dépassée (durée raccourcie) : on repart du début.

        fadeLength = n;
        fadeCount = 0;
        posB = -n;
        fadeIsWrap = true;
        fadeCarry = carry;
        fading = true;
    }

    /** Fondu vers une autre position de la boucle. */
    void startJumpFade (int target) noexcept
    {
        fadeLength = fadeFor (length);
        fadeCount = 0;
        posB = target;
        fadeIsWrap = false;
        fading = true;
    }

    void finishFade() noexcept
    {
        fading = false;
        pos = posB;

        if (fadeIsWrap)
            iterPos = fadeCarry;

        if (pendingReanchor)
        {
            pendingReanchor = false;
            applyReanchor();
        }
    }

    void applyReanchor() noexcept
    {
        const double newIterPos = std::fmod ((double) elapsed, length);
        const int target = (int) newIterPos;

        if (std::abs (target - pos) <= 1)
        {
            iterPos = newIterPos;
            return;
        }

        const double remaining = length - newIterPos;

        if (remaining <= (double) fadeFor (length))
        {
            iterPos = newIterPos;
            startWrapFade (remaining);
        }
        else
        {
            iterPos = newIterPos;
            startJumpFade (target);
        }
    }

    //==========================================================================
    std::vector<std::vector<float>> buffer;    // [canal][preRoll + capacity]
    std::vector<std::vector<float>> history;   // [canal][preRoll] : dernières ms d'entrée

    int channels = 0;
    int maxFade = 16, preRoll = 16, maxLoop = minLoop * 2, capacity = 0;
    int historyPos = 0;

    State state = State::idle;
    bool wantActive = false;
    float gate = 0.0f, gateStep = 1.0f;

    double requestedLength = 22050.0;   // dernière durée demandée
    double length = 22050.0;            // durée utilisée par la boucle en cours
    double iterPos = 0.0;               // position dans l'itération courante (fractionnaire)
    std::int64_t elapsed = 0;           // échantillons depuis le début de la capture en cours
    int captured = 0;                   // échantillons enregistrés
    int pos = 0;                        // tête de lecture principale

    bool fading = false, fadeIsWrap = false, pendingReanchor = false;
    int posB = 0, fadeLength = 0, fadeCount = 0;
    double fadeCarry = 0.0;

    // Refresh
    bool refreshEnabled = false, refreshFading = false;
    double refreshInterval = 88200.0;   // intervalle demandé
    double refreshPos = 0.0;            // position dans l'intervalle courant (fractionnaire)
    double refreshCarry = 0.0;
    std::int64_t totalElapsed = 0;      // échantillons depuis l'activation de la boucle
    int refreshFadeLength = 0, refreshFadeCount = 0;

    // Fondus par passage (fractions de la durée de boucle)
    double fadeInTarget = 0.0, fadeOutTarget = 0.0;   // demandés
    double fadeInNow = 0.0, fadeOutNow = 0.0;         // lissés
    double shapeSmoothing = 1.0;
};
