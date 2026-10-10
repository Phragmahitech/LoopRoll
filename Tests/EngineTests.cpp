// Tests hors-ligne du moteur (aucune dépendance) :
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -I Source Tests/EngineTests.cpp -o engine_tests && ./engine_tests

#include "LooperEngine.h"
#include "OutputFilter.h"
#include "PitchShifter.h"
#include "StutterGate.h"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

static int failures = 0;

#define CHECK(cond, ...) \
    do { if (! (cond)) { ++failures; std::printf ("  ECHEC ligne %d : ", __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (false)

static const double kSampleRate = 44100.0;
static const int kFade = 132;   // 3 ms à 44,1 kHz

static std::vector<float> makeNoise (int n, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    std::vector<float> v ((size_t) n);
    for (auto& x : v) x = dist (rng);
    return v;
}

static std::vector<float> makeSine (int n, double freq, float amp)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = amp * (float) std::sin (2.0 * 3.14159265358979323846 * freq * i / kSampleRate);
    return v;
}

struct Event { int sample; int type; double value; bool reanchor; double value2 = 0.0; };   // type 0 = on/off, 1 = durée, 2 = refresh (value = on/off, value2 = intervalle), 3 = fondus (value = entrée, value2 = sortie)

// Fait tourner le moteur en mono, par blocs, en appliquant les évènements au début du bloc qui les contient.
static std::vector<float> run (LooperEngine& engine, const std::vector<float>& in, std::vector<Event> events, int blockSize)
{
    std::vector<float> out = in;
    size_t next = 0;

    for (int start = 0; start < (int) in.size(); start += blockSize)
    {
        const int n = std::min (blockSize, (int) in.size() - start);

        while (next < events.size() && events[next].sample <= start)
        {
            const auto& e = events[next++];
            if (e.type == 0)      engine.setActive (e.value > 0.5);
            else if (e.type == 1) engine.setLoopLength (e.value, e.reanchor);
            else if (e.type == 2) engine.setRefresh (e.value > 0.5, e.value2, e.reanchor);
            else                  engine.setFades (e.value, e.value2);
        }

        float* channel[1] = { out.data() + start };
        engine.process (channel, 1, n);
    }

    return out;
}

static float maxStep (const std::vector<float>& v, int from, int to)
{
    float m = 0.0f;
    for (int i = std::max (1, from); i < to; ++i)
        m = std::max (m, std::abs (v[(size_t) i] - v[(size_t) (i - 1)]));
    return m;
}

//==============================================================================
static void testPassthrough()
{
    std::printf ("1. Passthrough quand la boucle est inactive\n");
    LooperEngine e;
    e.prepare (kSampleRate, 1, 8.0);
    e.setLoopLength (5000.0, false);

    const auto in = makeNoise (30000, 1);
    const auto out = run (e, in, {}, 512);

    bool same = true;
    for (size_t i = 0; i < in.size(); ++i) same = same && in[i] == out[i];
    CHECK (same, "la sortie devrait etre identique a l'entree");
}

static void testLoopIsExact()
{
    std::printf ("2. Boucle exacte, premier passage transparent, attaque preservee\n");
    const int L = 5000, S = 4096, N = 60000;

    LooperEngine e;
    e.prepare (kSampleRate, 1, 8.0);
    e.setLoopLength ((double) L, false);

    const auto in = makeNoise (N, 2);
    const auto out = run (e, in, { { S, 0, 1.0, false } }, 512);

    bool before = true, firstPass = true, repeats = true;

    for (int t = 0; t < S; ++t)
        before = before && out[(size_t) t] == in[(size_t) t];

    for (int t = S; t < S + L - kFade; ++t)
        firstPass = firstPass && out[(size_t) t] == in[(size_t) t];

    // Répétitions : tout sauf la zone de fondu en fin de boucle doit être l'enregistrement exact,
    // y compris le tout premier échantillon de chaque répétition (pas de fondu sur l'attaque).
    for (int t = S + L + kFade; t < N; ++t)   // on laisse la rampe d'entrée (3 ms) se terminer
    {
        const int p = (t - S) % L;
        if (p < L - kFade)
            repeats = repeats && out[(size_t) t] == in[(size_t) (S + p)];
    }

    CHECK (before, "audio modifie avant l'activation");
    CHECK (firstPass, "le premier passage devrait etre l'entree intacte");
    CHECK (repeats, "les repetitions ne correspondent pas a l'enregistrement");

    // Pendant la boucle, l'entrée en direct ne doit plus passer.
    int leaks = 0;
    for (int t = S + 2 * L; t < S + 2 * L + 1000; ++t)
        leaks += out[(size_t) t] == in[(size_t) t] ? 1 : 0;
    CHECK (leaks == 0, "l'entree directe fuit dans la boucle");
}

static void testNoClicks()
{
    std::printf ("3. Pas de clic au raccord de boucle ni a la sortie (sinus 55 Hz)\n");
    const int L = 5003, S = 3000, OFF = 40000, N = 60000;   // 5003 n'est pas un multiple de la période

    LooperEngine e;
    e.prepare (kSampleRate, 1, 8.0);
    e.setLoopLength ((double) L, false);

    const auto in = makeSine (N, 55.0, 0.8f);
    const auto out = run (e, in, { { S, 0, 1.0, false }, { OFF, 0, 0.0, false } }, 256);

    const float natural = maxStep (in, 0, N);
    const float worst = maxStep (out, 0, N);

    // Référence : la même boucle sans aucun fondu.
    float hardCut = 0.0f;
    for (int t = S + 1; t < OFF; ++t)
        hardCut = std::max (hardCut, std::abs (in[(size_t) (S + (t - S) % L)] - in[(size_t) (S + (t - 1 - S) % L)]));

    std::printf ("   pente max : entree %.4f | boucle sans fondu %.4f | moteur %.4f\n", natural, hardCut, worst);
    CHECK (worst < 0.03f, "discontinuite trop forte : %.4f", worst);
    CHECK (hardCut > 10.0f * worst, "le test ne discrimine pas (%.4f vs %.4f)", hardCut, worst);

    bool backToDry = true;
    for (int t = OFF + 512; t < N; ++t)   // 256 de bloc + rampe de sortie
        backToDry = backToDry && out[(size_t) t] == in[(size_t) t];
    CHECK (backToDry, "la sortie ne revient pas a l'entree apres l'arret");
}

static void testFractionalLengthNoDrift()
{
    std::printf ("4. Duree fractionnaire (174 BPM, 1/16) : pas de derive apres 600 repetitions\n");
    const double Lf = 60.0 / 174.0 / 4.0 * kSampleRate;   // 3801,72 échantillons
    const int S = 960;   // sur une frontière de bloc (480)
    const int N = S + (int) (Lf * 605.0);

    LooperEngine e;
    e.prepare (kSampleRate, 1, 8.0);
    e.setLoopLength (Lf, true);

    const auto in = makeNoise (N, 4);
    const auto out = run (e, in, { { S, 0, 1.0, false } }, 480);

    int checked = 0, bad = 0, worstOffset = 0;

    for (int k = 590; k < 600; ++k)
    {
        const int ideal = S + (int) std::lround (k * Lf);   // début idéal de la k-ième répétition
        int found = 999;

        for (int d = -3; d <= 3 && found == 999; ++d)
        {
            bool match = true;
            for (int i = 200; i < 1200 && match; ++i)
                match = out[(size_t) (ideal + d + i)] == in[(size_t) (S + i)];
            if (match) found = d;
        }

        ++checked;
        if (found == 999) ++bad; else worstOffset = std::max (worstOffset, std::abs (found));
    }

    std::printf ("   %d repetitions verifiees, decalage max %d echantillon(s)\n", checked, worstOffset);
    CHECK (bad == 0, "%d repetitions hors grille", bad);
    CHECK (worstOffset <= 1, "derive de %d echantillons", worstOffset);
}

static void testReanchor()
{
    std::printf ("5. Changement de division en cours de boucle : la grille reste calee sur le depart\n");
    const int S = 2048, L1 = 20000, L2 = 5000, L3 = 10000;
    const int C1 = S + 33000, C2 = S + 71000, N = S + 120000;

    LooperEngine e;
    e.prepare (kSampleRate, 1, 8.0);
    e.setLoopLength ((double) L1, true);

    const auto in = makeNoise (N, 5);
    const auto out = run (e, in, { { S, 0, 1.0, false }, { C1, 1, (double) L2, true }, { C2, 1, (double) L3, true } }, 128);

    auto verify = [&] (int from, int to, int L, const char* name)
    {
        int bad = 0, n = 0;
        for (int t = from; t < to; ++t)
        {
            const int p = (t - S) % L;
            if (p < L - kFade) { ++n; bad += out[(size_t) t] == in[(size_t) (S + p)] ? 0 : 1; }
        }
        CHECK (bad == 0, "%s : %d/%d echantillons hors grille", name, bad, n);
    };

    verify (S + L1 + kFade, C1, L1, "1/1");
    verify (C1 + 128 + kFade + 2, C2, L2, "passage a 1/4");
    verify (C2 + 128 + kFade + 2, N, L3, "passage a 1/2");

    // Les sauts ne doivent pas claquer non plus.
    LooperEngine e2;
    e2.prepare (kSampleRate, 1, 8.0);
    e2.setLoopLength (20003.0, true);
    const auto sine = makeSine (N, 55.0, 0.8f);
    const auto out2 = run (e2, sine, { { S, 0, 1.0, false }, { C1, 1, 5003.0, true }, { C2, 1, 10007.0, true } }, 128);
    const float worst = maxStep (out2, 0, N);
    std::printf ("   pente max pendant les sauts : %.4f\n", worst);
    CHECK (worst < 0.03f, "clic lors d'un changement de division : %.4f", worst);
}

static void testFreeLengthChange()
{
    std::printf ("6. Mode free : raccourcir / rallonger la duree pendant la boucle\n");
    const int S = 1024, N = 200000;

    LooperEngine e;
    e.prepare (kSampleRate, 1, 8.0);
    e.setLoopLength (30000.0, false);

    // Balayage du potard : une nouvelle valeur à chaque bloc.
    std::vector<Event> events { { S, 0, 1.0, false } };
    for (int t = 40000; t < 90000; t += 64)
        events.push_back ({ t, 1, 30000.0 - (t - 40000) * 0.58, false });   // 30000 -> ~1000
    for (int t = 120000; t < 160000; t += 64)
        events.push_back ({ t, 1, 1000.0 + (t - 120000) * 0.4, false });     // ~1000 -> 17000

    const auto sine = makeSine (N, 55.0, 0.8f);
    const auto out = run (e, sine, events, 64);
    const float worst = maxStep (out, 0, N);
    std::printf ("   pente max pendant le balayage : %.4f\n", worst);
    CHECK (worst < 0.03f, "clic pendant le balayage de duree : %.4f", worst);

    // Après le balayage, la boucle doit être périodique à la nouvelle durée (16999,6 -> alternance 16999/17000).
    LooperEngine e2;
    e2.prepare (kSampleRate, 1, 8.0);
    e2.setLoopLength (30000.0, false);
    const auto noise = makeNoise (N, 6);
    const auto out2 = run (e2, noise, { { S, 0, 1.0, false }, { 50000, 1, 4000.0, false } }, 64);

    // Trouve le début d'une répétition après le changement, puis vérifie la période exacte.
    int start = -1;
    for (int t = 60000; t < 70000 && start < 0; ++t)
    {
        bool match = true;
        for (int i = 0; i < 500 && match; ++i)
            match = out2[(size_t) (t + i)] == noise[(size_t) (S + i)];
        if (match) start = t;
    }
    CHECK (start >= 0, "aucune repetition trouvee apres le changement de duree");

    if (start >= 0)
    {
        int bad = 0;
        for (int t = start; t < N; ++t)
        {
            const int p = (t - start) % 4000;
            if (p < 4000 - kFade) bad += out2[(size_t) t] == noise[(size_t) (S + p)] ? 0 : 1;
        }
        CHECK (bad == 0, "periode incorrecte apres changement de duree (%d echantillons faux)", bad);
    }
}

static void testRetriggerAndCapacity()
{
    std::printf ("7. Re-declenchements rapides, boucle plus longue que le buffer, tres longue duree\n");

    // Capacité volontairement petite (0,5 s) : boucle tenue 20 s, puis durée demandée trop grande.
    LooperEngine e;
    e.prepare (kSampleRate, 1, 0.5);
    e.setLoopLength (1.0e9, false);
    CHECK (e.getLoopLength() <= (double) e.getMaxLoopLength(), "duree non bornee");

    const int N = 900000;
    const auto in = makeNoise (N, 7);
    std::vector<Event> events { { 1000, 0, 1.0, false }, { 400000, 1, 100.0, false }, { 500000, 1, 1.0e9, false } };

    // Re-déclenchements toutes les ~1,5 ms (plus court que le fondu de sortie).
    for (int t = 600000, on = 0; t < 700000; t += 64, on ^= 1)
        events.push_back ({ t, 0, (double) on, false });
    events.push_back ({ 700000, 0, 0.0, false });

    const auto out = run (e, in, events, 64);

    bool finite = true, bounded = true;
    for (float x : out) { finite = finite && std::isfinite (x); bounded = bounded && std::abs (x) <= 1.0001f; }
    CHECK (finite, "valeurs non finies");
    CHECK (bounded, "la sortie depasse le niveau d'entree");

    bool dry = true;
    for (int t = 701000; t < N; ++t) dry = dry && out[(size_t) t] == in[(size_t) t];
    CHECK (dry, "ne revient pas au signal direct a la fin");
}

static void testRefresh()
{
    std::printf ("8. Refresh : recapture periodique exacte, sans clic, sans derive\n");

    // --- Contenu exact : boucle de 5000, refresh tous les 20000 ---
    {
        const int L = 5000, R = 20000, S = 4096, N = S + 5 * R + 3000;

        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength ((double) L, false);
        e.setRefresh (true, (double) R, false);

        const auto in = makeNoise (N, 8);
        const auto out = run (e, in, { { S, 0, 1.0, false } }, 512);

        int badLive = 0, badRepeat = 0, badFirst = 0;

        for (int k = 0; k < 5; ++k)
        {
            const int T = S + k * R;   // début de la k-ième capture

            // Le tout premier échantillon de chaque capture est le direct, à plein niveau.
            badFirst += out[(size_t) T] == in[(size_t) T] ? 0 : 1;

            // Premier passage = direct.
            for (int t = T; t < T + L - kFade; ++t)
                badLive += out[(size_t) t] == in[(size_t) t] ? 0 : 1;

            // Ensuite la boucle répète CETTE capture, jusqu'au fondu qui précède le refresh suivant.
            for (int t = T + L + kFade; t < T + R - kFade; ++t)
            {
                const int p = (t - T) % L;
                if (p < L - kFade)
                    badRepeat += out[(size_t) t] == in[(size_t) (T + p)] ? 0 : 1;
            }
        }

        CHECK (badFirst == 0, "debut de capture altere (%d)", badFirst);
        CHECK (badLive == 0, "premier passage apres refresh different du direct (%d echantillons)", badLive);
        CHECK (badRepeat == 0, "la boucle ne repete pas la derniere capture (%d echantillons)", badRepeat);
    }

    // --- Pas de clic (sinus, durées non multiples de la période) ---
    {
        const int S = 3000, N = 150000;
        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength (5003.0, false);
        e.setRefresh (true, 20011.0, false);

        const auto in = makeSine (N, 55.0, 0.8f);
        const auto out = run (e, in, { { S, 0, 1.0, false }, { 120000, 0, 0.0, false } }, 256);
        const float worst = maxStep (out, 0, N);
        std::printf ("   pente max avec refresh : %.4f\n", worst);
        CHECK (worst < 0.03f, "clic au refresh : %.4f", worst);
    }

    // --- Sync 174 BPM : boucle 1/16, refresh 1/4, pas de dérive après 150 refresh ---
    {
        const double Lf = 60.0 / 174.0 / 4.0 * kSampleRate;   // 3801,72
        const double Rf = Lf * 4.0;                             // 15206,90
        const int S = 960;
        const int N = S + (int) (Rf * 153.0);

        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength (Lf, true);
        e.setRefresh (true, Rf, true);

        const auto in = makeNoise (N, 9);
        const auto out = run (e, in, { { S, 0, 1.0, false } }, 480);

        int bad = 0, worstOffset = 0;

        for (int k = 140; k < 150; ++k)
        {
            const int ideal = S + (int) std::lround (k * Rf);       // point de refresh idéal
            const int repeat = ideal + (int) std::lround (Lf);      // début de la première répétition
            int found = 999;

            // La répétition rejoue le début de la capture : on retrouve où elle a démarré.
            for (int d = -3; d <= 3 && found == 999; ++d)
            {
                bool match = true;
                for (int i = 200; i < 1200 && match; ++i)
                    match = out[(size_t) (repeat + i)] == in[(size_t) (ideal + d + i)];
                if (match) found = d;
            }

            if (found == 999) ++bad; else worstOffset = std::max (worstOffset, std::abs (found));
        }

        std::printf ("   decalage max du point de refresh : %d echantillon(s)\n", worstOffset);
        CHECK (bad == 0, "%d refresh hors grille", bad);
        CHECK (worstOffset <= 2, "derive du refresh de %d echantillons", worstOffset);
    }

    // --- Refresh allumé en cours de boucle : il tombe sur la grille de l'activation ---
    {
        const int L = 5000, R = 20000, S = 2048, ON = S + 33024, N = S + 70000;

        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength ((double) L, true);

        const auto in = makeNoise (N, 10);
        const auto out = run (e, in, { { S, 0, 1.0, false }, { ON, 2, 1.0, true, (double) R }, { S + 50048, 2, 0.0, true, (double) R } }, 128);

        const int T = S + 2 * R;   // premier point de grille après l'allumage
        int bad = 0;

        for (int t = ON + 256; t < T - kFade; ++t)   // avant : toujours la capture d'origine
        {
            const int p = (t - S) % L;
            if (p < L - kFade) bad += out[(size_t) t] == in[(size_t) (S + p)] ? 0 : 1;
        }
        CHECK (bad == 0, "la boucle d'origine est perturbee avant le refresh (%d)", bad);

        bad = 0;
        for (int t = T; t < T + L - kFade; ++t)
            bad += out[(size_t) t] == in[(size_t) t] ? 0 : 1;
        CHECK (bad == 0, "pas de recapture sur la grille (%d)", bad);

        // Refresh éteint ensuite : plus de recapture, la dernière capture continue de tourner.
        bad = 0;
        for (int t = T + L + kFade; t < N; ++t)
        {
            const int p = (t - T) % L;
            if (p < L - kFade) bad += out[(size_t) t] == in[(size_t) (T + p)] ? 0 : 1;
        }
        CHECK (bad == 0, "la boucle change encore apres l'arret du refresh (%d)", bad);
    }

    // --- Intervalle plus court que la boucle : le direct passe ---
    {
        const int S = 1024, N = 60000;
        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength (8000.0, false);
        e.setRefresh (true, 3000.0, false);

        const auto in = makeNoise (N, 11);
        const auto out = run (e, in, { { S, 0, 1.0, false } }, 256);

        bool same = true;
        for (size_t i = 0; i < in.size(); ++i) same = same && in[i] == out[i];
        CHECK (same, "refresh plus court que la boucle : la sortie devrait etre le direct");
    }
}

static void testFades()
{
    std::printf ("9. Fondus : enveloppe exacte sur chaque passage, sans clic\n");

    const int L = 4000, S = 4096, N = S + 12 * L;

    // Enveloppe attendue, calculée comme dans le moteur.
    auto envelope = [L] (int p, double fadeIn, double fadeOut)
    {
        double g = 1.0;
        if (fadeIn > 0.0)  g = p <= 0 ? 0.0 : std::min (1.0, (double) p / (fadeIn * L));
        if (fadeOut > 0.0) g = std::min (g, std::max (0.0, std::min (1.0, ((double) L - (double) p) / (fadeOut * L))));
        return (float) g;
    };

    struct Case { double in, out; const char* name; };

    for (const auto& c : { Case { 0.25, 0.0, "entree 25 %" }, Case { 0.0, 0.5, "sortie 50 %" },
                           Case { 0.3, 0.3, "entree 30 % + sortie 30 %" }, Case { 1.0, 1.0, "entree 100 % + sortie 100 % (triangle)" } })
    {
        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength ((double) L, false);
        e.setFades (c.in, c.out);

        const auto in = makeNoise (N, 12);
        const auto out = run (e, in, { { S, 0, 1.0, false } }, 512);

        int bad = 0, n = 0;
        float peak = 0.0f;

        // À partir de la 2e répétition (rampe d'entrée du looper terminée), hors zone de raccord.
        for (int t = S + L + kFade; t < N; ++t)
        {
            const int pos = (t - S) % L;
            if (pos >= L - kFade) continue;

            ++n;
            bad += out[(size_t) t] == in[(size_t) (S + pos)] * envelope (pos, c.in, c.out) ? 0 : 1;
            peak = std::max (peak, std::abs (out[(size_t) t]));
        }

        CHECK (bad == 0, "%s : %d/%d echantillons hors enveloppe", c.name, bad, n);
        CHECK (peak <= 1.0f, "%s : niveau au-dessus de l'entree", c.name);

        // Le premier passage est enveloppé lui aussi (hors rampe d'entrée du looper).
        int badFirst = 0;
        for (int t = S + kFade + 8; t < S + L - kFade; ++t)
            badFirst += out[(size_t) t] == in[(size_t) t] * envelope (t - S, c.in, c.out) ? 0 : 1;
        CHECK (badFirst == 0, "%s : premier passage hors enveloppe (%d)", c.name, badFirst);
    }

    // Avec un fondu d'entrée, chaque répétition repart du silence ; avec un fondu de sortie, elle y finit.
    {
        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength ((double) L, false);
        e.setFades (0.2, 0.2);
        const auto in = makeNoise (N, 13);
        const auto out = run (e, in, { { S, 0, 1.0, false } }, 512);

        float atBoundary = 0.0f;
        for (int k = 2; k < 10; ++k)
            for (int d = -2; d <= 2; ++d)
                atBoundary = std::max (atBoundary, std::abs (out[(size_t) (S + k * L + d)]));
        CHECK (atBoundary < 0.01f, "le raccord devrait etre au silence avec les deux fondus (%.4f)", atBoundary);
    }

    // Pas de clic : sinus, fondus fixes puis modifiés en cours de boucle, avec et sans refresh.
    struct ClickCase { double in, out; bool refresh; };

    for (const auto& c : { ClickCase { 0.05, 0.0, false }, ClickCase { 0.0, 0.05, false }, ClickCase { 0.4, 0.4, false },
                           ClickCase { 0.05, 0.0, true }, ClickCase { 0.0, 0.3, true }, ClickCase { 0.5, 0.5, true } })
    {
        const int M = 160000;
        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength (5003.0, false);
        e.setFades (c.in, c.out);
        e.setRefresh (c.refresh, 20011.0, false);

        const auto sine = makeSine (M, 55.0, 0.8f);
        const auto out = run (e, sine, { { 3000, 0, 1.0, false },
                                         { 60000, 3, 0.0, false, 0.0 },        // fondus coupés en cours de boucle
                                         { 90000, 3, c.out, false, c.in },     // puis inversés
                                         { 130000, 0, 0.0, false } }, 256);
        const float worst = maxStep (out, 0, M);
        CHECK (worst < 0.03f, "clic avec fondus %.2f / %.2f%s : %.4f", c.in, c.out, c.refresh ? " + refresh" : "", worst);

        bool dry = true;
        for (int t = 131000; t < M; ++t) dry = dry && out[(size_t) t] == sine[(size_t) t];
        CHECK (dry, "retour au direct incorrect avec fondus %.2f / %.2f", c.in, c.out);
    }
}

// Fait passer un signal mono dans le gate, par blocs ; "onBlock" est appelé avant chaque bloc.
template <typename Processor, typename Callback>
static std::vector<float> runMono (Processor& processor, const std::vector<float>& in, int blockSize, Callback onBlock)
{
    std::vector<float> out = in;

    for (int start = 0; start < (int) in.size(); start += blockSize)
    {
        onBlock (start);
        float* channel[1] = { out.data() + start };
        processor.process (channel, 1, std::min (blockSize, (int) in.size() - start));
    }

    return out;
}

static void testStutter()
{
    std::printf ("10. Stutter : gate en dent de scie, forme reglable, sans clic\n");

    const int step = 2000, N = 60000, ON = 8192;
    const std::vector<float> ones ((size_t) N, 1.0f);   // entrée constante : la sortie EST le gain du gate

    // Éteint : intact.
    {
        StutterGate gate;
        gate.prepare (kSampleRate);
        gate.setStepLength ((double) step);
        const auto in = makeNoise (N, 20);
        const auto out = runMono (gate, in, 256, [] (int) {});
        bool same = true;
        for (size_t i = 0; i < in.size(); ++i) same = same && in[i] == out[i];
        CHECK (same, "gate eteint : la sortie devrait etre l'entree");
    }

    // Forme du pas selon le réglage.
    struct ShapeCase { double shape; float atTenth, atHalf, atNinety; const char* name; };

    for (const auto& c : { ShapeCase { 0.5, 0.90f, 0.50f, 0.10f, "lineaire" },
                           ShapeCase { 0.0, 0.99f, 0.96f, 0.87f, "quasi carre" },
                           ShapeCase { 1.0, 0.19f, 0.00f, 0.00f, "tres ferme" } })
    {
        StutterGate gate;
        gate.prepare (kSampleRate);
        gate.setStepLength ((double) step);
        gate.setShape (c.shape);
        const auto out = runMono (gate, ones, 256, [&] (int start) { gate.setActive (start >= ON); });

        const int s0 = ON + 5 * step;   // début d'un pas, bien après la rampe d'entrée
        CHECK (out[(size_t) s0] < 0.05f, "%s : le pas devrait s'ouvrir depuis zero (%.3f)", c.name, out[(size_t) s0]);
        CHECK (std::abs (out[(size_t) (s0 + step / 10)] - c.atTenth) < 0.03f, "%s : niveau a 10 %% = %.3f", c.name, out[(size_t) (s0 + step / 10)]);
        CHECK (std::abs (out[(size_t) (s0 + step / 2)] - c.atHalf) < 0.03f, "%s : niveau a 50 %% = %.3f", c.name, out[(size_t) (s0 + step / 2)]);
        CHECK (std::abs (out[(size_t) (s0 + step * 9 / 10)] - c.atNinety) < 0.03f, "%s : niveau a 90 %% = %.3f", c.name, out[(size_t) (s0 + step * 9 / 10)]);
        CHECK (out[(size_t) (s0 + step - 1)] < 0.05f, "%s : le pas devrait finir a zero (%.3f)", c.name, out[(size_t) (s0 + step - 1)]);

        float peak = 0.0f;
        bool periodic = true;
        for (int t = s0; t < s0 + 4 * step; ++t)
        {
            peak = std::max (peak, out[(size_t) t]);
            periodic = periodic && std::abs (out[(size_t) t] - out[(size_t) (t + step)]) < 1.0e-3f;
        }
        CHECK (peak <= 1.0f, "%s : le gate ne doit jamais amplifier", c.name);
        CHECK (periodic, "%s : le motif devrait se repeter a chaque pas", c.name);
    }

    // Calage sur la grille de l'hôte : le pas démarre là où on le lui dit.
    {
        StutterGate gate;
        gate.prepare (kSampleRate);
        gate.setStepLength ((double) step);
        gate.setShape (0.5);
        gate.setActive (true);

        const int block = 500;
        const double offset = 0.25;   // au début du rendu, on est à 25 % d'un pas
        const auto out = runMono (gate, ones, block, [&] (int start) { gate.syncPosition (offset + (double) start / step); });

        const int boundary = (int) ((1.0 - offset) * step) + 6 * step;   // un début de pas sur la grille
        CHECK (out[(size_t) boundary] < 0.05f && out[(size_t) (boundary + step / 10)] > 0.8f,
               "le pas ne tombe pas sur la grille demandee (%.3f, %.3f)", out[(size_t) boundary], out[(size_t) (boundary + step / 10)]);
        CHECK (out[(size_t) (boundary - step / 10)] < 0.2f, "fin de pas mal placee (%.3f)", out[(size_t) (boundary - step / 10)]);
    }

    // Pas de clic, et retour exact au direct.
    for (double shape : { 0.0, 0.5, 1.0 })
    {
        StutterGate gate;
        gate.prepare (kSampleRate);
        gate.setStepLength (3801.72);   // 1/16 à 174 BPM
        const auto sine = makeSine (N, 55.0, 0.8f);
        const auto out = runMono (gate, sine, 256, [&] (int start)
        {
            gate.setActive (start >= ON && start < 40000);
            gate.setShape (start < 24000 ? shape : 1.0 - shape);   // forme changée en cours de route
        });

        const float worst = maxStep (out, 0, N);
        CHECK (worst < 0.03f, "clic du gate (forme %.1f) : %.4f", shape, worst);

        bool dry = true;
        for (int t = 41000; t < N; ++t) dry = dry && out[(size_t) t] == sine[(size_t) t];
        CHECK (dry, "le gate ne rend pas le signal intact une fois eteint");
    }

    // Profondeur : à 50 %, le gate ne descend qu'à mi-niveau.
    {
        StutterGate gate;
        gate.prepare (kSampleRate);
        gate.setStepLength ((double) step);
        gate.setDepth (0.5);
        const auto out = runMono (gate, ones, 256, [&] (int start) { gate.setActive (start >= ON); });
        const int s0 = ON + 5 * step;
        CHECK (std::abs (out[(size_t) s0] - 0.5f) < 0.03f, "profondeur 50 %% : debut de pas a %.3f", out[(size_t) s0]);
        CHECK (std::abs (out[(size_t) (s0 + step / 2)] - 0.75f) < 0.03f, "profondeur 50 %% : mi-pas a %.3f", out[(size_t) (s0 + step / 2)]);

        float lowest = 1.0f;
        for (int t = s0; t < s0 + 4 * step; ++t) lowest = std::min (lowest, out[(size_t) t]);
        CHECK (lowest > 0.47f, "profondeur 50 %% : le gate descend trop bas (%.3f)", lowest);
    }

    // Motif : un pas éteint reste fermé, les autres jouent normalement ; le motif fait 16 pas.
    {
        StutterGate gate;
        gate.prepare (kSampleRate);
        gate.setStepLength ((double) step);
        gate.setPattern (0xffffu & ~(1u << 2) & ~(1u << 3));   // pas 3 et 4 éteints
        const std::vector<float> longOnes (100000, 1.0f);
        const auto out = runMono (gate, longOnes, 256, [&] (int start) { gate.setActive (start >= ON); });

        for (int cycle : { 0, 1 })
        {
            const int base = ON + cycle * 16 * step;
            float closed = 0.0f;
            for (int t = base + 2 * step; t < base + 4 * step; ++t) closed = std::max (closed, out[(size_t) t]);
            CHECK (closed < 1.0e-6f, "motif : les pas eteints devraient etre fermes (%.4f)", closed);
            CHECK (out[(size_t) (base + step + step / 10)] > 0.85f, "motif : le pas 2 devrait jouer (%.3f)", out[(size_t) (base + step + step / 10)]);
            CHECK (out[(size_t) (base + 4 * step + step / 10)] > 0.85f, "motif : le pas 5 devrait jouer (%.3f)", out[(size_t) (base + 4 * step + step / 10)]);
        }

        CHECK (maxStep (out, 0, 100000) < 0.03f, "motif : clic (%.4f)", maxStep (out, 0, 100000));
    }

    // Swing à fond : le premier pas de chaque paire dure 1,5 pas, le second 0,5.
    {
        StutterGate gate;
        gate.prepare (kSampleRate);
        gate.setStepLength ((double) step);
        gate.setSwing (1.0);
        gate.reset();
        const auto out = runMono (gate, ones, 256, [&] (int start) { gate.setActive (start >= ON); });
        const int pair = ON + 3 * 2 * step;                     // début d'une paire
        const int second = pair + step * 3 / 2;                 // le second pas démarre en retard
        CHECK (out[(size_t) pair] < 0.05f, "swing : la paire devrait s'ouvrir depuis zero (%.3f)", out[(size_t) pair]);
        CHECK (std::abs (out[(size_t) (pair + step * 3 / 4)] - 0.5f) < 0.03f, "swing : mi-chemin du pas long a %.3f", out[(size_t) (pair + step * 3 / 4)]);
        CHECK (out[(size_t) (pair + step)] > 0.25f, "swing : le pas long ne devrait pas etre referme a un pas (%.3f)", out[(size_t) (pair + step)]);
        CHECK (out[(size_t) second] < 0.05f, "swing : le pas court devrait demarrer a 1,5 pas (%.3f)", out[(size_t) second]);
        CHECK (std::abs (out[(size_t) (second + step / 4)] - 0.5f) < 0.03f, "swing : mi-chemin du pas court a %.3f", out[(size_t) (second + step / 4)]);

        const auto step5 = StutterGate::locate (5.25, 0.0);
        CHECK (step5.index == 5 && std::abs (step5.phase - 0.25) < 1.0e-9, "reperage d'une position sans swing (%d, %.3f)", step5.index, step5.phase);
        const auto wrapped = StutterGate::locate (-0.5, 0.0);
        CHECK (wrapped.index == 15 && std::abs (wrapped.phase - 0.5) < 1.0e-9, "reperage d'une position negative (%d, %.3f)", wrapped.index, wrapped.phase);
    }

    // Fuzz
    {
        std::mt19937 rng (77);
        std::uniform_real_distribution<double> u (0.0, 1.0);
        StutterGate gate;
        gate.prepare (48000.0);
        const auto in = makeNoise (400000, 21);
        const auto out = runMono (gate, in, 97, [&] (int)
        {
            if (u (rng) < 0.1) gate.setActive (u (rng) < 0.6);
            if (u (rng) < 0.3) gate.setStepLength (std::pow (10.0, u (rng) * 6.0));
            if (u (rng) < 0.3) gate.setShape (u (rng) * 1.4 - 0.2);
            if (u (rng) < 0.2) gate.setDepth (u (rng) * 1.4 - 0.2);
            if (u (rng) < 0.2) gate.setSwing (u (rng) * 1.4 - 0.2);
            if (u (rng) < 0.2) gate.setPattern ((unsigned int) (u (rng) * 70000.0));
            if (u (rng) < 0.2) gate.syncPosition (u (rng) * 5000.0 - 2500.0);
        });

        bool ok = true;
        for (size_t i = 0; i < in.size(); ++i) ok = ok && std::isfinite (out[i]) && std::abs (out[i]) <= std::abs (in[i]) + 1.0e-6f;
        CHECK (ok, "fuzz du gate : sortie invalide ou amplifiee");
    }
}

// Gain mesuré du filtre sur une sinusoïde (régime établi).
static double filterGain (double frequency, double cutoff, double resonance, int type, bool steep = false)
{
    OutputFilter filter;
    filter.prepare (kSampleRate);
    filter.setFrequency (cutoff);
    filter.setResonance (resonance);
    filter.setType (type);
    filter.setSteep (steep);
    filter.reset();
    filter.setActive (true);

    const int N = 40000;
    const auto in = makeSine (N, frequency, 0.5f);
    const auto out = runMono (filter, in, 256, [] (int) {});

    double sumIn = 0.0, sumOut = 0.0;
    for (int i = N / 2; i < N; ++i) { sumIn += (double) in[(size_t) i] * in[(size_t) i]; sumOut += (double) out[(size_t) i] * out[(size_t) i]; }
    return std::sqrt (sumOut / sumIn);
}

static void testFilter()
{
    std::printf ("11. Filtre : passe-haut, passe-bande, resonance, stabilite\n");

    // Éteint : intact.
    {
        OutputFilter filter;
        filter.prepare (kSampleRate);
        filter.setFrequency (2000.0);
        const auto in = makeNoise (30000, 30);
        const auto out = runMono (filter, in, 256, [] (int) {});
        bool same = true;
        for (size_t i = 0; i < in.size(); ++i) same = same && in[i] == out[i];
        CHECK (same, "filtre eteint : la sortie devrait etre l'entree");
    }

    // Passe-haut à 1 kHz, sans résonance.
    {
        const double low = filterGain (100.0, 1000.0, 0.0, OutputFilter::highPass);
        const double high = filterGain (8000.0, 1000.0, 0.0, OutputFilter::highPass);
        std::printf ("   passe-haut 1 kHz : 100 Hz -> %.1f dB, 8 kHz -> %.1f dB\n", 20.0 * std::log10 (low), 20.0 * std::log10 (high));
        CHECK (low < 0.02, "le passe-haut ne coupe pas assez les graves (%.4f)", low);
        CHECK (high > 0.9 && high < 1.1, "le passe-haut touche les aigus (%.4f)", high);
        CHECK (filterGain (40.0, 20.0, 0.0, OutputFilter::highPass) > 0.9, "a 20 Hz le passe-haut devrait etre transparent");
    }

    // Passe-bande à 1 kHz : pic au centre, coupe de part et d'autre ; la résonance resserre et accentue.
    {
        const double centre = filterGain (1000.0, 1000.0, 0.5, OutputFilter::bandPass);
        const double below = filterGain (100.0, 1000.0, 0.5, OutputFilter::bandPass);
        const double above = filterGain (10000.0, 1000.0, 0.5, OutputFilter::bandPass);
        const double expected = std::sqrt (OutputFilter::qualityFor (0.5));
        std::printf ("   passe-bande 1 kHz : centre %.2f (attendu %.2f), 100 Hz %.3f, 10 kHz %.3f\n", centre, expected, below, above);
        CHECK (std::abs (centre - expected) < 0.15 * expected, "pic du passe-bande : %.3f", centre);
        CHECK (below < 0.2 && above < 0.2, "le passe-bande ne coupe pas assez (%.3f, %.3f)", below, above);
        CHECK (filterGain (1000.0, 1000.0, 1.0, OutputFilter::bandPass) > filterGain (1000.0, 1000.0, 0.0, OutputFilter::bandPass), "la resonance devrait accentuer le pic");
        CHECK (filterGain (1000.0, 1000.0, 0.8, OutputFilter::highPass) > 2.0, "passe-haut : la resonance devrait creer un pic a la coupure");
    }

    // Passe-bas à 1 kHz, et pente raide (24 dB par octave).
    {
        const double low = filterGain (100.0, 1000.0, 0.0, OutputFilter::lowPass);
        const double high = filterGain (10000.0, 1000.0, 0.0, OutputFilter::lowPass);
        const double steepLow = filterGain (100.0, 1000.0, 0.0, OutputFilter::highPass, true);
        const double steepPass = filterGain (8000.0, 1000.0, 0.0, OutputFilter::highPass, true);
        std::printf ("   passe-bas 1 kHz : 10 kHz -> %.1f dB ; passe-haut 24 dB : 100 Hz -> %.1f dB\n", 20.0 * std::log10 (high), 20.0 * std::log10 (steepLow));
        CHECK (low > 0.95 && low < 1.05, "le passe-bas touche les graves (%.4f)", low);
        CHECK (high < 0.02, "le passe-bas ne coupe pas assez les aigus (%.4f)", high);
        CHECK (steepLow < 0.0005, "la pente 24 dB ne coupe pas assez (%.6f)", steepLow);
        CHECK (steepPass > 0.9 && steepPass < 1.1, "la pente 24 dB touche la bande passante (%.4f)", steepPass);

        // Le second étage n'ajoute pas de résonance : le pic reste du même ordre.
        const double peak12 = filterGain (1000.0, 1000.0, 1.0, OutputFilter::highPass);
        const double peak24 = filterGain (1000.0, 1000.0, 1.0, OutputFilter::highPass, true);
        CHECK (peak24 < peak12 * 1.05, "24 dB : le pic de resonance ne doit pas grossir (%.2f contre %.2f)", peak24, peak12);
    }

    // La courbe dessinée dans l'interface correspond au filtre réel.
    for (const int type : { (int) OutputFilter::highPass, (int) OutputFilter::bandPass, (int) OutputFilter::lowPass })
        for (const bool steep : { false, true })
            for (const double f : { 200.0, 700.0, 1000.0, 1800.0 })
            {
                const double q = OutputFilter::qualityFor (0.4);
                const double measured = filterGain (f, 1000.0, 0.4, type, steep);
                const double drawn = OutputFilter::magnitudeAt (f, 1000.0, q, type, steep);
                CHECK (std::abs (measured - drawn) < 0.12 * std::max (drawn, 0.05), "courbe type %d%s a %.0f Hz : mesure %.4f, dessine %.4f",
                       type, steep ? " (24 dB)" : "", f, measured, drawn);
            }

    // Limiteur : il plafonne à 0 dBFS, et ne touche à rien en dessous.
    {
        const int N = 60000;
        const auto loud = makeSine (N, 1000.0, 0.5f);
        const auto quiet = makeSine (N, 1000.0, 0.02f);

        const auto render = [&] (const std::vector<float>& in, bool limit)
        {
            OutputFilter filter;
            filter.prepare (kSampleRate);
            filter.setFrequency (1000.0);
            filter.setResonance (1.0);       // pic d'environ +20 dB à la coupure
            filter.setLimiter (limit);
            filter.reset();
            filter.setActive (true);
            return runMono (filter, in, 256, [] (int) {});
        };

        const auto free = render (loud, false), limited = render (loud, true);
        float freePeak = 0.0f, limitedPeak = 0.0f, limitedLate = 0.0f;
        for (int i = 0; i < N; ++i) { freePeak = std::max (freePeak, std::abs (free[(size_t) i])); limitedPeak = std::max (limitedPeak, std::abs (limited[(size_t) i])); }
        for (int i = N / 2; i < N; ++i) limitedLate = std::max (limitedLate, std::abs (limited[(size_t) i]));
        CHECK (freePeak > 4.0f, "sans limiteur le pic devrait depasser largement 0 dBFS (%.2f)", freePeak);
        CHECK (limitedPeak <= 1.0001f, "le limiteur laisse passer %.4f", limitedPeak);
        CHECK (limitedLate > 0.9f, "le limiteur ecrase trop le signal (%.3f)", limitedLate);

        const auto a = render (quiet, false), b = render (quiet, true);
        bool same = true;
        for (int i = 0; i < N; ++i) same = same && a[(size_t) i] == b[(size_t) i];
        CHECK (same, "sous le plafond, le limiteur ne devrait rien changer");
    }

    // Allumage / extinction sans clic (réglage neutre) et balayages rapides : stable.
    {
        OutputFilter filter;
        filter.prepare (kSampleRate);
        filter.setFrequency (20.0);
        const int N = 60000;
        const auto sine = makeSine (N, 220.0, 0.8f);
        const auto out = runMono (filter, sine, 256, [&] (int start) { filter.setActive (start >= 8192 && start < 40000); filter.setType (OutputFilter::highPass); });
        CHECK (maxStep (out, 0, N) < 0.04f, "clic a l'allumage du filtre : %.4f", maxStep (out, 0, N));

        bool dry = true;
        for (int t = 41000; t < N; ++t) dry = dry && out[(size_t) t] == sine[(size_t) t];
        CHECK (dry, "le filtre ne rend pas le signal intact une fois eteint");
    }

    for (double sr : { 22050.0, 44100.0, 96000.0, 192000.0 })
    {
        std::mt19937 rng (5);
        std::uniform_real_distribution<double> u (0.0, 1.0);
        OutputFilter filter;
        filter.prepare (sr);
        filter.setActive (true);
        const auto in = makeNoise (300000, 31);
        const auto out = runMono (filter, in, 64, [&] (int)
        {
            if (u (rng) < 0.5) filter.setFrequency (std::pow (10.0, u (rng) * 5.0));   // 1 Hz à 100 kHz : hors plage exprès
            if (u (rng) < 0.3) filter.setResonance (u (rng) * 1.4 - 0.2);
            if (u (rng) < 0.1) filter.setType ((int) (u (rng) * 3.0));
            if (u (rng) < 0.1) filter.setSteep (u (rng) < 0.5);
            if (u (rng) < 0.1) filter.setLimiter (u (rng) < 0.5);
            if (u (rng) < 0.05) filter.setActive (u (rng) < 0.7);
        });

        bool ok = true;
        float peak = 0.0f;
        for (float x : out) { ok = ok && std::isfinite (x); peak = std::max (peak, std::abs (x)); }
        CHECK (ok && peak < 40.0f, "filtre instable a %.0f Hz (pic %.1f)", sr, peak);
    }
}

// Fait tourner le moteur en mono par blocs ; "onBlock" est appelé avant chaque bloc (réglages, calage).
template <typename Callback>
static std::vector<float> runEngine (LooperEngine& engine, const std::vector<float>& in, int blockSize, Callback onBlock)
{
    std::vector<float> out = in;

    for (int start = 0; start < (int) in.size(); start += blockSize)
    {
        onBlock (start);
        float* channel[1] = { out.data() + start };
        engine.process (channel, 1, std::min (blockSize, (int) in.size() - start));
    }

    return out;
}

static void testRepeats()
{
    std::printf ("13. Repetitions : capture pre, pitch, reverse, decay\n");
    const int L = 5000, S = 20480, N = 90000;
    const auto in = makeNoise (N, 40);

    // Capture « pré » : la boucle répète tout de suite la tranche qui vient de jouer, à l'identique.
    {
        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength ((double) L, false);
        e.setPreCapture (true);
        const auto out = runEngine (e, in, 512, [&] (int start) { e.setActive (start >= S); });

        bool before = true, repeats = true;
        for (int t = 0; t < S; ++t) before = before && out[(size_t) t] == in[(size_t) t];

        for (int t = S + kFade + 2; t < N; ++t)   // la rampe d'entrée (3 ms) terminée
        {
            const int p = (t - S) % L;
            if (p < L - kFade)
                repeats = repeats && out[(size_t) t] == in[(size_t) (S - L + p)];
        }

        CHECK (before, "pre : audio modifie avant l'activation");
        CHECK (repeats, "pre : la boucle devrait repeter la tranche qui precede l'activation");
        CHECK (out[(size_t) (S + L)] == in[(size_t) (S - L)], "pre : attaque de la tranche adoucie");
    }

    // Capture « pré » + refresh : à chaque point, on repart sur la tranche qui vient de jouer, à plein niveau.
    {
        const int len = 2000, every = 8000;
        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength ((double) len, false);
        e.setRefresh (true, (double) every, false);
        e.setPreCapture (true);
        const auto out = runEngine (e, in, 512, [&] (int start) { e.setActive (start >= S); });

        bool ok = true;
        for (int j = 1; j <= 6; ++j)
        {
            const int point = S + j * every;
            for (int t = point; t < point + every - kFade - 2; ++t)
            {
                const int p = (t - point) % len;
                if (p < len - kFade)
                    ok = ok && out[(size_t) t] == in[(size_t) (point - len + p)];
            }
        }
        CHECK (ok, "pre + refresh : chaque refresh devrait reprendre la tranche qui le precede");
    }

    const int edge = 80;   // fenêtre de raccord des répétitions transposées ou inversées (1,5 ms)

    // Pitch : +12 demi-tons par répétition = lecture deux fois plus rapide, puis quatre fois.
    {
        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength ((double) L, false);
        e.setPitch (12.0);
        const auto out = runEngine (e, in, 512, [&] (int start) { e.setActive (start >= S); });

        bool live = true, doubleSpeed = true, quadSpeed = true;
        for (int t = S; t < S + L - kFade; ++t) live = live && out[(size_t) t] == in[(size_t) t];

        for (int p = 0; p < L; ++p)
        {
            const int inHalf = p % (L / 2), inQuarter = p % (L / 4);

            if (inHalf > edge && inHalf < L / 2 - edge - kFade)
                doubleSpeed = doubleSpeed && out[(size_t) (S + L + p)] == in[(size_t) (S + (2 * p) % L)];

            if (inQuarter > edge && inQuarter < L / 4 - edge - kFade)
                quadSpeed = quadSpeed && out[(size_t) (S + 2 * L + p)] == in[(size_t) (S + (4 * p) % L)];
        }

        CHECK (live, "pitch : le passage en direct ne doit pas etre transpose");
        CHECK (doubleSpeed, "pitch +12 : la premiere repetition devrait etre lue deux fois plus vite");
        CHECK (quadSpeed, "pitch +12 : la deuxieme repetition devrait etre lue quatre fois plus vite");
    }

    // Pitch vers le bas : -12 = deux fois plus lent, les échantillons intermédiaires sont interpolés.
    {
        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength ((double) L, false);
        e.setPitch (-12.0);
        const auto out = runEngine (e, in, 512, [&] (int start) { e.setActive (start >= S); });

        bool exact = true, interpolated = true;
        for (int p = edge; p < L - edge - kFade; ++p)
        {
            const float got = out[(size_t) (S + L + p)];
            if (p % 2 == 0) exact = exact && got == in[(size_t) (S + p / 2)];
            else            interpolated = interpolated && std::abs (got - 0.5f * (in[(size_t) (S + p / 2)] + in[(size_t) (S + p / 2 + 1)])) < 1.0e-6f;
        }
        CHECK (exact && interpolated, "pitch -12 : la repetition devrait etre lue deux fois plus lentement");
    }

    // Reverse : chaque répétition relit la tranche à l'envers.
    {
        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength ((double) L, false);
        e.setReverse (true);
        const auto out = runEngine (e, in, 512, [&] (int start) { e.setActive (start >= S); });

        bool ok = true;
        for (int pass : { 1, 2, 7 })
            for (int p = edge; p < L - edge - kFade; ++p)
                ok = ok && out[(size_t) (S + pass * L + p)] == in[(size_t) (S + L - 1 - p)];
        CHECK (ok, "reverse : la repetition devrait etre la tranche lue a l'envers");

        // En capture « pré », la tranche qui vient de jouer repart à l'envers tout de suite.
        LooperEngine pre;
        pre.prepare (kSampleRate, 1, 8.0);
        pre.setLoopLength ((double) L, false);
        pre.setReverse (true);
        pre.setPreCapture (true);
        const auto grabbed = runEngine (pre, in, 512, [&] (int start) { pre.setActive (start >= S); });

        bool reversed = true;
        for (int p = kFade + 2; p < L - edge - kFade; ++p)
            reversed = reversed && grabbed[(size_t) (S + p)] == in[(size_t) (S - 1 - p)];
        CHECK (reversed, "pre + reverse : la tranche precedente devrait partir a l'envers des l'activation");
    }

    // Decay : chaque répétition perd la moitié de son niveau.
    {
        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength ((double) L, false);
        e.setDecay (0.5);
        const auto out = runEngine (e, in, 512, [&] (int start) { e.setActive (start >= S); });

        bool ok = true;
        float gain = 1.0f;
        for (int pass = 1; pass <= 6; ++pass)
        {
            gain *= 0.5f;
            for (int p = 0; p < L - kFade; ++p)
                ok = ok && out[(size_t) (S + pass * L + p)] == in[(size_t) (S + p)] * gain;
        }
        CHECK (ok, "decay 50 %% : chaque repetition devrait etre deux fois moins forte");
    }

    // Pas de clic avec pitch, reverse et decay réunis, ni au retour au direct.
    for (const bool pre : { false, true })
        for (const double semitones : { 3.0, -5.0, 0.0 })
        {
            LooperEngine e;
            e.prepare (kSampleRate, 1, 8.0);
            e.setLoopLength (3801.72, false);
            e.setRefresh (true, 4.0 * 3801.72 * 3.0, false);
            e.setPitch (semitones);
            e.setDecay (0.2);
            e.setPreCapture (pre);
            const auto sine = makeSine (N, 55.0, 0.8f);
            const auto out = runEngine (e, sine, 256, [&] (int start)
            {
                e.setActive (start >= 8192 && start < 70000);
                e.setReverse (start >= 30000);          // inversion enclenchée en cours de boucle
            });

            const float worst = maxStep (out, 0, N);
            CHECK (worst < 0.05f, "clic (pitch %.0f, pre %d) : %.4f", semitones, (int) pre, worst);

            bool dry = true;
            for (int t = 71000; t < N; ++t) dry = dry && out[(size_t) t] == sine[(size_t) t];
            CHECK (dry, "le direct ne revient pas intact apres une boucle transposee");
        }
}

static void testSnap()
{
    std::printf ("14. Calage du depart sur la grille\n");
    const int G = 6000, L = 3000, N = 80000;
    const auto in = makeNoise (N, 41);

    const auto render = [&] (bool pre, int pressAt, int blockSize)
    {
        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength ((double) L, false);
        e.setPreCapture (pre);
        return runEngine (e, in, blockSize, [&, pressAt] (int start)
        {
            e.setSnap (true, (double) G, (double) (start % G));
            e.setActive (start >= pressAt);
        });
    };

    // Appui entre deux lignes : le départ attend la ligne suivante (24000), le direct passe d'ici là.
    {
        const auto out = render (false, 20480, 512);
        bool live = true, repeats = true;
        for (int t = 0; t < 24000 + L - kFade; ++t) live = live && out[(size_t) t] == in[(size_t) t];
        for (int t = 24000 + L; t < N; ++t)
        {
            const int p = (t - 24000) % L;
            if (p < L - kFade)
                repeats = repeats && out[(size_t) t] == in[(size_t) (24000 + p)];
        }
        CHECK (live, "calage : le direct devrait passer jusqu'a la ligne de grille");
        CHECK (repeats, "calage : la boucle devrait demarrer pile sur la ligne de grille");
    }

    // Appui légèrement en retard (500 échantillons après la ligne) : la boucle est ancrée sur la ligne passée.
    {
        const auto out = render (false, 24500, 500);
        bool ok = true;
        for (int t = 24000; t < N; ++t)
        {
            const int p = (t - 24000) % L;
            if (p < L - kFade)
                ok = ok && out[(size_t) t] == in[(size_t) (24000 + p)];
        }
        CHECK (ok, "calage : un appui en retard devrait etre rattrape sur la ligne qui vient de passer");
    }

    // Capture « pré » calée : la tranche d'avant la ligne démarre pile dessus, à plein niveau.
    {
        const auto out = render (true, 20480, 512);
        bool live = true, repeats = true;
        for (int t = 0; t < 24000 - kFade - 2; ++t) live = live && out[(size_t) t] == in[(size_t) t];
        for (int t = 24000; t < N; ++t)
        {
            const int p = (t - 24000) % L;
            if (p < L - kFade)
                repeats = repeats && out[(size_t) t] == in[(size_t) (24000 - L + p)];
        }
        CHECK (live, "pre calee : le direct devrait passer jusqu'au fondu d'approche");
        CHECK (repeats, "pre calee : la tranche precedente devrait demarrer pile sur la ligne, a plein niveau");
    }

    // Capture « pré », appui en retard : on entre dans la tranche là où elle en serait.
    {
        const auto out = render (true, 24500, 500);
        bool ok = true;
        for (int t = 24500 + kFade + 2; t < N; ++t)
        {
            const int p = (t - 24000) % L;
            if (p < L - kFade)
                ok = ok && out[(size_t) t] == in[(size_t) (24000 - L + p)];
        }
        CHECK (ok, "pre calee : un appui en retard devrait retomber sur la grille");
    }

    // Sans grille (hôte à l'arrêt), le départ est immédiat ; un départ armé puis annulé ne laisse pas de trace.
    {
        LooperEngine e;
        e.prepare (kSampleRate, 1, 8.0);
        e.setLoopLength ((double) L, false);
        const auto out = runEngine (e, in, 512, [&] (int start)
        {
            e.setSnap (start < 30000, (double) G, (double) (start % G));
            e.setActive ((start >= 20480 && start < 22000) || start >= 40960);
        });

        bool untouched = true, repeats = true;
        for (int t = 0; t < 40960 + L - kFade; ++t) untouched = untouched && out[(size_t) t] == in[(size_t) t];
        for (int t = 40960 + L; t < N; ++t)
        {
            const int p = (t - 40960) % L;
            if (p < L - kFade)
                repeats = repeats && out[(size_t) t] == in[(size_t) (40960 + p)];
        }
        CHECK (untouched, "un depart arme puis annule ne devrait rien changer au signal");
        CHECK (repeats, "sans grille, la boucle devrait demarrer tout de suite");
    }
}

static void testFuzz()
{
    std::printf ("15. Fuzz : blocs, durees, modes et activations aleatoires (stereo, plusieurs frequences d'echantillonnage)\n");
    std::mt19937 rng (1234);

    for (double sr : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        LooperEngine e;
        e.prepare (sr, 2, 2.0);

        std::uniform_real_distribution<float> sig (-1.0f, 1.0f);
        std::uniform_real_distribution<double> u (0.0, 1.0);
        std::vector<float> l (4096), r (4096);
        bool ok = true;
        bool active = false;

        for (int block = 0; block < 6000; ++block)
        {
            const int n = 1 + (int) (u (rng) * (block % 7 == 0 ? 4095 : 300));
            for (int i = 0; i < n; ++i) { l[(size_t) i] = sig (rng); r[(size_t) i] = sig (rng); }

            if (u (rng) < 0.08) { active = ! active; e.setActive (active); }
            if (u (rng) < 0.30) e.setLoopLength (std::pow (10.0, u (rng) * 6.0 - 0.5), u (rng) < 0.5);
            if (u (rng) < 0.25) e.setRefresh (u (rng) < 0.7, std::pow (10.0, u (rng) * 6.5 - 0.5), u (rng) < 0.5);
            if (u (rng) < 0.20) e.setFades (u (rng) < 0.3 ? 0.0 : u (rng) * 1.2 - 0.1, u (rng) < 0.3 ? 0.0 : u (rng) * 1.2 - 0.1);
            if (u (rng) < 0.10) e.setPitch (u (rng) < 0.4 ? 0.0 : u (rng) * 60.0 - 30.0);
            if (u (rng) < 0.10) e.setReverse (u (rng) < 0.5);
            if (u (rng) < 0.10) e.setDecay (u (rng) < 0.4 ? 0.0 : u (rng) * 1.4 - 0.2);
            if (u (rng) < 0.10) e.setPreCapture (u (rng) < 0.5);
            if (u (rng) < 0.30) { const double grid = std::pow (10.0, u (rng) * 5.0); e.setSnap (u (rng) < 0.6, grid, u (rng) * grid * 1.2); }
            if (u (rng) < 0.002) e.reset();

            float* io[2] = { l.data(), r.data() };
            e.process (io, 2, n);

            for (int i = 0; i < n; ++i)
                ok = ok && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i])
                        && std::abs (l[(size_t) i]) <= 1.0001f && std::abs (r[(size_t) i]) <= 1.0001f;
        }

        CHECK (ok, "sortie invalide a %.0f Hz", sr);
    }
}

// Fréquence d'un signal, mesurée par ses passages à zéro montants entre "from" et la fin.
static double measureFrequency (const std::vector<float>& v, int from, double sampleRate)
{
    double first = -1.0, last = -1.0;
    int count = 0;

    for (int i = from + 1; i < (int) v.size(); ++i)
    {
        const float before = v[(size_t) (i - 1)], after = v[(size_t) i];

        if (before < 0.0f && after >= 0.0f)
        {
            const double crossing = (double) (i - 1) + (double) (before / (before - after));
            if (first < 0.0) first = crossing; else ++count;
            last = crossing;
        }
    }

    return count > 0 ? count * sampleRate / (last - first) : 0.0;
}

// Niveau efficace par tranches de "window" échantillons, de "from" à la fin : moyenne, et rapport entre la plus faible et la plus forte.
struct Level { double mean, steadiness; };

static Level measureLevel (const std::vector<float>& v, int from, int window)
{
    double lowest = 1.0e9, highest = 0.0, sum = 0.0;
    int count = 0;

    for (int start = from; start + window <= (int) v.size(); start += window / 2)
    {
        double energy = 0.0;
        for (int i = start; i < start + window; ++i) energy += (double) v[(size_t) i] * (double) v[(size_t) i];
        const double rms = std::sqrt (energy / window);
        lowest = std::min (lowest, rms);
        highest = std::max (highest, rms);
        sum += rms;
        ++count;
    }

    return { count > 0 ? sum / count : 0.0, highest > 0.0 ? lowest / highest : 0.0 };
}

static void testPitch()
{
    std::printf ("16. Pitch : transposition de tout le son\n");
    const double pi = 3.14159265358979323846;
    const int N = 4 * 44100;

    const auto shift = [] (const std::vector<float>& in, double semitones, int blockSize = 256)
    {
        PitchShifter shifter;
        shifter.prepare (kSampleRate, 1);
        shifter.setSemitones (semitones);
        return runMono (shifter, in, blockSize, [] (int) {});
    };

    // À 0 demi-ton, rien ne change, à l'échantillon près.
    {
        PitchShifter shifter;
        shifter.prepare (kSampleRate, 1);
        const auto in = makeNoise (30000, 50);
        const auto out = runMono (shifter, in, 256, [] (int) {});
        CHECK (out == in, "pitch a 0 : le signal devrait passer tel quel");
        CHECK (! shifter.isActive(), "pitch a 0 : le pitch ne devrait pas travailler");
    }

    // Sinus : la hauteur est exacte (demi-tons entiers ou non), le niveau reste stable, aucun clic.
    struct Case { double frequency, semitones; };

    for (const auto c : { Case { 440.0, 12.0 }, Case { 440.0, -12.0 }, Case { 440.0, 7.0 }, Case { 440.0, -5.0 }, Case { 440.0, 0.5 }, Case { 440.0, -0.37 },
                          Case { 55.0, 12.0 }, Case { 55.0, 3.0 }, Case { 55.0, 0.5 }, Case { 55.0, -7.0 }, Case { 55.0, -12.0 },
                          Case { 41.2, 5.0 }, Case { 41.2, -2.0 }, Case { 3000.0, 4.0 }, Case { 3000.0, -9.0 } })
    {
        const float amplitude = 0.5f;
        const auto out = shift (makeSine (N, c.frequency, amplitude), c.semitones);
        const double expected = c.frequency * std::pow (2.0, c.semitones / 12.0);
        const double measured = measureFrequency (out, N / 4, kSampleRate);
        const auto level = measureLevel (out, N / 4, (int) std::lround (4.0 * kSampleRate / expected));
        const double steepest = 2.0 * amplitude * std::sin (pi * expected / kSampleRate);

        CHECK (std::abs (measured / expected - 1.0) < 0.003, "sinus %.1f Hz %+.2f st : %.2f Hz au lieu de %.2f", c.frequency, c.semitones, measured, expected);
        CHECK (std::abs (level.mean / (amplitude / std::sqrt (2.0)) - 1.0) < 0.03, "sinus %.1f Hz %+.2f st : niveau x%.3f", c.frequency, c.semitones, level.mean / (amplitude / std::sqrt (2.0)));
        CHECK (level.steadiness > 0.9, "sinus %.1f Hz %+.2f st : le niveau ondule (%.2f)", c.frequency, c.semitones, level.steadiness);
        CHECK (maxStep (out, N / 4, N) < 1.2f * (float) steepest, "sinus %.1f Hz %+.2f st : clic (%.4f pour une pente de %.4f)", c.frequency, c.semitones, maxStep (out, N / 4, N), steepest);
    }

    // Basse riche en harmoniques : le niveau ne se creuse pas aux raccords, et le son reste périodique à la bonne hauteur.
    for (const double semitones : { 12.0, 1.0, -3.0, -12.0 })
    {
        std::vector<float> in ((size_t) N);

        for (int i = 0; i < N; ++i)
        {
            double v = 0.0;
            for (int h = 1; h <= 20; ++h) v += std::sin (2.0 * pi * 50.0 * h * i / kSampleRate + 0.3 * h) / std::pow ((double) h, 1.2);
            in[(size_t) i] = (float) (0.3 * v);
        }

        const auto out = shift (in, semitones);
        const double period = kSampleRate / (50.0 * std::pow (2.0, semitones / 12.0));
        const auto before = measureLevel (in, N / 4, (int) std::lround (2.0 * kSampleRate / 50.0));
        const auto after = measureLevel (out, N / 4, (int) std::lround (2.0 * period));

        // Ressemblance du son avec lui-même, une période plus loin.
        const int lag = (int) std::lround (period);
        double dot = 0.0, a = 0.0, b = 0.0;
        for (int i = N / 2; i < N / 2 + 8820; ++i) { const double x = out[(size_t) i], y = out[(size_t) (i + lag)]; dot += x * y; a += x * x; b += y * y; }

        CHECK (std::abs (after.mean / before.mean - 1.0) < 0.03, "basse %+.0f st : niveau x%.3f", semitones, after.mean / before.mean);
        CHECK (after.steadiness > 0.9, "basse %+.0f st : le niveau ondule (%.2f)", semitones, after.steadiness);
        CHECK (dot / std::sqrt (a * b) > 0.9, "basse %+.0f st : la periode attendue n'y est pas (%.3f)", semitones, dot / std::sqrt (a * b));
    }

    // Bruit : le niveau est à peu près conservé (les raccords ne peuvent pas être en phase).
    for (const double semitones : { 12.0, 3.0, -3.0, -12.0 })
    {
        const auto in = makeNoise (N, 51);
        const auto out = shift (in, semitones);
        const double ratio = measureLevel (out, N / 4, 4410).mean / measureLevel (in, N / 4, 4410).mean;
        CHECK (ratio > 0.8 && ratio < 1.1, "bruit %+.0f st : niveau x%.3f", semitones, ratio);
    }

    // Enclenchement en cours de route puis retour à 0 : pas de clic, et le direct revient intact.
    {
        const auto in = makeSine (N, 220.0, 0.8f);
        PitchShifter shifter;
        shifter.prepare (kSampleRate, 1);
        const auto out = runMono (shifter, in, 256, [&] (int start) { shifter.setSemitones (start >= 20480 && start < 61440 ? 5.0 : 0.0); });

        bool before = true, after = true;
        for (int t = 0; t < 20480; ++t) before = before && out[(size_t) t] == in[(size_t) t];
        for (int t = 61440 + 441; t < N; ++t) after = after && out[(size_t) t] == in[(size_t) t];
        const double steepest = 2.0 * 0.8 * std::sin (pi * 220.0 * std::pow (2.0, 5.0 / 12.0) / kSampleRate);

        CHECK (before, "le son est modifie avant que le pitch quitte 0");
        CHECK (after, "le direct ne revient pas intact quand le pitch revient a 0");
        CHECK (std::abs (measureFrequency (std::vector<float> (out.begin() + 30000, out.begin() + 60000), 0, kSampleRate) / (220.0 * std::pow (2.0, 5.0 / 12.0)) - 1.0) < 0.005, "pitch enclenche en route : mauvaise hauteur");
        CHECK (maxStep (out, 0, N) < 1.5f * (float) steepest, "clic a l'enclenchement ou au retour (%.4f pour une pente de %.4f)", maxStep (out, 0, N), steepest);
    }

    // Stéréo : deux canaux identiques le restent ; deux canaux différents sont transposés tous les deux.
    // (Les deux notes sont à la douzième : un raccord ne peut tomber en phase que pour des sons de même période.)
    {
        const auto left = makeSine (N, 330.0, 0.5f), right = makeSine (N, 990.0, 0.4f);
        std::vector<float> a = left, b = left, c = left, d = right;
        PitchShifter same, different;
        same.prepare (kSampleRate, 2);
        different.prepare (kSampleRate, 2);
        same.setSemitones (-4.0);
        different.setSemitones (-4.0);

        for (int start = 0; start < N; start += 200)
        {
            float* x[2] = { a.data() + start, b.data() + start };
            float* y[2] = { c.data() + start, d.data() + start };
            same.process (x, 2, std::min (200, N - start));
            different.process (y, 2, std::min (200, N - start));
        }

        const double ratio = std::pow (2.0, -4.0 / 12.0);
        CHECK (a == b, "stereo : deux canaux identiques devraient le rester");
        CHECK (std::abs (measureFrequency (c, N / 4, kSampleRate) / (330.0 * ratio) - 1.0) < 0.01, "stereo : canal gauche a %.1f Hz", measureFrequency (c, N / 4, kSampleRate));
        CHECK (std::abs (measureFrequency (d, N / 4, kSampleRate) / (990.0 * ratio) - 1.0) < 0.01, "stereo : canal droit a %.1f Hz", measureFrequency (d, N / 4, kSampleRate));
    }

    // Automation : un balayage continu de -12 à +12 puis retour, par petits blocs, reste propre.
    {
        const auto in = makeSine (N, 110.0, 0.8f);
        PitchShifter shifter;
        shifter.prepare (kSampleRate, 1);
        const auto out = runMono (shifter, in, 64, [&] (int start)
        {
            const double position = (double) start / N;
            shifter.setSemitones (position < 0.5 ? -12.0 + 48.0 * position : 12.0 - 48.0 * (position - 0.5));
        });

        float peak = 0.0f;
        bool finite = true;
        for (const float v : out) { finite = finite && std::isfinite (v); peak = std::max (peak, std::abs (v)); }
        const double steepest = 2.0 * 0.8 * std::sin (pi * 220.0 / kSampleRate);

        CHECK (finite && peak < 1.0f, "balayage : sortie invalide (crete %.3f)", peak);
        CHECK (maxStep (out, 0, N) < 1.5f * (float) steepest, "balayage : clic (%.4f pour une pente de %.4f)", maxStep (out, 0, N), steepest);
        CHECK (measureLevel (out, 4410, 4410).steadiness > 0.7, "balayage : le niveau se creuse (%.2f)", measureLevel (out, 4410, 4410).steadiness);
    }

    // Fuzz : réglages et tailles de blocs au hasard, valeurs absurdes comprises.
    {
        std::mt19937 rng (4321);
        std::uniform_real_distribution<float> sig (-1.0f, 1.0f);
        std::uniform_real_distribution<double> u (0.0, 1.0);

        for (double sr : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            PitchShifter shifter;
            shifter.prepare (sr, 2);
            std::vector<float> l (4096), r (4096);
            bool ok = true;

            for (int block = 0; block < 4000; ++block)
            {
                const int n = 1 + (int) (u (rng) * (block % 7 == 0 ? 4095 : 300));
                const bool quiet = block % 97 > 80;   // des passages de silence
                for (int i = 0; i < n; ++i) { l[(size_t) i] = quiet ? 0.0f : sig (rng); r[(size_t) i] = quiet ? 0.0f : sig (rng); }

                const double pick = u (rng);
                if (pick < 0.15)       shifter.setSemitones (0.0);
                else if (pick < 0.45)  shifter.setSemitones (u (rng) * 60.0 - 30.0);
                else if (pick < 0.47)  shifter.setSemitones (std::nan (""));
                else if (pick < 0.49)  shifter.setSemitones (u (rng) < 0.5 ? 1.0e30 : -1.0e30);
                if (u (rng) < 0.002) shifter.reset();

                float* io[2] = { l.data(), r.data() };
                shifter.process (io, 2, n);

                for (int i = 0; i < n; ++i)
                    ok = ok && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i])
                            && std::abs (l[(size_t) i]) <= 2.5f && std::abs (r[(size_t) i]) <= 2.5f;
            }

            CHECK (ok, "pitch : sortie invalide a %.0f Hz", sr);
        }
    }
}

int main()
{
    testPassthrough();
    testLoopIsExact();
    testNoClicks();
    testFractionalLengthNoDrift();
    testReanchor();
    testFreeLengthChange();
    testRetriggerAndCapacity();
    testRefresh();
    testFades();
    testStutter();
    testFilter();
    testRepeats();
    testSnap();
    testFuzz();
    testPitch();

    std::printf (failures == 0 ? "\nTOUS LES TESTS PASSENT\n" : "\n%d ECHEC(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
