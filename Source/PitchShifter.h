#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

//==============================================================================
/**
    Pitch-shifter temps réel (aucune dépendance JUCE, testable seul).

    Le son est relu dans une courte mémoire par deux têtes qui avancent plus
    vite que le direct (vers l'aigu) ou plus lentement (vers le grave). Chaque
    tête joue un « grain » de 30 ms sous une fenêtre de Hann ; les grains se
    chevauchent de moitié, la somme des deux fenêtres vaut toujours 1.

    Une tête qui lit plus vite que le direct finit par le rattraper, une tête
    qui lit plus lentement prend du retard : il faut régulièrement la faire
    sauter en arrière ou en avant. Tant que ce n'est pas nécessaire, le nouveau
    grain prolonge exactement le précédent (aucun raccord). Quand il faut
    sauter, on cherche dans les 30 ms disponibles l'endroit qui ressemble le
    plus à ce que joue l'autre tête : sur un son tenu, le saut tombe sur un
    nombre entier de périodes et ne s'entend pas.

    La durée du son est conservée et aucune latence n'est déclarée : le son
    transposé sort avec un léger retard variable (quelques ms pour un petit
    écart, 10 à 20 ms en moyenne à l'octave, une soixantaine de ms dans le pire
    cas : son très grave transposé d'une octave vers le haut). À 0 demi-ton, le
    signal passe tel quel, à l'échantillon près.

    Limite de la méthode : un raccord ne peut être en phase que pour des sons
    de même période. Sur plusieurs sons sans rapport harmonique joués ensemble,
    il l'est pour le plus fort ; les autres deviennent un peu rugueux.
*/
class PitchShifter
{
public:
    static constexpr double maxSemitones = 24.0;

    void prepare (double sampleRate, int numChannels)
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        hop = std::max (16, (int) std::lround (0.015 * sr));      // grains de 30 ms, un départ toutes les 15 ms
        grain = 2 * hop;
        span = std::max (8, (int) std::lround (0.030 * sr));      // recherche du raccord : 30 ms (une période à 33 Hz)
        match = std::max (8, (int) std::lround (0.010 * sr));     // longueur comparée : 10 ms
        neighbourReach = 0.008 * sr;
        minimumJump = 0.003 * sr;
        decimation = std::max (1, (int) std::lround (sr / 12000.0));

        const double maxRatio = std::pow (2.0, maxSemitones / 12.0) * headroom;
        // Le plus loin qu'on relit : le recul d'un grain à la vitesse maximale, la zone de recherche (deux fois :
        // pour les candidats, puis pour comparer le son à lui-même), un grain de retard et la longueur comparée.
        const int needed = (int) std::ceil ((maxRatio - 1.0) * (double) grain) + 2 * span + grain + match + 4 * decimation + 64;

        size = 256;

        while (size < needed)
            size *= 2;

        mask = (std::int64_t) size - 1;

        window.resize ((size_t) grain);

        for (int i = 0; i < grain; ++i)
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * ((double) i + 0.5) / (double) grain));

        const auto channels = (size_t) std::max (1, numChannels);
        rings.assign (channels, std::vector<float> ((size_t) size, 0.0f));
        mono.assign ((size_t) size, 0.0f);
        frame.assign (channels, 0.0f);

        wetStep = (float) (1.0 / (0.005 * sr));                   // 5 ms pour entrer / sortir
        smoothing = 1.0 - std::exp (-1.0 / (0.005 * sr));         // ~5 ms : pas d'escalier sur une automation
        reset();
    }

    void reset() noexcept
    {
        for (auto& ring : rings)
            std::fill (ring.begin(), ring.end(), 0.0f);

        std::fill (mono.begin(), mono.end(), 0.0f);

        written = size;
        engaged = false;
        wet = 0.0f;
        current = target;
        ratio = std::exp2 (current / 12.0);
    }

    /** Transposition en demi-tons (valeurs décimales admises). 0 = le signal passe tel quel. */
    void setSemitones (double semitones) noexcept
    {
        target = std::isfinite (semitones) ? std::max (-maxSemitones, std::min (maxSemitones, semitones)) : 0.0;
    }

    bool isActive() const noexcept      { return engaged; }

    /** Traite le bloc sur place. */
    void process (float* const* io, int numChannels, int numSamples) noexcept
    {
        const int numCh = std::min (numChannels, (int) rings.size());

        if (numCh <= 0 || numSamples <= 0)
            return;

        const bool wanted = std::abs (target) > 1.0e-9;
        const double targetRatio = std::exp2 (target / 12.0);

        for (int n = 0; n < numSamples; ++n)
        {
            // La mémoire enregistre en permanence : la transposition peut démarrer à tout moment.
            const auto slot = (size_t) (written & mask);
            float sum = 0.0f;

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float in = io[ch][n];
                rings[(size_t) ch][slot] = in;
                sum += in;
            }

            mono[slot] = sum;
            ++written;

            if (! engaged)
            {
                if (! wanted)
                    continue;   // le direct passe tel quel

                engage (targetRatio);
            }

            const double delta = target - current;

            if (std::abs (delta) > 0.0)
            {
                current = std::abs (delta) < 1.0e-4 ? target : current + delta * smoothing;
                ratio = std::exp2 (current / 12.0);
            }

            const std::int64_t newest = written - 1;
            const double latestReadable = (double) (newest - 2);                 // l'interpolation lit deux échantillons plus loin
            const double earliestReadable = (double) (newest - size + 4);

            std::fill (frame.begin(), frame.end(), 0.0f);

            for (auto& g : grains)
            {
                // Garde-fou : une tête ne dépasse jamais le direct (montée de pitch très rapide en cours de grain).
                g.position = std::max (earliestReadable, std::min (latestReadable, g.position));

                const double whole = std::floor (g.position);
                const auto index = (std::int64_t) whole;
                const float fraction = (float) (g.position - whole);
                const float gain = window[(size_t) std::min (g.age, grain - 1)];

                for (int ch = 0; ch < numCh; ++ch)
                    frame[(size_t) ch] += gain * interpolate (rings[(size_t) ch].data(), index, fraction);

                g.position += ratio;
                ++g.age;
            }

            // Deux grains qui ne se ressemblent pas (bruit, percussions) s'additionnent en puissance, pas en
            // amplitude : sans correction, le niveau se creuserait à chaque raccord.
            if (coherence < 1.0)
            {
                const double w = (double) window[(size_t) std::min (grains[0].age - 1, grain - 1)];
                const auto makeUp = (float) (1.0 / std::sqrt (1.0 - 2.0 * w * (1.0 - w) * (1.0 - coherence)));

                for (int ch = 0; ch < numCh; ++ch)
                    frame[(size_t) ch] *= makeUp;
            }

            if (--untilNext <= 0)
            {
                startGrain (nextSlot, targetRatio);
                nextSlot = 1 - nextSlot;
                untilNext = hop;
            }

            wet = wanted ? std::min (1.0f, wet + wetStep) : std::max (0.0f, wet - wetStep);

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float dry = io[ch][n];
                io[ch][n] = wet >= 1.0f ? frame[(size_t) ch] : dry + wet * (frame[(size_t) ch] - dry);
            }

            if (! wanted && wet <= 0.0f)
                engaged = false;
        }
    }

private:
    struct Grain
    {
        double position;   // position de lecture dans la mémoire, en échantillons depuis le début
        int age;           // échantillons joués depuis le départ du grain
    };

    /** Interpolation cubique (Catmull-Rom) entre les échantillons index et index + 1. */
    float interpolate (const float* ring, std::int64_t index, float fraction) const noexcept
    {
        const float y0 = ring[(size_t) ((index - 1) & mask)];
        const float y1 = ring[(size_t) (index & mask)];
        const float y2 = ring[(size_t) ((index + 1) & mask)];
        const float y3 = ring[(size_t) ((index + 2) & mask)];

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * fraction + c2) * fraction + c1) * fraction + y1;
    }

    /** Position la plus récente d'où un grain peut partir sans rattraper le direct avant sa fin. */
    std::int64_t latestStart (double targetRatio, int remaining) const noexcept
    {
        const double ahead = std::max (ratio, targetRatio) * headroom;
        return written - 3 - (std::int64_t) std::ceil (std::max (0.0, ahead - 1.0) * (double) remaining);
    }

    void engage (double targetRatio) noexcept
    {
        current = target;
        ratio = targetRatio;

        // Deux grains d'emblée, au même endroit, l'un à mi-course : la somme des fenêtres vaut 1 dès le premier échantillon.
        const auto start = (double) latestStart (targetRatio, grain);
        grains[0] = { start, hop };
        grains[1] = { start, 0 };
        coherence = 1.0;

        nextSlot = 0;
        untilNext = hop;
        wet = 0.0f;
        engaged = true;
    }

    void startGrain (int slot, double targetRatio) noexcept
    {
        const double other = std::min (grains[1 - slot].position, (double) (written - 1));
        const auto start = chooseStart (other, latestStart (targetRatio, grain), std::max (ratio, targetRatio) < 1.0);

        grains[slot] = { start.position, 0 };
        coherence = start.coherence;
    }

    /** Ressemblance entre les "count" échantillons (pris tous les "step") qui précèdent a et ceux qui précèdent b. */
    double similarity (std::int64_t a, std::int64_t b, int count, int step) const noexcept
    {
        const float* m = mono.data();
        double dot = 0.0, energyA = 0.0, energyB = 0.0;

        for (int j = 0; j < count; ++j)
        {
            const double x = (double) m[(size_t) ((a - (std::int64_t) j * step) & mask)];
            const double y = (double) m[(size_t) ((b - (std::int64_t) j * step) & mask)];
            dot += x * y;
            energyA += x * x;
            energyB += y * y;
        }

        if (energyA < 1.0e-10 * (double) count)
            return -2.0;   // silence : rien à comparer

        return dot / std::sqrt (energyA * energyB + 1.0e-20);
    }

    /** Parcours des candidats au départ d'un grain, du plus proche de l'autre tête au plus éloigné. */
    struct Scan
    {
        std::int64_t first = 0;     // premier candidat
        std::int64_t step = 1;      // pas (négatif : on recule dans la mémoire)
        int count = 0;              // nombre de candidats
        bool skipNeighbours = true; // écarter d'abord les voisins immédiats de l'autre tête
        double towardsFirst = 0.0;  // préférence pour les premiers candidats (négatif : pour les derniers)
    };

    struct Found
    {
        std::int64_t position = 0;
        double alike = -2.0;        // -2 : aucun candidat retenu
        bool atEnd = false;         // le meilleur candidat est le dernier parcouru : le vrai maximum est peut-être au-delà
    };

    /**
        Le candidat qui ressemble le plus à ce que joue l'autre tête (qui en est à "reference").

        Les voisins immédiats de l'autre tête sont écartés d'abord : à quelques échantillons près
        le son se ressemble toujours, mais un si petit saut, répété à chaque grain, annulerait la
        transposition au lieu de la raccorder.
    */
    Found search (std::int64_t reference, const Scan& scan) const noexcept
    {
        const int coarseCount = std::max (1, match / decimation);
        bool neighbour = scan.skipNeighbours;
        Found found;
        double bestScore = -1.0e9;

        for (int k = 0; k < scan.count; ++k)
        {
            const auto candidate = scan.first + scan.step * k;
            const double alike = similarity (reference, candidate, coarseCount, decimation);

            if (neighbour)
            {
                if (alike >= neighbourThreshold)
                    continue;

                neighbour = false;
            }

            const double score = alike - scan.towardsFirst * (double) k / (double) std::max (1, scan.count - 1);

            if (score > bestScore)
            {
                bestScore = score;
                found = { candidate, alike, k == scan.count - 1 };
            }
        }

        if (found.alike > -1.5 && decimation > 1)   // affinage à l'échantillon près autour du meilleur candidat
        {
            const auto last = scan.first + scan.step * (scan.count - 1);
            const auto low = std::max (std::min (scan.first, last), found.position - (decimation - 1));
            const auto high = std::min (std::max (scan.first, last), found.position + (decimation - 1));
            bestScore = -1.0e9;

            for (auto candidate = low; candidate <= high; ++candidate)
            {
                const double alike = similarity (reference, candidate, match, 1);

                if (alike > bestScore)
                {
                    bestScore = alike;
                    found.position = candidate;
                    found.alike = alike;
                }
            }
        }

        return found;
    }

    struct Start
    {
        double position;
        double coherence;   // ressemblance avec l'autre tête au raccord, de 0 (aucune) à 1 (identique)
    };

    /**
        D'où faire partir un grain, sachant que l'autre tête en est à "other" et qu'on ne peut pas
        partir plus tard que "latest" (sinon le grain rattraperait le direct avant sa fin).

        Vers l'aigu, l'autre tête finit trop proche du direct : il faut reculer. On prend, parmi les
        30 ms qui précèdent "latest", le plus petit recul qui tombe en phase.

        Vers le grave ("falling"), l'autre tête prend du retard : il faut de temps en temps avancer.
        - Son tenu (périodique) : on avance d'un nombre entier de périodes dès que c'est possible.
        - Son sans hauteur nette (percussions, bruit) : on revient au plus près du direct dès que
          le retard dépasse quelques ms, pour ne laisser tomber que de très courts morceaux.

        Entre deux sauts, le nouveau grain part exactement de "other" : il prolonge l'autre, sans raccord.
    */
    Start chooseStart (double other, std::int64_t latest, bool falling) const noexcept
    {
        const double whole = std::floor (other);
        const auto reference = (std::int64_t) whole;
        const double fraction = other - whole;
        const double behind = (double) latest - other;      // > 0 : l'autre tête est en retard sur ce qu'on peut lire
        const int coarseCount = std::max (1, match / decimation);
        const int steps = span / decimation;

        const auto at = [=] (const Found& found)
        {
            return Start { std::min ((double) latest, (double) found.position + fraction), std::max (0.0, std::min (1.0, found.alike)) };
        };

        const bool silent = similarity (reference, reference, coarseCount, decimation) < -1.0;

        if (behind < 0.0)   // il faut reculer
        {
            if (silent)
                return { (double) latest, 1.0 };

            const auto found = search (reference, { latest, -decimation, steps + 1, -behind < neighbourReach, nearBias });
            return found.alike > -1.5 ? at (found) : Start { (double) latest, 0.0 };
        }

        const bool forced = behind >= (double) span;

        if (! (falling || forced) || behind < minimumJump)
            return { other, 1.0 };

        if (silent)
            return { (double) latest, 1.0 };

        // Le son est-il périodique ? On le compare à lui-même, un peu plus tôt.
        const auto past = search (reference, { reference - decimation, -decimation, steps, true, 0.0 });
        const bool pitched = past.alike >= pitchedThreshold;

        // Candidats devant l'autre tête, jusqu'à "latest" ; à ressemblance égale, le plus proche du direct.
        const int ahead = (int) std::min ((double) steps, std::floor (behind / (double) decimation));
        const auto forward = search (reference, { reference + decimation, decimation, ahead, true, -nearBias });

        // Son tenu : on attend qu'une période entière soit disponible (un maximum net, pas le bord de ce qu'on peut lire).
        const bool inPhase = forward.alike >= alignedThreshold && ! forward.atEnd;

        if (forward.alike > -1.5 && (forced || ! pitched || inPhase))
            return at (forward);

        return forced ? Start { (double) latest, 0.0 } : Start { other, 1.0 };
    }

    static constexpr double headroom = 1.03;            // marge sur la vitesse de lecture : le pitch peut encore monter pendant un grain
    static constexpr double nearBias = 0.03;            // à ressemblance égale, le plus petit saut (ou le moins de retard)
    static constexpr double neighbourThreshold = 0.3;   // en dessous, un candidat n'est plus un simple voisin de l'autre tête
    static constexpr double pitchedThreshold = 0.75;    // au-dessus, le son est considéré comme périodique
    static constexpr double alignedThreshold = 0.85;    // au-dessus, un saut vers l'avant tombe en phase

    std::vector<std::vector<float>> rings;      // [canal][size] : l'entrée, en continu
    std::vector<float> mono;                    // somme des canaux, pour la recherche du raccord
    std::vector<float> window, frame;
    int size = 0, grain = 2, hop = 1, span = 1, match = 1, decimation = 1;
    std::int64_t mask = 0, written = 0;

    Grain grains[2] = { { 0.0, 0 }, { 0.0, 0 } };
    int nextSlot = 0, untilNext = 0;
    bool engaged = false;

    double target = 0.0, current = 0.0, ratio = 1.0, smoothing = 1.0, neighbourReach = 0.0, minimumJump = 0.0;
    double coherence = 1.0;                     // ressemblance des deux grains en cours
    float wet = 0.0f, wetStep = 1.0f;
};
