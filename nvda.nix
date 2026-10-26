# SPDX-License-Identifier: GPL-2.0-or-later
{ pkgs ? import <nixpkgs> { } }:
let windows = import ./windows.nix { inherit pkgs; };
in pkgs.stdenvNoCC.mkDerivation {
  pname = "synthe3-ng-nvda";
  version = "0.4.0";
  src = pkgs.lib.cleanSourceWith {
    src = ./.;
    filter = path: type: !(builtins.elem (baseNameOf path) [ ".git" "build" "__pycache__" "result" ]);
  };
  nativeBuildInputs = [ pkgs.python3 ];
  dontConfigure = true;
  buildPhase = ''
    runHook preBuild
    python3 tools/build_nvda_addon.py --dll ${windows}/bin/synthe3.dll
    runHook postBuild
  '';
  installPhase = ''
    mkdir -p $out
    cp build/*.nvda-addon $out/
  '';
}
