{
  description = "LLZero Linux Environment";

  inputs = { nixpkgs.url = "github:nixos/nixpkgs/nixos-26.05"; };

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
              just
            ];
            inputsFrom = [ pkgs.linux ];
            hardeningDisable = [ "all" ];
            env = {
              NIX_CC_WRAPPER_SUPPRESS_TARGET_WARNING = 1;
              NIX_CFLAGS_COMPILE="-Wno-unused-command-line-argument";
              LLVM = 1;
              MAKEFLAGS="-j";
            };
          };
        });
    };
}
