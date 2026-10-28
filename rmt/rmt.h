#ifndef __RMT_H__
#define __RMT_H__

/* RMT player on the C64 with the VERA PSG (rmtplayr.s or lzsplayr.s,
 * rmtirq.s, psgrmt.s): the Atari RMT routine computes the POKEY registers
 * (in RAM, the C64 has no POKEY) and psgrmt.s plays them on the VERA PSG. */

/* RMT module linked in the program (tools/rmt2ca65.py or tools/sap2ca65.py) */
extern const unsigned char rmt_song[];

/* Init the player on a module, only while the IRQ is off; returns the
 * instrument speed (player calls per frame, 1..4) */
unsigned char __fastcall__ rmt_init(const void *module);

/* Install / remove the player on a raster IRQ chained to the KERNAL one
 * ($0314): once per frame (50 Hz PAL, 60 Hz NTSC), N times with instrument
 * speed N. rmt_irq_off silences the PSG and keeps the song position. */
void rmt_irq_on(void);
void rmt_irq_off(void);

extern volatile unsigned int rmt_frames;     /* frames played */
extern volatile unsigned char rmt_lines;     /* raster lines of the last frame's music */
extern volatile unsigned char rmt_maxlines;  /* the worst of them */
extern unsigned char rmt_ispeed;             /* player calls per frame */
extern unsigned char rmt_ntsc;               /* 1 = NTSC C64 (60 Hz) */
extern unsigned int rmt_frame_lines;         /* raster lines of a frame: 312, 263, 262 */
extern volatile unsigned char rmt_audc[];    /* AUDC values computed by the player */
extern volatile unsigned char rmt_pokey[16]; /* the POKEY registers (RAM) */

/* RMT_TRACKS = 4 (RMT4, mono: PSG voices 0-3) or 8 (RMT8, stereo for two
 * POKEYs: PSG voices 0-7) */
#ifndef RMT_TRACKS
#define RMT_TRACKS 4
#endif
extern const unsigned char rmt_tracks;                /* tracks the player was built for */
void psg_init(void);                                  /* PAL/NTSC constants, voices silent */
void psg_silence(void);                               /* PSG voices at volume 0 */
extern volatile unsigned char psg_enable;             /* 1 = voices follow the player */
extern volatile unsigned char psg_stereo;             /* 1 = stereo pan */
extern volatile unsigned char psg_volume[RMT_TRACKS]; /* VERA level of the voices (0..63) */

#endif
