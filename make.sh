#!/bin/bash

if [  "$1" == "clean"  ]; then
	make O=build-llfree-vm clean
else
	CCACHE_DIR="/srv/scratch/hen.cohrs/.ccache" make O=build-llfree-vm LLVM=1 -j4 CC="ccache clang"
fi
