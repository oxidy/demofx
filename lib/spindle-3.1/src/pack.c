/* Spindle by lft, https://linusakesson.net/software/spindle/
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <err.h>

#include "disk.h"
#include "pack.h"
#include "patch-offsets.h"

// Size of driveside continuation buffer:
#define CONT_BUFFER_SIZE 0x50

struct choice {
	uint8_t		is_literal;
	uint8_t		length;
	uint16_t	offset;
	uint16_t	ncopy;
	uint32_t	cost;
};

#define IF_LITERAL	1
#define IF_FAR_CHAIN	2

struct item {
	uint8_t		flags;
	uint8_t		length;
	uint16_t	offset;
	uint16_t	address;
};

struct package {
	int		nitem;
	int		nleft;
	struct item	*item;
	uint8_t		*data;
	uint16_t	loadaddr;
	int		size;
	char		*name;

	uint8_t		*chain_cont_ptr;
	uint16_t	chain_cont_address;
};

uint8_t scramble_bits[256];

void pack_init() {
	int i;

	// Bit transmission order:
	//	Data	Clock	Ends up at
	//	/1	/3	1, 0
	//	/0	/2	3, 2
	//	/5	/4	5, 4
	//	7	/6	7, 6

	for(i = 0; i < 256; i++) {
#if 1
		scramble_bits[i] =
			((i & 0x01)? 0x00 : 0x08) |
			((i & 0x02)? 0x00 : 0x02) |
			((i & 0x04)? 0x00 : 0x04) |
			((i & 0x08)? 0x00 : 0x01) |
			((i & 0x10)? 0x00 : 0x10) |
			((i & 0x20)? 0x00 : 0x20) |
			((i & 0x40)? 0x00 : 0x40) |
			((i & 0x80)? 0x80 : 0x00);
#else
		scramble_bits[i] = i; // nice when debugging disk dumps
#endif
	}
}

static void optimal_path(struct package *pkg) {
	int pos, offset, len, i, candcost, bestcost, bestncopy;
	uint8_t *data = pkg->data;
	int datasize = pkg->size;
	struct choice choice[datasize + 1], cand;
	int lastcopy, lastcopyaddr, nitem;
	struct item *items;

	choice[datasize].is_literal = 0;
	choice[datasize].length = 0;
	choice[datasize].cost = 0;
	choice[datasize].ncopy = 0;

	for(pos = datasize - 1; pos >= 0; pos--) {
		bestcost = 0x7fffffff;
		bestncopy = 0;
		for(len = 1; len <= 64 && pos + len <= datasize; len++) {
			cand.is_literal = 1;
			cand.length = len;
			cand.ncopy = choice[pos + len].ncopy;
			candcost = choice[pos + len].cost + 8 + len * 8;
			if(bestcost > candcost
			|| (bestcost == candcost && bestncopy > cand.ncopy)) {
				bestcost = cand.cost = candcost;
				bestncopy = cand.ncopy;
				choice[pos] = cand;
			}
		}
		for(offset = 1; offset <= 1024; offset++) {
			for(len = 1; len <= 16 + 2 && pos + len <= datasize && pos + offset + len <= datasize; len++) {
				if(data[pos + len - 1] != data[pos + offset + len - 1]) {
					break;
				}
				if(len == 2 && offset <= 127) {
					// We could copy 2 bytes from this offset.
					cand.is_literal = 0;
					cand.length = len;
					cand.offset = offset;
					cand.ncopy = choice[pos + len].ncopy + 1;
					candcost = choice[pos + len].cost + 8;
					if(bestcost > candcost
					|| (bestcost == candcost && bestncopy > cand.ncopy)) {
						bestcost = cand.cost = candcost;
						bestncopy = cand.ncopy;
						choice[pos] = cand;
					}
				} else if(len >= 3) {
					// We could copy len bytes from this offset.
					cand.is_literal = 0;
					cand.length = len;
					cand.offset = offset;
					cand.ncopy = choice[pos + len].ncopy + 1;
					candcost = choice[pos + len].cost + 8 + 8;
					if(bestcost > candcost
					|| (bestcost == candcost && bestncopy > cand.ncopy)) {
						bestcost = cand.cost = candcost;
						bestncopy = cand.ncopy;
						choice[pos] = cand;
					}
				}
			}
		}
		choice[pos].cost = bestcost;
	}

	// The chain of items starting at 0 describes the optimal path.

	if(verbose >= 1) {
		int bytecost = (choice[0].cost + 7) / 8;
		fprintf(stderr, "%04x-%04x \"%s\": %d bytes crunched to %d, ratio %.02f%%.\n",
			pkg->loadaddr,
			pkg->loadaddr + datasize - 1,
			pkg->name,
			datasize,
			bytecost,
			100.0 * bytecost / datasize);
	}

	// Convert to a flat array for further processing.

	nitem = 0;
	for(pos = 0; pos < datasize; pos += choice[pos].length) {
		nitem++;
	}

	items = malloc(nitem * sizeof(struct item));

	i = 0;
	lastcopy = -1;
	lastcopyaddr = -1;
	for(pos = 0; pos < datasize; pos += choice[pos].length) {
		items[i].length = choice[pos].length;
		items[i].offset = choice[pos].offset;
		items[i].address = pos;
		if(choice[pos].is_literal) {
			items[i].flags = IF_LITERAL;
		} else {
			items[i].flags = 0;
			if(lastcopy >= 0 && pos >= lastcopyaddr + 255) {
				items[i].flags |= IF_FAR_CHAIN;
			}
			lastcopy = i;
			lastcopyaddr = pos;
		}
		i++;
	}

	pkg->nitem = nitem;
	pkg->item = items;
}

static int item_size(struct item *itm) {
	if(itm->flags & IF_LITERAL) {
		return 1 + itm->length;
	} else if(itm->length == 2) {
		return 1;
	} else {
		return 2;
	}
}

static int generate_unit(uint8_t *buffer, int buffersize, uint8_t *idxbuffer, int *idxbuffersize, struct package *pkg, int *tailptr) {
	int size, ipos, i, j, chainhead, chaintail, upos, size_left, extra_length = 0;
	struct item *itm;
	uint16_t addr, extra_addr = 0;

	size = 1 + 3; // unit header
	for(ipos = pkg->nleft - 1; ipos >= 0; ipos--) {
		itm = &pkg->item[ipos];
		j = item_size(itm);
		if(size + j > buffersize) {
			break;
		}
		size += j;
		if(itm->flags & IF_FAR_CHAIN) {
			// We have to break the unit early because
			// the copy chain would stretch too far.
			ipos--;
			break;
		}
	}
	ipos++;
	size_left = buffersize - size;
	if(ipos == pkg->nleft || size_left < 0) {
		return 0;
	}

	assert(ipos >= 0 && ipos < pkg->nleft);

	if(size_left >= 2 && ipos > 0 && pkg->item[ipos - 1].flags & IF_LITERAL) {
		assert(pkg->item[ipos - 1].length >= 2);
		extra_length = pkg->item[ipos - 1].length - 1;
		if(extra_length > size_left - 1) {
			extra_length = size_left - 1;
		}
		pkg->item[ipos - 1].length -= extra_length;
		size += 1 + extra_length;
	}

	upos = 0;
	extra_addr = pkg->item[ipos].address - extra_length;
	addr = pkg->loadaddr + extra_addr;
	buffer[upos++] = addr >> 8;
	buffer[upos++] = addr & 0xff;
	upos++; // this is where the tail link pointer will go

	if(extra_length) {
		buffer[upos++] = 0xc0 | (extra_length - 1);
		for(i = 0; i < extra_length; i++) {
			buffer[upos++] = pkg->data[extra_addr + i];
		}
	}

	chainhead = -1;
	chaintail = -1;
	for(j = ipos; j < pkg->nleft; j++) {
		itm = &pkg->item[j];
		if(itm->flags & IF_LITERAL) {
			buffer[upos++] = 0xc0 | (itm->length - 1);
			for(i = 0; i < itm->length; i++) {
				buffer[upos++] = pkg->data[itm->address + i];
			}
		} else {
			if(chainhead >= 0) {
				assert(pkg->loadaddr + itm->address - chainhead <= 255);
			}
			chainhead = pkg->loadaddr + itm->address;
			if(chaintail < 0) {
				chaintail = pkg->loadaddr + itm->address;
			}
			if(itm->length == 2) {
				buffer[upos++] = itm->offset;
			} else {
				buffer[upos++] = 0x80 | (itm->length - 3) << 2 | (itm->offset - 1) >> 8;
				buffer[upos++] = (itm->offset - 1) & 0xff;
			}
		}
	}

	if(chainhead >= 0) {
		if(pkg->chain_cont_ptr
		&& pkg->chain_cont_address - chainhead <= 255) {
			*pkg->chain_cont_ptr = scramble_bits[chainhead & 0xff];
		} else {
			if(verbose >= 4) {
				fprintf(stderr, "Chain starting at %04x.\n", chainhead);
			}
			assert(*idxbuffersize >= 3);
			idxbuffer[--*idxbuffersize] = 2;
			idxbuffer[--*idxbuffersize] = scramble_bits[chainhead & 0xff];
			idxbuffer[--*idxbuffersize] = scramble_bits[chainhead >> 8];
		}
	}

	if(chaintail >= 0) {
		buffer[2] = chaintail & 0xff;
	} else {
		buffer[2] = 0;
	}

	if(verbose >= 4) {
		uint16_t end = pkg->loadaddr + pkg->item[pkg->nleft - 1].address + pkg->item[pkg->nleft - 1].length;
		fprintf(stderr, "Unit at $%04x-$%04x (%d bytes), %d bytes on disk.\n",
			addr,
			end - 1,
			end - addr,
			(size == 256)? 255 : size);
	}

	pkg->nleft = ipos;
	*tailptr = chaintail;

	assert(upos + 1 == size);
	return upos;
}

static int remaining_size(struct package *pkg, int max, int *idxsize) {
	int size = 0;
	int chainhead;
	int j;
	struct item *itm;

	if(pkg->nleft) {
		size += 1 + 3; // unit header
		chainhead = -1;
		for(j = 0; j < pkg->nleft && size <= max; j++) {
			itm = &pkg->item[j];
			size += item_size(itm);
			if(!(itm->flags & IF_LITERAL)) {
				chainhead = pkg->loadaddr + itm->address;
			}
			if(itm->flags & IF_FAR_CHAIN) {
				// there can't be gaps in the
				// chains in this case
				size = max + 1; // give up
			}
		}
		if(chainhead >= 0) {
			if(!pkg->chain_cont_ptr
			|| pkg->chain_cont_address - chainhead > 255) {
				size += 3;
				*idxsize += 3;
			}
		}
	}

	return size;
}

static void loaderpoke(uint8_t *buffer, int *buffersize, int offset, int value, int postpone, int loaderpage) {
	if(postpone) {
		// Units up to size 4 are postponed by the drivecode.
		assert(*buffersize >= 5);
		buffer[--*buffersize] = 4;
		buffer[--*buffersize] = scramble_bits[0x01];	// 2-byte copy item, becomes operand
		buffer[--*buffersize] = scramble_bits[value];	// tail link pointer, becomes opcode
	} else {
		assert(*buffersize >= 6);
		buffer[--*buffersize] = 5;
		buffer[--*buffersize] = scramble_bits[value];
		buffer[--*buffersize] = scramble_bits[0xc0];	// literal, length 1
		buffer[--*buffersize] = scramble_bits[0x00];
	}
	buffer[--*buffersize] = scramble_bits[offset];		// lsb
	buffer[--*buffersize] = scramble_bits[loaderpage];	// msb
}

int compress_job(struct chunk *chlist, struct blockfile *bf, char jobid, int loaderpage, int entryvector) {
	uint8_t *buffer, *idxbuffer;
	int buffersize = 0, idxbuffersize = 0;
	struct chunk *ch;
	int totalin = 0, totalblocks = 0;
	int at_boundary;
	struct package *pkg, *packages;
	int npkg, nnormalpkg = 0, nshadowpkg = 0;
	int pnum, spnum, i, j, size, idxsize, upos;
	int chaintail;
	uint8_t unitbuf[256];
	int total_packed_size = 0, appx_total_sectors;
	uint8_t *data;
	uint16_t addr;
	int shadow_enabled = 0;

	for(ch = chlist; ch; ch = ch->next) {
		totalin += ch->size;
		if(ch->loadaddr < 0xe000 && ch->loadaddr + ch->size > 0xd000) {
			nshadowpkg++;
			if(ch->loadaddr < 0xd000) nnormalpkg++;
			if(ch->loadaddr + ch->size > 0xe000) nnormalpkg++;
		} else {
			nnormalpkg++;
		}
	}

	npkg = nnormalpkg + nshadowpkg;
	packages = calloc(npkg, sizeof(struct package));

	pnum = 0;
	spnum = nnormalpkg;
	for(ch = chlist; ch; ch = ch->next) {
		data = ch->data;
		size = ch->size;
		addr = ch->loadaddr;
		while(size) {
			i = size;
			if(addr < 0xd000 && addr + i > 0xd000) {
				i = 0xd000 - addr;
			} else if(addr < 0xe000 && addr + i > 0xe000) {
				i = 0xe000 - addr;
			}
			if((addr & 0xf000) == 0xd000) {
				pkg = &packages[spnum++];
			} else {
				pkg = &packages[pnum++];
			}
			pkg->data = data;
			pkg->size = i;
			pkg->loadaddr = addr;
			pkg->name = ch->name;
			optimal_path(pkg);
			pkg->nleft = pkg->nitem;
			for(j = 0; j < pkg->nitem; j++) {
				total_packed_size += item_size(&pkg->item[j]);
			}
			data += i;
			addr += i;
			size -= i;
		}
	}
	assert(pnum == nnormalpkg);
	assert(spnum == npkg);

	if(verbose >= 2 && entryvector) {
		fprintf(stderr, "Side entry at %04x\n", entryvector);
	}

	appx_total_sectors = (total_packed_size + 251) / 252;

	// Go to a new track if there's little space left, unless we're squeezing.

	disk_allocblock(
		bf,
		1,
		0,
		!squeeze_option && appx_total_sectors > 6 && disk_sectors_left_on_track(bf) < 4,
		&idxbuffer,
		&idxbuffersize,
		jobid);
	totalblocks++;
	idxbuffer++;

	pnum = 0;
	for(;;) {
		// If all remaining items fit in the index sector, store them there.

		// Current space requirements in driveside continuation buffer:
		idxsize = 255 - idxbuffersize;

		size = 0;
		if(nshadowpkg && !shadow_enabled) {
			size += 16 + 15; // Turn on and off shadow RAM.
		} else if(shadow_enabled) {
			size += 15; // Turn off shadow RAM.
		}
		if(entryvector) {
			size += 4; // Leave entrypoint in zp_dest.
		}
		idxsize += size;
		for(i = pnum; i < npkg && size <= idxbuffersize; i++) {
			size += remaining_size(&packages[i], idxbuffersize - size, &idxsize);
		}
		if(size <= idxbuffersize && idxsize <= CONT_BUFFER_SIZE) {
			if(verbose >= 4) {
				fprintf(stderr, "Remaining data fits inside index sector.\n");
			}
			upos = 0;
			for(i = pnum; i < npkg; i++) {
				if(packages[i].nleft) {
					size = generate_unit(
						unitbuf + upos,
						sizeof(unitbuf) - upos,
						idxbuffer,
						&idxbuffersize,
						&packages[i],
						&chaintail);
					assert(size);
					for(j = 0; j < size; j++) {
						unitbuf[upos] = scramble_bits[unitbuf[upos]];
						upos++;
					}
					unitbuf[upos++] = size;
				}
			}
			if(nshadowpkg) {
				// Disable shadow RAM at the end of this last batch, after the chain heads.
				loaderpoke(idxbuffer, &idxbuffersize, PATCH_OFFSET_3, 0x80, 1, loaderpage);
				loaderpoke(idxbuffer, &idxbuffersize, PATCH_OFFSET_1, 0x80, 1, loaderpage);
				loaderpoke(idxbuffer, &idxbuffersize, PATCH_OFFSET_2, 0x80, 1, loaderpage);
			}
			if(entryvector) {
				assert(idxbuffersize >= 4);
				idxbuffer[--idxbuffersize] = 3;
				idxbuffer[--idxbuffersize] = scramble_bits[0x4c]; // jmp
				idxbuffer[--idxbuffersize] = scramble_bits[entryvector & 0xff];
				idxbuffer[--idxbuffersize] = scramble_bits[entryvector >> 8];
			}
			if(nshadowpkg && !shadow_enabled) {
				// Enable shadow RAM in the final sector (unusual).
				// This is not postponed, so it happens before the above operations.
				loaderpoke(idxbuffer, &idxbuffersize, PATCH_OFFSET_3, 0xc6, 0, loaderpage);
				loaderpoke(idxbuffer, &idxbuffersize, PATCH_OFFSET_1, 0xe6, 1, loaderpage);
				loaderpoke(idxbuffer, &idxbuffersize, PATCH_OFFSET_2, 0xc6, 1, loaderpage);
			}
			assert(255 - idxbuffersize <= CONT_BUFFER_SIZE);
			for(i = 0; i < upos; i++) {
				idxbuffer[--idxbuffersize] = unitbuf[upos - i - 1];
			}
			assert(idxbuffersize >= 0);
			break; // All done.
		}

		// Otherwise, allocate a new sector.
		// Force a new boundary if too many chain heads have piled up
		// or if we only have shadow packages left but shadow RAM isn't
		// enabled yet.

		at_boundary = disk_allocblock(
			bf,
			0,
			(255 - idxbuffersize >= CONT_BUFFER_SIZE) || (pnum >= nnormalpkg && !shadow_enabled),
			0,
			&buffer,
			&buffersize,
			jobid);
		totalblocks++;

		if(at_boundary) {
			if(verbose >= 4) {
				fprintf(stderr, "New sector batch.\n");
			}

			// Boundaries can happen sporadically as we move into
			// a new track, or upon request.

			// Break all ongoing chains, so the host can work on them
			// while the drive is stepping to the new track.
			// New chain heads will be added as they are encountered.

			for(i = pnum; i < npkg; i++) {
				packages[i].chain_cont_ptr = 0;
			}

			if(nshadowpkg && !shadow_enabled) {
				// This goes into the old index sector, part
				// of the first batch. It is not postponed,
				// so we can put shadow data after it in the
				// same sector.
				loaderpoke(idxbuffer, &idxbuffersize, PATCH_OFFSET_3, 0xc6, 0, loaderpage);
				loaderpoke(idxbuffer, &idxbuffersize, PATCH_OFFSET_1, 0xe6, 1, loaderpage);
				loaderpoke(idxbuffer, &idxbuffersize, PATCH_OFFSET_2, 0xc6, 1, loaderpage);
				shadow_enabled = 1;
			}

			// There's still room in the index sector, so we can put
			// crunched data there. New heads will be added to the
			// new index sector instead.

			uint8_t *temp_buf = buffer;
			int temp_bufsize = buffersize;
			buffer = idxbuffer - 1;
			buffersize = idxbuffersize;
			idxbuffer = temp_buf + 1;
			idxbuffersize = temp_bufsize;
		}

		while(buffersize && 255 - idxbuffersize <= CONT_BUFFER_SIZE) {
			if(buffersize < 5) {
				buffersize = 0;
				break;
			}

			while(pnum < npkg && !packages[pnum].nleft) {
				pnum++;
			}
			if(pnum < nnormalpkg
			|| (pnum < npkg && shadow_enabled)) {
				pkg = &packages[pnum];
			} else {
				// We've just allocated a sector, but there's
				// no more data to store. Shouldn't happen.
				if(verbose >= 3) {
					fprintf(
						stderr,
						"Internal problem. %d bytes wasted.\n",
						buffersize);
				}
				buffersize = 0;
				break;
			}
			assert(pkg->nleft);

			// Store as many items from pkg as will fit in the buffer.
			// New chain heads are added to the index buffer.

			size = generate_unit(
				unitbuf,
				(buffersize == 255)? 256 : buffersize,
				idxbuffer,
				&idxbuffersize,
				pkg,
				&chaintail);
			assert(255 - idxbuffersize <= CONT_BUFFER_SIZE);
			if(size == 255) {
				assert(buffersize == size);
				buffer[0] |= 0x80;
				for(i = 0; i < size; i++) {
					buffer[buffersize--] = scramble_bits[unitbuf[size - i - 1]];
				}
			} else {
				assert(size < buffersize);
				if(size) {
					buffer[buffersize--] = size;
					for(i = 0; i < size; i++) {
						buffer[buffersize--] = scramble_bits[unitbuf[size - i - 1]];
					}
				}
			}
			if(size) {
				if(chaintail >= 0) {
					pkg->chain_cont_ptr = &buffer[buffersize + 2 + 1];
					pkg->chain_cont_address = chaintail;
				} else {
					pkg->chain_cont_ptr = 0;
				}
			} else {
				buffersize = 0;
			}
		}
	}

	if(verbose >= 3) {
		fprintf(stderr, "%d bytes left in tail sector, and %d bytes in index sector.\n", buffersize, idxbuffersize);
	}

	if(verbose >= 2) {
		fprintf(
			stderr,
			"%d bytes crunched into %d blocks, "
			"effective compression ratio %d%%.\n",
			totalin,
			totalblocks,
			totalblocks * 100 / ((totalin + 253) / 254));
	}

	return totalblocks;
}
