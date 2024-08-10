/* Spindle by lft, https://linusakesson.net/software/spindle/
 */

#include <err.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "disk.h"
#include "pack.h"
#include "util.h"

struct group {
	struct chunk	*chunk;
	struct group	*next;
	uint8_t		seek;
};

struct group *script;

int verbose = 0;
int squeeze_option;

static uint16_t load_script(char *filename, int entry) {
	FILE *f, *cf;
	char buf[256], *ptr, *name;
	int bang, newgroup = 1, firstchunk = 1;
	int slot;
	long int loadaddr, offset, length;
	struct chunk *chunk, **cdest = 0;
	struct group *group = 0, **gdest = &script;

	f = fopen(filename, "r");
	if(!f) err(1, "fopen: %s", filename);

	while(fgets(buf, sizeof(buf), f)) {
		if(strlen(buf) && buf[strlen(buf) - 1] == '\n') {
			buf[strlen(buf) - 1] = 0;
		}
		if(strlen(buf) && buf[strlen(buf) - 1] == '\r') {
			buf[strlen(buf) - 1] = 0;
		}
		ptr = buf;
		while(*ptr == ' ' || *ptr == '\t') ptr++;
		if(!*ptr) {
			newgroup = 1;
		} else if(*ptr != ';' && *ptr != '#') {
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
			if(*name && name[strlen(name) - 1] == ':' && !*ptr) {
				slot = parseparam(name, &ptr);
				if(strcmp(ptr, ":") || slot < 0 || slot > 0x3f) {
					errx(1, "Invalid seek label (range 00-3f).");
				}
				group = malloc(sizeof(struct group));
				group->chunk = 0;
				group->seek = slot;
				*gdest = group;
				gdest = &group->next;
				newgroup = 1;
			} else {
				loadaddr = parseparam(ptr, &ptr);
				while(*ptr == ' ' || *ptr == '\t') ptr++;
				if(*ptr == '!') {
					bang = 1;
					ptr++;
				} else {
					bang = 0;
				}
				offset = parseparam(ptr, &ptr);
				length = parseparam(ptr, &ptr);
				while(*ptr == ' ' || *ptr == '\t') ptr++;
				if(*ptr) {
					errx(
						1,
						"Unexpected characters at end "
						"of script line (%s).",
						ptr);
				}

				if(loadaddr < 0 || loadaddr > 0xffff) {
					errx(1, "Invalid load address ($%lx)", loadaddr);
				}
				if(bang) {
					errx(1, "Loading directly into I/O registers (with '!') is no longer supported.");
				}
				if(length < 0 || length > 0xffff) {
					errx(1, "Invalid load length ($%lx)", length);
				}
				if(offset < 0) {
					errx(1, "Invalid load offset ($%lx)", offset);
				}

				if(newgroup) {
					group = malloc(sizeof(struct group));
					cdest = &group->chunk;
					*gdest = group;
					gdest = &group->next;
					newgroup = 0;
				}

				if(length == 0) length = 0xffff;

				chunk = calloc(1, sizeof(struct chunk));
				chunk->data = malloc(length);
				cf = fopen(name, "rb");
				if(!cf) err(1, "fopen: %s", name);
				if(fseek(cf, offset, SEEK_SET) < 0) {
					err(1, "fseek: %s, $%lx", name, offset);
				}
				if(!loadaddr) {
					loadaddr = fgetc(cf);
					loadaddr |= fgetc(cf) << 8;
					if(loadaddr < 0 || loadaddr > 0xffff) {
						errx(
							1,
							"Error obtaining load "
							"address from file: %s",
							name);
					}
				}
				length = fread(chunk->data, 1, length, cf);
				if(!length) err(1, "fread: %s", name);
				fclose(cf);
				if(firstchunk) {
					if(entry < 0) entry = loadaddr;
					firstchunk = 0;
				}
				chunk->loadaddr = loadaddr;
				chunk->size = length;
				snprintf(chunk->name, sizeof(chunk->name), "%s", name);
				*cdest = chunk;
				cdest = &chunk->next;
			}
		}
	}
	*gdest = 0;

	if(!script) errx(1, "Empty script!");

	fclose(f);

	return (uint16_t) entry;
}

void dump_script(uint16_t jaddr) {
	struct group *g;
	struct chunk *c;
	int ncall = 0;
	int i;

	for(g = script; g; g = g->next) {
		if(!g->chunk) {
			fprintf(stderr, "Seek point [%02x]:\n", g->seek);
		} else {
			if(!ncall) {
				fprintf(
					stderr,
					"At startup (with entry at $%04x):\n",
					jaddr);
			} else {
				fprintf(stderr, "Loader call #%d:\n", ncall);
			}
			for(c = g->chunk; c; c = c->next) {
				fprintf(
					stderr,
					" * $%04x-$%04x (",
					c->loadaddr,
					c->loadaddr + c->size - 1);
				for(i = 0; i < 8 && i < c->size; i++) {
					if(i) fprintf(stderr, " ");
					fprintf(stderr, "%02x", c->data[i]);
				}
				if(i < c->size) fprintf(stderr, " ...");
				fprintf(stderr, ") from \"%s\"\n", c->name);
			}
			ncall++;
		}
	}
}

static void usage(char *prgname) {
	fprintf(stderr, "%s\n\n", SPINDLE_VERSION);
	fprintf(stderr, "Usage: %s [options] script\n", prgname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -h --help         Display this text.\n");
	fprintf(stderr, "  -V --version      Display version information.\n");
	fprintf(stderr, "  -v --verbose      Be verbose. Can be specified multiple times.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -o --output       Output filename. Default: disk.d64\n");
	fprintf(stderr, "  -F --40           Create a 40-track disk image.\n");
	fprintf(stderr, "  -q --squeeze      Compress a little better (and load a little slower).\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -e --entry        Entry-point after initial loading, in hex.\n");
	fprintf(stderr, "  -r --resident     Page used by the loader, in hex. Default: 02.\n");
	fprintf(stderr, "  -b --buffer       Page acting as buffer while loading, in hex. Default: 03.\n");
	fprintf(stderr, "  -z --zeropage     Start of zeropage area (five bytes), in hex. Default: f4.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -n --next-magic   24-bit code to identify the next disk side.\n");
	fprintf(stderr, "  -m --my-magic     24-bit code required to enter this side.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -a --dirart       Name of file containing directory art.\n");
	fprintf(stderr, "  -t --title        Name of disk.\n");
	fprintf(stderr, "  -i --disk-id      Disk ID. Should differ between sides.\n");
	fprintf(stderr, "  -d --dir-entry    Which directory entry is the PRG, in hex. Default: 0.\n");
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
		{"dirart", 1, 0, 'a'},
		{"dir-entry", 1, 0, 'd'},
		{"entry", 1, 0, 'e'},
		{"errors", 1, 0, 'E'},
		{"title", 1, 0, 't'},
		{"disk-id", 1, 0, 'i'},
		{"my-magic", 1, 0, 'm'},
		{"next-magic", 1, 0, 'n'},
		{0, 0, 0, 0}
	};
	char *outname = "disk.d64", *dirart_fname = 0;
	uint8_t dirart[DIRARTBLOCKS * 8 * 16];
	int opt, dir_entry = 0, fortytracks = 0, err_prob = 0;
	long int entry = -1;
	char *disk_title = "SPINDLE DISK", *disk_id = "uk";
	uint16_t jumpaddr;
	struct blockfile bf;
	struct group *g;
	uint32_t my_magic = 0x54464c, next_magic = 0;
	int residentpage = 2, bufferpage = 3, zpreloc = 0xf4;
	int job;

	do {
		opt = getopt_long(
			argc,
			argv,
			"?hVvo:r:b:z:a:d:e:FqE:t:i:m:n:",
			longopts,
			0);
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
			case 'o':
				outname = strdup(optarg);
				break;
			case 'r':
				residentpage = parseparam(optarg, 0);
				if(residentpage < 2 || residentpage > 0xff || ((residentpage & 0xf0) == 0xd0)) {
					errx(1, "Invalid resident page ($%02xxx)", residentpage);
				}
				if(residentpage >= 8 && residentpage <= 9) {
					errx(1, "Resident page collides with bootloader (801-9ff).");
				}
				break;
			case 'b':
				bufferpage = parseparam(optarg, 0);
				if(bufferpage < 2 || bufferpage > 0xff || ((bufferpage & 0xf0) == 0xd0)) {
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
			case 'e':
				entry = parseparam(optarg, 0);
				if(entry < 0 || entry > 0xffff) {
					errx(
						1,
						"Invalid entry point ($%lx)",
						entry);
				}
				break;
			case 'F':
				fortytracks = 1;
				break;
			case 'q':
				squeeze_option = 1;
				break;
			case 'E':
				err_prob = atoi(optarg);
				if(err_prob < 0 || err_prob > 99)
					errx(1, "Error probability must be in the range 0-99.");
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
		}
	} while(opt >= 0);

	if(residentpage == bufferpage) {
		errx(1, "Resident page cannot be equal to buffer page.");
	}

	if(argc != optind + 1) usage(argv[0]);

	jumpaddr = load_script(argv[optind], entry);
	make_dirart(dirart, dirart_fname);

	pack_init();
	disk_init(disk_title, disk_id, fortytracks);
	disk_storeloader(
		&bf,
		dirart,
		dir_entry,
		my_magic,
		next_magic,
		err_prob,
		jumpaddr,
		residentpage,
		bufferpage,
		zpreloc);

	job = 0;
	for(g = script; g; g = g->next) {
		if(g->chunk) {
			compress_job(g->chunk, &bf, '0' + (job % 10), residentpage, 0);
			job++;
		} else {
			disk_set_seekpoint(&bf, g->seek);
		}
	}
	disk_closeside(&bf, !next_magic);

	if(verbose) dump_script(jumpaddr);

	disk_write(outname);

	return 0;
}
