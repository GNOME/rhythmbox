/*
 *  arch-tag: Implementation of ipod source object
 *
 *  Copyright (C) 2004 Christophe Fergeau  <teuf@gnome.org>
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
#include <gtk/gtktreeview.h>
#include <string.h>
#include "itunesdb.h"
#include "rhythmdb.h"
#include <libgnome/gnome-i18n.h>
#include "eel-gconf-extensions.h"
#include "rb-ipod-source.h"
#include "rb-stock-icons.h"
#define DEFAULT_MOUNT_PATH "/mnt/ipod"
#define GCONF_MOUNT_PATH "/apps/qahog/mount_path"


static void rb_ipod_source_init (RBiPodSource *source);
static void rb_ipod_source_finalize (GObject *object);
static void rb_ipod_source_class_init (RBiPodSourceClass *klass);

static gboolean ipod_itunesdb_monitor_cb (RBiPodSource *source);



struct RBiPodSourcePrivate
{
	guint ipod_polling_id;
};


GType
rb_ipod_source_get_type (void)
{
	static GType rb_ipod_source_type = 0;

	if (rb_ipod_source_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBiPodSourceClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_ipod_source_class_init,
			NULL,
			NULL,
			sizeof (RBiPodSource),
			0,
			(GInstanceInitFunc) rb_ipod_source_init
		};

		rb_ipod_source_type = g_type_register_static (RB_TYPE_LIBRARY_SOURCE,
							      "RBiPodSource",
							      &our_info, 0);

	}

	return rb_ipod_source_type;
}

static void
rb_ipod_source_class_init (RBiPodSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_ipod_source_finalize;
}

static void
rb_ipod_source_init (RBiPodSource *source)
{
	source->priv = g_new0 (RBiPodSourcePrivate, 1);	
	source->priv->ipod_polling_id = g_timeout_add (1000, (GSourceFunc)ipod_itunesdb_monitor_cb, source);
}


static void 
rb_ipod_source_finalize (GObject *object)
{
	RBiPodSource *source = RB_IPOD_SOURCE (object);

	if (source->priv->ipod_polling_id) {
		g_source_remove (source->priv->ipod_polling_id);
	}
}


RBSource *
rb_ipod_source_new (RBShell *shell, RhythmDB *db, BonoboUIComponent *component)
{
	RBSource *source;
	GtkWidget *dummy = gtk_tree_view_new ();
	GdkPixbuf *icon;

	icon = gtk_widget_render_icon (dummy, RB_STOCK_IPOD,
				       GTK_ICON_SIZE_LARGE_TOOLBAR,
				       NULL);
	gtk_widget_destroy (dummy);

	/* FIXME: need to set icon */
	source = RB_SOURCE (g_object_new (RB_TYPE_IPOD_SOURCE,
					  "name", _("iPod"),
					  "entry-type", RHYTHMDB_ENTRY_TYPE_IPOD,
					  "internal-name", "<ipod>",
					  "icon", icon,
					  "db", db,
					  "component", component,
					  NULL));

	rb_shell_register_entry_type_for_source (shell, source, 
						 RHYTHMDB_ENTRY_TYPE_IPOD);


	return source;
}


static char *
ipod_get_mount_path (void)
{
	gchar *path;

	path = eel_gconf_get_string (GCONF_MOUNT_PATH);
	if (path == NULL || strcmp (path, "") == 0)
		return g_strdup (DEFAULT_MOUNT_PATH);
	else
		return path;
}

static char *
ipod_get_itunesdb_path (void)
{
	gchar *result;
	gchar *mount_path = ipod_get_mount_path ();

	result = g_build_filename (mount_path,
				   "iPod_Control/iTunes/iTunesDB", 
				   NULL);
	g_free (mount_path);
	return result;
}

static void
entry_set_locked (RhythmDB *db, RhythmDBEntry *entry,
		  RhythmDBPropType propid, GValue *value) 
{
	rhythmdb_write_lock (RHYTHMDB (db));
	rhythmdb_entry_set (RHYTHMDB (db), entry, propid, value);
	rhythmdb_write_unlock (RHYTHMDB (db));
}

static void 
entry_set_string_prop (RhythmDB *db, RhythmDBEntry *entry,
		       RhythmDBPropType propid, const char *str)
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
	entry_set_locked (RHYTHMDB (db), entry, propid, &value);
	g_value_unset (&value);
}


#define MAX_SONGS_LOADED_AT_ONCE 250

typedef struct  {
	RhythmDB *db;
	iPodParser *parser;
} RBiPodSongAdderCtxt;


static gboolean
load_ipod_db_idle_cb (RBiPodSongAdderCtxt *ctxt)
{
	RhythmDBTreeEntry *entry;
	int i;

	for (i = 0; i < MAX_SONGS_LOADED_AT_ONCE; i++) {
		gchar *pc_path;
		gchar *mount_path;
		iPodItem *item;
		iPodSong *song;
		
		item = ipod_get_next_item (ctxt->parser);
		if ((item == NULL) || (item->type != IPOD_ITEM_SONG)) {
			ipod_item_destroy (item);
			return FALSE;
		}
		song = (iPodSong *)item->data;
				
		/* Set URI */
		mount_path = ipod_get_mount_path ();
		pc_path = itunesdb_get_track_name_on_ipod (mount_path, song);
		g_free (mount_path);
		rhythmdb_write_lock (RHYTHMDB (ctxt->db));
		entry = rhythmdb_entry_new (RHYTHMDB (ctxt->db), 
					    RHYTHMDB_ENTRY_TYPE_IPOD,
					    pc_path);
		rhythmdb_write_unlock (RHYTHMDB (ctxt->db));
		g_free (pc_path);

		/* Set track number */
		if (song->track_nr != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_INT);
			g_value_set_int (&value, song->track_nr);
			entry_set_locked (RHYTHMDB (ctxt->db), entry, 
					  RHYTHMDB_PROP_TRACK_NUMBER, 
					  &value);
			g_value_unset (&value);
		}

		/* Set disc number */
		if (song->cd_nr != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, song->cd_nr);
			rhythmdb_entry_set (RHYTHMDB (ctxt->db), entry, 
					    RHYTHMDB_PROP_DISC_NUMBER, 
					    &value);
			g_value_unset (&value);
		}
		
		/* Set bitrate */
		if (song->bitrate != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_INT);
			g_value_set_int (&value, song->bitrate);
			entry_set_locked (RHYTHMDB (ctxt->db), entry, 
					  RHYTHMDB_PROP_BITRATE, 
					  &value);
			g_value_unset (&value);
		}
		
		/* Set length */
		if (song->tracklen != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_LONG);
			g_value_set_long (&value, song->tracklen/1000);
			entry_set_locked (RHYTHMDB (ctxt->db), entry, 
					  RHYTHMDB_PROP_DURATION, 
					  &value);
			g_value_unset (&value);
		}
		
		/* Set file size */
		if (song->size != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_UINT64);
			g_value_set_uint64 (&value, song->size);
			entry_set_locked (RHYTHMDB (ctxt->db), entry, 
					  RHYTHMDB_PROP_FILE_SIZE, 
					  &value);
			g_value_unset (&value);
		}
		
		/* Set title */
		
		entry_set_string_prop (RHYTHMDB (ctxt->db), entry, 
				       RHYTHMDB_PROP_TITLE, song->title);
		
		/* Set album, artist and genre from iTunesDB */
		entry_set_string_prop (RHYTHMDB (ctxt->db), entry, 
				       RHYTHMDB_PROP_ARTIST, song->artist);
		
		entry_set_string_prop (RHYTHMDB (ctxt->db), entry, 
				       RHYTHMDB_PROP_ALBUM, song->album);
		
		entry_set_string_prop (RHYTHMDB (ctxt->db), entry, 
				       RHYTHMDB_PROP_GENRE, song->genre);
		
		ipod_item_destroy (item);
	}
	/* FIXME: item is leaked */
	return TRUE;
}

static void
context_free (gpointer data)
{
	RBiPodSongAdderCtxt *ctxt = (RBiPodSongAdderCtxt *)data;
	if (ctxt == NULL) {
		return;
	}
	if (ctxt->parser != NULL) {
		ipod_parser_destroy (ctxt->parser);
	}
	g_free (ctxt);
}

/* We need to be locked to use this function */
static GnomeVFSResult
add_ipod_songs_to_db (RhythmDB *db)
{
	char *path;
	iPodParser *parser;
	RBiPodSongAdderCtxt *ctxt;

	ctxt = g_new0 (RBiPodSongAdderCtxt, 1);
	if (ctxt == NULL) {
		return GNOME_VFS_ERROR_NO_MEMORY;
	}

	path = ipod_get_mount_path ();
	parser = ipod_parser_new (path);
	g_free (path);

	ctxt->db = db;
	ctxt->parser = parser;

	g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, 
			 (GSourceFunc)load_ipod_db_idle_cb,
			 ctxt, context_free);

	return GNOME_VFS_OK;
}


static void
ipod_load_songs (RhythmDB *rdb)
{
	GnomeVFSResult res;

	res = add_ipod_songs_to_db (rdb);

	if (res != GNOME_VFS_OK) {
		g_warning ("Error loading iPod database");
	}
}

static void
ipod_unload_songs (RhythmDB *db)
{
	rhythmdb_write_lock (db);
	rhythmdb_entry_delete_by_type (db, RHYTHMDB_ENTRY_TYPE_IPOD);
	rhythmdb_write_unlock (db);
}

static gboolean
ipod_itunesdb_monitor_cb (RBiPodSource *source)
{
	gboolean is_present;
	gchar *itunesdb_path;
	static gboolean was_present = FALSE;

	itunesdb_path = ipod_get_itunesdb_path ();
	g_assert (itunesdb_path != NULL);
	is_present = g_file_test (itunesdb_path, G_FILE_TEST_EXISTS);

	if (is_present && !was_present) {
		RhythmDB *db;

		g_print ("iPod plugged\n");
		was_present = TRUE;
		g_object_get (G_OBJECT (source), "db", &db, NULL);
		ipod_load_songs (db);
		/* FIXME: should we suspend this monitor until the iPod 
		 * database has been read and fed to rhythmbox?
		 */
	} else if (!is_present && was_present) {
		RhythmDB *db;

		g_print ("iPod unplugged\n");
		was_present = FALSE;
		g_object_get (G_OBJECT (source), "db", &db, NULL);
		ipod_unload_songs (db);
	}
	g_free (itunesdb_path);
	return TRUE;
}

RhythmDBEntryType rhythmdb_entry_ipod_get_type (void) 
{
	static RhythmDBEntryType ipod_type = -1;
       
	if (ipod_type == -1) {
		ipod_type = rhythmdb_entry_register_type ();
	}

	return ipod_type;
}
