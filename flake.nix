{
  description = "QWavRec - a small PipeWire audio player/recorder";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        lib = pkgs.lib;

        versionBase = lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION);
        gitRev = self.shortRev or self.dirtyShortRev or "dirty";
        isDev = lib.strings.hasInfix "-dev" versionBase;
        version =
          if isDev then
            "${versionBase}.${toString (self.revCount or 0)}+g${gitRev}"
          else
            versionBase;

        qwavrec = pkgs.stdenv.mkDerivation {
          pname = "qwavrec";
          inherit version;

          src = ./.;

          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
            qt6.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            qt6.qtbase
            qt6.qtmultimedia
            libpulseaudio
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DPROJECT_VERSION_FULL=${version}"
          ];

          meta = with lib; {
            description = "Simple PipeWire audio player and recorder";
            homepage = "https://github.com/grumbel/qwavrec";
            license = licenses.gpl3Plus;
            maintainers = [ ];
            platforms = platforms.linux;
          };
        };
      in
      {
        packages.default = qwavrec;

        # nix flake check
        # - qwavrec: full package build (compile + link)
        # - reuse: SPDX / REUSE.toml compliance
        checks = {
          qwavrec = qwavrec;
          reuse = pkgs.runCommand "qwavrec-reuse-lint" {
            nativeBuildInputs = [ pkgs.reuse ];
          } ''
            reuse --root ${self} lint
            touch "$out"
          '';
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ qwavrec ];
          packages = with pkgs; [
            cmake
            ninja
            qt6.qttools
            gdb
            reuse
          ];
        };
      });
}
