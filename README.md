# ONIRIQUE VOCALBOX (JUCE 8 / VST3)

Channel strip vocal : INPUT -> NOISE GATE (toujours en premier, repris du BROADCAST) -> EQ PARAMÉTRIQUE (8 bandes, nettoyage) -> RESONANCE -> COMPRESSION (1176 / LA-2A / DUO) -> PARALLEL (1176 écrasé remis dans la voix, AMOUNT) -> MULTIBAND (opto 4 bandes) -> EQ ANALOG (LOW / MID / HIGH, couleur) -> DE-ESSER (x2) -> SATURATION LAB -> AUTO GAIN -> OUTPUT (+ BYPASS)
Chaque module (EQ, EQ ANALOG, RESONANCE, COMPRESSION, PARALLEL, MULTIBAND, DE-ESSER, SATURATION LAB) a son bouton power (coupure sans clic, latence conservée).
Modes : MIX (qualité maximale, latence ~44 ms compensée par le DAW) / LIVE (latence nulle, pour s'enregistrer à travers le plugin).
Ordre de traitement des 7 modules réglable à la souris dans l en-tête (glisser-déposer, fondu de 15 ms). Les projets enregistrés avec l'ancien ordre à 6 modules sont convertis au chargement (l'EQ ANALOG se place juste après le MULTIBAND).
Suréchantillonnage de TOUTE la chaîne de modules (EQ -> SATURATION) OFF / 2X / 4X / 8X, 2X par défaut (MIX uniquement).
EQ paramétrique : bell / low shelf / high shelf / notch / low cut et high cut 12-24-36-48 dB/oct, mode DYNAMIQUE par bande (clic droit sur un point), plein écran dans la fenêtre du plugin.
ASSISTANT IA (bouton en haut) : écoute la voix pendant la lecture, l'analyse et règle tous les modules (calcul local, voir Source/Assistant/AssistantEngine.h).

## Générer le .vst3 (Windows)
Prérequis : Visual Studio Build Tools 2022 (charge C++), CMake, Git.

    build_windows.bat

ou à la main :

    cmake -B build
    cmake --build build --config Release

Résultat : build\OniriqueVocalBox_artefacts\Release\VST3\ONIRIQUE VOCALBOX.vst3
Copier ce dossier dans C:\Program Files\Common Files\VST3 puis rescanner les plugins dans le DAW.
La première compilation télécharge et compile JUCE : plusieurs minutes.

## Générer le plugin Mac (VST3 + AU + Standalone, universel Intel / Apple Silicon)
Impossible depuis Windows : il faut macOS et Xcode.

Sur un Mac (Xcode ou `xcode-select --install`, CMake, Git) :

    ./build_mac.sh

Sans Mac : pousser le projet sur GitHub, le workflow `.github/workflows/build.yml` compile Mac et Windows ;
le zip `OniriqueVocalBox-mac` se télécharge dans l onglet Actions.

Installation : `.vst3` -> ~/Library/Audio/Plug-Ins/VST3, `.component` -> ~/Library/Audio/Plug-Ins/Components.
Plugin non notarisé : après téléchargement, lever la quarantaine :

    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"ONIRIQUE VOCALBOX.vst3" ~/Library/Audio/Plug-Ins/Components/"ONIRIQUE VOCALBOX.component"

Presets utilisateur sur Mac : ~/Library/Onirique/VocalBox/Presets.

## Structure
- CMakeLists.txt, build_windows.bat, build_mac.sh, .github/workflows/build.yml
- Source/Parameters.h        : IDs + layout APVTS (40 paramètres automatables : COMPRESSION, LOW CUT, air du HIGH, SMART S, bandes RESONANCE (fréquence, largeur, sensibilité), réglages, type de saturation, fréquence du LOW, power par module, Bypass)
- Source/PluginProcessor.*   : paramètres, latence, bypass hôte, crête de sortie pour l'interface
- Source/PluginEditor.*      : interface redimensionnable (taille mémorisée) dans la DA du site, animations 60 i/s, afficheur RESONANCE interactif, analyseur temps réel (FFT 8192, lissage 1/6 d octave, courbe exacte de l EQ, calques en cache), EQ plein écran, barre de presets
- Source/AssistantPanel.cpp   : fenêtre de l ASSISTANT IA (style, intensité, écoute, progression, résultats, APPLY / UNDO)
- Source/Assistant/AssistantEngine.h : analyse de la voix (niveaux, équilibre tonal comparé à une cible, sifflantes, plosives, résonances, hauteur / auto-tune) et réglages ; compresseurs et de-essers ajustés en faisant tourner la chaîne hors temps réel sur la voix captée
- Source/PresetManager.h     : presets usine + utilisateur (%APPDATA%/Onirique/VocalBox/Presets/*.ovbpreset)
- Source/DSP/SpectrumTap.h   : prise audio sans verrou pour l analyseur
- Resources/logo.png         : sigil du site, intégré au plugin (BinaryData)
- Source/DSP/VocalChain.h    : chaîne complète, interrupteurs de module, bypass avec fondu, sécurité NaN/Inf
- Source/DSP/VocalEQ.h       : EQ ANALOG : LOW CUT 85 Hz, LOW (Pultec-like, 80 / 100 / 125 Hz) / MID / HIGH (air 8K / 10K / 20K), fin de course plus généreuse
- Source/DSP/NoiseGate.h     : noise gate transparent d ONIRIQUE BROADCAST (chaîne latérale filtrée, anticipation 1.5 ms en MIX, hystérésis, HOLD, RANGE, LEARN), sans la garde d écho
- Source/DSP/ParametricEQ.h  : EQ paramétrique 8 bandes ultra clean (filtres appariés à l'analogique, sans resserrement près de Nyquist), pentes jusqu'à 48 dB/oct, notch, mode dynamique par bande, édité dans l'analyseur
- Source/DSP/Comp1176.h      : 1176 (FET, contre-réaction, attaque 20-800 us, ratios 4 / 8 / 12 / 20 / ALL)
- Source/DSP/ParallelComp.h  : compression parallèle "New York" (copie de la voix -> 1176 INPUT +16 dB ratio ALL, bus ramené au RMS de la voix puis remélangé selon AMOUNT, 0 latence)
- Source/DSP/CompLA2A.h      : LA-2A (cellule optique, relâchement en deux temps, COMPRESS / LIMIT)
- Source/DSP/CompressorModule.h : solo / duo, ordre inversable en fondu (paire clonée, sans saut)
- Source/DSP/MultibandOpto.h : multibande opto 4 bandes (Linkwitz-Riley, somme plate), compression crémeuse pour la voix
- Source/MultibandPresets.h  : presets internes du multibande (Vocal Smooth, De-Harsh, Air Control...)
- Source/DSP/ResonanceSuppressor.h : suppression dynamique des résonances (STFT en MIX ; en LIVE, 28 cloches dynamiques pilotées par l'analyse, latence nulle), écoute LISTEN, mesure pour l'afficheur
- Source/DSP/PlosiveSuppressor.h : détection intelligente des plosives (bouffée grave qui domine le médium), shelf grave dynamique, latence nulle
- Source/DSP/DeEsser.h       : de-esser dynamique 2 bandes + anticipation ; la 2e bande = SMART S du HIGH (activable)
- Source/DSP/SaturationLab.h : sélecteur TUBE / ANALOG / TAPE, changement de type en fondu 60 ms, volume compensé quel que soit le DRIVE
- Source/DSP/SatStage.h      : base commune (suréchantillonnage x8, DRIVE 0 à +30 dB, compensation de volume, transitoires)
- Source/DSP/SatTube.h       : saturation tube asymétrique, transitoires préservés (son d'origine)
- Source/DSP/SatAnalog.h     : classe A + transformateurs, caractère inspiré du Neve 1073
- Source/DSP/SatTape.h       : bande : aigus tassés, compression lente, head bump
- Source/DSP/AutoGain.h      : compensation de loudness lente et stable
- Source/DSP/Biquad.h, Common.h
