
/* arch-tag: mp3 bitrate parsing implementation */
#include "config.h"

#include <glib.h>
#include "mp3bitrate.h"

#undef LOG
#ifdef LOG
#define lprintf(x...) g_print(x)
#else
#define lprintf(x...)
#endif

/* bitrate table tabsel_123[mpeg version][layer][bitrate index]
 * values stored in kbps
 */
const int tabsel_123[2][3][16] = {
  { {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,},
    {0,32,48,56, 64, 80, 96,112,128,160,192,224,256,320,384,},
    {0,32,40,48, 56, 64, 80, 96,112,128,160,192,224,256,320,} },

  { {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,},
    {0, 8,16,24,32,40,48, 56, 64, 80, 96,112,128,144,160,},
    {0, 8,16,24,32,40,48, 56, 64, 80, 96,112,128,144,160,} }
};
                                                                                
static int frequencies[3][3] = {
  { 44100, 48000, 32000 },
  { 22050, 24000, 16000 },
  { 11025, 12000,  8000 }
};

typedef struct
{
  int mpeg25_bit;
  int layer;
  int channel_mode;
  int lsf_bit;
} MP3Frame;

#ifndef WORDS_BIGENDIAN
#define BE_32(x) ((((guint8*)(x))[0] << 24) | \
		(((guint8*)(x))[1] << 16) | \
		(((guint8*)(x))[2] << 8) | \
		((guint8*)(x))[3])
#else
#define BE_32(x) (*(guint32 *) (x))
#endif

#define FOURCC_TAG( ch0, ch1, ch2, ch3 )		\
	( (long)(unsigned char)(ch3) |			\
	  ( (long)(unsigned char)(ch2) << 8 ) |		\
	  ( (long)(unsigned char)(ch1) << 16 ) |	\
	  ( (long)(unsigned char)(ch0) << 24 ) )

#define XING_TAG             FOURCC_TAG('X', 'i', 'n', 'g')
#define XING_FRAMES_FLAG     0x0001
#define XING_BYTES_FLAG      0x0002
#define XING_TOC_FLAG        0x0004
#define XING_VBR_SCALE_FLAG  0x0008

/* check for valid "Xing" VBR header */
static int is_xhead(unsigned char *buf)
{
  return (BE_32(buf) == XING_TAG);
}

static int mpg123_parse_xing_header(MP3Frame *frame,
		int samplerate,
		guint8 *buf, int bufsize, int *bitrate,
		int *time)
{
  int i;
  guint8 *ptr = buf;
  double frame_duration;
  int xflags, xframes, xbytes, xvbr_scale;
  int abr;
  guint8 xtoc[100];

  xframes = xbytes = 0;

  /* offset of the Xing header */
  if ( frame->lsf_bit )
  {
    if( frame->channel_mode != 3 )
      ptr += (32 + 4);
    else
      ptr += (17 + 4);
  } else {
    if( frame->channel_mode != 3 )
      ptr += (17 + 4);
    else
      ptr += (9 + 4);
  }

  if (ptr >= (buf + bufsize))
    return 0;

  if (is_xhead(ptr))
  {
    lprintf("Xing header found\n");

    ptr += 4; if (ptr >= (buf + bufsize)) return 0;

    xflags = BE_32(ptr);
    ptr += 4; if (ptr >= (buf + bufsize)) return 0;

    if (xflags & XING_FRAMES_FLAG)
    {
      xframes = BE_32(ptr);
      lprintf("xframes: %d\n", xframes);
      ptr += 4; if (ptr >= (buf + bufsize)) return 0;
    }
    if (xflags & XING_BYTES_FLAG)
    {
      xbytes = BE_32(ptr);
      lprintf("xbytes: %d\n", xbytes);
      ptr += 4; if (ptr >= (buf + bufsize)) return 0;
    }
    if (xflags & XING_TOC_FLAG)
    {
      lprintf("toc found\n");
      for (i = 0; i < 100; i++)
      {
        xtoc[i] = *(ptr + i);
	lprintf("%d ", xtoc[i]);
      }
      lprintf("\n");
    }
    ptr += 100; if (ptr >= (buf + bufsize)) return 0;
    xvbr_scale = -1;
    if (xflags & XING_VBR_SCALE_FLAG) {
      xvbr_scale = BE_32(ptr);
      lprintf("xvbr_scale: %d\n", xvbr_scale);
    }

    /* 1 kbit = 1000 bits ! (and not 1024 bits) */
    if (xflags & (XING_FRAMES_FLAG | XING_BYTES_FLAG)) {
      if (frame->layer == 1) {
        frame_duration = 384.0 / (double)samplerate;
      } else {
        int slots_per_frame;
	slots_per_frame = (frame->layer == 3 && !frame->lsf_bit) ? 72 : 144;
	frame_duration = slots_per_frame * 8.0 / (double)samplerate;
      }
      abr = ((double)xbytes * 8.0) / ((double)xframes * frame_duration);
      lprintf("abr: %d bps\n", abr);
      *bitrate = abr;
      *time = (double)xframes * frame_duration;
      lprintf("stream_length: %d s, %d min %d s\n", *time,
		      *time / 60, *time % 60);
    } else {
      /* it's a stupid Xing header */
      lprintf ("not a Xing VBR file\n");
    }
    return 1;
  } else {
    lprintf("Xing header not found\n");
    return 0;
  }
}

/*
 * Returns 1 if the header was parsed successfully, 0 if it failed
 *
 * bitrate: self-explanatory
 * samplerate: ditto
 * time: only informed if we have a VBR file with Xing headers, needs to be
 *       deduced from the bitrate and filesize otherwise
 * version: 1 for MPEG Version 1, 2 for MPEG Version 2, and
 *          3 for MPEG Version 2.5
 * vbr: whether it is a variable bitrate stream
 */
int
mp3_bitrate_parse_header (guchar *buffer, guint length_read, int *bitrate, int *samplerate, int *time, int *version, int *vbr, int *channels)
{
    guint32 head;
    int i = 0;
    MP3Frame frame;
    int bitrate_idx, version_idx, freq_idx, frame_sync;

    head = BE_32(buffer);
    lprintf ("buffer2: %08X\n", head);

    frame_sync = head >> 21;
    if (frame_sync != 0x7ff)
    {
      lprintf ("invalid frame sync\n");
      return 0;
    }
    /* Magic to detect MPEG version 2.5 */
    frame.mpeg25_bit = (head >> 20) & 0x1;
    frame.lsf_bit = (head >> 19) & 0x1;
    if (!frame.mpeg25_bit)
    {
      if (frame.lsf_bit)
      {
        lprintf("reserved mpeg25 lsf combination\n");
	return 0;
      } else
        version_idx = 2; /* MPEG Version 2.5 */
    } else {
      if (!frame.lsf_bit)
        version_idx = 1; /* MPEG Version 2 */
      else
        version_idx = 0; /* MPEG Version 1 */
    }
    lprintf ("version_idx %d\n", version_idx);
    *version = version_idx + 1;

    frame.layer = 4 - ((head >> 17) & 0x3);
    if (frame.layer == 4)
    {
      lprintf("reserved layer\n");
      return 0;
    }
    lprintf ("layer %d\n", frame.layer);

    bitrate_idx = (head >> 12) & 0xf;
    if ((bitrate_idx == 0) || (bitrate_idx == 15))
    {
      lprintf("invalid bitrate index\n");
      return 0;
    }
    lprintf ("bitrate_idx %d\n", bitrate_idx);

    freq_idx = (head >> 10) & 0x3;
    if (freq_idx == 3) {
      lprintf("invalid frequence index\n");
      return 0;
    }
    lprintf ("freq_idx %d\n", freq_idx);

    /* 0: Stereo
     * 1: Joint Stereo
     * 2: Dual Stereo
     * 3: Mono */
    frame.channel_mode = (head >>  6) & 0x3;

    *bitrate = tabsel_123[!frame.lsf_bit][frame.layer - 1][bitrate_idx] * 1000;
    *samplerate = frequencies[version_idx][freq_idx];
    *channels = (frame.channel_mode == 3 ? 1 : 2);
    lprintf ("frequencies[%d][%d]", version_idx, freq_idx);

    for( i = 0; i + 4 < length_read; i++)
    {
      if( mpg123_parse_xing_header (&frame, *samplerate,
			      buffer+i, length_read-i, bitrate, time ) )
      {
        *vbr = 1;
        break;
      }
    }

    return 1;
}

