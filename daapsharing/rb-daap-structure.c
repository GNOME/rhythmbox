/*
 *  Header for DAAP (iTunes Music Sharing) structures
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

#include <glib.h>
#include <glib-object.h>
#include <gobject/gvaluecollector.h>
#include "rb-daap-structure.h"
#include "rb-debug.h"

#include <gst/gstutils.h>

#include <string.h>
#include <stdarg.h>

#define MAKE_CONTENT_CODE(ch0, ch1, ch2, ch3) \
    (( (gint32)(gchar)(ch0) | ( (gint32)(gchar)(ch1) << 8 ) | \
    ( (gint32)(gchar)(ch2) << 16 ) | \
    ( (gint32)(gchar)(ch3) << 24 ) ))

static const RBDAAPContentCodeDefinition cc_defs[] = {
	{RB_DAAP_CC_MDCL, MAKE_CONTENT_CODE('m','d','c','l'), "dmap.dictionary", "mdcl", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_MSTT, MAKE_CONTENT_CODE('m','s','t','t'), "dmap.status", "mstt", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_MIID, MAKE_CONTENT_CODE('m','i','i','d'), "dmap.itemid", "miid", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_MINM, MAKE_CONTENT_CODE('m','i','n','m'), "dmap.itemname", "minm", RB_DAAP_TYPE_STRING},
	{RB_DAAP_CC_MIKD, MAKE_CONTENT_CODE('m','i','k','d'), "dmap.itemkind", "mikd", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_MPER, MAKE_CONTENT_CODE('m','p','e','r'), "dmap.persistentid", "mper", RB_DAAP_TYPE_INT64},
	{RB_DAAP_CC_MCON, MAKE_CONTENT_CODE('m','c','o','n'), "dmap.container", "mcon", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_MCTI, MAKE_CONTENT_CODE('m','c','t','i'), "dmap.containeritemid", "mcti", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_MPCO, MAKE_CONTENT_CODE('m','p','c','o'), "dmap.parentcontainerid", "mpco", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_MSTS, MAKE_CONTENT_CODE('m','s','t','s'), "dmap.statusstring", "msts", RB_DAAP_TYPE_STRING},
	{RB_DAAP_CC_MIMC, MAKE_CONTENT_CODE('m','i','m','c'), "dmap.itemcount", "mimc", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_MCTC, MAKE_CONTENT_CODE('m','c','t','c'), "dmap.containercount", "mctc", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_MRCO, MAKE_CONTENT_CODE('m','r','c','o'), "dmap.returnedcount", "mrco", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_MTCO, MAKE_CONTENT_CODE('m','t','c','o'), "dmap.specifiedtotalcount", "mtco", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_MLCL, MAKE_CONTENT_CODE('m','l','c','l'), "dmap.listing", "mlcl", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_MLIT, MAKE_CONTENT_CODE('m','l','i','t'), "dmap.listingitem", "mlit", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_MBCL, MAKE_CONTENT_CODE('m','b','c','l'), "dmap.bag", "mbcl", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_MSRV, MAKE_CONTENT_CODE('m','s','r','v'), "dmap.serverinforesponse", "msrv", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_MSAU, MAKE_CONTENT_CODE('m','s','a','u'), "dmap.authenticationmethod", "msau", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_MSLR, MAKE_CONTENT_CODE('m','s','l','r'), "dmap.loginrequired", "mslr", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_MPRO, MAKE_CONTENT_CODE('m','p','r','o'), "dmap.protocolversion", "mpro", RB_DAAP_TYPE_VERSION},
	{RB_DAAP_CC_APRO, MAKE_CONTENT_CODE('a','p','r','o'), "daap.protocolversion", "apro", RB_DAAP_TYPE_VERSION},
	{RB_DAAP_CC_MSAL, MAKE_CONTENT_CODE('m','s','a','l'), "dmap.supportsautologout", "msal", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_MSUP, MAKE_CONTENT_CODE('m','s','u','p'), "dmap.supportsupdate", "msup", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_MSPI, MAKE_CONTENT_CODE('m','s','p','i'), "dmap.supportspersistenids", "mspi", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_MSEX, MAKE_CONTENT_CODE('m','s','e','x'), "dmap.supportsextensions", "msex", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_MSBR, MAKE_CONTENT_CODE('m','s','b','r'), "dmap.supportsbrowse", "msbr", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_MSQY, MAKE_CONTENT_CODE('m','s','q','y'), "dmap.supportsquery", "msqy", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_MSIX, MAKE_CONTENT_CODE('m','s','i','x'), "dmap.supportsindex", "msix", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_MSRS, MAKE_CONTENT_CODE('m','s','r','s'), "dmap.supportsresolve", "msrs", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_MSTM, MAKE_CONTENT_CODE('m','s','t','m'), "dmap.timeoutinterval", "mstm", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_MSDC, MAKE_CONTENT_CODE('m','s','d','c'), "dmap.databasescount", "msdc", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_MCCR, MAKE_CONTENT_CODE('m','c','c','r'), "dmap.contentcodesresponse", "mccr", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_MCNM, MAKE_CONTENT_CODE('m','c','n','m'), "dmap.contentcodesnumber", "mcnm", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_MCNA, MAKE_CONTENT_CODE('m','c','n','a'), "dmap.contentcodesname", "mcna", RB_DAAP_TYPE_STRING},
	{RB_DAAP_CC_MCTY, MAKE_CONTENT_CODE('m','c','t','y'), "dmap.contentcodestype", "mcty", RB_DAAP_TYPE_SHORT},
	{RB_DAAP_CC_MLOG, MAKE_CONTENT_CODE('m','l','o','g'), "dmap.loginresponse", "mlog", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_MLID, MAKE_CONTENT_CODE('m','l','i','d'), "dmap.sessionid", "mlid", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_MUPD, MAKE_CONTENT_CODE('m','u','p','d'), "dmap.updateresponse", "mupd", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_MUSR, MAKE_CONTENT_CODE('m','u','s','r'), "dmap.serverrevision", "musr", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_MUTY, MAKE_CONTENT_CODE('m','u','t','y'), "dmap.updatetype", "muty", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_MUDL, MAKE_CONTENT_CODE('m','u','d','l'), "dmap.deletedidlisting", "mudl", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_AVDB, MAKE_CONTENT_CODE('a','v','d','b'), "daap.serverdatabases", "avdb", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_ABRO, MAKE_CONTENT_CODE('a','b','r','o'), "daap.databasebrowse", "abro", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_ABAL, MAKE_CONTENT_CODE('a','b','a','l'), "daap.browsealbumlisting", "abal", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_ABAR, MAKE_CONTENT_CODE('a','b','a','r'), "daap.browseartistlisting", "abar", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_ABCP, MAKE_CONTENT_CODE('a','b','c','p'), "daap.browsecomposerlisting", "abcp", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_ABGN, MAKE_CONTENT_CODE('a','b','g','n'), "daap.browsegenrelisting", "abgn", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_ADBS, MAKE_CONTENT_CODE('a','d','b','s'), "daap.returndatabasesongs", "adbs", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_ASAL, MAKE_CONTENT_CODE('a','s','a','l'), "daap.songalbum", "asal", RB_DAAP_TYPE_STRING},
	{RB_DAAP_CC_ASAR, MAKE_CONTENT_CODE('a','s','a','r'), "daap.songartist", "asar", RB_DAAP_TYPE_STRING},
	{RB_DAAP_CC_ASBT, MAKE_CONTENT_CODE('a','s','b','t'), "daap.songsbeatsperminute", "asbt", RB_DAAP_TYPE_SHORT},
	{RB_DAAP_CC_ASBR, MAKE_CONTENT_CODE('a','s','b','r'), "daap.songbitrate", "asbr", RB_DAAP_TYPE_SHORT},
	{RB_DAAP_CC_ASCM, MAKE_CONTENT_CODE('a','s','c','m'), "daap.songcomment", "ascm", RB_DAAP_TYPE_STRING},
	{RB_DAAP_CC_ASCO, MAKE_CONTENT_CODE('a','s','c','o'), "daap.songcompliation", "asco", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_ASDA, MAKE_CONTENT_CODE('a','s','d','a'), "daap.songdateadded", "asda", RB_DAAP_TYPE_DATE},
	{RB_DAAP_CC_ASDM, MAKE_CONTENT_CODE('a','s','d','m'), "daap.songdatemodified", "asdm", RB_DAAP_TYPE_DATE},
	{RB_DAAP_CC_ASDC, MAKE_CONTENT_CODE('a','s','d','c'), "daap.songdisccount", "asdc", RB_DAAP_TYPE_SHORT},
	{RB_DAAP_CC_ASDN, MAKE_CONTENT_CODE('a','s','d','n'), "daap.songdiscnumber", "asdn", RB_DAAP_TYPE_SHORT},
	{RB_DAAP_CC_ASDB, MAKE_CONTENT_CODE('a','s','d','b'), "daap.songdisabled", "asdb", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_ASEQ, MAKE_CONTENT_CODE('a','s','e','q'), "daap.songeqpreset", "aseq", RB_DAAP_TYPE_STRING},
	{RB_DAAP_CC_ASFM, MAKE_CONTENT_CODE('a','s','f','m'), "daap.songformat", "asfm", RB_DAAP_TYPE_STRING},
	{RB_DAAP_CC_ASGN, MAKE_CONTENT_CODE('a','s','g','n'), "daap.songgenre", "asgn", RB_DAAP_TYPE_STRING},
	{RB_DAAP_CC_ASDT, MAKE_CONTENT_CODE('a','s','d','t'), "daap.songdescription", "asdt", RB_DAAP_TYPE_STRING},
	{RB_DAAP_CC_ASRV, MAKE_CONTENT_CODE('a','s','r','v'), "daap.songrelativevolume", "asrv", RB_DAAP_TYPE_SIGNED_INT},
	{RB_DAAP_CC_ASSR, MAKE_CONTENT_CODE('a','s','s','r'), "daap.songsamplerate", "assr", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_ASSZ, MAKE_CONTENT_CODE('a','s','s','z'), "daap.songsize", "assz", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_ASST, MAKE_CONTENT_CODE('a','s','s','t'), "daap.songstarttime", "asst", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_ASSP, MAKE_CONTENT_CODE('a','s','s','p'), "daap.songstoptime", "assp", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_ASTM, MAKE_CONTENT_CODE('a','s','t','m'), "daap.songtime", "astm", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_ASTC, MAKE_CONTENT_CODE('a','s','t','c'), "daap.songtrackcount", "astc", RB_DAAP_TYPE_SHORT},
	{RB_DAAP_CC_ASTN, MAKE_CONTENT_CODE('a','s','t','n'), "daap.songtracknumber", "astn", RB_DAAP_TYPE_SHORT},
	{RB_DAAP_CC_ASUR, MAKE_CONTENT_CODE('a','s','u','r'), "daap.songuserrating", "asur", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_ASYR, MAKE_CONTENT_CODE('a','s','y','r'), "daap.songyear", "asyr", RB_DAAP_TYPE_SHORT},
	{RB_DAAP_CC_ASDK, MAKE_CONTENT_CODE('a','s','d','k'), "daap.songdatakind", "asdk", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_ASUL, MAKE_CONTENT_CODE('a','s','u','l'), "daap.songdataurl", "asul", RB_DAAP_TYPE_STRING},
	{RB_DAAP_CC_APLY, MAKE_CONTENT_CODE('a','p','l','y'), "daap.databaseplaylists", "aply", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_ABPL, MAKE_CONTENT_CODE('a','b','p','l'), "daap.baseplaylist", "abpl", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_APSO, MAKE_CONTENT_CODE('a','p','s','o'), "daap.playlistsongs", "apso", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_PRSV, MAKE_CONTENT_CODE('p','r','s','v'), "daap.resolve", "prsv", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_ARIF, MAKE_CONTENT_CODE('a','r','i','f'), "daap.resolveinfo", "arif", RB_DAAP_TYPE_CONTAINER},
	{RB_DAAP_CC_AESV, MAKE_CONTENT_CODE('a','e','S','V'), "com.applie.itunes.music-sharing-version", "aesv", RB_DAAP_TYPE_INT},
	{RB_DAAP_CC_MSAS, MAKE_CONTENT_CODE('m','s','a','s'), "daap.authentication.schemes", "msas", RB_DAAP_TYPE_BYTE},
	{RB_DAAP_CC_AGRP, MAKE_CONTENT_CODE('a','g','r','p'), "daap.songgrouping", "agrp", RB_DAAP_TYPE_STRING},
	{RB_DAAP_CC_ASCP, MAKE_CONTENT_CODE('a','s','c','p'), "daap.songcomposer", "ascp", RB_DAAP_TYPE_STRING}
	};


const gchar * 
rb_daap_content_code_name (RBDAAPContentCode code)
{
	return cc_defs[code-1].name;
}

RBDAAPType 
rb_daap_content_code_rb_daap_type (RBDAAPContentCode code)
{
	return cc_defs[code-1].type;
}

const gchar * 
rb_daap_content_code_string (RBDAAPContentCode code)
{
	return cc_defs[code-1].string;
}
			
static GType
rb_daap_content_code_gtype (RBDAAPContentCode code)
{
	switch (rb_daap_content_code_rb_daap_type (code)) {
		case RB_DAAP_TYPE_BYTE:
		case RB_DAAP_TYPE_SIGNED_INT:
			return G_TYPE_CHAR;
		case RB_DAAP_TYPE_SHORT:
		case RB_DAAP_TYPE_INT:
		case RB_DAAP_TYPE_DATE:
			return G_TYPE_INT;
		case RB_DAAP_TYPE_INT64:
			return G_TYPE_INT64;
		case RB_DAAP_TYPE_VERSION:
			return G_TYPE_DOUBLE;
		case RB_DAAP_TYPE_STRING:
			return G_TYPE_STRING;
		case RB_DAAP_TYPE_CONTAINER:
		default:
			return G_TYPE_NONE;
	}
}

GNode * 
rb_daap_structure_add (GNode *parent, 
		       RBDAAPContentCode cc, 
		       ...)
{
	RBDAAPType rb_daap_type;
	GType gtype;
	RBDAAPItem *item;
	va_list list;
	GNode *node;
	gchar *error = NULL;
	
	va_start (list, cc);

	rb_daap_type = rb_daap_content_code_rb_daap_type (cc);
	gtype = rb_daap_content_code_gtype (cc);

	item = g_new0(RBDAAPItem, 1);
	item->content_code = cc;
	
	if (gtype != G_TYPE_NONE) {
		g_value_init (&(item->content), gtype);
	}

	if (rb_daap_type != RB_DAAP_TYPE_STRING && rb_daap_type != RB_DAAP_TYPE_CONTAINER) {
		G_VALUE_COLLECT (&(item->content), list, G_VALUE_NOCOPY_CONTENTS, &error);
		if (error) {
			g_warning (error);
			g_free (error);
		}
	}

	switch (rb_daap_type) {
		case RB_DAAP_TYPE_BYTE: 
		case RB_DAAP_TYPE_SIGNED_INT:
			item->size = 1;
			break;
		case RB_DAAP_TYPE_SHORT: 
			item->size = 2;
			break;
		case RB_DAAP_TYPE_DATE:
		case RB_DAAP_TYPE_INT:
		case RB_DAAP_TYPE_VERSION:
			item->size = 4;
			break;
		case RB_DAAP_TYPE_INT64: 
			item->size = 8;
			break;
		case RB_DAAP_TYPE_STRING: {
			gchar *s = va_arg (list, gchar *);

			g_value_set_string (&(item->content), s);

			/* we dont use G_VALUE_COLLECT for this because we also
			 * need the length */
			item->size = strlen (s);
			break;
		}
		case RB_DAAP_TYPE_CONTAINER:
		default:
			break;
	}
	
	node = g_node_new (item);
	
	if (parent) {
		g_node_append (parent, node);

		while (parent) {
			RBDAAPItem *parent_item = parent->data;

			parent_item->size += (4 + 4 + item->size);

			parent = parent->parent;
		}
	}

	return node;
}

static gboolean 
rb_daap_structure_node_serialize (GNode *node, 
				  GByteArray *array)
{
	RBDAAPItem *item = node->data;
	RBDAAPType rb_daap_type;
	guint32 size = GINT32_TO_BE (item->size);

	g_byte_array_append (array, (const guint8 *)rb_daap_content_code_string (item->content_code), 4);
	g_byte_array_append (array, (const guint8 *)&size, 4);
	
	rb_daap_type = rb_daap_content_code_rb_daap_type (item->content_code);

	switch (rb_daap_type) {
		case RB_DAAP_TYPE_BYTE: 
		case RB_DAAP_TYPE_SIGNED_INT: {
			gchar c = g_value_get_char (&(item->content));
			
			g_byte_array_append (array, (const guint8 *)&c, 1);
			
			break;
		}
		case RB_DAAP_TYPE_SHORT: {
			gint32 i = g_value_get_int (&(item->content));
			gint16 s = GINT16_TO_BE ((gint16) i);
			
			g_byte_array_append (array, (const guint8 *)&s, 2);

			break;
	        }
		case RB_DAAP_TYPE_DATE: 
		case RB_DAAP_TYPE_INT: {
			gint32 i = g_value_get_int (&(item->content));
			gint32 s = GINT32_TO_BE (i);

			g_byte_array_append (array, (const guint8 *)&s, 4);
			
			break;
		}
		case RB_DAAP_TYPE_VERSION: {
			gdouble v = g_value_get_double (&(item->content));
			gint16 major;
			gint8 minor;
			gint8 patch = 0;

			major = (gint16)v;
			minor = (gint8)(v - ((gdouble)major));

			major = GINT16_TO_BE (major);

			g_byte_array_append (array, (const guint8 *)&major, 2);
			g_byte_array_append (array, (const guint8 *)&minor, 1);
			g_byte_array_append (array, (const guint8 *)&patch, 1);
			
			break;
		}		
		case RB_DAAP_TYPE_INT64: {
			gint64 i = g_value_get_int64 (&(item->content));
			gint64 s = GINT64_TO_BE (i);

			g_byte_array_append (array, (const guint8 *)&s, 8);
			
			break;
		}
		case RB_DAAP_TYPE_STRING: {
			const gchar *s = g_value_get_string (&(item->content));

			g_byte_array_append (array, (const guint8 *)s, strlen (s));
			
			break;
		}
		case RB_DAAP_TYPE_CONTAINER:
		default:
			break;
	}

	return FALSE;
}
	
gchar * 
rb_daap_structure_serialize (GNode *structure, 
			     guint *length)
{
	GByteArray *array;
	gchar *data;

	array = g_byte_array_new ();
	
	if (structure) {
		g_node_traverse (structure, G_PRE_ORDER, G_TRAVERSE_ALL, -1, (GNodeTraverseFunc)rb_daap_structure_node_serialize, array);
	}
	
	data = (gchar *) array->data;
	*length = array->len;
	g_byte_array_free (array, FALSE);
	
	return data;
}

static RBDAAPContentCode 
rb_daap_buffer_read_content_code (const gchar *buf)
{
	gint32 c = MAKE_CONTENT_CODE (buf[0], buf[1], buf[2], buf[3]);
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cc_defs); i++) {
		if (cc_defs[i].int_code == c) {
			return cc_defs[i].code;
		}
	}

	return RB_DAAP_CC_INVALID;
}

#define rb_daap_buffer_read_int8(b)  	GST_READ_UINT8 (b)
#define rb_daap_buffer_read_int16(b) 	(gint16) GST_READ_UINT16_BE (b)
#define rb_daap_buffer_read_int32(b) 	(gint32) GST_READ_UINT32_BE (b)
#define rb_daap_buffer_read_int64(b) 	(gint64) GST_READ_UINT64_BE (b)

static gchar *
rb_daap_buffer_read_string (const gchar *buf, gssize size)
{
	if (g_utf8_validate (buf, size, NULL) == TRUE) {
		return g_strndup (buf, size);
	} else {
		return g_strdup ("");
	}
}

//#define PARSE_DEBUG
#define PARSE_DEBUG_FILE "daapbuffer"

#ifdef PARSE_DEBUG
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

static void 
rb_daap_structure_parse_container_buffer (GNode *parent, 
					  const guchar *buf, 
					  gint buf_length)
{
	gint l = 0;

	while (l < buf_length) {
		RBDAAPContentCode cc;
		gint codesize = 0;
		RBDAAPItem *item = NULL;
		GNode *node = NULL;
		GType gtype;
		
#ifdef PARSE_DEBUG
		g_print ("l is %d and buf_length is %d\n", l, buf_length);
#endif		

		/* we need at least 8 bytes, 4 of content_code and 4 of size */
		if (buf_length - l < 8) {
#ifdef PARSE_DEBUG
			g_print ("Malformed response recieved\n");
#endif
			return;
		}
		
		cc = rb_daap_buffer_read_content_code ((const gchar*)&(buf[l]));
		if (cc == RB_DAAP_CC_INVALID) {
#ifdef PARSE_DEBUG
			g_print ("Invalid content_code recieved\n");
#endif
			return;
		}
		l += 4;

		codesize = rb_daap_buffer_read_int32(&(buf[l]));
		/* CCCCSIZECONTENT
		 * if the buffer length (minus 8 for the content code & size)
		 * is smaller than the read codesize (ie, someone sent us
		 * a codesize that is larger than the remaining data)
		 * then get out before we start processing it
		 */
		if (codesize > buf_length - l - 4 || codesize < 0) {
#ifdef PARSE_DEBUG
			g_print ("Invalid codesize %d recieved in buf_length %d\n", codesize, buf_length);
#endif
			return;
		}
		l += 4;

#ifdef PARSE_DEBUG
		g_print ("content_code = %d, codesize is %d, l is %d\n", cc, codesize, l);
#endif
		
		item = g_new0 (RBDAAPItem, 1);
		item->content_code = cc;
		node = g_node_new (item);
		g_node_append (parent, node);
		
		gtype = rb_daap_content_code_gtype (item->content_code);

		if (gtype != G_TYPE_NONE) {
			g_value_init (&(item->content), gtype);
		}
		
#ifdef PARSE_DEBUG 
		{
			guint i;

			for (i = 2; i < g_node_depth (node); i++) {
				g_print ("\t");
			}
		}
#endif
		
// FIXME USE THE G_TYPE CONVERTOR FUNCTION rb_daap_type_to_gtype
		switch (rb_daap_content_code_rb_daap_type (item->content_code)) {
			case RB_DAAP_TYPE_SIGNED_INT:
			case RB_DAAP_TYPE_BYTE: {
				gchar c = 0;
				
				if (codesize == 1) {
					c = (gchar) rb_daap_buffer_read_int8(&(buf[l]));
				}
				
				g_value_set_char (&(item->content), c);
#ifdef PARSE_DEBUG
				g_print ("Code: %s, content (%d): \"%c\"\n", rb_daap_content_code_string (item->content_code), codesize, (gchar)c);
#endif

				break;
			}
			case RB_DAAP_TYPE_SHORT: {
				gint16 s = 0;

				if (codesize == 2) {
					s = rb_daap_buffer_read_int16(&(buf[l]));
				}

				g_value_set_int (&(item->content),(gint32)s);
#ifdef PARSE_DEBUG
				g_print ("Code: %s, content (%d): %hi\n", rb_daap_content_code_string (item->content_code), codesize, s);
#endif

				break;
			}
			case RB_DAAP_TYPE_DATE:
			case RB_DAAP_TYPE_INT: {
				gint32 i = 0;

				if (codesize == 4) {
					i = rb_daap_buffer_read_int32(&(buf[l]));
				}
				
				g_value_set_int (&(item->content), i);
#ifdef PARSE_DEBUG
				g_print ("Code: %s, content (%d): %d\n", rb_daap_content_code_string (item->content_code), codesize, i);
#endif
				break;
			}
			case RB_DAAP_TYPE_INT64: {
				gint64 i = 0;
		
				if (codesize == 8) {
					i = rb_daap_buffer_read_int16(&(buf[l]));
				}
				
				g_value_set_int64 (&(item->content), i);
#ifdef PARSE_DEBUG
				g_print ("Code: %s, content (%d): %"G_GINT64_FORMAT"\n", rb_daap_content_code_string (item->content_code), codesize, i);
#endif

				break;
			}
			case RB_DAAP_TYPE_STRING: {
				gchar *s = rb_daap_buffer_read_string ((const gchar*)&(buf[l]), codesize);

				g_value_take_string (&(item->content), s);
#ifdef PARSE_DEBUG
				g_print ("Code: %s, content (%d): \"%s\"\n", rb_daap_content_code_string (item->content_code), codesize, s);
#endif

				break;
			}
			case RB_DAAP_TYPE_VERSION: {
				gint16 major = 0;
				gint16 minor = 0;
				gdouble v = 0;

				if (codesize == 4) {
					major = rb_daap_buffer_read_int16(&(buf[l]));
					minor = rb_daap_buffer_read_int16(&(buf[l]) + 2);
				}

				v = (gdouble)major;
				v += (gdouble)(minor * 0.1);
				
				g_value_set_double (&(item->content), v);
#ifdef PARSE_DEBUG
				g_print ("Code: %s, content: %f\n", rb_daap_content_code_string (item->content_code), v);
#endif

				break;
			}
			case RB_DAAP_TYPE_CONTAINER: {
#ifdef PARSE_DEBUG
				g_print ("Code: %s, container\n", rb_daap_content_code_string (item->content_code));
#endif
				rb_daap_structure_parse_container_buffer (node,&(buf[l]), codesize);
				break;
			}
		}

		l += codesize;
	}

	return;
}

GNode * 
rb_daap_structure_parse (const gchar *buf, 
			 gint buf_length)
{
	GNode *root = NULL;
	GNode *child = NULL;

#ifdef PARSE_DEBUG
	{
		int fd;

		fd = open (PARSE_DEBUG_FILE, O_WRONLY | O_CREAT);
		write (fd, (const void *)buf, (size_t)buf_length);
		close (fd);
	}
#endif
	
	root = g_node_new (NULL);

	rb_daap_structure_parse_container_buffer (root, (guchar *)buf, buf_length);

	child = root->children;
	if (child) {
		g_node_unlink (child);
	}
	g_node_destroy (root);
	
	return child;
}

struct NodeFinder {
	RBDAAPContentCode code;
	GNode *node;
};

static gboolean 
gnode_find_node (GNode *node, 
		 gpointer data)
{
	struct NodeFinder *finder = (struct NodeFinder *)data;
	RBDAAPItem *item = node->data;

	if (item->content_code == finder->code) {
		finder->node = node;
		return TRUE;
	}

	return FALSE;
}

RBDAAPItem * 
rb_daap_structure_find_item (GNode *structure, 
			     RBDAAPContentCode code)
{
	GNode *node = NULL;
	
	node = rb_daap_structure_find_node (structure, code);

	if (node) {
		return node->data;
	}

	return NULL;
}

GNode * 
rb_daap_structure_find_node (GNode *structure, 
			     RBDAAPContentCode code)
{
	struct NodeFinder *finder;
	GNode *node = NULL;

	finder = g_new0(struct NodeFinder,1);
	finder->code = code;

	g_node_traverse (structure, G_IN_ORDER, G_TRAVERSE_ALL, -1, gnode_find_node, finder);

	node = finder->node;
	g_free (finder);
	finder = NULL;

	return node;
}



static void
rb_daap_item_free (RBDAAPItem *item)
{
	if (rb_daap_content_code_rb_daap_type (item->content_code) != RB_DAAP_TYPE_CONTAINER) {
		g_value_unset (&(item->content));
	}

	g_free (item);
}

static gboolean
gnode_free_rb_daap_item (GNode *node,
			 gpointer data)
{
	rb_daap_item_free ((RBDAAPItem *)node->data);

	return FALSE;
}

void 
rb_daap_structure_destroy (GNode *structure)
{
	if (structure) {
		g_node_traverse (structure, G_IN_ORDER, G_TRAVERSE_ALL, -1, gnode_free_rb_daap_item, NULL);

		g_node_destroy (structure);

		structure = NULL;
	}
}

const RBDAAPContentCodeDefinition *
rb_daap_content_codes (guint *number)
{
	*number = G_N_ELEMENTS (cc_defs);

	return cc_defs;
}

gint32 
rb_daap_content_code_string_as_int32 (const gchar *str)
{
	union {
		gint32 i;
		gchar str[4];
	} u;

	strncpy (u.str, str, 4);

	return u.i;
}

static gboolean 
print_rb_daap_item (GNode *node, 
		    gpointer data)
{
	RBDAAPItem *item;
	const gchar *name;
	gchar *value;
	gint i;

	for (i = 1; i < g_node_depth (node); i++) {
		g_print ("\t");
	}

	item = node->data;

	name = rb_daap_content_code_name (item->content_code);

	if (G_IS_VALUE (&(item->content))) {
		value = g_strdup_value_contents (&(item->content));
	} else {
		value = g_strdup ("");
	}

	g_print ("%d, %s = %s (%d)\n", g_node_depth (node), name, value, item->size);
	g_free (value);

	return FALSE;
}

void
rb_daap_structure_print (GNode *structure)
{
	if (structure) {
		g_node_traverse (structure, G_PRE_ORDER, G_TRAVERSE_ALL, -1, (GNodeTraverseFunc)print_rb_daap_item, NULL);
	}
}
