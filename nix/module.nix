# SPDX-License-Identifier: GPL-2.0-or-later
{ config, lib, pkgs, ... }:
let
  cfg = config.services.speechd.synthe3-ng;
  withVoice = package: package.overrideAttrs (old: {
    postInstall = (old.postInstall or "") + ''
      mkdir -p "$out/libexec/speech-dispatcher-modules" "$out/etc/speech-dispatcher/modules"
      ln -s ${cfg.package}/libexec/synthe3-ng/sd_synthe3_ng "$out/libexec/speech-dispatcher-modules/sd_synthe3_ng"
      cp ${../config/synthe3_ng.conf} "$out/etc/speech-dispatcher/modules/synthe3_ng.conf"
    '' + lib.optionalString cfg.makeDefault ''
      echo 'DefaultModule synthe3_ng' >> "$out/etc/speech-dispatcher/speechd.conf"
    '';
  });
in {
  options.services.speechd = {
    synthe3-ng = {
      enable = lib.mkEnableOption "Synthé3-ng";
      package = lib.mkOption {
        type = lib.types.package;
        default = import ../default.nix { inherit pkgs; };
        description = "Paquet Synthé3-ng.";
      };
      makeDefault = lib.mkOption { type = lib.types.bool; default = false; };
    };
    package = lib.mkOption { apply = package: if cfg.enable then withVoice package else package; };
  };
  config = lib.mkIf cfg.enable {
    services.speechd.enable = true;
    services.speechd.modules.synthe3_ng = builtins.readFile ../config/synthe3_ng.conf;
    environment.systemPackages = [ cfg.package ];
    assertions = [{
      assertion = !cfg.makeDefault || config.services.speechd.config == null;
      message = "Avec services.speechd.config explicite, utiliser DefaultModule synthe3_ng dans cette configuration et laisser synthe3-ng.makeDefault désactivé.";
    }];
  };
}
