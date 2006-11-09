/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * rb-audiocd-plugin.c
 * * Copyright (C) 2006  James Livingston
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#define __EXTENSIONS__

#include "config.h"

#include <string.h> /* For strlen */

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>

/* nautilus-cd-burner stuff */
#include <nautilus-burn-drive.h>
#ifndef NAUTILUS_BURN_CHECK_VERSION
#define NAUTILUS_BURN_CHECK_VERSION(a,b,c) FALSE
#endif

#if NAUTILUS_BURN_CHECK_VERSION(2,15,3)
#include <nautilus-burn.h>
#endif

#ifndef HAVE_BURN_DRIVE_UNREF
#define nautilus_burn_drive_unref nautilus_burn_drive_free
#endif


#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-dialog.h"
#include "rb-removable-media-manager.h"
#include "rb-audiocd-source.h"


#define RB_TYPE_AUDIOCD_PLUGIN		(rb_audiocd_plugin_get_type ())
#define RB_AUDIOCD_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOCD_PLUGIN, RBAudioCdPlugin))
#define RB_AUDIOCD_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_AUDIOCD_PLUGIN, RBAudioCdPluginClass))
#define RB_IS_AUDIOCD_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOCD_PLUGIN))
#define RB_IS_AUDIOCD_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOCD_PLUGIN))
#define RB_AUDIOCD_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOCD_PLUGIN, RBAudioCdPluginClass))

typedef struct
{
	RBPlugin    parent;

	RBShell    *shell;
	guint       ui_merge_id;

	GHashTable *sources;
	char       *playing_uri;

#if !NAUTILUS_BURN_CHECK_VERSION(2,15,3) && !HAVE_HAL
	GHashTable *cd_drive_mapping;
#endif
} RBAudioCdPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBAudioCdPluginClass;


G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_audiocd_plugin_get_type		(void) G_GNUC_CONST;

static void rb_audiocd_plugin_init (RBAudioCdPlugin *plugin);
static void rb_audiocd_plugin_finalize (GObject *object);
static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);

static void rb_audiocd_plugin_playing_uri_changed_cb (RBShellPlayer *player,
						      const char *uri,
						      RBAudioCdPlugin *plugin);
static RBSource * create_source_cb (RBRemovableMediaManager *rmm,
				    GnomeVFSVolume *volume,
				    RBAudioCdPlugin *plugin);

RB_PLUGIN_REGISTER(RBAudioCdPlugin, rb_audiocd_plugin)

static void
rb_audiocd_plugin_class_init (RBAudioCdPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	object_class->finalize = rb_audiocd_plugin_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;

	RB_PLUGIN_REGISTER_TYPE(rb_audiocd_source);
}

static void
rb_audiocd_plugin_init (RBAudioCdPlugin *plugin)
{
	rb_debug ("RBAudioCdPlugin initialising");

#if NAUTILUS_BURN_CHECK_VERSION(2,15,3)
	nautilus_burn_init ();
#endif
}

static void
rb_audiocd_plugin_finalize (GObject *object)
{
/*
	RBAudioCdPlugin *plugin = RB_AUDIOCD_PLUGIN (object);
*/
	rb_debug ("RBAudioCdPlugin finalising");

#if NAUTILUS_BURN_CHECK_VERSION(2,15,3)
	nautilus_burn_shutdown ();
#endif

	G_OBJECT_CLASS (rb_audiocd_plugin_parent_class)->finalize (object);
}

static void
rb_audiocd_plugin_mount_volume (RBAudioCdPlugin *plugin,
				GnomeVFSVolume  *volume)
{
	RBRemovableMediaManager *rmm = NULL;
	RBSource *source;

	g_object_get (G_OBJECT (plugin->shell),
		      "removable-media-manager", &rmm,
		      NULL);

	rb_debug ("checking audiocd for %s", gnome_vfs_volume_get_device_path (volume));
	source = create_source_cb (rmm, volume, plugin);
	if (source) {
		rb_debug ("creating audio cd source behind RMMs back for %p", volume);
		rb_shell_append_source (plugin->shell, source, NULL);
	}

	g_object_unref (rmm);
}

#if !NAUTILUS_BURN_CHECK_VERSION(2,15,3) && !HAVE_HAL
typedef struct
{
	gboolean           removed;
	gboolean           tray_opened;
	RBAudioCdPlugin   *plugin;
	NautilusBurnDrive *drive;
} RbCdDriveInfo;

static void
rb_audiocd_plugin_unmount_volume (RBAudioCdPlugin *plugin,
				  GnomeVFSVolume  *volume)
{
	RBSource *source;

	source = g_hash_table_lookup (plugin->sources, volume);
	if (source != NULL) {
		rb_source_delete_thyself (source);
	}
}

#ifdef HAVE_BURN_DRIVE_DOOR
static
gboolean poll_tray_opened (RbCdDriveInfo *info)
{
	GnomeVFSVolumeMonitor *monitor =  gnome_vfs_get_volume_monitor ();
	gboolean new_status;
	GnomeVFSVolume *volume;

	if (info->removed) {
		nautilus_burn_drive_unref (info->drive);
		g_free (info);
		return FALSE;
	}

	new_status = nautilus_burn_drive_door_is_open (info->drive);

	if (new_status != info->tray_opened) {
		volume = gnome_vfs_volume_monitor_get_volume_for_path (monitor, info->drive->device);
		rb_debug ("found volume for %s",  info->drive->device);
		if (volume) {
			if (new_status) {
				rb_audiocd_plugin_unmount_volume (info->plugin, volume);
			} else {
				rb_audiocd_plugin_mount_volume (info->plugin, volume);
			}
			gnome_vfs_volume_unref (volume);
		}
	}
	info->tray_opened = new_status;

	return TRUE;
}
#endif

static
void end_cd_drive_monitor (RbCdDriveInfo *info,
			   gpointer       data)
{
	/* this will be freed when the poll next gets called */
	info->removed = TRUE;
}

static
void begin_cd_drive_monitor (NautilusBurnDrive *drive,
			     RBAudioCdPlugin   *plugin)
{
#ifdef HAVE_BURN_DRIVE_DOOR
	RbCdDriveInfo *info = g_new0 (RbCdDriveInfo, 1);
	GnomeVFSVolumeMonitor *monitor=  gnome_vfs_get_volume_monitor ();
	GnomeVFSVolume *volume;

	info->drive = drive;
	info->tray_opened = nautilus_burn_drive_door_is_open (drive);
	info->plugin = plugin;

	g_hash_table_insert (plugin->cd_drive_mapping, drive, info);
	g_timeout_add (1000, (GSourceFunc)poll_tray_opened, info);

	volume = gnome_vfs_volume_monitor_get_volume_for_path (monitor, drive->device);
	rb_debug ("found volume for %s", drive->device);

	if (volume) {
		if (!nautilus_burn_drive_door_is_open (drive)) {
			rb_audiocd_plugin_mount_volume (plugin, volume);
		} else {
			/* it may have got ejected while we weren't monitoring */
			rb_audiocd_plugin_unmount_volume (plugin, volume);
		}
	}
#endif
}

static NautilusBurnDrive *
get_nautilus_burn_drive_for_path (const char *path)
{
#ifdef HAVE_BURN_DRIVE_NEW_FROM_PATH
	return nautilus_burn_drive_new_from_path (path);
#else
	GList *drives, *l;
	NautilusBurnDrive *path_drive = NULL;

	drives = nautilus_burn_drive_get_list (FALSE, FALSE);
	for (l = drives; l != NULL; l = g_list_next (l)) {
		NautilusBurnDrive *drive = (NautilusBurnDrive*)l->data;

		if (path_drive == NULL && strcmp (drive->device, path) == 0) {
			path_drive = drive;
		} else {
			nautilus_burn_drive_unref (drive);
		}
	}
	g_list_free (drives);

	return path_drive;
#endif
}
#endif /* NAUTILUS_BURN < 2.15.3 */

static char *
split_drive_from_cdda_uri (const char *uri)
{
	gchar *copy, *temp, *split;
	int len;

	if (!g_str_has_prefix (uri, "cdda://"))
		return NULL;

	len = strlen ("cdda://");

	copy = g_strdup (uri);
	split = g_utf8_strrchr (copy + len, -1, ':');

	if (split == NULL) {
		/* invalid URI, it doesn't contain a ':' */
		g_free (copy);
		return NULL;
	}

	*split = 0;
	temp = g_strdup (copy + len);
	g_free (copy);

	return temp;
}

static void
rb_audiocd_plugin_playing_uri_changed_cb (RBShellPlayer   *player,
					  const char      *uri,
					  RBAudioCdPlugin *plugin)
{
	char *old_drive = NULL;
	char *new_drive = NULL;

	/* extract the drive paths */
	if (plugin->playing_uri)
		old_drive = split_drive_from_cdda_uri (plugin->playing_uri);

	if (uri != NULL) {
		new_drive = split_drive_from_cdda_uri (uri);
	}

#if !NAUTILUS_BURN_CHECK_VERSION(2,15,3) && !HAVE_HAL
	/* if the drive we're playing from has changed, adjust the polling */
	if (old_drive == NULL || new_drive == NULL || strcmp (old_drive, new_drive) != 0) {
		if (old_drive != NULL) {
			NautilusBurnDrive *drive;

			rb_debug ("restarting monitoring of drive %s after playing", old_drive);
			drive = get_nautilus_burn_drive_for_path (old_drive);
			begin_cd_drive_monitor (drive, plugin);
			nautilus_burn_drive_unref (drive);
		}

		if (new_drive != NULL) {
			NautilusBurnDrive *drive;

			rb_debug ("stopping monitoring of drive %s while playing", new_drive);
			drive = get_nautilus_burn_drive_for_path (new_drive);
			/* removing it from the hash table makes it stop monitoring */
			g_hash_table_remove (plugin->cd_drive_mapping, drive);
			nautilus_burn_drive_unref (drive);
		}
	}
#endif

	g_free (plugin->playing_uri);
	plugin->playing_uri = uri ? g_strdup (uri) : NULL;
}

#if !NAUTILUS_BURN_CHECK_VERSION(2,15,3)
static const char *
nautilus_burn_drive_get_device (NautilusBurnDrive *drive)
{
	g_return_val_if_fail (drive != NULL, NULL);

	return drive->device;
}
#endif

static void
rb_audiocd_plugin_source_deleted (RBAudioCdSource *source,
				  RBAudioCdPlugin *plugin)
{
	GnomeVFSVolume *volume;

	g_object_get (source, "volume", &volume, NULL);
	g_hash_table_remove (plugin->sources, volume);
	g_object_unref (volume);
}

static RBSource *
create_source_cb (RBRemovableMediaManager *rmm,
		  GnomeVFSVolume          *volume,
		  RBAudioCdPlugin         *plugin)
{
	RBSource *source = NULL;

	if (rb_audiocd_is_volume_audiocd (volume)) {
		source = RB_SOURCE (rb_audiocd_source_new (RB_PLUGIN (plugin), plugin->shell, volume));
	}

	if (source != NULL) {
		g_hash_table_insert (plugin->sources, g_object_ref (volume), g_object_ref (source));
		g_signal_connect_object (G_OBJECT (source),
					 "deleted", G_CALLBACK (rb_audiocd_plugin_source_deleted),
					 plugin, 0);
	}

	return source;
}

static void
impl_activate (RBPlugin *plugin,
	       RBShell  *shell)
{
	RBAudioCdPlugin         *pi = RB_AUDIOCD_PLUGIN (plugin);
	RBRemovableMediaManager *rmm;
	gboolean                 scanned;
	GList                   *drives;
	GList                   *it;
	GnomeVFSVolumeMonitor   *monitor;

	pi->sources = g_hash_table_new_full (g_direct_hash,
					     g_direct_equal,
					     g_object_unref,
					     g_object_unref);

	pi->shell = shell;
	g_object_get (G_OBJECT (shell),
		      "removable-media-manager", &rmm,
		      NULL);

	/* watch for new removable media.  use connect_after so	 * plugins for more specific device types can get in first.
	 */
	g_signal_connect_after (G_OBJECT (rmm),
				"create-source", G_CALLBACK (create_source_cb),
				pi);

	/* only scan if we're being loaded after the initial scan has been done */
	g_object_get (G_OBJECT (rmm), "scanned", &scanned, NULL);
	if (scanned) {
		rb_removable_media_manager_scan (rmm);
	}

	g_object_unref (rmm);



	/* monitor the playing song, to disable cd drive polling */
	g_signal_connect_object (rb_shell_get_player (shell), "playing-uri-changed",
				 G_CALLBACK (rb_audiocd_plugin_playing_uri_changed_cb),
				 plugin, 0);

	/*
	 * Monitor all cd drives for inserted audio cds
	 *
	 * This needs to be done seperately from the above, because non-HAL systems don't
	 * (currently) report audio cd insertions as mount events.
	 */
#if !NAUTILUS_BURN_CHECK_VERSION(2,15,3) && !HAVE_HAL
	pi->cd_drive_mapping = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify)end_cd_drive_monitor);
	drives = nautilus_burn_drive_get_list (FALSE, FALSE);
	g_list_foreach (drives, (GFunc)begin_cd_drive_monitor, plugin);
	g_list_free (drives);
#endif

	/* scan cd drives */
#if NAUTILUS_BURN_CHECK_VERSION(2,15,3)
	drives = nautilus_burn_drive_monitor_get_drives (nautilus_burn_get_drive_monitor ());
#else
	drives = nautilus_burn_drive_get_list (FALSE, FALSE);
#endif

	monitor = gnome_vfs_get_volume_monitor ();
	for  (it = drives; it != NULL; it = g_list_next (it)) {
		NautilusBurnDrive *drive = (NautilusBurnDrive *)it->data;
		GnomeVFSVolume    *volume;

		volume = gnome_vfs_volume_monitor_get_volume_for_path (monitor, nautilus_burn_drive_get_device (drive));
		rb_debug ("found volume for %s", nautilus_burn_drive_get_device (drive));
		if (volume != NULL) {
			rb_audiocd_plugin_mount_volume (pi, volume);
			gnome_vfs_volume_unref (volume);
		}
	}
	g_list_free (drives);
}

static void
_delete_cb (GnomeVFSVolume  *volume,
	    RBSource        *source,
	    RBAudioCdPlugin *plugin)
{
	/* block the source deleted handler so we don't modify the hash table
	 * while iterating it.
	 */
	g_signal_handlers_block_by_func (source, rb_audiocd_plugin_source_deleted, plugin);
	rb_source_delete_thyself (source);
}

static void
impl_deactivate	(RBPlugin *bplugin,
		 RBShell  *shell)
{
	RBAudioCdPlugin         *plugin = RB_AUDIOCD_PLUGIN (bplugin);
	RBRemovableMediaManager *rmm = NULL;
	GtkUIManager            *uimanager = NULL;

	g_object_get (G_OBJECT (shell),
		      "removable-media-manager", &rmm,
		      "ui-manager", &uimanager,
		      NULL);
	g_signal_handlers_disconnect_by_func (G_OBJECT (rmm), create_source_cb, plugin);

	g_hash_table_foreach (plugin->sources, (GHFunc)_delete_cb, plugin);
	g_hash_table_destroy (plugin->sources);
	plugin->sources = NULL;
	if (plugin->ui_merge_id) {
		gtk_ui_manager_remove_ui (uimanager, plugin->ui_merge_id);
		plugin->ui_merge_id = 0;
	}

	g_object_unref (G_OBJECT (uimanager));
	g_object_unref (G_OBJECT (rmm));

#if !NAUTILUS_BURN_CHECK_VERSION(2,15,3) && !HAVE_HAL
	g_hash_table_destroy (plugin->cd_drive_mapping);
#endif
}
