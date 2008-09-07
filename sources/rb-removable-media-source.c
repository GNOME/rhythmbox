/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of removable media source object (based of the ipod source)
 *
 *  Copyright (C) 2004 Christophe Fergeau  <teuf@gnome.org>
 *  Copyright (C) 2005 James Livingston  <doclivingston@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grants permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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

#include "rhythmdb.h"
#include "eel-gconf-extensions.h"
#include "rb-removable-media-source.h"
#include "rb-removable-media-manager.h"
#include "rb-encoder.h"
#include "rb-stock-icons.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-util.h"
#include "rb-file-helpers.h"

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
#ifdef ENABLE_TRACK_TRANSFER
static void impl_paste (RBSource *source, GList *entries);
#endif
static gboolean impl_receive_drag (RBSource *asource, GtkSelectionData *data);
static guint impl_want_uri (RBSource *source, const char *uri);
static gboolean impl_uri_is_source (RBSource *source, const char *uri);

typedef struct
{
	GVolume *volume;
	GMount *mount;
} RBRemovableMediaSourcePrivate;

G_DEFINE_TYPE (RBRemovableMediaSource, rb_removable_media_source, RB_TYPE_BROWSER_SOURCE)
#define REMOVABLE_MEDIA_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_REMOVABLE_MEDIA_SOURCE, RBRemovableMediaSourcePrivate))

enum
{
	PROP_0,
	PROP_VOLUME,
	PROP_MOUNT,
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
#ifdef ENABLE_TRACK_TRANSFER
  	source_class->impl_paste = impl_paste;
#endif
  	source_class->impl_receive_drag = impl_receive_drag;
	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_delete = NULL;
	source_class->impl_get_config_widget = NULL;
	source_class->impl_show_popup = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_want_uri = impl_want_uri;
	source_class->impl_uri_is_source = impl_uri_is_source;

	browser_source_class->impl_get_paned_key = NULL;
	browser_source_class->impl_has_drop_support = (RBBrowserSourceFeatureFunc) rb_false_function;

	g_object_class_install_property (object_class,
					 PROP_VOLUME,
					 g_param_spec_object ("volume",
							      "Volume",
							      "GIO Volume",
							      G_TYPE_VOLUME,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_MOUNT,
					 g_param_spec_object ("mount",
							      "Mount",
							      "GIO Mount",
							      G_TYPE_MOUNT,
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
	GMount *mount;
	GIcon *icon = NULL;
	char *display_name;
	GdkPixbuf *pixbuf = NULL;
	RBRemovableMediaSourcePrivate *priv;

	source = G_OBJECT_CLASS(rb_removable_media_source_parent_class)
			->constructor (type, n_construct_properties, construct_properties);
	priv = REMOVABLE_MEDIA_SOURCE_GET_PRIVATE (source);

	/* prefer mount details to volume details, as the nautilus sidebar does */
	if (priv->mount != NULL) {
		mount = g_object_ref (priv->mount);
	} else if (priv->volume != NULL) {
		mount = g_volume_get_mount (priv->volume);
	} else {
		mount = NULL;
	}

	if (mount != NULL) {
		display_name = g_mount_get_name (mount);
		icon = g_mount_get_icon (mount);
		rb_debug ("details from mount: display name = %s, icon = %p", display_name, icon);
	} else if (priv->volume != NULL) {
		display_name = g_volume_get_name (priv->volume);
		icon = g_volume_get_icon (priv->volume);
		rb_debug ("details from volume: display name = %s, icon = %p", display_name, icon);
	} else {
		display_name = g_strdup ("Unknown Device");
		icon = g_themed_icon_new ("multimedia-player");
	}

	g_object_set (source, "name", display_name, NULL);
	g_free (display_name);

	if (icon == NULL) {
		rb_debug ("no icon set");
		pixbuf = NULL;
	} else if (G_IS_THEMED_ICON (icon)) {
		GtkIconTheme *theme;
		const char * const *names;
		gint size;
		int i;

		theme = gtk_icon_theme_get_default ();
		gtk_icon_size_lookup (RB_SOURCE_ICON_SIZE, &size, NULL);

		i = 0;
		names = g_themed_icon_get_names (G_THEMED_ICON (icon));
		while (names[i] != NULL && pixbuf == NULL) {
			rb_debug ("looking up themed icon: %s", names[i]);
			pixbuf = gtk_icon_theme_load_icon (theme, names[i], size, 0, NULL);
			i++;
		}

	} else if (G_IS_LOADABLE_ICON (icon)) {
		rb_debug ("loading of GLoadableIcons is not implemented yet");
		pixbuf = NULL;
	}

	rb_source_set_pixbuf (RB_SOURCE (source), pixbuf);
	if (pixbuf != NULL) {
		g_object_unref (pixbuf);
	}
	if (mount != NULL) {
		g_object_unref (mount);
	}
	g_object_unref (icon);

	return source;
}

static void
rb_removable_media_source_dispose (GObject *object)
{
	RBRemovableMediaSourcePrivate *priv = REMOVABLE_MEDIA_SOURCE_GET_PRIVATE (object);

	if (priv->volume) {
		g_object_unref (priv->volume);
		priv->volume = NULL;
	}
	if (priv->mount) {
		g_object_unref (priv->mount);
		priv->mount = NULL;
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
			g_object_unref (priv->volume);
		}
		priv->volume = g_value_get_object (value);
		if (priv->volume) {
			g_object_ref (priv->volume);
		}
		break;
	case PROP_MOUNT:
		if (priv->mount) {
			g_object_unref (priv->mount);
		}
		priv->mount = g_value_get_object (value);
		if (priv->mount) {
			g_object_ref (priv->mount);
		}
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
		g_value_set_object (value, priv->volume);
		break;
	case PROP_MOUNT:
		g_value_set_object (value, priv->mount);
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

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	g_object_unref (shell);

	g_object_get (source, "entry-type", &entry_type, NULL);
	rb_debug ("deleting all entries of type '%s'", entry_type->name);
	rhythmdb_entry_delete_by_type (db, entry_type);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	rhythmdb_commit (db);
	g_object_unref (db);
}

#ifdef ENABLE_TRACK_TRANSFER

struct _TrackAddedData {
	RBRemovableMediaSource *source;
	char *mimetype;
};

static void
_track_added_cb (RhythmDBEntry *entry, const char *uri, gint64 dest_size, struct _TrackAddedData *data)
{
	rb_removable_media_source_track_added (data->source, entry, uri, dest_size, data->mimetype);
	g_free (data->mimetype);
	g_free (data);
}

static void
impl_paste (RBSource *source, GList *entries)
{
	RBRemovableMediaManager *rm_mgr;
	RBShell *shell;
	GList *l;
	RhythmDBEntryType our_entry_type;
	RBEncoder *encoder;

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell,
		      "removable-media-manager", &rm_mgr,
		      NULL);
	g_object_unref (shell);

	g_object_get (source,
		      "entry-type", &our_entry_type,
		      NULL);

	encoder = rb_encoder_new ();

	for (l = entries; l != NULL; l = l->next) {
		RhythmDBEntry *entry;
		RhythmDBEntryType entry_type;
		GList *mime_types;
		const char *entry_mime;
		char *mimetype;
		char *extension;
		char *dest;
		struct _TrackAddedData *added_data;

		dest = NULL;
		mimetype = NULL;
		extension = NULL;
		mime_types = NULL;
		entry = (RhythmDBEntry *)l->data;
		entry_type = rhythmdb_entry_get_entry_type (entry);

		if (entry_type == our_entry_type ||
		    entry_type->category != RHYTHMDB_ENTRY_NORMAL) {
			goto impl_paste_end;
		}

		entry_mime = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE);
		/* hackish mapping of gstreamer media types to mime types; this
		 * should be easier when we do proper (deep) typefinding.
		 */
		if (strcmp (entry_mime, "audio/x-wav") == 0) {
			/* if it has a bitrate, assume it's mp3-in-wav */
			if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE) != 0)
				entry_mime = "audio/mpeg";
		} else if (strcmp (entry_mime, "audio/x-m4a") == 0) {
			entry_mime = "audio/aac";
		} else if (strcmp (entry_mime, "application/x-id3") == 0) {
			entry_mime = "audio/mpeg";
		} else if (strcmp (entry_mime, "audio/x-flac") == 0) {
			entry_mime = "audio/flac";
		}

		mime_types = rb_removable_media_source_get_mime_types (RB_REMOVABLE_MEDIA_SOURCE (source));
		if (mime_types != NULL && !rb_string_list_contains (mime_types, entry_mime)) {
			if (!rb_encoder_get_preferred_mimetype (encoder, mime_types, &mimetype, &extension)) {
				rb_debug ("failed to find acceptable mime type for %s", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
				goto impl_paste_end;
			}
		} else {
			const char *s;
			char       *path;

			rb_debug ("copying using existing format");
			path = rb_uri_get_short_path_name (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
			s = g_strrstr (path, ".");
			extension = (s != NULL) ? g_strdup (s + 1) : NULL;
			g_free (path);
		}

		dest = rb_removable_media_source_build_dest_uri (RB_REMOVABLE_MEDIA_SOURCE (source), entry, mimetype, extension);
		if (dest == NULL) {
			rb_debug ("could not create destination path for entry");
			goto impl_paste_end;
		}

		rb_list_deep_free (mime_types);
		if (mimetype != NULL)
			mime_types = g_list_prepend (NULL, g_strdup (mimetype));
		else
			mime_types = NULL;
		added_data = g_new0 (struct _TrackAddedData, 1);
		added_data->source = RB_REMOVABLE_MEDIA_SOURCE (source);
		added_data->mimetype = g_strdup (mimetype);
		rb_removable_media_manager_queue_transfer (rm_mgr, entry,
							   dest, mime_types,
							   (RBTransferCompleteCallback)_track_added_cb, added_data);
impl_paste_end:
		g_free (dest);
		g_free (mimetype);
		g_free (extension);
		if (mime_types)
			rb_list_deep_free (mime_types);
		if (entry_type)
			g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);
	}

	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, our_entry_type);
	g_object_unref (rm_mgr);
	g_object_unref (encoder);
}

#endif

static guint
impl_want_uri (RBSource *source, const char *uri)
{
	RBRemovableMediaSourcePrivate *priv = REMOVABLE_MEDIA_SOURCE_GET_PRIVATE (source);
	GVolume *volume;
	const char *uri_path;
	char *device_path;
	int retval;
	int len;

	retval = 0;

	/* A default version for use with the audio players
	 * that use mass storage */
	if (g_str_has_prefix (uri, "file://") == FALSE)
		return 0;
	uri_path = uri + strlen ("file://");

	if (priv->mount) {
		volume = g_mount_get_volume (priv->mount);
	} else if (priv->volume) {
		volume = g_object_ref (priv->volume);
	} else {
		return 0;
	}

	if (volume == NULL)
		return 0;

	device_path = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	g_object_unref (volume);
	if (device_path == NULL)
		return 0;

	len = strlen (uri_path);
	if (uri_path[len - 1] == '/') {
		if (strncmp (uri_path, device_path, len - 1) == 0)
			retval = 100;
	} else if (strcmp (uri_path, device_path) == 0)
		retval = 100;

	g_free (device_path);
	return retval;
}

static gboolean
impl_uri_is_source (RBSource *source, const char *uri)
{
	if (impl_want_uri (source, uri) == 100)
		return TRUE;
	return FALSE;
}

static RhythmDB *
get_db_for_source (RBSource *source)
{
	RBShell *shell;
	RhythmDB *db;

  	g_object_get (source, "shell", &shell, NULL);
  	g_object_get (shell, "db", &db, NULL);
  	g_object_unref (shell);

        return db;
}

static gboolean
impl_receive_drag (RBSource *asource, GtkSelectionData *data)
{
	GList *entries;
	RhythmDB *db;
	char *type;

	entries = NULL;
	type = gdk_atom_name (data->type);
        db = get_db_for_source (asource);

	if (strcmp (type, "text/uri-list") == 0) {
		GList *list;
		GList *i;

		rb_debug ("parsing uri list");
		list = rb_uri_list_parse ((const char *) data->data);

		for (i = list; i != NULL; i = g_list_next (i)) {
			char *uri;
			RhythmDBEntry *entry;

			if (i->data == NULL)
				continue;

			uri = i->data;
			entry = rhythmdb_entry_lookup_by_location (db, uri);

			if (entry == NULL) {
				/* add to the library */
				rb_debug ("received drop of unknown uri: %s", uri);
			} else {
				/* add to list of entries to copy */
				entries = g_list_prepend (entries, entry);
			}
			g_free (uri);
		}
		g_list_free (list);
	} else if (strcmp (type, "application/x-rhythmbox-entry") == 0) {
		char **list;
		char **i;

		rb_debug ("parsing entry ids");
		list = g_strsplit ((const char*)data->data, "\n", -1);
		for (i = list; *i != NULL; i++) {
			RhythmDBEntry *entry;
			gulong id;

			id = atoi (*i);
			entry = rhythmdb_entry_lookup_by_id (db, id);
			if (entry != NULL)
				entries = g_list_prepend (entries, entry);
		}

		g_strfreev (list);
	} else {
		rb_debug ("received unknown drop type");
	}

	g_object_unref (db);
	g_free (type);

	if (entries) {
		entries = g_list_reverse (entries);
		if (rb_source_can_paste (asource))
			rb_source_paste (asource, entries);
		g_list_free (entries);
	}

	return TRUE;
}

char *
rb_removable_media_source_build_dest_uri (RBRemovableMediaSource *source,
					  RhythmDBEntry *entry,
					  const char *mimetype,
					  const char *extension)
{
	RBRemovableMediaSourceClass *klass = RB_REMOVABLE_MEDIA_SOURCE_GET_CLASS (source);
	char *uri = NULL;
	char *sane_uri = NULL;

	if (klass->impl_build_dest_uri) {
		uri = klass->impl_build_dest_uri (source, entry, mimetype, extension);
	} else {
		uri = NULL;
	}

	sane_uri = rb_sanitize_uri_for_filesystem(uri);
	g_return_val_if_fail(sane_uri != NULL, NULL);
	g_free(uri);
	uri = sane_uri;

	rb_debug ("Built dest URI for mime='%s', extension='%s': '%s'",
		  mimetype,
		  extension,
		  uri);

	return uri;
}

GList *
rb_removable_media_source_get_mime_types (RBRemovableMediaSource *source)
{
	RBRemovableMediaSourceClass *klass = RB_REMOVABLE_MEDIA_SOURCE_GET_CLASS (source);

	if (klass->impl_get_mime_types)
		return klass->impl_get_mime_types (source);
	else
		return NULL;
}

void
rb_removable_media_source_track_added (RBRemovableMediaSource *source,
				       RhythmDBEntry *entry,
				       const char *uri,
				       guint64 filesize,
				       const char *mimetype)
{
	RBRemovableMediaSourceClass *klass = RB_REMOVABLE_MEDIA_SOURCE_GET_CLASS (source);
	gboolean add_to_db = TRUE;

	if (klass->impl_track_added)
		add_to_db = klass->impl_track_added (source, entry, uri, filesize, mimetype);

	if (add_to_db) {
		RhythmDBEntryType entry_type;
		RhythmDB *db;
		RBShell *shell;

		g_object_get (source, "shell", &shell, NULL);
		g_object_get (shell, "db", &db, NULL);
		g_object_unref (shell);

		g_object_get (source, "entry-type", &entry_type, NULL);
		rhythmdb_add_uri_with_types (db, uri, entry_type, RHYTHMDB_ENTRY_TYPE_INVALID, RHYTHMDB_ENTRY_TYPE_INVALID);
		g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

		g_object_unref (db);
	}
}

