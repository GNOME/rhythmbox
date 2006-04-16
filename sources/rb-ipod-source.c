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

#include <config.h>


#include <gtk/gtktreeview.h>
#include <string.h>
#include "rhythmdb.h"
#include <libgnome/gnome-i18n.h>
#ifdef HAVE_HAL
#include <libhal.h>
#include <dbus/dbus.h>
#endif
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-volume.h>
#include <libgnomevfs/gnome-vfs-volume-monitor.h>
#include <gpod/itdb.h>
#include "eel-gconf-extensions.h"
#include "rb-ipod-source.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-removable-media-manager.h"
#include "rb-static-playlist-source.h"
#include "rb-util.h"
#include "rhythmdb.h"

static GObject *rb_ipod_source_constructor (GType type, guint n_construct_properties,
			       GObjectConstructParam *construct_properties);
static void rb_ipod_source_dispose (GObject *object);

static GObject *rb_ipod_source_constructor (GType type, guint n_construct_properties,
			       GObjectConstructParam *construct_properties);
static void rb_ipod_source_dispose (GObject *object);

static gboolean impl_show_popup (RBSource *source);

static void rb_ipod_load_songs (RBiPodSource *source);
static gchar *rb_ipod_get_mount_path (GnomeVFSVolume *volume);
static void impl_delete_thyself (RBSource *source);
static GList* impl_get_ui_actions (RBSource *source);
static void rb_ipod_source_cmd_rename (GtkAction *action,
				       RBiPodSource *source);
#ifdef HAVE_HAL
static gboolean hal_udi_is_ipod (const char *udi);
#endif

typedef struct
{
	Itdb_iTunesDB *ipod_db;
	gchar *ipod_mount_path;
	GtkActionGroup *action_group;	
} RBiPodSourcePrivate;


static GtkActionEntry rb_ipod_source_actions [] =
{
	{ "iPodSourceRename", NULL, N_("_Rename"), NULL,
	  N_("Rename iPod"),
	  G_CALLBACK (rb_ipod_source_cmd_rename) }
};


G_DEFINE_TYPE (RBiPodSource, rb_ipod_source, RB_TYPE_REMOVABLE_MEDIA_SOURCE)
#define IPOD_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_IPOD_SOURCE, RBiPodSourcePrivate))


static void
rb_ipod_source_class_init (RBiPodSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->constructor = rb_ipod_source_constructor;
	object_class->dispose = rb_ipod_source_dispose;

	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_delete_thyself = impl_delete_thyself;
	source_class->impl_can_rename = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_get_ui_actions = impl_get_ui_actions;

	g_type_class_add_private (klass, sizeof (RBiPodSourcePrivate));
}

static void
rb_ipod_source_set_ipod_name (RBiPodSource *source, const char *name)
{
	Itdb_Playlist *mpl;
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);

	mpl = itdb_playlist_mpl (priv->ipod_db);
	rb_debug ("Renaming iPod from %s to %s", mpl->name, name);
	g_free (mpl->name);
	mpl->name = g_strdup (name);
	itdb_write (priv->ipod_db, NULL);
}

static void
rb_ipod_source_name_changed_cb (RBiPodSource *source, GParamSpec *spec, 
				gpointer data)
{
	char *name;

	g_object_get (RB_SOURCE (source), "name", &name, NULL);
	rb_ipod_source_set_ipod_name (source, name);
	g_free (name);
}

static void
rb_ipod_source_init (RBiPodSource *source)
{
	g_signal_connect (G_OBJECT (source), "notify::name", 
			  (GCallback)rb_ipod_source_name_changed_cb, NULL);
}

static GObject *
rb_ipod_source_constructor (GType type, guint n_construct_properties,
			       GObjectConstructParam *construct_properties)
{
	RBiPodSource *source;
	RBEntryView *songs;
	RBiPodSourcePrivate *priv;
	GtkUIManager *uimanager;

	source = RB_IPOD_SOURCE (G_OBJECT_CLASS (rb_ipod_source_parent_class)->
			constructor (type, n_construct_properties, construct_properties));
	priv = IPOD_SOURCE_GET_PRIVATE (source);
	songs = rb_source_get_entry_view (RB_SOURCE (source));
	rb_entry_view_append_column (songs, RB_ENTRY_VIEW_COL_RATING, FALSE);
	rb_entry_view_append_column (songs, RB_ENTRY_VIEW_COL_LAST_PLAYED, FALSE);

	priv->action_group = _rb_source_register_action_group (RB_SOURCE (source),
							       "iPodActions",
							       rb_ipod_source_actions,
							       G_N_ELEMENTS (rb_ipod_source_actions),
							       source);

	/* FIXME: shouldn't it be done only once for the class instead of 
	 * being done for every RBiPodSource object created?
	 */
	g_object_get (G_OBJECT (source), "ui-manager", &uimanager, NULL);
	gtk_ui_manager_add_ui_from_file (uimanager,
					 rb_file ("ipod-ui.xml"), NULL);
	g_object_unref (G_OBJECT (uimanager));

	rb_ipod_load_songs (source);

	return G_OBJECT (source);
}

static void 
rb_ipod_source_dispose (GObject *object)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (object);

 	if (priv->ipod_db != NULL) {
 		itdb_free (priv->ipod_db);
 		priv->ipod_db = NULL;
  	}

	if (priv->ipod_mount_path) {
		g_free (priv->ipod_mount_path);
		priv->ipod_mount_path = NULL;
	}

	G_OBJECT_CLASS (rb_ipod_source_parent_class)->dispose (object);
}

RBRemovableMediaSource *
rb_ipod_source_new (RBShell *shell, GnomeVFSVolume *volume)
{
	RBiPodSource *source;
	RhythmDBEntryType entry_type;

	g_assert (rb_ipod_is_volume_ipod (volume));

	entry_type =  rhythmdb_entry_register_type ();

	source = RB_IPOD_SOURCE (g_object_new (RB_TYPE_IPOD_SOURCE,
					  "entry-type", entry_type,
					  "volume", volume,
					  "shell", shell,
					  NULL));

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_REMOVABLE_MEDIA_SOURCE (source);
}

static void 
entry_set_string_prop (RhythmDB *db, RhythmDBEntry *entry,
		       RhythmDBPropType propid, const char *str)
{
	GValue value = {0,};

	if (!str)
		str = _("Unknown");

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_static_string (&value, str);
	rhythmdb_entry_set_uninserted (RHYTHMDB (db), entry, propid, &value);
	g_value_unset (&value);
}


static char *
ipod_path_to_uri (const char *mount_point, const char *ipod_path)
{
 	char *rel_pc_path;
 	char *full_pc_path;
 	char *uri;
	
 	rel_pc_path = g_strdup (ipod_path);	
 	itdb_filename_ipod2fs (rel_pc_path);
 	full_pc_path = g_build_filename (mount_point, rel_pc_path, NULL);
 	g_free (rel_pc_path);
 	uri = g_filename_to_uri (full_pc_path, NULL, NULL);
 	g_free (full_pc_path);
 	return uri;
}

static void
add_rb_playlist (RBiPodSource *source, Itdb_Playlist *playlist)
{
	RBShell *shell;
	RBSource *playlist_source;
	GList *it;
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	RhythmDBEntryType entry_type;

  	g_object_get (G_OBJECT (source), 
		      "shell", &shell, 
		      "entry-type", &entry_type,
		      NULL);

	playlist_source = rb_static_playlist_source_new (shell, 
							 playlist->name, 
							 FALSE,
							 entry_type);

	for (it = playlist->members; it != NULL; it = it->next) {
		Itdb_Track *song;
		char *filename;

		song = (Itdb_Track *)it->data;
 		filename = ipod_path_to_uri (priv->ipod_mount_path, 
 					    song->ipod_path);
		rb_static_playlist_source_add_location (RB_STATIC_PLAYLIST_SOURCE (playlist_source),
							filename, -1);
		g_free (filename);
	}

	rb_shell_append_source (shell, playlist_source, RB_SOURCE (source));
	g_object_unref (G_OBJECT (shell));
}


static void
load_ipod_playlists (RBiPodSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	GList *it;

	for (it = priv->ipod_db->playlists; it != NULL; it = it->next) {
		Itdb_Playlist *playlist;

		playlist = (Itdb_Playlist *)it->data;
		if (itdb_playlist_is_mpl (playlist)) {
			continue;
		}
		if (playlist->is_spl) {
			continue;
		}

		add_rb_playlist (source, playlist);
	}

}

static gboolean
load_ipod_db_idle_cb (RBiPodSource *source)
{
	RBShell *shell;
	RhythmDB *db;
 	GList *it;
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
  
  	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
  	g_object_get (G_OBJECT (shell), "db", &db, NULL);
  	g_object_unref (G_OBJECT (shell));
  
  	g_assert (db != NULL);
 	for (it = priv->ipod_db->tracks; it != NULL; it = it->next) {
 		Itdb_Track *song;
 		RhythmDBEntry *entry;
		RhythmDBEntryType entry_type;
 		char *pc_path;

 		song = (Itdb_Track *)it->data;
  		
  		/* Set URI */
		g_object_get (G_OBJECT (source), "entry-type", &entry_type, 
			      NULL);

 		pc_path = ipod_path_to_uri (priv->ipod_mount_path, 
 					    song->ipod_path);
  		entry = rhythmdb_entry_new (RHYTHMDB (db), entry_type,
 					    pc_path);

		if (entry == NULL) {
			rb_debug ("cannot create entry %s", pc_path);
 			g_free (pc_path);
			continue;
		}
		
		rb_debug ("Adding %s from iPod", pc_path);
 		g_free (pc_path);

		/* Set track number */
		if (song->track_nr != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, song->track_nr);
			rhythmdb_entry_set_uninserted (RHYTHMDB (db), entry, 
						       RHYTHMDB_PROP_TRACK_NUMBER, 
						       &value);
			g_value_unset (&value);
		}

		/* Set disc number */
		if (song->cd_nr != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, song->cd_nr);
			rhythmdb_entry_set_uninserted (RHYTHMDB (db), entry, 
						       RHYTHMDB_PROP_DISC_NUMBER, 
						       &value);
			g_value_unset (&value);
		}
		
		/* Set bitrate */
		if (song->bitrate != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, song->bitrate);
			rhythmdb_entry_set_uninserted (RHYTHMDB (db), entry, 
						       RHYTHMDB_PROP_BITRATE, 
						       &value);
			g_value_unset (&value);
		}
		
		/* Set length */
		if (song->tracklen != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, song->tracklen/1000);
			rhythmdb_entry_set_uninserted (RHYTHMDB (db), entry, 
						       RHYTHMDB_PROP_DURATION, 
						       &value);
			g_value_unset (&value);
		}
		
		/* Set file size */
		if (song->size != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_UINT64);
			g_value_set_uint64 (&value, song->size);
			rhythmdb_entry_set_uninserted (RHYTHMDB (db), entry, 
						       RHYTHMDB_PROP_FILE_SIZE, 
						       &value);
			g_value_unset (&value);
		}

		/* Set playcount */
		if (song->playcount != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, song->playcount);
			rhythmdb_entry_set_uninserted (RHYTHMDB (db), entry,
						       RHYTHMDB_PROP_PLAY_COUNT,
						       &value);
			g_value_unset (&value);
		}

		/* Set year */
		if (song->year != 0) {
			GDate *date = NULL;
			GType type;
			GValue value = {0, };
			
			date = g_date_new_dmy (1, G_DATE_JANUARY, song->year);

			type = rhythmdb_get_property_type (RHYTHMDB(db),
							    RHYTHMDB_PROP_DATE);
			
			g_value_init (&value, type);
			g_value_set_ulong (&value, (date ? g_date_get_julian (date) : 0));
			
			rhythmdb_entry_set_uninserted (RHYTHMDB (db), entry,
						       RHYTHMDB_PROP_DATE,
						       &value);
			g_value_unset (&value);
			if (date)
				g_date_free (date);
		}

		/* Set rating */
		if (song->rating != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_DOUBLE);
			g_value_set_double (&value, song->rating/20.0);
			rhythmdb_entry_set_uninserted (RHYTHMDB (db), entry,
						       RHYTHMDB_PROP_RATING,
						       &value);
			g_value_unset (&value);
		}
		
		/* Set last played */
		if (song->time_played != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, itdb_time_mac_to_host (song->time_played));
			rhythmdb_entry_set_uninserted (RHYTHMDB (db), entry,
						       RHYTHMDB_PROP_LAST_PLAYED,
						       &value);
			g_value_unset (&value);
		}
		
		/* Set title */		
		entry_set_string_prop (RHYTHMDB (db), entry, 
				       RHYTHMDB_PROP_TITLE, song->title);
		
		/* Set album, artist and genre from iTunesDB */
		entry_set_string_prop (RHYTHMDB (db), entry, 
				       RHYTHMDB_PROP_ARTIST, song->artist);
		
		entry_set_string_prop (RHYTHMDB (db), entry, 
				       RHYTHMDB_PROP_ALBUM, song->album);
		
		entry_set_string_prop (RHYTHMDB (db), entry, 
				       RHYTHMDB_PROP_GENRE, song->genre);
		
		rhythmdb_commit (RHYTHMDB (db));
	
	}

	load_ipod_playlists (source);

	g_object_unref (G_OBJECT (db));
	return FALSE;
}

static void
rb_ipod_load_songs (RBiPodSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	GnomeVFSVolume *volume;

	g_object_get (G_OBJECT (source), "volume", &volume, NULL);
	priv->ipod_mount_path = rb_ipod_get_mount_path (volume);

 	priv->ipod_db = itdb_parse (priv->ipod_mount_path, NULL);
	if (priv->ipod_db != NULL) {
		/* FIXME: we could set a different icon depending on the iPod
		 * model
		 */
		g_object_set (RB_SOURCE (source), 
			      "name", itdb_playlist_mpl (priv->ipod_db)->name, 
			      NULL);
		g_idle_add ((GSourceFunc)load_ipod_db_idle_cb, source);
	}
}

static gchar *
rb_ipod_get_mount_path (GnomeVFSVolume *volume)
{
	gchar *path;
	gchar *uri;

	uri = gnome_vfs_volume_get_activation_uri (volume);
	path = g_filename_from_uri (uri, NULL, NULL);
	g_assert (path != NULL);
	g_free (uri);

	return path;
}

static gchar *
rb_ipod_get_itunesdb_path (GnomeVFSVolume *volume)
{
	gchar *mount_point_uri;
	gchar *mount_point;
	gchar *result;

	mount_point_uri = gnome_vfs_volume_get_activation_uri (volume);
	if (mount_point_uri == NULL) {
		return NULL;
	}
	mount_point = g_filename_from_uri (mount_point_uri, NULL, NULL);
	g_free (mount_point_uri);
	if (mount_point == NULL) {
		return NULL;
	}
	result = g_build_filename (mount_point, 
				   "iPod_Control/iTunes/iTunesDB",
				   NULL);
	g_free (mount_point);
	return result;
}

static gboolean
rb_ipod_volume_has_ipod_db (GnomeVFSVolume *volume)
{
	char *itunesdb_path;
	gboolean result;

	itunesdb_path = rb_ipod_get_itunesdb_path (volume);
	if (itunesdb_path != NULL) {
		result = g_file_test (itunesdb_path, G_FILE_TEST_EXISTS);
	} else {
		result = FALSE;
	}
	g_free (itunesdb_path);

	return result;
}

gboolean
rb_ipod_is_volume_ipod (GnomeVFSVolume *volume)
{
#ifdef HAVE_HAL
	gchar *udi;
#endif
	if (gnome_vfs_volume_get_volume_type (volume) != GNOME_VFS_VOLUME_TYPE_MOUNTPOINT) {
		return FALSE;
	}

#ifdef HAVE_HAL
	udi = gnome_vfs_volume_get_hal_udi (volume);
	if (udi != NULL) {
		gboolean result;

		result = hal_udi_is_ipod (udi);
		g_free (udi);
		if (result == FALSE) {
			return FALSE;
		}
	}
#endif
	
	return rb_ipod_volume_has_ipod_db (volume);
}

#ifdef HAVE_HAL_0_5

static gboolean
hal_udi_is_ipod (const char *udi)
{
	LibHalContext *ctx;
	DBusConnection *conn;
	char *parent_udi, *parent_name;
	gboolean result;
	DBusError error;

	result = FALSE;
	dbus_error_init (&error);
	
	conn = NULL;
	ctx = libhal_ctx_new ();
	if (ctx == NULL) {
		/* FIXME: should we return an error somehow so that we can 
		 * fall back to a check for iTunesDB presence instead ?
		 */
		rb_debug ("cannot connect to HAL");
		goto end;
	}
	conn = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
	if (conn == NULL || dbus_error_is_set (&error))
		goto end;

	libhal_ctx_set_dbus_connection (ctx, conn);
	if (!libhal_ctx_init (ctx, &error) || dbus_error_is_set (&error))
		goto end;

	parent_udi = libhal_device_get_property_string (ctx, udi,
			"info.parent", &error);
	if (parent_udi == NULL || dbus_error_is_set (&error))
		goto end;
		
	parent_name = libhal_device_get_property_string (ctx, parent_udi,
			"storage.model", &error);
	g_free (parent_udi);
	if (parent_name == NULL || dbus_error_is_set (&error))
		goto end;

	if (strcmp (parent_name, "iPod") == 0)
		result = TRUE;

	g_free (parent_name);
end:
	if (dbus_error_is_set (&error)) {
		rb_debug ("Error: %s\n", error.message);
		dbus_error_free (&error);
		dbus_error_init (&error);
	}

	if (ctx) {
		libhal_ctx_shutdown (ctx, &error);
		libhal_ctx_free(ctx);
	}

	dbus_error_free (&error);

	return result;
}

#elif HAVE_HAL_0_2

static gboolean
hal_udi_is_ipod (const char *udi)
{
	LibHalContext *ctx;
	char *parent_udi, *parent_name;
	gboolean result;

	result = FALSE;
	ctx = hal_initialize (NULL, FALSE);
	if (ctx == NULL) {
		/* FIXME: should we return an error somehow so that we can 
		 * fall back to a check for iTunesDB presence instead ?
		 */
		return FALSE;
	}
	parent_udi = hal_device_get_property_string (ctx, udi,
			"info.parent");
	parent_name = hal_device_get_property_string (ctx, parent_udi,
			"storage.model");
	g_free (parent_udi);

	if (parent_name != NULL && strcmp (parent_name, "iPod") == 0) {
		result = TRUE;
	}

	g_free (parent_name);
	hal_shutdown (ctx);

	return result;
}

#endif

static GList*
impl_get_ui_actions (RBSource *source)
{
	GList *actions = NULL;
 
	actions = g_list_prepend (actions, g_strdup ("RemovableSourceEject"));
 
	return actions;
}


static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/iPodSourcePopup");
	return TRUE;
}

static void
impl_delete_thyself (RBSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);

	itdb_free (priv->ipod_db);
	priv->ipod_db = NULL;

	RB_SOURCE_CLASS (rb_ipod_source_parent_class)->impl_delete_thyself (source);
}

static void 
rb_ipod_source_cmd_rename (GtkAction *action,
			   RBiPodSource *source)
{
	RBShell *shell;
	RBRemovableMediaManager *manager;
	RBSourceList *sourcelist;

	/* FIXME: this is pretty ugly, the sourcelist should automatically add
	 * a "rename" menu item for sources that have can_rename == TRUE.
	 * This is a bit trickier to handle though, since playlists want 
	 * to make rename sensitive/unsensitive instead of showing/hiding it
	 */
	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), 
		      "removable-media-manager", &manager, 
		      NULL);
	g_object_get (G_OBJECT (manager), "sourcelist", &sourcelist, NULL);
	g_object_unref (G_OBJECT (manager));
	g_object_unref (G_OBJECT (shell));
	
	rb_sourcelist_edit_source_name (sourcelist, RB_SOURCE (source)); 
	/* Once editing is done, notify::name will be fired on the source, and
	 * we'll catch that in our rename callback
	 */
	g_object_unref (sourcelist);
}
