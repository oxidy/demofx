/* Spindle by lft, https://linusakesson.net/software/spindle/
 */

// Effect-wide flags.
#define EF_SAFE_IO	0x01
#define EF_DONT_LOAD	0x02
#define EF_UNSAFE	0x04
#define EF_JUMP		0x08
#define EF_UNMUSIC	0x10
#define EF_AVOID_LOAD	0x20

// Page flags.
#define PF_LOADED	0x01
#define PF_USED		0x02
#define PF_ZPUSED	0x04
#define PF_INHERIT	0x08
#define PF_MUSIC	0x10
#define PF_CODE		0x20

// Used by pefchain internally.
#define PF_RESERVED	0x100

#define MAXCHUNKS 64

struct efoheader {
	uint8_t		magic[4];
	uint8_t		v_prepare[2];
	uint8_t		v_setup[2];
	uint8_t		v_irq[2];
	uint8_t		v_main[2];
	uint8_t		v_fadeout[2];
	uint8_t		v_cleanup[2];
	uint8_t		v_jsr[2];
};

struct header {
	uint8_t			magic[4];
	uint8_t			flags;
	uint8_t			pageflags[256];
	uint8_t			chunkmap[256];
	struct efoheader	efo;
	uint8_t			installs_music[2];
	uint8_t			n_stream_chunk;
	uint8_t			reserved[21];
	uint8_t			nchunk;
};
