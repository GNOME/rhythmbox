/*
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "rb-daap-connection.h"
#include "rb-daap-structure.h"
#include "rb-daap-dialog.h"

#include <libgnome/gnome-i18n.h>
#include "rb-debug.h"

/* hashing - based on/copied from libopendaap
 * Copyright (c) 2004 David Hammerton
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

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

static void 
rb_daap_hash_generate (short version_major, 
		       const guchar *url, 
		       guchar hash_select, 
		       guchar *out, 
		       gint request_id)
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

/* end hashing */


/* connection */
#include <math.h>
#include <libsoup/soup.h>
#include <libsoup/soup-connection.h>
#include <libsoup/soup-session-sync.h>

#include <libsoup/soup-uri.h>

#define RB_DAAP_USER_AGENT "iTunes/4.6 (Windows; N)"

struct _RBDAAPConnection {
	gchar *name;
	gboolean password_protected;
	gchar *password;
	
	SoupSession *session;
	SoupUri *base_uri;
	
	gdouble daap_version;
	gint session_id;
	gint revision_number;

	gint request_id;
	gint database_id;

	GSList *playlists;
	GHashTable *item_id_to_uri;
};

static gchar *
connection_get_password (RBDAAPConnection *connection)
{
	return rb_daap_password_dialog_new_run (connection->name);
}


static SoupMessage * 
build_message (RBDAAPConnection *connection, 
	       const gchar *path, 
	       gboolean need_hash, 
	       gdouble version, 
	       gint req_id, 
	       gboolean send_close)
{
	SoupMessage *message = NULL;
	SoupUri *uri = NULL;
	
	uri = soup_uri_new_with_base (connection->base_uri, path);
	if (uri == NULL) {
		return NULL;
	}		
	
	message = soup_message_new_from_uri (SOUP_METHOD_GET, uri);
	soup_message_set_http_version (message, SOUP_HTTP_1_1);
	
	soup_message_add_header (message->request_headers, "Client-DAAP-Version", 	"3.0");
	soup_message_add_header (message->request_headers, "Accept-Laungage", 		"en-us, en;q=5.0");
	soup_message_add_header (message->request_headers, "Client-DAAP-Access-Index", 	"2");
	if (connection->password_protected) {
		gchar *h = g_strconcat ("Basic ", connection->password, NULL);
		
		soup_message_add_header (message->request_headers, "Authorization", h);
		g_free (h);
	}
	
	if (need_hash) {
		gchar hash[33] = {0};
		gchar *no_daap_path = (gchar *)path;
		
		if (g_strncasecmp (path, "daap://", 7) == 0) {
			no_daap_path = strstr (path, "/data");
		}

		rb_daap_hash_generate ((short)floor (version), (const guchar*)no_daap_path, 2, (guchar*)hash, req_id);

		soup_message_add_header (message->request_headers, "Client-DAAP-Validation", hash);
	}
	if (send_close) {
		soup_message_add_header (message->request_headers, "Connection", "close");
	}

	soup_uri_free (uri);
	
	return message;
}

typedef enum {
	HTTP_OK,
	HTTP_ERR,
	HTTP_AUTH_ERR
} HTTPResponse;

static HTTPResponse
http_get (RBDAAPConnection *connection, 
	  const gchar *path, 
	  gboolean need_hash, 
	  gdouble version, 
	  gint req_id, 
	  gboolean send_close, 
	  GNode **structure)
{
	SoupMessage *message;
	RBDAAPItem *item = NULL;
	
	message = build_message (connection, path, need_hash, version, req_id, send_close);
	if (message == NULL) {
		rb_debug ("Error building message for %s", path);
		return HTTP_ERR;
	}
	
	soup_session_send_message (connection->session, message);
	
	if (message->status_code == 401) {
		g_object_unref (message);
		return HTTP_AUTH_ERR;
	}
	
	if (SOUP_STATUS_IS_SUCCESSFUL (message->status_code) == FALSE) {
		rb_debug ("Error getting %s: %d, %s\n", path, message->status_code, message->reason_phrase);
		g_object_unref (message);

		return HTTP_ERR;
	} 

	*structure = rb_daap_structure_parse (message->response.body, message->response.length);
	g_object_unref (message);
	
	if (*structure == NULL) {
		rb_debug ("No daap structure returned %s", path);
		return HTTP_ERR;
	}

	item = rb_daap_structure_find_item (*structure, RB_DAAP_CC_MSTT);
	if (item == NULL) {
		rb_debug ("Could not find dmap.status item in %s", path);
		return HTTP_ERR;
	}

	if (g_value_get_int (&(item->content)) != 200) {
		rb_debug ("Error, dmap.status is not 200 in %s", path);
		return HTTP_ERR;
	}

	return HTTP_OK;
}
	
static void 
entry_set_string_prop (RhythmDB *db, 
		       RhythmDBEntry *entry,
		       RhythmDBPropType propid, 
		       const char *str)
{
	GValue value = {0,};
	gchar *tmp;

	if (str == NULL) {
		tmp = g_strdup (_("Unknown"));
	} else {
		tmp = g_strdup (str);
	}

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string_take_ownership (&value, tmp);
	rhythmdb_entry_set_uninserted (RHYTHMDB (db), entry, propid, &value);
	g_value_unset (&value);
}

static gboolean
connection_get_info (RBDAAPConnection *connection)
{
	GNode *structure = NULL;
	HTTPResponse resp;
	gboolean ret;
	RBDAAPItem *item = NULL;

	/* get the daap version number */
	resp = http_get (connection, "/server-info", FALSE, 0.0, 0, FALSE, &structure);
	if (structure == NULL || resp == HTTP_ERR) {
		rb_debug ("Got invalid response from /server-info");
		ret = FALSE;
		goto out;
	}
	
	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_APRO);
	if (item == NULL ){
		rb_debug ("Could not find daap.protocolversion item in /server-info");
		ret = FALSE;
		goto out;
	}

	connection->daap_version = g_value_get_double (&(item->content));
	ret = TRUE;

out:
	rb_daap_structure_destroy (structure);

	return ret;
}

static gboolean
connection_login (RBDAAPConnection *connection)
{
	GNode *structure = NULL;
	HTTPResponse resp;
	gboolean ret;
	RBDAAPItem *item = NULL;
	gchar *path = NULL;

get_password:	
        if (connection->password_protected) {
		rb_debug ("Need a password for %s", connection->name);
                connection->password = connection_get_password (connection);
		if (connection->password == NULL || connection->password[0] == '\0') {
			rb_debug ("Password entry canceled");
			return FALSE;
		}
        }
	
	/* get a session id */
	resp = http_get (connection, "/login", FALSE, 0.0, 0, FALSE, &structure);

	if (resp == HTTP_AUTH_ERR) {
		rb_debug ("Incorrect password");
		goto get_password;
	}

	if (structure == NULL || resp == HTTP_ERR) {
		rb_debug ("Got invalid response from /login");
		ret = FALSE;
		goto out;
	}

	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MLID);
	if (item == NULL) {
		rb_debug ("Could not find daap.sessionid item in /login");
		ret = FALSE;
		goto out;
	}

	connection->session_id = g_value_get_int (&(item->content));

	rb_daap_structure_destroy (structure);
	structure = NULL;

	/* get a revision number */
	path = g_strdup_printf ("/update?session-id=%d&revision-number=1", connection->session_id);
	resp = http_get (connection, path, TRUE, connection->daap_version,0, FALSE,&structure);

	if (structure == NULL || resp == HTTP_ERR) {
		rb_debug ("Got invalid response from %s", path);
		ret = FALSE;
		goto out;
	}

	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MUSR);
	if (item == NULL) {
		rb_debug ("Could not find daap.serverrevision item in %s", path);
		ret = FALSE;
		goto out;
	}

	connection->revision_number = g_value_get_int (&(item->content));

	ret = TRUE;
out:
	rb_daap_structure_destroy (structure);
	g_free (path);

	return ret;
}

static gboolean
connection_get_database_info (RBDAAPConnection *connection)
{
	GNode *structure = NULL;
	HTTPResponse resp;
	gboolean ret;
	RBDAAPItem *item = NULL;
	GNode *listing_node;
	gchar *path;
	gint n_databases = 0;

	/* get database id */
	/* get a list of databases, there should be only 1 */

	path = g_strdup_printf ("/databases?session-id=%d&revision-number=%d", connection->session_id, connection->revision_number);
	resp = http_get (connection, path, TRUE, connection->daap_version,0, FALSE,&structure);
	
	if (structure == NULL || resp == HTTP_ERR) {
		rb_debug ("Got invalid response from %s", path);
		ret = FALSE;
		goto out;
	}

	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MRCO);
	if (item == NULL) {
		rb_debug ("Could not find dmap.returnedcount item in %s", path);
		ret = FALSE;
		goto out;
	}

	n_databases = g_value_get_int (&(item->content));
	if (n_databases != 1) {
		rb_debug ("Host seems to have more than 1 database, how strange\n");
	}
	
	listing_node = rb_daap_structure_find_node (structure, RB_DAAP_CC_MLCL);
	if (listing_node == NULL) {
		rb_debug ("Could not find dmap.listing item in %s", path);
		ret = FALSE;
		goto out;
	}

	item = rb_daap_structure_find_item (listing_node->children, RB_DAAP_CC_MIID);
	if (item == NULL) {
		rb_debug ("Could not find dmap.itemid item in %s", path);
		ret = FALSE;
		goto out;
	}
	
	connection->database_id = g_value_get_int (&(item->content));

	ret = TRUE;

out:
	rb_daap_structure_destroy (structure);
	g_free (path);

	return ret;
}

static gboolean
connection_get_song_listing (RBDAAPConnection *connection,
			     const gchar *host,
			     gint port,
			     RhythmDB *db,
			     RhythmDBEntryType type)
{
	GNode *structure = NULL;
	HTTPResponse resp;
	gboolean ret;
	RBDAAPItem *item = NULL;
	GNode *listing_node;
	gchar *path;
	gint returned_count;
	gint i;
	GNode *n;
	gint specified_total_count;
	gboolean update_type;

	/* get the songs */
	path = g_strdup_printf ("/databases/%i/items?session-id=%i&revision-number=%i&meta=dmap.itemid,dmap.itemname,daap.songalbum,daap.songartist,daap.daap.songgenre,daap.songsize,daap.songtime,daap.songtrackcount,daap.songtracknumber,daap.songyear,daap.songformat,daap.songgenre,daap.songbitrate", connection->database_id, connection->session_id, connection->revision_number);
	resp = http_get (connection, path, TRUE, connection->daap_version,0, FALSE,&structure);
	
	if (structure == NULL || resp == HTTP_ERR) {
		rb_debug ("Got invalid response from %s", path);
		ret = FALSE;
		goto out;
	}

	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MRCO);
	if (item == NULL) {
		rb_debug ("Could not find dmap.returnedcount item in %s", path);
		ret = FALSE;
		goto out;
	}
	returned_count = g_value_get_int (&(item->content));
	
	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MTCO);
	if (item == NULL) {
		rb_debug ("Could not find dmap.specifiedtotalcount item in %s", path);
		ret = FALSE;
		goto out;
	}
	specified_total_count = g_value_get_int (&(item->content));
	
	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MUTY);
	if (item == NULL) {
		rb_debug ("Could not find dmap.updatetype item in %s", path);
		ret = FALSE;
		goto out;
	}
	update_type = g_value_get_char (&(item->content));

	listing_node = rb_daap_structure_find_node (structure, RB_DAAP_CC_MLCL);
	if (listing_node == NULL) {
		rb_debug ("Could not find dmap.listing item in %s", path);
		ret = FALSE;
		goto out;
	}

	connection->item_id_to_uri = g_hash_table_new_full ((GHashFunc)g_direct_hash,(GEqualFunc)g_direct_equal, NULL, g_free);
	
	for (i = 0, n = listing_node->children; n; i++, n = n->next) {
		GNode *n2;
		RhythmDBEntry *entry = NULL;
		GValue value = {0,};
		gchar *uri = NULL;
		gint item_id = 0;
		const gchar *title = NULL;
		const gchar *album = NULL;
		const gchar *artist = NULL;
		const gchar *format = NULL;
		const gchar *genre = NULL;
		gint length = 0;
		gint track_number = 0;
		gint disc_number = 0;
		gint year = 0;
		gint size = 0;
		gint bitrate = 0;
		
		for (n2 = n->children; n2; n2 = n2->next) {
			RBDAAPItem *meta_item;
			
			meta_item = n2->data;

			switch (meta_item->content_code) {
				case RB_DAAP_CC_MIID:
					item_id = g_value_get_int (&(meta_item->content));
					break;
				case RB_DAAP_CC_MINM:
					title = g_value_get_string (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASAL:
					album = g_value_get_string (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASAR:
					artist = g_value_get_string (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASFM:
					format = g_value_get_string (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASGN:
					genre = g_value_get_string (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASTM:
					length = g_value_get_int (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASTN:
					track_number = g_value_get_int (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASDN:
					disc_number = g_value_get_int (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASYR:
					year = g_value_get_int (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASSZ:
					size = g_value_get_int (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASBR:
					bitrate = g_value_get_int (&(meta_item->content));
					break;
				default:
					break;
			}
		}

//		if (connection->daap_version == 3.0) {
			uri = g_strdup_printf ("daap://%s:%d/databases/%d/items/%d.%s?session-id=%d", host, port, connection->database_id, item_id, format, connection->session_id);
//		} else {
//		??FIXME??
//		OLD ITUNES
		// uri should be 
		// "/databases/%d/items/%d.%s?session-id=%d&revision-id=%d";
		// but its not going to work cause the other parts of the code 
		// depend on the uri to have the ip address so that the
		// RBDAAPSource can be found to ++request_id
		// maybe just /dont/ support older itunes.  doesn't seem 
		// unreasonable to me, honestly
//		}
		entry = rhythmdb_entry_new (db, type, uri);
		g_hash_table_insert (connection->item_id_to_uri, GINT_TO_POINTER (item_id), uri);

		 /* track number */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value,(gulong)track_number);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_TRACK_NUMBER, &value);
		g_value_unset (&value);

		/* disc number */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value,(gulong)disc_number);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_DISC_NUMBER, &value);
		g_value_unset (&value);

		/* bitrate */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value,(gulong)bitrate);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_BITRATE, &value);
		g_value_unset (&value);
		
		/* length */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value,(gulong)length / 1000);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_DURATION, &value);
		g_value_unset (&value);

		/* file size */
		g_value_init (&value, G_TYPE_UINT64);
		g_value_set_uint64(&value,(gint64)size);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_FILE_SIZE, &value);
		g_value_unset (&value);

		/* title */
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_TITLE, title);

		/* album */
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ALBUM, album);

		/* artist */
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ARTIST, artist);

		/* genre */
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_GENRE, genre);
	}

	rhythmdb_commit (db);

	ret = TRUE;
out:
	rb_daap_structure_destroy (structure);
	g_free (path);

	return ret;
}

/* FIXME
 * what we really should do is only get a list of playlists and their ids
 * then when they are clicked on ('activate'd) by the user, get a list of
 * the files that are actually in them.  This will speed up initial daap 
 * connection times and reduce memory consumption.
 */

static gboolean
connection_get_playlists (RBDAAPConnection *connection)
{
	GNode *structure = NULL;
	HTTPResponse resp;
	gboolean ret;
	GNode *listing_node;
	gchar *path;
	gint i;
	GNode *n;

	path = g_strdup_printf ("/databases/%d/containers?session-id=%d&revision-number=%d", connection->database_id, connection->session_id, connection->revision_number);
	resp = http_get (connection, path, TRUE, connection->daap_version,0, FALSE,&structure);
	
	if (structure == NULL || resp == HTTP_ERR) {
		rb_debug ("Got invalid response from %s", path);
		ret = FALSE;
		goto out;
	}

	listing_node = rb_daap_structure_find_node (structure, RB_DAAP_CC_MLCL);
	if (listing_node == NULL) {
		rb_debug ("Could not find dmap.listing item in %s", path);
		ret = FALSE;
		goto out;
	}

	for (i = 0, n = listing_node->children; n; n = n->next, i++) {
		RBDAAPItem *item;
		gint id;
		gchar *name;
		RBDAAPPlaylist *playlist;
		GNode *playlist_structure;
		GNode *playlist_listing_node;
		GNode *n2;
		gint j;
		GList *playlist_uris = NULL;
		gchar *path2;
		
		item = rb_daap_structure_find_item (n, RB_DAAP_CC_ABPL);
		if (item != NULL) {
			continue;
		}

		item = rb_daap_structure_find_item (n, RB_DAAP_CC_MIID);
		if (item == NULL) {
			rb_debug ("Could not find dmap.itemid item in %s", path);
			ret = FALSE;
			goto out;
		}
		id = g_value_get_int (&(item->content));

		item = rb_daap_structure_find_item (n, RB_DAAP_CC_MINM);
		if (item == NULL) {
			rb_debug ("Could not find dmap.itemname item in %s", path);
			ret = FALSE;
			goto out;
		}
		name = g_value_dup_string (&(item->content));

		path2 = g_strdup_printf ("/databases/%d/containers/%d/items?session-id=%d&revision-number=%d&meta=dmap.itemid", connection->database_id, id, connection->session_id, connection->revision_number);
		resp = http_get (connection, path2, TRUE, connection->daap_version,0, FALSE,&playlist_structure);
		
		if (playlist_structure == NULL || resp == HTTP_ERR) {
			g_free (name);
			continue;
		}

		playlist_listing_node = rb_daap_structure_find_node (playlist_structure, RB_DAAP_CC_MLCL);
		if (playlist_listing_node == NULL) {
			rb_debug ("Could not find dmap.listing item in %s", path2);
			g_free (name);
			continue;
		}

		for (j = 0, n2 = playlist_listing_node->children; n2; n2 = n2->next, j++) {
			gchar *item_uri;
			gint playlist_item_id;

			item = rb_daap_structure_find_item (n2, RB_DAAP_CC_MIID);
			if (item == NULL) {
				rb_debug ("Could not find dmap.itemid item in %s", path2);
				continue;
			}
			playlist_item_id = g_value_get_int (&(item->content));
		
			item_uri = g_hash_table_lookup (connection->item_id_to_uri, GINT_TO_POINTER (playlist_item_id));
			if (item_uri == NULL) {
				rb_debug ("%d in %s doesnt exist in the database\n", playlist_item_id, name);
				continue;
			}
			
			playlist_uris = g_list_prepend (playlist_uris, item_uri);
		}
		
		playlist = g_new0(RBDAAPPlaylist,1);

		playlist->id = id;
		playlist->name = name;
		playlist->uris = playlist_uris;

		connection->playlists = g_slist_prepend (connection->playlists, playlist);
	}

	ret = TRUE;
out:
	rb_daap_structure_destroy (structure);
	structure = NULL;

	return ret;
}
	
RBDAAPConnection * 
rb_daap_connection_new (const gchar *name,
			const gchar *host, 
			gint port, 
			gboolean password_protected,
			RhythmDB *db, 
			RhythmDBEntryType type)
{
	RBDAAPConnection *connection = NULL;
	gchar *path = NULL;
	
	connection = g_new0 (RBDAAPConnection, 1);
	connection->name = g_strdup (name);

	rb_debug ("Creating new DAAP connection to %s:%d", host, port);

	connection->session = soup_session_sync_new ();
	path = g_strdup_printf ("http://%s:%d/", host, port);
	connection->base_uri = soup_uri_new (path);
	if (connection->base_uri == NULL) {
		rb_debug ("Error parsing %s", path);
		goto error_out;
	}
	
	rb_debug ("Getting DAAP connection info");
	if (connection_get_info (connection) == FALSE) {
		rb_debug ("Could not get DAAP connection info");
		goto error_out;
	}

	rb_debug ("Logging into DAAP server");
	connection->password_protected = password_protected;
	if (connection_login (connection) == FALSE) {
		rb_debug ("Could not login to DAAP server");
		goto error_out;
	}
	
	rb_debug ("Getting DAAP database info");
	if (connection_get_database_info (connection) == FALSE) {
		rb_debug ("Could not get DAAP database info");
		goto error_out;
	}
	
	rb_debug ("Getting DAAP song listing");
	if (connection_get_song_listing (connection, host, port, db, type) == FALSE) {
		rb_debug ("Could not get DAAP song listing");
		goto error_out;
	}
	/* now that we have gotten the song listing, creation shouldn't fail */

	rb_debug ("Getting DAAP playlists");
	if (connection_get_playlists (connection) == FALSE) {
		rb_debug ("Could not get DAAP playlists");
	}
	
	rb_debug ("Successfully created DAAP connection");
	
	goto out;

	
error_out:
	rb_daap_connection_destroy (connection);
	connection = NULL;
	
out:
	if (path) {
		g_free (path);
	}
	
	return connection;
}

gchar * 
rb_daap_connection_get_headers (RBDAAPConnection *connection, 
				const gchar *uri, 
				gint64 bytes)
{
	GString *headers;
	gchar hash[33] = {0};
	gchar *norb_daap_uri = (gchar *)uri;
	gchar *s;
	
	connection->request_id++;
	
	if (g_strncasecmp (uri,"daap://",7) == 0) {
		norb_daap_uri = strstr (uri,"/data");
	}

	rb_daap_hash_generate ((short)floorf (connection->daap_version), (const guchar*)norb_daap_uri,2, (guchar*)hash, connection->request_id);

	headers = g_string_new ("Accept: */*\r\nCache-Control: no-cache\r\nUser-Agent: "RB_DAAP_USER_AGENT"\r\nAccept-Language: en-us, en;q=5.0\r\nClient-DAAP-Access-Index: 2\r\nClient-DAAP-Version: 3.0\r\n");
	g_string_append_printf (headers, "Client-DAAP-Validation: %s\r\nClient-DAAP-Request-ID: %d\r\nConnection: close\r\n", hash, connection->request_id);
	if (connection->password_protected) {
		g_string_append_printf (headers, "Authentication: Basic %s\r\n", connection->password);
	}

	if (bytes != 0) {
		g_string_append_printf (headers,"Range: bytes=%"G_GINT64_FORMAT"-\r\n", bytes);
	}
	
	s = headers->str;
	g_string_free (headers, FALSE);

	return s;
}

GSList * 
rb_daap_connection_get_playlists (RBDAAPConnection *connection)
{
	if (connection) {
		return connection->playlists;
	}

	return NULL;
}


void 
rb_daap_connection_destroy (RBDAAPConnection *connection)
{
	GSList *l;

	if (connection->name) {
		g_free (connection->name);
		connection->name = NULL;
	}
	
	if (connection->password) {
		g_free (connection->password);
		connection->password = NULL;
	}
	
	for (l = connection->playlists; l; l = l->next) {
		RBDAAPPlaylist *playlist = l->data;

		g_list_free (playlist->uris);
		g_free (playlist->name);
		g_free (playlist);
		l->data = NULL;
	}
	g_slist_free (connection->playlists);
	connection->playlists = NULL;

	if (connection->item_id_to_uri) {
		g_hash_table_destroy (connection->item_id_to_uri);
		connection->item_id_to_uri = NULL;
	}
	
	if (connection->session) {
		g_object_unref (connection->session);
		connection->session = NULL;
	}
	
	g_free (connection);
	connection = NULL;

	return;
}
