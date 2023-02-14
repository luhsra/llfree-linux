#make clean
#make CC="ccache clang" LLVM=15 defconfig
make CC="ccache clang" LLVM=15 -j `nproc`
cp arch/x86/boot/bzImage ~/vm/morsel/kernel
