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
#include "util.h"

struct header header;

struct chunk {
	uint16_t	loadaddr;
	uint16_t	size;
	uint8_t		*data;
	char		filename[32];
} chunk[MAXCHUNKS];

int is_stream = 0, stream_start = 0;

void add_chunk(FILE *f, int loadaddr, int loadsize, char *filename, int pageflags) {
	struct chunk *c;
	int i;
	char *ptr;

	while((ptr = strchr(filename, '/')) || (ptr = strchr(filename, '\\'))) {
		filename = ptr + 1;
	}

	if(header.nchunk >= MAXCHUNKS) {
		errx(1, "Too many files. (Max %d)", MAXCHUNKS);
	}
	c = &chunk[header.nchunk];
	if(loadaddr < 0) {
		loadaddr = fgetc(f);
		loadaddr |= fgetc(f) << 8;
		loadsize -= 2;
	}
	c->loadaddr = loadaddr;
	c->size = loadsize;
	if(!is_stream || header.nchunk == stream_start) {
		for(i = 0; i < header.nchunk; i++) {
			if(chunk[i].loadaddr < loadaddr + loadsize
			&& chunk[i].loadaddr + chunk[i].size > loadaddr) {
				errx(1, "Error: Overlapping files \"%s\" (%04x-%04x) and \"%s\" (%04x-%04x).",
					chunk[i].filename,
					chunk[i].loadaddr,
					chunk[i].loadaddr + chunk[i].size - 1,
					filename,
					loadaddr,
					loadaddr + loadsize - 1);
			}
		}
	}
	c->data = malloc(loadsize);
	fread(c->data, loadsize, 1, f);
	for(i = c->loadaddr >> 8; i <= (c->loadaddr + loadsize - 1) >> 8; i++) {
		header.pageflags[i] |= pageflags;
		if(pageflags) {
			header.chunkmap[i] = header.nchunk;
		}
	}
	snprintf(c->filename, sizeof(c->filename), "%s", filename);
	c->filename[sizeof(c->filename) - 1] = 0;
	header.nchunk++;
}

void get_range(FILE *f, int tag, char *filename, int *loadsize, int flag) {
	int first, last, i;

	first = fgetc(f);
	last = fgetc(f);
	*loadsize -= 2;
	if(first < 0 || first > 255 || last < 0 || last > 255 || last < first) {
		errx(1, "Bad range in '%c' tag in %s", tag, filename);
	}
	for(i = first; i <= last; i++) header.pageflags[i] |= flag;
}

void load_efo(char *fname, int pageflags) {
	FILE *f;
	struct stat sb;
	int loadsize, tag;

	f = fopen(fname, "rb");
	if(!f) err(1, "fopen: %s", fname);

	if(fstat(fileno(f), &sb)) err(1, "fstat: %s", fname);
	loadsize = (int) sb.st_size - sizeof(header.efo);

	fread(&header.efo, sizeof(header.efo), 1, f);
	if(strncmp((char *) header.efo.magic, "EFO2", 4)) {
		errx(1, "Wrong header magic: %s", fname);
	}

	for(;;) {
		tag = fgetc(f);
		loadsize--;
		if(tag == EOF || tag == 0) break;
		switch(tag) {
			case 'P':
				get_range(f, tag, fname, &loadsize, PF_USED);
				break;
			case 'Z':
				get_range(f, tag, fname, &loadsize, PF_ZPUSED);
				break;
			case 'I':
				get_range(f, tag, fname, &loadsize, PF_INHERIT);
				break;
			case 'J':
				header.flags |= EF_JUMP;
				break;
			case 'S':
				header.flags |= EF_SAFE_IO;
				break;
			case 'U':
				header.flags |= EF_UNSAFE;
				break;
			case 'X':
				header.flags |= EF_DONT_LOAD;
				break;
			case 'A':
				header.flags |= EF_AVOID_LOAD;
				break;
			case 'M':
				header.installs_music[0] = fgetc(f);
				header.installs_music[1] = fgetc(f);
				if(!header.installs_music[0]
				&& !header.installs_music[1]) {
					header.flags |= EF_UNMUSIC;
				}
				loadsize -= 2;
				break;
			default:
				errx(1, "Invalid tag '%c' in %s", tag, fname);
		}
	}

	add_chunk(f, -1, loadsize, fname, pageflags | PF_CODE);

	fclose(f);
}

char *my_strndup(char *str, int n) {
	// Not in win32 libc, so we provide our own implementation.

	int len;
	char *buf;

	len = strlen(str);
	if(len > n) len = n;
	buf = malloc(len + 1);
	memcpy(buf, str, len);
	buf[len] = 0;

	return buf;
}

void load_extra(char *spec, int pageflags) {
	FILE *f;
	struct stat sb;
	int filesize;
	int i, j;
	char *specpart[4];
	int npart = 0;
	int loadaddr = -1, offset = 0, length = -1;

	j = 0;
	for(i = 0; spec[i]; i++) {
		if(spec[i] == ',') {
			if(npart >= 4) {
				errx(1, "Syntax error in file specification.");
			}
			specpart[npart++] = my_strndup(spec + j, i - j);
			j = i + 1;
		}
	}
	if(npart >= 4) {
		errx(1, "Syntax error in file specification.");
	}
	specpart[npart++] = my_strndup(spec + j, i - j);

	if(npart > 1 && specpart[1][0]) {
		loadaddr = parseparam(specpart[1], 0);
		if(loadaddr < 0 || loadaddr > 0xffff) {
			errx(1, "Invalid load address for \"%s\"", specpart[0]);
		}
	}

	if(npart > 2 && specpart[2][0]) {
		offset = parseparam(specpart[2], 0);
		if(offset < 0) {
			errx(1, "Invalid offset for \"%s\"", specpart[0]);
		}
	}

	if(npart > 3 && specpart[3][0]) {
		length = parseparam(specpart[3], 0);
		if(offset < 0) {
			errx(1, "Invalid length for \"%s\"", specpart[0]);
		}
	}

	f = fopen(specpart[0], "rb");
	if(!f) err(1, "fopen: %s", specpart[0]);

	if(fstat(fileno(f), &sb)) err(1, "fstat: %s", specpart[0]);
	filesize = (int) sb.st_size;

	if(offset > 0) {
		if(offset > filesize) {
			warnx(
				"Warning: File \"%s\" is shorter than the specified offset.",
				specpart[0]);
		} else {
			fseek(f, offset, SEEK_SET);
			filesize -= offset;
		}
	}

	if(length >= 0) {
		if(length > filesize) {
			warnx(
				"Warning: File \"%s\" is shorter than the specified length.",
				specpart[0]);
		} else {
			filesize = length;
		}
	}

	add_chunk(f, loadaddr, filesize, specpart[0], pageflags);

	fclose(f);
	for(i = 0; i < npart; i++) {
		free(specpart[i]);
	}
}

void save_pef(char *filename) {
	FILE *f;
	struct chunk *c;
	int i;

	f = fopen(filename, "wb");
	if(!f) err(1, "fopen: %s", filename);

	header.magic[0] = 'P';
	header.magic[1] = 'E';
	header.magic[2] = 'F';
	header.magic[3] = '3';

	fwrite(&header, sizeof(header), 1, f);
	for(i = 0; i < header.nchunk; i++) {
		c = &chunk[i];
		fputc(c->size & 255, f);
		fputc(c->size >> 8, f);
		fputc(c->loadaddr & 255, f);
		fputc(c->loadaddr >> 8, f);
		fwrite(c->filename, 32, 1, f);
		fwrite(c->data, c->size, 1, f);
	}

	fclose(f);
}

static void visualise_mem(int ppc) {
	int i, j, p, flags;
	char ch;
	struct chunk *c;

	for(p = 0, i = -1; p < 256; p += ppc) {
		if(p >> 4 != i) {
			printf("%x", p >> 4);
			i = p >> 4;
		} else printf(" ");
	}
	printf("\n");

	for(p = 0; p < 256; p += ppc) {
		flags = 0;
		for(j = 0; j < ppc; j++) {
			flags |= header.pageflags[p + j];
		}
		if(header.n_stream_chunk) {
			c = &chunk[header.nchunk - header.n_stream_chunk];
			if(c->loadaddr < (p + ppc) << 8
			&& c->loadaddr + c->size - 1 >= p << 8) {
				flags |= PF_LOADED;
			}
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
		} else {
			ch = '.';
		}
		printf("%c", ch);
	}
	printf("\n");

	for(i = 1; i < header.n_stream_chunk; i++) {
		c = &chunk[header.nchunk - header.n_stream_chunk + i];
		for(p = 0; p < 256; p += ppc) {
			ch = '.';
			for(j = 0; j < ppc; j++) {
				if(p + j >= c->loadaddr >> 8
				&& p + j <= (c->loadaddr + c->size - 1) >> 8) {
					ch = 'L';
				}
			}
			printf("%c", ch);
		}
		printf("\n");
	}
}

void usage() {
	fprintf(stderr, "%s\n\n", SPINDLE_VERSION);
	fprintf(stderr, "Usage: mkpef [options] effect.efo\n");
	fprintf(stderr, "                       [FILE ...]\n");
	fprintf(stderr, "                       [--music FILE ...]\n");
	fprintf(stderr, "                       [--stream FILE ...]\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -h --help         Display this text.\n");
	fprintf(stderr, "  -V --version      Display version information.\n");
	fprintf(stderr, "  -v --verbose      Be verbose.\n");
	fprintf(stderr, "  -w --wide         Make memory chart wider. Can be specified twice.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -o --output       Output filename. Default: effect.pef\n");
	fprintf(stderr, "     --music        These files stay resident until the next music is loaded.\n");
	fprintf(stderr, "     --stream       These files are distributed across future effect slots.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Data files (\"FILE\") can be specified in several ways:\n");
	fprintf(stderr, "  filename\n");
	fprintf(stderr, "  filename,address\n");
	fprintf(stderr, "  filename,address,offset\n");
	fprintf(stderr, "  filename,address,offset,length\n");
	fprintf(stderr, "  filename,,offset\n");
	fprintf(stderr, "  filename,,offset,length\n");
	fprintf(stderr, "  filename,,,length\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "All numbers are given in hex by default. Decimal prefix is +.\n");
	fprintf(stderr, "If no address is given, it is taken from the file (as in the .prg format). The\n");
	fprintf(stderr, "offset is applied before the address is taken (so e.g. \"music.sid,,7c\" works).\n");
	exit(1);
}

int main(int argc, char **argv) {
	int opt, i, flags;
	struct option longopts[] = {
		{"help", 0, 0, 'h'},
		{"version", 0, 0, 'V'},
		{"verbose", 0, 0, 'v'},
		{"wide", 0, 0, 'w'},
		{"output", 1, 0, 'o'},
		{0, 0, 0, 0}
	};
	char *outname = "effect.pef";
	int is_efo = 1, is_music = 0, any_music = 0;
	int verbose = 0, visualisation_width = 4;

	// A portable way of dealing with options interleaved with arguments:
	for(i = 1; i < argc; i++) {
		if(!strcmp(argv[i], "--music")) {
			argv[i] = "__MUSIC__";
		} else if(!strcmp(argv[i], "--stream")) {
			argv[i] = "__STREAM__";
		}
	}

	do {
		opt = getopt_long(argc, argv, "?hVvwo:", longopts, 0);
		switch(opt) {
			case 0:
			case '?':
			case 'h':
				usage();
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
		}
	} while(opt >= 0);

	if(argc == optind) usage();
	argc -= optind;
	argv += optind;

	for(i = 0; i < argc; i++) {
		if(!strcmp(argv[i], "__MUSIC__")) {
			if(is_stream) {
				errx(1, "Error: --music must appear before --stream.");
			}
			is_music = 1;
		} else if(!strcmp(argv[i], "__STREAM__")) {
			if(is_stream) {
				errx(1, "Error: --stream can only be used once.");
			}
			is_stream = 1;
			is_music = 0;
			stream_start = header.nchunk;
		} else {
			if(is_stream) {
				flags = 0;
			} else {
				flags = PF_LOADED | PF_USED;
				if(is_music) {
					flags |= PF_MUSIC;
					any_music = 1;
				}
			}
			if(is_efo) {
				load_efo(argv[i], flags);
				is_efo = 0;
			} else {
				load_extra(argv[i], flags);
			}
		}
	}

	if((header.installs_music[0] || header.installs_music[1]) && !any_music) {
		warnx("Warning: Effect installs a music player, but no --music file(s) specified.");
		if(header.nchunk >= 2) {
			warnx(
				"Assuming that the first additional file (\"%s\") should stay resident.",
				chunk[1].filename);
			for(i = 0; i < 256; i++) {
				if((header.pageflags[i] & PF_LOADED)
				&& (header.chunkmap[i] == 1)) {
					header.pageflags[i] |= PF_MUSIC;
				}
			}
		}
	}

	if(is_stream) {
		header.n_stream_chunk = header.nchunk - stream_start;
	}

	if(verbose) {
		visualise_mem(visualisation_width);
	}

	save_pef(outname);

	return 0;
}
