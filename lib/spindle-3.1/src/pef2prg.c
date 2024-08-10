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
#include "prgloader.h"
#include "commonsetup.h"

#define SPLIT_ADDR 0xa00

struct chunk {
	uint16_t	loadaddr;
	uint16_t	size;
	uint8_t		*data;
	char		filename[32];
} chunk[MAXCHUNKS + 1];
int nchunk = 0;

struct header header;

static void load_pef(char *filename, uint16_t playroutine) {
	FILE *f;
	int i;
	struct chunk *c;
	uint16_t jsr, nextjsr;

	f = fopen(filename, "rb");
	if(!f) err(1, "fopen: %s", filename);
	
	fread(&header, sizeof(header), 1, f);
	if(strncmp((char *) header.magic, "PEF3", 4)) {
		errx(1, "Invalid pef header: %s", filename);
	}

	for(i = 0; i < header.nchunk; i++) {
		c = &chunk[nchunk];
		c->size = fgetc(f);
		c->size |= fgetc(f) << 8;
		c->data = malloc(c->size);
		c->loadaddr = fgetc(f);
		c->loadaddr |= fgetc(f) << 8;
		fread(c->filename, 32, 1, f);
		fread(c->data, c->size, 1, f);
		if(i == 0 && playroutine) {
			jsr = header.efo.v_jsr[0];
			jsr |= header.efo.v_jsr[1] << 8;
			while(jsr) {
				if(jsr < c->loadaddr || jsr >= c->loadaddr + c->size) {
					fprintf(stderr, "Playroutine call address out of bounds ($%04x)\n", jsr);
					exit(1);
				}
				nextjsr = c->data[jsr - c->loadaddr + 1];
				nextjsr |= c->data[jsr - c->loadaddr + 2] << 8;
				c->data[jsr - c->loadaddr + 0] = 0x20; // jsr
				c->data[jsr - c->loadaddr + 1] = playroutine & 0xff;
				c->data[jsr - c->loadaddr + 2] = playroutine >> 8;
				jsr = nextjsr;
			}
		}
		nchunk++;
		if(c->loadaddr < SPLIT_ADDR && c->loadaddr + c->size > SPLIT_ADDR) {
			chunk[nchunk].size = c->size - (SPLIT_ADDR - c->loadaddr);
			chunk[nchunk].loadaddr = SPLIT_ADDR;
			chunk[nchunk].data = c->data + (SPLIT_ADDR - c->loadaddr);
			memcpy(chunk[nchunk].filename, c->filename, sizeof(chunk[nchunk].filename));
			c->size = (SPLIT_ADDR - c->loadaddr);
			nchunk++;
		}
	}

	fclose(f);

	if(header.n_stream_chunk) {
		errx(1, "Streaming data is not supported by pef2prg. Please use pefchain.");
	}

	if(header.pageflags[0x02] & (PF_LOADED | PF_USED)) {
		errx(1,
			"Effects using memory in the range $200-$2ff "
			"are not supported by pef2prg. Use pefchain "
			"or spin.");
	}
}

static int cmp_chunk(const void *a, const void *b) {
	const struct chunk *aa = (const struct chunk *) a;
	const struct chunk *bb = (const struct chunk *) b;

	return
		((aa->loadaddr - SPLIT_ADDR) & 0xffff) -
		((bb->loadaddr - SPLIT_ADDR) & 0xffff);
}

static void put_vector(int *pos, uint8_t *ptr) {
	*pos -= 3;
	if(ptr[0] || ptr[1]) {
		data_prgloader[*pos + 1] = ptr[0];
		data_prgloader[*pos + 2] = ptr[1];
	}
}

static void save_prg(char *filename, int playtime, char *setupname) {
	FILE *f;
	uint16_t end_addr = 0xa00;
	int i;
	uint8_t *setupcode = data_commonsetup;
	int setupsize = sizeof(data_commonsetup);

	for(i = 0; i < nchunk; i++) {
		end_addr += chunk[i].size;
		end_addr += 4;
	}

	if(setupname) {
		f = fopen(setupname, "rb");
		if(!f) err(1, "fopen: %s", setupname);
		setupcode = malloc(128);
		setupsize = fread(setupcode, 1, 128, f);
		fclose(f);
	}

	f = fopen(filename, "wb");
	if(!f) err(1, "fopen: %s", filename);

	i = sizeof(data_prgloader);
	data_prgloader[--i] = end_addr >> 8;
	data_prgloader[--i] = end_addr & 0xff;
	data_prgloader[--i] = 0xa9;
	data_prgloader[--i] = playtime;
	data_prgloader[--i] = header.efo.v_irq[1];
	data_prgloader[--i] = header.efo.v_irq[0];
	put_vector(&i, header.efo.v_cleanup);
	put_vector(&i, header.efo.v_fadeout);
	put_vector(&i, header.efo.v_main);
	put_vector(&i, header.efo.v_setup);
	put_vector(&i, header.efo.v_prepare);

	fwrite(data_prgloader, sizeof(data_prgloader), 1, f);
	fwrite(setupcode, setupsize, 1, f);
	fputc(0x60, f); // rts

	i = 0x801 - 2 + sizeof(data_prgloader) + setupsize + 1;
	while(i < SPLIT_ADDR) {
		fputc(0, f);
		i++;
	}

	for(i = 0; i < nchunk; i++) {
		fwrite(chunk[i].data, chunk[i].size, 1, f);
		fputc(chunk[i].size & 0xff, f);
		fputc(chunk[i].size >> 8, f);
		fputc((chunk[i].loadaddr + chunk[i].size) & 0xff, f);
		fputc((chunk[i].loadaddr + chunk[i].size) >> 8, f);
	}

	fclose(f);
}

static void protect_range(int first, int last, FILE *f) {
	fprintf(
		f,
		"watch store %04x %04x if @ram:$2ff==$aa\n",
		first << 8,
		(last == 0xff)? 0xfff9 : ((last << 8) | 0xff));
}

static void protect_zprange(int first, int last, FILE *f) {
	fprintf(
		f,
		"watch store %02x %02x if @ram:$2ff==$aa\n",
		first,
		last);
}

static void emit_vice_commands(char *filename) {
	int i, start = 0;
	FILE *f;

	f = fopen(filename, "w");
	if(!f) err(1, "fopen: %s", filename);

	fprintf(f, "watch store 0200 02fe if @ram:$2ff==$aa\n");
	for(i = 3; i <= 0xff; i++) {
		if((header.pageflags[i] & (PF_LOADED | PF_USED | PF_INHERIT | PF_MUSIC))
		|| (i & 0xf0) == 0xd0) {
			if(start) {
				protect_range(start, i - 1, f);
			}
			start = 0;
		} else {
			if(!start) {
				start = i;
			}
		}
	}
	if(start) {
		protect_range(start, i - 1, f);
	}
	start = 0;
	for(i = 2; i <= 0xff; i++) {
		if(header.pageflags[i] & PF_ZPUSED) {
			if(start) {
				protect_zprange(start, i - 1, f);
			}
			start = 0;
		} else {
			if(!start) {
				start = i;
			}
		}
	}
	if(start) {
		protect_zprange(start, i - 1, f);
	}

	fclose(f);
}

static void usage(char *prgname) {
	fprintf(stderr, "%s\n\n", SPINDLE_VERSION);
	fprintf(stderr, "Usage: %s [options] effect.pef\n\n", prgname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -h --help         Display this text.\n");
	fprintf(stderr, "  -V --version      Display version information.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -o --output       Output filename (prg).\n");
	fprintf(stderr, "  -m --moncommands  Output filename for Vice monitor commands.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -s --early-setup  Read setup-code from file (up to 128 bytes, unknown pc).\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -p --playtime     Simulated playroutine time in rasterlines (dec, 0 = off).\n");
	exit(1);
}

int main(int argc, char **argv) {
	int opt;
	struct option longopts[] = {
		{"help", 0, 0, 'h'},
		{"version", 0, 0, 'V'},
		{"output", 1, 0, 'o'},
		{"mon", 1, 0, 'm'},
		{"playtime", 1, 0, 'p'},
		{"early-setup", 1, 0, 's'},
		{0, 0, 0, 0}
	};
	char *outname = "a.prg", *monname = 0, *setupname = 0;
	int playtime = 25;

	do {
		opt = getopt_long(argc, argv, "?hVo:m:p:s:", longopts, 0);
		switch(opt) {
			case 0:
			case '?':
			case 'h':
				usage(argv[0]);
				break;
			case 'V':
				fprintf(stderr, "%s\n", SPINDLE_VERSION);
				return 0;
			case 'o':
				outname = strdup(optarg);
				break;
			case 'm':
				monname = strdup(optarg);
				break;
			case 's':
				setupname = strdup(optarg);
				break;
			case 'p':
				playtime = atoi(optarg);
				if(playtime < 0 || playtime > 255) {
					fprintf(stderr, "Invalid playroutine time.\n");
					exit(1);
				}
				break;
		}
	} while(opt >= 0);

	if(argc != optind + 1) usage(argv[0]);

	load_pef(argv[optind], playtime? 0x200 : 0);
	qsort(chunk, nchunk, sizeof(struct chunk), cmp_chunk);
	save_prg(outname, playtime, setupname);
	if(monname) {
		emit_vice_commands(monname);
	}

	return 0;
}
