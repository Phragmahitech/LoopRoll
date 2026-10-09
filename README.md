# LoopRoll

Effet audio VST3 de type looper / beat-repeat, pour macOS et Windows.

- **LOOP** : capture l'audio à partir de l'instant où tu l'allumes, puis le répète. Éteint = signal direct.
- **Potard de durée** : un seul potard, deux modes.
  - **SYNC** : calé sur le tempo de l'hôte — 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1/1.
  - **FREE** : durée libre de 10 ms à 4 s.
- Mono et stéréo, latence nulle, les trois contrôles sont automatisables.

## Comportement

- Le premier passage laisse passer l'audio tel quel, les suivants le répètent à l'identique (copie exacte de l'enregistrement).
- Le raccord de boucle est fondu sur 3 ms **avant** la fin de boucle, vers l'audio qui précédait le point de capture. Le début de la boucle est donc toujours rejoué à plein niveau : un kick capté sur le temps garde son attaque à chaque répétition.
- En SYNC, changer de division pendant la boucle garde la grille calée sur le point de départ (passer de 1/4 à 1/16 puis revenir à 1/4 retombe sur les mêmes temps). La durée est suivie à la fraction d'échantillon : pas de dérive sur la longueur.
- En FREE, tourner le potard pendant la boucle ne déplace que la fin de boucle.
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

- **macOS** : copier `LoopRoll.vst3` dans `~/Library/Audio/Plug-Ins/VST3/`, puis dans Terminal :
  `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/LoopRoll.vst3`
  (le plugin n'est pas notarisé par Apple, macOS le bloque sinon).
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
| `Source/LooperEngine.h` | Tout le traitement audio. Aucune dépendance, testable seul. |
| `Source/PluginProcessor.*` | Paramètres, lecture du tempo de l'hôte, sauvegarde d'état. |
| `Source/PluginEditor.*` | Interface. |
| `Tests/EngineTests.cpp` | Tests du moteur : boucle exacte, absence de clics, grille sans dérive, changements de durée, fuzz. |
| `Tests/PluginTests.cpp` | Tests du plugin complet : sync au tempo, modes, état, interface. |

Lancer les tests : ajouter `-DLOOPROLL_BUILD_TESTS=ON` à la configuration, compiler, puis `ctest --test-dir build -C Release --output-on-failure`.

Réglages rapides :

- Nom d'éditeur affiché dans le DAW : `LOOPROLL_COMPANY_NAME` en haut de `CMakeLists.txt`.
- Divisions du mode sync : `getDivisionNames()` et `getDivisionInBeats()` dans `PluginProcessor.cpp` (les deux listes doivent rester alignées).
- Plage du mode free : `minTimeMs` / `maxTimeMs` dans `PluginProcessor.h`.

Ne pas changer `PLUGIN_CODE`, `PLUGIN_MANUFACTURER_CODE` ni les identifiants de paramètres une fois le plugin utilisé dans des projets : les DAW ne retrouveraient plus le plugin ou ses automations.

## Licence de JUCE

Le plugin est construit avec [JUCE](https://juce.com) 8, distribué sous AGPLv3 ou sous licence commerciale. Pour un usage personnel ça ne change rien ; avant de distribuer le plugin, vérifier les conditions en vigueur sur juce.com.
