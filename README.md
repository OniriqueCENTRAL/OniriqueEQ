# ONIRIQUE EQ

EQ paramétrique 8 bandes : le même EQ que la VOCALBOX (algorithme, analyseur, bandes, écoute), en plugin à part.
Pas d'EQ ANALOG ici : seulement le paramétrique.

Trois traitements au choix (menu déroulant en haut de l'EQ), affichés en direct dans la pastille à côté du titre :

- **Zero Latency** : filtres IIR "appariés à l'analogique" (méthode de Vicanek), double précision, aucune latence.
- **Natural Phase** : filtre FIR à phase minimale (16384 coefficients à 44.1 / 48 kHz), calculé à partir de la réponse
  analogique exacte des bandes : amplitude analogique jusqu'à Nyquist (aucun resserrement dans l'extrême aigu) et la phase
  d'un EQ analogique. Aucune latence.
- **Linear Phase** : filtre FIR à phase nulle (32768 coefficients à 44.1 / 48 kHz, fenêtre de Tukey) : aucun déphasage.
  Latence 16384 échantillons (341 ms à 48 kHz), compensée par le DAW. Le fond d'un notch très étroit dans le grave
  (ex. 60 Hz, Q 8) s'arrête vers -20 dB : limite de toute phase linéaire de longueur finie (Zero Latency / Natural Phase
  descendent bien plus bas).

Les tailles des FIR doublent à 88.2 / 96 kHz (même résolution en Hz). Les bandes DYNAMIQUES restent en IIR dans les modes
FIR (leur gain bouge à chaque instant), après le FIR. Chaque réglage fabrique un nouveau FIR sur un thread à part, appliqué en
fondu de 50 ms (juce::dsp::Convolution, partitions non uniformes, sans latence ajoutée).

Types : bell, low shelf, high shelf, notch, low cut / high cut 12-24-36-48 dB/oct, mode DYNAMIQUE par bande, écoute d'une
bande (casque : maintenir le point de la bande), OUTPUT, BYPASS (signal direct aligné sur la latence), presets d'usine +
utilisateur (%APPDATA%/Onirique/EQ/Presets, *.oeqpreset), licence ONIRIQUE (identifiant `onirique-eq`).

## Générer le .vst3 (Windows)
Prérequis : Visual Studio Build Tools 2022 (charge C++), CMake, Git.

    build_windows.bat

Résultat : build\OniriqueEQ_artefacts\Release\VST3\ONIRIQUE EQ.vst3

## Mac
    ./build_mac.sh

Sans Mac : chaque envoi sur main lance `.github/workflows/build.yml`, qui compile Mac (universel, VST3 + AU) et Windows,
puis publie la version dans ONIRIQUE CENTRAL (site et application). Le message du commit devient la note de version.

## Fichiers
- Source/DSP/ParametricEQ.h : les 8 bandes IIR (identique à la VOCALBOX)
- Source/DSP/EqAnalog.h     : réponse analogique exacte des bandes (base des FIR et de leur courbe)
- Source/DSP/FirEQ.h        : fabrication des FIR (phase minimale par le cepstre, phase linéaire) et convolution
- Source/DSP/EqEngine.h     : chemin audio (mode, écoute, EQ ON / BYPASS, OUTPUT)
- Source/UIKit.*            : interface reprise de la VOCALBOX (analyseur, bandes, presets, style) + menu du traitement
- Source/PluginEditor.*     : fenêtre (en-tête, EQ), redimensionnable, taille mémorisée
- Source/FactoryPresets.inc : presets d'usine
- License/                  : licence ONIRIQUE (vérification, pastille, page d'activation)

Captures locales sans la page d'activation : compiler avec `/DONIRIQUE_UI_TEST=1` (jamais dans les versions publiées)
et lancer avec la variable d'environnement `ONIRIQUE_UI_TEST=nolicense`.
