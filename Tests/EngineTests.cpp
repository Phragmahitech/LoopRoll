// Tests hors-ligne du moteur (aucune dépendance) :
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -I Source Tests/EngineTests.cpp -o engine_tests && ./engine_tests

#include "LooperEngine.h"
#include "OutputFilter.h"
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
        const auto out = runMono (gate, ones, block, [&] (int start) { gate.syncPhase (offset + (double) start / step); });

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
            if (u (rng) < 0.2) gate.syncPhase (u (rng) * 50.0 - 25.0);
        });

        bool ok = true;
        for (size_t i = 0; i < in.size(); ++i) ok = ok && std::isfinite (out[i]) && std::abs (out[i]) <= std::abs (in[i]) + 1.0e-6f;
        CHECK (ok, "fuzz du gate : sortie invalide ou amplifiee");
    }
}

// Gain mesuré du filtre sur une sinusoïde (régime établi).
static double filterGain (double frequency, double cutoff, double resonance, bool bandPass)
{
    OutputFilter filter;
    filter.prepare (kSampleRate);
    filter.setFrequency (cutoff);
    filter.setResonance (resonance);
    filter.setBandPass (bandPass);
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
        const double low = filterGain (100.0, 1000.0, 0.0, false);
        const double high = filterGain (8000.0, 1000.0, 0.0, false);
        std::printf ("   passe-haut 1 kHz : 100 Hz -> %.1f dB, 8 kHz -> %.1f dB\n", 20.0 * std::log10 (low), 20.0 * std::log10 (high));
        CHECK (low < 0.02, "le passe-haut ne coupe pas assez les graves (%.4f)", low);
        CHECK (high > 0.9 && high < 1.1, "le passe-haut touche les aigus (%.4f)", high);
        CHECK (filterGain (40.0, 20.0, 0.0, false) > 0.9, "a 20 Hz le passe-haut devrait etre transparent");
    }

    // Passe-bande à 1 kHz : pic au centre, coupe de part et d'autre ; la résonance resserre et accentue.
    {
        const double centre = filterGain (1000.0, 1000.0, 0.5, true);
        const double below = filterGain (100.0, 1000.0, 0.5, true);
        const double above = filterGain (10000.0, 1000.0, 0.5, true);
        const double expected = std::sqrt (OutputFilter::qualityFor (0.5));
        std::printf ("   passe-bande 1 kHz : centre %.2f (attendu %.2f), 100 Hz %.3f, 10 kHz %.3f\n", centre, expected, below, above);
        CHECK (std::abs (centre - expected) < 0.15 * expected, "pic du passe-bande : %.3f", centre);
        CHECK (below < 0.2 && above < 0.2, "le passe-bande ne coupe pas assez (%.3f, %.3f)", below, above);
        CHECK (filterGain (1000.0, 1000.0, 1.0, true) > filterGain (1000.0, 1000.0, 0.0, true), "la resonance devrait accentuer le pic");
        CHECK (filterGain (1000.0, 1000.0, 0.8, false) > 2.0, "passe-haut : la resonance devrait creer un pic a la coupure");
    }

    // La courbe dessinée dans l'interface correspond au filtre réel.
    for (const bool bandPass : { false, true })
        for (const double f : { 200.0, 700.0, 1000.0, 1800.0 })
        {
            const double q = OutputFilter::qualityFor (0.4);
            const double measured = filterGain (f, 1000.0, 0.4, bandPass);
            const double drawn = OutputFilter::magnitudeAt (f, 1000.0, q, bandPass);
            CHECK (std::abs (measured - drawn) < 0.12 * std::max (drawn, 0.05), "courbe %s a %.0f Hz : mesure %.3f, dessine %.3f",
                   bandPass ? "passe-bande" : "passe-haut", f, measured, drawn);
        }

    // Allumage / extinction sans clic (réglage neutre) et balayages rapides : stable.
    {
        OutputFilter filter;
        filter.prepare (kSampleRate);
        filter.setFrequency (20.0);
        const int N = 60000;
        const auto sine = makeSine (N, 220.0, 0.8f);
        const auto out = runMono (filter, sine, 256, [&] (int start) { filter.setActive (start >= 8192 && start < 40000); filter.setBandPass (false); });
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
            if (u (rng) < 0.1) filter.setBandPass (u (rng) < 0.5);
            if (u (rng) < 0.05) filter.setActive (u (rng) < 0.7);
        });

        bool ok = true;
        float peak = 0.0f;
        for (float x : out) { ok = ok && std::isfinite (x); peak = std::max (peak, std::abs (x)); }
        CHECK (ok && peak < 40.0f, "filtre instable a %.0f Hz (pic %.1f)", sr, peak);
    }
}

static void testFuzz()
{
    std::printf ("12. Fuzz : blocs, durees, modes et activations aleatoires (stereo, plusieurs frequences d'echantillonnage)\n");
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
    testFuzz();

    std::printf (failures == 0 ? "\nTOUS LES TESTS PASSENT\n" : "\n%d ECHEC(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
