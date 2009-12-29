/*
 *  Copyright (C) 2006 Peter Grundström  <pete@openfestis.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
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
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gst/gst.h>

#include "rhythmdb.h"
#include "eel-gconf-extensions.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-plugin.h"
#include "rb-builder-helpers.h"
#include "rb-removable-media-manager.h"
#include "rb-static-playlist-source.h"
#include "rb-util.h"
#include "rb-refstring.h"
#include "rhythmdb.h"
#include "rb-dialog.h"
#include "rb-shell-player.h"
#include "rb-player.h"
#include "rb-encoder.h"

#include "rb-mtp-source.h"
#include "rb-mtp-thread.h"

#define CONF_STATE_PANED_POSITION CONF_PREFIX "/state/mtp/paned_position"
#define CONF_STATE_SHOW_BROWSER   CONF_PREFIX "/state/mtp/show_browser"

static void rb_mtp_source_constructed (GObject *object);
static void rb_mtp_source_dispose (GObject *object);
static void rb_mtp_source_finalize (GObject *object);

static void rb_mtp_source_set_property (GObject *object,
			                guint prop_id,
			                const GValue *value,
			                GParamSpec *pspec);
static void rb_mtp_source_get_property (GObject *object,
			                guint prop_id,
			                GValue *value,
			                GParamSpec *pspec);
static char *impl_get_browser_key (RBSource *source);
static char *impl_get_paned_key (RBBrowserSource *source);

static void impl_delete (RBSource *asource);
static gboolean impl_show_popup (RBSource *source);
static GList* impl_get_ui_actions (RBSource *source);

static GList * impl_get_mime_types (RBRemovableMediaSource *source);
static gboolean impl_track_added (RBRemovableMediaSource *source,
				  RhythmDBEntry *entry,
				  const char *dest,
				  guint64 filesize,
				  const char *mimetype);
static gboolean impl_track_add_error (RBRemovableMediaSource *source,
				      RhythmDBEntry *entry,
				      const char *dest,
				      GError *error);
static char* impl_build_dest_uri (RBRemovableMediaSource *source,
				  RhythmDBEntry *entry,
				  const char *mimetype,
				  const char *extension);

static void mtp_device_open_cb (LIBMTP_mtpdevice_t *device, RBMtpSource *source);
static void mtp_tracklist_cb (LIBMTP_track_t *tracks, RBMtpSource *source);
static RhythmDB * get_db_for_source (RBMtpSource *source);
static void artwork_notify_cb (RhythmDB *db,
			       RhythmDBEntry *entry,
			       const char *property_name,
			       const GValue *metadata,
			       RBMtpSource *source);

static guint64		impl_get_capacity	(RBMediaPlayerSource *source);
static guint64		impl_get_free_space	(RBMediaPlayerSource *source);
static void		impl_show_properties	(RBMediaPlayerSource *source, GtkWidget *info_box, GtkWidget *notebook);

static void prepare_player_source_cb (RBPlayer *player,
				      const char *stream_uri,
				      GstElement *src,
				      RBMtpSource *source);
static void prepare_encoder_source_cb (RBEncoderFactory *factory,
				       const char *stream_uri,
				       GObject *src,
				       RBMtpSource *source);
static void prepare_encoder_sink_cb (RBEncoderFactory *factory,
				     const char *stream_uri,
				     GObject *sink,
				     RBMtpSource *source);

typedef struct
{
	RBMtpThread *device_thread;
	LIBMTP_raw_device_t raw_device;
	GHashTable *entry_map;
	GHashTable *artwork_request_map;
	GHashTable *track_transfer_map;
#if !defined(HAVE_GUDEV)
	char *udi;
#endif
	uint16_t supported_types[LIBMTP_FILETYPE_UNKNOWN+1];
	GList *mediatypes;
	gboolean album_art_supported;

	/* device information */
	char *manufacturer;
	char *serial;
	char *device_version;
	char *model_name;
	guint64 capacity;
	guint64 free_space;		/* updated by callbacks */

} RBMtpSourcePrivate;

RB_PLUGIN_DEFINE_TYPE(RBMtpSource,
		       rb_mtp_source,
		       RB_TYPE_MEDIA_PLAYER_SOURCE)

#define MTP_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_MTP_SOURCE, RBMtpSourcePrivate))

enum
{
	PROP_0,
	PROP_RAW_DEVICE,
	PROP_UDI,
	PROP_DEVICE_SERIAL
};

static void
rb_mtp_source_class_init (RBMtpSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBRemovableMediaSourceClass *rms_class = RB_REMOVABLE_MEDIA_SOURCE_CLASS (klass);
	RBBrowserSourceClass *browser_source_class = RB_BROWSER_SOURCE_CLASS (klass);
	RBMediaPlayerSourceClass *mps_class = RB_MEDIA_PLAYER_SOURCE_CLASS (klass);

	object_class->constructed = rb_mtp_source_constructed;
	object_class->dispose = rb_mtp_source_dispose;
	object_class->finalize = rb_mtp_source_finalize;
	object_class->set_property = rb_mtp_source_set_property;
	object_class->get_property = rb_mtp_source_get_property;

	source_class->impl_can_browse = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_get_browser_key = impl_get_browser_key;

	source_class->impl_can_rename = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_paste = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;

	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_get_ui_actions = impl_get_ui_actions;
	source_class->impl_delete = impl_delete;

	browser_source_class->impl_get_paned_key = impl_get_paned_key;

	rms_class->impl_track_added = impl_track_added;
	rms_class->impl_track_add_error = impl_track_add_error;
	rms_class->impl_build_dest_uri = impl_build_dest_uri;
	rms_class->impl_get_mime_types = impl_get_mime_types;
	rms_class->impl_should_paste = rb_removable_media_source_should_paste_no_duplicate;

	mps_class->impl_get_capacity = impl_get_capacity;
	mps_class->impl_get_free_space = impl_get_free_space;
	mps_class->impl_show_properties = impl_show_properties;

	g_object_class_install_property (object_class,
					 PROP_RAW_DEVICE,
					 g_param_spec_pointer ("raw-device",
							       "raw-device",
							       "libmtp raw device",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
#if !defined(HAVE_GUDEV)
	g_object_class_install_property (object_class,
					 PROP_UDI,
					 g_param_spec_string ("udi",
						 	      "udi",
							      "udi",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
#endif
	g_object_class_override_property (object_class, PROP_DEVICE_SERIAL, "serial");

	g_type_class_add_private (klass, sizeof (RBMtpSourcePrivate));
}

static void
rb_mtp_source_name_changed_cb (RBMtpSource *source,
			       GParamSpec *spec,
			       gpointer data)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	char *name = NULL;

	g_object_get (source, "name", &name, NULL);
	rb_mtp_thread_set_device_name (priv->device_thread, name);
	g_free (name);
}

static void
rb_mtp_source_init (RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);

	priv->entry_map = g_hash_table_new_full (g_direct_hash,
						 g_direct_equal,
						 NULL,
						 (GDestroyNotify) LIBMTP_destroy_track_t);
	priv->artwork_request_map = g_hash_table_new (g_direct_hash, g_direct_equal);

	priv->track_transfer_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
rb_mtp_source_constructed (GObject *object)
{
	RBMtpSource *source;
	RBMtpSourcePrivate *priv;
	RBEntryView *tracks;
	RBShell *shell;
	RBShellPlayer *shell_player;
	GObject *player_backend;
	GtkIconTheme *theme;
	GdkPixbuf *pixbuf;
	gint size;

	RB_CHAIN_GOBJECT_METHOD (rb_mtp_source_parent_class, constructed, object);
	source = RB_MTP_SOURCE (object);
	priv = MTP_SOURCE_GET_PRIVATE (source);

	/* start the device thread */
	priv->device_thread = rb_mtp_thread_new ();
	rb_mtp_thread_open_device (priv->device_thread,
				   &priv->raw_device,
				   (RBMtpOpenCallback)mtp_device_open_cb,
				   g_object_ref (source),
				   g_object_unref);

	tracks = rb_source_get_entry_view (RB_SOURCE (source));
	rb_entry_view_append_column (tracks, RB_ENTRY_VIEW_COL_RATING, FALSE);
	rb_entry_view_append_column (tracks, RB_ENTRY_VIEW_COL_LAST_PLAYED, FALSE);

	/* the source element needs our cooperation */
	g_object_get (source, "shell", &shell, NULL);
	shell_player = RB_SHELL_PLAYER (rb_shell_get_player (shell));
	g_object_get (shell_player, "player", &player_backend, NULL);

	g_signal_connect_object (player_backend,
				 "prepare-source",
				 G_CALLBACK (prepare_player_source_cb),
				 source, 0);

	g_object_unref (player_backend);
	g_object_unref (shell);

	g_signal_connect_object (rb_encoder_factory_get (),
				 "prepare-source",
				 G_CALLBACK (prepare_encoder_source_cb),
				 source, 0);
	g_signal_connect_object (rb_encoder_factory_get (),
				 "prepare-sink",
				 G_CALLBACK (prepare_encoder_sink_cb),
				 source, 0);

	/* icon */
	theme = gtk_icon_theme_get_default ();
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
	pixbuf = gtk_icon_theme_load_icon (theme, "multimedia-player", size, 0, NULL);

	rb_source_set_pixbuf (RB_SOURCE (source), pixbuf);
	g_object_unref (pixbuf);

	if (priv->album_art_supported) {
		RhythmDB *db;

		db = get_db_for_source (source);
		g_signal_connect_object (db, "entry-extra-metadata-notify::rb:coverArt",
					 G_CALLBACK (artwork_notify_cb), source, 0);
		g_object_unref (db);
	}
}

static void
rb_mtp_source_set_property (GObject *object,
			    guint prop_id,
			    const GValue *value,
			    GParamSpec *pspec)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (object);
	LIBMTP_raw_device_t *raw_device;

	switch (prop_id) {
	case PROP_RAW_DEVICE:
		raw_device = g_value_get_pointer (value);
		priv->raw_device = *raw_device;
		break;
#if !defined(HAVE_GUDEV)
	case PROP_UDI:
		priv->udi = g_value_dup_string (value);
		break;
#endif
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_mtp_source_get_property (GObject *object,
			    guint prop_id,
			    GValue *value,
			    GParamSpec *pspec)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_RAW_DEVICE:
		g_value_set_pointer (value, &priv->raw_device);
		break;
#if !defined(HAVE_GUDEV)
	case PROP_UDI:
		g_value_set_string (value, priv->udi);
		break;
#endif
	case PROP_DEVICE_SERIAL:
		g_value_set_string (value, priv->serial);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_mtp_source_dispose (GObject *object)
{
	RBMtpSource *source = RB_MTP_SOURCE (object);
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	RhythmDBEntryType entry_type;
	RhythmDB *db;

	if (priv->device_thread != NULL) {
		g_object_unref (priv->device_thread);
		priv->device_thread = NULL;
	}

	db = get_db_for_source (source);

	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	rhythmdb_entry_delete_by_type (db, entry_type);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	rhythmdb_commit (db);
	g_object_unref (db);

	G_OBJECT_CLASS (rb_mtp_source_parent_class)->dispose (object);
}

static void
rb_mtp_source_finalize (GObject *object)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (object);

	g_hash_table_destroy (priv->entry_map);
	g_hash_table_destroy (priv->artwork_request_map);
	g_hash_table_destroy (priv->track_transfer_map);		/* probably need to destroy the tracks too.. */

#if !defined(HAVE_GUDEV)
	g_free (priv->udi);
#endif
	g_free (priv->manufacturer);
	g_free (priv->device_version);
	g_free (priv->model_name);
	g_free (priv->serial);

	G_OBJECT_CLASS (rb_mtp_source_parent_class)->finalize (object);
}

static char *
impl_get_browser_key (RBSource *source)
{
	return g_strdup (CONF_STATE_SHOW_BROWSER);
}

static char *
impl_get_paned_key (RBBrowserSource *source)
{
	return g_strdup (CONF_STATE_PANED_POSITION);
}

RBSource *
rb_mtp_source_new (RBShell *shell,
		   RBPlugin *plugin,
#if !defined(HAVE_GUDEV)
		   const char *udi,
#endif
		   LIBMTP_raw_device_t *device)
{
	RBMtpSource *source = NULL;
	RhythmDBEntryType entry_type;
	RhythmDB *db = NULL;
	char *name = NULL;

	g_object_get (shell, "db", &db, NULL);
	name = g_strdup_printf ("MTP-%u-%d", device->bus_location, device->devnum);

	entry_type = rhythmdb_entry_register_type (db, name);
	entry_type->save_to_disk = FALSE;
	entry_type->category = RHYTHMDB_ENTRY_NORMAL;

	g_free (name);
	g_object_unref (db);

	source = RB_MTP_SOURCE (g_object_new (RB_TYPE_MTP_SOURCE,
					      "plugin", plugin,
					      "entry-type", entry_type,
					      "shell", shell,
					      "visibility", TRUE,
					      "volume", NULL,
					      "source-group", RB_SOURCE_GROUP_DEVICES,
					      "raw-device", device,
#if !defined(HAVE_GUDEV)
					      "udi", udi,
#endif
					      NULL));

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_SOURCE (source);
}

static void
update_free_space_cb (LIBMTP_mtpdevice_t *device, RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	LIBMTP_devicestorage_t *storage;
	int ret;

	ret = LIBMTP_Get_Storage (device, LIBMTP_STORAGE_SORTBY_NOTSORTED);
	if (ret != 0) {
		rb_mtp_thread_report_errors (priv->device_thread, FALSE);
	}

	/* probably need a lock for this.. */
	priv->free_space = 0;
	for (storage = device->storage; storage != NULL; storage = storage->next) {
		priv->free_space += storage->FreeSpaceInBytes;
	}
}

static void
queue_free_space_update (RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	rb_mtp_thread_queue_callback (priv->device_thread,
				      (RBMtpThreadCallback) update_free_space_cb, source, NULL);
}

static void
entry_set_string_prop (RhythmDB *db,
		       RhythmDBEntry *entry,
		       RhythmDBPropType propid,
		       const char *str)
{
	GValue value = {0,};

	if (str == NULL || (g_utf8_validate (str, -1, NULL) == FALSE)) {
		str = _("Unknown");
	}

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_static_string (&value, str);
	rhythmdb_entry_set (RHYTHMDB (db), entry, propid, &value);
	g_value_unset (&value);
}

static RhythmDBEntry *
add_mtp_track_to_db (RBMtpSource *source,
		     RhythmDB *db,
		     LIBMTP_track_t *track)
{
	RhythmDBEntry *entry = NULL;
	RhythmDBEntryType entry_type;
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	char *name = NULL;

	/* ignore everything except audio (allow audio/video types too, since they're probably pretty common) */
	if (!(LIBMTP_FILETYPE_IS_AUDIO (track->filetype) || LIBMTP_FILETYPE_IS_AUDIOVIDEO (track->filetype))) {
		rb_debug ("ignoring non-audio item %d (filetype %s)",
			  track->item_id,
			  LIBMTP_Get_Filetype_Description (track->filetype));
		return NULL;
	}

	/* Set URI */
	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	name = g_strdup_printf ("xrbmtp://%i/%s", track->item_id, track->filename);
	entry = rhythmdb_entry_new (RHYTHMDB (db), entry_type, name);
	g_free (name);
        g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	if (entry == NULL) {
		rb_debug ("cannot create entry %i", track->item_id);
		g_object_unref (G_OBJECT (db));
		return NULL;
	}

	/* Set track number */
	if (track->tracknumber != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, track->tracknumber);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
				    RHYTHMDB_PROP_TRACK_NUMBER,
				    &value);
		g_value_unset (&value);
	}

	/* Set length */
	if (track->duration != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, track->duration/1000);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
				    RHYTHMDB_PROP_DURATION,
				    &value);
		g_value_unset (&value);
	}

	/* Set file size */
	if (track->filesize != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_UINT64);
		g_value_set_uint64 (&value, track->filesize);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
				    RHYTHMDB_PROP_FILE_SIZE,
				    &value);
		g_value_unset (&value);
	}

	/* Set playcount */
	if (track->usecount != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, track->usecount);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_PLAY_COUNT,
					       &value);
		g_value_unset (&value);
	}
	/* Set rating */
	if (track->rating != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_DOUBLE);
		g_value_set_double (&value, track->rating/20);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_RATING,
					       &value);
		g_value_unset (&value);
	}

	/* Set title */
	entry_set_string_prop (RHYTHMDB (db), entry, RHYTHMDB_PROP_TITLE, track->title);

	/* Set album, artist and genre from MTP */
	entry_set_string_prop (RHYTHMDB (db), entry, RHYTHMDB_PROP_ARTIST, track->artist);
	entry_set_string_prop (RHYTHMDB (db), entry, RHYTHMDB_PROP_ALBUM, track->album);
	entry_set_string_prop (RHYTHMDB (db), entry, RHYTHMDB_PROP_GENRE, track->genre);

	g_hash_table_insert (priv->entry_map, entry, track);
	rhythmdb_commit (RHYTHMDB (db));

	return entry;
}

typedef struct {
	RBMtpSource *source;
	char *name;
	guint16 *types;
	guint16 num_types;
} DeviceOpenedData;

static gboolean
device_opened_idle (DeviceOpenedData *data)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (data->source);
	int i;
	gboolean has_mp3 = FALSE;

	if (data->name != NULL) {
		g_object_set (data->source, "name", data->name, NULL);
	}

	/* when the source name changes after this, try to update the device name */
	g_signal_connect (G_OBJECT (data->source), "notify::name",
			  (GCallback)rb_mtp_source_name_changed_cb, NULL);

	for (i = 0; i < data->num_types; i++) {
		const char *mediatype;

		if (i <= LIBMTP_FILETYPE_UNKNOWN) {
			priv->supported_types[data->types[i]] = 1;
		}

		/* this has to work with the remapping done in
		 * rb-removable-media-source.c:impl_paste.
		 */
		switch (data->types[i]) {
		case LIBMTP_FILETYPE_WAV:
			mediatype = "audio/x-wav";
			break;
		case LIBMTP_FILETYPE_MP3:
			/* special handling for mp3: always put it at the front of the list
			 * if it's supported.
			 */
			has_mp3 = TRUE;
			mediatype = NULL;
			break;
		case LIBMTP_FILETYPE_WMA:
			mediatype = "audio/x-ms-wma";
			break;
		case LIBMTP_FILETYPE_OGG:
			mediatype = "application/ogg";
			break;
		case LIBMTP_FILETYPE_MP4:
		case LIBMTP_FILETYPE_M4A:
		case LIBMTP_FILETYPE_AAC:
			mediatype = "audio/aac";
			break;
		case LIBMTP_FILETYPE_WMV:
			mediatype = "audio/x-ms-wmv";
			break;
		case LIBMTP_FILETYPE_ASF:
			mediatype = "video/x-ms-asf";
			break;
		case LIBMTP_FILETYPE_FLAC:
			mediatype = "audio/flac";
			break;

		case LIBMTP_FILETYPE_JPEG:
			rb_debug ("JPEG (album art) supported");
			mediatype = NULL;
			priv->album_art_supported = TRUE;
			break;

		default:
			rb_debug ("unknown libmtp filetype %s supported", LIBMTP_Get_Filetype_Description (data->types[i]));
			mediatype = NULL;
			break;
		}

		if (mediatype != NULL) {
			rb_debug ("media type %s supported", mediatype);
			priv->mediatypes = g_list_prepend (priv->mediatypes,
							   g_strdup (mediatype));
		}
	}

	if (has_mp3) {
		rb_debug ("audio/mpeg supported");
		priv->mediatypes = g_list_prepend (priv->mediatypes, g_strdup ("audio/mpeg"));
	}

	g_object_unref (data->source);
	free (data->types);
	g_free (data->name);
	g_free (data);

	return FALSE;
}

static gboolean
device_open_failed_idle (RBMtpSource *source)
{
	/* libmtp doesn't give us a useful error message in this case, so
	 * all we can offer is this generic message.
	 */
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	rb_error_dialog (NULL,
			 _("Media player device error"),
			 /* Translators: first %s is the device manufacturer,
			  * second is the product name.
			  */
			 _("Unable to open the %s %s device"),
			 priv->raw_device.device_entry.vendor,
			 priv->raw_device.device_entry.product);
	rb_source_delete_thyself (RB_SOURCE (source));
	g_object_unref (source);
	return FALSE;
}

/* this callback runs on the device handling thread, so it can call libmtp directly */
static void
mtp_device_open_cb (LIBMTP_mtpdevice_t *device, RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	DeviceOpenedData *data;

	if (device != NULL) {
		/* can't delete the source on this thread, so move it to the main thread */
		g_idle_add ((GSourceFunc) device_open_failed_idle, g_object_ref (source));
		return;
	}

	/* set the source name to match the device, ignoring some
	 * particular broken device names.
	 */
	data = g_new0 (DeviceOpenedData, 1);
	data->source = g_object_ref (source);
	data->name = LIBMTP_Get_Friendlyname (device);
	if (data->name == NULL || strcmp (data->name, "?????") == 0) {
		g_free (data->name);
		data->name = LIBMTP_Get_Modelname (device);
	}
	if (data->name == NULL) {
		data->name = g_strdup (_("Digital Audio Player"));
	}

	/* get some other device information that doesn't change */
	priv->manufacturer = LIBMTP_Get_Manufacturername (device);
	priv->device_version = LIBMTP_Get_Deviceversion (device);
	priv->model_name = LIBMTP_Get_Modelname (device);
	priv->serial = LIBMTP_Get_Serialnumber (device);

	/* calculate the device capacity */
	priv->capacity = 0;
	if (LIBMTP_Get_Storage (device, LIBMTP_STORAGE_SORTBY_NOTSORTED) == 0) {
		LIBMTP_devicestorage_t *storage;
		for (storage = device->storage;
		     storage != NULL;
		     storage = storage->next) {
			priv->capacity += storage->MaxCapacity;
		}
	}

	update_free_space_cb (device, RB_MTP_SOURCE (source));

	/* figure out the set of formats supported by the device */
	if (LIBMTP_Get_Supported_Filetypes (device, &data->types, &data->num_types) != 0) {
		rb_mtp_thread_report_errors (priv->device_thread, FALSE);
	}

	g_idle_add ((GSourceFunc) device_opened_idle, data);

	/* now get the track list */
	rb_mtp_thread_get_track_list (priv->device_thread, (RBMtpTrackListCallback) mtp_tracklist_cb, g_object_ref (source), g_object_unref);
}

static void
mtp_tracklist_cb (LIBMTP_track_t *tracks, RBMtpSource *source)
{
	RhythmDB *db = NULL;
	LIBMTP_track_t *track;

	/* add tracks to database */
	db = get_db_for_source (source);
	for (track = tracks; track != NULL; track = track->next) {
		add_mtp_track_to_db (source, db, track);
	}
	g_object_unref (db);
}

static char *
gdate_to_char (GDate* date)
{
	return g_strdup_printf ("%04i%02i%02iT0000.0",
				g_date_get_year (date),
				g_date_get_month (date),
				g_date_get_day (date));
}

static LIBMTP_filetype_t
mimetype_to_filetype (RBMtpSource *source, const char *mimetype)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);

	if (!strcmp (mimetype, "audio/mpeg") || !strcmp (mimetype, "application/x-id3")) {
		return LIBMTP_FILETYPE_MP3;
	}  else if (!strcmp (mimetype, "audio/x-wav")) {
		return  LIBMTP_FILETYPE_WAV;
	} else if (!strcmp (mimetype, "application/ogg")) {
		return LIBMTP_FILETYPE_OGG;
	} else if (!strcmp (mimetype, "audio/x-m4a") || !strcmp (mimetype, "video/quicktime")) {
		/* try a few different filetypes that might work */
		if (priv->supported_types[LIBMTP_FILETYPE_M4A])
			return LIBMTP_FILETYPE_M4A;
		else if (!priv->supported_types[LIBMTP_FILETYPE_AAC] && priv->supported_types[LIBMTP_FILETYPE_MP4])
			return LIBMTP_FILETYPE_MP4;
		else
			return LIBMTP_FILETYPE_AAC;

	} else if (!strcmp (mimetype, "audio/x-ms-wma") || !strcmp (mimetype, "audio/x-ms-asf")) {
		return LIBMTP_FILETYPE_WMA;
	} else if (!strcmp (mimetype, "video/x-ms-asf")) {
		return LIBMTP_FILETYPE_ASF;
	} else if (!strcmp (mimetype, "audio/x-flac")) {
		return LIBMTP_FILETYPE_FLAC;
	} else {
		rb_debug ("\"%s\" is not a supported mimetype", mimetype);
		return LIBMTP_FILETYPE_UNKNOWN;
	}
}

static void
impl_delete (RBSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	GList *sel;
	GList *tem;
	RBEntryView *tracks;
	RhythmDB *db;

	db = get_db_for_source (RB_MTP_SOURCE (source));

	tracks = rb_source_get_entry_view (source);
	sel = rb_entry_view_get_selected_entries (tracks);
	for (tem = sel; tem != NULL; tem = tem->next) {
		LIBMTP_track_t *track;
		RhythmDBEntry *entry;
		const char *uri;
		const char *album_name;

		entry = (RhythmDBEntry *)tem->data;
		uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		track = g_hash_table_lookup (priv->entry_map, entry);
		if (track == NULL) {
			rb_debug ("Couldn't find track on mtp-device! (%s)", uri);
			continue;
		}

		album_name = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
		if (strcmp (album_name, _("Unknown")) != 0) {
			rb_mtp_thread_remove_from_album (priv->device_thread, track, album_name);
		}
		rb_mtp_thread_delete_track (priv->device_thread, track);

		g_hash_table_remove (priv->entry_map, entry);
		rhythmdb_entry_delete (db, entry);
	}
	rhythmdb_commit (db);

	g_list_free (sel);
	g_list_free (tem);
}

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/MTPSourcePopup");
	return TRUE;
}

static GList *
impl_get_ui_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, g_strdup ("MTPSourceEject"));

	return actions;
}

static RhythmDB *
get_db_for_source (RBMtpSource *source)
{
	RBShell *shell = NULL;
	RhythmDB *db = NULL;

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	g_object_unref (shell);

	return db;
}

typedef struct {
	RBMtpSource *source;
	RhythmDBEntry *entry;
} RequestAlbumArtData;

static gboolean
request_album_art_idle (RequestAlbumArtData *data)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (data->source);
	const char *album;

	/* pretty sure we don't need any extra locking here - we only touch the artwork
	 * request map on the main thread anyway.
	 */

	album = rhythmdb_entry_get_string (data->entry, RHYTHMDB_PROP_ALBUM);
	if (g_hash_table_lookup (priv->artwork_request_map, album) == NULL) {
		GValue *metadata;
		RhythmDB *db = get_db_for_source (data->source);

		rb_debug ("requesting cover art image for album %s", album);
		g_hash_table_insert (priv->artwork_request_map, (gpointer) album, GINT_TO_POINTER (1));
		metadata = rhythmdb_entry_request_extra_metadata (db, data->entry, "rb:coverArt");
		if (metadata) {
			artwork_notify_cb (db, data->entry, "rb:coverArt", metadata, data->source);
			g_value_unset (metadata);
			g_free (metadata);
		}
		g_object_unref (db);
	}

	g_object_unref (data->source);
	rhythmdb_entry_unref (data->entry);
	g_free (data);
	return FALSE;
}

static GList *
impl_get_mime_types (RBRemovableMediaSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	return rb_string_list_copy (priv->mediatypes);
}

static gboolean
impl_track_added (RBRemovableMediaSource *source,
		  RhythmDBEntry *entry,
		  const char *dest,
		  guint64 filesize,
		  const char *mimetype)
{
	LIBMTP_track_t *track = NULL;
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	RhythmDB *db;
	RhythmDBEntry *mtp_entry;

	track = g_hash_table_lookup (priv->track_transfer_map, dest);
	if (track == NULL) {
		rb_debug ("track-added called, but can't find a track for dest URI %s", dest);
		return FALSE;
	}
	g_hash_table_remove (priv->track_transfer_map, dest);

	db = get_db_for_source (RB_MTP_SOURCE (source));
	/* entry_map takes ownership of the track here */
	mtp_entry = add_mtp_track_to_db (RB_MTP_SOURCE (source), db, track);
	g_object_unref (db);

	if (strcmp (track->album, _("Unknown")) != 0) {
		rb_mtp_thread_add_to_album (priv->device_thread, track, track->album);
	}

	if (priv->album_art_supported) {
		RequestAlbumArtData *artdata;
		artdata = g_new0 (RequestAlbumArtData, 1);
		artdata->source = g_object_ref (source);
		artdata->entry = rhythmdb_entry_ref (mtp_entry);
		g_idle_add ((GSourceFunc) request_album_art_idle, artdata);
	}
	queue_free_space_update (RB_MTP_SOURCE (source));
	return FALSE;
}

static gboolean
impl_track_add_error (RBRemovableMediaSource *source,
		      RhythmDBEntry *entry,
		      const char *dest,
		      GError *error)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	/* we don't actually do anything with the error here, we just need to clean up the transfer map */
	LIBMTP_track_t *track = g_hash_table_lookup (priv->track_transfer_map, dest);
	if (track != NULL) {
		LIBMTP_destroy_track_t (track);
		g_hash_table_remove (priv->track_transfer_map, dest);
	} else {
		rb_debug ("track-add-error called, but can't find a track for dest URI %s", dest);
	}

	return TRUE;
}

static void
prepare_encoder_sink_cb (RBEncoderFactory *factory,
			 const char *stream_uri,
			 GObject *sink,
			 RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	RhythmDBEntry *entry;
	RhythmDB *db;
	LIBMTP_track_t *track;
	char **bits;
	char *extension;
	LIBMTP_filetype_t filetype;
	gulong track_id;
	GDate d;

	/* make sure this stream is for a file on our device */
	if (g_str_has_prefix (stream_uri, "xrbmtp://") == FALSE)
		return;

	/* extract the entry ID, extension, and MTP filetype from the URI */
	bits = g_strsplit (stream_uri + strlen ("xrbmtp://"), "/", 3);
	track_id = strtoul (bits[0], NULL, 0);
	extension = g_strdup (bits[1]);
	filetype = strtoul (bits[2], NULL, 0);
	g_strfreev (bits);

	db = get_db_for_source (source);
	entry = rhythmdb_entry_lookup_by_id (db, track_id);
	g_object_unref (db);
	if (entry == NULL)
		return;

	track = LIBMTP_new_track_t ();
	track->title = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE);
	track->album = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ALBUM);
	track->artist = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ARTIST);
	track->genre = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_GENRE);

	/* build up device filename; may want to reconsider if we start creating folders */
	track->filename = g_strdup_printf ("%s - %s.%s",
					   rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST),
					   rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE),
					   extension);
	g_free (extension);

	/* ensure the filename is safe for FAT filesystems and doesn't contain slashes */
	rb_sanitize_path_for_msdos_filesystem (track->filename);
	g_strdelimit (track->filename, "/", '_');

	if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE) > 0) {
		g_date_set_julian (&d, rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE));
		track->date = gdate_to_char (&d);
	}
	track->tracknumber = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER);
	track->duration = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION) * 1000;
	track->rating = rhythmdb_entry_get_double (entry, RHYTHMDB_PROP_RATING) * 20;
	track->usecount = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_PLAY_COUNT);

	track->filetype = filetype;

	g_object_set (sink, "device-thread", priv->device_thread, "mtp-track", track, NULL);
	rhythmdb_entry_unref (entry);

	g_hash_table_insert (priv->track_transfer_map, g_strdup (stream_uri), track);
}

static char *
impl_build_dest_uri (RBRemovableMediaSource *source,
		     RhythmDBEntry *entry,
		     const char *mimetype,
		     const char *extension)
{
	gulong id;
	char *uri;
	LIBMTP_filetype_t filetype;

	if (mimetype == NULL) {
		mimetype = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE);
	}
	filetype = mimetype_to_filetype (RB_MTP_SOURCE (source), mimetype);
	rb_debug ("using libmtp filetype %d (%s) for source media type %s",
		  filetype,
		  LIBMTP_Get_Filetype_Description (filetype),
		  mimetype);

	/* the prepare-sink callback needs the entry ID to set up the
	 * upload data, and we want to use the supplied extension for
	 * the filename on the device.
	 *
	 * this is pretty ugly - it'd be much nicer to have a source-defined
	 * structure that got passed around (or was accessible from) the various
	 * hooks and methods called during the track transfer process.  probably
	 * something to address in my horribly stalled track transfer rewrite..
	 *
	 * the structure would either be created when queuing the track for transfer,
	 * or here; passed to any prepare-source or prepare-sink callbacks for the
	 * encoder; and then passed to whatever gets called when the transfer is complete.
	 */
	id = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_ENTRY_ID);
	uri = g_strdup_printf ("xrbmtp://%lu/%s/%d", id, extension, filetype);
	return uri;
}

static void
artwork_notify_cb (RhythmDB *db,
		   RhythmDBEntry *entry,
		   const char *property_name,
		   const GValue *metadata,
		   RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	GdkPixbuf *pixbuf;
	const char *album_name;

	album_name = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);

	/* check if we're looking for art for this entry, and if we actually got some */
	if (g_hash_table_remove (priv->artwork_request_map, album_name) == FALSE)
		return;

	if (G_VALUE_HOLDS (metadata, GDK_TYPE_PIXBUF) == FALSE)
		return;

	pixbuf = GDK_PIXBUF (g_value_get_object (metadata));

	rb_mtp_thread_set_album_image (priv->device_thread, album_name, pixbuf);
	queue_free_space_update (source);

	g_object_unref (pixbuf);		/* ? */
}

static guint64
impl_get_capacity	(RBMediaPlayerSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	return priv->capacity;
}

static guint64
impl_get_free_space	(RBMediaPlayerSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	/* probably need a lock for this */
	return priv->free_space;
}

static void
impl_show_properties (RBMediaPlayerSource *source, GtkWidget *info_box, GtkWidget *notebook)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	GtkBuilder *builder;
	GtkWidget *widget;
	GHashTableIter iter;
	gpointer key, value;
	int num_podcasts;
	char *device_name;
	char *builder_file;
	RBPlugin *plugin;
	char *text;

	g_object_get (source, "plugin", &plugin, NULL);
	builder_file = rb_plugin_find_file (plugin, "mtp-info.ui");
	g_object_unref (plugin);

	if (builder_file == NULL) {
		g_warning ("Couldn't find mtp-info.ui");
		return;
	}

	builder = rb_builder_load (builder_file, NULL);
	g_free (builder_file);

	if (builder == NULL) {
		rb_debug ("Couldn't load mtp-info.ui");
		return;
	}

	/* 'basic' tab stuff */

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "mtp-basic-info"));
	gtk_box_pack_start (GTK_BOX (info_box), widget, TRUE, TRUE, 0);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry-mtp-name"));
	g_object_get (source, "name", &device_name, NULL);
	gtk_entry_set_text (GTK_ENTRY (widget), device_name);
	g_free (device_name);
	g_signal_connect (widget, "focus-out-event",
			  (GCallback)rb_mtp_source_name_changed_cb, source);

	num_podcasts = 0;
	g_hash_table_iter_init (&iter, priv->entry_map);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		LIBMTP_track_t *track = value;
		if (g_strcmp0 (track->genre, "Podcast") == 0) {
			num_podcasts++;
		}
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "mtp-num-tracks"));
	text = g_strdup_printf ("%d", g_hash_table_size (priv->entry_map) - num_podcasts);
	gtk_label_set_text (GTK_LABEL (widget), text);
	g_free (text);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "mtp-num-podcasts"));
	text = g_strdup_printf ("%d", num_podcasts);
	gtk_label_set_text (GTK_LABEL (widget), text);
	g_free (text);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "mtp-num-playlists"));
	text = g_strdup_printf ("%d", 0);						/* correct, but wrong */
	gtk_label_set_text (GTK_LABEL (widget), text);
	g_free (text);

	/* 'advanced' tab stuff */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "mtp-advanced-tab"));
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), widget, gtk_label_new (_("Advanced")));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label-mtp-model-value"));
	gtk_label_set_text (GTK_LABEL (widget), priv->model_name);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label-serial-number-value"));
	gtk_label_set_text (GTK_LABEL (widget), priv->serial);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label-firmware-version-value"));
	gtk_label_set_text (GTK_LABEL (widget), priv->device_version);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label-manufacturer-value"));
	gtk_label_set_text (GTK_LABEL (widget), priv->manufacturer);

	g_object_unref (builder);
}

static void
prepare_source (RBMtpSource *source, const char *stream_uri, GObject *src)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	RhythmDBEntry *entry;
	RhythmDB *db;

	/* make sure this stream is for a file on our device */
	if (g_str_has_prefix (stream_uri, "xrbmtp://") == FALSE)
		return;

	db = get_db_for_source (source);
	entry = rhythmdb_entry_lookup_by_location (db, stream_uri);
	g_object_unref (db);
	if (entry == NULL)
		return;

	if (_rb_source_check_entry_type (RB_SOURCE (source), entry) == FALSE) {
		rhythmdb_entry_unref (entry);
		return;
	}

	rb_debug ("setting device-thread for stream %s", stream_uri);
	g_object_set (src, "device-thread", priv->device_thread, NULL);
	rhythmdb_entry_unref (entry);
}

static void
prepare_player_source_cb (RBPlayer *player,
			  const char *stream_uri,
			  GstElement *src,
			  RBMtpSource *source)
{
	prepare_source (source, stream_uri, G_OBJECT (src));
}

static void
prepare_encoder_source_cb (RBEncoderFactory *factory,
			   const char *stream_uri,
			   GObject *src,
			   RBMtpSource *source)
{
	prepare_source (source, stream_uri, src);
}

