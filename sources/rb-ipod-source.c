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
#ifdef HAVE_HAL
#include <libhal.h>
#include <dbus/dbus-glib.h>
#endif
#include <libgnomevfs/gnome-vfs-volume.h>
#include <libgnomevfs/gnome-vfs-volume-monitor.h>
#include "eel-gconf-extensions.h"
#include "rb-ipod-source.h"
#include "rb-stock-icons.h"
#include "rb-debug.h"

static void rb_ipod_source_init (RBiPodSource *source);
static void rb_ipod_source_finalize (GObject *object);
static void rb_ipod_source_class_init (RBiPodSourceClass *klass);
static GObject *rb_ipod_source_constructor (GType type, 
					    guint n_construct_properties,
					    GObjectConstructParam *construct_properties);

static gboolean rb_ipod_is_volume_ipod (GnomeVFSVolume *volume);
static void rb_ipod_volume_mounted_cb (GnomeVFSVolumeMonitor *monitor, 
				       GnomeVFSVolume *volume, gpointer data);
static void rb_ipod_volume_unmounted_cb (GnomeVFSVolumeMonitor *monitor,
					 GnomeVFSVolume *volume, 
					 gpointer data);
static void rb_ipod_plugged   (RBiPodSource *source, 
			       const gchar *mount_path,
			       GnomeVFSVolume *volume);
static void rb_ipod_unplugged (RBiPodSource *source);
static gchar *rb_ipod_get_mount_path (GnomeVFSVolume *volume);

static void rb_ipod_monitor (RBiPodSource *source, gboolean enable);
#ifdef HAVE_HAL
static gboolean ipod_itunesdb_monitor_hal (RBiPodSource *source,
					   gboolean enable);
#endif

struct RBiPodSourcePrivate
{
#ifdef HAVE_HAL
	LibHalContext *hal_ctxt;
#else
	/* This is a bit ugly, but this avoids bloating rb_ipod_plugged and 
	 * rb_ipod_unplugged with #ifdef
	 */
	gpointer hal_ctxt;
#endif
	GnomeVFSVolume *ipod_volume;
	iPodParser *parser;
	gchar *ipod_mount_path;
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
	object_class->constructor = rb_ipod_source_constructor;
}


static GObject *
rb_ipod_source_constructor (GType type, guint n_construct_properties,
			    GObjectConstructParam *construct_properties)
{
	GObject *object;
	RBiPodSourceClass *klass;
	GObjectClass *parent_class;  

	/* Invoke parent constructor. */
	klass = RB_IPOD_SOURCE_CLASS (g_type_class_peek (RB_TYPE_IPOD_SOURCE));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	object = parent_class->constructor (type,
					    n_construct_properties,
					    construct_properties);
  
	/* This needs to be done once the GObject properties are set
	 * since rb_ipod_monitor expect the properties on the
	 * RBiPodSource to be set (especially the "db" property)
	 */
	rb_ipod_monitor (RB_IPOD_SOURCE (object), TRUE);
	return object;
}


static void
rb_ipod_monitor (RBiPodSource *source, gboolean enable)
{
	GnomeVFSVolumeMonitor *monitor;

#ifdef HAVE_HAL
	if (ipod_itunesdb_monitor_hal (source, enable) != FALSE) {
		return;
	}
#endif
	
	monitor = gnome_vfs_get_volume_monitor ();

	if (enable) {
		GList *volumes;
		GList *it;

		/* Look for already plugged iPod */
		volumes = gnome_vfs_volume_monitor_get_mounted_volumes (monitor);
		for (it = volumes; it != NULL; it = it->next) {
			if (rb_ipod_is_volume_ipod (GNOME_VFS_VOLUME (it->data))) {
				rb_ipod_plugged (source, NULL,
						 GNOME_VFS_VOLUME (it->data));
			}
		}
		
		/* Monitor new (un)mounted file systems to look for an iPod */
		g_signal_connect (G_OBJECT (monitor), "volume-mounted", 
				  G_CALLBACK (rb_ipod_volume_mounted_cb), 
				  source);
		g_signal_connect (G_OBJECT (monitor), "volume-unmounted", 
				  G_CALLBACK (rb_ipod_volume_unmounted_cb), 
				  source);
	} else {
		g_signal_handlers_disconnect_by_func (G_OBJECT (monitor), 
						      G_CALLBACK (rb_ipod_volume_mounted_cb), 
						      source);
		g_signal_handlers_disconnect_by_func (G_OBJECT (monitor), 
						      G_CALLBACK (rb_ipod_volume_unmounted_cb), 
						      source);

	}
}

static void
rb_ipod_source_init (RBiPodSource *source)
{
	source->priv = g_new0 (RBiPodSourcePrivate, 1);
}


static void 
rb_ipod_source_finalize (GObject *object)
{
	RBiPodSource *source = RB_IPOD_SOURCE (object);

	rb_ipod_monitor (source, FALSE);

	if (source->priv->parser != NULL) {
		ipod_parser_destroy (source->priv->parser);
		source->priv->parser = NULL;
	}

	if (source->priv->ipod_volume != NULL) {
		gnome_vfs_volume_unref (source->priv->ipod_volume);
		source->priv->ipod_volume = NULL;
	}
	g_free (source->priv->ipod_mount_path);
	g_free (source->priv);
}


RBSource *
rb_ipod_source_new (RBShell *shell, RhythmDB *db, GtkActionGroup *actiongroup)
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
					  "action-group", actiongroup,
					  "visibility", FALSE,
					  NULL));

	rb_shell_register_entry_type_for_source (shell, source, 
						 RHYTHMDB_ENTRY_TYPE_IPOD);

	return source;
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
	rhythmdb_entry_set (RHYTHMDB (db), entry, propid, &value);
	g_value_unset (&value);
}

#define MAX_SONGS_LOADED_AT_ONCE 250

static gboolean
load_ipod_db_idle_cb (RBiPodSource *source)
{
	RhythmDBEntry *entry;
	RhythmDB *db;
	int i;

	g_object_get (G_OBJECT (source), "db", &db, NULL);
	g_assert (db != NULL);
	g_assert (source->priv->parser != NULL);

	for (i = 0; i < MAX_SONGS_LOADED_AT_ONCE; i++) {
		gchar *pc_path;
		gchar *mount_path;
		iPodItem *item;
		iPodSong *song;
		
		item = ipod_get_next_item (source->priv->parser);
		if ((item == NULL) || (item->type != IPOD_ITEM_SONG)) {
			ipod_item_destroy (item);
			ipod_parser_destroy (source->priv->parser);
			source->priv->parser = NULL;
			return FALSE;
		}
		song = (iPodSong *)item->data;
				
		/* Set URI */
		mount_path = source->priv->ipod_mount_path;
		pc_path = itunesdb_get_track_name_on_ipod (mount_path, song);
		entry = rhythmdb_entry_new (RHYTHMDB (db), 
					    RHYTHMDB_ENTRY_TYPE_IPOD,
					    pc_path);
		g_free (pc_path);

		rb_debug ("Adding %s from iPod", pc_path);

		/* Set track number */
		if (song->track_nr != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, song->track_nr);
			rhythmdb_entry_set (RHYTHMDB (db), entry, 
					    RHYTHMDB_PROP_TRACK_NUMBER, 
					    &value);
			g_value_unset (&value);
		}
		
		/* Set bitrate */
		if (song->bitrate != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, song->bitrate);
			rhythmdb_entry_set (RHYTHMDB (db), entry, 
					    RHYTHMDB_PROP_BITRATE, 
					    &value);
			g_value_unset (&value);
		}
		
		/* Set length */
		if (song->tracklen != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, song->tracklen/1000);
			rhythmdb_entry_set (RHYTHMDB (db), entry, 
					    RHYTHMDB_PROP_DURATION, 
					    &value);
			g_value_unset (&value);
		}
		
		/* Set file size */
		if (song->size != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_UINT64);
			g_value_set_uint64 (&value, song->size);
			rhythmdb_entry_set (RHYTHMDB (db), entry, 
					    RHYTHMDB_PROP_FILE_SIZE, 
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


		ipod_item_destroy (item);
	}

	return TRUE;
}

static void
rb_ipod_load_songs (RBiPodSource *source)
{
	RhythmDB *db;

	g_object_get (G_OBJECT (source), "db", &db, NULL);
	g_assert (db != NULL);

	source->priv->parser = ipod_parser_new (source->priv->ipod_mount_path);

	g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, 
			 (GSourceFunc)load_ipod_db_idle_cb,
			 source, NULL);
}

static void
rb_ipod_unload_songs (RBiPodSource *source)
{
	RhythmDB *db;

	g_object_get (G_OBJECT (source), "db", &db, NULL);
	rhythmdb_entry_delete_by_type (db, RHYTHMDB_ENTRY_TYPE_IPOD);
	rhythmdb_commit (db);
}


static void
rb_ipod_plugged (RBiPodSource *source, 
		 const gchar *mount_path,
		 GnomeVFSVolume *volume)
{
	rb_debug ("iPod plugged\n");

	if (source->priv->ipod_mount_path != NULL) {
		/* Only one iPod can be recognized at once */
		return;
	}

	if (source->priv->hal_ctxt == NULL) {
		source->priv->ipod_volume = volume;
		source->priv->ipod_mount_path = rb_ipod_get_mount_path (volume);
		gnome_vfs_volume_ref (volume);
	}  else {
		source->priv->ipod_mount_path = g_strdup (mount_path);
	}
	g_object_set (G_OBJECT (source), "visibility", TRUE, NULL);	
	rb_ipod_load_songs (source);
	/* FIXME: should we suspend this monitor until the iPod 
	 * database has been read and fed to rhythmbox?
	 */
}

static void
rb_ipod_unplugged (RBiPodSource *source)
{
	rb_debug ("iPod unplugged\n");
	
	g_assert (source->priv->ipod_mount_path != NULL);

	if (source->priv->hal_ctxt == NULL) {
		source->priv->ipod_volume = NULL;
		gnome_vfs_volume_unref (source->priv->ipod_volume);
	}
	g_free (source->priv->ipod_mount_path);
	source->priv->ipod_mount_path = NULL;
	g_object_set (G_OBJECT (source), "visibility", FALSE, NULL);
	rb_ipod_unload_songs (source);
}

RhythmDBEntryType rhythmdb_entry_ipod_get_type (void) 
{
	static RhythmDBEntryType ipod_type = -1;
       
	if (ipod_type == -1) {
		ipod_type = rhythmdb_entry_register_type ();
	}

	return ipod_type;
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
	mount_point = g_filename_from_uri (mount_point_uri, NULL, NULL);
	g_free (mount_point_uri);
	g_assert (mount_point != NULL);
	result = g_build_filename (mount_point, 
				   "iPod_Control/iTunes/iTunesDB",
				   NULL);
	g_free (mount_point);
	return result;
}

static gboolean
rb_ipod_is_volume_ipod (GnomeVFSVolume *volume)
{
	gchar *itunesdb_path;
	gboolean result = FALSE;

	if (gnome_vfs_volume_get_volume_type (volume) != GNOME_VFS_VOLUME_TYPE_MOUNTPOINT) {
		return FALSE;
	}
	
	itunesdb_path = rb_ipod_get_itunesdb_path (volume);
	result = g_file_test (itunesdb_path, G_FILE_TEST_EXISTS);
	g_free (itunesdb_path);

	return result;
}

static void 
rb_ipod_volume_mounted_cb (GnomeVFSVolumeMonitor *monitor,
			   GnomeVFSVolume *volume, 
			   gpointer data)
{
	RBiPodSource *source = RB_IPOD_SOURCE (data);

	if (source->priv->ipod_volume != NULL) {
		rb_debug ("iPod plugged while another one is already present, ignoring the new one");
		return;
	}

	if (rb_ipod_is_volume_ipod (volume)) {
		rb_ipod_plugged (source, NULL, volume);
	}
}

static void 
rb_ipod_volume_unmounted_cb (GnomeVFSVolumeMonitor *monitor,
			     GnomeVFSVolume *volume, 
			     gpointer data)
{
	RBiPodSource *source = RB_IPOD_SOURCE (data);

	g_assert (volume != NULL);

	if (source->priv->ipod_volume == volume) {
		rb_ipod_unplugged (source);
	}
}


#ifdef HAVE_HAL
static void
hal_mainloop_integration (LibHalContext *ctx,
		DBusConnection * dbus_connection)
{
	dbus_connection_setup_with_g_main (dbus_connection, NULL);
}

static void
hal_check_udi (LibHalContext *ctx, const char *udi)
{
	dbus_bool_t val;
	RBiPodSource *source;
	gchar *mount_path = NULL;

	source = hal_ctx_get_user_data (ctx);

	val = hal_device_get_property_bool (ctx, udi,
			"volume.is_mounted");

	if (val != FALSE) {
		g_message ("Mounted: %s\n", udi);
		mount_path = hal_device_get_property_string (ctx,
				       udi, "volume.mount_point");
	}

	source = hal_ctx_get_user_data (ctx);

	if (val) {
		rb_ipod_plugged (source, mount_path, NULL);
	} else {
		rb_ipod_unplugged (source);
	}
}

static void
hal_property_modified (LibHalContext *ctx,
		const char *udi,
		const char *key,
		dbus_bool_t is_removed,
		dbus_bool_t is_added)
{
	char *parent_udi, *parent_name;

	if (g_strcasecmp (key, "volume.is_mounted") != 0) {
		return;
	}

	parent_udi = hal_device_get_property_string (ctx, udi,
			"info.parent");
	parent_name = hal_device_get_property_string (ctx, parent_udi,
			"storage.model");
	g_free (parent_udi);

	if (parent_name != NULL && strcmp (parent_name, "iPod") == 0) {
		hal_check_udi (ctx, udi);
	}

	g_free (parent_name);
}

static void
hal_find_existing (LibHalContext *ctx)
{
	int i;
	int num_devices;
	char **devices_names;
	char *parent_udi;

	parent_udi = NULL;
	devices_names = hal_manager_find_device_string_match (ctx,
			"info.product", "iPod", &num_devices);

	for (i = 0; i < num_devices; i++)
	{
		char *string;

		string = hal_device_get_property_string (ctx,
				devices_names[i], "storage.drive_type");
		if (string == NULL || strcmp (string, "disk") != 0) {
			g_free (string);
			continue;
		}
		g_free (string);

		parent_udi = hal_device_get_property_string (ctx,
				devices_names[i], "info.udi");
		break;
	}

	g_message ("found iPod");

	g_strfreev (devices_names);

	if (parent_udi == NULL) {
		return;
	}

	devices_names = hal_manager_find_device_string_match (ctx,
			"info.parent", parent_udi, &num_devices);

	g_free (parent_udi);

	for (i = 0; i < num_devices; i++)
	{
		gboolean mounted;

		mounted = hal_device_get_property_bool (ctx,
				devices_names[i], "volume.is_mounted");
		if (mounted != FALSE) {
			g_message ("found udi %s", devices_names[i]);
			hal_check_udi (ctx, devices_names[i]);
			break;
		}
	}

	g_strfreev (devices_names);
}

static gboolean
ipod_itunesdb_monitor_hal (RBiPodSource *source, gboolean enable)
{
	if (enable) {
		LibHalFunctions hal_functions = {
			hal_mainloop_integration,
			NULL, /* hal_device_added */
			NULL, /* hal_device_removed */
			NULL, /* hal_device_new_capability */
			NULL, /* hal_device_lost_capability */
			hal_property_modified,
			NULL  /* hal_device_condition */
		};

		source->priv->hal_ctxt = hal_initialize (&hal_functions, FALSE);
		if (source->priv->hal_ctxt == NULL) {
			return FALSE;
		}

		if (hal_device_property_watch_all (source->priv->hal_ctxt)) {
			return FALSE;
		}

		hal_ctx_set_user_data (source->priv->hal_ctxt, source);

		hal_find_existing (source->priv->hal_ctxt);
	} else {
		if (source->priv->hal_ctxt == NULL) {
			return FALSE;
		}
		hal_shutdown (source->priv->hal_ctxt);
		source->priv->hal_ctxt = NULL;
	}

	return TRUE;
}
#endif /* HAVE_HAL */
