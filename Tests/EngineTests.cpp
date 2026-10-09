// Tests hors-ligne du moteur (aucune dépendance) :
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -I Source Tests/EngineTests.cpp -o engine_tests && ./engine_tests

#include "LooperEngine.h"

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

struct Event { int sample; int type; double value; bool reanchor; };   // type 0 = on/off, 1 = durée

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
            if (e.type == 0) engine.setActive (e.value > 0.5);
            else             engine.setLoopLength (e.value, e.reanchor);
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

static void testFuzz()
{
    std::printf ("8. Fuzz : blocs, durees, modes et activations aleatoires (stereo, plusieurs frequences d'echantillonnage)\n");
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
    testFuzz();

    std::printf (failures == 0 ? "\nTOUS LES TESTS PASSENT\n" : "\n%d ECHEC(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
