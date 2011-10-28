all: ld-shatner interpatch

obj.elf: obj.c
	gcc -fpie -c obj.c -o obj.o
	ld -pie obj.o -o $@ -e func

ld-shatner: hooked.s ld-shatner.c obj.elf
	gcc hooked.s ld-shatner.c -o $@

interpatch: interpatch.o

clean:
	rm -f ld-shatner interpatch ld-hook.so obj.elf *.o
