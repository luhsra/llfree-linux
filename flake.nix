{
  description = "SRA flake for Linux kernel development";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/7c43f080a7f28b2774f3b3f43234ca11661bf334";
  };

  outputs = { self, nixpkgs, ... }: let
    supportedSystems = [ "aarch64-linux" "x86_64-linux"
                         "aarch64-darwin" "x86_64-darwin" ];
    forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
  in {
    devShells = forAllSystems (system: let pkgs = nixpkgs.legacyPackages.${system}; in {
      default = pkgs.mkShellNoCC {
        buildInputs = with pkgs; [
          llvmPackages_20.clang
          llvmPackages_20.lld
          llvmPackages_20.llvm
          llvmPackages_20.clang-tools
          ncurses
          pkg-config
          qemu qemu_kvm qemu-utils
          guestfs-tools
        ];
        inputsFrom = [ pkgs.linux ];
        hardeningDisable = [ "all" ];
        env = {
          LLVM = 1;
        };
      };
    });
  };
}
