# API C et banc Windows — phase 2

Le moteur et le DSP disposent d’une API C versionnée, décrite dans
[`src/api.h`](../src/api.h). Les mêmes sources produisent une bibliothèque Linux
et une DLL Windows x64. Aucun pilote NVDA ni périphérique audio n’intervient dans
cette étape.

## Construire

Sur Linux, dans l’environnement du projet :

```sh
nix develop --command make api test-api
nix develop --command make test
nix develop --command make sanitize
```

Cela produit `build/libsynthe3.so`, `build/render_api` et `build/check_api`.
Le module Speech Dispatcher reste construit et testé par les cibles habituelles.

Pour la compilation croisée reproductible vers Windows x64 :

```sh
nix build path:.#windows-x64 --out-link build/windows-result
```

Le résultat contient `bin/synthe3.dll`, `bin/render_api.exe`, `bin/check_api.exe`,
l’en-tête, les licences et la provenance. Le compilateur est le MinGW du nixpkgs
verrouillé par le projet ; les bibliothèques de support sont liées statiquement.
La DLL construite lors des essais n’importe que `KERNEL32.dll` et `msvcrt.dll`.
Il ne faut pas confondre cette DLL x64 avec une bibliothèque pour NVDA 32 bits
ou ARM64 natif, architectures non construites ici.

Avec une chaîne MinGW déjà configurée et GNU Make 4.3 ou supérieur, sans Nix :

```sh
make windows WINDOWS_CC=x86_64-w64-mingw32-gcc
```

Cette commande produit les fichiers dans `build/windows-x64/`. Le support de C11
et des atomiques d’entiers sans verrou est requis. La compilation native par
MSVC n’a pas été mise en place.

## Contrat de l’API, version 1

| Opération | Responsabilité |
| --- | --- |
| `s3_api_version` | Retourner la version de l’ABI, actuellement 1 |
| `s3_api_open` / `s3_api_close` | Initialiser/libérer l’instance unique |
| `s3_api_request_create` | Allouer un jeton d’annulation pour un énoncé |
| `s3_api_request_cancel` | Invalider ce jeton depuis un autre thread |
| `s3_api_request_destroy` | Libérer le jeton après le retour de tous ses utilisateurs |
| `s3_api_render` | Calculer un fragment Latin-1 et son DSP, en mémoire |
| `s3_api_sample_at` | Convertir une position de texte en position audio approximative |

La convention d’appel est C (`__cdecl` sous Windows). Les longueurs et paramètres
exportés utilisent `uint32_t` et `int32_t`, sans dépendre de la taille de `long`.
L’API ne crée ni thread, ni fichier, ni sortie audio. Les fonctions internes du
moteur ne sont pas exportées dans la DLL.

L’instance unique accepte une opération moteur à la fois. Le futur worker NVDA
appellera `open`, `render`, `sample_at` et `close` en série. Une opération concurrente
reçoit `S3_BUSY` ; une utilisation sans moteur ouvert reçoit `S3_STATE`.
Ne pas combiner cette interface avec des appels directs au moteur/DSP dans une
même instance de bibliothèque.

Créer le jeton **avant de publier l’énoncé dans la file**. Il reste valable pour
ses différents fragments. L’annulation est définitive : un nouvel énoncé obtient
un nouveau jeton. Cela évite qu’une remise à zéro dans le worker perde une
annulation arrivée avant le début du calcul. Seule l’annulation peut être appelée
concurremment avec le rendu ; la libération du jeton attend le retour du worker
et la fin de tout appel d’annulation. Un échec d’allocation retourne un pointeur
nul à la création du jeton.

`render` reçoit au plus 240 octets Latin-1 avec longueur explicite ; aucun NUL
interne n’est accepté et aucun terminateur n’est requis après ces octets. Le
résultat est du PCM mono signé 16 bits à **22 050 Hz**. Le pointeur appartient à
la bibliothèque : copier ou consommer les données avant le prochain rendu ou
la fermeture. Une erreur ou une annulation remet les sorties à NULL et zéro.
Les positions des marques sont invalidées dès le rendu suivant, même en erreur.

Les unités restent celles du cœur : débit/hauteur 0 à 15, modulation −100 à +100,
volume −100 à +100. Le volume 0 est un gain unitaire, −100 le silence et +100 un
gain de 2. Le futur pilote devra donc convertir son volume NVDA ; il ne peut pas
passer directement sa valeur 0 à 100. La hauteur usuelle actuelle est 5 ; les
références historiques utilisent 6.

`sample_at` s’appuie sur les frontières historiques des mots/diphones, converties
10 000 → 22 050 Hz, et borne la position à la longueur du PCM. Il ne s’agit pas
d’un alignement phonétique précis à l’intérieur des mots. Les notifications NVDA
devront être déclenchées lors du passage audio et invalidées après annulation.
L’API peut seulement interrompre le calcul : elle ne purge aucune sortie audio.
Une annulation arrivant après le dernier contrôle doit aussi être prise en compte
par l’adaptateur avant la livraison du résultat.

La préparation UTF-8 et les marques abstraites de `src/text.c` restent communes.
Le banc les utilise avant d’appeler l’API par fragment. Elles ne sont pas encore
exposées par une ABI de haut niveau à Python : cette interface sera à intégrer
avec les commandes structurées lors de la phase 3, sans dupliquer les règles
Unicode ou le découpage dans le pilote.

## Rendre et comparer

Le banc lit un fichier UTF-8, utilise la préparation commune et écrit du PCM
little-endian brut, sans en-tête WAV. Il affiche le nombre d’échantillons et la
valeur de `clock()` pendant le rendu : cette valeur n’est pas une mesure de
latence audio, et sa signification temporelle dépend de la plateforme.

```sh
./build/render_api texte.txt sortie.pcm 4 5 0 0
python3 tools/check_api_pcm.py --report /tmp/synthe3-api-linux.json
```

Sous Windows, conserver les exécutables à côté de la DLL :

```powershell
.\check_api.exe
.\render_api.exe texte.txt sortie.pcm 4 5 0 0
python tools/check_api_pcm.py --renderer C:\chemin\render_api.exe --report windows.json
```

La dernière commande suppose le dépôt disponible pour retrouver les références.
Le script compare les 40 empreintes historiques sans les réécrire, avec la
conversion des réglages de ces références. Il retourne un échec si un seul cas
diffère. `make test` effectue également cette vérification avec la bibliothèque
Linux, en plus des tests du module Speech Dispatcher.

Pour un essai sous Wine depuis Linux, initialiser un préfixe temporaire dédié et
utiliser le même environnement pour les deux commandes :

```sh
mkdir -p /tmp/synthe3-wine-prefix
export WINEPREFIX=/tmp/synthe3-wine-prefix
export WINEDLLOVERRIDES='winemenubuilder.exe=d;mscoree=d;mshtml=d'
export WINEDEBUG=-all
wine build/windows-result/bin/check_api.exe
python3 tools/check_api_pcm.py \
  --renderer build/windows-result/bin/render_api.exe --runner wine \
  --report /tmp/synthe3-api-windows.json
```

## Validation et limites

Bilan enregistré dans [windows-api-2026-10-26.json](benchmarks/windows-api-2026-10-26.json) :
28 tests Python Linux, tests C de l’API et sanitizers réussis ; 40 références
PCM identiques sous Linux et avec la DLL Windows exécutée sous Wine 11.0.
La construction des paquets Nix Linux et Windows a également réussi.

Le test C couvre le cycle de vie, les paramètres invalides, les entrées sans
terminateur, le volume silencieux, l’annulation avant calcul et depuis un autre
thread, puis la réutilisation et la réouverture. Les mêmes tests sont compilés
pour Linux et Windows ; la variante Linux passe aussi sous ASan/UBSan.

La validation sous Wine porte sur l’ABI et le calcul. Elle ne valide pas les
pilotes audio Windows, WASAPI, le confort dans NVDA, les changements de
périphérique, ni la latence au haut-parleur. Le pilote de synthèse NVDA et les
essais sur Windows réel constituent la suite du travail.
