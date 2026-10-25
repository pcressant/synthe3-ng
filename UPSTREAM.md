# Provenance du moteur

Source : https://github.com/acceslibreinfo/synthe3
Révision : 8e601dc1895d71c0150c39ee1d10f2e04a291fe8
Auteur du moteur : Michel Morel, CRISCO, université de Caen.
Maintenance du portage libre : Shérab (Sébastien Hinderer).

`src/engine.c` est le portage C de SynTrans.cpp, SynParle.cpp, SynVoix.cpp et
SynCalcul.cpp. Les algorithmes et la mention de copyright historique sont
conservés sous LGPL-2.1-or-later. Le contrôle des threads, le matériel audio et
les sorties fichiers ont été retirés. Les lectures 16 bits sont explicites,
les variables de retour arrière sont initialisées et une amplitude totale
nulle produit une période nulle sans conversion de NaN en entier.

`src/voice_data.h` contient les octets inchangés de `upstream/Michel.seg`
(adresses puis segments, sans les quatre octets de dimensions) et
`upstream/Synthe.tab` (fichier complet). Les tests vérifient cette correspondance.
Les ressources et les licences originales sont conservées. Le projet historique
n’indique pas de licence distincte pour ses données vocales.

`src/dsp.c` porte les réglages actifs de l’ancien `synthe3_ng.py` sous
GPL-2.0-or-later. L’extension expérimentale de bande, désactivée avant ce portage,
a été supprimée. `src/module.c` utilise les en-têtes et la bibliothèque installés
par Speech Dispatcher ; aucune copie du protocole ni de sa bibliothèque n’est
embarquée. `src/compat/spd_audio.h` expose seulement les types audio publics pour
pallier l’inclusion d’un en-tête privé absent des installations 0.12.

L’[inventaire complet de la réécriture](docs/reecriture-c.md) documente aussi
les corrections, la validation du PCM et les résultats des benchmarks.

Ce dépôt autonome extrait uniquement Synthé3-ng du projet de travail : aucun moteur original C++, renderer Python ou historique Git privé n’est embarqué.
