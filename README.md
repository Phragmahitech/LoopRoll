# LoopRoll

Effet audio VST3 pour macOS et Windows : looper / beat-repeat, stutter (gate rythmique) et filtre résonant.

Quatre modules côte à côte, chacun avec son grand potard, son bouton marche / arrêt et ses réglages :

- **LOOP** : capture l'audio à partir de l'instant où tu l'allumes, puis le répète. Éteint = signal direct.
  - **LENGTH** : durée de la boucle. Tourner à droite raccourcit.
  - **SYNC** : calé sur le tempo de l'hôte — 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/64. **FREE** : durée libre de 4 s à 10 ms.
  - **IN / OUT** : fondu d'entrée et de sortie sur chaque passage de la boucle, en % de sa durée. L'afficheur dessine la forme obtenue. À 0 % / 0 % la boucle est brute.
- **REFRESH** : quand il est allumé, la boucle est recapturée à intervalle régulier avec l'audio du moment.
  - **EVERY** : intervalle entre deux recaptures. Tourner à droite = plus fréquent.
  - **SYNC** : 4/1 (4 mesures), 2/1, 1/1, 1/2, 1/4, 1/8, 1/16, 1/32. **FREE** : de 16 s à 50 ms.
- **STUTTER** : gate rythmique. Chaque pas ouvre le son d'un coup puis le referme en dent de scie descendante.
  - **RATE** : durée d'un pas. Tourner à droite = plus rapide.
  - **SYNC** : 1/1 à 1/64, calé sur la grille de l'hôte quand le transport tourne. **FREE** : de 1 s à 5 ms.
  - **SHAPE** : courbe de la descente. 0 % = le niveau tient presque jusqu'au bout du pas (quasi carré), 50 % = descente linéaire, 100 % = très fermé (une pointe en début de pas). L'afficheur dessine deux pas.
- **FILTER** : filtre résonant en fin de chaîne, sur tout le son.
  - **CUTOFF** : fréquence, de 20 Hz à 20 kHz. **HP** : passe-haut 12 dB/octave. **BP** : passe-bande.
  - **RESO** : résonance, de 0 % (aucune) à 100 % (Q = 10). L'afficheur dessine la courbe de réponse.
- **CHAIN ORDER** : ordre du stutter et du looper dans la chaîne. Le filtre vient toujours en dernier.
  - **LOOP > STUTTER** (par défaut) : la boucle répète le son brut et le gate hache le résultat. Le gate continue à son propre rythme par-dessus la boucle.
  - **STUTTER > LOOP** : le son est haché d'abord, la boucle capture le résultat et le répète tel quel. Le motif du gate est figé dans la boucle.
- LOOP, REFRESH et STUTTER ont chacun leur interrupteur FREE / SYNC, indépendant des autres.
- LOOP, STUTTER et FILTER s'allument séparément : chacun marche seul ou avec les autres. REFRESH n'agit que quand LOOP est allumé.
- Mono et stéréo, latence nulle, tous les contrôles sont automatisables. Le sens des potards est celui des paramètres : en automation ou en mapping MIDI aussi, monter la valeur raccourcit les durées.

## Comportement

- Le premier passage laisse passer l'audio tel quel, les suivants le répètent à l'identique (copie exacte de l'enregistrement).
- Le raccord de boucle est fondu sur 3 ms **avant** la fin de boucle, vers l'audio qui précédait le point de capture. Le début de la boucle est donc toujours rejoué à plein niveau : un kick capté sur le temps garde son attaque à chaque répétition.
- En SYNC, changer de division pendant la boucle garde la grille calée sur le point de départ (passer de 1/4 à 1/16 puis revenir à 1/4 retombe sur les mêmes temps). La durée est suivie à la fraction d'échantillon : pas de dérive sur la longueur.
- En FREE, tourner le potard pendant la boucle ne déplace que la fin de boucle.
- REFRESH : à chaque point de refresh, une nouvelle capture démarre. On entend donc un passage de direct (une durée de boucle), puis ses répétitions jusqu'au refresh suivant. Exemple : LENGTH 1/16 et EVERY 1/4 = un seizième de direct, puis trois répétitions, à chaque noire.
- Les points de refresh sont comptés depuis l'allumage de LOOP, sans dérive. Allumer REFRESH en cours de boucle le fait tomber sur le prochain point de cette grille. Juste avant chaque refresh, la boucle sortante est fondue sur 3 ms vers le direct : la nouvelle capture démarre à plein niveau.
- Si l'intervalle de refresh est plus court que la boucle, rien n'a le temps de se répéter : c'est le direct qui passe.
- Fondus : l'enveloppe s'applique à tous les passages, y compris le premier (le direct). Avec un fondu d'entrée chaque passage repart du silence, avec un fondu de sortie il y retourne. Si les deux se chevauchent (plus de 100 % au total), l'enveloppe forme un triangle. Un fondu plus court que 3 ms ne descend pas jusqu'au silence, pour ne pas claquer.
- STUTTER en SYNC : quand le transport de l'hôte tourne, les pas tombent sur sa grille (un pas de 1/16 démarre sur chaque double croche du projet), quel que soit le moment où tu l'allumes. Transport arrêté, ou en FREE, le gate tourne librement et repart d'un pas plein à l'allumage.
- STUTTER : l'ouverture de chaque pas est adoucie sur 1,5 ms et la fin du pas retombe sur au moins 2 ms (moins sur les pas très courts), pour ne pas claquer. Même à SHAPE 0 % le pas se referme donc juste avant le suivant.
- FILTER : la fréquence et la résonance sont lissées (pas de bruit de potard), l'allumage et le changement HP / BP passent par un fondu de 5 ms. En passe-bande le pic vaut racine de Q : la résonance s'entend sans faire exploser le niveau. En passe-haut, la résonance à fond ajoute jusqu'à +20 dB à la fréquence de coupure : attention au niveau.
- Un projet sauvegardé avec la v0.3 se rouvre tel quel : le refresh reprend le mode FREE / SYNC de la boucle, stutter et filtre restent éteints.
- L'interface affiche son numéro de version en haut à droite : c'est le moyen le plus rapide de vérifier quelle version le DAW a chargée.
- Si l'hôte ne fournit pas de tempo, le dernier tempo connu est utilisé (120 BPM par défaut).
- La capture démarre au début du bloc audio dans lequel LOOP passe à ON : la précision dépend de la taille de buffer du DAW (≈ 3 ms à 128 échantillons, ≈ 12 ms à 512). Il n'y a pas de quantification du départ sur la grille.

## Obtenir les .vst3 sans rien installer (GitHub Actions)

Le dépôt contient un build automatique qui compile le plugin pour macOS (universel Apple Silicon + Intel) et Windows (x64), lance les tests et valide le résultat avec pluginval.

1. Crée un compte sur github.com, puis un nouveau dépôt (privé si tu veux), **sans** README.
2. Sur la page du dépôt : *uploading an existing file*, puis glisse **tout le contenu** du dossier `LoopRoll`, y compris le dossier caché `.github` (sur Mac : `Cmd + Shift + .` dans le Finder pour l'afficher). Les fichiers doivent être à la racine du dépôt, pas dans un sous-dossier. Valide avec *Commit changes*.
3. Onglet **Actions** : le build « Build VST3 » démarre tout seul (10 à 15 minutes).
4. Ouvre le run terminé : en bas de page, section **Artifacts**, télécharge `LoopRoll-macOS.zip` et `LoopRoll-Windows.zip`.

Si le dossier `.github` n'est pas passé à l'upload : onglet Actions → *set up a workflow yourself*, colle le contenu de `.github/workflows/build.yml`, et valide.

Les `.zip` sont publiés avant l'étape de validation : ils restent téléchargeables même si pluginval signale un problème.

## Installation

Voir `INSTALLATION.txt` (inclus dans chaque `.zip`). En résumé :

- **macOS** : copier `LoopRoll.vst3` dans `/Library/Audio/Plug-Ins/VST3/`, puis dans Terminal :
  `xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/LoopRoll.vst3`
  (le plugin n'est pas notarisé par Apple, macOS le bloque sinon).
  Ne garder qu'UNE copie : s'il en reste une autre dans `~/Library/Audio/Plug-Ins/VST3/`, le DAW peut charger l'ancienne.
- **Windows** : copier le dossier `LoopRoll.vst3` dans `C:\Program Files\Common Files\VST3\`.

## Compiler soi-même

Prérequis : [CMake](https://cmake.org/download/) 3.22+, et Xcode (macOS) ou Visual Studio 2022 avec « Développement Desktop en C++ » (Windows). JUCE est téléchargé automatiquement à la configuration.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLOOPROLL_COPY_AFTER_BUILD=ON
cmake --build build --config Release --parallel 4
```

Le plugin est dans `build/LoopRoll_artefacts/Release/VST3/LoopRoll.vst3` ; avec `LOOPROLL_COPY_AFTER_BUILD=ON` il est aussi copié dans le dossier VST3 du système (sous Windows, lancer le terminal en administrateur ou retirer cette option).

## Code

| Fichier | Rôle |
| --- | --- |
| `Source/LooperEngine.h` | Le looper : capture, répétition, refresh, fondus. Aucune dépendance, testable seul. |
| `Source/StutterGate.h` | Le stutter (gate rythmique). Aucune dépendance. |
| `Source/OutputFilter.h` | Le filtre de fin de chaîne (variable d'état, passe-haut / passe-bande). Aucune dépendance. |
| `Source/PluginProcessor.*` | Paramètres, lecture du tempo et de la position de l'hôte, ordre de la chaîne, sauvegarde d'état. |
| `Source/PluginEditor.*` | Interface (design clair, dessin vectoriel, quatre modules sur une même grille). |
| `Assets/` | Police Chakra Petch embarquée dans le plugin (licence OFL, voir `ChakraPetch-OFL.txt`). |
| `Tests/EngineTests.cpp` | Tests du traitement audio : boucle exacte, absence de clics, grille sans dérive, changements de durée, refresh, fondus, stutter, filtre, fuzz. |
| `Tests/PluginTests.cpp` | Tests du plugin complet : sync au tempo, modes indépendants, ordre de chaîne, état (y compris un état v0.3), interface. |

Lancer les tests : ajouter `-DLOOPROLL_BUILD_TESTS=ON` à la configuration, compiler, puis `ctest --test-dir build -C Release --output-on-failure`.

Réglages rapides :

- Nom d'éditeur affiché dans le DAW : `LOOPROLL_COMPANY_NAME` en haut de `CMakeLists.txt`.
- Divisions du mode sync : `getDivisionNames()` / `getDivisionInBeats()` pour la boucle et le stutter, `getRefreshDivisionNames()` / `getRefreshDivisionInBeats()` pour le refresh, dans `PluginProcessor.cpp`. Dans chaque paire les deux listes doivent rester alignées, de la plus longue à la plus courte.
- Plages du mode free : `minTimeMs` / `maxTimeMs`, `minRefreshMs` / `maxRefreshMs` et `minStutterMs` / `maxStutterMs` dans `PluginProcessor.h`.
- Résonance maximale du filtre : `qualityFor()` dans `OutputFilter.h` (Q de 0,7 à 10).

Ne pas changer `PLUGIN_CODE`, `PLUGIN_MANUFACTURER_CODE` ni les identifiants de paramètres une fois le plugin utilisé dans des projets : les DAW ne retrouveraient plus le plugin ou ses automations.

## Licence de JUCE

Le plugin est construit avec [JUCE](https://juce.com) 8, distribué sous AGPLv3 ou sous licence commerciale. Pour un usage personnel ça ne change rien ; avant de distribuer le plugin, vérifier les conditions en vigueur sur juce.com.
