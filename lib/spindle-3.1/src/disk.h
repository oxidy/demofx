/* Spindle by lft, https://linusakesson.net/software/spindle/
 */

extern int verbose;
extern int squeeze_option;

#define DIRARTBLOCKS 6

struct blockfile {
	int		currtr;
	int		currse;
	int		interleavestate;
	uint8_t		*chainptr;
	uint8_t		*nextptr;
	uint8_t		nextptr_t, nextptr_s;
};

void disk_init(char *name, char *id, int fortytracks);
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
	uint8_t zpreloc);
void disk_set_seekpoint(struct blockfile *bf, int slot);
int disk_allocblock(
	struct blockfile *bf,
	int newjob,
	int force_boundary,
	int force_track,
	uint8_t **dataptr,
	int *datasize,
	char jobid);
int disk_sectors_left_on_track(struct blockfile *bf);
void disk_closeside(struct blockfile *bf, int last);
void disk_write(char *filename);
