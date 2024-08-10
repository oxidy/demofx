CFLAGS=-Wall
SPINDLE=../src
MKPEF=$(SPINDLE)/mkpef
PEF2PRG=$(SPINDLE)/pef2prg

all:			part.pef part.prg

effect.efo:		effect.s
			xa -o $@ $<

part.pef:		effect.efo # Add more files here...
			$(MKPEF) -o $@ $^

%.prg:			%.pef
			$(PEF2PRG) -o $@ $^
