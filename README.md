# Phragments — by Phragma

Effet audio VST3 pour macOS et Windows : looper / beat-repeat, stutter (gate rythmique à pas) et filtre résonant.

Le plugin s'est appelé LoopRoll (jusqu'à la v0.4) puis Fragment (v1.0). Le dépôt, le projet CMake et les classes gardent le nom LoopRoll ; seuls le nom affiché et le fichier `.vst3` ont changé. Les identifiants internes sont les mêmes : un projet fait avec une ancienne version se rouvre avec Phragments.

Quatre modules côte à côte, chacun avec son grand potard, son bouton marche / arrêt et ses réglages :

- **LOOP** : capture l'audio à partir de l'instant où tu l'allumes, puis le répète. Éteint = signal direct.
  - **LENGTH** : durée de la boucle. Tourner à droite raccourcit. **SYNC** : 1/1 à 1/128, calé sur le tempo. **FREE** : de 4 s à 10 ms.
  - **IN / OUT** : fondu d'entrée et de sortie sur chaque passage, en % de sa durée.
  - **PITCH** : transposition ajoutée à chaque répétition, de -12 à +12 demi-tons (+3 : la 1re répétition est à +3, la 2e à +6…, cumul borné à ±36). Le passage en direct n'est jamais transposé.
  - **DECAY** : baisse de niveau à chaque répétition (50 % : chaque répétition est deux fois moins forte que la précédente).
  - **REV** : les répétitions sont lues à l'envers.
  - **PRE** : la boucle répète tout de suite la tranche qui vient de jouer, au lieu de laisser passer un premier passage en direct.
  - **SNAP** : le départ est calé sur la grille de l'hôte (transport en lecture). Un appui légèrement en retard est rattrapé, sinon le départ attend la ligne suivante.
  - **MOMENT** : mode momentané. Le bouton LOOP de l'interface ne fait tourner la boucle que tant qu'il est enfoncé.
- **REFRESH** : quand il est allumé, la boucle est recapturée à intervalle régulier avec l'audio du moment.
  - **EVERY** : intervalle entre deux recaptures. **SYNC** : 4/1 (4 mesures) à 1/32. **FREE** : de 16 s à 50 ms.
- **STUTTER** : gate rythmique à 16 pas. Chaque pas ouvre le son d'un coup puis le referme en dent de scie descendante.
  - **RATE** : durée d'un pas. **SYNC** : 1/1 à 1/128, calé sur la grille de l'hôte quand le transport tourne. **FREE** : de 1 s à 5 ms.
  - **Motif** : 16 cases cliquables (on peut glisser pour en peindre plusieurs). Une case éteinte reste fermée. L'afficheur dessine la forme réelle du gate, pas par pas.
  - **SHAPE** : courbe de la descente. 0 % = quasi carré, 50 % = linéaire, 100 % = très fermé.
  - **DEPTH** : profondeur. 100 % = le gate referme jusqu'au silence, 0 % = sans effet.
  - **SWING** : allonge un pas sur deux et raccourcit le suivant (100 % = trois quarts / un quart).
- **FILTER** : filtre résonant en fin de chaîne, sur tout le son.
  - **CUTOFF** : de 20 Hz à 20 kHz. **HP / BP / LP** : passe-haut, passe-bande, passe-bas. **12 dB / 24 dB** : pente par octave.
  - **RESO** : résonance, de 0 % (aucune) à 100 % (Q = 10).
  - **LIMITER** : limiteur de sécurité à 0 dBFS sur la sortie du filtre (allumé par défaut).
- **T / D** : en SYNC, chaque module (LOOP, REFRESH, STUTTER) peut passer en triolet (durée x 2/3) ou en pointé (x 3/2). L'afficheur indique « 1/16 T » ou « 1/16 D ».
- **CHAIN** : ordre du stutter et du looper, et **MIX** (dry / wet) sur l'ensemble du plugin. Le filtre vient toujours après le stutter et le looper.
  - **LOOP > STUTTER** (par défaut) : la boucle répète le son brut et le gate hache le résultat.
  - **STUTTER > LOOP** : le son est haché d'abord, la boucle capture le résultat et le répète tel quel.
- LOOP, REFRESH et STUTTER ont chacun leur interrupteur FREE / SYNC, indépendant des autres.
- LOOP, STUTTER et FILTER s'allument séparément : chacun marche seul ou avec les autres. REFRESH n'agit que quand LOOP est allumé.
- Mono et stéréo, latence nulle, tous les contrôles sont automatisables. Le sens des potards est celui des paramètres : en automation ou en mapping MIDI aussi, monter la valeur raccourcit les durées.

## Comportement

- Sans PRE, le premier passage laisse passer l'audio tel quel, les suivants le répètent. Tant que PITCH, DECAY et REV sont à zéro, chaque répétition est la copie exacte de l'enregistrement.
- Le raccord de boucle est fondu sur 3 ms **avant** la fin de boucle, vers l'audio qui précédait le point de capture. Le début de la boucle est donc toujours rejoué à plein niveau : un kick capté sur le temps garde son attaque à chaque répétition.
- Une répétition transposée ou inversée est relue par interpolation. Elle est encadrée par une fenêtre de 1,5 ms à chaque raccord (son attaque est donc très légèrement adoucie). Transposée vers le haut, la tranche est lue plus vite et reboucle à l'intérieur du passage ; vers le bas, on n'en entend que le début. PITCH, REV et DECAY sont pris en compte au début de chaque passage, pas en cours de route.
- PRE : le plugin enregistre l'entrée en permanence. À l'allumage, la boucle prend la tranche qui vient de jouer et la répète aussitôt ; avec REFRESH, chaque point de refresh reprend la tranche qui le précède (on n'entend alors jamais le direct). L'entrée en boucle se fait sur un fondu de 3 ms.
- SNAP : la grille est la division de la boucle (jamais plus fine que la double croche, sans triolet ni pointé) ; en FREE, la double croche. Un appui en retard d'au plus un tiers de pas (60 ms au maximum) est rattrapé : la boucle est ancrée sur la ligne qui vient de passer. Transport arrêté, le départ est immédiat. Avec PRE, la tranche démarre pile sur la ligne, à plein niveau.
- MOMENT ne concerne que le bouton de l'interface. Avec un contrôleur MIDI, c'est le réglage du bouton du contrôleur (momentané ou bascule) qui décide.
- En SYNC, changer de division pendant la boucle garde la grille calée sur le point de départ. La durée est suivie à la fraction d'échantillon : pas de dérive sur la longueur. En FREE, tourner le potard pendant la boucle ne déplace que la fin de boucle.
- REFRESH : à chaque point de refresh, une nouvelle capture démarre. Les points sont comptés depuis l'allumage de LOOP, sans dérive. Si l'intervalle de refresh est plus court que la boucle, rien n'a le temps de se répéter.
- Fondus : l'enveloppe s'applique à tous les passages, y compris le premier. Si les deux se chevauchent (plus de 100 % au total), l'enveloppe forme un triangle. Un fondu plus court que 3 ms ne descend pas jusqu'au silence, pour ne pas claquer.
- STUTTER en SYNC : quand le transport de l'hôte tourne, les pas et le motif tombent sur sa grille (à 1/16, le motif de 16 pas fait une mesure et repart au début de chaque mesure). Transport arrêté, ou en FREE, le gate tourne librement et repart du premier pas à l'allumage.
- STUTTER : l'ouverture de chaque pas est adoucie sur 1,5 ms et la fin du pas retombe sur au moins 2 ms (moins sur les pas très courts), pour ne pas claquer.
- FILTER : la fréquence et la résonance sont lissées, l'allumage et les changements de type ou de pente passent par un fondu de 5 ms. En 24 dB, le second étage n'ajoute pas de résonance : le pic reste le même qu'en 12 dB. En passe-bande le pic vaut racine de Q. En passe-haut et passe-bas, la résonance à fond ajoute jusqu'à +20 dB à la fréquence de coupure.
- LIMITER : attaque instantanée, relâchement de 100 ms, plafond à 0 dBFS. Il ne touche pas au signal tant que la sortie du filtre reste sous le plafond, et ne travaille que quand FILTER est allumé. Sur un son déjà proche de 0 dBFS, un filtre peut faire dépasser les crêtes : le limiteur les retient alors (l'éteindre pour entendre le filtre brut).
- MIX : mélange entre l'entrée du plugin et la sortie de toute la chaîne. À 100 %, la sortie est celle de la chaîne, sans calcul supplémentaire.
- Compatibilité : un projet sauvegardé avec une version précédente se rouvre tel quel, les nouveaux réglages prennent leur valeur neutre. Seule réserve : une automation déjà écrite sur la division de la boucle ou du stutter, ou sur le type de filtre, se décale légèrement, parce que ces listes ont une valeur de plus (1/128, passe-bas).
- L'interface affiche son nom et son numéro de version dans l'en-tête : c'est le moyen le plus rapide de vérifier quelle version le DAW a chargée.
- Si l'hôte ne fournit pas de tempo, le dernier tempo connu est utilisé (120 BPM par défaut).
- Sans SNAP, la capture démarre au début du bloc audio dans lequel LOOP passe à ON : la précision dépend de la taille de buffer du DAW (≈ 3 ms à 128 échantillons, ≈ 12 ms à 512).

## Obtenir les .vst3 sans rien installer (GitHub Actions)

Le dépôt contient un build automatique qui compile le plugin pour macOS (universel Apple Silicon + Intel) et Windows (x64), lance les tests et valide le résultat avec pluginval.

1. Crée un compte sur github.com, puis un nouveau dépôt (privé si tu veux), **sans** README.
2. Sur la page du dépôt : *uploading an existing file*, puis glisse **tout le contenu** du dossier `LoopRoll`, y compris le dossier caché `.github` (sur Mac : `Cmd + Shift + .` dans le Finder pour l'afficher). Les fichiers doivent être à la racine du dépôt, pas dans un sous-dossier. Valide avec *Commit changes*.
3. Onglet **Actions** : le build « Build VST3 » démarre tout seul (10 à 15 minutes).
4. Ouvre le run terminé : en bas de page, section **Artifacts**, télécharge `Phragments-macOS.zip` et `Phragments-Windows.zip`.

Si le dossier `.github` n'est pas passé à l'upload : onglet Actions → *set up a workflow yourself*, colle le contenu de `.github/workflows/build.yml`, et valide.

Les `.zip` sont publiés avant l'étape de validation : ils restent téléchargeables même si pluginval signale un problème.

## Installation

Voir `INSTALLATION.txt` (inclus dans chaque `.zip`). En résumé :

- **macOS** : copier `Phragments.vst3` dans `/Library/Audio/Plug-Ins/VST3/`, puis dans Terminal :
  `xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/Phragments.vst3`
  (le plugin n'est pas notarisé par Apple, macOS le bloque sinon).
  Ne garder qu'UNE copie : s'il en reste une autre dans `~/Library/Audio/Plug-Ins/VST3/`, le DAW peut charger l'ancienne.
- **Windows** : copier le dossier `Phragments.vst3` dans `C:\Program Files\Common Files\VST3\`.
- Dans les deux cas, supprimer un éventuel ancien `LoopRoll.vst3` ou `Fragment.vst3` : c'est le même plugin sous un ancien nom.

## Compiler soi-même

Prérequis : [CMake](https://cmake.org/download/) 3.22+, et Xcode (macOS) ou Visual Studio 2022 avec « Développement Desktop en C++ » (Windows). JUCE est téléchargé automatiquement à la configuration.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLOOPROLL_COPY_AFTER_BUILD=ON
cmake --build build --config Release --parallel 4
```

Le plugin est dans `build/LoopRoll_artefacts/Release/VST3/Phragments.vst3` ; avec `LOOPROLL_COPY_AFTER_BUILD=ON` il est aussi copié dans le dossier VST3 du système (sous Windows, lancer le terminal en administrateur ou retirer cette option).

## Code

| Fichier | Rôle |
| --- | --- |
| `Source/LooperEngine.h` | Le looper : capture (directe ou « pré »), répétition, pitch, reverse, decay, refresh, fondus, calage du départ. Aucune dépendance, testable seul. |
| `Source/StutterGate.h` | Le stutter : gate rythmique à 16 pas, forme, profondeur, swing. Aucune dépendance. |
| `Source/OutputFilter.h` | Le filtre de fin de chaîne (variable d'état, passe-haut / passe-bande / passe-bas, 12 ou 24 dB) et son limiteur. Aucune dépendance. |
| `Source/PluginProcessor.*` | Paramètres, lecture du tempo et de la position de l'hôte, ordre de la chaîne, mélange dry / wet, sauvegarde d'état. |
| `Source/PluginEditor.*` | Interface (design clair en relief, dessin vectoriel, quatre modules sur une même grille ; le fond est dessiné une fois et gardé en image). |
| `Assets/` | Police Chakra Petch embarquée dans le plugin (licence OFL, voir `ChakraPetch-OFL.txt`). |
| `Tests/EngineTests.cpp` | Tests du traitement audio : boucle exacte, absence de clics, grille sans dérive, changements de durée, refresh, fondus, stutter (motif, swing, profondeur), filtre (types, pentes, limiteur), capture « pré », pitch, reverse, decay, calage, fuzz. |
| `Tests/PluginTests.cpp` | Tests du plugin complet : sync au tempo, modes indépendants, ordre de chaîne, triolets, pitch, capture « pré », calage, mix, état (y compris un ancien état), interface. |

Lancer les tests : ajouter `-DLOOPROLL_BUILD_TESTS=ON` à la configuration, compiler, puis `ctest --test-dir build -C Release --output-on-failure`.

Réglages rapides :

- Nom du plugin et nom d'éditeur affichés dans le DAW : `LOOPROLL_PRODUCT_NAME` et `LOOPROLL_COMPANY_NAME` en haut de `CMakeLists.txt` (le nom dessiné dans l'interface est dans `paintReadouts()`, `PluginEditor.cpp`).
- Divisions du mode sync : `getDivisionNames()` / `getDivisionInBeats()` pour la boucle et le stutter, `getRefreshDivisionNames()` / `getRefreshDivisionInBeats()` pour le refresh, `getFeelNames()` / `getFeelFactor()` pour triolet et pointé, dans `PluginProcessor.cpp`. Dans chaque paire les deux listes doivent rester alignées, de la plus longue à la plus courte.
- Plages du mode free : `minTimeMs` / `maxTimeMs`, `minRefreshMs` / `maxRefreshMs` et `minStutterMs` / `maxStutterMs` dans `PluginProcessor.h`.
- Résonance maximale du filtre : `qualityFor()` dans `OutputFilter.h` (Q de 0,7 à 10).

Ne pas changer `PLUGIN_CODE`, `PLUGIN_MANUFACTURER_CODE`, l'étiquette d'état `"LoopRoll"` (`PluginProcessor.cpp`) ni les identifiants de paramètres une fois le plugin utilisé dans des projets : les DAW ne retrouveraient plus le plugin ou ses automations.

## Licence de JUCE

Le plugin est construit avec [JUCE](https://juce.com) 8, distribué sous AGPLv3 ou sous licence commerciale. Pour un usage personnel ça ne change rien ; avant de distribuer le plugin, vérifier les conditions en vigueur sur juce.com.
