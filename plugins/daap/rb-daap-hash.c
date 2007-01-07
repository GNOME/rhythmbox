/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Implementation of DAAP (iTunes Music Sharing) hashing, parsing, connection
 *
 *  Copyright (C) 2004,2005 Charles Schmidt <cschmidt2@emich.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <math.h>
#ifdef HAVE_LIBZ
#include <zlib.h>
#endif

#include <glib.h>

#include "rb-daap-hash.h"

/* hashing - based on/copied from libopendaap
 * Copyright (c) 2004 David Hammerton
 */

typedef struct {
    guint32 buf[4];
    guint32 bits[2];
    unsigned char in[64];
    int version;
} MD5_CTX;

/*
* This code implements the MD5 message-digest algorithm.
* The algorithm is due to Ron Rivest.  This code was
* written by Colin Plumb in 1993, no copyright is claimed.
* This code is in the public domain; do with it what you wish.
*
* Equivalent code is available from RSA Data Security, Inc.
* This code has been tested against that, and is equivalent,
* except that you don't need to include two pages of legalese
* with every copy.
*
* To compute the message digest of a chunk of bytes, declare an MD5Context
* structure, pass it to OpenDaap_MD5Init, call OpenDaap_MD5Update as needed
* on buffers full of bytes, and then call OpenDaap_MD5Final, which will fill
* a supplied 16-byte array with the digest.
*/
static void
MD5Transform (guint32 buf[4],
	      guint32 const in[16],
	      gint version);
/* for some reason we still have to reverse bytes on bigendian machines
 * I don't really know why... but otherwise it fails..
 * Any MD5 gurus out there know why???
 */
#if 0 //ndef WORDS_BIGENDIAN /* was: HIGHFIRST */
#define byteReverse(buf, len)     /* Nothing */
#else
static void
byteReverse (unsigned char *buf,
	     unsigned longs);

#ifndef ASM_MD5
/*
* Note: this code is harmless on little-endian machines.
*/
static void
byteReverse (unsigned char *buf,
	     unsigned longs)
{
     guint32 t;
     do {
          t = (guint32) ((unsigned) buf[3] << 8 | buf[2]) << 16 |
               ((unsigned) buf[1] << 8 | buf[0]);
          *(guint32 *) buf = t;
          buf += 4;
     } while (--longs);
}
#endif /* ! ASM_MD5 */
#endif /* #if 0 */

static void
OpenDaap_MD5Init (MD5_CTX *ctx,
		  gint version)
{
    memset (ctx, 0, sizeof (MD5_CTX));
    ctx->buf[0] = 0x67452301;
    ctx->buf[1] = 0xefcdab89;
    ctx->buf[2] = 0x98badcfe;
    ctx->buf[3] = 0x10325476;

    ctx->bits[0] = 0;
    ctx->bits[1] = 0;

    ctx->version = version;
}

static void
OpenDaap_MD5Update (MD5_CTX *ctx,
		    unsigned char const *buf,
		    unsigned int len)
{
    guint32 t;

    /* Update bitcount */

    t = ctx->bits[0];
    if ((ctx->bits[0] = t + ((guint32) len << 3)) < t)
        ctx->bits[1]++;          /* Carry from low to high */
    ctx->bits[1] += len >> 29;

    t = (t >> 3) & 0x3f;     /* Bytes already in shsInfo->data */

    /* Handle any leading odd-sized chunks */

    if (t) {
        unsigned char *p = (unsigned char *) ctx->in + t;

        t = 64 - t;
        if (len < t) {
            memcpy (p, buf, len);
            return;
        }
        memcpy (p, buf, t);
        byteReverse (ctx->in, 16);
        MD5Transform (ctx->buf, (guint32 *) ctx->in, ctx->version);
        buf += t;
        len -= t;
    }
    /* Process data in 64-byte chunks */

    while (len >= 64) {
        memcpy (ctx->in, buf, 64);
        byteReverse (ctx->in, 16);
        MD5Transform (ctx->buf, (guint32 *) ctx->in, ctx->version);
        buf += 64;
        len -= 64;
    }

    /* Handle any remaining bytes of data. */

    memcpy (ctx->in, buf, len);

}

static void
OpenDaap_MD5Final (MD5_CTX *ctx,
		   unsigned char digest[16])
{
    unsigned count;
    unsigned char *p;

    /* Compute number of bytes mod 64 */
    count = (ctx->bits[0] >> 3) & 0x3F;

    /* Set the first char of padding to 0x80.  This is safe since there is
    always at least one byte free */
    p = ctx->in + count;
    *p++ = 0x80;

    /* Bytes of padding needed to make 64 bytes */
    count = 64 - 1 - count;

    /* Pad out to 56 mod 64 */
    if (count < 8) {
        /* Two lots of padding:  Pad the first block to 64 bytes */
        memset (p, 0, count);
        byteReverse (ctx->in, 16);
        MD5Transform (ctx->buf, (guint32 *) ctx->in, ctx->version);

        /* Now fill the next block with 56 bytes */
        memset (ctx->in, 0, 56);
    } else {
        /* Pad block to 56 bytes */
        memset (p, 0, count - 8);
    }
    byteReverse (ctx->in, 14);

    /* Append length in bits and transform */
    ((guint32 *) ctx->in)[14] = ctx->bits[0];
    ((guint32 *) ctx->in)[15] = ctx->bits[1];

    MD5Transform (ctx->buf, (guint32 *) ctx->in, ctx->version);
    byteReverse ((unsigned char *) ctx->buf, 4);
    memcpy (digest, ctx->buf, 16);
    memset (ctx, 0, sizeof(ctx));     /* In case it's sensitive */

    return;
}

#ifndef ASM_MD5

/* The four core functions - F1 is optimized somewhat */

/* #define F1(x, y, z) (x & y | ~x & z) */
#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))

/* This is the central step in the MD5 algorithm. */
#define MD5STEP(f, w, x, y, z, data, s) \
( w += f(x, y, z) + data,  w = w<<s | w>>(32-s),  w += x )

/*
* The core of the MD5 algorithm, this alters an existing MD5 hash to reflect
* the addition of 16 longwords of new data.  OpenDaap_MD5Update blocks the
* data and converts bytes into longwords for this routine.
*/
static void
MD5Transform (guint32 buf[4],
	      guint32 const in[16],
	      gint version)
{
    guint32 a, b, c, d;

    a = buf[0];
    b = buf[1];
    c = buf[2];
    d = buf[3];

    MD5STEP(F1, a, b, c, d, in[0] + 0xd76aa478, 7);
    MD5STEP(F1, d, a, b, c, in[1] + 0xe8c7b756, 12);
    MD5STEP(F1, c, d, a, b, in[2] + 0x242070db, 17);
    MD5STEP(F1, b, c, d, a, in[3] + 0xc1bdceee, 22);
    MD5STEP(F1, a, b, c, d, in[4] + 0xf57c0faf, 7);
    MD5STEP(F1, d, a, b, c, in[5] + 0x4787c62a, 12);
    MD5STEP(F1, c, d, a, b, in[6] + 0xa8304613, 17);
    MD5STEP(F1, b, c, d, a, in[7] + 0xfd469501, 22);
    MD5STEP(F1, a, b, c, d, in[8] + 0x698098d8, 7);
    MD5STEP(F1, d, a, b, c, in[9] + 0x8b44f7af, 12);
    MD5STEP(F1, c, d, a, b, in[10] + 0xffff5bb1, 17);
    MD5STEP(F1, b, c, d, a, in[11] + 0x895cd7be, 22);
    MD5STEP(F1, a, b, c, d, in[12] + 0x6b901122, 7);
    MD5STEP(F1, d, a, b, c, in[13] + 0xfd987193, 12);
    MD5STEP(F1, c, d, a, b, in[14] + 0xa679438e, 17);
    MD5STEP(F1, b, c, d, a, in[15] + 0x49b40821, 22);

    MD5STEP(F2, a, b, c, d, in[1] + 0xf61e2562, 5);
    MD5STEP(F2, d, a, b, c, in[6] + 0xc040b340, 9);
    MD5STEP(F2, c, d, a, b, in[11] + 0x265e5a51, 14);
    MD5STEP(F2, b, c, d, a, in[0] + 0xe9b6c7aa, 20);
    MD5STEP(F2, a, b, c, d, in[5] + 0xd62f105d, 5);
    MD5STEP(F2, d, a, b, c, in[10] + 0x02441453, 9);
    MD5STEP(F2, c, d, a, b, in[15] + 0xd8a1e681, 14);
    MD5STEP(F2, b, c, d, a, in[4] + 0xe7d3fbc8, 20);
    MD5STEP(F2, a, b, c, d, in[9] + 0x21e1cde6, 5);
    MD5STEP(F2, d, a, b, c, in[14] + 0xc33707d6, 9);
    MD5STEP(F2, c, d, a, b, in[3] + 0xf4d50d87, 14);

    if (version == 1)
    {
        MD5STEP(F2, b, c, d, a, in[8] + 0x445a14ed, 20);
    }
    else
    {
        MD5STEP(F2, b, c, d, a, in[8] + 0x455a14ed, 20);
    }
    MD5STEP(F2, a, b, c, d, in[13] + 0xa9e3e905, 5);
    MD5STEP(F2, d, a, b, c, in[2] + 0xfcefa3f8, 9);
    MD5STEP(F2, c, d, a, b, in[7] + 0x676f02d9, 14);
    MD5STEP(F2, b, c, d, a, in[12] + 0x8d2a4c8a, 20);

    MD5STEP(F3, a, b, c, d, in[5] + 0xfffa3942, 4);
    MD5STEP(F3, d, a, b, c, in[8] + 0x8771f681, 11);
    MD5STEP(F3, c, d, a, b, in[11] + 0x6d9d6122, 16);
    MD5STEP(F3, b, c, d, a, in[14] + 0xfde5380c, 23);
    MD5STEP(F3, a, b, c, d, in[1] + 0xa4beea44, 4);
    MD5STEP(F3, d, a, b, c, in[4] + 0x4bdecfa9, 11);
    MD5STEP(F3, c, d, a, b, in[7] + 0xf6bb4b60, 16);
    MD5STEP(F3, b, c, d, a, in[10] + 0xbebfbc70, 23);
    MD5STEP(F3, a, b, c, d, in[13] + 0x289b7ec6, 4);
    MD5STEP(F3, d, a, b, c, in[0] + 0xeaa127fa, 11);
    MD5STEP(F3, c, d, a, b, in[3] + 0xd4ef3085, 16);
    MD5STEP(F3, b, c, d, a, in[6] + 0x04881d05, 23);
    MD5STEP(F3, a, b, c, d, in[9] + 0xd9d4d039, 4);
    MD5STEP(F3, d, a, b, c, in[12] + 0xe6db99e5, 11);
    MD5STEP(F3, c, d, a, b, in[15] + 0x1fa27cf8, 16);
    MD5STEP(F3, b, c, d, a, in[2] + 0xc4ac5665, 23);

    MD5STEP(F4, a, b, c, d, in[0] + 0xf4292244, 6);
    MD5STEP(F4, d, a, b, c, in[7] + 0x432aff97, 10);
    MD5STEP(F4, c, d, a, b, in[14] + 0xab9423a7, 15);
    MD5STEP(F4, b, c, d, a, in[5] + 0xfc93a039, 21);
    MD5STEP(F4, a, b, c, d, in[12] + 0x655b59c3, 6);
    MD5STEP(F4, d, a, b, c, in[3] + 0x8f0ccc92, 10);
    MD5STEP(F4, c, d, a, b, in[10] + 0xffeff47d, 15);
    MD5STEP(F4, b, c, d, a, in[1] + 0x85845dd1, 21);
    MD5STEP(F4, a, b, c, d, in[8] + 0x6fa87e4f, 6);
    MD5STEP(F4, d, a, b, c, in[15] + 0xfe2ce6e0, 10);
    MD5STEP(F4, c, d, a, b, in[6] + 0xa3014314, 15);
    MD5STEP(F4, b, c, d, a, in[13] + 0x4e0811a1, 21);
    MD5STEP(F4, a, b, c, d, in[4] + 0xf7537e82, 6);
    MD5STEP(F4, d, a, b, c, in[11] + 0xbd3af235, 10);
    MD5STEP(F4, c, d, a, b, in[2] + 0x2ad7d2bb, 15);
    MD5STEP(F4, b, c, d, a, in[9] + 0xeb86d391, 21);

    buf[0] += a;
    buf[1] += b;
    buf[2] += c;
    buf[3] += d;
}

#endif

static int staticHashDone = 0;
static unsigned char staticHash_42[256*65] = {0};
static unsigned char staticHash_45[256*65] = {0};

static const char hexchars[] = "0123456789ABCDEF";
static char ac[] = "Dpqzsjhiu!3114!Bqqmf!Dpnqvufs-!Jod/"; /* +1 */
static gboolean ac_unfudged = FALSE;

static void
DigestToString (const unsigned char *digest,
		char *string)
{
    int i;
    for (i = 0; i < 16; i++)
    {
        unsigned char tmp = digest[i];
        string[i*2+1] = hexchars[tmp & 0x0f];
        string[i*2] = hexchars[(tmp >> 4) & 0x0f];
    }
}

static void
GenerateStatic_42 ()
{
    MD5_CTX ctx;
    unsigned char *p = staticHash_42;
    int i;
    unsigned char buf[16];

    for (i = 0; i < 256; i++)
    {
        OpenDaap_MD5Init (&ctx, 0);

#define MD5_STRUPDATE(str) OpenDaap_MD5Update(&ctx, (unsigned char const *)str, strlen(str))

        if ((i & 0x80) != 0)
            MD5_STRUPDATE("Accept-Language");
        else
            MD5_STRUPDATE("user-agent");

        if ((i & 0x40) != 0)
            MD5_STRUPDATE("max-age");
        else
            MD5_STRUPDATE("Authorization");

        if ((i & 0x20) != 0)
            MD5_STRUPDATE("Client-DAAP-Version");
        else
            MD5_STRUPDATE("Accept-Encoding");

        if ((i & 0x10) != 0)
            MD5_STRUPDATE("daap.protocolversion");
        else
            MD5_STRUPDATE("daap.songartist");

        if ((i & 0x08) != 0)
            MD5_STRUPDATE("daap.songcomposer");
        else
            MD5_STRUPDATE("daap.songdatemodified");

        if ((i & 0x04) != 0)
            MD5_STRUPDATE("daap.songdiscnumber");
        else
            MD5_STRUPDATE("daap.songdisabled");

        if ((i & 0x02) != 0)
            MD5_STRUPDATE("playlist-item-spec");
        else
            MD5_STRUPDATE("revision-number");

        if ((i & 0x01) != 0)
            MD5_STRUPDATE("session-id");
        else
            MD5_STRUPDATE("content-codes");
#undef MD5_STRUPDATE

        OpenDaap_MD5Final (&ctx, buf);
        DigestToString (buf, (char *)p);
        p += 65;
    }
}

static void GenerateStatic_45()
{
    MD5_CTX ctx;
    unsigned char *p = staticHash_45;
    int i;
    unsigned char buf[16];

    for (i = 0; i < 256; i++)
    {
        OpenDaap_MD5Init (&ctx, 1);

#define MD5_STRUPDATE(str) OpenDaap_MD5Update(&ctx, (unsigned char const *)str, strlen(str))

        if ((i & 0x40) != 0)
            MD5_STRUPDATE("eqwsdxcqwesdc");
        else
            MD5_STRUPDATE("op[;lm,piojkmn");

        if ((i & 0x20) != 0)
            MD5_STRUPDATE("876trfvb 34rtgbvc");
        else
            MD5_STRUPDATE("=-0ol.,m3ewrdfv");

        if ((i & 0x10) != 0)
            MD5_STRUPDATE("87654323e4rgbv ");
        else
            MD5_STRUPDATE("1535753690868867974342659792");

        if ((i & 0x08) != 0)
            MD5_STRUPDATE("Song Name");
        else
            MD5_STRUPDATE("DAAP-CLIENT-ID:");

        if ((i & 0x04) != 0)
            MD5_STRUPDATE("111222333444555");
        else
            MD5_STRUPDATE("4089961010");

        if ((i & 0x02) != 0)
            MD5_STRUPDATE("playlist-item-spec");
        else
            MD5_STRUPDATE("revision-number");

        if ((i & 0x01) != 0)
            MD5_STRUPDATE("session-id");
        else
            MD5_STRUPDATE("content-codes");

        if ((i & 0x80) != 0)
            MD5_STRUPDATE("IUYHGFDCXWEDFGHN");
        else
            MD5_STRUPDATE("iuytgfdxwerfghjm");

#undef MD5_STRUPDATE

        OpenDaap_MD5Final (&ctx, buf);
        DigestToString (buf, (char *)p);
        p += 65;
    }
}

void
rb_daap_hash_generate (short         version_major,
		       const guchar *url,
		       guchar        hash_select,
		       guchar       *out,
		       gint          request_id)
{
    unsigned char buf[16];
    MD5_CTX ctx;
    int i;

    unsigned char *hashTable = (version_major == 3) ?
                      staticHash_45 : staticHash_42;

    if (!staticHashDone)
    {
        GenerateStatic_42 ();
        GenerateStatic_45 ();
        staticHashDone = 1;
    }

    OpenDaap_MD5Init (&ctx, (version_major == 3) ? 1 : 0);

    OpenDaap_MD5Update (&ctx, url, strlen ((const gchar*)url));
    if (ac_unfudged == FALSE) {
	    for (i = 0; i < strlen (ac); i++) {
		ac[i] = ac[i]-1;
	    }
    	    ac_unfudged = TRUE;
    }
    OpenDaap_MD5Update (&ctx, (const guchar*)ac, strlen (ac));

    OpenDaap_MD5Update (&ctx, &hashTable[hash_select * 65], 32);

    if (request_id && version_major == 3)
    {
        char scribble[20];
        sprintf (scribble, "%u", request_id);
        OpenDaap_MD5Update (&ctx, (const guchar*)scribble, strlen (scribble));
    }

    OpenDaap_MD5Final (&ctx, buf);
    DigestToString (buf, (char *)out);

    return;
}
