# Synthé3-ng

Moteur de synthèse vocale française C11, voix Michel, pour Speech Dispatcher/Orca
et extension NVDA sous Windows x64.

Issu de [Synthé3 de Michel Morel, maintenu par Shérab (Sébastien Hinderer)](https://github.com/acceslibreinfo/synthe3). Les algorithmes et données historiques sont conservés ; voir [les crédits et la provenance](UPSTREAM.md) et [les licences](LICENSE.md).

## NVDA — Windows x64

La version 0.5.0 fournit une extension pour NVDA 2026.2. Ouvrir le fichier
`synthe3-ng-0.5.0.nvda-addon` sur Windows, installer, redémarrer NVDA puis choisir
**Synthé3-ng — Michel (français)** dans les paramètres Parole.
Voir [le guide NVDA](docs/nvda.md) pour la construction et les essais.
**Testé avec succès sur NVDA 26.3 (2026.3) sous Windows 11 26H2**, pendant
de longues sessions de travail sans problème. La réactivité est jugée très bonne ;
la latence acoustique n’a pas été mesurée.

```sh
nix build path:.#nvda-addon --out-link build/nvda-result
```

## Installation Linux

```sh
nix build
nix flake check
nix develop --command make test
# Hors Nix, avec les dépendances de développement Speech Dispatcher >= 0.12 et libxml2 :
make
sudo make install PREFIX=/usr/local
```

L’installation comprend le module, sa configuration, les crédits et les licences. Ajouter la ligne AddModule ci-dessous à speechd.conf, puis redémarrer Speech Dispatcher et sélectionner synthe3_ng dans Orca.

Pour NixOS et Arch Linux, voir [le guide de packaging](docs/packaging.md).

## Architecture et améliorations

La branche `nvda` prépare le partage du code entre Linux et une future extension
NVDA ; voir [la proposition Windows](evolution-windows.md). La première étape
isole la normalisation UTF-8 → Latin-1, les fragments de 240 octets et les marques
dans `src/text.c` et `src/text.h`. Cette couche utilise des callbacks de livraison
et d’annulation ; elle ne dépend ni de Speech Dispatcher, ni de libxml2, ni d’une
sortie audio. Le protocole, le SSML, les réglages Speech Dispatcher et la livraison
audio restent dans `src/module.c`. Le moteur et le DSP sont inchangés.

`make core` construit `build/libsynthe3.a` (moteur, DSP et préparation du texte)
avec un compilateur C11 et un archiveur, sans les dépendances Speech Dispatcher
et libxml2. `make test-core` vérifie directement l’interface texte, notamment les
marques après normalisation et aux frontières des fragments. Ces contrôles sont
aussi exécutés par `make test` et couverts par `make sanitize`.
La phase 2 ajoute une API C et une DLL Windows x64, avec un banc de rendu
hors NVDA : voir [la construction et le contrat de l’API](docs/windows-api.md).
La phase 3 fournit le pilote, la sortie audio NVDA et le paquet installable :
voir [l’intégration NVDA](docs/nvda.md).

Le [dossier de la réécriture C](docs/reecriture-c.md) regroupe l’inventaire des
changements, l’architecture, les benchmarks, les procédures de validation et la
migration. Les [mesures enregistrées](docs/benchmarks/2026-10-25.json) distinguent
les essais CPU, les notifications d’interruption et leurs limites.

`make` construit `build/sd_synthe3_ng` et `build/libsynthe3.a` avec un compilateur
C11, Make, pkg-config, Speech Dispatcher 0.12+ (`libspeechd_module`) et libxml2.
`make test` utilise Python uniquement pour piloter les tests. `make sanitize`
exécute 3 000 entrées aléatoires sous AddressSanitizer et UndefinedBehaviorSanitizer.
Si strace est disponible et ptrace autorisé, les tests vérifient aussi l’absence
de créations de processus, de threads et de fichiers ouverts en écriture.
`make install PREFIX=/usr/local` installe le module natif et les licences.

```conf
AddModule "synthe3_ng" "/usr/local/libexec/synthe3-ng/sd_synthe3_ng" "synthe3_ng.conf"
```

Le fichier de configuration peut être vide. Retirer les anciens `Renderer` et
`DataDirectory` : la voix Michel et les tables sont intégrées en lecture seule
au binaire, sans fichier de données à ouvrir à l’exécution.

Le module utilise la boucle synchrone et la sortie audio serveur de la
bibliothèque native `libspeechd_module`. Une seule instance du moteur appartient
à cette boucle : aucun fork, exec, thread de synthèse ou WAV temporaire. Le moteur
interroge les commandes avec un budget temporel de 2 ms pendant le calcul,
indépendamment du débit et de la hauteur. La phonétisation, chaque passe DSP,
la préparation du texte et l’analyse SSML incrémentale ont des points de contrôle.
Le texte brut évite l’analyse XML. Aucun timer ne tourne au repos : le processus
attend une commande dans une lecture bloquante, sans réveil périodique.

Les nœuds texte SSML partagent le même tampon de 240 caractères : les marqueurs
ajoutés par Orca avant chaque mot ne relancent plus la synthèse. Le PCM reste
identique au texte sans marqueurs. Le moteur relève la position des indices de
mots historiques ; le module convertit ces positions en 22 050 Hz et intercale
les notifications entre les paquets audio, sans ajouter de silence ni de fondu.
Le repérage suit les frontières de diphones : il reste approximatif pour les
marqueurs placés à l’intérieur d’un mot. STOP et PAUSE restent traités pendant
le calcul et l’émission.

STOP abandonne le calcul courant ; il prime sur une PAUSE simultanée. PAUSE
attend le prochain marqueur Orca `__spd_` ou la fin du message. Les paquets audio
comptent au plus 1 024 échantillons ; la bibliothèque native traite aussi les
commandes après chaque paquet. Speech Dispatcher vide sa file audio sur STOP
et ignore les échantillons encore en transit. QUIT pendant un calcul diffère la
libération des tampons jusqu’au retour du moteur et du DSP.

Le budget de 2 ms n’est pas une garantie de latence acoustique : l’ordonnanceur,
le serveur et le périphérique audio interviennent aussi. Si le serveur cesse de
lire sa pipe, l’écriture native attend sa reprise, sans boucle active.

Les tampons audio grandissent à la demande puis sont réutilisés. Le texte est
découpé selon les mêmes frontières de 240 caractères que l’ancien module. Un
fragment est calculé en mémoire avant émission : cela conserve la normalisation
par crête historique. Le module accepte des messages de 1 Mio au maximum ; le
moteur limite un fragment à quatre millions d’échantillons natifs. Aucun DSP
spéculatif n’est activé.

La phonétisation, les diphones, le filtre historique,
les trois égalisations, les fondus de 2 ms et le sinc Blackman à 48 coefficients
sont conservés. Les 441 phases du rééchantillonnage 10 000 → 22 050 Hz et les
coefficients des biquads sont calculés une seule fois. Les arrondis restent au
pair et les contractions flottantes sont désactivées pour préserver le PCM.
Les entiers des tables sont décodés explicitement en little-endian.

La hauteur Speech Dispatcher 0 correspond désormais au niveau moteur 5
(auparavant 6), retenu à l’écoute comme correspondant à la voix matérielle.
Le gain final fixe est de 0 dB (facteur 1), après la seconde normalisation
et avant le volume Speech Dispatcher. L’écrêtage 16 bits reste la limite finale :
le gain de 0 dB remplace l’essai à +5 dB après un retour de saturation
à l’écoute via Speech Dispatcher. Les réglages du client et de sortie audio
interviennent aussi dans le niveau final.

Les empreintes de `tests/voice_reference.json` proviennent de l’ancienne chaîne
C++ + Python, avant portage : huit textes × cinq couples débit/hauteur. Elles
vérifient l’identité du PCM avec un binaire de test compilé aux anciens réglages
(hauteur 6 et gain 0 dB), sans modifier ces références. Un test séparé vérifie
la nouvelle hauteur par défaut et le gain unitaire effectif. Les
autres tests couvrent le protocole natif, les accents, entités XML, marqueurs,
volume, débit, ponctuation, épellation, STOP, PAUSE et réutilisation du moteur.
Ils ajoutent 60 rafales arrêt/reprise, les arrêts pendant le texte/SSML et l’envoi
audio, les commandes STOP/PAUSE concurrentes, QUIT en cours de calcul et l’absence
de consommation CPU/réveils périodiques au repos. `make sanitize` interrompt aussi
le DSP à chacun de ses points de contrôle et vérifie sa réutilisation.

Test silencieux de bout en bout, avec un serveur Speech Dispatcher temporaire :

```sh
make build/check_speechd
python3 tools/demo.py --variant ng --check-interruptions
```

Le client natif effectue 30 arrêts/reprises, cinq changements de priorité et trois
pauses/reprises sur les marqueurs du serveur. Mesure locale : environ 11 ms au
maximum jusqu’à la notification d’annulation, moins de 1 ms pour le début du
message suivant et environ 7 ms lors d’une préemption par priorité. Le test direct
du module observe environ 2 ms sur 60 interruptions. Ces mesures incluent le
transport et l’ordonnancement, sans mesurer le signal à la sortie du haut-parleur.
La validation dans une session Orca réelle reste à faire après activation.

L’API `src/engine.h` produit le PCM natif à 10 kHz à partir de 240 octets Latin-1 ;
`src/dsp.h` applique les réglages ng et fournit le PCM à 22 050 Hz. Ces API sont
synchrones et ne sont pas réentrantes.

Mesure locale (x86-64, GCC 15, `-O2`, dix lectures du même texte de 82 caractères,
initialisation comprise, sortie audio collectée sans lecture matérielle) :
5,716 s CPU pour l’ancienne chaîne contre 0,056 s pour le module C. Ce résultat
porte sur ce corpus et cette machine, sans garantir le même facteur partout.

Après renforcement des interruptions, un essai de 200 lectures du même texte
mesure 0,92 s CPU contre 1,04 s pour le premier portage C sur cette machine.
L’essai ne relève donc pas de surcoût CPU lié aux nouveaux contrôles ; ce n’est
pas une mesure électrique de consommation.

## Modulation facultative

La modulation Orca est convertie en `pitch_range` Speech Dispatcher par
`int(20 × modulation − 100)`. De 0 à 5 inclus, le PCM conserve l’intonation
historique, sans passe supplémentaire. Au-dessus de 5, des courbes progressives
s’ajoutent : 6 correspond à l’exemple discret (intensité 1,5 demi-ton), 7 à
l’exemple marqué (3), puis la progression continue jusqu’à 10 (7,5). Ces valeurs
sont les intensités des courbes, pas une transposition constante de la voix.

Une première synthèse repère les frontières de mots et les ponctuations dans
chaque fragment ; une seconde applique les variations à la période des diphones.
Les questions remontent en fin de groupe, les affirmations descendent, les
virgules et points-virgules marquent une légère continuation. Les transitions
sont adoucies sur environ 20 ms. Les deux passes sont interruptibles ; cette
option ajoute du calcul, sans thread ni sous-processus. Les marqueurs SSML
restent calés sur le rendu final et ne découpent pas les courbes.

Ce modèle simple n’analyse pas les accents syllabiques. Les fragments de
240 octets restent une limite : une phrase qui les traverse n’a pas une courbe
globale, et une coupure sans ponctuation n’ajoute pas de chute finale. La hauteur
de base et le gain 0 dB restent indépendants. L’absence d’écrêtage sur les
exemples ne garantit pas tous les textes ou les réglages extrêmes.

```sh
nix develop --command python3 tools/demo.py --variant ng --orca-rate 77 --modulation 6
```

## Nombres après prétraitement Speech Dispatcher

Le prétraitement des symboles du serveur peut transformer `5,58` en `5 ,58`.
Le lecteur de nombres refuse désormais une ponctuation ou une lettre à la place
d’un chiffre après un espace de groupement. Cela évite les énoncés parasites
sur les décimaux prétraités, tout en conservant les milliers comme `123 456`.
Les tests couvrent les décimaux, la ponctuation, les marqueurs SSML et les
40 références vocales historiques inchangées.
