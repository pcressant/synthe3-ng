# Construire et installer Synthé3-ng

## Nix et NixOS

`nix build` construit uniquement Synthé3-ng, avec ses tests. `nix flake check`
construit le même paquet sur la plateforme locale. `nix develop` fournit les
outils de compilation et de validation. Le verrou nixpkgs est conservé depuis
le projet de travail. Python sert aux tests et aux outils, pas au moteur installé.

Ajouter ce dépôt comme input de son flake NixOS, puis importer
`synthe3-ng.nixosModules.default` et activer :

```nix
services.speechd.synthe3-ng = {
  enable = true;
  makeDefault = true;
};
```

Avec `services.speechd.config` explicite, laisser `makeDefault = false` et ajouter :

```conf
AddModule "synthe3_ng" "sd_synthe3_ng" "synthe3_ng.conf"
DefaultModule synthe3_ng
```

Reconstruire NixOS, redémarrer Speech Dispatcher puis reconnecter Orca.

## Arch Linux

Depuis la racine des sources, lancer `makepkg -si`. Le PKGBUILD utilise les
sources locales et exécute les tests. Il installe le module sous
`/usr/libexec/synthe3-ng/`, la configuration sous `/etc/speech-dispatcher/modules/`,
les licences sous `/usr/share/licenses/synthe3-ng/` et la documentation sous
`/usr/share/doc/synthe3-ng/`. Ajouter à speechd.conf :

```conf
AddModule "synthe3_ng" "/usr/libexec/synthe3-ng/sd_synthe3_ng" "synthe3_ng.conf"
```

Choisir ensuite ce module dans Orca. Aucun script de paquet ne modifie le choix
de voix ni ne redémarre les services personnels.

Le PKGBUILD est prêt pour une construction locale. Avant une publication AUR,
remplacer ses sources locales par l'archive d'une version taguée de
<https://github.com/pcressant/synthe3-ng>, calculer la somme avec `updpkgsums`
et générer `.SRCINFO` avec `makepkg --printsrcinfo`.

## Publication GitHub

Les sources sont autonomes et ne contiennent ni l'ancien moteur C++/Python,
ni les sorties de compilation, ni l'historique Git du projet de travail.
Conserver les crédits, les licences et les ressources historiques. Le dossier
`docs/reecriture-c.md` décrit les changements et les limites ; les mesures
archivées sont historiques et ne prétendent pas mesurer toutes les machines.
