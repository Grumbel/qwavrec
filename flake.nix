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
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "qwavrec";
          version = "0.1.0";

          src = ./.;

          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
            qt6.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            qt6.qtbase
            qt6.qtmultimedia
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ];

          meta = with pkgs.lib; {
            description = "Simple PipeWire audio player and recorder";
            homepage = "https://github.com/grumbel/qwavrec";
            license = licenses.gpl3Plus;
            maintainers = [ ];
            platforms = platforms.linux;
          };
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.default ];
          packages = with pkgs; [
            cmake
            ninja
            qt6.qttools
            gdb
          ];
        };
      });
}
