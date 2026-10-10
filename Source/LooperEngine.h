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

    Capture « pré » : l'entrée est enregistrée en permanence dans un historique.
    Avec setPreCapture (true), la boucle prend la tranche qui vient de jouer et
    la répète tout de suite, sans passage en direct.

    Anti-clic sans ramollir les transitoires :
      - le raccord de fin de boucle est un fondu de la queue de boucle vers
        les quelques millisecondes d'audio qui PRÉCÉDAIENT le point de départ
        (pré-roll). Le premier échantillon de la boucle est donc toujours
        rejoué à plein niveau : un kick capté sur le temps garde son attaque ;
      - les sauts de position (changement de division) et l'arrêt de la
        boucle passent par un fondu court.

    Pitch, reverse, decay : réglés par répétition (le passage en direct n'est
    jamais touché). Une répétition transposée ou inversée est relue par
    interpolation, avec une courte fenêtre à chaque raccord ; tant que pitch,
    reverse et decay sont à zéro, la répétition reste la copie exacte de
    l'enregistrement.

    Refresh : à intervalle régulier, la boucle est recapturée. Juste avant le
    point de refresh, la boucle en cours est fondue vers ce qui suit (le direct,
    ou le début de la tranche en capture « pré »).

    Calage (snap) : le départ peut être aligné sur une grille fournie par l'hôte.
    Un appui légèrement en retard est rattrapé (la capture est ancrée sur la
    ligne de grille qui vient de passer), sinon le départ attend la suivante.

    Fondus : une enveloppe (fondu d'entrée / de sortie, en fraction de la durée
    de boucle) est appliquée à chaque passage, y compris le premier.

    Tout est alloué dans prepare() : process() ne fait ni allocation ni verrou.
*/
class LooperEngine
{
public:
    void prepare (double sampleRate, int numChannels, double maxLoopSeconds)
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        channels  = std::max (1, numChannels);
        maxFade   = std::max (16, (int) std::lround (0.003 * sr));   // 3 ms
        preRoll   = maxFade;
        maxLoop   = std::max (minLoop * 2, (int) std::ceil (maxLoopSeconds * sr));
        maxAnchor = std::max (16, (int) std::ceil (0.06 * sr));      // retard d'appui rattrapé par le calage : 60 ms
        capacity  = maxLoop + maxAnchor + maxFade + 8;
        historySize = preRoll + maxLoop + maxAnchor + 16;
        gateStep  = 1.0f / (float) maxFade;
        shapeSmoothing = 1.0 - std::exp (-1.0 / (0.02 * sr));   // ~20 ms

        buffer.assign ((std::size_t) channels, std::vector<float> ((std::size_t) (preRoll + capacity), 0.0f));
        history.assign ((std::size_t) channels, std::vector<float> ((std::size_t) historySize, 0.0f));

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
        armed = false;
        leadIn = false;
        historyPos = 0;
        captured = 0;
        elapsed = 0;
        totalElapsed = 0;
        pos = 0;
        iterPos = 0.0;
        refreshPos = 0.0;
        fadeInNow = fadeInTarget;
        fadeOutNow = fadeOutTarget;
        passA = Pass();
        passB = Pass();

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

    /** Transposition ajoutée à chaque répétition, en demi-tons (cumul borné à ±36). Prise en compte au passage suivant. */
    void setPitch (double semitonesPerPass) noexcept
    {
        pitchStep = std::isnan (semitonesPerPass) ? 0.0 : std::max (-24.0, std::min (24.0, semitonesPerPass));
    }

    /** Répétitions lues à l'envers. Prise en compte au passage suivant. */
    void setReverse (bool shouldReverse) noexcept    { reverse = shouldReverse; }

    /** Baisse de niveau à chaque répétition, de 0 (aucune) à 1 (silence dès la première). */
    void setDecay (double amountPerPass) noexcept
    {
        decay = std::isnan (amountPerPass) ? 0.0 : std::max (0.0, std::min (1.0, amountPerPass));
    }

    /** true = la boucle répète tout de suite la tranche qui vient de jouer (pas de passage en direct). */
    void setPreCapture (bool shouldGrabThePast) noexcept    { preCapture = shouldGrabThePast; }

    /**
        Calage du départ sur une grille. À appeler avant chaque process() :
        gridStepInSamples = pas de la grille, samplesSinceGridLine = où en est le
        premier échantillon du bloc par rapport à la dernière ligne de grille.
    */
    void setSnap (bool enabled, double gridStepInSamples, double samplesSinceGridLine) noexcept
    {
        const bool valid = enabled && ! std::isnan (gridStepInSamples) && ! std::isnan (samplesSinceGridLine)
                             && gridStepInSamples >= 16.0;

        snapEnabled = valid;
        snapStep = valid ? gridStepInSamples : 0.0;
        snapPhase = valid ? std::max (0.0, std::min (samplesSinceGridLine, gridStepInSamples)) : 0.0;
        snapFresh = true;
    }

    bool isLooping() const noexcept                  { return state == State::looping; }
    bool isArmed() const noexcept                    { return armed; }
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
                int anchor = 0;

                if (! waitingToStart (n, anchor))
                {
                    idleSample (io, numCh, n);
                    continue;
                }

                startLoop (anchor);
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

            // Raccord vers une répétition transposée ou inversée : la tête en cours s'éteint,
            // la suivante repartira du silence (pas de fondu enchaîné vers le pré-roll).
            const bool dip = fading && fadeIsWrap && passB.vari;

            const Tap tapA = makeTap (pos, passA, canCapture);
            const Tap tapB = (fading && ! dip) ? makeTap (posB, passB, canCapture) : tapA;
            const std::size_t delayed = historyIndexBack (sliceLength (requestedLength));

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float in = io[ch][n];
                auto& buf = buffer[(std::size_t) ch];
                auto& hist = history[(std::size_t) ch];

                hist[(std::size_t) historyPos] = in;

                if (canCapture)
                    buf[(std::size_t) (preRoll + captured)] = in;

                float wet = readTap (buf, tapA);

                if (shaping)
                    wet *= envA;

                if (fading)
                {
                    if (dip)
                    {
                        wet *= 1.0f - fadeGain;
                    }
                    else
                    {
                        const float other = readTap (buf, tapB);
                        wet += fadeGain * ((shaping ? other * envB : other) - wet);
                    }
                }

                if (refreshFading)   // la boucle sortante s'efface vers le début de ce qui suit
                {
                    const float next = refreshToSilence ? 0.0f : (refreshToPast ? hist[delayed] : in);
                    wet += refreshGain * ((shaping ? next * envPreRoll : next) - wet);
                }

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

        snapFresh = false;
    }

private:
    enum class State { idle, looping, releasing };

    static constexpr int minLoop = 64;

    /** Réglages d'un passage, figés à son début : un passage ne change pas de vitesse en route. */
    struct Pass
    {
        double rate = 1.0;      // vitesse de lecture
        bool transposed = false;
        double len = 0.0;       // durée de la tranche relue
        float gain = 1.0f;      // decay
        bool reverse = false;
        bool vari = false;      // transposé ou inversé : lecture interpolée + fenêtre
        int index = 0;          // 0 = passage en direct, 1 = première répétition…
    };

    /** Lecture d'une tête à un instant donné : indices, interpolation, gain. */
    struct Tap
    {
        std::size_t i0 = 0, i1 = 0;
        float frac = 0.0f, gain = 1.0f;
        bool interpolate = false;
    };

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

    /** Nombre d'échantillons de la tranche prise dans le passé en capture « pré ». */
    static int sliceLength (double l) noexcept       { return (int) std::ceil (l); }

    double smoothTowards (double current, double target) const noexcept
    {
        const double delta = target - current;
        return std::abs (delta) < 1.0e-5 ? target : current + delta * shapeSmoothing;
    }

    Pass makePass (int index) const noexcept
    {
        Pass p;
        p.index = index;
        p.len = length;

        if (index > 0)
        {
            const double semitones = std::max (-36.0, std::min (36.0, pitchStep * (double) index));

            if (std::abs (semitones) > 1.0e-9)
            {
                p.rate = std::pow (2.0, semitones / 12.0);
                p.transposed = true;
            }

            p.reverse = reverse;

            if (decay > 0.0)
                p.gain = (float) std::pow (1.0 - decay, (double) index);
        }

        p.vari = p.reverse || p.transposed;
        return p;
    }

    /** true si la première répétition sera transposée ou inversée (elle repartira alors du silence). */
    bool firstRepeatIsVari() const noexcept          { return reverse || std::abs (pitchStep) > 1.0e-9; }

    Tap makeTap (int p, const Pass& pass, bool capturingNow) const noexcept
    {
        Tap tap;
        tap.gain = pass.gain;

        if (! pass.vari)
        {
            tap.i0 = bufferIndex (p, capturingNow);
            return tap;
        }

        // Position dans la tranche : on avance à la vitesse du passage et on reboucle en bout de tranche.
        const double len = std::max ((double) minLoop, pass.len);
        const double out = (double) std::max (0, p);
        const double travelled = pass.rate * out;
        const double inSlice = travelled - len * std::floor (travelled / len);
        const double source = pass.reverse ? std::max (0.0, len - 1.0 - inSlice) : inSlice;
        const double whole = std::floor (source);

        tap.frac = (float) (source - whole);
        tap.interpolate = tap.frac > 0.0f;
        tap.i0 = bufferIndex ((int) whole, capturingNow);
        tap.i1 = bufferIndex ((int) whole + 1, capturingNow);

        // Fenêtre : courte montée après chaque raccord, courte descente avant le suivant.
        const double period = len / pass.rate;                       // durée d'une traversée de la tranche
        const double segment = std::min (len, period);
        const double ramp = std::max (4.0, std::min ((double) maxFade * 0.5, segment * 0.25));
        double window = std::min ((out + 1.0) / ramp, (len - out) / ramp);

        if (pass.rate > 1.0)
        {
            const double sinceWrap = out - period * std::floor (out / period);
            window = std::min (window, std::min ((sinceWrap + 1.0) / ramp, (period - sinceWrap) / ramp));
        }

        tap.gain *= (float) std::max (0.0, std::min (1.0, window));
        return tap;
    }

    static float readTap (const std::vector<float>& buf, const Tap& tap) noexcept
    {
        float v = buf[tap.i0];

        if (tap.interpolate)
            v += tap.frac * (buf[tap.i1] - v);

        return tap.gain < 1.0f ? v * tap.gain : v;   // gain == 1 : copie exacte
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
        if (++historyPos >= historySize)
            historyPos = 0;
    }

    /** Index, dans l'historique, de l'entrée d'il y a "back" échantillons. */
    std::size_t historyIndexBack (int back) const noexcept
    {
        int index = historyPos - std::max (0, std::min (back, historySize - 1));

        if (index < 0)
            index += historySize;

        return (std::size_t) index;
    }

    /** Position de lecture (relative au début de boucle, négative = pré-roll) -> index dans le buffer. */
    std::size_t bufferIndex (int p, bool capturingNow) const noexcept
    {
        const int lastValid = capturingNow ? captured : captured - 1;
        return (std::size_t) (preRoll + std::max (-preRoll, std::min (p, lastValid)));
    }

    //==========================================================================
    /**
        Boucle à l'arrêt : faut-il démarrer à cet échantillon ? Gère le calage sur la grille.
        Renvoie true pour démarrer ; "anchor" = nombre d'échantillons dont la capture est antidatée.
    */
    bool waitingToStart (int n, int& anchor) noexcept
    {
        anchor = 0;

        if (! wantActive)
        {
            armed = false;
            leadIn = false;
            return false;
        }

        if (! snapEnabled)
        {
            armed = false;
            leadIn = false;
            return true;
        }

        double phase = snapPhase + (double) n;

        if (phase >= snapStep)
            phase -= snapStep * std::floor (phase / snapStep);

        if (! armed)
        {
            const double tolerance = std::min (snapStep / 3.0, (double) maxAnchor);

            if (phase <= tolerance)
            {
                anchor = (int) std::lround (phase);   // appui en retard : on s'ancre sur la ligne qui vient de passer
                return true;
            }

            armed = true;
            armCountdown = snapStep - phase;
            leadLength = fadeFor (requestedLength);
            leadIn = preCapture && armCountdown >= (double) leadLength + 1.0;
            return false;
        }

        if (n == 0 && snapFresh)   // début de bloc : on se recale sur la position de l'hôte
            armCountdown = phase < 1.5 ? 0.0 : snapStep - phase;

        if (armCountdown < 0.5)
        {
            armed = false;
            return true;
        }

        return false;
    }

    /** Un échantillon de direct, boucle à l'arrêt (avec le fondu d'approche d'un départ calé en capture « pré »). */
    void idleSample (float* const* io, int numCh, int n) noexcept
    {
        float lead = 0.0f;

        if (armed)
        {
            if (leadIn && armCountdown <= (double) leadLength)
            {
                const float t = 1.0f - (float) std::max (0.0, armCountdown - 1.0) / (float) leadLength;
                lead = t * t * (3.0f - 2.0f * t);
            }

            armCountdown -= 1.0;
        }

        const bool toSilence = firstRepeatIsVari();
        const std::size_t delayed = historyIndexBack (sliceLength (requestedLength));

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& hist = history[(std::size_t) ch];
            const float in = io[ch][n];
            hist[(std::size_t) historyPos] = in;

            if (lead > 0.0f)   // le direct s'efface vers ce qui précède la tranche : elle démarrera à plein niveau
                io[ch][n] = in + lead * ((toSilence ? 0.0f : hist[delayed]) - in);
        }

        advanceHistory();
    }

    /**
        Démarre une capture à l'échantillon courant (activation ou refresh).
        anchor = la capture est antidatée de ce nombre d'échantillons (calage sur la grille).
        En capture « pré », la tranche qui précède est reprise dans l'historique.
    */
    void beginCapture (int anchor) noexcept
    {
        length = requestedLength;

        const int slice = preCapture ? sliceLength (length) : 0;
        const int back = std::max (0, std::min (slice + anchor, std::min (capacity - maxFade - 8, historySize - preRoll - 1)));
        const int count = preRoll + back;
        int start = historyPos - count;

        if (start < 0)
            start += historySize;

        const int first = std::min (count, historySize - start);

        for (std::size_t ch = 0; ch < buffer.size(); ++ch)
        {
            const auto& h = history[ch];
            auto& buf = buffer[ch];

            std::copy (h.begin() + start, h.begin() + start + first, buf.begin());
            std::copy (h.begin(), h.begin() + (count - first), buf.begin() + first);
        }

        captured = back;
        elapsed = back;

        // Position dans le passage : "anchor" échantillons après la ligne de grille.
        const double sinceLine = (double) std::max (0, back - slice);
        const int passesDone = (int) (sinceLine / length);

        iterPos = sinceLine - (double) passesDone * length;
        pos = (int) iterPos;
        fading = false;
        pendingReanchor = false;
        passA = makePass ((slice > 0 ? 1 : 0) + passesDone);
        passB = passA;
    }

    void startLoop (int anchor) noexcept
    {
        beginCapture (anchor);
        totalElapsed = anchor;
        refreshPos = (double) anchor;
        refreshFading = false;
        fadeInNow = fadeInTarget;
        fadeOutNow = fadeOutTarget;
        gate = leadIn ? 1.0f : 0.0f;   // après un fondu d'approche, la boucle démarre à plein niveau
        leadIn = false;
        state = State::looping;
    }

    /** Fondu de la boucle en cours vers ce qui suit, qui se termine pile sur le point de refresh. */
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
        refreshToPast = preCapture;
        refreshToSilence = preCapture && firstRepeatIsVari();
        refreshFading = true;
    }

    void finishRefresh() noexcept
    {
        refreshFading = false;
        refreshPos = refreshCarry;
        beginCapture (0);   // le gate reste ouvert : pas de transition
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
        passB = makePass (passA.index + 1);
        fadeIsWrap = true;
        fadeCarry = carry;
        fading = true;
    }

    /** Fondu vers une autre position de la boucle. */
    void startJumpFade (int target, int passIndex) noexcept
    {
        fadeLength = fadeFor (length);
        fadeCount = 0;
        posB = target;
        passB = makePass (passIndex);
        fadeIsWrap = false;
        fading = true;
    }

    void finishFade() noexcept
    {
        fading = false;
        pos = posB;
        passA = passB;

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
            startJumpFade (target, (int) ((double) elapsed / length));
        }
    }

    //==========================================================================
    std::vector<std::vector<float>> buffer;    // [canal][preRoll + capacity]
    std::vector<std::vector<float>> history;   // [canal][historySize] : l'entrée, en continu

    int channels = 0;
    int maxFade = 16, preRoll = 16, maxLoop = minLoop * 2, maxAnchor = 16, capacity = 0;
    int historySize = 1, historyPos = 0;

    State state = State::idle;
    bool wantActive = false;
    float gate = 0.0f, gateStep = 1.0f;

    double requestedLength = 22050.0;   // dernière durée demandée
    double length = 22050.0;            // durée utilisée par la boucle en cours
    double iterPos = 0.0;               // position dans l'itération courante (fractionnaire)
    std::int64_t elapsed = 0;           // échantillons depuis le début de la capture en cours
    int captured = 0;                   // échantillons enregistrés
    int pos = 0;                        // tête de lecture principale

    Pass passA, passB;                  // réglages du passage de chaque tête
    bool fading = false, fadeIsWrap = false, pendingReanchor = false;
    int posB = 0, fadeLength = 0, fadeCount = 0;
    double fadeCarry = 0.0;

    // Répétitions
    double pitchStep = 0.0, decay = 0.0;
    bool reverse = false, preCapture = false;

    // Refresh
    bool refreshEnabled = false, refreshFading = false, refreshToPast = false, refreshToSilence = false;
    double refreshInterval = 88200.0;   // intervalle demandé
    double refreshPos = 0.0;            // position dans l'intervalle courant (fractionnaire)
    double refreshCarry = 0.0;
    std::int64_t totalElapsed = 0;      // échantillons depuis l'activation de la boucle
    int refreshFadeLength = 0, refreshFadeCount = 0;

    // Calage du départ sur la grille
    bool snapEnabled = false, snapFresh = false, armed = false, leadIn = false;
    double snapStep = 0.0, snapPhase = 0.0, armCountdown = 0.0;
    int leadLength = 0;

    // Fondus par passage (fractions de la durée de boucle)
    double fadeInTarget = 0.0, fadeOutTarget = 0.0;   // demandés
    double fadeInNow = 0.0, fadeOutNow = 0.0;         // lissés
    double shapeSmoothing = 1.0;
};
