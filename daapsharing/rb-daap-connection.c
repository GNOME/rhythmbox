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

#ifdef HAVE_LIBZ
#include <zlib.h>
#endif

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


static GObject * rb_daap_connection_constructor (GType type, guint n_construct_properties,
						 GObjectConstructParam *construct_properties);
static void rb_daap_connection_dispose (GObject *obj);
static void rb_daap_connection_set_property (GObject *object,
					     guint prop_id,
					     const GValue *value,
					     GParamSpec *pspec);
static void rb_daap_connection_get_property (GObject *object,
					     guint prop_id,
					     GValue *value,
					     GParamSpec *pspec);

static void rb_daap_connection_do_something (RBDAAPConnection *connection);
static void rb_daap_connection_state_done (RBDAAPConnection *connection, gboolean result);


G_DEFINE_TYPE (RBDAAPConnection, rb_daap_connection, G_TYPE_OBJECT)
#define DAAP_CONNECTION_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_DAAP_CONNECTION, RBDaapConnectionPrivate))

typedef void (*RBDAAPResponseHandler) (RBDAAPConnection *connection,
				       guint status,
				       GNode *structure);

typedef struct {
	gchar *name;
	gboolean password_protected;
	gchar *password;
	char *host;
	guint port;
	
	SoupSession *session;
	SoupUri *base_uri;
	gchar *daap_base_uri;
	
	gdouble daap_version;
	gint session_id;
	gint revision_number;

	gint request_id;
	gint database_id;

	guint reading_playlist;
	GSList *playlists;
	GHashTable *item_id_to_uri;

	RhythmDB *db;
	RhythmDBEntryType db_type;

	enum {
		DAAP_GET_INFO = 0,
		DAAP_GET_PASSWORD,
		DAAP_LOGIN,
		DAAP_GET_REVISION_NUMBER,
		DAAP_GET_DB_INFO,
		DAAP_GET_SONGS,
		DAAP_GET_PLAYLISTS,
		DAAP_GET_PLAYLIST_ENTRIES,
		DAAP_LOGOUT,
		DAAP_DONE
	} state;
	RBDAAPResponseHandler response_handler;

	gboolean result;
	RBDAAPConnectionCallback callback;
	gpointer callback_user_data;
} RBDaapConnectionPrivate;


enum {
	PROP_0,
	PROP_DB,
	PROP_NAME,
	PROP_CALLBACK,
	PROP_CALLBACK_DATA,
	PROP_ENTRY_TYPE,
	PROP_PASSWORD_PROTECTED,
	PROP_HOST,
	PROP_PORT,
};

static void
rb_daap_connection_class_init (RBDAAPConnectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructor = rb_daap_connection_constructor;
	object_class->dispose = rb_daap_connection_dispose;
	object_class->set_property = rb_daap_connection_set_property;
	object_class->get_property = rb_daap_connection_get_property;

	g_type_class_add_private (klass, sizeof (RBDaapConnectionPrivate));

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_CALLBACK,
					 g_param_spec_pointer ("callback",
							      "callback",
							      "callback function",
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_CALLBACK_DATA,
					 g_param_spec_pointer ("callback-data",
							      "Callback Data",
							      "callback user data",
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_ENTRY_TYPE,
					 g_param_spec_uint ("entry-type",
							    "entry type",
							    "RhythmDBEntryType",
							    0, G_MAXINT, 0,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_PASSWORD_PROTECTED,
					 g_param_spec_boolean ("password-protected",
							       "password protected",
							       "connection is password protected",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
						 	      "connection name",
							      "connection name",
							      NULL,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_HOST,
					 g_param_spec_string ("host",
						 	      "host",
							      "host",
							      NULL,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_PORT,
					 g_param_spec_uint ("port",
							    "port",
							    "port",
							    0, G_MAXINT, 0,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_daap_connection_init (RBDAAPConnection *connection)
{

}


static gchar *
connection_get_password (RBDAAPConnection *connection)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);

	return rb_daap_password_dialog_new_run (priv->name);
}


static SoupMessage * 
build_message (RBDAAPConnection *connection, 
	       const gchar *path, 
	       gboolean need_hash, 
	       gdouble version, 
	       gint req_id, 
	       gboolean send_close)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);
	SoupMessage *message = NULL;
	SoupUri *uri = NULL;
	
	uri = soup_uri_new_with_base (priv->base_uri, path);
	if (uri == NULL) {
		return NULL;
	}
	
	message = soup_message_new_from_uri (SOUP_METHOD_GET, uri);
	soup_message_set_http_version (message, SOUP_HTTP_1_1);
	
	soup_message_add_header (message->request_headers, "Client-DAAP-Version", 	"3.0");
	soup_message_add_header (message->request_headers, "Accept-Language", 		"en-us, en;q=5.0");
#ifdef HAVE_LIBZ
	soup_message_add_header (message->request_headers, "Accept-Encoding",		"gzip");
#endif
	soup_message_add_header (message->request_headers, "Client-DAAP-Access-Index", 	"2");
	if (priv->password_protected) {
		gchar *h = g_strconcat ("Basic ", priv->password, NULL);
		
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

#ifdef HAVE_LIBZ
static void *g_zalloc_wrapper (voidpf opaque, uInt items, uInt size)
{
	return g_malloc0 (items * size);
}

static void g_zfree_wrapper (voidpf opaque, voidpf address)
{
	g_free (address);
}
#endif

static void
http_response_handler (SoupMessage *message,
		       RBDAAPConnection *connection)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);
	GNode *structure = NULL;
	guint status = message->status_code;
	char *response = message->response.body;
	int response_length = message->response.length;
	const char *encoding_header = NULL;

	if (message->response_headers)
		encoding_header = soup_message_get_header (message->response_headers, "Content-Encoding");

	if (SOUP_STATUS_IS_SUCCESSFUL (status) && encoding_header && strcmp(encoding_header, "gzip") == 0) {
#ifdef HAVE_LIBZ
		z_stream stream;
		char *new_response;
		int factor = 4;
		int unc_size = response_length * factor;

		stream.next_in = (unsigned char *)response;
		stream.avail_in = response_length;
		stream.total_in = 0;

		new_response = g_malloc (unc_size + 1);
		stream.next_out = (unsigned char *)new_response;
		stream.avail_out = unc_size;
		stream.total_out = 0;
		stream.zalloc = g_zalloc_wrapper;
		stream.zfree = g_zfree_wrapper;
		stream.opaque = NULL;

		if (inflateInit2 (&stream, 32 /* auto-detect */ + 15 /* max */ ) != Z_OK) {
			inflateEnd (&stream);
			g_free (new_response);
			rb_debug ("Unable to decompress response from http://%s:%d/%s",
				  priv->base_uri->host,
				  priv->base_uri->port,
				  priv->base_uri->path);
			status = SOUP_STATUS_MALFORMED;
		} else {
			do {
				int z_res = inflate (&stream, Z_FINISH);
				if (z_res == Z_STREAM_END)
					break;
				if ((z_res != Z_OK && z_res != Z_BUF_ERROR) || stream.avail_out != 0 || unc_size > 40*1000*1000) {
					inflateEnd (&stream);
					g_free (new_response);
					new_response = NULL;
					break;
				}

				factor *= 4;
				unc_size = (response_length * factor);
				new_response = g_realloc (new_response, unc_size + 1);
				stream.next_out = (unsigned char *)(new_response + stream.total_out);
				stream.avail_out = unc_size - stream.total_out;
			} while (1);
		}

		if (new_response) {
			response = new_response;
			response_length = stream.total_out;
		}
#else
		rb_debug ("Received compressed response from http://%s:%d/%s but can't handle it",
			  priv->base_uri->host,
			  priv->base_uri->port,
			  priv->base_uri->path);
		status = SOUP_STATUS_MALFORMED;
#endif
	}

	if (SOUP_STATUS_IS_SUCCESSFUL (status)) {
		RBDAAPItem *item;

		structure = rb_daap_structure_parse (response, response_length);
		if (structure == NULL) {
			rb_debug ("No daap structure returned from http://%s:%d/%s", 
				  priv->base_uri->host,
				  priv->base_uri->port,
				  priv->base_uri->path);
			status = SOUP_STATUS_MALFORMED;
		} else {
			int dmap_status = 0;
			item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MSTT);
			if (item)
				dmap_status = g_value_get_int (&(item->content));

			if (dmap_status != 200) {
				rb_debug ("Error, dmap.status is not 200 in response from http://%s:%d/%s",
					  priv->base_uri->host,
					  priv->base_uri->port,
					  priv->base_uri->path);
				status = SOUP_STATUS_MALFORMED;
			}
		}
	} else {
		rb_debug ("Error getting http://%s:%d/%s: %d, %s\n", 
			  priv->base_uri->host,
			  priv->base_uri->port,
			  priv->base_uri->path, 
			  message->status_code, message->reason_phrase);
	}

	if (priv->response_handler) {
		RBDAAPResponseHandler h = priv->response_handler;
		priv->response_handler = NULL;
		(*h) (connection, status, structure);
	}

	if (structure)
		rb_daap_structure_destroy (structure);

	if (response != message->response.body)
		g_free (response);
}

static gboolean
http_get (RBDAAPConnection *connection, 
	  const gchar *path, 
	  gboolean need_hash, 
	  gdouble version, 
	  gint req_id, 
	  gboolean send_close,
	  RBDAAPResponseHandler handler)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);
	SoupMessage *message;
       
	message = build_message (connection, path, need_hash, version, req_id, send_close);
	if (message == NULL) {
		rb_debug ("Error building message for http://%s:%d/%s", 
			  priv->base_uri->host,
			  priv->base_uri->port,
			  path);
		return FALSE;
	}
	
	priv->response_handler = handler;
	soup_session_queue_message (priv->session, message,
				    (SoupMessageCallbackFn) http_response_handler, 
				    connection);
	rb_debug ("Queued message for http://%s:%d/%s",
		  priv->base_uri->host,
		  priv->base_uri->port,
		  path);
	return TRUE;
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

static void
handle_server_info (RBDAAPConnection *connection, guint status, GNode *structure)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);
	RBDAAPItem *item = NULL;

	if (!SOUP_STATUS_IS_SUCCESSFUL (status) || structure == NULL) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}
	
	/* get the daap version number */
	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_APRO);
	if (item == NULL) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	priv->daap_version = g_value_get_double (&(item->content));
	rb_daap_connection_state_done (connection, TRUE);
}

static void
handle_login (RBDAAPConnection *connection, guint status, GNode *structure)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);
	RBDAAPItem *item = NULL;

	if (status == SOUP_STATUS_UNAUTHORIZED || status == SOUP_STATUS_FORBIDDEN) {
		rb_debug ("Incorrect password");
		priv->state = DAAP_GET_PASSWORD;
		rb_daap_connection_do_something (connection);
	}

	if (structure == NULL || SOUP_STATUS_IS_SUCCESSFUL (status) == FALSE) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MLID);
	if (item == NULL) {
		rb_debug ("Could not find daap.sessionid item in /login");
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	priv->session_id = g_value_get_int (&(item->content));
	rb_daap_connection_state_done (connection, TRUE);
}

static void
handle_update (RBDAAPConnection *connection, guint status, GNode *structure)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);
	RBDAAPItem *item;

	if (structure == NULL || SOUP_STATUS_IS_SUCCESSFUL (status) == FALSE) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	/* get a revision number */
	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MUSR);
	if (item == NULL) {
		rb_debug ("Could not find daap.serverrevision item in /update");
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	priv->revision_number = g_value_get_int (&(item->content));
	rb_daap_connection_state_done (connection, TRUE);
}

static void 
handle_database_info (RBDAAPConnection *connection, guint status, GNode *structure)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);
	RBDAAPItem *item = NULL;
	GNode *listing_node;
	gint n_databases = 0;

	/* get a list of databases, there should be only 1 */

	if (structure == NULL || SOUP_STATUS_IS_SUCCESSFUL (status) == FALSE) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MRCO);
	if (item == NULL) {
		rb_debug ("Could not find dmap.returnedcount item in /databases");
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	n_databases = g_value_get_int (&(item->content));
	if (n_databases != 1) {
		rb_debug ("Host seems to have more than 1 database, how strange\n");
	}
	
	listing_node = rb_daap_structure_find_node (structure, RB_DAAP_CC_MLCL);
	if (listing_node == NULL) {
		rb_debug ("Could not find dmap.listing item in /databases");
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	item = rb_daap_structure_find_item (listing_node->children, RB_DAAP_CC_MIID);
	if (item == NULL) {
		rb_debug ("Could not find dmap.itemid item in /databases");
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}
	
	priv->database_id = g_value_get_int (&(item->content));
	rb_daap_connection_state_done (connection, TRUE);
}

static void
handle_song_listing (RBDAAPConnection *connection, guint status, GNode *structure)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);
	RBDAAPItem *item = NULL;
	GNode *listing_node;
	gint returned_count;
	gint i;
	GNode *n;
	gint specified_total_count;
	gboolean update_type;

	/* get the songs */
	
	if (structure == NULL || SOUP_STATUS_IS_SUCCESSFUL (status) == FALSE) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MRCO);
	if (item == NULL) {
		rb_debug ("Could not find dmap.returnedcount item in /databases/%d/items",
			  priv->database_id);
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}
	returned_count = g_value_get_int (&(item->content));
	
	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MTCO);
	if (item == NULL) {
		rb_debug ("Could not find dmap.specifiedtotalcount item in /databases/%d/items",
			  priv->database_id);
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}
	specified_total_count = g_value_get_int (&(item->content));
	
	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MUTY);
	if (item == NULL) {
		rb_debug ("Could not find dmap.updatetype item in /databases/%d/items",
			  priv->database_id);
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}
	update_type = g_value_get_char (&(item->content));

	listing_node = rb_daap_structure_find_node (structure, RB_DAAP_CC_MLCL);
	if (listing_node == NULL) {
		rb_debug ("Could not find dmap.listing item in /databases/%d/items",
			  priv->database_id);
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	priv->item_id_to_uri = g_hash_table_new_full ((GHashFunc)g_direct_hash,(GEqualFunc)g_direct_equal, NULL, g_free);
	
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
			uri = g_strdup_printf ("%s/databases/%d/items/%d.%s?session-id=%d", 
					       priv->daap_base_uri, 
					       priv->database_id, 
					       item_id, format, 
					       priv->session_id);
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
		entry = rhythmdb_entry_new (priv->db, priv->db_type, uri);
		g_hash_table_insert (priv->item_id_to_uri, GINT_TO_POINTER (item_id), uri);

		 /* track number */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value,(gulong)track_number);
		rhythmdb_entry_set_uninserted (priv->db, entry, RHYTHMDB_PROP_TRACK_NUMBER, &value);
		g_value_unset (&value);

		/* disc number */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value,(gulong)disc_number);
		rhythmdb_entry_set_uninserted (priv->db, entry, RHYTHMDB_PROP_DISC_NUMBER, &value);
		g_value_unset (&value);

		/* bitrate */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value,(gulong)bitrate);
		rhythmdb_entry_set_uninserted (priv->db, entry, RHYTHMDB_PROP_BITRATE, &value);
		g_value_unset (&value);
		
		/* length */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value,(gulong)length / 1000);
		rhythmdb_entry_set_uninserted (priv->db, entry, RHYTHMDB_PROP_DURATION, &value);
		g_value_unset (&value);

		/* file size */
		g_value_init (&value, G_TYPE_UINT64);
		g_value_set_uint64(&value,(gint64)size);
		rhythmdb_entry_set_uninserted (priv->db, entry, RHYTHMDB_PROP_FILE_SIZE, &value);
		g_value_unset (&value);

		/* title */
		entry_set_string_prop (priv->db, entry, RHYTHMDB_PROP_TITLE, title);

		/* album */
		entry_set_string_prop (priv->db, entry, RHYTHMDB_PROP_ALBUM, album);

		/* artist */
		entry_set_string_prop (priv->db, entry, RHYTHMDB_PROP_ARTIST, artist);

		/* genre */
		entry_set_string_prop (priv->db, entry, RHYTHMDB_PROP_GENRE, genre);
	}

	rhythmdb_commit (priv->db);
		
	rb_daap_connection_state_done (connection, TRUE);
}

/* FIXME
 * what we really should do is only get a list of playlists and their ids
 * then when they are clicked on ('activate'd) by the user, get a list of
 * the files that are actually in them.  This will speed up initial daap 
 * connection times and reduce memory consumption.
 */

static void
handle_playlists (RBDAAPConnection *connection, guint status, GNode *structure)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);
	GNode *listing_node;
	gint i;
	GNode *n;
	
	if (structure == NULL || SOUP_STATUS_IS_SUCCESSFUL (status) == FALSE) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	listing_node = rb_daap_structure_find_node (structure, RB_DAAP_CC_MLCL);
	if (listing_node == NULL) {
		rb_debug ("Could not find dmap.listing item in /databases/%d/containers", 
			  priv->database_id);
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	for (i = 0, n = listing_node->children; n; n = n->next, i++) {
		RBDAAPItem *item;
		gint id;
		gchar *name;
		RBDAAPPlaylist *playlist;
		
		item = rb_daap_structure_find_item (n, RB_DAAP_CC_ABPL);
		if (item != NULL) {
			continue;
		}

		item = rb_daap_structure_find_item (n, RB_DAAP_CC_MIID);
		if (item == NULL) {
			rb_debug ("Could not find dmap.itemid item in /databases/%d/containers",
				  priv->database_id);
			continue;
		}
		id = g_value_get_int (&(item->content));

		item = rb_daap_structure_find_item (n, RB_DAAP_CC_MINM);
		if (item == NULL) {
			rb_debug ("Could not find dmap.itemname item in /databases/%d/containers",
				  priv->database_id);
			continue;
		}
		name = g_value_dup_string (&(item->content));

		playlist = g_new0 (RBDAAPPlaylist, 1);
		playlist->id = id;
		playlist->name = name;
		rb_debug ("Got playlist %p: name %s, id %d", playlist, playlist->name, playlist->id);

		priv->playlists = g_slist_prepend (priv->playlists, playlist);
	}

	rb_daap_connection_state_done (connection, TRUE);
}

static void
handle_playlist_entries (RBDAAPConnection *connection, guint status, GNode *structure)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);
	RBDAAPPlaylist *playlist;
	GNode *listing_node;
	GNode *node;
	gint i;
	GList *playlist_uris = NULL;

	if (structure == NULL || SOUP_STATUS_IS_SUCCESSFUL (status) == FALSE) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	playlist = (RBDAAPPlaylist *)g_slist_nth_data (priv->playlists, priv->reading_playlist);
	g_assert (playlist);

	listing_node = rb_daap_structure_find_node (structure, RB_DAAP_CC_MLCL);
	if (listing_node == NULL) {
		rb_debug ("Could not find dmap.listing item in /databases/%d/containers/%d/items", 
			  priv->database_id, playlist->id);
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	for (i = 0, node = listing_node->children; node; node = node->next, i++) {
		gchar *item_uri;
		gint playlist_item_id;
		RBDAAPItem *item;

		item = rb_daap_structure_find_item (node, RB_DAAP_CC_MIID);
		if (item == NULL) {
			rb_debug ("Could not find dmap.itemid item in /databases/%d/containers/%d/items",
				  priv->database_id, playlist->id);
			continue;
		}
		playlist_item_id = g_value_get_int (&(item->content));
	
		item_uri = g_hash_table_lookup (priv->item_id_to_uri, GINT_TO_POINTER (playlist_item_id));
		if (item_uri == NULL) {
			rb_debug ("Entry %d in playlist %s doesn't exist in the database\n", 
				  playlist_item_id, playlist->name);
			continue;
		}
		
		playlist_uris = g_list_prepend (playlist_uris, g_strdup (item_uri));
	}

	playlist->uris = playlist_uris;
	rb_daap_connection_state_done (connection, TRUE);
}

static void
handle_logout (RBDAAPConnection *connection, guint status, GNode *structure)
{
	/* is there any point handling errors here? */
	rb_daap_connection_state_done (connection, TRUE);
}
	
RBDAAPConnection * 
rb_daap_connection_new (const gchar *name,
			const gchar *host,
			gint port, 
			gboolean password_protected,
			RhythmDB *db, 
			RhythmDBEntryType type,
			RBDAAPConnectionCallback callback,
			gpointer user_data)
{
	return g_object_new (RB_TYPE_DAAP_CONNECTION,
			     "name", name,
			     "entry-type", type,
			     "password-protected", password_protected,
			     "callback", callback,
			     "callback-data", user_data,
			     "db", db,
			     "host", host,
			     "port", port,
			     NULL);
}

static GObject *
rb_daap_connection_constructor (GType type, guint n_construct_properties,
				GObjectConstructParam *construct_properties)
{
	RBDAAPConnection *connection;
	RBDaapConnectionPrivate *priv;
	gchar *path = NULL;

	connection = RB_DAAP_CONNECTION (G_OBJECT_CLASS(rb_daap_connection_parent_class)->
			constructor (type, n_construct_properties, construct_properties));

	priv = DAAP_CONNECTION_GET_PRIVATE (connection);
	priv->result = TRUE;

	rb_debug ("Creating new DAAP connection to %s:%d", priv->host, priv->port);

	priv->session = soup_session_async_new ();
	path = g_strdup_printf ("http://%s:%d", priv->host, priv->port);
	priv->base_uri = soup_uri_new (path);
	g_free (path);

	if (priv->base_uri == NULL) {
		rb_debug ("Error parsing http://%s:%d", priv->host, priv->port);
		g_object_unref (G_OBJECT (connection));
		return NULL;
	}

	priv->daap_base_uri = g_strdup_printf ("daap://%s:%d", priv->host, priv->port);

	priv->state = DAAP_GET_INFO;
	rb_daap_connection_do_something (connection);

	return G_OBJECT (connection);
}

void
rb_daap_connection_logout (RBDAAPConnection *connection,
			   RBDAAPConnectionCallback callback,
			   gpointer user_data)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);
	int old_state = priv->state;

	if (priv->state == DAAP_LOGOUT)
		return;
	
	priv->state = DAAP_LOGOUT;

	if (old_state <= DAAP_LOGIN) {
		/* we haven't logged in yet, so we don't need to do anything here */
		rb_daap_connection_state_done (connection, TRUE);
	} else {
		priv->callback = callback;
		priv->callback_user_data = user_data;
		priv->result = TRUE;
		
		rb_daap_connection_do_something (connection);
	}
}

static void
rb_daap_connection_state_done (RBDAAPConnection *connection, gboolean result)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);

	if (result == FALSE) {
		priv->state = DAAP_DONE;
		priv->result = FALSE;
	} else {
		switch (priv->state) {
		case DAAP_GET_PLAYLISTS:
			if (priv->playlists == NULL)
				priv->state = DAAP_DONE;
			else
				priv->state = DAAP_GET_PLAYLIST_ENTRIES;
			break;
		case DAAP_GET_PLAYLIST_ENTRIES:
			/* keep reading playlists until we've got them all */
			if (++priv->reading_playlist >= g_slist_length (priv->playlists))
				priv->state = DAAP_DONE;
			break;

		case DAAP_LOGOUT:
			priv->state = DAAP_DONE;
			break;

		case DAAP_DONE:
			/* uhh.. */
			rb_debug ("This should never happen.");
			break;

		default:
			/* in most states, we just move on to the next */
			if (priv->state > DAAP_DONE) {
				rb_debug ("This should REALLY never happen.");
				return;
			}
			priv->state++;
			break;
		}
	}

	rb_daap_connection_do_something (connection);
}

static void
rb_daap_connection_do_something (RBDAAPConnection *connection)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);
	char *path;

	switch (priv->state) {
	case DAAP_GET_INFO:
		rb_debug ("Getting DAAP server info");
		if (!http_get (connection, "/server-info", FALSE, 0.0, 0, FALSE, 
			       (RBDAAPResponseHandler) handle_server_info)) {
			rb_debug ("Could not get DAAP connection info");
			rb_daap_connection_state_done (connection, FALSE);
		}
		break;
	
	case DAAP_GET_PASSWORD:
		if (priv->password_protected) {
			/* FIXME this bit is still synchronous */
			rb_debug ("Need a password for %s", priv->name);
			priv->password = connection_get_password (connection);
			if (priv->password == NULL || priv->password[0] == '\0') {
				rb_debug ("Password entry canceled");
				priv->result = FALSE;
				priv->state = DAAP_DONE;
				rb_daap_connection_do_something (connection);
				return;
			}

			/* If the share went away while we were asking for the password,
			 * don't bother trying to log in.
			 */
			if (priv->state != DAAP_GET_PASSWORD) {
				return;
			}
		}

		/* otherwise, fall through */
		priv->state = DAAP_LOGIN;
		
	case DAAP_LOGIN:
		rb_debug ("Logging into DAAP server");
		if (!http_get (connection, "/login", FALSE, 0.0, 0, FALSE, 
			       (RBDAAPResponseHandler) handle_login)) {
			rb_debug ("Could not login to DAAP server");
			rb_daap_connection_state_done (connection, FALSE);
		}
		break;

	case DAAP_GET_REVISION_NUMBER:
		rb_debug ("Getting DAAP server database revision number");
		path = g_strdup_printf ("/update?session-id=%d&revision-number=1", priv->session_id);
		if (!http_get (connection, path, TRUE, priv->daap_version, 0, FALSE, 
			       (RBDAAPResponseHandler) handle_update)) {
			rb_debug ("Could not get server database revision number");
			rb_daap_connection_state_done (connection, FALSE);
		}
		g_free (path);
		break;

	case DAAP_GET_DB_INFO:
		rb_debug ("Getting DAAP database info");
		path = g_strdup_printf ("/databases?session-id=%d&revision-number=%d", 
					priv->session_id, priv->revision_number);
		if (!http_get (connection, path, TRUE, priv->daap_version, 0, FALSE, 
			       (RBDAAPResponseHandler) handle_database_info)) {
			rb_debug ("Could not get DAAP database info");
			rb_daap_connection_state_done (connection, FALSE);
		}
		g_free (path);
		break;

	case DAAP_GET_SONGS:
		rb_debug ("Getting DAAP song listing");
		path = g_strdup_printf ("/databases/%i/items?session-id=%i&revision-number=%i"
				        "&meta=dmap.itemid,dmap.itemname,daap.songalbum,"
					"daap.songartist,daap.daap.songgenre,daap.songsize,"
					"daap.songtime,daap.songtrackcount,daap.songtracknumber,"
					"daap.songyear,daap.songformat,daap.songgenre,"
					"daap.songbitrate", 
					priv->database_id, 
					priv->session_id, 
					priv->revision_number);
		if (!http_get (connection, path, TRUE, priv->daap_version, 0, FALSE, 
			       (RBDAAPResponseHandler) handle_song_listing)) {
			rb_debug ("Could not get DAAP song listing");
			rb_daap_connection_state_done (connection, FALSE);
		}
		g_free (path);
		break;

	case DAAP_GET_PLAYLISTS:
		rb_debug ("Getting DAAP playlists");
		path = g_strdup_printf ("/databases/%d/containers?session-id=%d&revision-number=%d", 
					priv->database_id, 
					priv->session_id, 
					priv->revision_number);
		if (!http_get (connection, path, TRUE, priv->daap_version, 0, FALSE, 
			       (RBDAAPResponseHandler) handle_playlists)) {
			rb_debug ("Could not get DAAP playlists");
			rb_daap_connection_state_done (connection, FALSE);
		}
		g_free (path);
		break;

	case DAAP_GET_PLAYLIST_ENTRIES:
		{
			RBDAAPPlaylist *playlist = 
				(RBDAAPPlaylist *) g_slist_nth_data (priv->playlists, 
								     priv->reading_playlist);
			g_assert (playlist);
			rb_debug ("Reading DAAP playlist %d entries", priv->reading_playlist);
			path = g_strdup_printf ("/databases/%d/containers/%d/items?session-id=%d&revision-number=%d&meta=dmap.itemid", 
						priv->database_id, 
						playlist->id,
						priv->session_id, priv->revision_number);
			if (!http_get (connection, path, TRUE, priv->daap_version, 0, FALSE, 
				       (RBDAAPResponseHandler) handle_playlist_entries)) {
				rb_debug ("Could not get entries for DAAP playlist %d", 
					  priv->reading_playlist);
				rb_daap_connection_state_done (connection, FALSE);
			}
			g_free (path);
		}
		break;

	case DAAP_LOGOUT:
		rb_debug ("Logging out of DAAP server");
		path = g_strdup_printf ("/logout?session-id=%d", priv->session_id);
		if (!http_get (connection, path, TRUE, priv->daap_version, 0, FALSE,
			       (RBDAAPResponseHandler) handle_logout)) {
			rb_debug ("Could not log out of DAAP server");
			rb_daap_connection_state_done (connection, FALSE);
		}
		g_free (path);
		break;

	case DAAP_DONE:
		if (priv->callback) {
			/* do it this way, in case the callback sets another one or destroys the object */
			RBDAAPConnectionCallback callback = priv->callback;
			priv->callback = NULL;
			(*callback) (connection, priv->result, priv->callback_user_data);
		}
		break;
	}
}

gchar * 
rb_daap_connection_get_headers (RBDAAPConnection *connection, 
				const gchar *uri, 
				gint64 bytes)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);
	GString *headers;
	gchar hash[33] = {0};
	gchar *norb_daap_uri = (gchar *)uri;
	gchar *s;
	
	priv->request_id++;
	
	if (g_strncasecmp (uri,"daap://",7) == 0) {
		norb_daap_uri = strstr (uri,"/data");
	}

	rb_daap_hash_generate ((short)floorf (priv->daap_version), 
			       (const guchar*)norb_daap_uri, 2, 
			       (guchar*)hash, 
			       priv->request_id);

	headers = g_string_new ("Accept: */*\r\n"
				"Cache-Control: no-cache\r\n"
				"User-Agent: " RB_DAAP_USER_AGENT "\r\n"
				"Accept-Language: en-us, en;q=5.0\r\n"
				"Client-DAAP-Access-Index: 2\r\n"
				"Client-DAAP-Version: 3.0\r\n");
	g_string_append_printf (headers, 
				"Client-DAAP-Validation: %s\r\n"
				"Client-DAAP-Request-ID: %d\r\n"
				"Connection: close\r\n", 
				hash, priv->request_id);
	if (priv->password_protected) {
		g_string_append_printf (headers, "Authentication: Basic %s\r\n", priv->password);
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
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (connection);

	return priv->playlists;
}


static void 
rb_daap_connection_dispose (GObject *object)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (object);
	GSList *l;

	g_assert (priv->callback == NULL);

	if (priv->name) {
		g_free (priv->name);
		priv->name = NULL;
	}
	
	if (priv->password) {
		g_free (priv->password);
		priv->password = NULL;
	}
	
	if (priv->host) {
		g_free (priv->host);
		priv->host = NULL;
	}
	
	if (priv->playlists) {
		for (l = priv->playlists; l; l = l->next) {
			RBDAAPPlaylist *playlist = l->data;

			g_list_foreach (playlist->uris, (GFunc)g_free, NULL);
			g_list_free (playlist->uris);
			g_free (playlist->name);
			g_free (playlist);
			l->data = NULL;
		}
		g_slist_free (priv->playlists);
		priv->playlists = NULL;
	}

	if (priv->item_id_to_uri) {
		g_hash_table_destroy (priv->item_id_to_uri);
		priv->item_id_to_uri = NULL;
	}
	
	if (priv->session) {
		g_object_unref (G_OBJECT (priv->session));
		priv->session = NULL;
	}

	if (priv->base_uri) {
		soup_uri_free (priv->base_uri);
		priv->base_uri = NULL;
	}

	if (priv->daap_base_uri) {
		g_free (priv->daap_base_uri);
		priv->daap_base_uri = NULL;
	}

	if (priv->db) {
		g_object_unref (G_OBJECT (priv->db));
		priv->db = NULL;
	}
	
	G_OBJECT_CLASS (rb_daap_connection_parent_class)->dispose (object);
}

static void
rb_daap_connection_set_property (GObject *object,
				 guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_free (priv->name);
		priv->name = g_value_dup_string (value);
		break;
	case PROP_DB:
		priv->db = RHYTHMDB (g_value_dup_object (value));
		break;
	case PROP_PASSWORD_PROTECTED:
		priv->password_protected = g_value_get_boolean (value);
		break;
	case PROP_ENTRY_TYPE:
		priv->db_type = g_value_get_uint (value);
		break;
	case PROP_CALLBACK:
		priv->callback = g_value_get_pointer (value);
		break;
	case PROP_CALLBACK_DATA:
		priv->callback_user_data = g_value_get_pointer (value);
		break;
	case PROP_HOST:
		g_free (priv->host);
		priv->host = g_value_dup_string (value);
		break;
	case PROP_PORT:
		priv->port = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_daap_connection_get_property (GObject *object,
				 guint prop_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	RBDaapConnectionPrivate *priv = DAAP_CONNECTION_GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_DB:
		g_value_set_object (value, priv->db);
		break;
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_CALLBACK:
		g_value_set_pointer (value, priv->callback);
		break;
	case PROP_CALLBACK_DATA:
		g_value_set_pointer (value, priv->callback_user_data);
		break;
	case PROP_ENTRY_TYPE:
		g_value_set_uint (value, priv->db_type);
		break;
	case PROP_PASSWORD_PROTECTED:
		g_value_set_boolean (value, priv->password_protected);
		break;
	case PROP_HOST:
		g_value_set_string (value, priv->host);
		break;
	case PROP_PORT:
		g_value_set_uint (value, priv->port);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

