{
  description = "Synthé3-ng : moteur vocal français Michel pour Speech Dispatcher";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      each = nixpkgs.lib.genAttrs systems;
    in {
      packages = each (system: rec {
        synthe3-ng = import ./default.nix { pkgs = nixpkgs.legacyPackages.${system}; };
        default = synthe3-ng;
        windows-x64 = import ./windows.nix { pkgs = nixpkgs.legacyPackages.${system}; };
        nvda-addon = import ./nvda.nix { pkgs = nixpkgs.legacyPackages.${system}; };
      });
      checks = each (system: { package = self.packages.${system}.default; });
      nixosModules.default = import ./nix/module.nix;
      devShells = each (system:
        let pkgs = nixpkgs.legacyPackages.${system}; in {
          default = pkgs.mkShell {
            packages = with pkgs; [ gcc gnumake pkg-config speechd libxml2 python3 strace ];
          };
        });
    };
}
