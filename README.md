# OneOhOne

Monosynthé inspiré du Roland SH-101. Une entrée MIDI, une sortie audio, aucun effet (l'original n'en a pas). Formats : VST3 (Mac et Windows) et Audio Unit (Mac).

## Ce qui est modélisé

Chaque bloc reproduit le comportement du circuit d'origine, pas juste son nom :

- **VCO** : dent de scie et pulse avec PWM (source Env, Manual ou LFO), coeur de type CEM3340. Range 16', 8', 4', 2', tune ±50 cents, légère dérive de pitch analogique (~2 cents).
- **Sub-oscillateur** : obtenu en divisant le VCO comme les bascules de l'original, donc verrouillé en phase. Modes -1 oct carré, -2 oct carré, -2 oct pulse 25 %.
- **Bruit blanc** dans le mixer, avec les 4 niveaux (pulse, saw, sub, noise). Pousser tout le mixer sature l'entrée du filtre, comme sur la machine.
- **VCF** : passe-bas 4 pôles OTA type IR3109. Écrêtage par diodes dans la boucle de résonance (c'est ce qui limite l'auto-oscillation sur le schéma d'origine), compensation de niveau prise côté sortie comme sur le 101. Auto-oscillation juste, utilisable en sinus avec Kybd à 100 %.
- **Enveloppe** ADSR à courbes RC exponentielles, modes Gate+Trig, Gate (legato sans retrigger) et LFO.
- **LFO** triangle, carré, random (S&H), bruit. 0,1 à 30 Hz.
- **VCA** en mode Env ou Gate, couplage AC en sortie.
- **Portamento** Auto / Off / On (Auto = glisse seulement en legato), lag exponentiel comme un RC.
- Plages des potards reprises des specs officielles (attack 1,5 ms à 4 s, decay et release 2 ms à 10 s, VCF 10 Hz à 20 kHz, portamento 0 à 5 s).
- Oversampling x2, oscillateurs anti-aliasés (PolyBLEP).

Pas inclus : l'arpégiateur et le séquenceur. Mets l'Arpeggiator d'Ableton (MIDI Effects) devant le plugin.

## Contrôles MIDI

- Pitch bend : bender, profondeur réglée par les faders Bender VCO et VCF.
- Molette (CC1) : vibrato par le LFO, profondeur réglée par Bender LFO (équivalent du levier MOD).
- Pas de vélocité, comme l'original. Priorité à la dernière note jouée.
- Double-clic sur un fader : retour à la valeur par défaut.

## Compiler sans rien installer (GitHub Actions)

1. Crée un compte sur github.com, puis **New repository**. Public = minutes de build illimitées. Privé marche aussi, mais chaque build Mac compte 10x dans le quota gratuit.
2. Dans le repo vide, clique **uploading an existing file** et sélectionne les 7 fichiers du dossier (pas le dossier lui-même) et glisse-les dans la page. Commit.
3. **Add file > Create new file**. Nom exact : `.github/workflows/build.yml`. Colle le contenu du fichier `build.yml` fourni. Commit. (Le Finder cache les dossiers qui commencent par un point, d'où cette étape à la main.)
4. Onglet **Actions** : le build démarre tout seul, compte 10 à 20 min. Quand il est vert, ouvre le run et télécharge l'artefact `OneOhOne-mac` ou `OneOhOne-windows` en bas de la page. Dézippe jusqu'à obtenir `OneOhOne.vst3` (et `OneOhOne.component` sur Mac).

## Installer

**Mac**

1. Finder > Aller > Aller au dossier (Cmd+Maj+G) > `~/Library/Audio/Plug-Ins/VST3` : copie `OneOhOne.vst3`.
2. Même chose dans `~/Library/Audio/Plug-Ins/Components` : copie `OneOhOne.component`.
3. Dans Terminal, colle :

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/OneOhOne.vst3 ~/Library/Audio/Plug-Ins/Components/OneOhOne.component
```

Sans cette commande, macOS bloque le plugin parce qu'il n'est pas signé par un compte développeur Apple payant. Si Ableton dit que le plugin est endommagé, c'est ça.

**Windows**

Copie le dossier `OneOhOne.vst3` dans `C:\Program Files\Common Files\VST3`.

## Dans Ableton

1. Settings (Cmd+, ou Ctrl+,) > **Plug-Ins** : active *Use VST3 Plug-In System Folders*, et sur Mac *Use Audio Units v2*.
2. Clique **Rescan**.
3. Browser > Plug-Ins > Gustave > OneOhOne. Glisse-le sur une piste MIDI. Sur Mac il apparaît en VST3 et en AU, même son, prends le VST3.

## Modifier le son

Tous les réglages fins sont dans `Engine.h` : les courbes des potards dans `namespace mapping`, et les constantes `kMixerGain` (drive du filtre), `kResComp` (compensation de résonance), `kOutputGain`, `knee` (seuil des diodes). Modifie, commit sur GitHub, le build se relance tout seul.

## Compiler en local (optionnel)

- Mac : Xcode + CMake (`brew install cmake`), puis
  `cmake -B build -DCMAKE_BUILD_TYPE=Release` et `cmake --build build --config Release`
- Windows : Visual Studio 2022 (charge de travail C++) + CMake, mêmes commandes.

Les plugins sortent dans `build/OneOhOne_artefacts/Release/`.

## Légal

Aucun code, firmware ni graphisme Roland. Les brevets de 1982 sont expirés et le fonctionnement d'un circuit n'est pas protégé par le droit d'auteur. La marque SH-101 l'est encore, d'où le nom OneOhOne si tu le partages un jour.
