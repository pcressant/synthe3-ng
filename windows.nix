# SPDX-License-Identifier: GPL-2.0-or-later
# Cross-build on Linux. The result is a DLL and offline Windows test programs.
{ pkgs ? import <nixpkgs> { } }:
let cross = pkgs.pkgsCross.mingwW64;
in cross.stdenv.mkDerivation {
  pname = "synthe3-ng-windows-x64";
  version = "0.3.0";
  src = pkgs.lib.cleanSourceWith {
    src = ./.;
    filter = path: type: !(builtins.elem (baseNameOf path) [ ".git" "build" "__pycache__" "result" ]);
  };
  buildInputs = [ cross.windows.mcfgthreads ];
  dontConfigure = true;
  dontStrip = true;
  buildPhase = ''
    runHook preBuild
    make windows WINDOWS_CC=${cross.stdenv.cc.targetPrefix}cc
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin $out/include $out/share/licenses/synthe3-ng $out/share/doc/synthe3-ng
    cp build/windows-x64/*.dll build/windows-x64/*.exe $out/bin/
    cp src/api.h $out/include/
    cp LICENSE.md upstream/LICENSE-* $out/share/licenses/synthe3-ng/
    cp UPSTREAM.md $out/share/doc/synthe3-ng/
    cp docs/windows-api.md $out/share/doc/synthe3-ng/
    runHook postInstall
  '';
}
