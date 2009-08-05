/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  arch-tag: Implementation of Rhythmbox removable media manager
 *
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
 * SECTION:rb-removable-media-manager
 * @short_description: handling of removable media such as audio CDs and DAP devices
 * 
 * The removable media manager maintains the mapping between GIO GVolume and GMount
 * objects and rhythmbox sources, and also performs track transfers between
 * removable media sources and the library.
 */

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#if defined(HAVE_GUDEV)
#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>
#endif

#include "rb-removable-media-manager.h"
#include "rb-library-source.h"
#include "rb-removable-media-source.h"

#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-stock-icons.h"
#include "rhythmdb.h"
#include "rb-marshal.h"
#include "rb-util.h"
#include "rb-encoder.h"

static void rb_removable_media_manager_class_init (RBRemovableMediaManagerClass *klass);
static void rb_removable_media_manager_init (RBRemovableMediaManager *mgr);
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

static void rb_removable_media_manager_cmd_scan_media (GtkAction *action,
						       RBRemovableMediaManager *manager);
static void rb_removable_media_manager_cmd_eject_medium (GtkAction *action,
					       RBRemovableMediaManager *mgr);
static gboolean rb_removable_media_manager_source_can_eject (RBRemovableMediaManager *mgr);
static void rb_removable_media_manager_set_uimanager (RBRemovableMediaManager *mgr,
					     GtkUIManager *uimanager);

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

static void do_transfer (RBRemovableMediaManager *manager);
static void rb_removable_media_manager_cmd_copy_tracks (GtkAction *action,
							RBRemovableMediaManager *mgr);

typedef struct
{
	RBShell *shell;

	RBSource *selected_source;

	GtkActionGroup *actiongroup;
	GtkUIManager *uimanager;

	GList *sources;
	GHashTable *volume_mapping;
	GHashTable *mount_mapping;
	GHashTable *device_mapping;
	gboolean scanned;

	GAsyncQueue *transfer_queue;
	gboolean transfer_running;
	gint transfer_total;
	gint transfer_done;
	double transfer_fraction;

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
	PROP_SOURCE,
	PROP_SCANNED
};

enum
{
	MEDIUM_ADDED,
	TRANSFER_PROGRESS,
	CREATE_SOURCE_DEVICE,
	CREATE_SOURCE_VOLUME,
	CREATE_SOURCE_MOUNT,
	LAST_SIGNAL
};

static guint rb_removable_media_manager_signals[LAST_SIGNAL] = { 0 };

static GtkActionEntry rb_removable_media_manager_actions [] =
{
	{ "RemovableSourceEject", GNOME_MEDIA_EJECT, N_("_Eject"), NULL,
	  N_("Eject this medium"),
	  G_CALLBACK (rb_removable_media_manager_cmd_eject_medium) },
	{ "RemovableSourceCopyAllTracks", GTK_STOCK_CDROM, N_("_Extract to Library"), NULL,
	  N_("Copy all tracks to the library"),
	  G_CALLBACK (rb_removable_media_manager_cmd_copy_tracks) },
	{ "MusicScanMedia", NULL, N_("_Scan Removable Media"), NULL,
	  N_("Scan for new Removable Media"),
	  G_CALLBACK (rb_removable_media_manager_cmd_scan_media) },
};
static guint rb_removable_media_manager_n_actions = G_N_ELEMENTS (rb_removable_media_manager_actions);

static void
rb_removable_media_manager_class_init (RBRemovableMediaManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_removable_media_manager_dispose;
	object_class->finalize = rb_removable_media_manager_finalize;
	object_class->set_property = rb_removable_media_manager_set_property;
	object_class->get_property = rb_removable_media_manager_get_property;

	/**
	 * RBRemovableMediaManager:source:
	 *
	 * The current selected source.
	 */
	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "RBSource",
							      "RBSource object",
							      RB_TYPE_SOURCE,
							      G_PARAM_READWRITE));
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
							      G_PARAM_READWRITE));

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
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, G_TYPE_OBJECT);

	/**
	 * RBRemovableMediaManager::transfer-progress:
	 * @mgr: the #RBRemovableMediaManager
	 * @done: number of tracks that have been fully transferred
	 * @total: total number of tracks to transfer
	 * @progress: fraction of the current track that has been transferred
	 *
	 * Emitted throughout the track transfer process to allow UI elements
	 * showing transfer progress to be updated.
	 */
	rb_removable_media_manager_signals[TRANSFER_PROGRESS] =
		g_signal_new ("transfer-progress",
			      RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRemovableMediaManagerClass, transfer_progress),
			      NULL, NULL,
			      rb_marshal_VOID__INT_INT_DOUBLE,
			      G_TYPE_NONE,
			      3, G_TYPE_INT, G_TYPE_INT, G_TYPE_DOUBLE);

	/**
	 * RBRemovableMediaManager::create-source-device
	 * @mgr: the #RBRemovableMediaManager
	 * @device: the device (actually a #GUdevDevice)
	 *
	 * Emitted when a new device is detected to allow plugins to create a
	 * corresponding #RBSource.  The first signal handler that returns a
	 * source wins.  Plugins should only use this signal if there will be
	 * no #GVolume or #GMount created for the device.
	 */
	rb_removable_media_manager_signals[CREATE_SOURCE_DEVICE] =
		g_signal_new ("create-source-device",
			      RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRemovableMediaManagerClass, create_source_device),
			      rb_signal_accumulator_object_handled, NULL,
			      rb_marshal_OBJECT__OBJECT,
			      RB_TYPE_SOURCE,
			      1, G_TYPE_OBJECT);
	/**
	 * RBRemovableMediaManager::create-source-volume
	 * @mgr: the #RBRemovableMediaManager
	 * @volume: the #GVolume 
	 *
	 * Emitted when a new volume is added to allow plugins to create a
	 * corresponding #RBSource.  The first signal handler that returns
	 * a source wins.  A plugin should only use this signal if it
	 * doesn't require the volume to be mounted.  If the volume must be
	 * mounted to be useful, use the create-source-mount signal instead.
	 */
	rb_removable_media_manager_signals[CREATE_SOURCE_VOLUME] =
		g_signal_new ("create-source-volume",
			      RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRemovableMediaManagerClass, create_source_volume),
			      rb_signal_accumulator_object_handled, NULL,
			      rb_marshal_OBJECT__OBJECT,
			      RB_TYPE_SOURCE,
			      1, G_TYPE_VOLUME);

	/**
	 * RBRemovableMediaManager::create-source-mount
	 * @mgr: the #RBRemovableMediaManager
	 * @device_info: a #MPIDDevice containing information on the device
	 * @mount: the #GMount
	 *
	 * Emitted when a new mount is added to allow plugins to create a
	 * corresponding #RBSource.  The first signal handler that returns
	 * a source wins.  If a source was created for the #GVolume
	 * for a mount, then this signal will not be emitted.
	 */
	rb_removable_media_manager_signals[CREATE_SOURCE_MOUNT] =
		g_signal_new ("create-source-mount",
			      RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRemovableMediaManagerClass, create_source_mount),
			      rb_signal_accumulator_object_handled, NULL,
			      rb_marshal_OBJECT__OBJECT_OBJECT,
			      RB_TYPE_SOURCE,
			      2, G_TYPE_MOUNT, MPID_TYPE_DEVICE);

	g_type_class_add_private (klass, sizeof (RBRemovableMediaManagerPrivate));
}

static void
rb_removable_media_manager_init (RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);

	priv->volume_mapping = g_hash_table_new (NULL, NULL);
	priv->mount_mapping = g_hash_table_new (NULL, NULL);
	priv->device_mapping = g_hash_table_new (g_direct_hash, g_direct_equal);
	priv->transfer_queue = g_async_queue_new ();

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
	priv->mount_removed_id = g_signal_connect_object (G_OBJECT (priv->volume_monitor),
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

	G_OBJECT_CLASS (rb_removable_media_manager_parent_class)->dispose (object);
}

static void
rb_removable_media_manager_finalize (GObject *object)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (object);

	g_hash_table_destroy (priv->device_mapping);
	g_hash_table_destroy (priv->volume_mapping);
	g_hash_table_destroy (priv->mount_mapping);
	g_async_queue_unref (priv->transfer_queue);

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
	case PROP_SOURCE:
	{
		GtkAction *action;
		gboolean can_eject;

		priv->selected_source = g_value_get_object (value);
		/* make 'eject' command sensitive if the source can be ejected. */
		action = gtk_action_group_get_action (priv->actiongroup, "RemovableSourceEject");
		can_eject = rb_removable_media_manager_source_can_eject (RB_REMOVABLE_MEDIA_MANAGER (object));
		gtk_action_set_sensitive (action, can_eject);
		break;
	}
	case PROP_SHELL:
	{
		GtkUIManager *uimanager;

		priv->shell = g_value_get_object (value);
		g_object_get (priv->shell,
			      "ui-manager", &uimanager,
			      NULL);
		rb_removable_media_manager_set_uimanager (RB_REMOVABLE_MEDIA_MANAGER (object), uimanager);
		g_object_unref (uimanager);
		break;
	}
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
	case PROP_SOURCE:
		g_value_set_object (value, priv->selected_source);
		break;
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
	GUdevDeviceNumber devnum;
	devnum = g_udev_device_get_device_number (device);
	rb_debug ("%s event for %s (%x)", action, g_udev_device_get_sysfs_path (device), devnum);

	if (g_str_equal (action, "add")) {
		RBSource *source = NULL;

		/* probably need to filter out devices related to things we've already seen.. */
		if (g_hash_table_lookup (priv->device_mapping, GINT_TO_POINTER (devnum)) != NULL) {
			rb_debug ("already have a source for this device");
			return;
		}

		g_signal_emit (mgr, rb_removable_media_manager_signals[CREATE_SOURCE_DEVICE], 0, device, &source);
		if (source != NULL) {
			rb_debug ("created a source for this device");
			g_hash_table_insert (priv->device_mapping, GINT_TO_POINTER (devnum), source);
			rb_removable_media_manager_append_media_source (mgr, source);
		}
	} else if (g_str_equal (action, "remove")) {
		RBSource *source;

		source = g_hash_table_lookup (priv->device_mapping, GINT_TO_POINTER (devnum));
		if (source) {
			rb_debug ("removing the source created for this device");
			rb_source_delete_thyself (source);
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
	RBRemovableMediaSource *source = NULL;
	GMount *mount;

	g_assert (volume != NULL);

	if (g_hash_table_lookup (priv->volume_mapping, volume) != NULL) {
		return;
	}

	mount = g_volume_get_mount (volume);
	if (mount != NULL) {
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
		rb_removable_media_manager_append_media_source (mgr, RB_SOURCE (source));
	} else {
		rb_debug ("Unhandled media");
	}

}

static void
rb_removable_media_manager_remove_volume (RBRemovableMediaManager *mgr, GVolume *volume)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);
	RBRemovableMediaSource *source;

	g_assert (volume != NULL);

	rb_debug ("volume removed");
	source = g_hash_table_lookup (priv->volume_mapping, volume);
	if (source) {
		rb_source_delete_thyself (RB_SOURCE (source));
	}
}

static void
rb_removable_media_manager_add_mount (RBRemovableMediaManager *mgr, GMount *mount)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);
	RBRemovableMediaSource *source = NULL;
	GVolume *volume;
	GFile *mount_root;
	char *mountpoint;
	MPIDDevice *device_info;

	g_assert (mount != NULL);

	if (g_hash_table_lookup (priv->mount_mapping, mount) != NULL) {
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
	g_object_unref (mount_root);

	device_info = mpid_device_new (mountpoint);
	g_free (mountpoint);

	g_signal_emit (G_OBJECT (mgr), rb_removable_media_manager_signals[CREATE_SOURCE_MOUNT], 0, mount, device_info, &source);

	if (source) {
		g_hash_table_insert (priv->mount_mapping, mount, source);
		rb_removable_media_manager_append_media_source (mgr, RB_SOURCE (source));
	} else {
		rb_debug ("Unhandled media");
	}

	g_object_unref (device_info);
}

static void
rb_removable_media_manager_remove_mount (RBRemovableMediaManager *mgr, GMount *mount)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);
	RBRemovableMediaSource *source;

	g_assert (mount != NULL);

	rb_debug ("mount removed");
	source = g_hash_table_lookup (priv->mount_mapping, mount);
	if (source) {
		rb_source_delete_thyself (RB_SOURCE (source));
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
rb_removable_media_manager_set_uimanager (RBRemovableMediaManager *mgr,
					  GtkUIManager *uimanager)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);
	GtkAction *action;

	if (priv->uimanager != NULL) {
		if (priv->actiongroup != NULL) {
			gtk_ui_manager_remove_action_group (priv->uimanager,
							    priv->actiongroup);
		}
		g_object_unref (G_OBJECT (priv->uimanager));
		priv->uimanager = NULL;
	}

	priv->uimanager = uimanager;

	if (priv->uimanager != NULL) {
		g_object_ref (priv->uimanager);
	}

	if (priv->actiongroup == NULL) {
		priv->actiongroup = gtk_action_group_new ("RemovableMediaActions");
		gtk_action_group_set_translation_domain (priv->actiongroup,
							 GETTEXT_PACKAGE);
		gtk_action_group_add_actions (priv->actiongroup,
					      rb_removable_media_manager_actions,
					      rb_removable_media_manager_n_actions,
					      mgr);
	}

	gtk_ui_manager_insert_action_group (priv->uimanager,
					    priv->actiongroup,
					    0);

	action = gtk_action_group_get_action (priv->actiongroup,
					      "RemovableSourceCopyAllTracks");
	/* Translators: this is the toolbar button label
	   for Copy to Library action. */
	g_object_set (G_OBJECT (action), "short-label", _("Extract"), NULL);

}

static void
rb_removable_media_manager_eject_cb (GObject *object,
				     GAsyncResult *result,
				     RBRemovableMediaManager *mgr)
{
	GError *error = NULL;

	if (G_IS_VOLUME (object)) {
		GVolume *volume = G_VOLUME (object);

		rb_debug ("finishing ejection of volume");
		g_volume_eject_finish (volume, result, &error);
		if (error == NULL) {
			rb_removable_media_manager_remove_volume (mgr, volume);
		}
	} else if (G_IS_MOUNT (object)) {
		GMount *mount = G_MOUNT (object);

		rb_debug ("finishing ejection of mount");
		g_mount_eject_finish (mount, result, &error);
		if (error == NULL) {
			rb_removable_media_manager_remove_mount (mgr, mount);
		}
	}

	if (error != NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FAILED_HANDLED)) {
			rb_error_dialog (NULL, _("Unable to eject"), "%s", error->message);
		} else {
			rb_debug ("eject failure has already been handled");
		}
		g_error_free (error);
	}
	g_object_unref (mgr);
}

static void
rb_removable_media_manager_unmount_cb (GObject *object,
				       GAsyncResult *result,
				       RBRemovableMediaManager *mgr)
{
	GMount *mount = G_MOUNT (object);
	GError *error = NULL;

	rb_debug ("finishing unmount of mount");
	g_mount_unmount_finish (mount, result, &error);
	if (error != NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FAILED_HANDLED)) {
			rb_error_dialog (NULL, _("Unable to unmount"), "%s", error->message);
		} else {
			rb_debug ("unmount failure has already been handled");
		}
		g_error_free (error);
	} else {
		rb_removable_media_manager_remove_mount (mgr, mount);
	}
	g_object_unref (mgr);
}

static gboolean
rb_removable_media_manager_source_can_eject (RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);
	GVolume *volume;
	GMount *mount;
	gboolean result;

	if (RB_IS_REMOVABLE_MEDIA_SOURCE (priv->selected_source) == FALSE) {
		return FALSE;
	}

	g_object_get (priv->selected_source, "volume", &volume, NULL);
	if (volume != NULL) {
		result = g_volume_can_eject (volume);
		g_object_unref (volume);
		return result;
	}

	g_object_get (priv->selected_source, "mount", &mount, NULL);
	if (mount != NULL) {
		result = g_mount_can_eject (mount) || g_mount_can_unmount (mount);
		g_object_unref (mount);
		return result;
	}

	return FALSE;
}

static void
rb_removable_media_manager_cmd_eject_medium (GtkAction *action, RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);
	RBRemovableMediaSource *source = RB_REMOVABLE_MEDIA_SOURCE (priv->selected_source);
	GVolume *volume;
	GMount *mount;

	/* try ejecting based on volume first, then based on the mount,
	 * and finally try unmounting.
	 */

	g_object_get (source, "volume", &volume, NULL);
	if (volume != NULL) {
		if (g_volume_can_eject (volume)) {
			rb_debug ("ejecting volume");
			g_volume_eject (volume,
					G_MOUNT_UNMOUNT_NONE,
					NULL,
					(GAsyncReadyCallback) rb_removable_media_manager_eject_cb,
					g_object_ref (mgr));
		} else {
			/* this should never happen; the eject command will be
			 * insensitive if the selected source cannot be ejected.
			 */
			rb_debug ("don't know what to do with this volume");
		}
		g_object_unref (volume);
		return;
	}

	g_object_get (source, "mount", &mount, NULL);
	if (mount != NULL) {
		if (g_mount_can_eject (mount)) {
			rb_debug ("ejecting mount");
			g_mount_eject (mount,
				       G_MOUNT_UNMOUNT_NONE,
				       NULL,
				       (GAsyncReadyCallback) rb_removable_media_manager_eject_cb,
				       g_object_ref (mgr));
		} else if (g_mount_can_unmount (mount)) {
			rb_debug ("unmounting mount");
			g_mount_unmount (mount,
					 G_MOUNT_UNMOUNT_NONE,
					 NULL,
					 (GAsyncReadyCallback) rb_removable_media_manager_unmount_cb,
					 g_object_ref (mgr));
		} else {
			/* this should never happen; the eject command will be
			 * insensitive if the selected source cannot be ejected.
			 */
			rb_debug ("don't know what to do with this mount");
		}
		g_object_unref (mount);
	}
}

static void
rb_removable_media_manager_cmd_scan_media (GtkAction *action, RBRemovableMediaManager *manager)
{
	rb_removable_media_manager_scan (manager);
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

/* Track transfer */

typedef struct {
	RBRemovableMediaManager *manager;
	RhythmDBEntry *entry;
	char *dest;
	guint64 dest_size;
	GList *mime_types;
	gboolean failed;
	RBTransferCompleteCallback callback;
	gpointer userdata;
} TransferData;

static void
emit_progress (RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);

	g_signal_emit (G_OBJECT (mgr), rb_removable_media_manager_signals[TRANSFER_PROGRESS], 0,
		       priv->transfer_done,
		       priv->transfer_total,
		       priv->transfer_fraction);
}

static void
error_cb (RBEncoder *encoder, GError *error, TransferData *data)
{
	/* ignore 'file exists' */
	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
		rb_debug ("ignoring 'file exists' error for %s", data->dest);
		data->failed = TRUE;
		return;
	}

	rb_debug ("Error transferring track to %s: %s", data->dest, error->message);
	rb_error_dialog (NULL, _("Error transferring track"), "%s", error->message);

	data->failed = TRUE;
	rb_encoder_cancel (encoder);
}

static void
progress_cb (RBEncoder *encoder, double fraction, TransferData *data)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (data->manager);

	rb_debug ("transfer progress %f", (float)fraction);
	priv->transfer_fraction = fraction;
	emit_progress (data->manager);
}

static void
completed_cb (RBEncoder *encoder, guint64 dest_size, TransferData *data)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (data->manager);

	rb_debug ("completed transferring track to %s (%" G_GUINT64_FORMAT " bytes)", data->dest, dest_size);
	if (!data->failed)
		(data->callback) (data->entry, data->dest, dest_size, data->userdata);

	priv->transfer_running = FALSE;
	priv->transfer_done++;
	priv->transfer_fraction = 0.0;
	do_transfer (data->manager);

	g_object_unref (G_OBJECT (encoder));
	g_free (data->dest);
	rb_list_deep_free (data->mime_types);
	g_free (data);
}

static void
do_transfer (RBRemovableMediaManager *manager)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (manager);
	TransferData *data;
	RBEncoder *encoder;

	g_assert (rb_is_main_thread ());

	emit_progress (manager);

	if (priv->transfer_running) {
		rb_debug ("already transferring something");
		return;
	}

	data = g_async_queue_try_pop (priv->transfer_queue);
	if (data == NULL) {
		rb_debug ("transfer queue is empty");
		priv->transfer_total = 0;
		priv->transfer_done = 0;
		emit_progress (manager);
		return;
	}

	priv->transfer_running = TRUE;
	priv->transfer_fraction = 0.0;

	encoder = rb_encoder_new ();
	g_signal_connect (G_OBJECT (encoder),
			  "error", G_CALLBACK (error_cb),
			  data);
	g_signal_connect (G_OBJECT (encoder),
			  "progress", G_CALLBACK (progress_cb),
			  data);
	g_signal_connect (G_OBJECT (encoder),
			  "completed", G_CALLBACK (completed_cb),
			  data);
	rb_debug ("starting transfer of %s to %s",
		  rhythmdb_entry_get_string (data->entry, RHYTHMDB_PROP_LOCATION),
		  data->dest);
	if (rb_encoder_encode (encoder, data->entry, data->dest, data->mime_types) == FALSE) {
		rb_debug ("unable to start transfer");
	}
}

/**
 * rb_removable_media_manager_queue_transfer:
 * @manager: the #RBRemovableMediaManager
 * @entry: the #RhythmDBEntry to transfer
 * @dest: the destination URI
 * @mime_types: a list of acceptable output MIME types
 * @callback: function to call when the transfer is complete
 * @userdata: data to pass to the callback
 *
 * Initiates a track transfer.  This will transfer the track identified by the
 * #RhythmDBEntry to the given destination, transcoding it if its
 * current media type is not in the list of acceptable output types.
 */
void
rb_removable_media_manager_queue_transfer (RBRemovableMediaManager *manager,
					  RhythmDBEntry *entry,
					  const char *dest,
					  GList *mime_types,
					  RBTransferCompleteCallback callback,
					  gpointer userdata)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (manager);
	TransferData *data;

	g_assert (rb_is_main_thread ());

	data = g_new0 (TransferData, 1);
	data->manager = manager;
	data->entry = entry;
	data->dest = g_strdup (dest);
	data->mime_types = rb_string_list_copy (mime_types);
	data->callback = callback;
	data->userdata = userdata;

	g_async_queue_push (priv->transfer_queue, data);
	priv->transfer_total++;
	do_transfer (manager);
}

static gboolean
copy_entry (RhythmDBQueryModel *model,
	    GtkTreePath *path,
	    GtkTreeIter *iter,
	    GList **list)
{
	GList *l;
	l = g_list_append (*list, rhythmdb_query_model_iter_to_entry (model, iter));
	*list = l;
	return FALSE;
}

static void
rb_removable_media_manager_cmd_copy_tracks (GtkAction *action, RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = GET_PRIVATE (mgr);
	RBRemovableMediaSource *source;
	RBLibrarySource *library;
	RhythmDBQueryModel *model;
	GList *list = NULL;

	source = RB_REMOVABLE_MEDIA_SOURCE (priv->selected_source);
	g_object_get (source, "query-model", &model, NULL);
	g_object_get (priv->shell, "library-source", &library, NULL);

	gtk_tree_model_foreach (GTK_TREE_MODEL (model), (GtkTreeModelForeachFunc)copy_entry, &list);
	rb_source_paste (RB_SOURCE (library), list);
	g_list_free (list);

	g_object_unref (model);
	g_object_unref (library);
}
