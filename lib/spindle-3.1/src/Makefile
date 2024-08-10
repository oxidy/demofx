# Spindle by lft, https://linusakesson.net/software/spindle/

CFLAGS+=-Wall -O3
LDFLAGS+=
CC=gcc
XA65=xa
WCC=i686-w64-mingw32-gcc -Wl,--stack,5000000 -Wall -O3 -idirafter compat

all:			mkheader mkpef pef2prg pefchain spin

.PHONY:			all win

# executables

mkpef:			mkpef.o util.o

pef2prg:		pef2prg.c common.h pef.h prgloader.h commonsetup.h
			$(CC) $(CFLAGS) -o $@ $<

pefchain:		pefchain.o pack.o disk.o util.o

spin:			spin.o pack.o disk.o util.o

# windows

win:			mkheader prgloader.h commonsetup.h datatables.h seektable.h
			$(WCC) -o mkpef.exe mkpef.c util.c compat/err.c
			$(WCC) -o pef2prg.exe pef2prg.c compat/err.c
			$(WCC) -o spin.exe spin.c pack.c disk.c util.c compat/err.c
			$(WCC) -o pefchain.exe pefchain.c pack.c disk.c util.c compat/err.c

# object files

mkpef.o:		mkpef.c common.h pef.h util.h
			$(CC) $(CFLAGS) -c -o $@ $<

pefchain.o:		pefchain.c common.h pef.h commonsetup.h pack.h disk.h seektable.h util.h patch-offsets.h
			$(CC) $(CFLAGS) -c -o $@ $<

pack.o:			pack.c disk.h pack.h patch-offsets.h
			$(CC) $(CFLAGS) -c -o $@ $<

disk.o:			disk.c disk.h datatables.h
			$(CC) $(CFLAGS) -c -o $@ $<

spin.o:			spin.c common.h disk.h pack.h util.h
			$(CC) $(CFLAGS) -c -o $@ $<

util.o:			util.c util.h
			$(CC) $(CFLAGS) -c -o $@ $<

# native code

%.prg:			%.s
			$(XA65) -o $@ $<

%.bin:			%.s
			$(XA65) -o $@ $<

stage1.prg:		stage1.s silence.bin
			$(XA65) -l stage1.lab -DGENERATE_ERRORS=0 -DRELOC=0 -o $@ $<

stage1reloc.prg:	stage1.s silence.bin
			$(XA65) -DGENERATE_ERRORS=0 -DRELOC=1 -o $@ $<

eflagwarning.bin:	eflagwarning.s
			$(XA65) -O PETSCREEN -o $@ $<

silence.bin:		silence.s
			$(XA65) -o $@ $<

drivecode.bin:		drivecode.s
			$(XA65) -l drivecode.lab -DGENERATE_ERRORS=0 -o $@ $<

drivecodeerr.bin:	drivecode.s
			$(XA65) -DGENERATE_ERRORS=1 -o $@ $<

seek.bin:		../template/seek.s
			$(XA65) -o $@ $<

commonsetup.bin:	../template/earlysetup.s
			$(XA65) -o $@ $<

# header wrangling

datatables.h:		stage1.prg stage1reloc.prg drivecode.bin drivecodeerr.bin eflagwarning.bin
			./mkheader >$@ $^

seektable.h:		seek.bin
			./mkheader >$@ $^

commonsetup.h:		commonsetup.bin
			./mkheader >$@ $^

prgloader.h:		prgloader.prg
			./mkheader >$@ $^
