
/*
 * arch-tag: mp3 bitrate parsing header
 * Returns 1 if the header was parsed successfully, 0 if it failed
 *
 * bitrate: in bps, not kbps
 * samplerate: ditto
 * time: only informed if we have a VBR file with Xing headers, needs to be
 *       deduced from the bitrate and filesize otherwise, in seconds
 * version: 1 for MPEG Version 1, 2 for MPEG Version 2, and
 *          3 for MPEG Version 2.5
 * vbr: whether it is a variable bitrate stream
 * channels: number of channels used in the stream
 */

int mp3_bitrate_parse_header (guchar *buffer, guint length_read, int *bitrate,
		int *samplerate, int *time, int *version, int *vbr,
		int *channels);

