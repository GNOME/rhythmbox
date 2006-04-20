/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of removable media source object (based of the ipod source)
 *
 *  Copyright (C) 2004 Christophe Fergeau  <teuf@gnome.org>
 *  Copyright (C) 2005 James Livingston  <jrl@ids.org.au>
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

#include <config.h>

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtkicontheme.h>
#include <gtk/gtkiconfactory.h>
#include <libgnomevfs/gnome-vfs-volume.h>
#include <libgnomevfs/gnome-vfs-volume-monitor.h>

#include "rhythmdb.h"
#include "eel-gconf-extensions.h"
#include "rb-removable-media-source.h"
#include "rb-stock-icons.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-util.h"

static GObject *rb_removable_media_source_constructor (GType type,
						       guint n_construct_properties,
						       GObjectConstructParam *construct_properties);
static void rb_removable_media_source_dispose (GObject *object);

static void rb_removable_media_source_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_removable_media_source_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);

static void impl_delete_thyself (RBSource *source);


typedef struct
{
	GnomeVFSVolume *volume;
} RBRemovableMediaSourcePrivate;

G_DEFINE_TYPE (RBRemovableMediaSource, rb_removable_media_source, RB_TYPE_BROWSER_SOURCE)
#define REMOVABLE_MEDIA_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_REMOVABLE_MEDIA_SOURCE, RBRemovableMediaSourcePrivate))

enum
{
	PROP_0,
	PROP_VOLUME,
};



static void
rb_removable_media_source_class_init (RBRemovableMediaSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBBrowserSourceClass *browser_source_class = RB_BROWSER_SOURCE_CLASS (klass);

	object_class->constructor = rb_removable_media_source_constructor;
	object_class->dispose = rb_removable_media_source_dispose;
	object_class->set_property = rb_removable_media_source_set_property;
	object_class->get_property = rb_removable_media_source_get_property;

	source_class->impl_delete_thyself = impl_delete_thyself;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_paste = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_receive_drag = NULL;
	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_paste = NULL;
	source_class->impl_delete = NULL;
	source_class->impl_get_config_widget = NULL;
	source_class->impl_show_popup = (RBSourceFeatureFunc) rb_false_function;

	browser_source_class->impl_get_paned_key = NULL;
	browser_source_class->impl_has_first_added_column = (RBBrowserSourceFeatureFunc) rb_false_function;
	browser_source_class->impl_has_drop_support = (RBBrowserSourceFeatureFunc) rb_false_function;

	g_object_class_install_property (object_class,
					 PROP_VOLUME,
					 g_param_spec_object ("volume",
							      "Volume",
							      "GnomeVfs Volume",
							      GNOME_VFS_TYPE_VOLUME,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBRemovableMediaSourcePrivate));
}

static void
rb_removable_media_source_init (RBRemovableMediaSource *self)
{
}

static GObject *
rb_removable_media_source_constructor (GType type, guint n_construct_properties,
				       GObjectConstructParam *construct_properties)
{
	GObject *source; 
	GnomeVFSVolume *volume;
	GnomeVFSDrive *drive;
	char *display_name;
	gint size;
	char *icon_name;
	GtkIconTheme *theme;
	GdkPixbuf *pixbuf;

	source = G_OBJECT_CLASS(rb_removable_media_source_parent_class)
			->constructor (type, n_construct_properties, construct_properties);

	g_object_get (source, "volume", &volume, NULL);
	drive = gnome_vfs_volume_get_drive (volume);
	if (drive != NULL) {
		display_name = gnome_vfs_drive_get_display_name (drive);
		gnome_vfs_drive_unref (drive);
	} else {
		display_name = gnome_vfs_volume_get_display_name (volume);
	}
	g_object_set (source, "name", display_name, NULL);
	g_free (display_name);

	icon_name = gnome_vfs_volume_get_icon (volume);
	theme = gtk_icon_theme_get_default ();
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
	pixbuf = gtk_icon_theme_load_icon (theme, icon_name, size, 0, NULL);
	g_free (icon_name);

	rb_source_set_pixbuf (RB_SOURCE (source), pixbuf);
	if (pixbuf != NULL) {
		g_object_unref (pixbuf);
	}

	g_object_unref (volume);

	return source;
}


static void 
rb_removable_media_source_dispose (GObject *object)
{
	RBRemovableMediaSourcePrivate *priv = REMOVABLE_MEDIA_SOURCE_GET_PRIVATE (object);

	if (priv->volume) {
		gnome_vfs_volume_unref (priv->volume);
		priv->volume = NULL;
	}

	G_OBJECT_CLASS (rb_removable_media_source_parent_class)->dispose (object);
}

static void
rb_removable_media_source_set_property (GObject *object,
				        guint prop_id,
				        const GValue *value,
				        GParamSpec *pspec)
{
	RBRemovableMediaSourcePrivate *priv = REMOVABLE_MEDIA_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_VOLUME:
		if (priv->volume) {
			gnome_vfs_volume_unref (priv->volume);
		}
		priv->volume = g_value_get_object (value);
		gnome_vfs_volume_ref (priv->volume);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_removable_media_source_get_property (GObject *object,
				        guint prop_id,
				        GValue *value,
				        GParamSpec *pspec)
{
	RBRemovableMediaSourcePrivate *priv = REMOVABLE_MEDIA_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_VOLUME:
		gnome_vfs_volume_ref (priv->volume);
		g_value_take_object (value, priv->volume);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_delete_thyself (RBSource *source)
{
	RhythmDB *db;
	RBShell *shell;
	RhythmDBEntryType entry_type;

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "db", &db, NULL);
	g_object_unref (G_OBJECT (shell));

	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	rhythmdb_entry_delete_by_type (db, entry_type);
	rhythmdb_commit (db);
	g_object_unref (db);
}
