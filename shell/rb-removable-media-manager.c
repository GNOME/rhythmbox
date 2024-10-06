/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2005 James Livingston  <doclivingston@gmail.com>
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

/**
 * SECTION:rbremovablemediamanager
 * @short_description: handling of removable media such as audio CDs and DAP devices
 * 
 * The removable media manager maintains the mapping between GIO GVolume and GMount
 * objects and rhythmbox sources.
 */

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#if defined(HAVE_GUDEV)
#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#if defined(WITH_LIBMTP)
#include <libmtp.h>

/* bug flags set for Android devices */
#define DEVICE_FLAG_UNLOAD_DRIVER		0x00000002
#define DEVICE_FLAG_BROKEN_MTPGETOBJPROPLIST	0x00000004
#define DEVICE_FLAG_BROKEN_SET_OBJECT_PROPLIST  0x00000100
#define DEVICE_FLAG_BROKEN_SEND_OBJECT_PROPLIST 0x00008000
#define DEVICE_FLAG_LONG_TIMEOUT		0x08000000
#define DEVICE_FLAG_FORCE_RESET_ON_CLOSE	0x10000000

/* Nexus/Pixel (MTP) (1831:4ee1) masks off BROKEN_MTPGETOBJPROPLIST */
#define DEVICE_FLAGS_ANDROID_BUGS (\
	/*DEVICE_FLAG_BROKEN_MTPGETOBJPROPLIST |*/ \
	DEVICE_FLAG_BROKEN_SET_OBJECT_PROPLIST | \
	DEVICE_FLAG_BROKEN_SEND_OBJECT_PROPLIST | \
	DEVICE_FLAG_UNLOAD_DRIVER | \
	DEVICE_FLAG_LONG_TIMEOUT | \
	DEVICE_FLAG_FORCE_RESET_ON_CLOSE)

#endif /* WITH_LIBMTP */
#endif /* HAVE_GUDEV */

#include "rb-removable-media-manager.h"
#include "rb-library-source.h"
#include "rb-device-source.h"

#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-stock-icons.h"
#include "rhythmdb.h"
#include "rb-util.h"

static void rb_removable_media_manager_class_init (RBRemovableMediaManagerClass *klass);
static void rb_removable_media_manager_init (RBRemovableMediaManager *mgr);
static void rb_removable_media_manager_constructed (GObject *object);
static void rb_removable_media_manager_dispose (GObject *object);
static void rb_removable_media_manager_finalize (GObject *object);
static void rb_removable_media_manager_set_property (GObject *object,
					      guint prop_id,
					      const GValue *value,
					      GParamSpec *pspec);
static void rb_removable_media_manager_get_property (GObject *object,
					      guint prop_id,
					      GValue *value,
					      GParamSpec *pspec);

static void eject_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);
static void check_devices_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);
static void page_changed_cb (RBShell *shell, GParamSpec *pspec, RBRemovableMediaManager *mgr);

static void rb_removable_media_manager_append_media_source (RBRemovableMediaManager *mgr, RBSource *source);

static void rb_removable_media_manager_add_volume (RBRemovableMediaManager *mgr, GVolume *volume);
static void rb_removable_media_manager_remove_volume (RBRemovableMediaManager *mgr, GVolume *volume);
static void rb_removable_media_manager_add_mount (RBRemovableMediaManager *mgr, GMount *mount);
static void rb_removable_media_manager_remove_mount (RBRemovableMediaManager *mgr, GMount *mount);

static void volume_added_cb (GVolumeMonitor *monitor, GVolume *volume, RBRemovableMediaManager *manager);
static void volume_removed_cb (GVolumeMonitor *monitor, GVolume *volume, RBRemovableMediaManager *manager);
static void mount_added_cb (GVolumeMonitor *monitor, GMount *mount, RBRemovableMediaManager *manager);
static void mount_removed_cb (GVolumeMonitor *monitor, GMount *mount, RBRemovableMediaManager *manager);
#if defined(HAVE_GUDEV)
static void uevent_cb (GUdevClient *client, const char *action, GUdevDevice *device, RBRemovableMediaManager *manager);
#endif

typedef struct
{
	RBShell *shell;
	guint page_changed_id;

	GList *sources;
	GHashTable *volume_mapping;
	GHashTable *mount_mapping;
	GHashTable *device_mapping;
	gboolean scanned;

	GVolumeMonitor *volume_monitor;
	guint mount_added_id;
	guint mount_pre_unmount_id;
	guint mount_removed_id;
	guint volume_added_id;
	guint volume_removed_id;

#if defined(HAVE_GUDEV)
	GUdevClient *gudev_client;
	guint uevent_id;
#endif
} RBRemovableMediaManagerPrivate;

G_DEFINE_TYPE (RBRemovableMediaManager, rb_removable_media_manager, G_TYPE_OBJECT)
#define GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_REMOVABLE_MEDIA_MANAGER, RBRemovableMediaManagerPrivate))

enum
{
	PROP_0,
	PROP_SHELL,
	PROP_SCANNED
};

enum
{
	MEDIUM_ADDED,
	CREATE_SOURCE_DEVICE,
	CREATE_SOURCE_VOLUME,
	CREATE_SOURCE_MOUNT,
	LAST_SIGNAL
};

static guint rb_removable_media_manager_signals[LAST_SIGNAL] = { 0 };

static void
rb_removable_media_manager_class_init (RBRemovableMediaManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = rb_removable_media_manager_constructed;
	object_class->dispose = rb_removable_media_manager_dispose;
	object_class->finalize = rb_removable_media_manager_finalize;
	object_class->set_property = rb_removable_media_manager_set_property;
	object_class->get_property = rb_removable_media_manager_get_property;

	/**
	 * RBRemovableMediaManager:shell:
	 *
	 * The #RBShell instance.
	 */
	g_object_class_install_property (object_class,
					 PROP_SHELL,
					 g_param_spec_object ("shell",
							      "RBShell",
							      "RBShell object",
							      RB_TYPE_SHELL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBRemovableMediaManager:scanned:
	 *
	 * This is set to TRUE when the removable media manager has scanned
	 * all existing volumes and mounts.  When a plugin that handles removable
	 * media is activated, it should request a new scan if this property is
	 * already set to TRUE.
	 */
	g_object_class_install_property (object_class,
					 PROP_SCANNED,
					 g_param_spec_boolean ("scanned",
						 	       "scanned",
							       "Whether a scan has been performed",
							       FALSE,
							       G_PARAM_READABLE));

	/**
	 * RBRemovableMediaManager::medium-added:
	 * @mgr: the #RBRemovableMediaManager
	 * @source: the newly added #RBSource
	 *
	 * Emitted when a new source is added for a removable medium.
	 */
	rb_removable_media_manager_signals[MEDIUM_ADDED] =
		g_signal_new ("medium_added",
			      RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRemovableMediaManagerClass, medium_added),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1, G_TYPE_OBJECT);


	/**
	 * RBRemovableMediaManager::create-source-device:
	 * @mgr: the #RBRemovableMediaManager
	 * @device: the device (actually a #GUdevDevice)
	 *
	 * Emitted when a new device is detected to allow plugins to create a
	 * corresponding #RBSource.  The first signal handler that returns a
	 * source wins.  Plugins should only use this signal if there will be
	 * no #GVolume or #GMount created for the device.
	 *
	 * Return value: (transfer full): a source for the device, or NULL
	 */
	rb_removable_media_manager_signals[CREATE_SOURCE_DEVICE] =
		g_signal_new ("create-source-device",
			      RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRemovableMediaManagerClass, create_source_device),
			      rb_signal_accumulator_object_handled, NULL,
			      NULL,
			      RB_TYPE_SOURCE,
			      1, G_TYPE_OBJECT);
	/**
	 * RBRemovableMediaManager::create-source-volume:
	 * @mgr: the #RBRemovableMediaManager
	 * @volume: the #GVolume 
	 *
	 * Emitted when a new volume is added to allow plugins to create a
	 * corresponding #RBSource.  The first signal handler that returns
	 * a source wins.  A plugin should only use this signal if it
	 * doesn't require the volume to be mounted.  If the volume must be
	 * mounted to be useful, use the create-source-mount signal instead.
	 *
	 * Return value: (transfer full): a source for the volume, or NULL
	 */
	rb_removable_media_manager_signals[CREATE_SOURCE_VOLUME] =
		g_signal_new ("create-source-volume",
			      RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRemovableMediaManagerClass, create_source_volume),
			      rb_signal_accumulator_object_handled, NULL,
			      NULL,
			      RB_TYPE_SOURCE,
			      1, G_TYPE_VOLUME);

	/**
	 * RBRemovableMediaManager::create-source-mount:
	 * @mgr: the #RBRemovableMediaManager
	 * @device_info: a #MPIDDevice containing information on the device
	 * @mount: the #GMount
	 *
	 * Emitted when a new mount is added to allow plugins to create a
	 * corresponding #RBSource.  The first signal handler that returns
	 * a source wins.  If a source was created for the #GVolume
	 * for a mount, then this signal will not be emitted.
	 *
	 * Return value: (transfer full): a source for the mount, or NULL
	 */
	rb_removable_media_manager_signals[CREATE_SOURCE_MOUNT] =
		g_signal_new ("create-source-mount",
			      RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRemovableMediaManagerClass, create_source_mount),
			      rb_signal_accumulator_object_handled, NULL,
			      NULL,
			      RB_TYPE_SOURCE,
			      2, G_TYPE_MOUNT, MPID_TYPE_DEVICE);

	g_type_class_add_private (klass, sizeof (RBRemovableMediaManagerPrivate));
}

static guint
uint64_hash (gconstpointer v)
{
	return (guint) *(const guint64*)v;
}

static gboolean
uint64_equal (gconstpointer a, gconstpointer b)
{
	return *((const guint64*)a) == *((const guint64*) b);
}

static void
rb_removable_media_manager_init (RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);

	priv->volume_mapping = g_hash_table_new (NULL, NULL);
	priv->mount_mapping = g_hash_table_new (NULL, NULL);
	priv->device_mapping = g_hash_table_new_full (uint64_hash, uint64_equal, g_free, NULL);

	/*
	 * Monitor new (un)mounted file systems to look for new media;
	 * we watch for both volumes and mounts because for some devices,
	 * we don't require the volume to actually be mounted.
	 *
	 * both pre-unmount and unmounted callbacks are registered because it is
	 * better to do it before the unmount, but sometimes we don't get those
	 * (e.g. someone pressing the eject button on a cd drive). If we get the
	 * pre-unmount signal, the corresponding unmounted signal is ignored
	 */
	priv->volume_monitor = g_object_ref (g_volume_monitor_get ());

	priv->volume_added_id = g_signal_connect_object (priv->volume_monitor,
							 "volume-added",
							 G_CALLBACK (volume_added_cb),
							 mgr, 0);
	priv->volume_removed_id = g_signal_connect_object (priv->volume_monitor,
							   "volume-removed",
							   G_CALLBACK (volume_removed_cb),
							   mgr, 0);
	priv->mount_added_id = g_signal_connect_object (priv->volume_monitor,
							"mount-added",
							G_CALLBACK (mount_added_cb),
							mgr, 0);
	priv->mount_pre_unmount_id = g_signal_connect_object (priv->volume_monitor,
							      "mount-pre-unmount",
							      G_CALLBACK (mount_removed_cb),
							      mgr, 0);
	priv->mount_removed_id = g_signal_connect_object (priv->volume_monitor,
							  "mount-removed",
							  G_CALLBACK (mount_removed_cb),
							  mgr, 0);

#if defined(HAVE_GUDEV)
	/*
	 * Monitor udev device events - we're only really interested in events
	 * for USB devices.
	 */
	{
		const char * const subsystems[] = { "usb", NULL };
		priv->gudev_client = g_udev_client_new (subsystems);
	}

	priv->uevent_id = g_signal_connect_object (priv->gudev_client,
						   "uevent",
						   G_CALLBACK (uevent_cb),
						   mgr, 0);
#endif

	/* enable debugging of media player device lookups if requested */
	if (rb_debug_matches ("mpid", "")) {
		mpid_enable_debug (TRUE);
	}
}

static void
rb_removable_media_manager_constructed (GObject *object)
{
	RBRemovableMediaManager *mgr = RB_REMOVABLE_MEDIA_MANAGER (object);
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);

	GApplication *app;
	GActionEntry actions[] = {
		{ "check-devices", check_devices_action_cb },
		{ "removable-media-eject", eject_action_cb }
	};

	RB_CHAIN_GOBJECT_METHOD (rb_removable_media_manager_parent_class, constructed, object);

	app = g_application_get_default ();
	g_action_map_add_action_entries (G_ACTION_MAP (app), actions, G_N_ELEMENTS (actions), mgr);

	priv->page_changed_id = g_signal_connect (priv->shell, "notify::selected-page", G_CALLBACK (page_changed_cb), mgr);
}

static void
rb_removable_media_manager_dispose (GObject *object)
{
	RBRemovableMediaManager *mgr = RB_REMOVABLE_MEDIA_MANAGER (object);
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);

	if (priv->volume_monitor != NULL) {
		g_signal_handler_disconnect (priv->volume_monitor,
					     priv->mount_added_id);
		g_signal_handler_disconnect (priv->volume_monitor,
					     priv->mount_pre_unmount_id);
		g_signal_handler_disconnect (priv->volume_monitor,
					     priv->mount_removed_id);
		g_signal_handler_disconnect (priv->volume_monitor,
					     priv->volume_added_id);
		g_signal_handler_disconnect (priv->volume_monitor,
					     priv->volume_removed_id);

		priv->mount_added_id = 0;
		priv->mount_pre_unmount_id = 0;
		priv->mount_removed_id = 0;
		priv->volume_added_id = 0;
		priv->volume_removed_id = 0;

		g_object_unref (priv->volume_monitor);
		priv->volume_monitor = NULL;
	}

#if defined(HAVE_GUDEV)
	if (priv->gudev_client != NULL) {
		g_signal_handler_disconnect (priv->gudev_client,
					     priv->uevent_id);
		priv->uevent_id = 0;

		g_object_unref (priv->gudev_client);
		priv->gudev_client = NULL;
	}
#endif

	if (priv->sources) {
		g_list_free (priv->sources);
		priv->sources = NULL;
	}

	if (priv->page_changed_id != 0) {
		g_signal_handler_disconnect (priv->shell, priv->page_changed_id);
		priv->page_changed_id = 0;
	}

	G_OBJECT_CLASS (rb_removable_media_manager_parent_class)->dispose (object);
}

static void
rb_removable_media_manager_finalize (GObject *object)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (object);

	g_hash_table_destroy (priv->device_mapping);
	g_hash_table_destroy (priv->volume_mapping);
	g_hash_table_destroy (priv->mount_mapping);

	G_OBJECT_CLASS (rb_removable_media_manager_parent_class)->finalize (object);
}

static void
rb_removable_media_manager_set_property (GObject *object,
				  guint prop_id,
				  const GValue *value,
				  GParamSpec *pspec)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_SHELL:
		priv->shell = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_removable_media_manager_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_SHELL:
		g_value_set_object (value, priv->shell);
		break;
	case PROP_SCANNED:
		g_value_set_boolean (value, priv->scanned);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_removable_media_manager_new:
 * @shell: the #RBShell
 *
 * Creates the #RBRemovableMediaManager instance.
 *
 * Return value: the #RBRemovableMediaManager
 */
RBRemovableMediaManager *
rb_removable_media_manager_new (RBShell *shell)
{
	return g_object_new (RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			     "shell", shell,
			     NULL);
}

static void
volume_added_cb (GVolumeMonitor *monitor,
		 GVolume *volume,
		 RBRemovableMediaManager *mgr)
{
	rb_removable_media_manager_add_volume (mgr, volume);
}

static void
volume_removed_cb (GVolumeMonitor *monitor,
		   GVolume *volume,
		   RBRemovableMediaManager *mgr)
{
	rb_removable_media_manager_remove_volume (mgr, volume);
}

static void
mount_added_cb (GVolumeMonitor *monitor,
		GMount *mount,
		RBRemovableMediaManager *mgr)
{
	rb_removable_media_manager_add_mount (mgr, mount);
}

static void
mount_removed_cb (GVolumeMonitor *monitor,
		  GMount *mount,
		  RBRemovableMediaManager *mgr)
{
	rb_removable_media_manager_remove_mount (mgr, mount);
}

#if defined(HAVE_GUDEV)

static void
uevent_cb (GUdevClient *client, const char *action, GUdevDevice *device, RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);
	guint64 devnum;

	devnum = (guint64) g_udev_device_get_device_number (device);
	rb_debug ("%s event for %s (%"G_GINT64_MODIFIER"x)", action,
                  g_udev_device_get_sysfs_path (device), devnum);

	if (g_str_equal (action, "add")) {
		RBSource *source = NULL;

		/* probably need to filter out devices related to things we've already seen.. */
		if (g_hash_table_lookup (priv->device_mapping, &devnum) != NULL) {
			rb_debug ("already have a source for this device");
		} else {
			g_signal_emit (mgr, rb_removable_media_manager_signals[CREATE_SOURCE_DEVICE], 0, device, &source);
			if (source != NULL) {
				guint64 *key = g_new0 (guint64, 1);
				rb_debug ("created a source for this device");
				key[0] = devnum;
				g_hash_table_insert (priv->device_mapping, key, source);
				rb_removable_media_manager_append_media_source (mgr, source);
			}
		}
	} else if (g_str_equal (action, "remove")) {
		RBSource *source;

		source = g_hash_table_lookup (priv->device_mapping, &devnum);
		if (source) {
			rb_debug ("removing the source created for this device");
			rb_display_page_delete_thyself (RB_DISPLAY_PAGE (source));
		}
	}
}
#endif

static gboolean
remove_by_source (gpointer thing, RBSource *source, RBSource *ref_source)
{
	return (ref_source == source);
}

static void
rb_removable_media_manager_source_deleted_cb (RBSource *source, RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);

	rb_debug ("removing source %p", source);
	g_hash_table_foreach_remove (priv->volume_mapping,
				     (GHRFunc)remove_by_source,
				     source);
	g_hash_table_foreach_remove (priv->mount_mapping,
				     (GHRFunc)remove_by_source,
				     source);
	g_hash_table_foreach_remove (priv->device_mapping,
				     (GHRFunc)remove_by_source,
				     source);
	priv->sources = g_list_remove (priv->sources, source);
}

static void
dump_volume_identifiers (GVolume *volume)
{
	char **identifiers;
	int i;

	if (volume == NULL) {
		rb_debug ("mount has no volume");
		return;
	}

	/* dump all volume identifiers in debug output */
	identifiers = g_volume_enumerate_identifiers (volume);
	if (identifiers != NULL) {
		for (i = 0; identifiers[i] != NULL; i++) {
			char *ident;

			ident = g_volume_get_identifier (volume, identifiers[i]);
			rb_debug ("%s = %s", identifiers[i], ident);
		}
		g_strfreev (identifiers);
	}
}

static void
rb_removable_media_manager_add_volume (RBRemovableMediaManager *mgr, GVolume *volume)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);
	RBSource *source = NULL;
	GMount *mount;

	g_assert (volume != NULL);

	if (g_hash_table_lookup (priv->volume_mapping, volume) != NULL) {
		return;
	}

	mount = g_volume_get_mount (volume);
	if (mount != NULL) {
		if (g_mount_is_shadowed (mount) != FALSE) {
			rb_debug ("mount is shadowed, so ignoring the volume");
			g_object_unref (mount);
			return;
		}
		if (g_hash_table_lookup (priv->mount_mapping, mount) != NULL) {
			/* this can probably never happen, but it's OK */
			rb_debug ("already created a source for the mount, so ignoring the volume");
			g_object_unref (mount);
			return;
		}
		g_object_unref (mount);
	}

	dump_volume_identifiers (volume);

	g_signal_emit (G_OBJECT (mgr), rb_removable_media_manager_signals[CREATE_SOURCE_VOLUME], 0, volume, &source);

	if (source) {
		g_hash_table_insert (priv->volume_mapping, volume, source);
		rb_removable_media_manager_append_media_source (mgr, source);
	} else {
		rb_debug ("Unhandled media");
	}
}

static void
rb_removable_media_manager_remove_volume (RBRemovableMediaManager *mgr, GVolume *volume)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);
	RBSource *source;

	g_assert (volume != NULL);

	rb_debug ("volume removed");
	source = g_hash_table_lookup (priv->volume_mapping, volume);
	if (source) {
		rb_display_page_delete_thyself (RB_DISPLAY_PAGE (source));
	}
}

static void
enum_children_ready (GObject *obj, GAsyncResult *result, gpointer user_data)
{
	GFileEnumerator *e;
	GMount *mount = G_MOUNT (user_data);

	e = g_file_enumerate_children_finish (G_FILE (obj), result, NULL);
	g_object_set_data (G_OBJECT (mount), "rb-file-enum", e);

	g_object_unref (mount);
}

static void
rb_removable_media_manager_add_mount (RBRemovableMediaManager *mgr, GMount *mount)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);
	RBSource *source = NULL;
	GVolume *volume;
	GFile *mount_root;
	char *mountpoint;
	MPIDDevice *device_info;

	g_assert (mount != NULL);

	if (g_hash_table_lookup (priv->mount_mapping, mount) != NULL) {
		return;
	}
	if (g_mount_is_shadowed (mount) != FALSE) {
		return;
	}
	volume = g_mount_get_volume (mount);
	if (volume == NULL) {
		rb_debug ("Unhandled media, no volume for mount");
		return;
	}

	/* if we've already created a source for the volume,
	 * don't do anything with the mount.
	 */
	if (g_hash_table_lookup (priv->volume_mapping, volume) != NULL) {
		rb_debug ("already created a source for the volume, so ignoring the mount");
		g_object_unref (volume);
		return;
	}

	dump_volume_identifiers (volume);
	g_object_unref (volume);

	/* look the device up in the device info database */
	mount_root = g_mount_get_root (mount);
	if (mount_root == NULL) {
		rb_debug ("unable to get mount root, can't create a source for this mount");
		return;
	}
	mountpoint = g_file_get_path (mount_root);

	device_info = mpid_device_new (mountpoint);

	g_signal_emit (G_OBJECT (mgr), rb_removable_media_manager_signals[CREATE_SOURCE_MOUNT], 0, mount, device_info, &source);

	if (source) {
		g_hash_table_insert (priv->mount_mapping, mount, source);
		rb_removable_media_manager_append_media_source (mgr, source);

		/*
		 * if we don't have an open connection to the mount process,
		 * gvfs won't invalidate its mount info cache if the mount
		 * goes away.
		 */
		g_file_enumerate_children_async (mount_root, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT, NULL,
			enum_children_ready, g_object_ref (mount));
	} else {
		rb_debug ("Unhandled media");
	}

	g_object_unref (device_info);
	g_free (mountpoint);
	g_object_unref (mount_root);
}

static void
rb_removable_media_manager_remove_mount (RBRemovableMediaManager *mgr, GMount *mount)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);
	RBSource *source;
	GFileEnumerator *e;

	g_assert (mount != NULL);

	rb_debug ("mount removed");
	source = g_hash_table_lookup (priv->mount_mapping, mount);
	if (source) {
		rb_display_page_delete_thyself (RB_DISPLAY_PAGE (source));
	}

	e = G_FILE_ENUMERATOR (g_object_get_data (G_OBJECT (mount), "rb-file-enum"));
	if (e != NULL) {
		g_object_unref (e);
		g_object_set_data (G_OBJECT (mount), "rb-file-enum", NULL);
	}
}

static void
rb_removable_media_manager_append_media_source (RBRemovableMediaManager *mgr, RBSource *source)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);

	priv->sources = g_list_prepend (priv->sources, source);
	g_signal_connect_object (G_OBJECT (source), "deleted",
				 G_CALLBACK (rb_removable_media_manager_source_deleted_cb), mgr, 0);

	g_signal_emit (G_OBJECT (mgr), rb_removable_media_manager_signals[MEDIUM_ADDED], 0,
		       source);
}

static void
page_changed_cb (RBShell *shell, GParamSpec *pspec, RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);
	RBDisplayPage *page;
	gboolean can_eject;
	GApplication *app;
	GAction *action;

	g_object_get (priv->shell, "selected-page", &page, NULL);

	if (RB_IS_DEVICE_SOURCE (page)) {
		can_eject = rb_device_source_can_eject (RB_DEVICE_SOURCE (page));
	} else {
		can_eject = FALSE;
	}

	app = g_application_get_default ();
	action = g_action_map_lookup_action (G_ACTION_MAP (app), "removable-media-eject");
	g_object_set (action, "enabled", can_eject, NULL);

	g_object_unref (page);
}

static void
eject_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBRemovableMediaManager *mgr = RB_REMOVABLE_MEDIA_MANAGER (data);
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);
	RBDisplayPage *page;

	g_object_get (priv->shell, "selected-page", &page, NULL);

	if (RB_IS_DEVICE_SOURCE (page)) {
		rb_device_source_eject (RB_DEVICE_SOURCE (page));
	}
}

static void
check_devices_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	rb_removable_media_manager_scan (RB_REMOVABLE_MEDIA_MANAGER (data));
}

/**
 * rb_removable_media_manager_scan:
 * @manager: the #RBRemovableMediaManager
 *
 * Initiates a new scan of all attached media.  Newly activated plugins that use
 * the create-source-volume or create-source-mount signals should call this if
 * the 'scanned' property is %TRUE.  Otherwise, the first scan will catch any
 * existing volumes or mounts that the plugin is interested in.
 */
void
rb_removable_media_manager_scan (RBRemovableMediaManager *manager)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (manager);
	GHashTableIter iter;
	GList *list, *it;
	gpointer hkey, hvalue;

	priv->scanned = TRUE;

	/* check volumes first */
	list = g_volume_monitor_get_volumes (priv->volume_monitor);

	/* - check for volumes that have disappeared */
	g_hash_table_iter_init (&iter, priv->volume_mapping);
	while (g_hash_table_iter_next (&iter, &hkey, &hvalue)) {
		GVolume *volume = G_VOLUME (hkey);

		if (g_list_index (list, volume) == -1) {
			/* volume has vanished */
			rb_removable_media_manager_remove_volume (manager, volume);
		}
	}

	/* - check for newly added volumes */
	for (it = list; it != NULL; it = g_list_next (it)) {
		GVolume *volume = G_VOLUME (it->data);
		rb_removable_media_manager_add_volume (manager, volume);
		g_object_unref (volume);
	}
	g_list_free (list);

	/* check mounts */
	list = g_volume_monitor_get_mounts (priv->volume_monitor);

	/* - check for mounts that have disappeared */
	g_hash_table_iter_init (&iter, priv->mount_mapping);
	while (g_hash_table_iter_next (&iter, &hkey, &hvalue)) {
		GMount *mount = G_MOUNT (hkey);

		if (g_list_index (list, mount) == -1) {
			rb_removable_media_manager_remove_mount (manager, mount);
		}
	}

	/* - check for newly added mounts */
	for (it = list; it != NULL; it = g_list_next (it)) {
		GMount *mount = G_MOUNT (it->data);
		rb_removable_media_manager_add_mount (manager, mount);
		g_object_unref (mount);
	}
	g_list_free (list);

	/* - check devices */
#if defined(HAVE_GUDEV)
	list = g_udev_client_query_by_subsystem (priv->gudev_client, "usb");
	for (it = list; it != NULL; it = g_list_next (it)) {
		/* pretend the device was just added */
		uevent_cb (priv->gudev_client, "add", G_UDEV_DEVICE (it->data), manager);
	}
	g_list_free (list);
#endif
}

/**
 * rb_removable_media_manager_get_gudev_device:
 * @manager: the #RBRemovableMediaManager
 * @volume: the #GVolume
 *
 * Finds the #GUdevDevice for the volume.
 *
 * Return value: (transfer full): the #GUDevDevice instance, if any
 */
GObject *
rb_removable_media_manager_get_gudev_device (RBRemovableMediaManager *manager, GVolume *volume)
{
#if defined(HAVE_GUDEV)
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (manager);
	char *devpath;
	GUdevDevice *udevice = NULL;

	devpath = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	if (devpath != NULL)
		udevice = g_udev_client_query_by_device_file (priv->gudev_client, devpath);

	g_free (devpath);
	return G_OBJECT (udevice);
#else
	return NULL;
#endif
}

#if defined(HAVE_GUDEV)
static int
get_property_as_int (GUdevDevice *device, const char *property, int base)
{
	const char *strvalue;

	strvalue = g_udev_device_get_property (device, property);
	if (strvalue == NULL) {
		return 0;
	}

	return strtol (strvalue, NULL, base);
}
#endif

/**
 * rb_removable_media_manager_device_is_android:
 * @manager: the #RBRemovableMediaManager
 * @device: the #GUdevDevice to query
 *
 * Determines whether the specified device looks like an Android device.
 *
 * Return value: %TRUE if the device appears to be Android-based
 */
gboolean
rb_removable_media_manager_device_is_android (RBRemovableMediaManager *manager, GObject *gdevice)
{
#if !defined(HAVE_GUDEV)
	return TRUE;
#else
	GUdevDevice *device = G_UDEV_DEVICE (gdevice);
#if defined(WITH_LIBMTP)
	LIBMTP_device_entry_t *device_list;
	int numdevs;
	int i;
#endif
	int vendor;
	int model;

	if (g_strcmp0 (g_udev_device_get_subsystem (device), "usb") != 0) {
		rb_debug ("device %s is not a USB device", g_udev_device_get_name (device));
		return FALSE;
	}

	/* check that it's not an iPhone or iPod Touch */
	if (g_udev_device_get_property_as_boolean (device, "USBMUX_SUPPORTED")) {
		rb_debug ("device %s is supported through AFC, ignore", g_udev_device_get_name (device));
		return FALSE;
	}

	if (g_udev_device_has_property (device, "ID_MTP_DEVICE") == FALSE) {
		rb_debug ("device %s does not support mtp, ignore", g_udev_device_get_name (device));
		return FALSE;
	}

	vendor = get_property_as_int (device, "ID_VENDOR_ID", 16);
	model = get_property_as_int (device, "ID_MODEL_ID", 16);
#if defined(WITH_LIBMTP)

	rb_debug ("matching device %x:%x against libmtp device list", vendor, model);
	LIBMTP_Get_Supported_Devices_List(&device_list, &numdevs);
	for (i = 0; i < numdevs; i++) {
		if (device_list[i].vendor_id == vendor &&
		    device_list[i].product_id == model) {
			rb_debug ("matched libmtp device vendor %s product %s", device_list[i].vendor, device_list[i].product);
			if ((device_list[i].device_flags & DEVICE_FLAGS_ANDROID_BUGS) != DEVICE_FLAGS_ANDROID_BUGS) {
				rb_debug ("device doesn't have all android bug flags set");
				return FALSE;
			} else {
				rb_debug ("device has android bug flags set");
				return TRUE;
			}
		}
	}
#endif
	rb_debug ("unable to match device %x:%x against device list, assuming android", vendor, model);
	return TRUE;
#endif
}

