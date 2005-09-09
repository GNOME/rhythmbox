/*
 *  Implmentation of DAAP (iTunes Music Sharing) sharing
 *
 *  Copyright (C) 2005 Charles Schmidt <cschmidt2@emich.edu>
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


#include "rb-daap-share.h"
#include "rb-daap-structure.h"
#include "rb-daap-mdns.h"
#include "rb-daap-dialog.h"

#include "rb-playlist-source.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"

#include <libsoup/soup.h>
#include <libsoup/soup-address.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-uri.h>
#include <libsoup/soup-server.h>
#include <libsoup/soup-server-message.h>
#include <libgnomevfs/gnome-vfs.h>

#include <time.h>
#include <string.h>

#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>

static void rb_daap_share_init	        (RBDAAPShare *share);
static void rb_daap_share_set_property  (GObject *object, 
					 guint prop_id, 
					 const GValue *value, 
					 GParamSpec *pspec);
static void rb_daap_share_get_property  (GObject *object, 
					 guint prop_id, 
					 GValue *value, 
				 	 GParamSpec *pspec);
static void rb_daap_share_finalize      (GObject *object);
static void rb_daap_share_class_init    (RBDAAPShareClass *klass);

static void rb_daap_share_start_publish (RBDAAPShare *share);
static void rb_daap_share_stop_publish  (RBDAAPShare *share);

struct RBDAAPSharePrivate {
	gchar *name;
	guint port;

	/* mdns/zeroconf/dns-sd/rendezvous publishing things */
	gboolean published;
	RBDAAPmDNSPublisher publisher;

	/* http server things */
	SoupServer *server;
	guint revision_number;

	/* db things */
	RhythmDB *db;
	gint32 num_songs;
	GHashTable *id_to_entry;
	GHashTable *entry_to_id;
	gulong entry_added_id;
	gulong entry_deleted_id;
	
	/* playlist things */
	RBPlaylistManager *playlist_manager;
};

enum {
	PROP_0,
	PROP_NAME,
	PROP_DB
};

GType 
rb_daap_share_get_type (void)
{
	static GType rb_daap_share_type = 0;

	if (rb_daap_share_type == 0) {
		static const GTypeInfo our_info = {
			sizeof (RBDAAPShareClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_daap_share_class_init,
			NULL,
			NULL,
			sizeof (RBDAAPShare),
			0,
			(GInstanceInitFunc) rb_daap_share_init
		};

		rb_daap_share_type = g_type_register_static (G_TYPE_OBJECT,
		 	 				     "RBDAAPShare",
							     &our_info, 0);

	}

	return rb_daap_share_type;
}

static void
rb_daap_share_class_init (RBDAAPShareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	object_class->get_property = rb_daap_share_get_property;
	object_class->set_property = rb_daap_share_set_property;
	object_class->finalize = rb_daap_share_finalize;

	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
						 	      "Name",
							      "Share Name",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db", 
							      "RhythmDB", 
							      "RhythmDB object", 
							      RHYTHMDB_TYPE,
							       G_PARAM_READABLE));

	return;
}

static void
rb_daap_share_init (RBDAAPShare *share)
{
	share->priv = g_new0 (RBDAAPSharePrivate, 1);
	share->priv->revision_number = 5;

	return;
}

static void
rb_daap_share_set_property (GObject *object, 
			    guint prop_id, 
			    const GValue *value, 
			    GParamSpec *pspec)
{
	RBDAAPShare *share = RB_DAAP_SHARE (object);

	switch (prop_id) {
		case PROP_NAME: {
			gboolean restart_publish = FALSE;
			
			if (share->priv->name && share->priv->published) {
				rb_daap_share_stop_publish (share);
				g_free (share->priv->name);
				restart_publish = TRUE;
			}

			share->priv->name = g_value_dup_string (value);

			if (restart_publish) {
				rb_daap_share_start_publish (share);
			}

			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
rb_daap_share_get_property (GObject *object, 
			    guint prop_id, 
			    GValue *value, 
			    GParamSpec *pspec)
{
	RBDAAPShare *share = RB_DAAP_SHARE (object);

	switch (prop_id) {
		case PROP_NAME:
			g_value_set_string (value, share->priv->name);
			break;
		case PROP_DB:
			g_value_set_object (value, share->priv->db);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}


static void 
rb_daap_share_finalize (GObject *object)
{
	RBDAAPShare *share = RB_DAAP_SHARE (object);

	if (share->priv->published) {
		rb_daap_share_stop_publish (share);
	}
	
	if (share->priv) {
		g_free (share->priv->name);
		g_object_unref (share->priv->db);
		g_object_unref (share->priv->playlist_manager);
		g_free (share->priv);
		share->priv = NULL;
	}

	return;
}


RBDAAPShare * 
rb_daap_share_new (const gchar *name, 
		   RhythmDB *db, 
		   RBPlaylistManager *playlist_manager)
{
	RBDAAPShare *share;

	share = RB_DAAP_SHARE (g_object_new (RB_TYPE_DAAP_SHARE, "name", name, NULL));
	share->priv->db = g_object_ref (db);
	share->priv->playlist_manager = g_object_ref (playlist_manager);

	rb_daap_share_start_publish (share);
	
	return share;
}

static void 
message_add_standard_headers (SoupMessage *message)
{
	gchar *s;
	time_t t;
	struct tm *tm;

	soup_message_add_header (message->response_headers, "DAAP-Server", "Rhythmbox " VERSION);
	
	soup_message_add_header (message->response_headers, "Content-Type", "application/x-dmap-tagged");
	
	t = time (NULL);
	tm = gmtime (&t);
	s = g_new (gchar, 100);
	strftime (s, 100, "%a, %d %b %Y %T GMT", tm);
	soup_message_add_header (message->response_headers, "Date", s);
	g_free (s);

	return;
}

static void 
message_set_from_rb_daap_structure (SoupMessage *message, 
				    GNode *structure)
{
	gchar *resp;
	guint length;

	resp = rb_daap_structure_serialize (structure, &length);

	if (resp == NULL) {
		g_print ("serialize gave us null?\n");
		return;
	}

	message->response.owner = SOUP_BUFFER_SYSTEM_OWNED;
	message->response.length = length;
	message->response.body = resp;
		
	message_add_standard_headers (message);
		
	soup_message_set_status (message, SOUP_STATUS_OK);
	soup_server_message_set_encoding (SOUP_SERVER_MESSAGE (message), SOUP_TRANSFER_CONTENT_LENGTH);

	return;
}

#define DMAP_STATUS_OK 200

#define DMAP_VERSION 2.0
#define DAAP_VERSION 3.0
#define DMAP_TIMEOUT 1800

static void 
server_info_cb (RBDAAPShare *share, 
		SoupMessage *message)
{
/* MSRV	server info response
 * 	MSTT status
 * 	MPRO dmap version
 * 	APRO daap version
 * 	MINM name
 * 	MSAU authentication method
 * 	MSLR login required
 * 	MSTM timeout interval
 * 	MSAL supports auto logout
 * 	MSUP supports update
 * 	MSPI supports persistent ids
 * 	MSEX supports extensions
 * 	MSBR supports browse
 * 	MSQY supports query
 * 	MSIX supports index
 * 	MSRS supports resolve
 * 	MSDC databases count
 */
	GNode *msrv;

	msrv = rb_daap_structure_add (NULL, RB_DAAP_CC_MSRV);
	rb_daap_structure_add (msrv, RB_DAAP_CC_MSTT, (gint32) DMAP_STATUS_OK);
	rb_daap_structure_add (msrv, RB_DAAP_CC_MPRO, (gdouble) DMAP_VERSION);
	rb_daap_structure_add (msrv, RB_DAAP_CC_APRO, (gdouble) DAAP_VERSION);
	/* 2/3 is for itunes 4.8 (at least).  its determined by the
	 * Client-DAAP-Version header sent, but if we decide not to support
	 * older versions..? anyway
	 *
	 * 1.0 is 1/1
	 * 2.0 is 1/2
	 * 3.0 is 2/3
	 */
	rb_daap_structure_add (msrv, RB_DAAP_CC_MINM, share->priv->name);
	rb_daap_structure_add (msrv, RB_DAAP_CC_MSAU, 0);
	/* authentication method
	 * 0 is nothing
	 * 1 is name & password
	 * 2 is password only
	 */
	rb_daap_structure_add (msrv, RB_DAAP_CC_MSLR, 0);
	rb_daap_structure_add (msrv, RB_DAAP_CC_MSTM, (gint32) DMAP_TIMEOUT);
	rb_daap_structure_add (msrv, RB_DAAP_CC_MSAL, (gchar) 0);
	rb_daap_structure_add (msrv, RB_DAAP_CC_MSUP, (gchar) 0);
	rb_daap_structure_add (msrv, RB_DAAP_CC_MSPI, (gchar) 0);
	rb_daap_structure_add (msrv, RB_DAAP_CC_MSEX, (gchar) 0);
	rb_daap_structure_add (msrv, RB_DAAP_CC_MSBR, (gchar) 0);
	rb_daap_structure_add (msrv, RB_DAAP_CC_MSQY, (gchar) 0);
	rb_daap_structure_add (msrv, RB_DAAP_CC_MSIX, (gchar) 0);
	rb_daap_structure_add (msrv, RB_DAAP_CC_MSRS, (gchar) 0);
	rb_daap_structure_add (msrv, RB_DAAP_CC_MSDC, (gint32) 1);

	message_set_from_rb_daap_structure (message, msrv);
	rb_daap_structure_destroy (msrv);
	
	return;
}

static void 
content_codes_cb (RBDAAPShare *share, 
		  SoupMessage *message)
{
/* MCCR content codes response
 * 	MSTT status
 * 	MDCL dictionary
 * 		MCNM content codes number
 * 		MCNA content codes name
 * 		MCTY content codes type
 * 	MDCL dictionary
 * 	...
 */
	const RBDAAPContentCodeDefinition *defs;
	guint num_defs = 0;
	guint i;
	GNode *mccr;
	
	defs = rb_daap_content_codes (&num_defs);

	mccr = rb_daap_structure_add (NULL, RB_DAAP_CC_MCCR);
	rb_daap_structure_add (mccr, RB_DAAP_CC_MSTT, (gint32) DMAP_STATUS_OK);

	for (i = 0; i < num_defs; i++) {
		GNode *mdcl;
		
		mdcl = rb_daap_structure_add (mccr, RB_DAAP_CC_MDCL);
		rb_daap_structure_add (mdcl, RB_DAAP_CC_MCNM, rb_daap_content_code_string_as_int32(defs[i].string));
		rb_daap_structure_add (mdcl, RB_DAAP_CC_MCNA, defs[i].name);
		rb_daap_structure_add (mdcl, RB_DAAP_CC_MCTY, (gint32) defs[i].type);
	}
	
	message_set_from_rb_daap_structure (message, mccr);
	rb_daap_structure_destroy (mccr);

	return;
}

/* This is arbitrary.  iTunes communicates with a session id for
 * reasons relating to updates to the database and such, I think
 * Since we don't do that, and since we don't keep track of connections
 * like iTunes do, everyone can just share the same, special, arbitrary
 * session id.
 */
#define DAAP_SESSION_ID 42
	
static void 
login_cb (RBDAAPShare *share, 
	  SoupMessage *message)
{
/* MLOG login response
 * 	MSTT status
 * 	MLID session id
 */
	GNode *mlog;

	mlog = rb_daap_structure_add (NULL, RB_DAAP_CC_MLOG);
	rb_daap_structure_add (mlog, RB_DAAP_CC_MSTT, (gint32) DMAP_STATUS_OK);
	rb_daap_structure_add (mlog, RB_DAAP_CC_MLID, (gint32) DAAP_SESSION_ID);

	message_set_from_rb_daap_structure (message, mlog);
	rb_daap_structure_destroy (mlog);
	
	return;
}

static void 
update_cb (RBDAAPShare *share, 
	   SoupMessage *message)
{
	gchar *path;
	gchar *revision_number_position;
	guint revision_number;
	
	path = soup_uri_to_string (soup_message_get_uri (message), TRUE);

	revision_number_position = strstr (path, "revision-number=");
	
	if (revision_number_position == NULL) {
		g_print ("client asked for an update without a revision number?!?\n");
		g_free (path);
		return;
	}

	revision_number_position += 16;
	revision_number = atoi (revision_number_position);

	g_free (path);
	
	if (revision_number != share->priv->revision_number) {
		/* MUPD update response
		 * 	MSTT status
		 * 	MUSR server revision
		 */
		GNode *mupd;
		
		mupd = rb_daap_structure_add (NULL, RB_DAAP_CC_MUPD);
		rb_daap_structure_add (mupd, RB_DAAP_CC_MSTT, (gint32) DMAP_STATUS_OK);
		rb_daap_structure_add (mupd, RB_DAAP_CC_MUSR, (gint32) share->priv->revision_number);

		message_set_from_rb_daap_structure (message, mupd);
		rb_daap_structure_destroy (mupd);
	} else {
		g_object_ref (message);
		soup_message_io_pause (message);
	}
	
	return;
}

typedef enum {
	ITEM_ID = 0,
	ITEM_NAME,
	ITEM_KIND,
	PERSISTENT_ID,
	CONTAINER_ITEM_ID,
	SONG_ALBUM,
	SONG_GROUPING,
	SONG_ARTIST,
	SONG_BITRATE,
	SONG_BPM,
	SONG_COMMENT,
	SONG_COMPILATION,
	SONG_COMPOSER,
	SONG_DATA_KIND,
	SONG_DATA_URL,
	SONG_DATE_ADDED,
	SONG_DATE_MODIFIED,
	SONG_DISC_COUNT,
	SONG_DISC_NUMBER,
	SONG_DISABLED,
	SONG_EQ_PRESET,
	SONG_FORMAT,
	SONG_GENRE,
	SONG_DESCRIPTION,
	SONG_RELATIVE_VOLUME,
	SONG_SAMPLE_RATE,
	SONG_SIZE,
	SONG_START_TIME,
	SONG_STOP_TIME,
	SONG_TIME,
	SONG_TRACK_COUNT,
	SONG_TRACK_NUMBER,
	SONG_USER_RATING,
	SONG_YEAR
} DAAPMetaData;

struct DAAPMetaDataMap {
	gchar *tag;
	DAAPMetaData md;
};

struct DAAPMetaDataMap meta_data_map[] = {
	{"dmap.itemid",			ITEM_ID},			
    	{"dmap.itemname",		ITEM_NAME},		
    	{"dmap.itemkind",		ITEM_KIND},			
    	{"dmap.persistentid",		PERSISTENT_ID},	
	{"dmap.containeritemid",	CONTAINER_ITEM_ID},	
    	{"daap.songalbum",		SONG_ALBUM},
    	{"daap.songartist",		SONG_ARTIST},
    	{"daap.songbitrate",		SONG_BITRATE},
    	{"daap.songbeatsperminute",	SONG_BPM},
    	{"daap.songcomment",		SONG_COMMENT},
    	{"daap.songcompilation",	SONG_COMPILATION},
    	{"daap.songcomposer",		SONG_COMPOSER},
    	{"daap.songdatakind",		SONG_DATA_KIND},
    	{"daap.songdataurl",		SONG_DATA_URL},
    	{"daap.songdateadded",		SONG_DATE_ADDED},
    	{"daap.songdatemodified",	SONG_DATE_MODIFIED},
    	{"daap.songdescription",	SONG_DESCRIPTION},
    	{"daap.songdisabled",		SONG_DISABLED},
    	{"daap.songdisccount",		SONG_DISC_COUNT},
    	{"daap.songdiscnumber",		SONG_DISC_NUMBER},
    	{"daap.songeqpreset",		SONG_EQ_PRESET},
    	{"daap.songformat",		SONG_FORMAT},
    	{"daap.songgenre",		SONG_GENRE},
    	{"daap.songgrouping",		SONG_GROUPING},
    	{"daap.songrelativevolume",	SONG_RELATIVE_VOLUME},
    	{"daap.songsamplerate",		SONG_SAMPLE_RATE},
    	{"daap.songsize",		SONG_SIZE},
    	{"daap.songstarttime",		SONG_START_TIME},
    	{"daap.songstoptime",		SONG_STOP_TIME},
   	{"daap.songtime",		SONG_TIME},
    	{"daap.songtrackcount",		SONG_TRACK_COUNT},
    	{"daap.songtracknumber",	SONG_TRACK_NUMBER},
    	{"daap.songuserrating",		SONG_USER_RATING},
    	{"daap.songyear",		SONG_YEAR}};

typedef unsigned long long bitwise;

struct MLCL_Bits {
	GNode *mlcl;
	bitwise bits;
	gpointer pointer;
};

static gboolean 
client_requested (bitwise bits,
		  gint field)
{
	return 0 != (bits & (((bitwise) 1) << field));
}

#define DMAP_ITEM_KIND_AUDIO 2
#define DAAP_SONG_DATA_KIND_NONE 0

static void 
add_entry_to_mlcl (RhythmDBEntry *entry, 
		   gint id, 
		   struct MLCL_Bits *mb)
{
	GNode *mlit;
	
	mlit = rb_daap_structure_add (mb->mlcl, RB_DAAP_CC_MLIT);
	
	if (client_requested (mb->bits, ITEM_KIND))
		rb_daap_structure_add (mlit, RB_DAAP_CC_MIKD, (gchar) DMAP_ITEM_KIND_AUDIO);
	if (client_requested (mb->bits, ITEM_ID)) 
		rb_daap_structure_add (mlit, RB_DAAP_CC_MIID, (gint32) id);
	if (client_requested (mb->bits, ITEM_NAME))
		rb_daap_structure_add (mlit, RB_DAAP_CC_MINM, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE));
	if (client_requested (mb->bits, PERSISTENT_ID))
		rb_daap_structure_add (mlit, RB_DAAP_CC_MPER, (gint64) id);
	if (client_requested (mb->bits, CONTAINER_ITEM_ID))
		rb_daap_structure_add (mlit, RB_DAAP_CC_MCTI, (gint32) id);
	if (client_requested (mb->bits, SONG_DATA_KIND))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASDK, (gchar) DAAP_SONG_DATA_KIND_NONE);
	if (client_requested (mb->bits, SONG_DATA_URL))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASUL, "");
	if (client_requested (mb->bits, SONG_ALBUM))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASAL, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM));
	if (client_requested (mb->bits, SONG_GROUPING))
		rb_daap_structure_add (mlit, RB_DAAP_CC_AGRP, "");
	if (client_requested (mb->bits, SONG_ARTIST))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASAR, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST));
	if (client_requested (mb->bits, SONG_BITRATE)) {
		gulong bitrate = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE);
		
		if (bitrate == 0) { /* because gstreamer is stupid */
		/* bitrate needs to be sent in kbps, kb/s
		 * a kilobit is 128 bytes
		 * if the length is L seconds, 
		 * and the file is S bytes
		 * then 
		 * (S / 128) / L is kbps */
			gulong length = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);
			guint64 file_size = rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE);
			
			bitrate = (file_size / 128) / length;
		}
		
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASBR, (gint32) rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE));
	}
	if (client_requested (mb->bits, SONG_BPM))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASBT, (gint32) 0);
	if (client_requested (mb->bits, SONG_COMMENT))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASCM, "");
	if (client_requested (mb->bits, SONG_COMPILATION))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASCO, (gchar) FALSE);
	if (client_requested (mb->bits, SONG_COMPOSER))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASCP, "");
	if (client_requested (mb->bits, SONG_DATE_ADDED))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASDA, (gint32) rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_FIRST_SEEN));
	if (client_requested (mb->bits, SONG_DATE_MODIFIED))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASDM, (gint32) rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_MTIME));
	if (client_requested (mb->bits, SONG_DISC_COUNT))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASDC, (gint32) 0);
	if (client_requested (mb->bits, SONG_DISC_NUMBER))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASDN, (gint32) rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER));
	if (client_requested (mb->bits, SONG_DISABLED))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASDB, (gchar) FALSE);
	if (client_requested (mb->bits, SONG_EQ_PRESET))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASEQ, "");
	if (client_requested (mb->bits, SONG_FORMAT)) {
		const gchar *filename;
		gchar *ext;
		
		filename = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		ext = strrchr (filename, '.');
		ext++;
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASFM, ext);
	}
	if (client_requested (mb->bits, SONG_GENRE))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASGN, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE));
	if (client_requested (mb->bits, SONG_DESCRIPTION))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASDT, "");
	if (client_requested (mb->bits, SONG_RELATIVE_VOLUME))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASRV, 0);
	if (client_requested (mb->bits, SONG_SAMPLE_RATE))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASSR, 0);
	if (client_requested (mb->bits, SONG_SIZE))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASSZ, (gint32) rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE));
	if (client_requested (mb->bits, SONG_START_TIME))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASST, 0);
	if (client_requested (mb->bits, SONG_STOP_TIME))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASSP, 0);
	if (client_requested (mb->bits, SONG_TIME))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASTM, (gint32) (1000 * rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION)));
	if (client_requested (mb->bits, SONG_TRACK_COUNT))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASTC, 0);
	if (client_requested (mb->bits, SONG_TRACK_NUMBER))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASTN, (gint32) rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER));
	if (client_requested (mb->bits, SONG_USER_RATING))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASUR, 0); // fixme
	if (client_requested (mb->bits, SONG_YEAR))
		rb_daap_structure_add (mlit, RB_DAAP_CC_ASYR, 0);
	
	return;
}

static void 
add_playlist_to_mlcl (RBSource *source, 
		      GNode *mlcl)
{
 /* 	MLIT listing item
 * 		MIID item id
 * 		MPER persistent item id
 * 		MINM item name
 * 		MIMC item count
 */
	GNode *mlit;
	gchar *name;
	RBEntryView *ev;
	guint num_songs;
	
	g_object_get (G_OBJECT (source), "name", &name, NULL);
	
	ev = rb_source_get_entry_view (source);
	num_songs = rb_entry_view_get_num_entries (ev);
	
	mlit = rb_daap_structure_add (mlcl, RB_DAAP_CC_MLIT);
	rb_daap_structure_add (mlit, RB_DAAP_CC_MIID, (gint32) source);
	rb_daap_structure_add (mlit, RB_DAAP_CC_MPER, (gint64)(gint32) source);
	rb_daap_structure_add (mlit, RB_DAAP_CC_MINM, name);
	rb_daap_structure_add (mlit, RB_DAAP_CC_MIMC, (gint32) num_songs);
	
	g_free (name);
	
	return;
}

static gboolean 
add_playlist_entry_to_mlcl (GtkTreeModel *model,
			    GtkTreePath *path, 
			    GtkTreeIter *iter, 
			    struct MLCL_Bits *mb)
{
	GNode *mlit;
	RhythmDBEntry *entry;
	gint id;
	
	mlit = rb_daap_structure_add (mb->mlcl, RB_DAAP_CC_MLIT);

	gtk_tree_model_get (model, iter, 0, &entry, -1);
	
	id = GPOINTER_TO_INT (g_hash_table_lookup ((GHashTable *)mb->pointer, entry));

	if (client_requested (mb->bits, ITEM_KIND))
		rb_daap_structure_add (mlit, RB_DAAP_CC_MIKD, (gchar) DMAP_ITEM_KIND_AUDIO);
	if (client_requested (mb->bits, ITEM_ID)) 
		rb_daap_structure_add (mlit, RB_DAAP_CC_MIID, (gint32) id);
	if (client_requested (mb->bits, CONTAINER_ITEM_ID))
		rb_daap_structure_add (mlit, RB_DAAP_CC_MCTI, (gint32) id);
		
	return FALSE;
}	

static bitwise 
parse_meta (const gchar *s)
{
	gchar *start_of_attrs;
	gchar *end_of_attrs;
	gchar *attrs;
	gchar **attrsv;
	guint i;
	bitwise bits = 0;

	start_of_attrs = strstr (s, "meta=");
	if (start_of_attrs == NULL) {
		return 0;
	}
	start_of_attrs += 5;

	end_of_attrs = strchr (start_of_attrs, '&');
	if (end_of_attrs) {
		attrs = g_strndup (start_of_attrs, end_of_attrs - start_of_attrs);
	} else {
		attrs = g_strdup (start_of_attrs);
	}

	attrsv = g_strsplit (attrs,",",-1);
	
	for (i = 0; attrsv[i]; i++) {
		guint j;

		for (j = 0; j < G_N_ELEMENTS (meta_data_map); j++) {
			if (strcmp (meta_data_map[j].tag, attrsv[i]) == 0) {
				bits |= (((bitwise) 1) << meta_data_map[j].md);
			}
		}
	}

	g_free (attrs);
	g_strfreev (attrsv);
	
	return bits;
}

static void 
databases_cb (RBDAAPShare *share, 
	      SoupMessage *message)
{
	gchar *path;
	gchar *rest_of_path;
//	guint revision_number;
	
	path = soup_uri_to_string (soup_message_get_uri (message), TRUE);

	rest_of_path = strchr (path + 1, '/');
	
	if (rest_of_path == NULL) {
	/* AVDB server databases
	 * 	MSTT status
	 * 	MUTY update type
	 * 	MTCO specified total count
	 * 	MRCO returned count
	 * 	MLCL listing
	 * 		MLIT listing item
	 * 			MIID item id
	 * 			MPER persistent id
	 * 			MINM item name
	 * 			MIMC item count
	 * 			MCTC container count
	 */
		GNode *avdb;
		GNode *mlcl;
		GNode *mlit;

		avdb = rb_daap_structure_add (NULL, RB_DAAP_CC_AVDB);
		rb_daap_structure_add (avdb, RB_DAAP_CC_MSTT, (gint32) DMAP_STATUS_OK);
		rb_daap_structure_add (avdb, RB_DAAP_CC_MUTY, 0);
		rb_daap_structure_add (avdb, RB_DAAP_CC_MTCO, (gint32) 1);
		rb_daap_structure_add (avdb, RB_DAAP_CC_MRCO, (gint32) 1);
		mlcl = rb_daap_structure_add (avdb, RB_DAAP_CC_MLCL);
		mlit = rb_daap_structure_add (mlcl, RB_DAAP_CC_MLIT);
		rb_daap_structure_add (mlit, RB_DAAP_CC_MIID, (gint32) 1);
		rb_daap_structure_add (mlit, RB_DAAP_CC_MPER, (gint64) 1);
		rb_daap_structure_add (mlit, RB_DAAP_CC_MINM, share->priv->name);
		rb_daap_structure_add (mlit, RB_DAAP_CC_MIMC, (gint32)share->priv->num_songs);
		rb_daap_structure_add (mlit, RB_DAAP_CC_MCTC, (gint32) 1);
	
		message_set_from_rb_daap_structure (message, avdb);
		rb_daap_structure_destroy (avdb);
	} else if (g_ascii_strncasecmp ("/1/items?", rest_of_path, 9) == 0) {
	/* ADBS database songs
	 * 	MSTT status
	 * 	MUTY update type
	 * 	MTCO specified total count
	 * 	MRCO returned count
	 * 	MLCL listing
	 * 		MLIT
	 * 			attrs
	 * 		MLIT
	 * 		...
	 */
		GNode *adbs;
		struct MLCL_Bits mb = {NULL,0};

		mb.bits = parse_meta (rest_of_path);
		
		adbs = rb_daap_structure_add (NULL, RB_DAAP_CC_ADBS);
		rb_daap_structure_add (adbs, RB_DAAP_CC_MSTT, (gint32) DMAP_STATUS_OK);
		rb_daap_structure_add (adbs, RB_DAAP_CC_MUTY, 0);
		rb_daap_structure_add (adbs, RB_DAAP_CC_MTCO, (gint32) share->priv->num_songs);
		rb_daap_structure_add (adbs, RB_DAAP_CC_MRCO, (gint32) share->priv->num_songs);
		mb.mlcl = rb_daap_structure_add (adbs, RB_DAAP_CC_MLCL);
		
		g_hash_table_foreach (share->priv->entry_to_id, (GHFunc) add_entry_to_mlcl, &mb);

		message_set_from_rb_daap_structure (message, adbs);
		rb_daap_structure_destroy (adbs);
		adbs = NULL;
	} else if (g_ascii_strncasecmp ("/1/containers?", rest_of_path, 14) == 0) {
	/* APLY database playlists
	 * 	MSTT status
	 * 	MUTY update type
	 * 	MTCO specified total count
	 * 	MRCO returned count
	 * 	MLCL listing
	 * 		MLIT listing item
	 * 			MIID item id
	 * 			MPER persistent item id
	 * 			MINM item name
	 * 			MIMC item count
	 * 			ABPL baseplaylist (only for base)
	 * 		MLIT
	 * 		...
	 */
		GNode *aply;
		GNode *mlcl;
		GNode *mlit;
		GList *playlists;

		aply = rb_daap_structure_add (NULL, RB_DAAP_CC_APLY);
		rb_daap_structure_add (aply, RB_DAAP_CC_MSTT, (gint32) DMAP_STATUS_OK);
		rb_daap_structure_add (aply, RB_DAAP_CC_MUTY, 0);
		rb_daap_structure_add (aply, RB_DAAP_CC_MTCO, (gint32) 1);
		rb_daap_structure_add (aply, RB_DAAP_CC_MRCO, (gint32) 1);
		mlcl = rb_daap_structure_add (aply, RB_DAAP_CC_MLCL);
		mlit = rb_daap_structure_add (mlcl, RB_DAAP_CC_MLIT);
		rb_daap_structure_add (mlit, RB_DAAP_CC_MIID, (gint32) 1);
		rb_daap_structure_add (mlit, RB_DAAP_CC_MPER, (gint64) 1);
		rb_daap_structure_add (mlit, RB_DAAP_CC_MINM, share->priv->name);
		rb_daap_structure_add (mlit, RB_DAAP_CC_MIMC, (gint32) share->priv->num_songs);
		rb_daap_structure_add (mlit, RB_DAAP_CC_ABPL, (gchar) 1); /* base playlist */

		playlists = rb_playlist_manager_get_playlists (share->priv->playlist_manager);
		g_list_foreach (playlists, (GFunc)add_playlist_to_mlcl, mlcl);
		
		message_set_from_rb_daap_structure (message, aply);
		rb_daap_structure_destroy (aply);
	} else if (g_ascii_strncasecmp ("/1/containers/", rest_of_path, 14) == 0) {
	/* APSO playlist songs
	 * 	MSTT status
	 * 	MUTY update type
	 * 	MTCO specified total count
	 * 	MRCO returned count
	 * 	MLCL listing
	 * 		MLIT listing item
	 * 			MIKD item kind
	 * 			MIID item id
	 * 			MCTI container item id
	 * 		MLIT
	 * 		...
	 */
		GNode *apso;
		struct MLCL_Bits mb = {NULL,0};
		gint pl_id = atoi (rest_of_path + 14);

		mb.bits = parse_meta (rest_of_path);
		
		apso = rb_daap_structure_add (NULL, RB_DAAP_CC_APSO);
		rb_daap_structure_add (apso, RB_DAAP_CC_MSTT, (gint32) DMAP_STATUS_OK);
		rb_daap_structure_add (apso, RB_DAAP_CC_MUTY, 0);

		if (pl_id == 1) {
			rb_daap_structure_add (apso, RB_DAAP_CC_MTCO, (gint32) share->priv->num_songs);
			rb_daap_structure_add (apso, RB_DAAP_CC_MRCO, (gint32) share->priv->num_songs);
			mb.mlcl = rb_daap_structure_add (apso, RB_DAAP_CC_MLCL);
		
			g_hash_table_foreach (share->priv->entry_to_id, (GHFunc) add_entry_to_mlcl, &mb);
		} else {
			RBSource *source = GINT_TO_POINTER (pl_id);
			RBEntryView *ev;
			guint num_songs;
			RhythmDBQueryModel *model;

			mb.mlcl = rb_daap_structure_add (apso, RB_DAAP_CC_MLCL);

			mb.pointer = share->priv->entry_to_id;
			
			ev = rb_source_get_entry_view (source);
			num_songs = rb_entry_view_get_num_entries (ev);
				
			rb_daap_structure_add (apso, RB_DAAP_CC_MTCO, (gint32) num_songs);
			rb_daap_structure_add (apso, RB_DAAP_CC_MRCO, (gint32) num_songs);

			g_object_get (G_OBJECT (source), "query-model", &model, NULL);
			gtk_tree_model_foreach (GTK_TREE_MODEL (model), (GtkTreeModelForeachFunc) add_playlist_entry_to_mlcl, &mb);
			g_object_unref (model);
			
		}
		
		message_set_from_rb_daap_structure (message, apso);
		rb_daap_structure_destroy (apso);
	} else if (g_ascii_strncasecmp ("/1/items/", rest_of_path, 9) == 0) {
	/* just the file :) */
		gchar *id_str;
		gint id;
		RhythmDBEntry *entry;
		const gchar *location;
		guint64 file_size;
		GnomeVFSResult result;
		GnomeVFSHandle *handle;
		const gchar *range_header;
		gchar *buf;
		guint status_code = SOUP_STATUS_OK;
		
		id_str = rest_of_path + 9;
		id = atoi (id_str);

		entry = g_hash_table_lookup (share->priv->id_to_entry, GINT_TO_POINTER (id));
		location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		file_size = rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE);

		result = gnome_vfs_open (&handle, location, GNOME_VFS_OPEN_READ);
		if (result != GNOME_VFS_OK) {
			soup_message_set_status (message, SOUP_STATUS_INTERNAL_SERVER_ERROR);
			goto out;
		}

		range_header = soup_message_get_header (message->request_headers, "Range");
		if (range_header) {
			const gchar *s;
			GnomeVFSFileOffset range;
			gchar *content_range;
			
			s = range_header + 6; // bytes=
			range = atoll (s);
			
			result = gnome_vfs_seek (handle, GNOME_VFS_SEEK_START, range);
			
			if (result != GNOME_VFS_OK) {
				g_message ("Error seeking: %s", gnome_vfs_result_to_string (result));
				soup_message_set_status (message, SOUP_STATUS_INTERNAL_SERVER_ERROR);
				goto out;
			}

			status_code = SOUP_STATUS_PARTIAL_CONTENT;

			content_range = g_strdup_printf ("bytes %"GNOME_VFS_OFFSET_FORMAT_STR"-%"G_GUINT64_FORMAT"/%"G_GUINT64_FORMAT, range, file_size, file_size);
			soup_message_add_header (message->response_headers, "Content-Range", content_range);
			g_free (content_range);

			file_size -= range;
		}
		
		/* FIXME FIXME FIXME
		 * Ideally, it seems that an mmap type solution should be used
		 * here.  However, this works for now.
		 */
		buf = g_try_malloc (file_size);
		if (buf == NULL) {
			g_warning ("Unable to malloc %"G_GUINT64_FORMAT" bytes to transfer file", file_size);
			soup_message_set_status (message, SOUP_STATUS_INTERNAL_SERVER_ERROR);
			goto out;
		}

		result = gnome_vfs_read (handle, buf, file_size, NULL);
		if (result != GNOME_VFS_OK) {
			soup_message_set_status (message, SOUP_STATUS_INTERNAL_SERVER_ERROR);
			g_free (buf);
			goto out;
		}
		
		message_add_standard_headers (message);
		soup_message_add_header (message->response_headers, "Accept-Ranges", "bytes");
		message->response.owner = SOUP_BUFFER_SYSTEM_OWNED;
		message->response.length = file_size;
		message->response.body = buf;

		gnome_vfs_close (handle);
		
		soup_message_set_status (message, status_code);
		soup_server_message_set_encoding (SOUP_SERVER_MESSAGE (message), SOUP_TRANSFER_CONTENT_LENGTH);

	} else {
		g_print ("unhandled: %s\n", path);
	}
		
out:
	g_free (path);
		
	return;
}

typedef void (* DAAPPathFunction) (RBDAAPShare *share, SoupMessage *message);

struct DAAPPath {
	const gchar *path;
	guint path_length;
	DAAPPathFunction function;
};

static const struct DAAPPath paths_to_functions[] = {
	{"/server-info", 12, server_info_cb},
	{"/content-codes", 14, content_codes_cb},
	{"/login", 6, login_cb},
	{"/update", 7, update_cb},
	{"/databases", 10, databases_cb}
};

static void 
server_cb (SoupServerContext *context, 
	   SoupMessage *message, 
	   RBDAAPShare *share)
{
	gchar *path;
	guint i;
	
	path = soup_uri_to_string (soup_message_get_uri (message), TRUE);
	
	for (i = 0; i < G_N_ELEMENTS (paths_to_functions); i++) {
		if (g_ascii_strncasecmp (paths_to_functions[i].path, path, paths_to_functions[i].path_length) == 0) {
			paths_to_functions[i].function (share, message);
			
			return;
		}
	}
	
	g_warning ("unhandled path %s\n", soup_uri_to_string (soup_message_get_uri (message), TRUE));

	g_free (path);
	
	return;
}

static void 
add_db_entry (RhythmDBEntry *entry, 
	      RBDAAPShare *share)
{
	RhythmDBEntryType type;
	
	type = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TYPE);

	if (type == rhythmdb_entry_song_get_type ()) {
		share->priv->num_songs++;
	
		g_hash_table_insert (share->priv->id_to_entry, GINT_TO_POINTER (share->priv->num_songs), entry);
		g_hash_table_insert (share->priv->entry_to_id, entry, GINT_TO_POINTER (share->priv->num_songs));
	}

	return;
}

static void 
db_entry_added_cb (RhythmDB *db, 
		   RhythmDBEntry *entry,
		   RBDAAPShare *share)
{
	RhythmDBEntryType type;
	
	type = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TYPE);

	if (type == rhythmdb_entry_song_get_type ()) {
		share->priv->num_songs++;

		g_hash_table_insert (share->priv->id_to_entry, GINT_TO_POINTER (share->priv->num_songs), entry);
		g_hash_table_insert (share->priv->entry_to_id, entry, GINT_TO_POINTER (share->priv->num_songs));
	}
	
	return;
}

static void 
db_entry_deleted_cb (RhythmDB *db, 
		     RhythmDBEntry *entry, 
		     RBDAAPShare *share)
{
	RhythmDBEntryType type;
	
	type = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TYPE);

	if (type == rhythmdb_entry_song_get_type ()) {
		gpointer id;

		id = g_hash_table_lookup (share->priv->entry_to_id, entry);
		g_hash_table_remove (share->priv->entry_to_id, entry);
		g_hash_table_remove (share->priv->id_to_entry, id);

		share->priv->num_songs--;
	}

	return;
}

#define CONF_NAME "/apps/rhythmbox/sharing/share_name"

static void 
publish_cb (RBDAAPmDNSPublisher publisher,
	    RBDAAPmDNSPublisherStatus status,
	    RBDAAPShare *share)
{
	switch (status) {
		case RB_DAAP_MDNS_PUBLISHER_STARTED:
			rb_debug ("mDNS publish successful");
			share->priv->published = TRUE;
			break;
		case RB_DAAP_MDNS_PUBLISHER_COLLISION: {
			gchar *new_name;
			
			g_message ("Duplicate share name on mDNS");
			
			new_name = rb_daap_collision_dialog_new_run (share->priv->name);

			/* FIXME test & apply this
			 * or, do we even need it?
			 *
			 * Howl already automaticall renames for us, that was a
			 * pain til I figured out a better way to filter us
			 * out of browsing.  Why not have avahi autorename
			 * also.  no ones the wiser?
			 */
			break;
		}
		
	}

	return;
}

#define STANDARD_DAAP_PORT 3689

static void
rb_daap_share_start_publish (RBDAAPShare *share)
{
	gint port = STANDARD_DAAP_PORT;
	gboolean ret;
	
	share->priv->server = soup_server_new (SOUP_SERVER_PORT, port, NULL);
	if (share->priv->server == NULL) {
		rb_debug ("Unable to start music sharing server on port %d, trying any open port", port);
		share->priv->server = soup_server_new (SOUP_SERVER_PORT, SOUP_ADDRESS_ANY_PORT, NULL);

		if (share->priv->server == NULL) {
			g_warning ("Unable to start music sharing server");
			return;
		}
	}

	share->priv->port = soup_server_get_port (share->priv->server);
	rb_debug ("Started DAAP server on port %d", port);

	soup_server_add_handler (share->priv->server, 
				 NULL, 
				 NULL, 
				 (SoupServerCallbackFn)server_cb,
				 NULL,
				 share);
	soup_server_run_async (share->priv->server);
	
	ret = rb_daap_mdns_publish (&(share->priv->publisher),
				    share->priv->name,
				    share->priv->port,
				    (RBDAAPmDNSPublisherCallback) publish_cb,
				    share);
	
	if (ret == FALSE) {
		g_warning ("Unable to notify network of music sharing");
		return;
	}

	rb_debug ("Published DAAP server information to mdns");

	share->priv->id_to_entry = g_hash_table_new (NULL, NULL);
	share->priv->entry_to_id = g_hash_table_new (NULL, NULL);
	share->priv->num_songs = 0;

	rhythmdb_entry_foreach (share->priv->db, (GFunc)add_db_entry, share);
	
	share->priv->entry_added_id = g_signal_connect (G_OBJECT (share->priv->db), "entry-added", G_CALLBACK (db_entry_added_cb), share);
	share->priv->entry_deleted_id = g_signal_connect (G_OBJECT (share->priv->db), "entry-deleted", G_CALLBACK (db_entry_deleted_cb), share);
	
	return;
}

static void
rb_daap_share_stop_publish (RBDAAPShare *share)
{
	if (share->priv->server) {
		/* FIXME */
		/* This will spew:
		 * GLib-CRITICAL **: g_main_loop_quit: assertion `loop != NULL' failed
		 * But it doesn't seem to matter.
		 */
//		soup_server_quit (share->priv->server);
		g_object_unref (G_OBJECT (share->priv->server));
		share->priv->server = NULL;
	}
	
	if (share->priv->id_to_entry) {
		g_hash_table_destroy (share->priv->id_to_entry);
		share->priv->id_to_entry = NULL;
	}

	if (share->priv->entry_to_id) {
		g_hash_table_destroy (share->priv->entry_to_id);
		share->priv->entry_to_id = NULL;
	}
	
	if (share->priv->entry_added_id != 0) {
		g_signal_handler_disconnect (share->priv->db, share->priv->entry_added_id);
		share->priv->entry_added_id = 0;
	}

	if (share->priv->entry_deleted_id != 0) {
		g_signal_handler_disconnect (share->priv->db, share->priv->entry_deleted_id);
		share->priv->entry_deleted_id = 0;
	}
	
	if (share->priv->publisher) {
		rb_daap_mdns_publish_cancel (share->priv->publisher);
		share->priv->publisher = 0;
	}

	share->priv->published = FALSE;
	
	return;
}


