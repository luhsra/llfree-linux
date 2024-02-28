obj-m += automatic-llfree-balloon-test.o
automatic-llfree-balloon-test-y := automatic-llfree-balloon-tester-main.o automatic-llfree-balloon-test-lib.o llfree_ihpq.o

all:
	make CC=clang -C ../../.. M=$(PWD) modules
clean:
	make CC=clang -C ../../.. M=$(PWD) clean
