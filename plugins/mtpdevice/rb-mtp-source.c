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
#include <glib/gstdio.h>
#include <gst/gst.h>

#if defined(HAVE_GUDEV)
#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>
#endif

#include "rhythmdb.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-removable-media-manager.h"
#include "rb-static-playlist-source.h"
#include "rb-transfer-target.h"
#include "rb-device-source.h"
#include "rb-util.h"
#include "rb-refstring.h"
#include "rhythmdb.h"
#include "rb-dialog.h"
#include "rb-shell-player.h"
#include "rb-player.h"
#include "rb-encoder.h"
#include "rb-sync-settings.h"
#include "rb-gst-media-types.h"
#include "rb-ext-db.h"
#include "rb-application.h"

#include "rb-mtp-source.h"
#include "rb-mtp-thread.h"

static void rb_mtp_source_constructed (GObject *object);
static void rb_mtp_source_dispose (GObject *object);
static void rb_mtp_source_finalize (GObject *object);
static void rb_mtp_device_source_init (RBDeviceSourceInterface *interface);
static void rb_mtp_source_transfer_target_init (RBTransferTargetInterface *interface);

static void rb_mtp_source_set_property (GObject *object,
			                guint prop_id,
			                const GValue *value,
			                GParamSpec *pspec);
static void rb_mtp_source_get_property (GObject *object,
			                guint prop_id,
			                GValue *value,
			                GParamSpec *pspec);

static void impl_delete_selected (RBSource *asource);
static RBTrackTransferBatch *impl_paste (RBSource *asource, GList *entries);
static gboolean impl_uri_is_source (RBSource *asource, const char *uri);

static void impl_track_upload (RBTransferTarget *target,
			       RhythmDBEntry *entry,
			       const char *dest,
			       guint64 filesize,
			       const char *media_type,
			       GError **error);
static gboolean impl_track_added (RBTransferTarget *target,
				  RhythmDBEntry *entry,
				  const char *dest,
				  guint64 filesize,
				  const char *media_type);
static gboolean impl_track_add_error (RBTransferTarget *target,
				      RhythmDBEntry *entry,
				      const char *dest,
				      GError *error);

static void impl_eject (RBDeviceSource *source);
static gboolean impl_can_eject (RBDeviceSource *source);

static void impl_selected (RBDisplayPage *page);
static void mtp_device_open_cb (LIBMTP_mtpdevice_t *device, RBMtpSource *source);
static void mtp_tracklist_cb (LIBMTP_track_t *tracks, RBMtpSource *source);
static RhythmDB * get_db_for_source (RBMtpSource *source);

static void		impl_get_entries	(RBMediaPlayerSource *source, const char *category, GHashTable *map);
static guint64		impl_get_capacity	(RBMediaPlayerSource *source);
static guint64		impl_get_free_space	(RBMediaPlayerSource *source);
static void		impl_delete_entries	(RBMediaPlayerSource *source,
						 GList *entries,
						 GAsyncReadyCallback callback,
						 gpointer callback_data);
static void		impl_show_properties	(RBMediaPlayerSource *source, GtkWidget *info_box, GtkWidget *notebook);

static void prepare_player_source_cb (RBPlayer *player,
				      const char *stream_uri,
				      GstElement *src,
				      RBMtpSource *source);
static void prepare_encoder_source_cb (RBEncoderFactory *factory,
				       const char *stream_uri,
				       GObject *src,
				       RBMtpSource *source);
static void art_request_cb (RBExtDBKey *key,
			    RBExtDBKey *store_key,
			    const char *filename,
			    GValue *data,
			    RBMtpSource *source);
#if defined(HAVE_GUDEV)
static GMount *find_mount_for_device (GUdevDevice *device);
#endif

typedef struct
{
	RBMtpSource *source;
	LIBMTP_track_t *track;
	char *tempfile;
	GError *error;
	GCond cond;
	GMutex lock;
} RBMtpSourceTrackUpload;

typedef struct
{
	gboolean tried_open;
	RBMtpThread *device_thread;
	LIBMTP_raw_device_t raw_device;
	GHashTable *entry_map;
#if defined(HAVE_GUDEV)
	GUdevDevice *udev_device;
	GVolume *remount_volume;
#else
	char *udi;
#endif
	uint16_t supported_types[LIBMTP_FILETYPE_UNKNOWN+1];
	gboolean album_art_supported;
	RBExtDB *art_store;

	/* device information */
	char *manufacturer;
	char *serial;
	char *device_version;
	char *model_name;
	guint64 capacity;
	guint64 free_space;		/* updated by callbacks */

} RBMtpSourcePrivate;

G_DEFINE_DYNAMIC_TYPE_EXTENDED(
	RBMtpSource,
	rb_mtp_source,
	RB_TYPE_MEDIA_PLAYER_SOURCE,
	0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (RB_TYPE_DEVICE_SOURCE, rb_mtp_device_source_init)
	G_IMPLEMENT_INTERFACE_DYNAMIC (RB_TYPE_TRANSFER_TARGET, rb_mtp_source_transfer_target_init))

#define MTP_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_MTP_SOURCE, RBMtpSourcePrivate))

enum
{
	PROP_0,
	PROP_RAW_DEVICE,
	PROP_UDEV_DEVICE,
	PROP_UDI,
	PROP_DEVICE_SERIAL
};

static void
rb_mtp_source_class_init (RBMtpSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBMediaPlayerSourceClass *mps_class = RB_MEDIA_PLAYER_SOURCE_CLASS (klass);

	object_class->constructed = rb_mtp_source_constructed;
	object_class->dispose = rb_mtp_source_dispose;
	object_class->finalize = rb_mtp_source_finalize;
	object_class->set_property = rb_mtp_source_set_property;
	object_class->get_property = rb_mtp_source_get_property;

	page_class->selected = impl_selected;

	source_class->can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_paste = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->delete_selected = impl_delete_selected;
	source_class->paste = impl_paste;
	source_class->uri_is_source = impl_uri_is_source;

	mps_class->get_entries = impl_get_entries;
	mps_class->get_capacity = impl_get_capacity;
	mps_class->get_free_space = impl_get_free_space;
	mps_class->delete_entries = impl_delete_entries;
	mps_class->show_properties = impl_show_properties;

	g_object_class_install_property (object_class,
					 PROP_RAW_DEVICE,
					 g_param_spec_pointer ("raw-device",
							       "raw-device",
							       "libmtp raw device",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
#if defined(HAVE_GUDEV)
	g_object_class_install_property (object_class,
					 PROP_UDEV_DEVICE,
					 g_param_spec_object ("udev-device",
							      "udev-device",
							      "GUdev device object",
							      G_UDEV_TYPE_DEVICE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
#else
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
rb_mtp_device_source_init (RBDeviceSourceInterface *interface)
{
	interface->can_eject = impl_can_eject;
	interface->eject = impl_eject;
}

static void
rb_mtp_source_transfer_target_init (RBTransferTargetInterface *interface)
{
	interface->track_upload = impl_track_upload;
	interface->track_added = impl_track_added;
	interface->track_add_error = impl_track_add_error;
}

static void
rb_mtp_source_class_finalize (RBMtpSourceClass *klass)
{
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
}


static void
open_device (RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);

	rb_debug ("actually opening device");
	g_object_set (source, "load-status", RB_SOURCE_LOAD_STATUS_LOADING, NULL);
	priv->device_thread = rb_mtp_thread_new ();
	rb_mtp_thread_open_device (priv->device_thread,
				   &priv->raw_device,
				   (RBMtpOpenCallback)mtp_device_open_cb,
				   g_object_ref (source),
				   g_object_unref);
}

#if defined(HAVE_GUDEV)
static void
unmount_done_cb (GObject *object, GAsyncResult *result, gpointer psource)
{
	GMount *mount;
	RBMtpSource *source;
	gboolean ok;
	GError *error = NULL;
	RBMtpSourcePrivate *priv;

	mount = G_MOUNT (object);
	source = RB_MTP_SOURCE (psource);
	priv = MTP_SOURCE_GET_PRIVATE (source);

	ok = g_mount_unmount_with_operation_finish (mount, result, &error);
	if (ok) {
		rb_debug ("successfully unmounted mtp device");
		priv->remount_volume = g_mount_get_volume (mount);

		open_device (source);
	} else {
		g_warning ("Unable to unmount MTP device: %s", error->message);
		g_error_free (error);
	}

	g_object_unref (mount);
	g_object_unref (source);
}

#endif

static gboolean
ensure_loaded (RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
#if defined(HAVE_GUDEV)
	GMount *mount;
#endif
	if (priv->tried_open) {
		RBSourceLoadStatus status;
		g_object_get (source, "load-status", &status, NULL);
		return (status == RB_SOURCE_LOAD_STATUS_LOADED);
	}
	priv->tried_open = TRUE;

	/* try to open the device.  if gvfs has mounted it, unmount it first */
#if defined(HAVE_GUDEV)
	mount = find_mount_for_device (priv->udev_device);
	if (mount != NULL) {
		rb_debug ("device is already mounted, waiting until activated");
		g_mount_unmount_with_operation (mount,
						G_MOUNT_UNMOUNT_NONE,
						NULL,
						NULL,
						unmount_done_cb,
						g_object_ref (source));
		/* mount gets unreffed in callback */
	} else {
		rb_debug ("device isn't mounted");
		open_device (source);
	}
#else
	open_device (source);
#endif
	return FALSE;
}

static void
impl_selected (RBDisplayPage *page)
{
	ensure_loaded (RB_MTP_SOURCE (page));
}

static void
rb_mtp_source_constructed (GObject *object)
{
	RBMtpSource *source;
	RBEntryView *tracks;
	RBShell *shell;
	RBShellPlayer *shell_player;
	GObject *player_backend;

	RB_CHAIN_GOBJECT_METHOD (rb_mtp_source_parent_class, constructed, object);
	source = RB_MTP_SOURCE (object);

	tracks = rb_source_get_entry_view (RB_SOURCE (source));
	rb_entry_view_append_column (tracks, RB_ENTRY_VIEW_COL_RATING, FALSE);
	rb_entry_view_append_column (tracks, RB_ENTRY_VIEW_COL_LAST_PLAYED, FALSE);

	/* the source element needs our cooperation */
	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "shell-player", &shell_player, NULL);
	g_object_get (shell_player, "player", &player_backend, NULL);
	g_object_unref (shell_player);

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

	rb_display_page_set_icon_name (RB_DISPLAY_PAGE (source), "multimedia-player-symbolic");
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
#if defined(HAVE_GUDEV)
	case PROP_UDEV_DEVICE:
		priv->udev_device = g_value_dup_object (value);
		break;
#else
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
#if defined(HAVE_GUDEV)
	case PROP_UDEV_DEVICE:
		g_value_set_object (value, priv->udev_device);
		break;
#else
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

#if defined(HAVE_GUDEV)
static void
remount_done_cb (GObject *object, GAsyncResult *result, gpointer no)
{
	gboolean ok;
	GError *error = NULL;

	ok = g_volume_mount_finish (G_VOLUME (object), result, &error);
	if (ok) {
		rb_debug ("volume remounted successfully");
	} else {
		g_warning ("Unable to remount MTP device: %s", error->message);
		g_error_free (error);
	}
	g_object_unref (object);
}
#endif

static void
rb_mtp_source_dispose (GObject *object)
{
	RBMtpSource *source = RB_MTP_SOURCE (object);
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	RhythmDBEntryType *entry_type;
	RhythmDB *db;

	if (priv->device_thread != NULL) {
		g_object_unref (priv->device_thread);
		priv->device_thread = NULL;
	}

#if defined(HAVE_GUDEV)
	if (priv->remount_volume != NULL) {
		rb_debug ("remounting gvfs volume for mtp device");
		/* the callback will unref remount_volume */
		g_volume_mount (priv->remount_volume,
				G_MOUNT_MOUNT_NONE,
				NULL,
				NULL,
				remount_done_cb,
				NULL);
		priv->remount_volume = NULL;
	}
#endif
	if (priv->art_store != NULL) {
		rb_ext_db_cancel_requests (priv->art_store, (RBExtDBRequestCallback) art_request_cb, source);
		g_clear_object (&priv->art_store);
	}

	db = get_db_for_source (source);

	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	rhythmdb_entry_delete_by_type (db, entry_type);
	g_object_unref (entry_type);

	rhythmdb_commit (db);
	g_object_unref (db);

	G_OBJECT_CLASS (rb_mtp_source_parent_class)->dispose (object);
}

static void
rb_mtp_source_finalize (GObject *object)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (object);

	g_hash_table_destroy (priv->entry_map);

#if defined(HAVE_GUDEV)
	if (priv->udev_device) {
		g_object_unref (G_OBJECT (priv->udev_device));
	}
#else
	g_free (priv->udi);
#endif
	g_free (priv->manufacturer);
	g_free (priv->device_version);
	g_free (priv->model_name);
	g_free (priv->serial);

	G_OBJECT_CLASS (rb_mtp_source_parent_class)->finalize (object);
}

RBSource *
rb_mtp_source_new (RBShell *shell,
		   GObject *plugin,
#if defined(HAVE_GUDEV)
		   GUdevDevice *udev_device,
#else
		   const char *udi,
#endif
		   LIBMTP_raw_device_t *device)
{
	RBMtpSource *source = NULL;
	RhythmDBEntryType *entry_type;
	RhythmDB *db = NULL;
	GSettings *settings;
	GtkBuilder *builder;
	GMenu *toolbar;
	char *name = NULL;

	g_object_get (shell, "db", &db, NULL);
	name = g_strdup_printf ("MTP-%u-%d", device->bus_location, device->devnum);

	entry_type = g_object_new (RHYTHMDB_TYPE_ENTRY_TYPE,
				   "db", db,
				   "name", name,
				   "save-to-disk", FALSE,
				   "category", RHYTHMDB_ENTRY_NORMAL,
				   NULL);
	g_free (name);
	g_object_unref (db);

	builder = rb_builder_load_plugin_file (plugin, "mtp-toolbar.ui", NULL);
	toolbar = G_MENU (gtk_builder_get_object (builder, "mtp-toolbar"));
	rb_application_link_shared_menus (RB_APPLICATION (g_application_get_default ()), toolbar);

	settings = g_settings_new ("org.gnome.rhythmbox.plugins.mtpdevice");
	source = RB_MTP_SOURCE (g_object_new (RB_TYPE_MTP_SOURCE,
					      "plugin", plugin,
					      "entry-type", entry_type,
					      "shell", shell,
					      "visibility", TRUE,
					      "raw-device", device,
#if defined(HAVE_GUDEV)
					      "udev-device", udev_device,
#else
					      "udi", udi,
#endif
					      "load-status", RB_SOURCE_LOAD_STATUS_NOT_LOADED,
					      "settings", g_settings_get_child (settings, "source"),
					      "encoding-settings", g_settings_get_child (settings, "encoding"),
					      "toolbar-menu", toolbar,
					      "name", _("Media Player"),
					      NULL));
	g_object_unref (settings);
	g_object_unref (builder);

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
		rb_mtp_thread_report_errors (priv->device_thread);
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
	RhythmDBEntryType *entry_type;
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
        g_object_unref (entry_type);

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
	/* Set release date */
	if (track->date != NULL && track->date[0] != '\0') {
		GTimeVal tv;
		if (g_time_val_from_iso8601 (track->date, &tv)) {
			GDate d;
			GValue value = {0, };
			g_value_init (&value, G_TYPE_ULONG);
			g_date_set_time_val (&d, &tv);
			g_value_set_ulong (&value, g_date_get_julian (&d));
			rhythmdb_entry_set (RHYTHMDB (db), entry, RHYTHMDB_PROP_DATE, &value);
			g_value_unset (&value);
		}
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
	GstEncodingTarget *target;
	GList *profiles = NULL;

	if (data->name != NULL) {
		g_object_set (data->source, "name", data->name, NULL);
	}

	/* when the source name changes after this, try to update the device name */
	g_signal_connect (G_OBJECT (data->source), "notify::name",
			  (GCallback)rb_mtp_source_name_changed_cb, NULL);

	rb_media_player_source_load (RB_MEDIA_PLAYER_SOURCE (data->source));

	for (i = 0; i < data->num_types; i++) {
		const char *mediatype;
		gboolean prepend;
		if (i <= LIBMTP_FILETYPE_UNKNOWN) {
			priv->supported_types[data->types[i]] = 1;
		}

		mediatype = NULL;
		prepend = FALSE;
		switch (data->types[i]) {
		case LIBMTP_FILETYPE_WAV:
			/*mediatype = "audio/x-wav";*/
			/* don't bother including this? */
			break;
		case LIBMTP_FILETYPE_MP3:
			mediatype = "audio/mpeg";
			prepend = TRUE;		/* always goes first if supported */
			break;
		case LIBMTP_FILETYPE_WMA:
			mediatype = "audio/x-wma";
			break;
		case LIBMTP_FILETYPE_OGG:
			mediatype = "audio/x-vorbis";
			break;
		case LIBMTP_FILETYPE_MP4:
		case LIBMTP_FILETYPE_M4A:
		case LIBMTP_FILETYPE_AAC:
			mediatype = "audio/x-aac";
			break;
		case LIBMTP_FILETYPE_WMV:
			mediatype = "audio/x-ms-wmv";		/* media type? */
			break;
		case LIBMTP_FILETYPE_ASF:
			mediatype = "video/x-ms-asf";		/* media type? */
			break;
		case LIBMTP_FILETYPE_FLAC:
			mediatype = "audio/x-flac";
			break;

		case LIBMTP_FILETYPE_JPEG:
			rb_debug ("JPEG (album art) supported");
			priv->album_art_supported = TRUE;
			break;

		default:
			rb_debug ("unknown libmtp filetype %s supported", LIBMTP_Get_Filetype_Description (data->types[i]));
			break;
		}

		if (mediatype != NULL) {
			GstEncodingProfile *profile;
			profile = rb_gst_get_encoding_profile (mediatype);
			if (profile != NULL) {
				rb_debug ("media type %s supported", mediatype);
				if (prepend) {
					profiles = g_list_prepend (profiles, profile);
				} else {
					profiles = g_list_append (profiles, profile);
				}
			} else {
				rb_debug ("no encoding profile for supported media type %s", mediatype);
			}
		}
	}

	if (priv->album_art_supported) {
		priv->art_store = rb_ext_db_new ("album-art");
	}

	target = gst_encoding_target_new ("mtpdevice", "device", "", profiles);
	g_object_set (data->source, "encoding-target", target, NULL);

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
	rb_display_page_delete_thyself (RB_DISPLAY_PAGE (source));
	g_object_unref (source);
	return FALSE;
}

static gboolean
device_open_ignore_idle (DeviceOpenedData *data)
{
	rb_display_page_delete_thyself (RB_DISPLAY_PAGE (data->source));
	g_object_unref (data->source);
	free (data->types);
	g_free (data->name);
	g_free (data);
	return FALSE;
}

/* this callback runs on the device handling thread, so it can call libmtp directly */
static void
mtp_device_open_cb (LIBMTP_mtpdevice_t *device, RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	gboolean has_audio = FALSE;
	DeviceOpenedData *data;

	if (device == NULL) {
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

	/* figure out the set of formats supported by the device, ensuring there's at least
	 * one audio format aside from WAV.  the purpose of this is to exclude cameras and other
	 * MTP devices that aren't interesting to us.
	 */
	if (LIBMTP_Get_Supported_Filetypes (device, &data->types, &data->num_types) != 0) {
		rb_mtp_thread_report_errors (priv->device_thread);
	} else {
		int i;
		for (i = 0; i < data->num_types; i++) {
			if (data->types[i] != LIBMTP_FILETYPE_WAV && LIBMTP_FILETYPE_IS_AUDIO (data->types[i])) {
				has_audio = TRUE;
				break;
			}
		}
	}

	if (has_audio == FALSE) {
		rb_debug ("device doesn't support any audio formats");
		g_idle_add ((GSourceFunc) device_open_ignore_idle, data);
		return;
	}

	g_idle_add ((GSourceFunc) device_opened_idle, data);

	/* now get the track list */
	rb_mtp_thread_get_track_list (priv->device_thread, (RBMtpTrackListCallback) mtp_tracklist_cb, g_object_ref (source), g_object_unref);
}

static gboolean
device_loaded_idle (RBMtpSource *source)
{
	GSettings *settings;

	g_object_set (source, "load-status", RB_SOURCE_LOAD_STATUS_LOADED, NULL);
	g_object_get (source, "encoding-settings", &settings, NULL);
	rb_transfer_target_transfer (RB_TRANSFER_TARGET (source), settings, NULL, FALSE);
	g_object_unref (settings);
	return FALSE;
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

	g_idle_add ((GSourceFunc) device_loaded_idle, source);
}

static char *
gdate_to_char (GDate* date)
{
	return g_strdup_printf ("%04i%02i%02iT000000.0",
				g_date_get_year (date),
				g_date_get_month (date),
				g_date_get_day (date));
}

static LIBMTP_filetype_t
media_type_to_filetype (RBMtpSource *source, const char *media_type)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);

	if (!strcmp (media_type, "audio/mpeg")) {
		return LIBMTP_FILETYPE_MP3;
	}  else if (!strcmp (media_type, "audio/x-wav")) {
		return  LIBMTP_FILETYPE_WAV;
	} else if (!strcmp (media_type, "audio/x-vorbis")) {
		return LIBMTP_FILETYPE_OGG;
	} else if (!strcmp (media_type, "audio/x-aac")) {
		/* try a few different filetypes that might work */
		if (priv->supported_types[LIBMTP_FILETYPE_M4A])
			return LIBMTP_FILETYPE_M4A;
		else if (!priv->supported_types[LIBMTP_FILETYPE_AAC] && priv->supported_types[LIBMTP_FILETYPE_MP4])
			return LIBMTP_FILETYPE_MP4;
		else
			return LIBMTP_FILETYPE_AAC;

	} else if (!strcmp (media_type, "audio/x-wma")) {
		return LIBMTP_FILETYPE_WMA;
	} else if (!strcmp (media_type, "video/x-ms-asf")) {
		return LIBMTP_FILETYPE_ASF;
	} else if (!strcmp (media_type, "audio/x-flac")) {
		return LIBMTP_FILETYPE_FLAC;
	} else {
		rb_debug ("\"%s\" is not a supported media_type", media_type);
		return LIBMTP_FILETYPE_UNKNOWN;
	}
}

static void
impl_delete_selected (RBSource *source)
{
	GList *sel;
	RBEntryView *songs;

	songs = rb_source_get_entry_view (source);
	sel = rb_entry_view_get_selected_entries (songs);
	impl_delete_entries (RB_MEDIA_PLAYER_SOURCE (source), sel, NULL, NULL);
	g_list_free_full (sel, (GDestroyNotify) rhythmdb_entry_unref);
}

static gboolean
impl_uri_is_source (RBSource *source, const char *uri)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	char *source_uri;
	gboolean result;

	if (g_str_has_prefix (uri, "gphoto2://") == FALSE)
		return FALSE;

	source_uri = g_strdup_printf ("gphoto2://[usb:%03d,%03d]/", 
				      priv->raw_device.bus_location,
				      priv->raw_device.devnum);
	result = g_str_has_prefix (uri, source_uri);
	g_free (source_uri);
	return result;
}

static RBTrackTransferBatch *
impl_paste (RBSource *source, GList *entries)
{
	gboolean defer;
	RBTrackTransferBatch *batch;
	GSettings *settings;

	defer = (ensure_loaded (RB_MTP_SOURCE (source)) == FALSE);
	g_object_get (source, "encoding-settings", &settings, NULL);
	batch = rb_transfer_target_transfer (RB_TRANSFER_TARGET (source), settings, entries, defer);
	g_object_unref (settings);
	return batch;
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

static void
art_request_cb (RBExtDBKey *key, RBExtDBKey *store_key, const char *filename, GValue *data, RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);

	if (G_VALUE_HOLDS (data, GDK_TYPE_PIXBUF)) {
		GdkPixbuf *pixbuf;
		const char *album_name;

		pixbuf = GDK_PIXBUF (g_value_get_object (data));

		album_name = rb_ext_db_key_get_field (key, "album");
		rb_mtp_thread_set_album_image (priv->device_thread, album_name, pixbuf);
		queue_free_space_update (source);
	}
}

static void
upload_callback (LIBMTP_track_t *track, GError *error, RBMtpSourceTrackUpload *upload)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (upload->source);
	RhythmDB *db;

	if (error) {
		rb_debug ("upload failed: %s", error->message);
		upload->error = error;
		g_mutex_lock (&upload->lock);
		g_cond_signal (&upload->cond);
		g_mutex_unlock (&upload->lock);
		return;
	}

	if (strcmp (track->album, _("Unknown")) != 0) {
		rb_debug ("adding track to album %s", track->album);
		rb_mtp_thread_add_to_album (priv->device_thread, track, track->album);

		if (priv->album_art_supported) {
			RBExtDBKey *key;

			/* need to do this in an idle handler? */
			key = rb_ext_db_key_create_lookup ("album", track->album);
			rb_ext_db_key_add_field (key, "artist", track->artist);
			rb_ext_db_request (priv->art_store,
					   key,
					   (RBExtDBRequestCallback) art_request_cb,
					   g_object_ref (upload->source),
					   (GDestroyNotify) g_object_unref);
			rb_ext_db_key_free (key);
		}
	}

	db = get_db_for_source (upload->source);
	add_mtp_track_to_db (upload->source, db, track);
	g_object_unref (db);

	queue_free_space_update (upload->source);
	g_mutex_lock (&upload->lock);
	g_cond_signal (&upload->cond);
	g_mutex_unlock (&upload->lock);
}

static void
create_folder_callback (uint32_t folder_id, RBMtpSourceTrackUpload *upload)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (upload->source);
	upload->track->parent_id = folder_id;

	rb_debug ("folder created, uploading file from %s", upload->tempfile);
	rb_mtp_thread_upload_track (priv->device_thread,
				    upload->track,
				    upload->tempfile,
				    (RBMtpUploadCallback) upload_callback,
				    upload,
				    NULL);
}

static void
impl_track_upload (RBTransferTarget *target,
		   RhythmDBEntry *entry,
		   const char *dest,
		   guint64 filesize,
		   const char *media_type,
		   GError **error)
{
	LIBMTP_track_t *track = NULL;
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (target);
	RBMtpSourceTrackUpload *upload;
	GFile *destfile;
	char *track_str;
	char **folder_path;

	track = LIBMTP_new_track_t ();
	track->title = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE);
	track->album = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ALBUM);
	track->artist = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ARTIST);
	track->genre = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_GENRE);
	track->filesize = filesize;

	/* build up device filename */
	if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER) > 0) {
		track_str = g_strdup_printf ("%.2lu.%.2lu ",
					     rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER),
					     rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER));
	} else {
		track_str = g_strdup_printf ("%.2lu ",
					     rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER));
	}
	
	track->filename = g_strdup_printf ("%s%s - %s.%s",
					   track_str,
					   rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST),
					   rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE),
					   rb_gst_media_type_to_extension (media_type));
	g_free (track_str);

	/* construct folder path: artist/album */
	folder_path = g_new0 (char *, 3);
	folder_path[0] = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ALBUM_ARTIST);
	if (folder_path[0] == NULL || folder_path[0][0] == '\0') {
		g_free (folder_path[0]);
		folder_path[0] = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ARTIST);
	}
	folder_path[1] = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ALBUM);

	/* ensure the filename is safe for FAT filesystems and doesn't contain slashes */
	rb_sanitize_path_for_msdos_filesystem (track->filename);
	rb_sanitize_path_for_msdos_filesystem (folder_path[0]);
	rb_sanitize_path_for_msdos_filesystem (folder_path[1]);

	if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE) > 0) {
		GDate d;
		g_date_set_julian (&d, rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE));
		track->date = gdate_to_char (&d);
	}
	track->tracknumber = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER);
	track->duration = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION) * 1000;
	track->rating = rhythmdb_entry_get_double (entry, RHYTHMDB_PROP_RATING) * 20;
	track->usecount = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_PLAY_COUNT);

	track->filetype = media_type_to_filetype (RB_MTP_SOURCE (target), media_type);

	upload = g_new0 (RBMtpSourceTrackUpload, 1);
	g_cond_init (&upload->cond);
	g_mutex_init (&upload->lock);

	g_mutex_lock (&upload->lock);
	upload->track = track;
	upload->source = RB_MTP_SOURCE (g_object_ref (target));

	destfile = g_file_new_for_uri (dest);
	upload->tempfile = g_file_get_path (destfile);
	g_object_unref (destfile);

	/* create folder, then upload the track, then clean up */
	rb_debug ("creating folder %s/%s", folder_path[0], folder_path[1]);
	rb_mtp_thread_create_folder (priv->device_thread,
				     (const char **)folder_path,
				     (RBMtpCreateFolderCallback) create_folder_callback,
				     upload,
				     NULL);

	g_cond_wait (&upload->cond, &upload->lock);

	g_unlink (upload->tempfile);
	g_free (upload->tempfile);

	LIBMTP_destroy_track_t (upload->track);
	g_object_unref (upload->source);

	if (upload->error)
		*error = upload->error;
	g_mutex_unlock (&upload->lock);
	g_free (upload);

	rb_debug ("track upload finished");
}

static gboolean
impl_track_added (RBTransferTarget *target,
		  RhythmDBEntry *entry,
		  const char *dest,
		  guint64 filesize,
		  const char *media_type)
{
	/* already added to the database in upload */
	return FALSE;
}

static gboolean
impl_track_add_error (RBTransferTarget *target,
		      RhythmDBEntry *entry,
		      const char *dest,
		      GError *error)
{
	return TRUE;
}

static void
impl_get_entries (RBMediaPlayerSource *source, const char *category, GHashTable *map)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	GHashTableIter iter;
	gpointer key, value;
	gboolean podcast;

	/* sync category mapping is a bit hackish here, as MTP doesn't categorise
	 * tracks itself.  matching specific genres is about the best we can do.
	 */
	podcast = (g_str_equal (category, SYNC_CATEGORY_PODCAST));

	g_hash_table_iter_init (&iter, priv->entry_map);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		LIBMTP_track_t *track = value;

		if ((g_strcmp0 (track->genre, "Podcast") == 0) == podcast) {
			RhythmDBEntry *entry = key;
			_rb_media_player_source_add_to_map (map, entry);
		}
	}
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
delete_destroy_data (gpointer data)
{
	GTask *task = data;
	g_task_return_boolean (task, FALSE);
	g_object_unref (task);
}

static void
delete_done_cb (LIBMTP_mtpdevice_t *device, GTask *task)
{
	GHashTable *check_folders = g_task_get_task_data (task);
	LIBMTP_folder_t *folders;
	LIBMTP_file_t *files;

	update_free_space_cb (device, RB_MTP_SOURCE (g_task_get_source_object (task)));

	/* if any of the folders we just deleted from are now empty, delete them */
	folders = LIBMTP_Get_Folder_List (device);
	files = LIBMTP_Get_Filelisting_With_Callback (device, NULL, NULL);
	if (folders != NULL) {
		GHashTableIter iter;
		gpointer key;
		g_hash_table_iter_init (&iter, check_folders);
		while (g_hash_table_iter_next (&iter, &key, NULL)) {
			LIBMTP_folder_t *f;
			LIBMTP_folder_t *c;
			LIBMTP_file_t *file;
			uint32_t folder_id = GPOINTER_TO_UINT(key);

			while (folder_id != device->default_music_folder && folder_id != 0) {

				f = LIBMTP_Find_Folder (folders, folder_id);
				if (f == NULL) {
					rb_debug ("unable to find folder %u", folder_id);
					break;
				}

				/* don't delete folders with children that we didn't just delete */
				for (c = f->child; c != NULL; c = c->sibling) {
					if (g_hash_table_lookup (check_folders,
								 GUINT_TO_POINTER (c->folder_id)) == NULL) {
						break;
					}
				}
				if (c != NULL) {
					rb_debug ("folder %s has children", f->name);
					break;
				}

				/* don't delete folders that contain files */
				for (file = files; file != NULL; file = file->next) {
					if (file->parent_id == folder_id) {
						break;
					}
				}

				if (file != NULL) {
					rb_debug ("folder %s contains at least one file: %s", f->name, file->filename);
					break;
				}

				/* ok, the folder is empty */
				rb_debug ("deleting empty folder %s", f->name);
				LIBMTP_Delete_Object (device, f->folder_id);

				/* if the folder we just deleted has siblings, the parent
				 * can't be empty.
				 */
				if (f->sibling != NULL) {
					rb_debug ("folder %s has siblings, can't delete parent", f->name);
					break;
				}
				folder_id = f->parent_id;
			}
		}

		LIBMTP_destroy_folder_t (folders);
	} else {
		rb_debug ("unable to get device folder list");
	}

	/* clean up the file list */
	while (files != NULL) {
		LIBMTP_file_t *n;

		n = files->next;
		LIBMTP_destroy_file_t (files);
		files = n;
	}

	g_task_return_boolean (task, TRUE);
	g_object_unref (task);
}

static void
impl_delete_entries	(RBMediaPlayerSource *source,
			 GList *entries,
			 GAsyncReadyCallback callback,
			 gpointer user_data)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	RhythmDB *db;
	GList *i;
	GHashTable *check_folders;
	GTask *task;

	task = g_task_new (source, NULL, callback, user_data);
	check_folders = g_hash_table_new (g_direct_hash, g_direct_equal);
	g_task_set_task_data (task, check_folders, (GDestroyNotify) g_hash_table_destroy);

	db = get_db_for_source (RB_MTP_SOURCE (source));
	for (i = entries; i != NULL; i = i->next) {
		LIBMTP_track_t *track;
		const char *uri;
		const char *album_name;
		RhythmDBEntry *entry;

		entry = i->data;
		uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		track = g_hash_table_lookup (priv->entry_map, entry);
		if (track == NULL) {
			rb_debug ("Couldn't find track on mtp-device! (%s)", uri);
			continue;
		}

		album_name = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
		if (g_strcmp0 (album_name, _("Unknown")) != 0) {
			rb_mtp_thread_remove_from_album (priv->device_thread, track, album_name);
		}
		rb_mtp_thread_delete_track (priv->device_thread, track);

		g_hash_table_insert (check_folders,
				     GUINT_TO_POINTER (track->parent_id),
				     GINT_TO_POINTER (1));

		g_hash_table_remove (priv->entry_map, entry);
		rhythmdb_entry_delete (db, entry);
	}

	/* callback when all tracks have been deleted */
	rb_mtp_thread_queue_callback (priv->device_thread,
				      (RBMtpThreadCallback) delete_done_cb,
				      task,
				      delete_destroy_data);

	rhythmdb_commit (db);
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
	GObject *plugin;
	char *text;
	GList *output_formats;
	GList *t;
	GString *str;

	g_object_get (source, "plugin", &plugin, NULL);
	builder = rb_builder_load_plugin_file (G_OBJECT (plugin), "mtp-info.ui", NULL);
	g_object_unref (plugin);

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

	str = g_string_new ("");
	output_formats = rb_transfer_target_get_format_descriptions (RB_TRANSFER_TARGET (source));
	for (t = output_formats; t != NULL; t = t->next) {
		if (t != output_formats) {
			g_string_append (str, "\n");
		}
		g_string_append (str, t->data);
	}
	rb_list_deep_free (output_formats);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label-audio-formats-value"));
	gtk_label_set_text (GTK_LABEL (widget), str->str);
	g_string_free (str, TRUE);

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

	if (rb_source_check_entry_type (RB_SOURCE (source), entry) == FALSE) {
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

static gboolean
impl_can_eject (RBDeviceSource *source)
{
	return TRUE;
}

static void
impl_eject (RBDeviceSource *source)
{
	rb_display_page_delete_thyself (RB_DISPLAY_PAGE (source));
}

#if defined(HAVE_GUDEV)

static GMount *
find_mount_for_device (GUdevDevice *device)
{
	GMount *mount = NULL;
	const char *device_file;
	GVolumeMonitor *volmon;
	GList *mounts;
	GList *i;

	device_file = g_udev_device_get_device_file (device);
	if (device_file == NULL) {
		return NULL;
	}

	volmon = g_volume_monitor_get ();
	mounts = g_volume_monitor_get_mounts (volmon);
	g_object_unref (volmon);

	for (i = mounts; i != NULL; i = i->next) {
		GVolume *v;

		v = g_mount_get_volume (G_MOUNT (i->data));
		if (v != NULL) {
			char *devname = NULL;
			gboolean match;

			devname = g_volume_get_identifier (v, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
			g_object_unref (v);
			if (devname == NULL)
				continue;

			match = g_str_equal (devname, device_file);
			g_free (devname);

			if (match) {
				mount = G_MOUNT (i->data);
				g_object_ref (G_OBJECT (mount));
				break;
			}
		}
	}
	g_list_foreach (mounts, (GFunc)g_object_unref, NULL);
	g_list_free (mounts);
	return mount;
}

#endif

void
_rb_mtp_source_register_type (GTypeModule *module)
{
	rb_mtp_source_register_type (module);
}
