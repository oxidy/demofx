/* Spindle by lft, https://linusakesson.net/software/spindle/
 */

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "disk.h"

long parseparam(char *str, char **end) {
	int hex = 1, digit;
	long accum = 0;
	char ch;

	while(*str == ' ' || *str == '\t') str++;

	if(str[0] == '0' && str[1] == 'x') {
		str += 2;
	} else if(*str == '+') {
		hex = 0;
		str++;
	} else if(*str == '$') {
		str++;
	}

	for(;;) {
		ch = *str;
		if(ch >= '0' && ch <= '9') {
			digit = ch - '0';
		} else if(hex && ch >= 'a' && ch <= 'f') {
			digit = ch - 'a' + 10;
		} else if(hex && ch >= 'A' && ch <= 'F') {
			digit = ch - 'A' + 10;
		} else {
			break;
		}
		accum *= hex? 16 : 10;
		accum += digit;
		str++;
	}

	if(end) *end = str;
	return accum;
}

uint8_t ascii_to_petscii(uint8_t ch) {
	if(ch & 0x80) {
		return '?';
	} else if(ch >= 0x60) {
		return 0xc0 | (ch & 0x1f);
	} else {
		return ch;
	}
}

static uint8_t screen_to_petscii(uint8_t scr) {
	static int did_warn;

	if(scr & 0x80) {
		if(!did_warn) {
			warnx("Warning: Inverted screen codes are not allowed in directory art.");
			did_warn = 1;
		}
		scr &= 0x7f;
	}

	switch(scr & 0xe0) {
		case 0x00:
			return 0x40 | (scr & 0x1f);
		case 0x20:
			return scr;
		case 0x40:
			return 0xc0 | (scr & 0x1f);
		default:
			return 0xa0 | (scr & 0x1f);
	}
}

static int sector18offset(int sector) {
	return (17 * 21 + sector) * 256;
}

void make_dirart(uint8_t *dirart, char *fname) {
	int i, n = 0, ch, any, trk, sector;
	FILE *f;
	char buf[1024];

	memset(dirart, 0xa0, DIRARTBLOCKS * 8 * 16);
	if(fname) {
		f = fopen(fname, "rb");
		if(!f) err(1, "%s", fname);
		fseek(f, sector18offset(0), SEEK_SET);
		if(ftell(f) == sector18offset(0) && fgetc(f) == 18) {
			sector = fgetc(f);
			do {
				fseek(f, sector18offset(sector), SEEK_SET);
				trk = fgetc(f);
				sector = fgetc(f);
				for(i = 0; i < 8; i++) {
					if(i) {
						(void) fgetc(f);
						(void) fgetc(f);
					}
					fread(buf, 30, 1, f);
					if(buf[0]) {
						memcpy(dirart + n * 16, buf + 3, 16);
						n++;
					}
				}
			} while(trk == 18 && n < DIRARTBLOCKS * 8);
		} else {
			fseek(f, 0, SEEK_SET);
			ch = fgetc(f);
			if(ch == 0) {
				// assume this is the load address
				// of a dumped video matrix
				(void) fgetc(f);
				if(fread(buf, 1, 1000, f) != 1000) {
					errx(1, "%s: Failed to detect directory art format.", fname);
				}
				for(n = 0; n < 25; n++) {
					any = 0;
					for(i = 0; i < 16; i++) {
						any |= buf[n * 40 + i] ^ 0x20;
						dirart[n * 16 + i] = screen_to_petscii(buf[n * 40 + i]);
					}
					if(!any) {
						memset(&dirart[n * 16], 0xa0, 16);
					}
				}
			} else {
				i = 0;
				while(ch != EOF) {
					if(ch == '\r') {
						i = 0;
						n++;
						ch = fgetc(f);
						if(ch == '\n') {
							ch = fgetc(f);
							continue;
						}
					} else if(ch == '\n') {
						i = 0;
						n++;
						ch = fgetc(f);
						continue;
					}
					if(n >= DIRARTBLOCKS * 8) {
						errx(1, "Too many directory art lines.");
					}
					if(i < 16) {
						dirart[n * 16 + i] = ascii_to_petscii(ch);
					}
					i++;
					ch = fgetc(f);
				}
			}
		}
		fclose(f);
	} else {
		dirart[0] = 'D';
		dirart[1] = 'E';
		dirart[2] = 'M';
		dirart[3] = 'O';
	}
}
