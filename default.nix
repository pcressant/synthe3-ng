# SPDX-License-Identifier: GPL-2.0-or-later
{ pkgs ? import <nixpkgs> { } }:
pkgs.stdenv.mkDerivation {
  pname = "synthe3-ng";
  version = "0.3.0";
  src = pkgs.lib.cleanSourceWith {
    src = ./.;
    filter = path: type: !(builtins.elem (baseNameOf path) [ ".git" "build" "__pycache__" "result" ]);
  };
  nativeBuildInputs = [ pkgs.pkg-config ];
  buildInputs = [ pkgs.speechd pkgs.libxml2 ];
  nativeCheckInputs = [ pkgs.python3 pkgs.strace ];
  doCheck = true;
  checkPhase = "make test";
  makeFlags = [ "PREFIX=$(out)" ];
  meta = {
    description = "Moteur vocal français Michel en C pour Speech Dispatcher";
    license = [ pkgs.lib.licenses.gpl2Plus pkgs.lib.licenses.lgpl21Plus ];
    platforms = pkgs.lib.platforms.linux;
  };
}
