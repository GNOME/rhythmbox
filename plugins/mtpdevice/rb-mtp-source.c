/*
 *  arch-tag: Implementation of mtp source object
 *
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

#include "rhythmdb.h"
#include "eel-gconf-extensions.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-plugin.h"
#include "rb-removable-media-manager.h"
#include "rb-static-playlist-source.h"
#include "rb-util.h"
#include "rb-refstring.h"
#include "rhythmdb.h"
#include "rb-encoder.h"
#include "rb-dialog.h"

#include "rb-mtp-source.h"

#define CONF_STATE_PANED_POSITION CONF_PREFIX "/state/mtp/paned_position"
#define CONF_STATE_SHOW_BROWSER   CONF_PREFIX "/state/mtp/show_browser"

#ifndef HAVE_LIBMTP_030
/*
 * Helpful macros to determine filetype properties
 */
#define LIBMTP_FILETYPE_IS_AUDIO(a)\
(a == LIBMTP_FILETYPE_WAV ||\
 a == LIBMTP_FILETYPE_MP3 ||\
 a == LIBMTP_FILETYPE_MP2 ||\
 a == LIBMTP_FILETYPE_WMA ||\
 a == LIBMTP_FILETYPE_OGG ||\
 a == LIBMTP_FILETYPE_FLAC ||\
 a == LIBMTP_FILETYPE_AAC ||\
 a == LIBMTP_FILETYPE_M4A ||\
 a == LIBMTP_FILETYPE_UNDEF_AUDIO)

#define LIBMTP_FILETYPE_IS_VIDEO(a)\
(a == LIBMTP_FILETYPE_WMV ||\
 a == LIBMTP_FILETYPE_AVI ||\
 a == LIBMTP_FILETYPE_MPEG ||\
 a == LIBMTP_FILETYPE_UNDEF_VIDEO)

#define LIBMTP_FILETYPE_IS_AUDIOVIDEO(a)\
(a == LIBMTP_FILETYPE_MP4 ||\
 a == LIBMTP_FILETYPE_ASF ||\
 a == LIBMTP_FILETYPE_QT)

#define LIBMTP_FILETYPE_IS_TRACK(a)\
(LIBMTP_FILETYPE_IS_AUDIO(a) ||\
 LIBMTP_FILETYPE_IS_VIDEO(a) ||\
 LIBMTP_FILETYPE_IS_AUDIOVIDEO(a))

#endif


static GObject *rb_mtp_source_constructor (GType type,
					   guint n_construct_properties,
					   GObjectConstructParam *construct_properties);
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

static void rb_mtp_source_load_tracks (RBMtpSource*);
static gboolean rb_mtp_source_transfer_track_to_disk (LIBMTP_mtpdevice_t *device,
						      LIBMTP_track_t *track,
						      const char *uri);
static char* rb_mtp_source_get_playback_uri (RhythmDBEntry *entry,
					     gpointer data);

static void impl_delete (RBSource *asource);
static gboolean impl_show_popup (RBSource *source);
static GList* impl_get_ui_actions (RBSource *source);
static GList* impl_copy (RBSource *source);

static GList * impl_get_mime_types (RBRemovableMediaSource *source);
static gboolean impl_track_added (RBRemovableMediaSource *source,
				  RhythmDBEntry *entry,
				  const char *dest,
				  guint64 filesize,
				  const char *mimetype);
static char* impl_build_dest_uri (RBRemovableMediaSource *source,
				  RhythmDBEntry *entry,
				  const char *mimetype,
				  const char *extension);

static RhythmDB * get_db_for_source (RBMtpSource *source);
static void artwork_notify_cb (RhythmDB *db,
			       RhythmDBEntry *entry,
			       const char *property_name,
			       const GValue *metadata,
			       RBMtpSource *source);

static void add_track_to_album (RBMtpSource *source, const char *album_name, LIBMTP_track_t *track);

typedef struct
{
	LIBMTP_mtpdevice_t *device;
	GHashTable *entry_map;
	GHashTable *album_map;
	GHashTable *artwork_request_map;
	char *udi;
	uint16_t supported_types[LIBMTP_FILETYPE_UNKNOWN+1];
	GList *mediatypes;
	gboolean album_art_supported;

	guint load_songs_idle_id;
} RBMtpSourcePrivate;

RB_PLUGIN_DEFINE_TYPE(RBMtpSource,
		       rb_mtp_source,
		       RB_TYPE_REMOVABLE_MEDIA_SOURCE)

#define MTP_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_MTP_SOURCE, RBMtpSourcePrivate))

enum
{
	PROP_0,
	PROP_LIBMTP_DEVICE,
	PROP_UDI,
};

static void
report_libmtp_errors (LIBMTP_mtpdevice_t *device, gboolean use_dialog)
{
	LIBMTP_error_t *stack;

	for (stack = LIBMTP_Get_Errorstack (device); stack != NULL; stack = stack->next) {
		if (use_dialog) {
			rb_error_dialog (NULL, _("Media player device error"), "%s", stack->error_text);

			/* only display one dialog box per error */
			use_dialog = FALSE;
		} else {
			g_warning ("libmtp error: %s", stack->error_text);
		}
	}

	LIBMTP_Clear_Errorstack (device);
}

static void
rb_mtp_source_class_init (RBMtpSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBRemovableMediaSourceClass *rms_class = RB_REMOVABLE_MEDIA_SOURCE_CLASS (klass);
	RBBrowserSourceClass *browser_source_class = RB_BROWSER_SOURCE_CLASS (klass);

	object_class->constructor = rb_mtp_source_constructor;
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
	source_class->impl_copy = impl_copy;

	browser_source_class->impl_get_paned_key = impl_get_paned_key;

	rms_class->impl_track_added = impl_track_added;
	rms_class->impl_build_dest_uri = impl_build_dest_uri;
	rms_class->impl_get_mime_types = impl_get_mime_types;
	rms_class->impl_should_paste = rb_removable_media_source_should_paste_no_duplicate;

	g_object_class_install_property (object_class,
					 PROP_LIBMTP_DEVICE,
					 g_param_spec_pointer ("libmtp-device",
							       "libmtp-device",
							       "libmtp device",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_UDI,
					 g_param_spec_string ("udi",
						 	      "udi",
							      "udi",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

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
	if (LIBMTP_Set_Friendlyname (priv->device, name) != 0) {
		report_libmtp_errors (priv->device, TRUE);
	}
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
	priv->album_map = g_hash_table_new_full (g_str_hash,
						 g_str_equal,
						 NULL,
						 (GDestroyNotify) LIBMTP_destroy_album_t);
	priv->artwork_request_map = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static GObject *
rb_mtp_source_constructor (GType type, guint n_construct_properties,
			   GObjectConstructParam *construct_properties)
{
	RBMtpSource *source;
	RBMtpSourcePrivate *priv;
	RBEntryView *tracks;
	GtkIconTheme *theme;
	GdkPixbuf *pixbuf;
	gint size;
	guint16 *types = NULL;
	guint16 num_types= 0;

	source = RB_MTP_SOURCE (G_OBJECT_CLASS (rb_mtp_source_parent_class)->
				constructor (type, n_construct_properties, construct_properties));

	tracks = rb_source_get_entry_view (RB_SOURCE (source));
	rb_entry_view_append_column (tracks, RB_ENTRY_VIEW_COL_RATING, FALSE);
	rb_entry_view_append_column (tracks, RB_ENTRY_VIEW_COL_LAST_PLAYED, FALSE);

	/* icon */
	theme = gtk_icon_theme_get_default ();
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
	pixbuf = gtk_icon_theme_load_icon (theme, "multimedia-player", size, 0, NULL);

	rb_source_set_pixbuf (RB_SOURCE (source), pixbuf);
	g_object_unref (pixbuf);

	g_signal_connect (G_OBJECT (source), "notify::name",
			  (GCallback)rb_mtp_source_name_changed_cb, NULL);
	
	/* figure out supported file types */
	priv = MTP_SOURCE_GET_PRIVATE (source);
	if (LIBMTP_Get_Supported_Filetypes(priv->device, &types, &num_types) == 0) {
		int i;
		gboolean has_mp3 = FALSE;
		for (i = 0; i < num_types; i++) {
			const char *mediatype;

			if (i <= LIBMTP_FILETYPE_UNKNOWN) {
				priv->supported_types[types[i]] = 1;
			}

			/* this has to work with the remapping done in 
			 * rb-removable-media-source.c:impl_paste.
			 */
			switch (types[i]) {
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
				rb_debug ("unknown libmtp filetype %s supported", LIBMTP_Get_Filetype_Description (types[i]));
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
	} else {
		report_libmtp_errors (priv->device, FALSE);
	}
	
	if (priv->album_art_supported) {
		RhythmDB *db;

		db = get_db_for_source (source);
		g_signal_connect_object (db, "entry-extra-metadata-notify::rb:coverArt",
					 G_CALLBACK (artwork_notify_cb), source, 0);
		g_object_unref (db);
	}

	rb_mtp_source_load_tracks (source);

	return G_OBJECT (source);
}

static void
rb_mtp_source_set_property (GObject *object,
			    guint prop_id,
			    const GValue *value,
			    GParamSpec *pspec)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_LIBMTP_DEVICE:
		priv->device = g_value_get_pointer (value);
		break;
	case PROP_UDI:
		priv->udi = g_value_dup_string (value);
		break;
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
	case PROP_LIBMTP_DEVICE:
		g_value_set_pointer (value, priv->device);
		break;
	case PROP_UDI:
		g_value_set_string (value, priv->udi);
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

	db = get_db_for_source (source);

	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	rhythmdb_entry_delete_by_type (db, entry_type);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	rhythmdb_commit (db);
	g_object_unref (db);

	if (priv->load_songs_idle_id != 0) {
		g_source_remove (priv->load_songs_idle_id);
		priv->load_songs_idle_id = 0;
	}

	G_OBJECT_CLASS (rb_mtp_source_parent_class)->dispose (object);
}

static void
rb_mtp_source_finalize (GObject *object)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (object);

	g_hash_table_destroy (priv->entry_map);
	g_hash_table_destroy (priv->album_map);
	g_hash_table_destroy (priv->artwork_request_map);

	g_free (priv->udi);

	LIBMTP_Release_Device (priv->device);

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

RBBrowserSource *
rb_mtp_source_new (RBShell *shell,
		   LIBMTP_mtpdevice_t *device,
		   const char *udi)
{
	RBMtpSource *source = NULL;
	RhythmDBEntryType entry_type;
	RhythmDB *db = NULL;
	char *name = NULL;

	g_object_get (shell, "db", &db, NULL);
	name = g_strdup_printf ("MTP-%s", LIBMTP_Get_Serialnumber (device));

	entry_type = rhythmdb_entry_register_type (db, name);
	entry_type->save_to_disk = FALSE;
	entry_type->category = RHYTHMDB_ENTRY_NORMAL;
	entry_type->get_playback_uri = (RhythmDBEntryStringFunc)rb_mtp_source_get_playback_uri;

	g_free (name);
	g_object_unref (db);

	source = RB_MTP_SOURCE (g_object_new (RB_TYPE_MTP_SOURCE,
					      "entry-type", entry_type,
					      "shell", shell,
					      "visibility", TRUE,
					      "volume", NULL,
					      "source-group", RB_SOURCE_GROUP_DEVICES,
					      "libmtp-device", device,
					      "udi", udi,
					      NULL));

	entry_type->get_playback_uri_data = source;

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_BROWSER_SOURCE (source);
}

static void
entry_set_string_prop (RhythmDB *db,
		       RhythmDBEntry *entry,
		       RhythmDBPropType propid,
		       const char *str)
{
	GValue value = {0,};

	if (!str)
		str = _("Unknown");

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_static_string (&value, str);
	rhythmdb_entry_set (RHYTHMDB (db), entry, propid, &value);
	g_value_unset (&value);
}

static void
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
		return;
	}

	/* Set URI */
	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	name = g_strdup_printf ("x-rb-mtp://%i/%s", track->item_id, track->filename);
	entry = rhythmdb_entry_new (RHYTHMDB (db), entry_type, name);
	g_free (name);
        g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	if (entry == NULL) {
		rb_debug ("cannot create entry %i", track->item_id);
		g_object_unref (G_OBJECT (db));
		return;
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
}

static gboolean
load_mtp_db_idle_cb (RBMtpSource* source)
{
	RhythmDB *db = NULL;
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	LIBMTP_track_t *tracks = NULL;
	LIBMTP_album_t *albums;
	gboolean device_forgets_albums = TRUE;

	db = get_db_for_source (source);

	g_assert (db != NULL);

	albums = LIBMTP_Get_Album_List (priv->device);
	report_libmtp_errors (priv->device, FALSE);
	if (albums != NULL) {
		LIBMTP_album_t *album;

		for (album = albums; album != NULL; album = album->next) {
			rb_debug ("album: %s, %d tracks", album->name, album->no_tracks);
			g_hash_table_insert (priv->album_map, album->name, album);
			if (album->no_tracks != 0) {
				device_forgets_albums = FALSE;
			}
		}

		if (device_forgets_albums) {
			rb_debug ("stupid mtp device detected.  will rebuild all albums.");
		}
	} else {
		rb_debug ("No albums");
		device_forgets_albums = FALSE;
	}

#ifdef HAVE_LIBMTP_030
	tracks = LIBMTP_Get_Tracklisting_With_Callback (priv->device, NULL, NULL);
#else
	tracks = LIBMTP_Get_Tracklisting (priv->device);
#endif
	report_libmtp_errors (priv->device, FALSE);
	if (tracks != NULL) {
		LIBMTP_track_t *track;
		for (track = tracks; track != NULL; track = track->next) {
			add_mtp_track_to_db (source, db, track);

			if (device_forgets_albums && track->album != NULL) {
				add_track_to_album (source, track->album, track);
			}
		}
	} else {
		rb_debug ("No tracks");
	}

	/* for stupid devices, remove any albums left with no tracks */
	if (device_forgets_albums) {
		GHashTableIter iter;
		gpointer value;
		LIBMTP_album_t *album;

		g_hash_table_iter_init (&iter, priv->album_map);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			int ret;

			album = value;
			if (album->no_tracks == 0) {
				rb_debug ("pruning empty album \"%s\"", album->name); 
				ret = LIBMTP_Delete_Object (priv->device, album->album_id);
				if (ret != 0) {
					report_libmtp_errors (priv->device, FALSE);
				}
				g_hash_table_iter_remove (&iter);
			}
		}
	}


	g_object_unref (G_OBJECT (db));
	return FALSE;
}

static void
rb_mtp_source_load_tracks (RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	char *name = NULL;

	name = LIBMTP_Get_Friendlyname (priv->device);
	/* ignore some particular broken device names */
	if (name == NULL || strcmp (name, "?????") == 0) {
		g_free (name);
		name = LIBMTP_Get_Modelname (priv->device);
	}
	if (name == NULL) {
		name = g_strdup (_("Digital Audio Player"));
	}

	g_object_set (RB_SOURCE (source),
		      "name", name,
		      NULL);

	priv->load_songs_idle_id = g_idle_add ((GSourceFunc)load_mtp_db_idle_cb, source);
	g_free (name);
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
add_track_to_album (RBMtpSource *source, const char *album_name, LIBMTP_track_t *track)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	LIBMTP_album_t *album;

	album = g_hash_table_lookup (priv->album_map, album_name);
	if (album != NULL) {
		/* add track to album */

		album->tracks = realloc (album->tracks, sizeof(uint32_t) * (album->no_tracks+1));
		album->tracks[album->no_tracks] = track->item_id;
		album->no_tracks++;
		rb_debug ("adding track ID %d to album ID %d; now %d tracks",
			  track->item_id,
			  album->album_id,
			  album->no_tracks);

		if (LIBMTP_Update_Album (priv->device, album) != 0) {
			rb_debug ("LIBMTP_Update_Album failed..");
			report_libmtp_errors (priv->device, FALSE);
		}
	} else {
		/* add new album */
		album = LIBMTP_new_album_t ();
		album->name = strdup (album_name);
		album->no_tracks = 1;
		album->tracks = malloc (sizeof(uint32_t));
		album->tracks[0] = track->item_id;
		album->storage_id = track->storage_id;

		/* fill in artist and genre? */

		rb_debug ("creating new album (%s) for track ID %d", album->name, track->item_id);

		if (LIBMTP_Create_New_Album (priv->device, album) != 0) {
			LIBMTP_destroy_album_t (album);
			rb_debug ("LIBMTP_Create_New_Album failed..");
			report_libmtp_errors (priv->device, FALSE);
		} else {
			g_hash_table_insert (priv->album_map, album->name, album);
		}
	}
}

static void
remove_track_from_album (RBMtpSource *source, const char *album_name, LIBMTP_track_t *track)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	LIBMTP_album_t *album;
	int i;

	album = g_hash_table_lookup (priv->album_map, album_name);
	if (album == NULL) {
		rb_debug ("Couldn't find an album for %s", album_name);
		return;
	}

	for (i = 0; i < album->no_tracks; i++) {
		if (album->tracks[i] == track->item_id) {
			break;
		}
	}

	if (i == album->no_tracks) {
		rb_debug ("Couldn't find track %d in album %d", track->item_id, album->album_id);
		return;
	}

	memmove (album->tracks + i, album->tracks + i + 1, album->no_tracks - (i+1));
	album->no_tracks--;

	if (album->no_tracks == 0) {
		rb_debug ("deleting empty album %d", album->album_id);
		if (LIBMTP_Delete_Object (priv->device, album->album_id) != 0) {
			report_libmtp_errors (priv->device, FALSE);
		}
		g_hash_table_remove (priv->album_map, album_name);
	} else {
		rb_debug ("updating album %d: %d tracks remaining", album->album_id, album->no_tracks);
		if (LIBMTP_Update_Album (priv->device, album) != 0) {
			report_libmtp_errors (priv->device, FALSE);
		}
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
	int ret;

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

		ret = LIBMTP_Delete_Object (priv->device, track->item_id);
		if (ret != 0) {
			rb_debug ("Delete track %d failed", track->item_id);
			report_libmtp_errors (priv->device, TRUE);
			continue;
		}

		album_name = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
		if (strcmp (album_name, _("Unknown")) != 0) {
			remove_track_from_album (RB_MTP_SOURCE (source), album_name, track);
		}

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

static gboolean
rb_mtp_source_transfer_track_to_disk (LIBMTP_mtpdevice_t *device,
				      LIBMTP_track_t *track,
				      const char *uri)
{
	int ret = -1;
	char *path;

	if (device == NULL || track == NULL || strlen (uri) == 0) {
		rb_debug ("device (%p), track (%p), or URI (%s) not supplied", device, track, uri);
		return FALSE;
	}

	if (rb_check_dir_has_space_uri (uri, track->filesize) == FALSE) {
		rb_debug ("not enough space to transfer track %d to %s", track->item_id, uri);
		return FALSE;
	}

	path = g_filename_from_uri (uri, NULL, NULL);
	if (path != NULL) {
		ret = LIBMTP_Get_Track_To_File (device, track->item_id, path, NULL, NULL);
		rb_debug ("LIBMTP_Get_Track_To_File(%d, %s) returned %d", track->item_id, path, ret);
		if (ret != 0) {
			report_libmtp_errors (device, TRUE);
		}
		g_free (path);
	} else {
		g_warning ("couldn't get path from URI %s", uri);
	}

	return (ret == 0);
}

static char *
rb_mtp_source_get_playback_uri (RhythmDBEntry *entry, gpointer data)
{
	RBMtpSourcePrivate *priv;
	LIBMTP_track_t *track;
	char *path;
	char *uri = NULL;
	GError *error = NULL;

	priv = MTP_SOURCE_GET_PRIVATE (data);

	track = g_hash_table_lookup (priv->entry_map, entry);
	path = g_strdup_printf ("%s/%s-%s",
				g_get_tmp_dir (),
				rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ARTIST),
				rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE));
	uri = g_filename_to_uri (path, NULL, &error);
	g_free (path);
	if (error != NULL) {
		g_warning ("unable to convert path %s to filename: %s", path, error->message);
		g_error_free (error);
		g_free (path);
		return NULL;
	}

	if (rb_mtp_source_transfer_track_to_disk (priv->device, track, uri) == TRUE) {
		rb_debug ("playback URI for %s: %s",
			  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION),
			  uri);
		return uri;
	} else {
		g_free (uri);
		return NULL;
	}
}

static GList *
impl_copy (RBSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (RB_MTP_SOURCE (source));
	RhythmDB *db;
	GList *selected_entries;
	GList *iter;
	GList *copy_entries;
	int ret = -1;
	LIBMTP_track_t *track = NULL;

	db = get_db_for_source (RB_MTP_SOURCE (source));

	copy_entries = NULL;
	selected_entries = rb_entry_view_get_selected_entries (rb_source_get_entry_view (source));
	for (iter = selected_entries; iter != NULL; iter = g_list_next (iter)) {
		RhythmDBEntry *entry;
		char *path;
		char *uri;

		entry = (RhythmDBEntry *)iter->data;
		track = g_hash_table_lookup (priv->entry_map, entry);

		if (track == NULL)
			continue;

		path = g_strdup_printf ("%s/%s", g_get_tmp_dir (), track->filename);
		uri = g_filename_to_uri (path, NULL, NULL);
		g_free (path);
		ret = rb_mtp_source_transfer_track_to_disk (priv->device, track, uri);

		if (ret == 0) {
			entry_set_string_prop (RHYTHMDB (db), entry, RHYTHMDB_PROP_LOCATION, uri);
			copy_entries = g_list_prepend (copy_entries, entry);
		}
		g_free (uri);
	}

	g_list_free (selected_entries);
	g_object_unref (G_OBJECT (db));

	return copy_entries;
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

static LIBMTP_track_t *
transfer_track (RBMtpSource *source,
		LIBMTP_mtpdevice_t *device,
		RhythmDBEntry *entry,
		const char *filename,
		guint64 filesize,
		const char *mimetype)
{
	LIBMTP_track_t *trackmeta = LIBMTP_new_track_t ();
	GDate d;
	int ret;

	trackmeta->title = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE);
	trackmeta->album = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ALBUM);
	trackmeta->artist = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ARTIST);
	trackmeta->genre = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_GENRE);
	trackmeta->filename = g_path_get_basename (filename);

	if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE) > 0) { /* Entries without a date returns 0, g_date_set_julian don't accept that */
		g_date_set_julian (&d, rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE));
		trackmeta->date	= gdate_to_char (&d);
	}
	trackmeta->tracknumber = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER);
	trackmeta->duration = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION) * 1000;
	trackmeta->rating = rhythmdb_entry_get_double (entry, RHYTHMDB_PROP_RATING) * 20;
	trackmeta->usecount = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_PLAY_COUNT);
	trackmeta->filesize = filesize;
	if (mimetype == NULL) {
		mimetype = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE);
	}
	trackmeta->filetype = mimetype_to_filetype (source, mimetype);
	rb_debug ("using libmtp filetype %d (%s) for source media type %s",
		  trackmeta->filetype,
		  LIBMTP_Get_Filetype_Description (trackmeta->filetype),
		  mimetype);

#ifdef HAVE_LIBMTP_030
	ret = LIBMTP_Send_Track_From_File (device, filename, trackmeta, NULL, NULL);
#else
	ret = LIBMTP_Send_Track_From_File (device, filename, trackmeta, NULL, NULL, 0);
#endif
	rb_debug ("LIBMTP_Send_Track_From_File (%s) returned %d", filename, ret);
	if (ret != 0) {
		report_libmtp_errors (device, TRUE);
		LIBMTP_destroy_track_t (trackmeta);
		return NULL;
	}

	if (strcmp (trackmeta->album, _("Unknown")) != 0) {
		add_track_to_album (source, trackmeta->album, trackmeta);
	}

	return trackmeta;
}

static GList *
impl_get_mime_types (RBRemovableMediaSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	return rb_string_list_copy (priv->mediatypes);
}

static gboolean
impl_track_added (RBRemovableMediaSource *isource,
		  RhythmDBEntry *entry,
		  const char *dest,
		  guint64 filesize,
		  const char *mimetype)
{
	RBMtpSource *source = RB_MTP_SOURCE (isource);
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	GFile *file;
	char *path;
	LIBMTP_track_t *track = NULL;

	file = g_file_new_for_uri (dest);
	path = g_file_get_path (file);
	track = transfer_track (source, priv->device, entry, path, filesize, mimetype);
	g_free (path);

	g_file_delete (file, NULL, NULL);

	if (track != NULL) {
		RhythmDB *db = get_db_for_source (source);

		if (priv->album_art_supported) {
			const char *album;

			album = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
			if (g_hash_table_lookup (priv->artwork_request_map, album) == NULL) {
				GValue *metadata;

				rb_debug ("requesting cover art image for album %s", album);
				g_hash_table_insert (priv->artwork_request_map, (gpointer) album, GINT_TO_POINTER (1));
				metadata = rhythmdb_entry_request_extra_metadata (db, entry, "rb:coverArt");
				if (metadata) {
					artwork_notify_cb (db, entry, "rb:coverArt", metadata, source);
					g_value_unset (metadata);
					g_free (metadata);
				}
			}
		}

		add_mtp_track_to_db (source, db, track);
		g_object_unref (db);
	}

	return FALSE;
}

static char *
impl_build_dest_uri (RBRemovableMediaSource *source,
		     RhythmDBEntry *entry,
		     const char *mimetype,
		     const char *extension)
{
	char* file = g_strdup_printf ("%s/%s-%s.%s", g_get_tmp_dir (),
				      rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ARTIST),
				      rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE),
				      extension);
	char* uri = g_filename_to_uri (file, NULL, NULL);
	g_free (file);
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
	LIBMTP_album_t *album;
	LIBMTP_filesampledata_t *albumart;
	GError *error = NULL;
	const char *album_name;
	char *image_data;
	gsize image_size;
	int ret;

	album_name = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);

	/* check if we're looking for art for this entry, and if we actually got some */
	if (g_hash_table_remove (priv->artwork_request_map, album_name) == FALSE)
		return;

	if (G_VALUE_HOLDS (metadata, GDK_TYPE_PIXBUF) == FALSE)
		return;

	pixbuf = GDK_PIXBUF (g_value_get_object (metadata));

	/* we should already have created an album object */
	album = g_hash_table_lookup (priv->album_map, album_name);
	if (album == NULL) {
		rb_debug ("couldn't find an album for %s", album_name);
		return;
	}

	/* probably should scale the image down, since some devices have a size limit and they all have
	 * tiny displays anyway.
	 */

	if (gdk_pixbuf_save_to_buffer (pixbuf, &image_data, &image_size, "jpeg", &error, NULL) == FALSE) {
		rb_debug ("unable to convert album art image to a JPEG buffer: %s", error->message);
		g_error_free (error);
		return;
	}

	albumart = LIBMTP_new_filesampledata_t ();
	albumart->filetype = LIBMTP_FILETYPE_JPEG;
	albumart->data = image_data;
	albumart->size = image_size;

	ret = LIBMTP_Send_Representative_Sample (priv->device, album->album_id, albumart);
	if (ret != 0) {
		report_libmtp_errors (priv->device, TRUE);
	} else {
		rb_debug ("successfully set album art for %s (%" G_GSIZE_FORMAT " bytes)", album_name, image_size);
	}

	/* libmtp will try to free this if we don't clear the pointer */
	albumart->data = NULL;
	LIBMTP_destroy_filesampledata_t (albumart);
	g_free (image_data);
}

