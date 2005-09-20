/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  arch-tag: Implementation of Rhythmbox removable media manager
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

#include <config.h>
#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs.h>
#include <string.h>

#include "rb-removable-media-manager.h"
#include "rb-sourcelist.h"
#include "rb-removable-media-source.h"
#ifdef WITH_IPOD_SUPPORT
#include "rb-ipod-source.h"
#endif

#include "rb-debug.h"
#include "rb-dialog.h"
#include "rhythmdb.h"

static void rb_removable_media_manager_class_init (RBRemovableMediaManagerClass *klass);
static void rb_removable_media_manager_init (RBRemovableMediaManager *mgr);
static void rb_removable_media_manager_dispose (GObject *object);
static void rb_removable_media_manager_set_property (GObject *object,
					      guint prop_id,
					      const GValue *value,
					      GParamSpec *pspec);
static void rb_removable_media_manager_get_property (GObject *object,
					      guint prop_id,
					      GValue *value,
					      GParamSpec *pspec);

static void rb_removable_media_manager_cmd_eject_medium (GtkAction *action,
					       RBRemovableMediaManager *mgr);
static void rb_removable_media_manager_set_uimanager (RBRemovableMediaManager *mgr, 
					     GtkUIManager *uimanager);

static void rb_removable_media_manager_append_media_source (RBRemovableMediaManager *mgr, RBRemovableMediaSource *source);

static void rb_removable_media_manager_mount_volume (RBRemovableMediaManager *mgr,
				GnomeVFSVolume *volume);
static void rb_removable_media_manager_unmount_volume (RBRemovableMediaManager *mgr,
				GnomeVFSVolume *volume);

static void  rb_removable_media_manager_volume_mounted_cb (GnomeVFSVolumeMonitor *monitor,
				GnomeVFSVolume *volume, 
				gpointer data);
static void  rb_removable_media_manager_volume_unmounted_cb (GnomeVFSVolumeMonitor *monitor,
				GnomeVFSVolume *volume, 
				gpointer data);
typedef struct
{
	RBShell *shell;
	gboolean disposed;

	RBSourceList *sourcelist;
	RBSource *selected_source;

	GtkActionGroup *actiongroup;
	GtkUIManager *uimanager;

	GList *sources;
	GHashTable *volume_mapping;
} RBRemovableMediaManagerPrivate;

G_DEFINE_TYPE (RBRemovableMediaManager, rb_removable_media_manager, G_TYPE_OBJECT)
#define REMOVABLE_MEDIA_MANAGER_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_REMOVABLE_MEDIA_MANAGER, RBRemovableMediaManagerPrivate))

enum
{
	PROP_0,
	PROP_SHELL,
	PROP_SOURCELIST,
	PROP_SOURCE,
};

enum
{
	MEDIUM_ADDED,
	LAST_SIGNAL
};

static guint rb_removable_media_manager_signals[LAST_SIGNAL] = { 0 };

static GtkActionEntry rb_removable_media_manager_actions [] =
{
	{ "RemovableSourceEject", NULL, N_("_Eject"), NULL,
	  N_("Eject this medium"),
	  G_CALLBACK (rb_removable_media_manager_cmd_eject_medium) },
};
static guint rb_removable_media_manager_n_actions = G_N_ELEMENTS (rb_removable_media_manager_actions);


static void
rb_removable_media_manager_class_init (RBRemovableMediaManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_removable_media_manager_dispose;

	object_class->set_property = rb_removable_media_manager_set_property;
	object_class->get_property = rb_removable_media_manager_get_property;

	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "RBSource",
							      "RBSource object",
							      RB_TYPE_SOURCE,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_SHELL,
					 g_param_spec_object ("shell",
							      "RBShell",
							      "RBShell object",
							      RB_TYPE_SHELL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_SOURCELIST,
					 g_param_spec_object ("sourcelist",
							      "RBSourceList",
							      "RBSourceList",
							      RB_TYPE_SOURCELIST,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	rb_removable_media_manager_signals[MEDIUM_ADDED] =
		g_signal_new ("medium_added",
			      RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRemovableMediaManagerClass, medium_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, G_TYPE_OBJECT);

	g_type_class_add_private (klass, sizeof (RBRemovableMediaManagerPrivate));
}

static void
rb_removable_media_manager_init (RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);

	priv->volume_mapping = g_hash_table_new (NULL, NULL);
}

static void
rb_removable_media_manager_dispose (GObject *object)
{
	RBRemovableMediaManager *mgr = RB_REMOVABLE_MEDIA_MANAGER (object);
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);

	if (!priv->disposed)
	{
		GnomeVFSVolumeMonitor *monitor = gnome_vfs_get_volume_monitor ();

		g_signal_handlers_disconnect_by_func (G_OBJECT (monitor), 
						      G_CALLBACK (rb_removable_media_manager_volume_mounted_cb), 
						      mgr);
		g_signal_handlers_disconnect_by_func (G_OBJECT (monitor), 
						      G_CALLBACK (rb_removable_media_manager_volume_unmounted_cb), 
						      mgr);
	}

	if (priv->sources) {
		g_list_free (priv->sources);
		priv->sources = NULL;
	}

	if (priv->volume_mapping) {
		g_hash_table_destroy (priv->volume_mapping);
		priv->volume_mapping = NULL;
	}

	priv->disposed = TRUE;

	G_OBJECT_CLASS (rb_removable_media_manager_parent_class)->dispose (object);
}

static void
rb_removable_media_manager_set_property (GObject *object,
				  guint prop_id,
				  const GValue *value,
				  GParamSpec *pspec)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
	{
		priv->selected_source = g_value_get_object (value);
		break;
	}
	case PROP_SHELL:
	{
		GtkUIManager *uimanager;

		priv->shell = g_value_get_object (value);
		g_object_get (G_OBJECT (priv->shell), 
			      "ui-manager", &uimanager, 
			      NULL);
		rb_removable_media_manager_set_uimanager (RB_REMOVABLE_MEDIA_MANAGER (object), uimanager);
		break;
	}
	case PROP_SOURCELIST:
		priv->sourcelist = g_value_get_object (value);
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
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
		g_value_set_object (value, priv->selected_source);
		break;
	case PROP_SHELL:
		g_value_set_object (value, priv->shell);
		break;
	case PROP_SOURCELIST:
		g_value_set_object (value, priv->sourcelist);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBRemovableMediaManager *
rb_removable_media_manager_new (RBShell *shell,
				 RBSourceList *sourcelist)
{
	return g_object_new (RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			     "shell", shell,
			     "sourcelist", sourcelist,
			     NULL);
}

gboolean
rb_removable_media_manager_load_media (RBRemovableMediaManager *mgr)
{
	GnomeVFSVolumeMonitor *monitor = gnome_vfs_get_volume_monitor ();
	GList *volumes;
	GList *it;

	/* Look for already inserted media */
	volumes = gnome_vfs_volume_monitor_get_mounted_volumes (monitor);
	for (it = volumes; it != NULL; it = it->next) {
		GnomeVFSVolume *volume = GNOME_VFS_VOLUME (it->data);

		rb_removable_media_manager_mount_volume (mgr, volume);
		gnome_vfs_volume_unref (volume);
	}

	g_list_free (volumes);

	/*
	 * Monitor new (un)mounted file systems to look for new media
	 *
	 * both pre-unmount and unmounted callbacks are registered because it is
	 * better to do it before the unmount, but sometimes we don't get those
	 * (e.g. someone pressing the eject button on a cd drive). If we get the
	 * pre-unmount signal, the corrosponding unmounted signal is ignored
	 */
	g_signal_connect (G_OBJECT (monitor), "volume-mounted", 
			  G_CALLBACK (rb_removable_media_manager_volume_mounted_cb), 
			  mgr);
	g_signal_connect (G_OBJECT (monitor), "volume-pre-unmount", 
			  G_CALLBACK (rb_removable_media_manager_volume_unmounted_cb), 
			  mgr);
	g_signal_connect (G_OBJECT (monitor), "volume-unmounted", 
			  G_CALLBACK (rb_removable_media_manager_volume_unmounted_cb), 
			  mgr);
	return FALSE;
}

static void 
rb_removable_media_manager_volume_mounted_cb (GnomeVFSVolumeMonitor *monitor,
			   GnomeVFSVolume *volume, 
			   gpointer data)
{
	RBRemovableMediaManager *mgr = RB_REMOVABLE_MEDIA_MANAGER (data);

	rb_removable_media_manager_mount_volume (mgr, volume);
}


static void
rb_removable_media_manager_volume_unmounted_cb (GnomeVFSVolumeMonitor *monitor,
			     GnomeVFSVolume *volume, 
			     gpointer data)
{
	RBRemovableMediaManager *mgr = RB_REMOVABLE_MEDIA_MANAGER (data);

	g_assert (volume != NULL);
	rb_removable_media_manager_unmount_volume (mgr, volume);
}


static void
rb_removable_media_manager_mount_volume (RBRemovableMediaManager *mgr, GnomeVFSVolume *volume)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);
	RBRemovableMediaSource *source = NULL;
	RBShell *shell;
	char *fs_type, *device_path, *display_name, *hal_udi, *icon_name;

	g_object_get (G_OBJECT (mgr), "shell", &shell, NULL);

	fs_type = gnome_vfs_volume_get_filesystem_type (volume);
	device_path = gnome_vfs_volume_get_device_path (volume);
	display_name = gnome_vfs_volume_get_display_name (volume);
	hal_udi = gnome_vfs_volume_get_hal_udi (volume);
	icon_name = gnome_vfs_volume_get_icon (volume);
	rb_debug ("detecting new media - device_type=%d", gnome_vfs_volume_get_device_type (volume));
	rb_debug ("detecting new media - volumd_type=%d", gnome_vfs_volume_get_volume_type (volume));
	rb_debug ("detecting new media - fs type=%s", fs_type);
	rb_debug ("detecting new media - device path=%s", device_path);
	rb_debug ("detecting new media - display name=%s", display_name);
	rb_debug ("detecting new media - hal udi=%s", hal_udi);
	rb_debug ("detecting new media - icon=%s", icon_name);

	/* rb_xxx_source_new first checks if the 'volume' parameter corresponds
	 * to a medium of type 'xxx', and returns NULL if it doesn't.
	 * When volume is of the appropriate type, it creates a new source
	 * to handle this volume
	 */
#ifdef WITH_IPOD_SUPPORT
	if (source == NULL && rb_ipod_is_volume_ipod (volume))
		source = rb_ipod_source_new (shell, volume);
#endif

	if (source) {
		g_hash_table_insert (priv->volume_mapping, volume, source);
		rb_removable_media_manager_append_media_source (mgr, source);
	} else
		rb_debug ("Unhanded media");

	g_free (fs_type);
	g_free (device_path);
	g_free (display_name);
	g_free (hal_udi);
	g_free (icon_name);
	g_object_unref (G_OBJECT (shell));
}

static void
rb_removable_media_manager_unmount_volume (RBRemovableMediaManager *mgr, GnomeVFSVolume *volume)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);
	RBRemovableMediaSource *source;

	g_assert (volume != NULL);

	rb_debug ("media removed");
	source = g_hash_table_lookup (priv->volume_mapping, volume);
	if (source) {
		rb_source_delete_thyself (RB_SOURCE (source));
		g_hash_table_remove (priv->volume_mapping, volume);
	}
}

static void
rb_removable_media_manager_source_deleted_cb (RBSource *source, RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);

	rb_debug ("removing source %p", source);
	priv->sources = g_list_remove (priv->sources, source);
}

static void
rb_removable_media_manager_append_media_source (RBRemovableMediaManager *mgr, RBRemovableMediaSource *source)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);

	priv->sources = g_list_append (priv->sources, source);
	g_signal_connect_object (G_OBJECT (source), "deleted",
				 G_CALLBACK (rb_removable_media_manager_source_deleted_cb), mgr, 0);
	g_signal_emit (G_OBJECT (mgr), rb_removable_media_manager_signals[MEDIUM_ADDED], 0,
		       source);
}

static void
rb_removable_media_manager_set_uimanager (RBRemovableMediaManager *mgr, 
					  GtkUIManager *uimanager)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);

	if (priv->uimanager != NULL) {
		if (priv->actiongroup != NULL) {
			gtk_ui_manager_remove_action_group (priv->uimanager,
							    priv->actiongroup);
		}
		g_object_unref (G_OBJECT (priv->uimanager));
		priv->uimanager = NULL;
	}

	priv->uimanager = uimanager;

	if (priv->actiongroup == NULL) {
		priv->actiongroup = gtk_action_group_new ("RemovableMediaActions");
		gtk_action_group_add_actions (priv->actiongroup,
					      rb_removable_media_manager_actions,
					      rb_removable_media_manager_n_actions,
					      mgr);
	}

	gtk_ui_manager_insert_action_group (priv->uimanager,
					    priv->actiongroup,
					    0);
}

static void
rb_removable_media_manager_eject_medium_cb (gboolean succeeded,
					   const char *error,
					   const char *detailed_error,
					   gpointer *data)
{
	if (succeeded)
		return;

	rb_error_dialog (NULL, error, detailed_error);
}

static void
rb_removable_media_manager_cmd_eject_medium (GtkAction *action, RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);
	RBRemovableMediaSource *source = RB_REMOVABLE_MEDIA_SOURCE (priv->selected_source);
	GnomeVFSVolume *volume;

	g_object_get (G_OBJECT (source), "volume", &volume, NULL);
	gnome_vfs_volume_eject (volume, (GnomeVFSVolumeOpCallback)rb_removable_media_manager_eject_medium_cb, mgr);
	gnome_vfs_volume_unref (volume);
}
