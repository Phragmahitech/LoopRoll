# Phragments — by Phragma

Effet audio VST3 pour macOS et Windows : looper / beat-repeat, stutter (gate rythmique à pas), pitch et filtre résonant.

Le plugin s'est appelé LoopRoll (jusqu'à la v0.4) puis Fragment (v1.0). Le dépôt, le projet CMake et les classes gardent le nom LoopRoll ; seuls le nom affiché et le fichier `.vst3` ont changé. Les identifiants internes sont les mêmes : un projet fait avec une ancienne version se rouvre avec Phragments.

Quatre modules côte à côte, chacun avec son grand potard, son bouton marche / arrêt et ses réglages :

- **LOOP** : capture l'audio à partir de l'instant où tu l'allumes, puis le répète. Éteint = signal direct.
  - **LENGTH** : durée de la boucle. Tourner à droite raccourcit. **SYNC** : 1/1 à 1/128, calé sur le tempo. **FREE** : de 4 s à 10 ms.
  - **IN / OUT** : fondu d'entrée et de sortie sur chaque passage, en % de sa durée.
  - **REVERSE** : les répétitions sont lues à l'envers.
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
- **CHAIN** : ordre du stutter et du looper, **PITCH** et **MIX**. Le trajet du son est toujours : looper et stutter (dans l'ordre choisi), pitch, filtre, mix.
  - **LOOP > STUTTER** (par défaut) : la boucle répète le son brut et le gate hache le résultat.
  - **STUTTER > LOOP** : le son est haché d'abord, la boucle capture le résultat et le répète tel quel.
  - **PITCH** : transpose tout le son qui sort du looper et du stutter (boucle allumée ou non), de -12 à +12 demi-tons, sans changer sa durée. **SEMI** : par demi-tons, le potard se pose sur le demi-ton le plus proche quand on le lâche. **FREE** : en continu, au centième de demi-ton. À 0, le son passe tel quel.
  - **MIX** : dry / wet sur l'ensemble du plugin.
- LOOP, REFRESH et STUTTER ont chacun leur interrupteur FREE / SYNC, indépendant des autres.
- LOOP, STUTTER et FILTER s'allument séparément : chacun marche seul ou avec les autres. REFRESH n'agit que quand LOOP est allumé.
- Mono et stéréo, latence nulle, tous les contrôles sont automatisables. Le sens des potards est celui des paramètres : en automation ou en mapping MIDI aussi, monter la valeur raccourcit les durées.

## Presets et taille de la fenêtre

En haut de la façade : `‹  NOM ▾  ›  SAVE  100 %`.

- **‹ ›** : preset précédent / suivant (DEFAULT, puis les presets enregistrés, par ordre alphabétique, en boucle).
- **Nom** : menu des presets, SAVE AS…, DELETE (le fichier part à la corbeille), SHOW PRESETS FOLDER. Une étoile `*` signale un preset modifié.
- **SAVE** : panneau dans la façade ; Entrée = enregistrer, Échap = annuler. Si le nom existe, le bouton devient REPLACE.
- **100 %** : taille de la fenêtre (60, 75, 100, 125, 150 %). On peut aussi tirer le coin en bas à droite. La taille est gardée avec le projet.

Les presets sont des fichiers `.phgpreset` (XML, valeurs en unités réelles) :
- macOS : `~/Library/Audio/Presets/Phragma/Phragments/`
- Windows : `%APPDATA%\Phragma\Phragments\Presets\`

Un preset garde tous les paramètres. Le projet du DAW garde le nom du preset en cours. Un paramètre absent du fichier revient à sa valeur par défaut ; une valeur hors plage est bornée ; un fichier illisible est refusé sans rien changer.

## Comportement

- Le premier passage laisse passer l'audio tel quel, les suivants le répètent. Sans REVERSE, chaque répétition est la copie exacte de l'enregistrement.
- Le raccord de boucle est fondu sur 3 ms **avant** la fin de boucle, vers l'audio qui précédait le point de capture. Le début de la boucle est donc toujours rejoué à plein niveau : un kick capté sur le temps garde son attaque à chaque répétition.
- REVERSE : une répétition inversée est encadrée par une fenêtre de 1,5 ms à chaque raccord (son attaque est donc très légèrement adoucie). Le réglage est pris en compte au début de chaque passage, pas en cours de route.
- PITCH est un pitch-shifter temps réel, à grains : le son est relu plus vite ou plus lentement dans une mémoire de quelques dizaines de ms, et raccordé en phase avec lui-même (recherche de la période). La durée est conservée, aucune latence n'est déclarée à l'hôte. Ce que ça implique :
  - Le son transposé sort avec un léger retard, variable : quelques ms pour un petit écart, 10 à 20 ms vers l'octave (davantage sur une basse très grave). À 0, aucun retard : le signal passe à l'échantillon près.
  - Sur un son seul (basse, lead, kick), les raccords tombent sur un nombre entier de périodes : hauteur exacte, niveau stable. Sur plusieurs sons sans rapport harmonique joués ensemble, le raccord ne peut être en phase que pour le plus fort ; les autres deviennent un peu rugueux et légèrement désaccordés. C'est la limite de ce type de pitch.
  - Vers l'aigu, de courts morceaux sont rejoués (une attaque peut doubler, à quelques ms d'écart) ; vers le grave, de courts morceaux sont sautés (une attaque très brève peut disparaître à -12).
  - Une automation continue (montée, descente) est suivie sans palier. En SEMI, la valeur est arrondie au demi-ton le plus proche, y compris en automation.
- En SYNC, changer de division pendant la boucle garde la grille calée sur le point de départ. La durée est suivie à la fraction d'échantillon : pas de dérive sur la longueur. En FREE, tourner le potard pendant la boucle ne déplace que la fin de boucle.
- REFRESH : à chaque point de refresh, une nouvelle capture démarre. Les points sont comptés depuis l'allumage de LOOP, sans dérive. Si l'intervalle de refresh est plus court que la boucle, rien n'a le temps de se répéter.
- Fondus : l'enveloppe s'applique à tous les passages, y compris le premier. Si les deux se chevauchent (plus de 100 % au total), l'enveloppe forme un triangle. Un fondu plus court que 3 ms ne descend pas jusqu'au silence, pour ne pas claquer.
- STUTTER en SYNC : quand le transport de l'hôte tourne, les pas et le motif tombent sur sa grille (à 1/16, le motif de 16 pas fait une mesure et repart au début de chaque mesure). Transport arrêté, ou en FREE, le gate tourne librement et repart du premier pas à l'allumage.
- STUTTER : l'ouverture de chaque pas est adoucie sur 1,5 ms et la fin du pas retombe sur au moins 2 ms (moins sur les pas très courts), pour ne pas claquer.
- FILTER : la fréquence et la résonance sont lissées, l'allumage et les changements de type ou de pente passent par un fondu de 5 ms. En 24 dB, le second étage n'ajoute pas de résonance : le pic reste le même qu'en 12 dB. En passe-bande le pic vaut racine de Q. En passe-haut et passe-bas, la résonance à fond ajoute jusqu'à +20 dB à la fréquence de coupure.
- LIMITER : attaque instantanée, relâchement de 100 ms, plafond à 0 dBFS. Il ne touche pas au signal tant que la sortie du filtre reste sous le plafond, et ne travaille que quand FILTER est allumé. Sur un son déjà proche de 0 dBFS, un filtre peut faire dépasser les crêtes : le limiteur les retient alors (l'éteindre pour entendre le filtre brut).
- MIX : mélange entre l'entrée du plugin et la sortie de toute la chaîne. À 100 %, la sortie est celle de la chaîne, sans calcul supplémentaire.
- Compatibilité : un projet sauvegardé avec une version précédente se rouvre, les nouveaux réglages prennent leur valeur neutre. Réserves : une automation écrite avant la v1.1 sur la division de la boucle ou du stutter, ou sur le type de filtre, se décale légèrement (ces listes ont gagné une valeur : 1/128, passe-bas) ; les réglages PRE, SNAP, MOMENT et DECAY de la v1.1 n'existent plus (un projet qui les utilisait se rouvre sans eux), et PITCH, qui transposait chaque répétition en v1.1, transpose maintenant tout le son.
- L'interface affiche son nom et son numéro de version dans l'en-tête : c'est le moyen le plus rapide de vérifier quelle version le DAW a chargée.
- Si l'hôte ne fournit pas de tempo, le dernier tempo connu est utilisé (120 BPM par défaut).
- La capture démarre au début du bloc audio dans lequel LOOP passe à ON : la précision dépend de la taille de buffer du DAW (≈ 3 ms à 128 échantillons, ≈ 12 ms à 512).

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
| `Source/LooperEngine.h` | Le looper : capture, répétition, reverse, refresh, fondus. Aucune dépendance, testable seul. Il sait aussi faire ce que la v1.1 proposait et que l'interface n'expose plus (capture « pré », pitch et decay par répétition, départ calé sur la grille) : ce code reste en place, testé, mais le plugin ne l'appelle pas. |
| `Source/PitchShifter.h` | Le pitch : transposition temps réel de tout le son, à grains, avec raccords en phase. Aucune dépendance. |
| `Source/StutterGate.h` | Le stutter : gate rythmique à 16 pas, forme, profondeur, swing. Aucune dépendance. |
| `Source/OutputFilter.h` | Le filtre de fin de chaîne (variable d'état, passe-haut / passe-bande / passe-bas, 12 ou 24 dB) et son limiteur. Aucune dépendance. |
| `Source/PluginProcessor.*` | Paramètres, lecture du tempo et de la position de l'hôte, ordre de la chaîne, mélange dry / wet, sauvegarde d'état. |
| `Source/PluginEditor.*` | Interface (design clair en relief, dessin vectoriel, quatre modules sur une même grille ; le fond est dessiné une fois et gardé en image). |
| `Source/PresetLibrary.h`, `Source/PresetBar.h` | Presets utilisateur (fichiers, menu, panneau SAVE) et bouton de taille de la fenêtre. Mêmes fichiers dans Phibres et Phractal. |
| `Assets/` | Police Chakra Petch embarquée dans le plugin (licence OFL, voir `ChakraPetch-OFL.txt`). |
| `Tests/EngineTests.cpp` | Tests du traitement audio : boucle exacte, absence de clics, grille sans dérive, changements de durée, refresh, fondus, stutter (motif, swing, profondeur), filtre (types, pentes, limiteur), reverse, pitch (hauteur, niveau, clics, stéréo, automation), fonctions du moteur non exposées, fuzz. |
| `Tests/PluginTests.cpp` | Tests du plugin complet : sync au tempo, modes indépendants, ordre de chaîne, triolets, reverse, pitch (demi-tons et libre), mix, état (y compris d'anciens états), interface. |

Lancer les tests : ajouter `-DLOOPROLL_BUILD_TESTS=ON` à la configuration, compiler, puis `ctest --test-dir build -C Release --output-on-failure`.

Réglages rapides :

- Nom du plugin et nom d'éditeur affichés dans le DAW : `LOOPROLL_PRODUCT_NAME` et `LOOPROLL_COMPANY_NAME` en haut de `CMakeLists.txt` (le nom dessiné dans l'interface est dans `paintBackground()`, `PluginEditor.cpp`).
- Divisions du mode sync : `getDivisionNames()` / `getDivisionInBeats()` pour la boucle et le stutter, `getRefreshDivisionNames()` / `getRefreshDivisionInBeats()` pour le refresh, `getFeelNames()` / `getFeelFactor()` pour triolet et pointé, dans `PluginProcessor.cpp`. Dans chaque paire les deux listes doivent rester alignées, de la plus longue à la plus courte.
- Plages du mode free : `minTimeMs` / `maxTimeMs`, `minRefreshMs` / `maxRefreshMs` et `minStutterMs` / `maxStutterMs` dans `PluginProcessor.h`.
- Résonance maximale du filtre : `qualityFor()` dans `OutputFilter.h` (Q de 0,7 à 10).

Ne pas changer `PLUGIN_CODE`, `PLUGIN_MANUFACTURER_CODE`, l'étiquette d'état `"LoopRoll"` (`PluginProcessor.cpp`) ni les identifiants de paramètres une fois le plugin utilisé dans des projets : les DAW ne retrouveraient plus le plugin ou ses automations.

## Licence de JUCE

Le plugin est construit avec [JUCE](https://juce.com) 8, distribué sous AGPLv3 ou sous licence commerciale. Pour un usage personnel ça ne change rien ; avant de distribuer le plugin, vérifier les conditions en vigueur sur juce.com.
