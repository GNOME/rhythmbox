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
#if defined(HAVE_HAL_0_5) || defined(HAVE_HAL_0_2)
#define HAVE_HAL 1
#endif


#include <gtk/gtktreeview.h>
#include <gtk/gtkicontheme.h>
#include <string.h>
#include "itunesdb.h"
#include "rhythmdb.h"
#include <libgnome/gnome-i18n.h>
#ifdef HAVE_HAL
#include <libhal.h>
#include <dbus/dbus.h>
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
static gboolean hal_udi_is_ipod (const char *udi);
#endif

struct RBiPodSourcePrivate
{
	GnomeVFSVolume *ipod_volume;
	iPodParser *parser;
	gchar *ipod_mount_path;
};

static GObjectClass *parent_class = NULL;


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

	parent_class = g_type_class_peek_parent (klass);

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

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GdkPixbuf *
rb_ipod_get_icon (void)
{
	GdkPixbuf *icon;
	GtkIconTheme *theme;

	theme = gtk_icon_theme_get_default ();
	icon = gtk_icon_theme_load_icon (theme, "gnome-dev-ipod", 24, 0, NULL);

	if (icon == NULL) {
		/* gnome-dev-ipod is only available in gnome 2.8, so fallback
		 * to an icon provided by rhythmbox for older gnome 
		 */
		GtkWidget *dummy;

		dummy = gtk_tree_view_new ();
		icon = gtk_widget_render_icon (dummy, RB_STOCK_IPOD,
					       GTK_ICON_SIZE_LARGE_TOOLBAR,
					       NULL);
		gtk_widget_destroy (dummy);
	}

	return icon;
}

RBSource *
rb_ipod_source_new (RBShell *shell)
{
	RBSource *source;

	source = RB_SOURCE (g_object_new (RB_TYPE_IPOD_SOURCE,
					  "name", _("iPod"),
					  "entry-type", RHYTHMDB_ENTRY_TYPE_IPOD,
					  "internal-name", "<ipod>",
					  "icon", rb_ipod_get_icon (),
					  "shell", shell,
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
	rhythmdb_entry_set_nonotify (RHYTHMDB (db), entry, propid, &value);
	g_value_unset (&value);
}

#define MAX_SONGS_LOADED_AT_ONCE 250

static gboolean
load_ipod_db_idle_cb (RBiPodSource *source)
{
	RhythmDBEntry *entry;
	RBShell *shell;
	RhythmDB *db;
	int i;

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "db", &db, NULL);
	g_object_unref (G_OBJECT (shell));

	g_assert (db != NULL);
	g_assert (source->priv->parser != NULL);

	for (i = 0; i < MAX_SONGS_LOADED_AT_ONCE; i++) {
		gchar *pc_path, *pc_vfs_path;
		gchar *mount_path;
		iPodItem *item;
		iPodSong *song;
		
		item = ipod_get_next_item (source->priv->parser);
		if ((item == NULL) || (item->type != IPOD_ITEM_SONG)) {
			ipod_item_destroy (item);
			ipod_parser_destroy (source->priv->parser);
			source->priv->parser = NULL;
			g_object_unref (G_OBJECT (db));
			return FALSE;
		}
		song = (iPodSong *)item->data;
				
		/* Set URI */
		mount_path = source->priv->ipod_mount_path;
		pc_path = itunesdb_get_track_name_on_ipod (mount_path, song);
		pc_vfs_path = g_strdup_printf ("file://%s", pc_path);
		g_free (pc_path);
		entry = rhythmdb_entry_new (RHYTHMDB (db), 
					    RHYTHMDB_ENTRY_TYPE_IPOD,
					    pc_vfs_path);
		g_free (pc_vfs_path);

		rb_debug ("Adding %s from iPod", pc_path);

		/* Set track number */
		if (song->track_nr != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, song->track_nr);
			rhythmdb_entry_set_nonotify (RHYTHMDB (db), entry, 
						     RHYTHMDB_PROP_TRACK_NUMBER, 
						     &value);
			g_value_unset (&value);
		}

		/* Set disc number */
		if (song->cd_nr != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, song->cd_nr);
			rhythmdb_entry_set_nonotify (RHYTHMDB (db), entry, 
						     RHYTHMDB_PROP_DISC_NUMBER, 
						     &value);
			g_value_unset (&value);
		}
		
		/* Set bitrate */
		if (song->bitrate != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, song->bitrate);
			rhythmdb_entry_set_nonotify (RHYTHMDB (db), entry, 
						     RHYTHMDB_PROP_BITRATE, 
						     &value);
			g_value_unset (&value);
		}
		
		/* Set length */
		if (song->tracklen != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, song->tracklen/1000);
			rhythmdb_entry_set_nonotify (RHYTHMDB (db), entry, 
						     RHYTHMDB_PROP_DURATION, 
						     &value);
			g_value_unset (&value);
		}
		
		/* Set file size */
		if (song->size != 0) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_UINT64);
			g_value_set_uint64 (&value, song->size);
			rhythmdb_entry_set_nonotify (RHYTHMDB (db), entry, 
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

	g_object_unref (G_OBJECT (db));
	return TRUE;
}

static int
rb_ipod_load_songs (RBiPodSource *source)
{
	source->priv->parser = ipod_parser_new (source->priv->ipod_mount_path);
	if (source->priv->parser == NULL) {
		return -1;
	}
	g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, 
			 (GSourceFunc)load_ipod_db_idle_cb,
			 source, NULL);

	return 0;
}

static void
rb_ipod_unload_songs (RBiPodSource *source)
{
	RhythmDB *db;

	g_object_get (G_OBJECT (source), "db", &db, NULL);
	rhythmdb_entry_delete_by_type (db, RHYTHMDB_ENTRY_TYPE_IPOD);
	rhythmdb_commit (db);
	g_object_unref (db);
}


static void
rb_ipod_plugged (RBiPodSource *source, 
		 const gchar *mount_path,
		 GnomeVFSVolume *volume)
{
	int res;

	rb_debug ("iPod plugged\n");

	if (source->priv->ipod_mount_path != NULL) {
		/* Only one iPod can be recognized at once */
		return;
	}
	
	source->priv->ipod_mount_path = rb_ipod_get_mount_path (volume);
	res = rb_ipod_load_songs (source);
	if (res == 0) {
		source->priv->ipod_volume = volume;
		gnome_vfs_volume_ref (volume);
		g_object_set (G_OBJECT (source), "visibility", TRUE, NULL);
	} else {
		g_free (source->priv->ipod_mount_path);
		source->priv->ipod_mount_path = NULL;
	}
	/* FIXME: should we suspend this monitor until the iPod 
	 * database has been read and fed to rhythmbox?
	 */
}

static void
rb_ipod_unplugged (RBiPodSource *source)
{
	rb_debug ("iPod unplugged\n");
	
	g_assert (source->priv->ipod_mount_path != NULL);

	gnome_vfs_volume_unref (source->priv->ipod_volume);
	source->priv->ipod_volume = NULL;
	
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
rb_ipod_is_volume_ipod (GnomeVFSVolume *volume)
{
	gchar *itunesdb_path;
	gboolean result = FALSE;
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
		return result;
	}
#endif
	
	itunesdb_path = rb_ipod_get_itunesdb_path (volume);
	if (itunesdb_path != NULL) {
		result = g_file_test (itunesdb_path, G_FILE_TEST_EXISTS);
		g_free (itunesdb_path);
	}

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
		g_print ("Error: %s\n", error.message);
		goto end;
	}
	conn = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
	if (conn == NULL) {
		g_print ("Error: %s\n", error.message);
		goto end;
	}
	libhal_ctx_set_dbus_connection (ctx, conn);
	parent_udi = libhal_device_get_property_string (ctx, udi,
			"info.parent", &error);
	parent_name = libhal_device_get_property_string (ctx, parent_udi,
			"storage.model", &error);
	g_free (parent_udi);
	if (parent_name != NULL && strcmp (parent_name, "iPod") == 0) {
		result = TRUE;
	}

	g_free (parent_name);
end:
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
