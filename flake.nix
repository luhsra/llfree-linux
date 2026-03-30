{
  description = "LLZero Linux Environment";

  inputs = { nixpkgs.url = "github:nixos/nixpkgs/nixos-25.11"; };

  outputs = { nixpkgs, ... }:
    let
      supportedSystems =
        [ "aarch64-linux" "x86_64-linux" "aarch64-darwin" "x86_64-darwin" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in {
      devShells = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          default = pkgs.mkShell.override { stdenv = pkgs.clangStdenv; } {
            buildInputs = with pkgs; [
              lld
              lldb
              llvm
              gdb
              ncurses
              pkg-config
              qemu
              qemu_kvm
              qemu-utils
              guestfs-tools
            ];
            inputsFrom = [ pkgs.linux ];
            hardeningDisable = [ "all" ];
            env = { LLVM = 1; };
          };
        });
    };
}
