/* Spindle by lft, https://linusakesson.net/software/spindle/
 */

struct chunk {
	struct chunk		*next;
	uint16_t		loadaddr;
	uint16_t		size;
	uint8_t			*data;
	char			name[64];
};

void pack_init();
int compress_job(struct chunk *ch, struct blockfile *bf, char jobid, int loaderpage, int entryvector);
