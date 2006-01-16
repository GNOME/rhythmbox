/*
 *  arch-tag: Implementation of audiocd source object (based of the ipod source)
 *
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */


/*
 * TODO
 *    * handle cases where MusicBrainz returns multiple albums
 *    * use release-date metadata (can have multiple values)
 */

#include <config.h>

#include <gtk/gtktreeview.h>
#include <gtk/gtkicontheme.h>
#include <string.h>
#include "rhythmdb.h"
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-volume.h>
#include <libgnomevfs/gnome-vfs-volume-monitor.h>
#include <gst/gst.h>
#include <totem-disc.h>
#include "eel-gconf-extensions.h"
#include "rb-audiocd-source.h"
#include "rb-util.h"
#include "rb-stock-icons.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-player.h"
#include "rhythmdb.h"
#ifdef HAVE_MUSICBRAINZ
#include "sj-metadata-musicbrainz.h"
#include "sj-structures.h"
#endif


static void rb_audiocd_source_dispose (GObject *object);
static GObject *rb_audiocd_source_constructor (GType type, guint n_construct_properties,
					        GObjectConstructParam *construct_properties);

static gboolean impl_show_popup (RBSource *source);
static void impl_delete_thyself (RBSource *source);

static gboolean rb_audiocd_load_songs (RBAudioCdSource *source);
static void rb_audiocd_load_metadata (RBAudioCdSource *source, RhythmDB *db);
static void rb_audiocd_load_metadata_cancel (RBAudioCdSource *source);

typedef struct
{
	gchar *device_path;
	GList *tracks;

	GstElement *pipeline, *cdda, *fakesink;
	
#ifdef HAVE_MUSICBRAINZ
	SjMetadata *metadata;
#endif
} RBAudioCdSourcePrivate;


G_DEFINE_TYPE (RBAudioCdSource, rb_audiocd_source, RB_TYPE_REMOVABLE_MEDIA_SOURCE)
#define AUDIOCD_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_AUDIOCD_SOURCE, RBAudioCdSourcePrivate))


static void
rb_audiocd_source_class_init (RBAudioCdSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->constructor = rb_audiocd_source_constructor;
	object_class->dispose = rb_audiocd_source_dispose;

	/* don't bother showing the browser/search bits */
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_false_function;

	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_delete_thyself = impl_delete_thyself;
	source_class->impl_get_browser_key = (RBSourceStringFunc) rb_null_function;

	g_type_class_add_private (klass, sizeof (RBAudioCdSourcePrivate));
}

static void
rb_audiocd_source_init (RBAudioCdSource *self)
{

}

static void 
rb_audiocd_source_dispose (GObject *object)
{
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (object);

	if (priv->device_path) {
		g_free (priv->device_path);
		priv->device_path = NULL;
	}
	if (priv->tracks) {
		g_list_free (priv->tracks);
		priv->tracks = NULL;
	}
	if (priv->pipeline) {
		gst_object_unref (GST_OBJECT (priv->pipeline));
		priv->pipeline = NULL;
	}

	G_OBJECT_CLASS (rb_audiocd_source_parent_class)->dispose (object);
}



static GObject *
rb_audiocd_source_constructor (GType type, guint n_construct_properties,
			       GObjectConstructParam *construct_properties)
{
	GObjectClass *klass, *parent_class; 
	RBAudioCdSource *source; 

	klass = G_OBJECT_CLASS (g_type_class_peek (type));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	source = RB_AUDIOCD_SOURCE (parent_class->constructor (type, n_construct_properties, construct_properties));

	g_idle_add ((GSourceFunc)rb_audiocd_load_songs, source);

	return G_OBJECT (source);
}

RBRemovableMediaSource *
rb_audiocd_source_new (RBShell *shell, GnomeVFSVolume *volume)
{
	char *device_path;
	GObject *source;
	RhythmDBEntryType entry_type;

	g_assert (rb_audiocd_is_volume_audiocd (volume));

	entry_type =  rhythmdb_entry_register_type ();

	device_path = gnome_vfs_volume_get_device_path (volume);

	source = g_object_new (RB_TYPE_AUDIOCD_SOURCE,
			       "entry-type", entry_type,
			       "volume", volume,
			       "shell", shell,
			       "sorting-key", NULL,
			       NULL);

	g_free (device_path);

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_REMOVABLE_MEDIA_SOURCE (source);
}

static void 
entry_set_string_prop (RhythmDB *db, RhythmDBEntry *entry, gboolean is_inserted,
		       RhythmDBPropType propid, const char *str)
{
	GValue value = {0,};

	if (!str)
		str = _("Unknown");

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, str);
	if (is_inserted)
		rhythmdb_entry_set (RHYTHMDB (db), entry, propid, &value);
	else
		rhythmdb_entry_set_uninserted (RHYTHMDB (db), entry, propid, &value);
	g_value_unset (&value);
}

static RhythmDBEntry*
rb_audiocd_create_track_entry (RBAudioCdSource *source, RhythmDB *db, guint track_number)
{
	RhythmDBEntry *entry;
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
	char *audio_path;
	GValue value = {0, };
	gchar *str;
	RhythmDBEntryType entry_type;

	audio_path = g_strdup_printf ("cdda://%s:%d", priv->device_path, track_number);

	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	rb_debug ("Audio CD - create entry for track %d from %s", track_number, audio_path);
	entry = rhythmdb_entry_new (db, entry_type, audio_path);
	if (entry == NULL) {
		g_free (audio_path);
		return NULL;
	}

	/* generate track # */
	g_value_init (&value, G_TYPE_ULONG);
	g_value_set_ulong (&value, track_number);
	rhythmdb_entry_set_uninserted (db, entry,
			    RHYTHMDB_PROP_TRACK_NUMBER, 
			    &value);
	g_value_unset (&value);

	/* generate track name */
	g_value_init (&value, G_TYPE_STRING);
	str = g_strdup_printf (_("Track %u"), track_number);
	g_value_take_string (&value, str);
	rhythmdb_entry_set_uninserted (db, entry,
			    RHYTHMDB_PROP_TITLE, 
			    &value);
	g_value_unset (&value);

	/* determine the duration */
	{
		GstFormat time_format = GST_FORMAT_TIME;
		GstFormat track_format = gst_format_get_by_nick ("track");
		gint64 duration;
		gboolean result;
#ifdef HAVE_GSTREAMER_0_8
		GstEvent *event;

		event = gst_event_new_seek (track_format | GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH, 
					    (guint64) track_number - 1);
		result = gst_element_send_event (priv->fakesink, event);

		if (result)
			result = gst_element_query (priv->fakesink, GST_QUERY_TOTAL, &time_format, &duration);
#elif HAVE_GSTREAMER_0_10
                result = gst_element_seek (priv->fakesink, 1.0, track_format, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, (guint64) track_number - 1, GST_SEEK_TYPE_NONE, -1);
		if (result) {
			result = gst_element_query_duration (priv->fakesink, &time_format, &duration) && time_format == GST_FORMAT_TIME;
		}
#endif

		if (result) {
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, (gulong)(duration / GST_SECOND));
			rhythmdb_entry_set_uninserted (db, entry,
					    RHYTHMDB_PROP_DURATION, 
					    &value);
			g_value_unset (&value);
		} else {
			g_warning ("Failed to query cd track duration");
		}
	}

	entry_set_string_prop (db, entry, FALSE, RHYTHMDB_PROP_ARTIST, NULL);
	entry_set_string_prop (db, entry, FALSE, RHYTHMDB_PROP_ALBUM, NULL);
	entry_set_string_prop (db, entry, FALSE, RHYTHMDB_PROP_GENRE, NULL);

	rhythmdb_commit (db);
	g_free (audio_path);

	return entry;
}

static gboolean
rb_audiocd_get_cd_info (RBAudioCdSource *source, gint64 *num_tracks)
{
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
	GstFormat fmt = gst_format_get_by_nick ("track");
#ifdef HAVE_GSTREAMER_0_8
	if (!gst_element_query (priv->cdda, GST_QUERY_TOTAL, &fmt, num_tracks)) {
		rb_debug ("failed to read cd track count");
		return FALSE;
	}
#elif HAVE_GSTREAMER_0_10
	GstFormat out_fmt = fmt;
	if (!gst_element_query_duration (priv->cdda, &out_fmt, num_tracks) || out_fmt != fmt) {
		return FALSE;
	}
#endif

	return TRUE;
}

static gboolean
rb_audiocd_scan_songs (RBAudioCdSource *source, RhythmDB *db)
{
	gint64 i, num_tracks;
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
#ifdef HAVE_GSTREAMER_0_10
        GstStateChangeReturn ret;
#endif
	gboolean ok = TRUE;

#ifdef HAVE_GSTREAMER_0_8
	if (gst_element_set_state (priv->pipeline, GST_STATE_PAUSED) != GST_STATE_SUCCESS) {
		rb_error_dialog (NULL, _("Couldn't load Audio CD"),
					_("Rhythmbox couldn't access the CD."));
		ok = FALSE;
	}
#elif HAVE_GSTREAMER_0_10
	ret = gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);
	if (ret == GST_STATE_CHANGE_ASYNC) {
		ret = gst_element_get_state (priv->pipeline, NULL, NULL, 3 * GST_SECOND);
	}
        if (ret == GST_STATE_CHANGE_FAILURE) {
		rb_error_dialog (NULL, _("Couldn't load Audio CD"),
					_("Rhythmbox couldn't access the CD."));
		ok = FALSE;
	}
#endif

	if (ok && !rb_audiocd_get_cd_info (source, &num_tracks))
	{
		rb_error_dialog (NULL, _("Couldn't load Audio CD"),
					_("Rhythmbox couldn't read the CD information."));
		ok = FALSE;
	}

	if (ok) {
		rb_debug ("importing Audio Cd %s - %d tracks", priv->device_path, num_tracks);
		for (i = 1; i <= num_tracks; i++) {
			RhythmDBEntry* entry = rb_audiocd_create_track_entry (source, db, i);

			if (entry)
				priv->tracks = g_list_prepend (priv->tracks, entry);
			else
				g_warning ("Could not create audio cd track entry");
		}
		priv->tracks = g_list_reverse (priv->tracks);
	}

#ifdef HAVE_GSTREAMER_0_8
	if (gst_element_set_state (priv->pipeline, GST_STATE_NULL) != GST_STATE_SUCCESS) {
#elif HAVE_GSTREAMER_0_10
	if (gst_element_set_state (priv->pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
#endif
		rb_debug ("failed to set cd state");
	}

	return ok;
}

#ifdef HAVE_MUSICBRAINZ
static void
metadata_cb (SjMetadata *metadata, GList *albums, GError *error, RBAudioCdSource *source)
{
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
	GList *cd_track = priv->tracks;
	RBShell *shell;
	RhythmDB *db;

	g_assert (metadata == priv->metadata);

	if (error != NULL) {
		rb_debug ("Failed to load cd metadata: %s\n", error->message);
		/* TODO display error to user? */
		g_error_free (error);
		g_object_unref (metadata);
		return;
	}
	if (cd_track == NULL) {
		/* empty cd? */
		return;
	}

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "db", &db, NULL);
	g_object_unref (G_OBJECT (shell));

	/*while (albums) {*/
		AlbumDetails *album;
		album = (AlbumDetails*)albums->data;

		g_object_set (G_OBJECT (source), "name", album->title, NULL);

		while (album->tracks) {
			TrackDetails *track = (TrackDetails*)album->tracks->data;
			RhythmDBEntry *entry = cd_track->data;
			GValue value = {0, };

			g_assert (cd_track != NULL);
			/* record track info in entry*/
			entry_set_string_prop (db, entry, TRUE, RHYTHMDB_PROP_TITLE, track->title);
			entry_set_string_prop (db, entry, TRUE, RHYTHMDB_PROP_ARTIST, track->artist);
			entry_set_string_prop (db, entry, TRUE, RHYTHMDB_PROP_ALBUM, album->title);
			entry_set_string_prop (db, entry, TRUE, RHYTHMDB_PROP_GENRE, album->genre);

			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, track->duration);
			rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DURATION, &value);
			g_value_unset (&value);
			
			/*album->release_date (has multiple values)*/
			rhythmdb_commit (db);

			album->tracks = g_list_next (album->tracks);
			cd_track = g_list_next (cd_track);
		}

		g_assert (cd_track == NULL);
		/*albums = g_list_next (albums);
	}*/

	g_object_unref (metadata);
	priv->metadata = NULL;
	g_object_unref (G_OBJECT (db));
}

static void
metadata_cancelled_cb (SjMetadata *metadata, GList *albums, GError *error, gpointer old_source)
{
	/* NOTE: the source may have been finalised, and so should NOT be used*/
	g_object_unref (metadata);
}
#endif

static void
rb_audiocd_load_metadata (RBAudioCdSource *source, RhythmDB *db)
{
#ifdef HAVE_MUSICBRAINZ
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);

	priv->metadata = (SjMetadata*)sj_metadata_musicbrainz_new();
	sj_metadata_set_cdrom (priv->metadata, priv->device_path);

	g_signal_connect (G_OBJECT (priv->metadata), "metadata",
			  G_CALLBACK (metadata_cb), source);
	sj_metadata_list_albums (priv->metadata, NULL);
#endif
}

static void
rb_audiocd_load_metadata_cancel (RBAudioCdSource *source)
{
#ifdef HAVE_MUSICBRAINZ
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);

	if (priv->metadata) {
		g_signal_handlers_disconnect_by_func (G_OBJECT (priv->metadata),
						      G_CALLBACK (metadata_cb), source);
		g_signal_connect (G_OBJECT (priv->metadata), "metadata",
				  G_CALLBACK (metadata_cancelled_cb), source);
	}
#endif
}

static gboolean
rb_audiocd_load_songs (RBAudioCdSource *source)
{
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
	RBShell *shell;
	RhythmDB *db;
	GnomeVFSVolume *volume;
	RBEntryView *entry_view;
	
	g_object_get (G_OBJECT (source), "volume", &volume, NULL);
	priv->device_path = gnome_vfs_volume_get_device_path (volume);
	g_object_unref (G_OBJECT (volume));
	
	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "db", &db, NULL);
	g_object_unref (G_OBJECT (shell));


	/* we want audio cds to sort by track# by default */
	entry_view = rb_source_get_entry_view (RB_SOURCE (source));
	rb_entry_view_set_sorting_order (entry_view, "Track", GTK_SORT_ASCENDING);


	rb_debug ("loading Audio CD from %s", priv->device_path);
	/* create a cdda gstreamer element, to get cd info from */
	priv->cdda = gst_element_make_from_uri (GST_URI_SRC, "cdda://", NULL);
	if (!priv->cdda) {
		rb_error_dialog (NULL, _("Couldn't load Audio CD"),
					_("Rhythmbox could not get access to the CD device."));
		goto error_out;
	}

	rb_debug ("cdda longname: %s", gst_element_factory_get_longname (gst_element_get_factory (priv->cdda)));
#ifdef HAVE_GSTREAMER_0_8
	gst_element_set (priv->cdda, "device", priv->device_path, NULL);
#elif HAVE_GSTREAMER_0_10
	g_object_set (G_OBJECT (priv->cdda), "device", priv->device_path, NULL);
#endif
	
	priv->pipeline = gst_pipeline_new ("pipeline");
	priv->fakesink = gst_element_factory_make ("fakesink", "fakesink");
	gst_bin_add_many (GST_BIN (priv->pipeline), priv->cdda, priv->fakesink, NULL);
	gst_element_link (priv->cdda, priv->fakesink);
	
	if (rb_audiocd_scan_songs (source, db))
		rb_audiocd_load_metadata (source, db);

error_out:
	g_object_unref (G_OBJECT (db));

	return FALSE;
}

static void
impl_delete_thyself (RBSource *source)
{
	RhythmDB *db;
	RBShell *shell;
	RhythmDBEntryType entry_type;
	rb_debug ("audio cd ejected\n");

	/* cancel the loading of metadata */
	rb_audiocd_load_metadata_cancel (RB_AUDIOCD_SOURCE (source));

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "db", &db, NULL);
	g_object_unref (G_OBJECT (shell));

	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	rhythmdb_entry_delete_by_type (db, entry_type);
	rhythmdb_commit (db);
	g_object_unref (db);
}


gboolean
rb_audiocd_is_volume_audiocd (GnomeVFSVolume *volume)
{
	char *device_path;
	GnomeVFSDeviceType device_type;
	gboolean result = FALSE;

	device_type = gnome_vfs_volume_get_device_type (volume);
	device_path = gnome_vfs_volume_get_device_path (volume);

	if (device_path == NULL)
		return FALSE;

	/* for sometimes device_type is GNOME_VFS_DEVICE_TYPE_CDROM */
	if (device_type == GNOME_VFS_DEVICE_TYPE_AUDIO_CD || device_type == GNOME_VFS_DEVICE_TYPE_CDROM) {
		GError *error = NULL;
		MediaType media_type;

		media_type = totem_cd_detect_type (device_path, &error);
		if (error != NULL) {
			rb_debug ("error while detecting cd: %s", error->message);
			g_error_free (error);
			return FALSE;
		}
		rb_debug ("detecting new cd - totem cd media type=%d", media_type);
		return (media_type == MEDIA_TYPE_CDDA);
	}

	return result;
}


static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/AudioCdSourcePopup");
	return TRUE;
}

