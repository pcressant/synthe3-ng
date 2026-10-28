# Extension NVDA : Synthé3-ng, voix Michel

La version 0.5.0 fournit un paquet `.nvda-addon` pour **NVDA 2026.2, processus
Windows x64**. La voix reste française. Le moteur, le DSP et la préparation du
texte sont partagés avec Linux ; le module Speech Dispatcher reste disponible.

## Installer et essayer

Copier `synthe3-ng-0.5.0.nvda-addon` sur Windows, l’ouvrir pour l’installer,
puis redémarrer NVDA lorsque proposé. Dans les paramètres Parole, choisir le
synthétiseur **Synthé3-ng — Michel (français)**. Le guide intégré est disponible
dans le gestionnaire d’extensions, dans l’aide de Synthé3-ng.

Les valeurs initiales sont débit 50, hauteur 50, volume 100 et inflexion 0.
Elles correspondent aux réglages moteur Linux 4/5, au gain unitaire et à
l’intonation historique. Le débit 77 atteint le niveau moteur 13. Les seize
niveaux du moteur sont conservés, donc les curseurs ne produisent pas cent
réglages acoustiques différents. L’inflexion positive active la double passe :
20 correspond à 1,5 demi-ton, 40 à 3 et 100 à 7,5.

Le premier essai utile est une session NVDA ordinaire : saisie, navigation rapide,
Contrôle pour interrompre, lecture continue et pause/reprise. En cas de problème,
revenir au synthétiseur précédent et conserver le journal NVDA ainsi que les
versions du système et le périphérique audio utilisé.

Cette première version est validée automatiquement avec le vrai moteur et une
sortie audio simulée, ainsi qu’avec la DLL exécutée sous Wine. L’utilisateur
confirme un fonctionnement sans problème pendant de longues sessions de travail
avec **NVDA 26.3 (2026.3) sous Windows 11 26H2**, et une très bonne réactivité.
Le périphérique audio n’a pas été renseigné ; la latence acoustique n’a pas été mesurée. Les champs de
compatibilité du manifeste désignent l’API ciblée, sans constituer une mesure
ou une certification d’usage matériel.

## Architecture

Le paquet contient un pilote Python dans `synthDrivers/synthe3_ng`, sa DLL C,
l’aide utilisateur française/anglaise et les licences. Le code source et la
documentation de développement sont disponibles sur
[GitHub](https://github.com/pcressant/synthe3-ng).
Python est celui de NVDA ; aucun interpréteur, serveur, sous-processus ou fichier
WAV supplémentaire n’est lancé pendant la parole.

- `_native.py` déclare les signatures ctypes et garde les callbacks en vie.
  L’ABI 2 expose la préparation UTF-8 commune : texte, marques et mode caractère.
  Les callbacks Python interviennent seulement à la livraison d’un fragment ou
  d’une marque ; l’annulation pendant les calculs reste une lecture atomique en C.
- `_worker.py` possède une file et un unique thread persistant. Ce thread dort sur
  une condition bloquante au repos. Les jetons d’annulation sont créés avant de
  rendre les travaux visibles et sont libérés après leur utilisation.
- Le point d’entrée `__init__.py` traduit les commandes NVDA, gère les réglages et
  renvoie les notifications via la file principale de NVDA. Une seconde
  vérification de génération écarte les notifications déjà en file lors d’un arrêt.
- La lecture utilise `nvwave.WavePlayer`, en PCM mono 16 bits à 22 050 Hz, sur la
  sortie choisie dans NVDA. Les livraisons comptent au plus 256 échantillons,
  soit environ 11,6 ms d’audio ; cela ne fixe pas la taille du tampon matériel.

Le rendu conserve les fragments de 240 octets Latin-1 et le DSP historique.
Une marque ne force pas de coupure de fragment. Les changements effectifs de
prosodie ou de mode caractère terminent le fragment avant d’appliquer le réglage.
Les indices reposent sur les frontières approximatives de mots/diphones du moteur.
La ponctuation est préparée par NVDA ; sa réexpansion est désactivée dans la voie
native NVDA. Aucune analyse SSML n’est ajoutée au pilote.

Le pilote prend en charge indices, épellation, pauses explicites, débit, hauteur
et volume dans les séquences. La hauteur des majuscules fonctionne avec
`PitchCommand`. Il n’annonce pas de support multilingue ou de phonèmes IPA.
Les commandes de prosodie sont locales à la séquence ; les réglages utilisateur
restent indépendants et sont enregistrés par les mécanismes NVDA habituels.

### Arrêt, pause et fin de parole

L’arrêt invalide la génération courante et les travaux en attente, annule le
calcul C et appelle immédiatement `WavePlayer.stop`, sans prendre un verrou
conservé pendant `feed` ou le rendu. Un envoi déjà réservé peut entrer dans le
lecteur après ce premier arrêt : le worker repurge alors la sortie avant de
commencer un nouveau travail. Les anciens indices sont ignorés.

La pause conserve la position du lecteur et bloque le worker, sans produire
une file de fragments en avance. L’arrêt révoque la pause et permet une nouvelle
annonce. La terminaison réveille le worker, attend sa sortie et libère le lecteur
et le moteur. Une erreur pendant le chargement des réglages libère également
l’instance native pour permettre une nouvelle sélection du synthétiseur.

Les indices utilisent les callbacks de fin de blocs audio ; ils ne sont pas
annoncés à la fin du calcul. Les séquences contenant seulement des indices ou
des espaces sont aussi traitées. La notification de fin attend la lecture audio
et la vidange de la file du worker ; une ancienne notification de fin ne doit
pas interrompre un nouveau travail en attente.

## Construire

La construction reproductible depuis Linux utilise le nixpkgs verrouillé :

```sh
nix build path:.#nvda-addon --out-link build/nvda-result
```

Le paquet est dans `build/nvda-result/synthe3-ng-0.5.0.nvda-addon`.
Pour produire séparément les exécutables Windows :

```sh
nix build path:.#windows-x64 --out-link build/windows-result
python3 tools/build_nvda_addon.py --dll build/windows-result/bin/synthe3.dll
```

Cette dernière commande écrit `build/synthe3-ng-0.5.0.nvda-addon`.
Avec une chaîne MinGW configurée, `make windows` puis `make nvda-addon`
produisent le même type de paquet. Le paquet ne contient ni cache Python,
ni environnement de compilation, ni copie des sources ou de la documentation de
développement. Le manifeste et l’aide utilisateur renvoient vers le dépôt GitHub.

## Validation

```sh
nix develop --command make test
nix develop --command make sanitize
```

Le [bilan du 26 octobre 2026](benchmarks/nvda-2026-10-26.json) enregistre
50 tests Python réussis, dont 22 propres à NVDA, et les contrôles C/sanitizers.
La suite vérifie le protocole Linux et ses références PCM, l’API C, le worker
avec le vrai moteur, l’entrée du pilote avec les services NVDA substitués et
le paquet ZIP. Les scénarios comprennent un `feed` qui livre tard après arrêt,
les notifications différées, les marques adjacentes, le texte Unicode, le volume
silencieux, les réglages temporaires, les longs textes, les pauses et trente
cycles arrêt/reprise. Le banc C Windows teste également les callbacks de l’ABI 2
et les indices uint32, dont `UINT32_MAX`. Le schéma du manifeste a été vérifié
avec la classe `AddonManifest` de NVDA 2026.2 et ConfigObj.

Les 40 références historiques sont conservées, sans régénération :

```sh
python3 tools/check_api_pcm.py
# Avec un préfixe Wine initialisé, voir windows-api.md :
python3 tools/check_api_pcm.py --renderer build/windows-result/bin/render_api.exe \
  --runner wine --report /tmp/synthe3-nvda-pcm.json
```

Les essais simulés ne remplacent pas la validation de WASAPI dans une session
NVDA. Aucun chiffre de latence audio Windows n’est revendiqué. Voir aussi
[le contrat C](windows-api.md) et [la proposition initiale](../evolution-windows.md).

Les signatures et le comportement d’intégration ont été examinés dans les
sources officielles de NVDA 2026.2 :
[contrat des pilotes](https://github.com/nvaccess/nvda/blob/release-2026.2/source/synthDriverHandler.py),
[commandes de parole](https://github.com/nvaccess/nvda/blob/release-2026.2/source/speech/commands.py),
[lecteur nvwave](https://github.com/nvaccess/nvda/blob/release-2026.2/source/nvwave.py)
et [WASAPI](https://github.com/nvaccess/nvda/blob/release-2026.2/nvdaHelper/local/wasapi.cpp).
