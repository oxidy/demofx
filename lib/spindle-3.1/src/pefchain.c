/* Spindle by lft, https://linusakesson.net/software/spindle/
 */

#include <err.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "pef.h"
#include "commonsetup.h"
#include "disk.h"
#include "pack.h"
#include "seektable.h"
#include "util.h"
#include "patch-offsets.h"

#define N_RESERVED_ZP	5

#define MAXEFFECTS 96

#define MAXACTIONS (MAXEFFECTS * 12)

int verbose;
int squeeze_option;

struct load {
	struct load	*next;
	struct chunk	*chunk;
	uint8_t		pages[256];
};

enum {
	COND_DROP,
	COND_SPACE,
	COND_STAY,
	COND_AT
};

#define PCEF_BLANK		0x01
#define PCEF_SCRIPTBLANK	0x02
#define PCEF_EXTEND		0x04
#define PCEF_HAS_LABEL		0x08

struct effect {
	struct header	header;
	struct chunk	chunk[MAXCHUNKS + 1];
	uint16_t	condaddr;
	uint8_t		condval;
	uint8_t		flags;		// PCEF_*
	char		filename[32];
	uint16_t	play_address;
	uint8_t		needs_loading[256];
	int		blocks_loaded;
	struct load	*loads;
	int		loading_preferable;
	struct chunk	*driver_chunk;
	int16_t		driver_frontdoor;
	int16_t		driver_sidedoor;
	int		driver_loadcall;
	int		driver_nextjmp;
} effect[MAXEFFECTS];
int neffect;

uint8_t seeklabel[64];

static void load_pef(struct effect *e, char *filename) {
	FILE *f;
	int i;
	char buf[32], *ptr;
	struct chunk *c;

	f = fopen(filename, "rb");
	if(!f) err(1, "load_pef: fopen: %s", filename);

	fread(&e->header, sizeof(e->header), 1, f);
	if(strncmp((char *) e->header.magic, "PEF3", 4)) {
		errx(1, "Invalid pef header: %s", filename);
	}

	while((ptr = strchr(filename, '/')) || (ptr = strchr(filename, '\\'))) {
		filename = ptr + 1;
	}
	snprintf(e->filename, sizeof(e->filename), "%s", filename);
	e->filename[sizeof(e->filename) - 1] = 0;
	if((ptr = strrchr(e->filename, '.'))) {
		*ptr = 0;
	}

	for(i = 0; i < e->header.nchunk; i++) {
		c = &e->chunk[i];
		c->size = fgetc(f);
		c->size |= fgetc(f) << 8;
		c->data = malloc(c->size);
		c->loadaddr = fgetc(f);
		c->loadaddr |= fgetc(f) << 8;
		fread(buf, 32, 1, f);
		snprintf(c->name, sizeof(c->name), "%s:%s", e->filename, buf);
		c->name[sizeof(c->name) - 1] = 0;
		fread(c->data, c->size, 1, f);
	}

	fclose(f);
}

static int find_free_page(int eid, int residentpage, int bufferpage, int looplabel) {
	static int page = 0;
	int count = 256, i;
	int eid_first = eid, eid_last = eid;

	while(effect[eid].flags & PCEF_EXTEND) {
		eid_first--;
	}
	while(eid_last + 1 < neffect && effect[eid_last + 1].flags & PCEF_EXTEND) {
		eid_last++;
	}
	if(eid_first) eid_first--;
	if(eid_last + 1 < neffect) eid_last++;

	while(--count) {
		page = (page + 1) & 0xff;
		if(page < 2 || page == residentpage || page == bufferpage) {
			continue;
		}
		if((page & 0xf0) == 0xd0) {
			continue;
		}
		for(i = eid_first; i <= eid_last; i++) {
			if(effect[i].header.pageflags[page] & (PF_LOADED | PF_USED | PF_INHERIT)) {
				break;
			}
		}
		if(i <= eid_last) {
			continue;
		}
		if(looplabel >= 0) {
			if(effect[seeklabel[looplabel]].header.pageflags[page] &
				(PF_LOADED | PF_USED | PF_INHERIT))
			{
				continue;
			}
		}
		return page;
	}

	return -1;
}

static void create_init_effect(struct effect *e) {
	int i;

	e->header.flags = EF_SAFE_IO;
	for(i = 0x04; i <= 0x07; i++) {
		e->header.pageflags[i] = PF_INHERIT;
	}
	snprintf(e->filename, sizeof(e->filename), "(loader)");
}

static void create_blank_effect(int eid, int residentpage, int bufferpage, int looplabel) {
	int page, i;
	struct effect *e = &effect[eid];
	uint8_t *driver;

	e->flags |= PCEF_BLANK;
	e->header.flags |= EF_SAFE_IO | EF_AVOID_LOAD;
	snprintf(e->filename, sizeof(e->filename), "(blank)");

	page = find_free_page(eid, residentpage, bufferpage, looplabel);
	if(page < 0) {
		fprintf(stderr, "Warning: Couldn't find a free page for driving a blank effect.\n");
		fprintf(stderr, "Will use the lower stack area ($100) as a last resort.\n");
		page = 1;
	}

	e->header.nchunk = 1;
	e->chunk[0].loadaddr = page << 8;
	driver = malloc(128);
	i = 0;
	e->header.efo.v_irq[0] = i;
	e->header.efo.v_irq[1] = page;
	driver[i++] = 0xc6;	// dec zp
	driver[i++] = 0x00;
	driver[i++] = 0x48;	// pha
	driver[i++] = 0x8a;	// txa
	driver[i++] = 0x48;	// pha
	driver[i++] = 0x98;	// tya
	driver[i++] = 0x48;	// pha
	e->header.efo.v_jsr[0] = i;
	e->header.efo.v_jsr[1] = page;
	driver[i++] = 0x2c;	// bit abs
	driver[i++] = 0x00;
	driver[i++] = 0x00;
	driver[i++] = 0x68;	// pla
	driver[i++] = 0xa8;	// tay
	driver[i++] = 0x68;	// pla
	driver[i++] = 0xaa;	// tax
	driver[i++] = 0x68;	// pla
	driver[i++] = 0x4e;	// lsr abs
	driver[i++] = 0x19;
	driver[i++] = 0xd0;
	driver[i++] = 0xe6;	// inc zp
	driver[i++] = 0x00;
	driver[i++] = 0x40;	// rti
	e->header.efo.v_setup[0] = i;
	e->header.efo.v_setup[1] = page;
	driver[i++] = 0xa9;	// lda imm
	driver[i++] = 0x00;
	driver[i++] = 0x8d;	// sta abs
	driver[i++] = 0x11;
	driver[i++] = 0xd0;
	driver[i++] = 0x8d;	// sta abs
	driver[i++] = 0x20;
	driver[i++] = 0xd0;
	driver[i++] = 0x8d;	// sta abs
	driver[i++] = 0x15;
	driver[i++] = 0xd0;
	driver[i++] = 0xa9;	// lda imm
	driver[i++] = 0xf0;
	driver[i++] = 0x8d;	// sta abs
	driver[i++] = 0x12;
	driver[i++] = 0xd0;
	driver[i++] = 0x60;	// rts
	e->chunk[0].data = driver;
	e->chunk[0].size = i;
	snprintf(
		e->chunk[0].name,
		sizeof(e->chunk[0].name),
		"(blank)");
	e->header.pageflags[page] |= PF_LOADED | PF_USED;
}

static void dump_range(uint8_t a, uint8_t b, int *first) {
	if(*first) {
		*first = 0;
	} else {
		fprintf(stderr, ",");
	}
	if(a == b) {
		fprintf(stderr, "%02x", a);
	} else {
		fprintf(stderr, "%02x-%02x", a, b);
	}
}

static void dump_ranges(uint8_t *table) {
	int i, start = -1, first = 1;

	for(i = 0; i < 256; i++) {
		if(table[i]) {
			if(start < 0) start = i;
		} else {
			if(start >= 0) {
				dump_range(start, i - 1, &first);
				start = -1;
			}
		}
	}
	if(start >= 0) dump_range(start, 255, &first);
}

static void add_filler_before(int i, int residentpage, int bufferpage, int looplabel) {
	int j, p;

	if(neffect == MAXEFFECTS) errx(1, "Increase MAXEFFECTS!");
	memmove(&effect[i + 1], &effect[i], sizeof(struct effect) * (neffect - i));
	neffect++;
	memset(&effect[i], 0, sizeof(struct effect));
	effect[i].play_address = effect[i + 1].play_address;
	if(effect[i + 1].flags & PCEF_HAS_LABEL) {
		effect[i + 1].flags &= ~PCEF_HAS_LABEL;
		effect[i].flags |= PCEF_HAS_LABEL;
	}
	create_blank_effect(i, residentpage, bufferpage, looplabel);
	for(p = 0; p < 256; p++) {
		if(effect[i + 1].header.pageflags[p] & PF_INHERIT) {
			effect[i].header.pageflags[p] |= PF_INHERIT;
		}
	}
	for(j = 0; j < 64; j++) {
		if(seeklabel[j] > i) {
			seeklabel[j]++;
		}
	}
}

static void suggest_filler(struct effect *e1, struct effect *e2, int residentpage, int bufferpage, int zpstart) {
	uint8_t page[256], zp[256];
	int p;

	for(p = 0; p < 256; p++) {
		page[p] = !((e1->header.pageflags[p] | e2->header.pageflags[p]) & PF_USED);
		zp[p] = !((e1->header.pageflags[p] | e2->header.pageflags[p]) & PF_ZPUSED);
	}

	page[0] = 0;
	page[1] = 0;
	page[residentpage] = 0;
	page[bufferpage] = 0;
	zp[0] = 0;
	zp[1] = 0;
	for(p = 0; p < N_RESERVED_ZP; p++) {
		zp[zpstart + p] = 0;
	}

	fprintf(
		stderr,
		"Suggestion: Move things around or "
		"insert a part that only touches pages ");
	dump_ranges(page);
	fprintf(stderr, " and zero-page locations ");
	dump_ranges(zp);
	fprintf(stderr, ".\n");
}

static struct chunk *add_file_chunk(struct effect *e, char *fname) {
	struct stat sb;
	struct chunk *c;
	FILE *f;
	int filesize, p;

	if(e->header.nchunk >= MAXCHUNKS + 1) {
		errx(1, "Increase MAXCHUNKS.");
	}
	c = &e->chunk[e->header.nchunk++];

	f = fopen(fname, "rb");
	if(!f) err(1, "fopen: %s", fname);
	if(fstat(fileno(f), &sb)) err(1, "fstat: %s", fname);
	filesize = (int) sb.st_size;
	c->loadaddr = fgetc(f);
	c->loadaddr |= fgetc(f) << 8;
	if(c->loadaddr + filesize - 2 > 0x10000) {
		errx(1, "File too big: %s", fname);
	}
	c->size = filesize - 2;
	c->data = malloc(c->size);
	fread(c->data, c->size, 1, f);
	snprintf(c->name, sizeof(c->name), "%s", fname);
	c->name[sizeof(c->name) - 1] = 0;
	fclose(f);

	for(p = c->loadaddr >> 8; p <= (c->loadaddr + c->size - 1) >> 8; p++) {
		if(e->header.pageflags[p] & PF_USED) {
			errx(1, "Streaming music data collides with '%s' at page %02x.", e->filename, p);
		}
		e->header.pageflags[p] |= PF_LOADED | PF_USED;
		e->needs_loading[p] = 1;
	}

	return c;
}

static void load_script(char *filename, int residentpage, int bufferpage, int zpstart, uint8_t *earlysetup, int earlysize, int looplabel, int interlock) {
	FILE *f;
	char buf[256], *ptr, *name, *cond, *extrafname;
	int i, j, p, offset, p1, p2, slot;
	uint16_t play_address = 0;
	struct effect *e;
	struct chunk *c, *extrachunk = 0;

	create_init_effect(&effect[neffect]);
	neffect++;

	f = fopen(filename, "r");
	if(!f) err(1, "load_script: fopen: %s", filename);

	while(fgets(buf, sizeof(buf), f)) {
		while(*buf && strchr("\n\r\t ", buf[strlen(buf) - 1])) {
			buf[strlen(buf) - 1] = 0;
		}
		if(*buf && *buf != '#') {
			ptr = buf;
			if(*ptr == '"') {
				name = ++ptr;
				while(*ptr && *ptr != '"') ptr++;
			} else {
				name = ptr;
				while(*ptr && *ptr != ' ' && *ptr != '\t') {
					ptr++;
				}
			}
			if(*ptr) *ptr++ = 0;
			while(*ptr == ' ' || *ptr == '\t') ptr++;
			cond = ptr;
			if(!*name) errx(1,
				"Unexpected whitespace at beginning of "
				"script line '%s'.",
				buf);
			if(name[strlen(name) - 1] == ':' && !*cond) {
				slot = parseparam(name, &ptr);
				if(strcmp(ptr, ":") || slot < 0 || slot > 0x3f) {
					errx(1, "Invalid seek label (range 00-3f).");
				}
				if(seeklabel[slot]) {
					errx(1, "Seek label %02x appears more than once.", slot);
				}
				if(neffect >= MAXEFFECTS) {
					errx(1, "Increase MAXEFFECTS!");
				}
				seeklabel[slot] = neffect;
				effect[neffect].flags |= PCEF_HAS_LABEL;
			} else {
				extrafname = 0;
				j = 0;
				for(i = 0; cond[i]; i++) {
					if(cond[i] == ':'
					&& cond[i + 1] != '\\') {
						j = i + 1;
					}
				}
				if(j) {
					i = strlen(cond + j);
					if(i >= 4
					&& (cond[j + i - 1] == 'g' || cond[j + i - 1] == 'G')
					&& (cond[j + i - 2] == 'r' || cond[j + i - 2] == 'R')
					&& (cond[j + i - 3] == 'p' || cond[j + i - 3] == 'P')
					&& (cond[j + i - 4] == '.')) {
						extrafname = cond + j;
						do {
							cond[--j] = 0;
						} while(j && (cond[j - 1] == ' ' || cond[j - 1] == '\t'));
					}
				}

				if(!*cond) errx(1, "Expected a condition on script line '%s'.", buf);
				if(neffect == MAXEFFECTS) {
					errx(1, "Increase MAXEFFECTS!");
				}
				e = &effect[neffect++];
				e->play_address = play_address;
				if(!strcmp(buf, "-")) {
					// Create the blank effect later, when we
					// know what free pages are available.
					e->flags |= PCEF_SCRIPTBLANK;
				} else if(!strcmp(buf, "|")) {
					if(neffect < 3) {
						errx(1, "Can't extend (\"|\") at beginning of script.");
					} else {
						e->flags |= PCEF_EXTEND;
						e->header.flags = effect[neffect - 2].header.flags;
						e->header.efo = effect[neffect - 2].header.efo;
						e->header.efo.v_jsr[0] = 0;
						e->header.efo.v_jsr[1] = 0;
						for(i = 0; i < 256; i++) {
							if(effect[neffect - 2].header.pageflags[i] & (PF_LOADED | PF_USED | PF_INHERIT)) {
								e->header.pageflags[i] |= PF_INHERIT;
							}
							if(effect[neffect - 2].header.pageflags[i] & PF_ZPUSED) {
								e->header.pageflags[i] |= PF_ZPUSED;
							}
						}
						ptr = effect[neffect - 2].filename;
						if(*ptr == '|') ptr++;
						snprintf(e->filename, sizeof(e->filename), "|%s", ptr);
					}
				} else {
					load_pef(e, buf);
					if(e->header.installs_music[0]
					|| e->header.installs_music[1]) {
						play_address =
							e->header.installs_music[0] |
							(e->header.installs_music[1] << 8);
					} else if(e->header.flags & EF_UNMUSIC) {
						play_address = 0;
					}
					for(i = 0; i < 256; i++) {
						if(e->header.pageflags[i] & (PF_LOADED | PF_USED)
						&& i == residentpage) {
							errx(1,
								"Effect %s uses reserved page "
								"$%02x. Use -r to move Spindle.",
								e->filename,
								i);
						} else if(i == bufferpage) {
							if(e->header.pageflags[i] & PF_LOADED) {
								errx(1,
									"Effect %s wants to "
									"load to page $%02x "
									"which is reserved "
									"during loading. "
									"Change with -b.",
									e->filename,
									i);
							}
							if(e->header.pageflags[i] & PF_USED) {
								if(!(e->header.flags & EF_DONT_LOAD)) {
									warnx(
										"Loading is disabled "
										"during effect %s "
										"because it uses page "
										"$%02x. Change with -b "
										"or use the X tag to "
										"hide this warning.",
										e->filename,
										i);
									e->header.flags |= EF_DONT_LOAD;
								}
							}
						}
						if((e->header.pageflags[i] & PF_ZPUSED)
						&& i >= zpstart
						&& i < zpstart + N_RESERVED_ZP) {
							if(!(e->header.flags & EF_DONT_LOAD)) {
								warnx(
									"Loading is disabled "
									"during effect %s "
									"because it uses "
									"zero-page address $%02x. "
									"Change with -z or "
									"use the X tag to hide "
									"this warning.",
									e->filename,
									i);
								e->header.flags |= EF_DONT_LOAD;
							}
						}
						e->needs_loading[i] = !!(e->header.pageflags[i] & PF_LOADED);
					}
				}

				if(e->header.installs_music[0]
				|| e->header.installs_music[1]
				|| (e->header.flags & EF_UNMUSIC)) {
					extrachunk = 0;
				}

				if(extrachunk) {
					for(p = extrachunk->loadaddr >> 8; p <= (extrachunk->loadaddr + extrachunk->size - 1) >> 8; p++) {
						e->header.pageflags[p] |= PF_INHERIT;
					}
				}

				if(extrafname) {
					extrachunk = add_file_chunk(e, extrafname);
				}

				if(!strcmp(cond, "space")) {
					e->condaddr = 0;
					e->condval = COND_SPACE;
				} else if(!strcmp(cond, "stay")) {
					e->condaddr = 0;
					e->condval = COND_STAY;
				} else if(!strcmp(cond, "-")) {
					e->condaddr = 0;
					e->condval = COND_DROP;
				} else if(cond[0] == '@') {
					e->condaddr = 0;
					e->condval = COND_AT;
					if(interlock < 0) {
						errx(1, "No interlock address defined! Use the -@ option.");
					}
				} else {
					p1 = parseparam(cond, &cond);
					while(*cond == ' ' || *cond == '\t') cond++;
					if(*cond == '=') {
						p2 = parseparam(cond + 1, &cond);
						if(!*cond) {
							e->condaddr = p1;
							e->condval = p2;
						} else {
							errx(1, "Invalid condition: '%s'", cond);
						}
					} else {
						errx(1, "Invalid condition: '%s'", cond);
					}
				}
			}
		}
	}

	if(neffect < 2) errx(1, "No effects in script!");

	fclose(f);

	if(looplabel >= 0 && !seeklabel[looplabel]) {
		errx(1, "Loop label %02x isn't defined anywhere.", looplabel);
	}

	for(i = 1; i < neffect; i++) {
		e = &effect[i];
		if(e->flags & PCEF_SCRIPTBLANK) {
			create_blank_effect(i, residentpage, bufferpage, looplabel);
			effect[i].header.flags &= ~EF_AVOID_LOAD;
		}
	}

	if(effect[neffect].flags & PCEF_HAS_LABEL) {
		fprintf(stderr, "Warning: Appending blank filler for final label.\n");
		add_filler_before(neffect, residentpage, bufferpage, looplabel);
		// neffect has just increased
		effect[neffect - 1].flags |= PCEF_HAS_LABEL;
		effect[neffect - 1].play_address = play_address;
	}

	for(i = 1; i < neffect; i++) {
		e = &effect[i];
		if(e->header.n_stream_chunk) {
			for(j = 0; j < e->header.n_stream_chunk; j++) {
				offset = e->header.nchunk - e->header.n_stream_chunk + j;
				if(i + j >= neffect) {
					fprintf(stderr,
						"Warning: Ignoring surplus streamed data "
						"from effect '%s' at end of script.\n",
						e->filename);
					break;
				}
				if(j) {
					if(effect[i + j].header.nchunk >= MAXCHUNKS + 1) {
						errx(1, "Increase MAXCHUNKS.");
					}
					c = &effect[i + j].chunk[effect[i + j].header.nchunk++];
					*c = e->chunk[offset];
					memset(&e->chunk[offset], 0, sizeof(e->chunk[offset]));
				} else {
					c = &e->chunk[offset];
				}
				for(p = c->loadaddr >> 8; p <= (c->loadaddr + c->size - 1) >> 8; p++) {
					if(effect[i + j].header.pageflags[p] & (PF_LOADED | PF_USED | PF_INHERIT)) {
						errx(1, "Streamed data '%s' interferes with effect '%s' at page $%02x.",
							c->name,
							effect[i + j].filename,
							p);
					}
					effect[i + j].header.pageflags[p] |= PF_LOADED | PF_USED;
					effect[i + j].needs_loading[p] = 1;
				}
			}
			e->header.nchunk -= e->header.n_stream_chunk - 1;
		}
	}
}

static void patch_effects() {
	int i, offs, p;
	uint16_t jsr;
	uint8_t musicpage[256];
	struct effect *e;

	memset(musicpage, 0, sizeof(musicpage));

	for(i = 0; i < neffect; i++) {
		e = &effect[i];
		for(p = 0; p < 256; p++) {
			if(musicpage[p]) {
				if(e->header.pageflags[p] & (PF_LOADED | PF_USED)) {
					errx(
						1,
						"Effect %s is using page $%02x "
						"which is already occupied by "
						"the music player.",
						e->filename,
						p);
				}
				e->header.pageflags[p] |= PF_INHERIT;
			}
			if(e->header.installs_music[0]
			|| e->header.installs_music[1]) {
				musicpage[p] = !!(e->header.pageflags[p] & PF_MUSIC);
			} else if(e->header.flags & EF_UNMUSIC) {
				musicpage[p] = 0;
			}
		}

		jsr = e->header.efo.v_jsr[0] | (e->header.efo.v_jsr[1] << 8);
		e->header.efo.v_jsr[0] = 0;
		e->header.efo.v_jsr[1] = 0;
		while(jsr
		&& jsr >= e->chunk[0].loadaddr
		&& jsr < e->chunk[0].loadaddr + e->chunk[0].size) {
			offs = jsr - e->chunk[0].loadaddr;
			if(e->play_address) {
				jsr = e->chunk[0].data[offs + 1] | (e->chunk[0].data[offs + 2] << 8);
				e->chunk[0].data[offs + 0] = 0x20;	// jsr
				e->chunk[0].data[offs + 1] = e->play_address & 255;
				e->chunk[0].data[offs + 2] = e->play_address >> 8;
			} else {
				jsr = 0;
				e->chunk[0].data[offs + 0] = 0x2c;	// bit abs
			}
		}
	}
}

static void insert_fillers(int residentpage, int bufferpage, int zpstart, int looplabel) {
	int i, p = 0, need_filler, need_filler_io, pagecoll, zpcoll, nexteid;
	uint8_t pages[256], zp[256];
	uint8_t okpage[256];

	memset(okpage, 0, sizeof(okpage));
	for(i = 0; i < neffect; i++) {
		need_filler = 0;
		need_filler_io = 0;
		for(p = 0; p < 256; p++) {
			if(effect[i].header.pageflags[p] & PF_LOADED) {
				if(!okpage[p]) {
					need_filler = 1;
					if((p & 0xf0) == 0xd0) {
						need_filler_io = 1;
					}
				}
			}
			if(effect[i].header.pageflags[p] & PF_USED) {
				okpage[p] = 0;
			} else if(!(effect[i].header.flags & EF_DONT_LOAD)) {
				if((p & 0xf0) != 0xd0 || (effect[i].header.flags & EF_SAFE_IO)) {
					okpage[p] = 1;
				}
			}
		}
		if(i + 1 < neffect && (effect[i + 1].flags & PCEF_HAS_LABEL)) {
			memset(okpage, 0, sizeof(okpage));
		}
		if(need_filler) {
			fprintf(stderr, "Warning: Inserting blank filler before ");
			fprintf(stderr, "'%s'", effect[i].filename);
			if(need_filler_io) {
				fprintf(stderr, " to be able to load under I/O.\n");
			} else {
				fprintf(stderr, " to be able to load.\n");
			}
			add_filler_before(i, residentpage, bufferpage, looplabel);
			i++;
			for(p = 0; p < 256; p++) {
				okpage[p] = !(effect[i].header.pageflags[p] & PF_USED);
			}
		}
		if(i < neffect - 1) {
			nexteid = i + 1;
		} else if(looplabel >= 0) {
			nexteid = seeklabel[looplabel];
		} else {
			nexteid = -1;
		}
		if(nexteid >= 0) {
			if((effect[nexteid].header.flags & EF_UNSAFE)
			&& !(effect[i].header.flags & EF_SAFE_IO)) {
				fprintf(stderr,
					"Warning: %s blank filler "
					"because '%s' is declared unsafe and "
					"'%s' is not declared safe.\n",
					i == neffect - 1? "Appending" : "Inserting",
					effect[nexteid].filename,
					effect[i].filename);
				add_filler_before(i + 1, residentpage, bufferpage, looplabel);
				for(p = 0; p < 256; p++) {
					okpage[p] = !(effect[i].header.pageflags[p] & PF_USED);
				}
			} else if(!(effect[nexteid].flags & PCEF_EXTEND)) {
				zpcoll = 0;
				pagecoll = 0;
				for(p = 0; p < 256; p++) {
					zp[p] =
						effect[i].header.pageflags[p] &
						effect[nexteid].header.pageflags[p] &
						PF_ZPUSED;
					pages[p] =
						(effect[i].header.pageflags[p] & (PF_USED | PF_INHERIT)) &&
						(effect[nexteid].header.pageflags[p] & PF_USED);
					zpcoll |= zp[p];
					pagecoll |= pages[p];
				}
				if(pagecoll || zpcoll) {
					fprintf(stderr,
						"Warning: %s blank filler "
						"because '%s' and '%s' share ",
						i == neffect - 1? "Appending" : "Inserting",
						effect[i].filename,
						effect[nexteid].filename);
					if(pagecoll) {
						fprintf(stderr, "pages ");
						dump_ranges(pages);
						if(zpcoll) fprintf(stderr, " and ");
					}
					if(zpcoll) {
						fprintf(stderr, "zero-page locations ");
						dump_ranges(zp);
					}
					fprintf(stderr, ".\n");
					suggest_filler(&effect[i], &effect[nexteid], residentpage, bufferpage, zpstart);
					add_filler_before(i + 1, residentpage, bufferpage, looplabel);
					for(p = 0; p < 256; p++) {
						okpage[p] = !(effect[i].header.pageflags[p] & PF_USED);
					}
				}
			}
		}
	}
}

static void add_load(int eid, struct chunk *c, uint8_t page) {
	struct load *l;
	struct effect *e;

	if(eid < 0) errx(1, "Internal error! Trying to add load to null effect.");

	e = &effect[eid];

	if(!(e->header.flags & EF_SAFE_IO) && page >= 0xd0 && page <= 0xdf) {
		errx(
			1,
			"Internal error! Trying to add load "
			"under I/O during unsafe effect.");
	}

	for(l = e->loads; l; l = l->next) {
		if(l->chunk == c) break;
	}
	if(!l) {
		l = calloc(1, sizeof(struct load));
		l->chunk = c;
		l->next = e->loads;
		e->loads = l;
	}
	l->pages[page] = 1;
}

static int preferred_loading_slot(int later, int earlier) {
	if(later < 0) {
		return earlier;
	}
	if(effect[later].header.flags & EF_DONT_LOAD) {
		return earlier;
	}
	if(!effect[earlier].loading_preferable && effect[later].loading_preferable) {
		return later;
	}
	return earlier;
}

static void schedule_loads_for(int fallback, struct effect *target) {
	int i, j, p, iosafe;
	struct effect *candidate;
	struct chunk *c;

	candidate = &effect[fallback];
	iosafe = (candidate->header.flags & EF_SAFE_IO)? fallback : -1;

	for(i = fallback - 1; i >= 0; i--) {
		if(i == 0
		|| (effect[i + 2].flags & PCEF_HAS_LABEL)) {
			break;
		}
		candidate = &effect[i];
		for(p = 0; p < 256; p++) {
			if(target->needs_loading[p]
			&& (candidate->header.pageflags[p] & (PF_USED | PF_INHERIT))) {
				// Page can't be loaded during or before the candidate.
				for(j = 0; j < target->header.nchunk; j++) {
					c = &target->chunk[j];
					if((p << 8) < c->loadaddr + c->size
					&& (p << 8) + 0xff >= c->loadaddr) {
						if(p >= 0xd0 && p <= 0xdf) {
							add_load(iosafe, c, p);
						} else {
							add_load(fallback, c, p);
						}
					}
				}
				target->needs_loading[p] = 0;
			}
		}
		if(!(candidate->header.flags & EF_DONT_LOAD)
		&& !(effect[i + 1].flags & PCEF_HAS_LABEL)) {
			fallback = preferred_loading_slot(fallback, i);
			if(candidate->header.flags & EF_SAFE_IO) {
				iosafe = preferred_loading_slot(iosafe, i);
			}
		}
	}

	for(p = 0; p < 256; p++) {
		if(target->needs_loading[p]) {
			for(j = 0; j < target->header.nchunk; j++) {
				c = &target->chunk[j];
				if((p << 8) < c->loadaddr + c->size
				&& (p << 8) + 0xff >= c->loadaddr) {
					if(p >= 0xd0 && p <= 0xdf) {
						add_load(iosafe, c, p);
					} else {
						add_load(fallback, c, p);
					}
				}
			}
		}
	}
}

static void schedule_loads() {
	int i;

	for(i = 1; i < neffect; i++) {
		effect[i].loading_preferable = 1;
		if(effect[i].header.efo.v_main[0]
		|| effect[i].header.efo.v_main[1]) {
			effect[i].loading_preferable = 0;
		}
		if(effect[i].header.flags & EF_AVOID_LOAD) {
			effect[i].loading_preferable = 0;
		}
	}

	for(i = neffect - 1; i > 0; i--) {
		schedule_loads_for(i - 1, &effect[i]);
	}
}

static void generate_main(uint8_t *driver, int *pos, int eid, int interlock) {
	int mainpos, j;
	struct effect *e = &effect[eid];

	if(!e->condaddr && e->condval == COND_SPACE) {
		for(j = 0; j < 2; j++) {
			mainpos = *pos;
			if(e->header.efo.v_main[0]
			|| e->header.efo.v_main[1]) {
				driver[(*pos)++] = 0x20;
				driver[(*pos)++] = e->header.efo.v_main[0];
				driver[(*pos)++] = e->header.efo.v_main[1];
			}
			driver[(*pos)++] = 0xa9;		// lda imm
			driver[(*pos)++] = 0x7f;
			driver[(*pos)++] = 0x8d;		// sta abs
			driver[(*pos)++] = 0x00;
			driver[(*pos)++] = 0xdc;
			driver[(*pos)++] = 0xa9;		// lda imm
			driver[(*pos)++] = 0x10;
			driver[(*pos)++] = 0x2c;		// bit abs
			driver[(*pos)++] = 0x01;
			driver[(*pos)++] = 0xdc;
			driver[(*pos)++] = j? 0xd0 : 0xf0;	// bne / beq
			driver[*pos] = (mainpos - *pos - 1) & 0xff;
			(*pos)++;
		}
	} else if(!e->condaddr && e->condval == COND_AT) {
		if(interlock < 0x100) {
			driver[(*pos)++] = 0xe6;		// inc zp
			driver[(*pos)++] = interlock;
		} else {
			driver[(*pos)++] = 0xee;		// inc abs
			driver[(*pos)++] = interlock & 0xff;
			driver[(*pos)++] = interlock >> 8;
		}
		mainpos = *pos;
		if(e->header.efo.v_main[0]
		|| e->header.efo.v_main[1]) {
			driver[(*pos)++] = 0x20;
			driver[(*pos)++] = e->header.efo.v_main[0];
			driver[(*pos)++] = e->header.efo.v_main[1];
		}
		if(e->condaddr < 0x100) {
			driver[(*pos)++] = 0xa5;	// lda zp
			driver[(*pos)++] = interlock;
		} else {
			driver[(*pos)++] = 0xad;	// lda abs
			driver[(*pos)++] = interlock & 0xff;
			driver[(*pos)++] = interlock >> 8;
		}
		driver[(*pos)++] = 0xd0;		// bne
		driver[*pos] = (mainpos - *pos - 1) & 0xff;
		(*pos)++;
	} else if(!e->condaddr && e->condval == COND_STAY) {
		mainpos = *pos;
		if(e->header.efo.v_main[0]
		|| e->header.efo.v_main[1]) {
			driver[(*pos)++] = 0x20;
			driver[(*pos)++] = e->header.efo.v_main[0];
			driver[(*pos)++] = e->header.efo.v_main[1];
		}
		driver[(*pos)++] = 0x18;			// clc
		driver[(*pos)++] = 0x90;			// bcc
		driver[*pos] = (mainpos - *pos - 1) & 0xff;
		(*pos)++;
	} else {
		mainpos = *pos;
		if(e->header.efo.v_main[0]
		|| e->header.efo.v_main[1]) {
			driver[(*pos)++] = 0x20;
			driver[(*pos)++] = e->header.efo.v_main[0];
			driver[(*pos)++] = e->header.efo.v_main[1];
		}
		if(e->condaddr) {
			if(e->condaddr < 0x100) {
				driver[(*pos)++] = 0xa5;	// lda zp
				driver[(*pos)++] = e->condaddr;
			} else {
				driver[(*pos)++] = 0xad;	// lda abs
				driver[(*pos)++] = e->condaddr & 255;
				driver[(*pos)++] = e->condaddr >> 8;
			}
			driver[(*pos)++] = 0xc9;		// cmp imm
			driver[(*pos)++] = e->condval;
			driver[(*pos)++] = 0xd0;		// bne
			driver[*pos] = (mainpos - *pos - 1) & 0xff;
			(*pos)++;
		} else if(e->condval != COND_DROP) {
			errx(1, "Internal error: Bad condval");
		}
	}
}

static void generate_seekjump(uint8_t *driver, int *pos, struct effect *e, int residentpage, int zpstart) {
	uint16_t cleanup = e->header.efo.v_cleanup[0] | (e->header.efo.v_cleanup[1] << 8);

	if(!cleanup) {
		cleanup = (residentpage << 8) | PATCH_RTS;
	}

	// Pass the address of the cleanup routine on the stack.
	driver[(*pos)++] = 0xa9;	// lda imm
	driver[(*pos)++] = cleanup >> 8;
	driver[(*pos)++] = 0x48;	// pha
	driver[(*pos)++] = 0xa9;	// lda imm
	driver[(*pos)++] = cleanup & 0xff;
	driver[(*pos)++] = 0x48;	// pha

	// By pushing this vector on the stack, it becomes safe to load over
	// the old (current) driver, which could be anywhere in memory.

	driver[(*pos)++] = 0xa9;		// lda imm
	driver[(*pos)++] = 0;
	driver[(*pos)++] = 0x48;		// pha
	driver[(*pos)++] = 0xa9;		// lda imm
	driver[(*pos)++] = zpstart - 1;
	driver[(*pos)++] = 0x48;		// pha

	driver[(*pos)++] = 0x4c;		// jmp
	driver[(*pos)++] = 0x00;
	driver[(*pos)++] = residentpage;
}

static void generate_fadeout(
	uint8_t *driver,
	int *pos,
	int eid,
	uint8_t *seekcode,
	int seeksize,
	int residentpage,
	int zpstart)
{
	int mainpos;
	struct effect *e = &effect[eid];

	if(e->condaddr == 0 && e->condval == COND_STAY) {
		return;
	}

	if(!(eid + 1 < neffect && effect[eid + 1].flags & PCEF_EXTEND)) {
		if(e->header.efo.v_fadeout[0]
		|| e->header.efo.v_fadeout[1]) {
			mainpos = *pos;
			if(e->header.efo.v_main[0]
			|| e->header.efo.v_main[1]) {
				driver[(*pos)++] = 0x20;
				driver[(*pos)++] = e->header.efo.v_main[0];
				driver[(*pos)++] = e->header.efo.v_main[1];
			}
			if(e->header.efo.v_fadeout[0]
			|| e->header.efo.v_fadeout[1]) {
				driver[(*pos)++] = 0x20;
				driver[(*pos)++] = e->header.efo.v_fadeout[0];
				driver[(*pos)++] = e->header.efo.v_fadeout[1];
			}
			driver[(*pos)++] = 0x90;		// bcc
			driver[*pos] = (mainpos - *pos - 1) & 0xff;
			(*pos)++;
			if(e->header.flags & EF_JUMP) {
				// A, if positive, is the seek target
				driver[(*pos)++] = 0xaa;	// tax
				driver[(*pos)++] = 0x30;	// bmi
				mainpos = (*pos)++;
				memcpy(driver + *pos, seekcode, seeksize);
				*pos += seeksize;
				generate_seekjump(driver, pos, e, residentpage, zpstart);
				driver[mainpos] = *pos - (mainpos + 1);
			}
		}
	}
}

static void generate_drivers(
	int lastside,
	int residentpage,
	int bufferpage,
	int zpstart,
	uint8_t *earlysetup,
	int earlysetupsize,
	uint8_t *seekcode,
	int seeksize,
	int looplabel,
	int interlock)
{
	int eid, eid_start, eid_end, nexteid;
	uint8_t page;
	uint8_t driver[512];
	int pos, mainpos;
	struct chunk *c;
	struct effect *e, *nexte;
	int need_own_page;
	int did_main;
	uint16_t driver_org;

	for(eid = 1; eid < neffect; eid++) {
		e = &effect[eid];
		did_main = 0;
		pos = 0;
		if(eid == 1) {
			// Entry point when coming from stage 1.
			// Interrupts are off.
			memcpy(driver, earlysetup, earlysetupsize);
			pos = earlysetupsize;

			if(e->header.efo.v_prepare[0]
			|| e->header.efo.v_prepare[1]) {
				driver[pos++] = 0x18;	// clc
				driver[pos++] = 0x20;
				driver[pos++] = e->header.efo.v_prepare[0];
				driver[pos++] = e->header.efo.v_prepare[1];
			}
			driver[pos++] = 0x18;	// clc
			driver[pos++] = 0x90;	// bcc
			mainpos = pos++;
		}

		if((e->flags & PCEF_HAS_LABEL) || eid == 1) {
			// Entry point when coming from a seek operation.
			// or another diskside.
			e->driver_sidedoor = pos;
			if(e->header.efo.v_prepare[0]
			|| e->header.efo.v_prepare[1]) {
				if(eid == 1) {
					driver[pos++] = 0x38;	// sec
				}
				driver[pos++] = 0x20;
				driver[pos++] = e->header.efo.v_prepare[0];
				driver[pos++] = e->header.efo.v_prepare[1];
			}
			// Address to cleanup of the previous effect
			// was passed on the stack.
			driver[pos++] = 0xa9;		// lda imm
			driver[pos++] = 0x4c;		// jmp
			driver[pos++] = 0x85;		// sta zp
			driver[pos++] = zpstart + 0;
			driver[pos++] = 0x68;		// pla
			driver[pos++] = 0x85;		// sta zp
			driver[pos++] = zpstart + 1;
			driver[pos++] = 0x68;		// pla
			driver[pos++] = 0x85;		// sta zp
			driver[pos++] = zpstart + 2;
			driver[pos++] = 0x20;		// jsr
			driver[pos++] = zpstart + 0;
			driver[pos++] = 0;
			if(eid == 1) {
				driver[pos++] = 0x38;	// sec
			}
		} else {
			e->driver_sidedoor = -1;
		}

		if(eid == 1) {
			driver[mainpos] = pos - (mainpos + 1);
		}

		// Entry point when coming from the previous effect or looping.
		e->driver_frontdoor = pos;

		if(!(e->flags & PCEF_EXTEND)) {
			driver[pos++] = 0x78;	// sei
			if(e->header.efo.v_irq[0]
			|| e->header.efo.v_irq[1]) {
				driver[pos++] = 0xa9;	// lda imm
				driver[pos++] = e->header.efo.v_irq[0];
				driver[pos++] = 0x8d;	// sta abs
				driver[pos++] = 0xfe;
				driver[pos++] = 0xff;
				driver[pos++] = 0xa9;	// lda imm
				driver[pos++] = e->header.efo.v_irq[1];
				driver[pos++] = 0x8d;	// sta abs
				driver[pos++] = 0xff;
				driver[pos++] = 0xff;
				if(e->header.efo.v_setup[0]
				|| e->header.efo.v_setup[1]) {
					driver[pos++] = 0x20;
					driver[pos++] = e->header.efo.v_setup[0];
					driver[pos++] = e->header.efo.v_setup[1];
				}
				driver[pos++] = 0x4e;	// lsr abs
				driver[pos++] = 0x19;
				driver[pos++] = 0xd0;
				driver[pos++] = 0x58;	// cli
			} else if(e->header.efo.v_setup[0]
			|| e->header.efo.v_setup[1]) {
				driver[pos++] = 0x20;
				driver[pos++] = e->header.efo.v_setup[0];
				driver[pos++] = e->header.efo.v_setup[1];
			}
		}
		if(eid < neffect - 1) {
			nexteid = eid + 1;
		} else if(looplabel >= 0) {
			nexteid = seeklabel[looplabel];
		} else {
			nexteid = 0;
		}
		e->driver_nextjmp = -1;
		if(!nexteid) {
			if(lastside) {
				if(!did_main) {
					generate_main(driver, &pos, eid, interlock);
					generate_fadeout(driver, &pos, eid, seekcode, seeksize, residentpage, zpstart);
				}
				if(e->header.efo.v_cleanup[0]
				|| e->header.efo.v_cleanup[1]) {
					driver[pos++] = 0x20;
					driver[pos++] = e->header.efo.v_cleanup[0];
					driver[pos++] = e->header.efo.v_cleanup[1];
				}
				driver[pos++] = 0x18;	// clc
				driver[pos++] = 0x90;	// bcc
				driver[pos++] = 0xfe;
			} else {
				// We've already checked that there's no main routine.
				// Wait for the new disk.
				driver[pos++] = 0x20;
				driver[pos++] = 0x00;
				driver[pos++] = residentpage;
				if(!did_main) {
					//generate_main(driver, &pos, eid, interlock);
					generate_fadeout(driver, &pos, eid, seekcode, seeksize, residentpage, zpstart);
				}
				generate_seekjump(driver, &pos, e, residentpage, zpstart);
			}
		} else {
			nexte = &effect[nexteid];
			if((e->header.efo.v_main[0] || e->header.efo.v_main[1]) && !did_main) {
				generate_main(driver, &pos, eid, interlock);
				generate_fadeout(driver, &pos, eid, seekcode, seeksize, residentpage, zpstart);
				did_main = 1;
			}
			if(nexteid != eid + 1) {
				driver[pos++] = 0xa9;	// lda imm
				driver[pos++] = looplabel;
				memcpy(driver + pos, seekcode, seeksize);
				pos += seeksize;
			}
			if(e->condaddr == 0 && e->condval == COND_STAY) {
				e->driver_loadcall = -1;
			} else {
				e->driver_loadcall = pos;
				driver[pos++] = 0x20;
				driver[pos++] = 0x00;
				driver[pos++] = residentpage;
				if(!(nexte->flags & PCEF_EXTEND)) {
					if(nexte->header.efo.v_prepare[0]
					|| nexte->header.efo.v_prepare[1]) {
						if(nexteid == 1) {
							driver[pos++] = 0x38;	// sec
						}
						driver[pos++] = 0x20;
						driver[pos++] = nexte->header.efo.v_prepare[0];
						driver[pos++] = nexte->header.efo.v_prepare[1];
					}
				}
			}
			if(!did_main) {
				generate_main(driver, &pos, eid, interlock);
				generate_fadeout(driver, &pos, eid, seekcode, seeksize, residentpage, zpstart);
			}
			if(!(e->condaddr == 0 && e->condval == COND_STAY)) {
				if(!(nexte->flags & PCEF_EXTEND)) {
					if(e->header.efo.v_cleanup[0]
					|| e->header.efo.v_cleanup[1]) {
						driver[pos++] = 0x20;
						driver[pos++] = e->header.efo.v_cleanup[0];
						driver[pos++] = e->header.efo.v_cleanup[1];
					}
				}
				if(nexteid == 1) {
					driver[pos++] = 0x38;	// sec
				}
				e->driver_nextjmp = pos;
				driver[pos++] = 0x4c;	// jmp
				driver[pos++] = 0;
				driver[pos++] = 0;
			}
		}
		if(pos > 250) errx(1, "Driver too large!"); // must fit on page ff

		eid_start = eid;
		while(effect[eid_start].flags & PCEF_EXTEND) {
			eid_start--;
		}
		eid_end = eid;
		while(eid_end + 1 < neffect && (effect[eid_end + 1].flags & PCEF_EXTEND)) {
			eid_end++;
		}

		if(e->flags & PCEF_EXTEND) {
			driver_org = effect[eid - 1].driver_chunk->loadaddr + effect[eid - 1].driver_chunk->size;
		} else {
			driver_org = effect[eid_start].chunk[0].loadaddr + effect[eid_start].chunk[0].size;
		}

		// The early setup code mustn't cross a page boundary.
		need_own_page = (eid == 1 && (driver_org & 0xff) > 256 - earlysetupsize);

		page = (driver_org + pos - 1) >> 8; // last page of driver
		if((driver_org >> 8) != page || (page & 0xf0) == 0xd0) {
			// Driver crosses into a new page. Check if it's free.
			if(page > 0xff
			|| (page & 0xf0) == 0xd0
			|| page == residentpage
			|| page == bufferpage
			|| (effect[eid_start].header.pageflags[page] & (PF_LOADED | PF_USED | PF_INHERIT))
			|| (eid_start && effect[eid_start - 1].header.pageflags[page] & (PF_LOADED | PF_USED | PF_INHERIT))
			|| (eid_end + 1 < neffect && effect[eid_end + 1].header.pageflags[page] & (PF_LOADED | PF_USED | PF_INHERIT))) {
				need_own_page = 1;
			}
		}

		if(need_own_page) {
			page = find_free_page(eid, residentpage, bufferpage, looplabel);
			if(page < 0) {
				errx(1,
					"No room left for an effect driver for '%s'.",
					e->filename);
			}
			driver_org = page << 8;
		}

		if(effect[eid_start].header.nchunk >= MAXCHUNKS + 1) {
			errx(1, "Increase MAXCHUNKS.");
		}
		c = &effect[eid_start].chunk[effect[eid_start].header.nchunk++];
		e->driver_chunk = c;
		c->loadaddr = driver_org;
		c->size = pos;
		c->data = malloc(pos);
		memcpy(c->data, driver, pos);
		snprintf(
			c->name,
			sizeof(c->name),
			"%s:(drv)",
			effect[eid_start].filename);

		effect[eid_start].header.pageflags[page] |= PF_LOADED | PF_USED | PF_CODE;
		effect[eid_start].needs_loading[page] = 1;
		while(eid_start < eid_end) {
			effect[++eid_start].header.pageflags[page] |= PF_INHERIT;
		}
	}
}

static void patch_drivers(int looplabel) {
	int i, j, n, nexteid;
	uint8_t *driver;
	uint16_t drv_addr, drv_length;
	uint16_t entry;
	struct effect *e;
	struct chunk *dc;

	for(i = 1; i < neffect - 1; i++) {
		if(!effect[i].loads) {
			dc = effect[i].driver_chunk;
			j = effect[i].driver_loadcall;
			if(j >= 0) {
				n = 3; // size of jsr
				memmove(dc->data + j, dc->data + j + n, dc->size - j - n);
				dc->size -= n;
				if(effect[i].driver_nextjmp >= 0) {
					effect[i].driver_nextjmp -= n;
				}
			}
		}
	}

	for(i = 1; i < neffect; i++) {
		e = &effect[i];
		if(i < neffect - 1) {
			nexteid = i + 1;
		} else {
			nexteid = (looplabel >= 0)? seeklabel[looplabel] : 0;
		}
		if(nexteid && e->driver_nextjmp >= 0) {
			entry = effect[nexteid].driver_chunk->loadaddr + effect[nexteid].driver_frontdoor;
			e->driver_chunk->data[e->driver_nextjmp + 1] = entry & 0xff;
			e->driver_chunk->data[e->driver_nextjmp + 2] = entry >> 8;
		}
	}

	if(verbose) {
		for(i = 1; i < neffect; i++) {
			dc = effect[i].driver_chunk;
			drv_addr = dc->loadaddr;
			if(verbose == 1) {
				fprintf(
					stderr,
					"Driver for '%s' at $%04x.\n",
					effect[i].filename,
					drv_addr);
			} else {
				driver = dc->data;
				drv_length = dc->size;
				fprintf(
					stderr,
					"Driver for '%s' at $%04x:",
					effect[i].filename,
					drv_addr);
				for(j = 0; j < drv_length; j++) {
					fprintf(
						stderr,
						" %02x",
						driver[j]);
				}
				fprintf(stderr, "\n");
			}
		}
	}
}

static void visualise_mem(int ppc, int residentpage, int bufferpage, int looplabel) {
	int i, j, p, flags, loaded;
	char ch;
	struct load *ld;
	uint8_t loadtbl[256];
	char chartbl[257];
	int seekline;
	struct effect *e;

	for(p = 0, i = -1; p < 256; p += ppc) {
		if(p >> 4 != i) {
			fprintf(stderr, "%x", p >> 4);
			i = p >> 4;
		} else fprintf(stderr, " ");
	}
	fprintf(stderr, "sectors\n");

	for(i = 0; i < neffect; i++) {
		e = &effect[i];
		memset(loadtbl, 0, sizeof(loadtbl));
		for(ld = e->loads; ld; ld = ld->next) {
			for(p = 0; p < 256; p++) {
				loadtbl[p] |= ld->pages[p];
			}
		}
		seekline = !!(i + 1 < neffect && effect[i + 1].flags & PCEF_HAS_LABEL);

		for(p = 0; p < 256; p += ppc) {
			flags = 0;
			loaded = 0;
			for(j = 0; j < ppc; j++) {
				flags |= e->header.pageflags[p + j];
				if(p + j == residentpage || p + j == bufferpage) {
					flags |= PF_RESERVED;
				}
				loaded |= loadtbl[p + j];
			}
			if(flags & PF_INHERIT) {
				ch = '|';
			} else if(flags & PF_MUSIC) {
				ch = 'M';
			} else if(flags & PF_CODE) {
				ch = 'c';
			} else if(flags & PF_LOADED) {
				ch = 'L';
			} else if(flags & PF_USED) {
				ch = 'U';
			} else if(flags & PF_RESERVED) {
				ch = 'r';
			} else if(loaded && !seekline) {
				ch = '*';
			} else {
				ch = '.';
			}
			chartbl[p / ppc] = ch;
		}
		chartbl[p / ppc] = 0;
		fprintf(stderr, "%s", chartbl);
		if(e->blocks_loaded && !seekline) {
			fprintf(stderr, "%3d %s",
				e->blocks_loaded,
				e->filename);
		} else {
			fprintf(stderr, "    %s",
				e->filename);
		}
		if(e->header.flags & EF_JUMP) {
			fprintf(stderr, " (J)\n");
		} else {
			fprintf(stderr, "\n");
		}
		if(seekline) {
			for(p = 0; p < 256; p += ppc) {
				if(!strchr("r.", chartbl[p / ppc])) {
					chartbl[p / ppc] = '|';
				}
			}
		}
		if(seekline) {
			for(p = 0; p < 256; p += ppc) {
				loaded = 0;
				for(j = 0; j < ppc; j++) {
					loaded |= loadtbl[p + j];
				}
				if(loaded && chartbl[p / ppc] == '.') {
					chartbl[p / ppc] = '*';
				}
			}
			fprintf(stderr, "%s", chartbl);
			if(e->blocks_loaded) {
				fprintf(stderr, "%3d |", e->blocks_loaded);
			} else {
				fprintf(stderr, "    |");
			}
			for(j = 0; j < 64; j++) {
				if(seeklabel[j] == i + 1) {
					fprintf(stderr, "%02x:", j);
				}
			}
			fprintf(stderr, "\n");
		}
	}
	if(looplabel >= 0) {
		fprintf(stderr, "...and loop to %02x.\n", looplabel);
	}
}

static struct chunk *rebuild_chunks(struct load *ld) {
	struct chunk *list = 0, *ch, *ch2, **ptr;
	int p;
	uint32_t start, end;

	while(ld) {
		for(p = 0; p < 256; p++) {
			if(ld->pages[p]) {
				start = p << 8;
				end = start + 0x100;
				if(start < ld->chunk->loadaddr) {
					start = ld->chunk->loadaddr;
				}
				if(end > ld->chunk->loadaddr + ld->chunk->size) {
					end = ld->chunk->loadaddr + ld->chunk->size;
				}
				ch = calloc(1, sizeof(*ch));
				ch->loadaddr = start;
				ch->size = end - start;
				ch->data = malloc(ch->size);
				memcpy(
					ch->data,
					ld->chunk->data + (start - ld->chunk->loadaddr),
					ch->size);
				memcpy(
					ch->name,
					ld->chunk->name,
					sizeof(ch->name));
				for(ptr = &list; *ptr; ptr = &(*ptr)->next) {
					if((*ptr)->loadaddr > start) break;
				}
				ch->next = *ptr;
				*ptr = ch;
			}
		}
		ld = ld->next;
	}

	for(ch = list; ch; ch = ch2) {
		ch2 = ch->next;
		if(ch2 && ch->loadaddr + ch->size == ch->next->loadaddr) {
			ch->data = realloc(
				ch->data,
				ch->size + ch->next->size);
			memcpy(
				ch->data + ch->size,
				ch->next->data,
				ch->next->size);
			if(strcmp(ch->name, ch->next->name)) {
				char buf[sizeof(ch->name)];
				unsigned int len = snprintf(
					buf,
					sizeof(ch->name),
					"%s+%s",
					ch->name,
					ch->next->name);
				if(len > sizeof(buf) - 1) {
					buf[sizeof(buf) - 4] = '.';
					buf[sizeof(buf) - 3] = '.';
					buf[sizeof(buf) - 2] = '.';
					buf[sizeof(buf) - 1] = 0;
				}
				memcpy(ch->name, buf, sizeof(buf));
			}
			ch->size += ch2->size;
			ch->next = ch->next->next;
			free(ch2->data);
			free(ch2);
			ch2 = ch;
		}
	}

#if 0
	if(verbose >= 2) {
		fprintf(stderr, "Chunks to compress:\n");
		for(ch = list; ch; ch = ch->next) {
			fprintf(
				stderr,
				"%04x-%04x %s\n",
				ch->loadaddr,
				ch->loadaddr + ch->size - 1,
				ch->name);
		}
	}
#endif

	return list;
}

static void usage(char *prgname) {
	fprintf(stderr, "%s\n\n", SPINDLE_VERSION);
	fprintf(stderr, "Usage: %s [options] script\n", prgname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -h --help         Display this text.\n");
	fprintf(stderr, "  -V --version      Display version information.\n");
	fprintf(stderr, "  -v --verbose      Be verbose. Can be specified multiple times.\n");
	fprintf(stderr, "  -w --wide         Make memory chart wider. Can be specified twice.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -o --output       Output filename. Default: disk.d64\n");
	fprintf(stderr, "  -F --40           Create a 40-track disk image.\n");
	fprintf(stderr, "  -q --squeeze      Compress a little better (and load a little slower).\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -r --resident     Page where the loader resides, in hex. Default: 02.\n");
	fprintf(stderr, "  -b --buffer       Page acting as buffer while loading, in hex. Default: 03.\n");
	fprintf(stderr, "  -z --zeropage     Start of zeropage area (five bytes), in hex. Default: f4.\n");
	fprintf(stderr, "  -@ --interlock    Set interlock address for @ conditions.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -n --next-magic   24-bit code to identify the next disk side.\n");
	fprintf(stderr, "  -m --my-magic     24-bit code required to enter this side.\n");
	fprintf(stderr, "  -L --loop         Loop to label (hex, 00-3f) at end of script.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -a --dirart       Name of file containing directory art.\n");
	fprintf(stderr, "  -t --title        Name of disk.\n");
	fprintf(stderr, "  -i --disk-id      Disk ID. Should differ between sides.\n");
	fprintf(stderr, "  -d --dir-entry    Which directory entry is the PRG, in hex. Default: 0.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -s --early-setup  Read setup-code from file (up to 128 bytes, unknown pc).\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -E --errors       Simulate read errors with given probability (0-99 decimal).\n");
	exit(1);
}

int main(int argc, char **argv) {
	struct option longopts[] = {
		{"help", 0, 0, 'h'},
		{"version", 0, 0, 'V'},
		{"verbose", 0, 0, 'v'},
		{"output", 1, 0, 'o'},
		{"40", 0, 0, 'F'},
		{"squeeze", 0, 0, 'q'},
		{"resident", 0, 0, 'r'},
		{"buffer", 0, 0, 'b'},
		{"zeropage", 0, 0, 'z'},
		{"wide", 0, 0, 'w'},
		{"dirart", 1, 0, 'a'},
		{"dir-entry", 1, 0, 'd'},
		{"early-setup", 1, 0, 's'},
		{"errors", 1, 0, 'E'},
		{"title", 1, 0, 't'},
		{"disk-id", 1, 0, 'i'},
		{"my-magic", 1, 0, 'm'},
		{"next-magic", 1, 0, 'n'},
		{"loop", 1, 0, 'L'},
		{"interlock", 1, 0, '@'},
		{0, 0, 0, 0}
	};
	char *outname = "disk.d64", *dirart_fname = 0, *setupname = 0;
	uint8_t dirart[DIRARTBLOCKS * 8 * 16];
	int opt, dir_entry = 0, visualisation_width = 4, fortytracks = 0, err_prob = 0;
	char *disk_title = "SPINDLE DISK", *disk_id = "uk";
	struct blockfile bf;
	int i, j;
	int looplabel = -1, interlock = -1;
	struct chunk *ch;
	uint32_t my_magic = 0x54464c, next_magic = 0;
	int residentpage = 2, bufferpage = 3, zpreloc = 0xf4;
	uint8_t *setupcode = data_commonsetup;
	int setupsize = sizeof(data_commonsetup);
	FILE *f;
	uint16_t entryvector;

	do {
		opt = getopt_long(argc, argv, "?hVvo:r:b:z:wa:d:Fqs:E:t:i:m:n:L:@:", longopts, 0);
		switch(opt) {
			case 0:
			case '?':
			case 'h':
				usage(argv[0]);
				break;
			case 'V':
				fprintf(stderr, "%s\n", SPINDLE_VERSION);
				return 0;
			case 'v':
				verbose++;
				break;
			case 'w':
				if(visualisation_width > 1) {
					visualisation_width /= 2;
				}
				break;
			case 'o':
				outname = strdup(optarg);
				break;
			case 'r':
				residentpage = parseparam(optarg, 0);
				if(residentpage < 2 || residentpage > 0xfe || ((residentpage & 0xf0) == 0xd0)) {
					errx(1, "Invalid resident page ($%02xxx)", residentpage);
				}
				if(residentpage >= 8 && residentpage <= 9) {
					errx(1, "Resident page collides with bootloader (801-9ff).");
				}
				break;
			case 'b':
				bufferpage = parseparam(optarg, 0);
				if(bufferpage < 2 || bufferpage > 0xfe || ((bufferpage & 0xf0) == 0xd0)) {
					errx(1, "Invalid buffer page ($%02xxx)", bufferpage);
				}
				break;
			case 'z':
				zpreloc = parseparam(optarg, 0);
				if(zpreloc < 2 || zpreloc > 0xfb) {
					errx(1, "Start of zeropage area must be in the range 02-fb.");
				}
				break;
			case 'a':
				dirart_fname = strdup(optarg);
				break;
			case 'd':
				dir_entry = parseparam(optarg, 0);
				break;
			case 'F':
				fortytracks = 1;
				break;
			case 'q':
				squeeze_option = 1;
				break;
			case 's':
				setupname = strdup(optarg);
				break;
			case 'E':
				err_prob = atoi(optarg);
				if(err_prob < 0 || err_prob > 99) {
					errx(1, "Error probability must be in the range 0-99.");
				}
				break;
			case 't':
				disk_title = strdup(optarg);
				break;
			case 'i':
				disk_id = strdup(optarg);
				if(strlen(disk_id) != 2) errx(1, "Disk ID must be two characters.");
				break;
			case 'm':
				my_magic = parseparam(optarg, 0) & 0xffffff;
				break;
			case 'n':
				next_magic = parseparam(optarg, 0) & 0xffffff;
				break;
			case 'L':
				looplabel = parseparam(optarg, 0);
				if(looplabel < 0 || looplabel > 0x3f) {
					errx(1, "Loop label must be in the range 00-3f.");
				}
				break;
			case '@':
				interlock = parseparam(optarg, 0);
				if(interlock < 0 || interlock > 0xffff) {
					errx(1, "Invalid interlock address.");
				}
		}
	} while(opt >= 0);

	if(residentpage == bufferpage) {
		errx(1, "Resident page cannot be equal to buffer page.");
	}

	if(argc != optind + 1) usage(argv[0]);

	if(setupname) {
		f = fopen(setupname, "rb");
		if(!f) err(1, "fopen: %s", setupname);
		setupcode = malloc(128);
		setupsize = fread(setupcode, 1, 128, f);
		fclose(f);
	}

	if(next_magic && (looplabel >= 0)) {
		errx(1, "You can't use --next-magic and --loop at the same time.");
	}

	load_script(argv[optind], residentpage, bufferpage, zpreloc, setupcode, setupsize, looplabel, interlock);
	if(next_magic
	&& (effect[neffect - 1].header.efo.v_main[0]
	|| effect[neffect - 1].header.efo.v_main[1])) {
		errx(
			1,
			"A flip-disk part (last effect on non-last side) "
			"cannot have a main routine.");
	}
	make_dirart(dirart, dirart_fname);

	insert_fillers(residentpage, bufferpage, zpreloc, looplabel);
	patch_effects();
	generate_drivers(
		!next_magic,
		residentpage,
		bufferpage,
		zpreloc,
		setupcode,
		setupsize,
		data_seek,
		sizeof(data_seek) - 1, // -1 removes the rts
		looplabel,
		interlock);
	schedule_loads();
	patch_drivers(looplabel);

	pack_init();
	disk_init(disk_title, disk_id, fortytracks);
	disk_storeloader(
		&bf,
		dirart,
		dir_entry,
		my_magic,
		next_magic,
		err_prob,
		effect[1].driver_chunk->loadaddr,
		residentpage,
		bufferpage,
		zpreloc);

	j = 0;
	for(i = 0; i < neffect; i++) {
		if(effect[i].loads) {
			if(i + 1 < neffect && effect[i + 1].flags & PCEF_HAS_LABEL) {
				for(j = 0; j < 64; j++) {
					if(seeklabel[j] == i + 1) {
						disk_set_seekpoint(&bf, j);
					}
				}
			}
			ch = rebuild_chunks(effect[i].loads);
			entryvector = 0;
			if(i + 1 < neffect && effect[i + 1].driver_sidedoor >= 0) {
				entryvector = effect[i + 1].driver_chunk->loadaddr + effect[i + 1].driver_sidedoor;
			}
			effect[i].blocks_loaded = compress_job(
				ch,
				&bf,
				'0' + (j % 10),
				residentpage,
				entryvector);
			j++;
		}
	}
	disk_closeside(&bf, !next_magic);
	visualise_mem(visualisation_width, residentpage, bufferpage, looplabel);
	disk_write(outname);

	return 0;
}
