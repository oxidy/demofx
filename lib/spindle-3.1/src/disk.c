/* Spindle by lft, https://linusakesson.net/software/spindle/
 */

#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "disk.h"
#include "util.h"

#include "datatables.h"

#define MAXTRACK 40

#define IDEAL_BATCH 8

static int ntrack, nsector;

static int tracksize[MAXTRACK] = {
	21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
	19, 19, 19, 19, 19, 19, 19,
	18, 18, 18, 18, 18, 18,
	17, 17, 17, 17, 17,
	17, 17, 17, 17, 17
};

static int trackstart[MAXTRACK];

static uint8_t available[MAXTRACK][21];
static uint8_t *image;

static char jobmap[MAXTRACK][21];

static uint8_t gcrdecode[256];

static uint8_t seektable[64][2];

#define SECTOR(tr, se) (&image[(trackstart[(tr) - 1] + (se)) * 256])

extern uint8_t scramble_bits[];

static void putgcr(uint8_t pos, uint8_t val) {
	assert(gcrdecode[pos] == 0x99 || gcrdecode[pos] == (val ^ 0x10));
	gcrdecode[pos] = val;
}

void disk_init(char *name, char *id, int fortytracks) {
	int i, pos = 0;
	uint8_t *bam;
	const static uint8_t encode[16] = {
		0x0a, 0x0b, 0x12, 0x13, 0x0e, 0x0f, 0x16, 0x17,
		0x09, 0x19, 0x1a, 0x1b, 0x0d, 0x1d, 0x1e, 0x15
	};

	ntrack = fortytracks? 40 : 35;
	for(i = 0; i < ntrack; i++) {
		trackstart[i] = pos;
		pos += tracksize[i];
	}
	nsector = pos;
	image = calloc(nsector, 256);

	bam = SECTOR(18, 0);
	bam[0] = 18;	// First dir block
	bam[1] = 1;
	bam[2] = 0x41;	// 1541

	memset(bam + 144, 0xa0, 27);
	for(i = 0; name[i]; i++) bam[144 + i] = ascii_to_petscii(name[i]);
	bam[162] = id[0];
	bam[163] = id[1];
	bam[165] = 0x32;
	bam[166] = 0x41;

	for(i = 0; i < ntrack; i++) {
		memset(available[i], 1, tracksize[i]);
	}

	memset(jobmap, '.', sizeof(jobmap));

	memset(gcrdecode, 0x99, 256);
	for(i = 0; i < 16; i++) {
		// aaaaa000 at offset 00
		putgcr(encode[i] << 3, i << 4);

		// bb001bbb at offset 00
		putgcr(encode[i] >> 2 | encode[i] << 6 | 8, i);

		// 000ccccc at offset 00
		putgcr(encode[i], i << 4);

		// ddddd000 at offset 01
		putgcr((encode[i] << 3) + 1, i);

		// 0000eeee at offset 79
		putgcr((encode[i] >> 1) + 0x79, (i ^ 1) << 4);

		// 0efffff0 at offset 40
		putgcr((encode[i] << 1) + 0x40, i);
		putgcr((encode[i] << 1) + 0x80, i | 0x10);

		// ggg000gg at offset 00
		putgcr(encode[i] >> 3 | encode[i] << 5, i << 4);

		// 000hhhhh at offset 20
		putgcr(encode[i] + 0x20, i);
	}

	// 0000eeee special case:
	putgcr(0x0f + 0x79, 0xe0);

	// zone bitrates
	putgcr(0x44 + 0, 0x6c);
	putgcr(0x44 + 1, 0x4c);
	putgcr(0x44 + 2, 0x2c);
	putgcr(0x44 + 3, 0x0c);

	// zone track lengths
	putgcr(0xc4 + 0, 21);
	putgcr(0xc4 + 1, 19);
	putgcr(0xc4 + 2, 18);
	putgcr(0xc4 + 3, 17);

	// zone branch offsets
	putgcr(0xbd + 0, data_drivecode[0x300 + 7]);	// without nop
	putgcr(0xbd + 1, data_drivecode[0x300 + 7]);	// without nop
	putgcr(0xbd + 2, data_drivecode[0x300 + 7]);	// without nop
	putgcr(0xbd + 3, data_drivecode[0x300 + 6]);	// with nop

	// bit-scrambling table
	putgcr(0x1f + 0x0, 0x7f);
	putgcr(0x1f + 0x1, 0x76);
	putgcr(0x1f + 0x8, 0x76);
	putgcr(0x1f + 0x9, 0x7f);

	// sector number
	putgcr(0, 3);
}

static int take_next_free(int *track, int *sector, char jobid, int force_track) {
	int tr = *track, se = *sector, count;
	int newtrack = 0;

	count = tracksize[tr - 1];

	for(;;) {
		if(!count-- || force_track) {
			newtrack = 1;
			force_track = 0;
			tr++;
			// The loader skips track 18, so we do that too.
			if(tr == 18) tr++;
			if(tr > ntrack) {
				errx(1, squeeze_option? "Disk full." : "Disk full. The --squeeze option might help.");
			}
			se %= tracksize[tr - 1];
			count = tracksize[tr - 1];
		}
		if(available[tr - 1][se]) {
			break;
		}
		se = (se + 1) % tracksize[tr - 1];
	}

	available[tr - 1][se] = 0;
	jobmap[tr - 1][se] = jobid;

	*track = tr;
	*sector = se;
	return newtrack;
}

int disk_sectors_left_on_track(struct blockfile *bf) {
	int tr = bf->currtr - 1;
	int n = 0, i;

	for(i = 0; i < tracksize[tr]; i++) {
		n += available[tr][i];
	}

	return n;
}

void disk_set_seekpoint(struct blockfile *bf, int slot) {
	seektable[slot][0] = bf->nextptr_t;
	seektable[slot][1] = bf->nextptr_s;
}

int disk_allocblock(
	struct blockfile *bf,
	int newjob,
	int force_boundary,
	int force_track,
	uint8_t **dataptr,
	int *datasize,
	char jobid)
{
	int newtrack, newbatch;
	int interleave;

	switch(bf->interleavestate) {
	case 0:
	case 2:
		interleave = tracksize[bf->currtr - 1] / 2;
		break;
	case 1:
		interleave = (tracksize[bf->currtr - 1] + 3) / 4;
		break;
	default:
		interleave = 4;
		break;
	}
	bf->interleavestate++;
	bf->currse = (bf->currse + interleave) % tracksize[bf->currtr - 1];

	newtrack = take_next_free(&bf->currtr, &bf->currse, jobid, force_track);
	*dataptr = SECTOR(bf->currtr, bf->currse);

	newbatch = newtrack || newjob || force_boundary;

	if(newbatch) {
		if(!bf->nextptr) errx(1, "Blank job not allowed!");
		bf->chainptr = bf->nextptr;
		bf->nextptr = 0;
		bf->interleavestate = 0;
	}
	if(newtrack) {
		bf->chainptr[0] |= 0x40;
	}
	if(newjob) {
		bf->chainptr[0] |= 0x80;
	}
	if(bf->nextptr) {
		*datasize = 255;
	} else {
		bf->nextptr_t = bf->currtr;
		bf->nextptr_s = bf->currse;
		bf->nextptr = &SECTOR(bf->currtr, bf->currse)[253];
		memset(bf->nextptr, 0, 3);
		(*dataptr)[0] |= 0x40;
		*datasize = 252;
	}
	if(bf->currse < 5) {
		bf->chainptr[0] |= 0x10 >> bf->currse;
	} else if(bf->currse < 13) {
		bf->chainptr[1] |= 0x80 >> (bf->currse - 5);
	} else {
		bf->chainptr[2] |= 0x80 >> (bf->currse - 13);
	}

	if(verbose >= 5) {
		fprintf(
			stderr,
			"Allocated sector %d:%d, file offset $%05x00.\n",
			bf->currtr,
			bf->currse,
			trackstart[bf->currtr - 1] + bf->currse);
	}

	return newbatch;
}

static void reorder_sector(uint8_t *data) {
	uint8_t copy[256];
	int i;

	memcpy(copy, data, 256);
	for(i = 0; i < 256; i++) {
		data[(-i) & 0xff] = copy[i];
	}
}

static void pad_track(int tr) {
	int s, s2, n, i, flag;

	n = 0;
	for(s = 0; s < tracksize[tr]; s++) {
		if(!available[tr][s]) {
			n++;
		}
	}
	if(n && n < tracksize[tr]) {
		do {
			flag = 0;
			// n and track size must be relatively prime
			if(tracksize[tr] == 18) {
				n |= 1;
				if(n == 3 || n == 9) n += 2;
			} else if(tracksize[tr] == 21) {
				if(n == 3 || n == 7) n++;
			}
			for(s = 0; s < tracksize[tr]; s++) {
				if(available[tr][s]) {
					flag = 1;
				} else {
					for(i = 1; i < tracksize[tr]; i++) {
						s2 = (s + i * n) % tracksize[tr];
						if(available[tr][s2]) {
							memcpy(
								SECTOR(tr + 1, s2),
								SECTOR(tr + 1, s),
								256);
							available[tr][s2] = 0;
							jobmap[tr][s2] = '+';
							break;
						}
					}
				}
			}
		} while(flag);
	}
}

void disk_closeside(struct blockfile *bf, int last) {
	int tr, se;
	int i;

	if(!bf->nextptr) errx(1, "Blank job not allowed!");
	bf->nextptr[0] = 0xa0;
	bf->nextptr[1] = 0x80; // sector 5 (on track 18)
	bf->nextptr[2] = 0x00;

	// clear the new-job flag of the first continuation record
	SECTOR(18, 17)[253] &= 0x7f;

	// install the seek table
	for(i = 0; i < 64; i++) {
		if(seektable[i][0]) {
			SECTOR(18, 6)[0x80 + i] = seektable[i][0] * 2;
			SECTOR(18, 6)[0xc0 + i] = seektable[i][1];
		} else {
			SECTOR(18, 6)[0x80 + i] = bf->nextptr_t * 2;
			SECTOR(18, 6)[0xc0 + i] = bf->nextptr_s;
		}
	}
	reorder_sector(SECTOR(18, 6));

	for(tr = 0; tr < ntrack; tr++) {
		if(tr != 18 - 1) {
			for(se = 0; se < tracksize[tr]; se++) {
				SECTOR(tr + 1, se)[0] |= se;
			}
			pad_track(tr);
		}
	}

	if(verbose >= 2) {
		for(tr = ntrack; tr >= 1; tr--) {
			fprintf(stderr, "Track %2d: ", tr);
			for(se = 0; se < tracksize[tr - 1]; se++) {
				fprintf(stderr, "%c", jobmap[tr - 1][se]);
			}
			fprintf(stderr, "\n");
		}
	}
}

/*
Track 18 layout:

	 0	bam
	 1	directory (block 1)
	 2	drivecode (block 2, misc, loaded by DOS)
	 3	gcr decoding table (loaded by DOS)
	 4	directory (block 2, optional)
	 5	drivecode for disk flip / end (loaded on demand)
	 6	drivecode and data for seek (loaded on demand)
	 7	directory (block 3, optional)
	 8	stage 1 (block 1)
	 9	stage 1 (block 3)
	 10	directory (block 4, optional)
	 11	drivecode (block 3, fetch, loaded by DOS)
	 12	drivecode (block 1, init, loaded by DOS via M-E)
	 13	directory (block 5, optional)
	 14
	 15	stage 1 (block 4, if needed)
	 16	directory (block 6, optional)
	 17	drivecode (block 4, communicate, loaded by drivecode)
	 18	stage 1 (block 2)

Reserved values in the first byte of each block (check opcodes):
	x5
	x6
	11, 31, 51, 71, 91, b1, d1, f1
	1x, 3x, 5x, 7x, 9x, bx, dx, fx where x is [7..f]
BAM, dir, stage1 sectors will have 12 or 00.
*/

void disk_storeloader(
	struct blockfile *bf,
	uint8_t *dirart,
	int active_dentry,
	uint32_t my_magic,
	uint32_t next_magic,
	int err_prob,
	uint16_t jumpaddr,
	uint8_t residentpage,
	uint8_t bufferpage,
	uint8_t zpreloc)
{
	uint8_t *dir = SECTOR(18, 1), *dentry;
	int i, j, e, last = 0;
	uint8_t *stage1, *drivecode;
	int stage1size;

	stage1size = sizeof(data_stage1);
	assert(stage1size == sizeof(data_stage1reloc));
	if(err_prob) {
		stage1size += sizeof(data_eflagwarning);
	}
	stage1 = malloc(stage1size);
	for(i = 0; i < sizeof(data_stage1); i++) {
		j = data_stage1[i];
		if(data_stage1[i] != data_stage1reloc[i]) {
			switch(data_stage1[i]) {
			case 0x02:
				j = residentpage;
				break;
			case 0x03:
				j = residentpage + 1;
				break;
			case 0x07:
				j = bufferpage;
				break;
			case 0x08:
				j = (jumpaddr - 1) >> 8;
				break;
			case 0x01:
				j = (jumpaddr - 1) & 0xff;
				break;
			default:
				if(data_stage1[i] >= 0xe0) {
					j = data_stage1[i] - 0xf4 + zpreloc;
				} else {
					errx(1, "Internal relocation error.");
				}
			}
		}
		stage1[i] = j;
	}

	if(err_prob) {
		memcpy(stage1 + sizeof(data_stage1), data_eflagwarning, sizeof(data_eflagwarning));
		// modify SYS target:
		sprintf((char *) stage1 + 7, "%d", (int) (0x801 + sizeof(data_stage1) - 2));
		drivecode = data_drivecodeerr;
	} else {
		drivecode = data_drivecode;
	}

	available[17][0] = 0;
	jobmap[17][0] = 'd';

	available[17][2] = 0;
	available[17][3] = 0;
	available[17][5] = 0;
	available[17][6] = 0;
	available[17][11] = 0;
	available[17][12] = 0;
	available[17][17] = 0;
	jobmap[17][2] = 's';
	jobmap[17][3] = 's';
	jobmap[17][5] = 's';
	jobmap[17][6] = 's';
	jobmap[17][11] = 's';
	jobmap[17][12] = 's';
	jobmap[17][17] = 's';

	available[17][8] = 0;
	available[17][18] = 0;
	available[17][9] = 0;
	jobmap[17][8] = 'd';
	jobmap[17][18] = 'd';
	jobmap[17][9] = 'd';
	if(stage1size > 254 * 3) {
		available[17][15] = 0;
		jobmap[17][15] = 'd';
	}

	for(i = 0; i < DIRARTBLOCKS * 8; i++) {
		if(dirart[i * 16] != 0xa0) last = i;
	}
	if(active_dentry > last) active_dentry = last;

	for(i = 0; i < DIRARTBLOCKS; i++) {
		available[17][1 + 3 * i] = 0;
		jobmap[17][1 + 3 * i] = 'd';
		dir = SECTOR(18, 1 + 3 * i);
		memset(dir, 0, 256);
		for(j = 0; j < 8; j++) {
			dentry = dir + j * 32 + 2;
			e = i * 8 + j;
			if(e <= last) {
				if(e == active_dentry) {
					dentry[0] = 0x82;
					dentry[1] = 18;
					dentry[2] = 8;
					dentry[28] = (stage1size + 253) / 254;
				} else {
					dentry[0] = 0x80;
				}
				memcpy(dentry + 3, dirart + e * 16, 16);
			}
		}
		if(last >= (i + 1) * 8) {
			dir[0] = 18;
			dir[1] = 1 + 3 * (i + 1);
		} else {
			dir[0] = 0;
			dir[1] = 0xff;
			break;
		}
	}

	SECTOR(18, 8)[0] = 18;
	SECTOR(18, 8)[1] = 18;
	memcpy(SECTOR(18, 8) + 2, stage1, 254);
	SECTOR(18, 18)[0] = 18;
	SECTOR(18, 18)[1] = 9;
	memcpy(SECTOR(18, 18) + 2, stage1 + 254, 254);
	if(stage1size > 254 * 3) {
		SECTOR(18, 9)[0] = 18;
		SECTOR(18, 9)[1] = 15;
		memcpy(SECTOR(18, 9) + 2, stage1 + 2 * 254, 254);
		SECTOR(18, 15)[0] = 0;
		SECTOR(18, 15)[1] = stage1size - 3 * 254 + 1;
		memcpy(
			SECTOR(18, 15) + 2,
			stage1 + 3 * 254,
			stage1size - 3 * 254);
	} else {
		SECTOR(18, 9)[0] = 0;
		SECTOR(18, 9)[1] = stage1size - 2 * 254 + 1;
		memcpy(
			SECTOR(18, 9) + 2,
			stage1 + 2 * 254,
			stage1size - 2 * 254);
	}

	memcpy(SECTOR(18, 3), gcrdecode, 256);

	memcpy(SECTOR(18, 12), drivecode + 0x000, 256);
	memcpy(SECTOR(18, 2), drivecode + 0x100, 256);
	memcpy(SECTOR(18, 11), drivecode + 0x200, 256);
	memcpy(SECTOR(18, 17), drivecode + 0x300, 256);
	memcpy(SECTOR(18, 5), drivecode + 0x400, 256);
	memcpy(SECTOR(18, 6), drivecode + 0x500, 256);

	// Patch msb of dummy data unit for flip:
	SECTOR(18, 5)[0xfb] = scramble_bits[bufferpage];

	SECTOR(18, 5)[0xff] = (next_magic >> 16) & 0xff;
	SECTOR(18, 5)[0xfe] = (next_magic >> 8) & 0xff;
	SECTOR(18, 5)[0xfd] = (next_magic >> 0) & 0xff;

	reorder_sector(SECTOR(18, 17));
	reorder_sector(SECTOR(18, 5));

	SECTOR(18, 17)[0xf8] = err_prob * 256 / 100;
	SECTOR(18, 17)[0xf9] = (my_magic >> 16) & 0xff;
	SECTOR(18, 17)[0xfa] = (my_magic >> 8) & 0xff;
	SECTOR(18, 17)[0xfb] = (my_magic >> 0) & 0xff;

	memset(bf, 0, sizeof(*bf));
	bf->currtr = 1;
	bf->currse = 0;
	bf->nextptr = &SECTOR(18, 17)[253];
	bf->nextptr_t = 18;
	bf->nextptr_s = 17;
}

void disk_write(char *fname) {
	FILE *f;
	int t, s, nfree = 0, nplus = 0;
	uint8_t bits[4];

	for(t = 0; t < ntrack; t++) {
		memset(bits, 0, sizeof(bits));
		for(s = 0; s < tracksize[t]; s++) {
			if(available[t][s]) {
				bits[0]++;
				bits[1 + s / 8] |= 1 << (s & 7);
				if(t != 17) nfree++;
			} else {
				if(jobmap[t][s] == '+') nplus++;
			}
		}
		if(t < 35) {
			memcpy(&SECTOR(18, 0)[0x04 + 4 * t], bits, 4);
		} else {
			memcpy(&SECTOR(18, 0)[0xc0 + 4 * (t - 35)], bits, 4);
		}
	}

	f = fopen(fname, "wb");
	if(!f) err(1, "%s", fname);
	if(nsector != fwrite(image, 256, nsector, f)) {
		errx(1, "%s: write error", fname);
	}
	fclose(f);

	fprintf(stderr, "%s: %d blocks free (%d for DOS).\n", fname, nfree + nplus, nfree);
}
