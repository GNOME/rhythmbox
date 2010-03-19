/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * rb-audiocd-plugin.c
 *
 * Copyright (C) 2006  James Livingston
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
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

#include <gst/gst.h>

#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-dialog.h"
#include "rb-removable-media-manager.h"
#include "rb-audiocd-source.h"
#include "rb-player.h"
#include "rb-encoder.h"


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
}

static void
rb_audiocd_plugin_finalize (GObject *object)
{
/*
	RBAudioCdPlugin *plugin = RB_AUDIOCD_PLUGIN (object);
*/
	rb_debug ("RBAudioCdPlugin finalising");
	G_OBJECT_CLASS (rb_audiocd_plugin_parent_class)->finalize (object);
}

static void
rb_audiocd_plugin_playing_uri_changed_cb (RBShellPlayer   *player,
					  const char      *uri,
					  RBAudioCdPlugin *plugin)
{
	g_free (plugin->playing_uri);
	plugin->playing_uri = uri ? g_strdup (uri) : NULL;
}

static void
set_source_properties (GstElement *source, const char *uri, gboolean playback_mode)
{
	const char *device;

	if (g_str_has_prefix (uri, "cdda://") == FALSE)
		return;

	device = g_utf8_strrchr (uri, -1, '#');
	if (device != NULL) {
		int track;

		device++;	/* skip the # */
		track = strtoul (uri + 7, NULL, 0);
		g_object_set (source, "device", device, "track", track, NULL);

		if (playback_mode) {
			/* disable paranoia (if using cdparanoiasrc) and set read speed to 1 */
			if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "paranoia-mode"))
				g_object_set (source, "paranoia-mode", 0, NULL);

			if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "read-speed"))
				g_object_set (source, "read-speed", 1, NULL);
		} else {
			/* enable full paranoia; maybe this should be configurable. */
			/* also, sound-juicer defaults to 8 (scratch) not 0xff (full) here.. */
			if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "paranoia-mode"))
				g_object_set (source, "paranoia-mode", 0xff, NULL);
		}
	}
}

static void
rb_audiocd_plugin_prepare_player_source_cb (RBPlayer *player,
					    const char *stream_uri,
					    GstElement *source,
					    RBAudioCdPlugin *plugin)
{
	set_source_properties (source, stream_uri, TRUE);
}

static void
rb_audiocd_plugin_prepare_encoder_source_cb (RBEncoderFactory *factory,
					     const char *stream_uri,
					     GObject *source,
					     RBAudioCdPlugin *plugin)
{
	set_source_properties (GST_ELEMENT (source), stream_uri, FALSE);
}

static gboolean
rb_audiocd_plugin_can_reuse_stream_cb (RBPlayer *player,
				       const char *new_uri,
				       const char *stream_uri,
				       GstElement *stream_bin,
				       RBAudioCdPlugin *plugin)
{
	const char *new_device;
	const char *old_device;

	/* can't do anything with non-cdda URIs */
	if (g_str_has_prefix (new_uri, "cdda://") == FALSE ||
	    g_str_has_prefix (stream_uri, "cdda://") == FALSE) {
		return FALSE;
	}

	/* check the device matches */
	new_device = g_utf8_strrchr (new_uri, -1, '#');
	old_device = g_utf8_strrchr (stream_uri, -1, '#');
	if (new_device == NULL || old_device == NULL || strcmp (old_device, new_device) != 0) {
		return FALSE;
	}

	return TRUE;
}

static void
rb_audiocd_plugin_reuse_stream_cb (RBPlayer *player,
				   const char *new_uri,
				   const char *stream_uri,
				   GstElement *element,
				   RBAudioCdPlugin *plugin)
{
	GstFormat track_format = gst_format_get_by_nick ("track");
	char *track_str;
	char *new_device;
	guint track;
	guint cdda_len;

	/* get the new track number */
	cdda_len = strlen ("cdda://");
	new_device = g_utf8_strrchr (new_uri, -1, '#');
	g_assert (new_device != NULL);
	track_str = g_strndup (new_uri + cdda_len, new_device - (new_uri + cdda_len));
	track = atoi (track_str);
	g_free (track_str);

	rb_debug ("seeking to track %d on CD device %s", track, new_device+1);

	gst_element_seek (element,
			  1.0, track_format, GST_SEEK_FLAG_FLUSH,
			  GST_SEEK_TYPE_SET, track-1,
			  GST_SEEK_TYPE_NONE, -1);
}

static void
rb_audiocd_plugin_source_deleted (RBAudioCdSource *source,
				  RBAudioCdPlugin *plugin)
{
	GVolume *volume;

	g_object_get (source, "volume", &volume, NULL);
	g_hash_table_remove (plugin->sources, volume);
	g_object_unref (volume);
}

static RBSource *
create_source_cb (RBRemovableMediaManager *rmm,
		  GMount                  *mount,
		  MPIDDevice              *device_info,
		  RBAudioCdPlugin         *plugin)
{
	RBSource *source = NULL;
	GVolume *volume = NULL;

	if (rb_audiocd_is_mount_audiocd (mount)) {

		volume = g_mount_get_volume (mount);
		if (volume != NULL) {
			source = rb_audiocd_source_new (RB_PLUGIN (plugin), plugin->shell, volume);
			g_object_unref (volume);
		}
	}

	if (source != NULL) {
		g_hash_table_insert (plugin->sources, g_object_ref (volume), g_object_ref (source));
		g_signal_connect_object (G_OBJECT (source),
					 "deleted", G_CALLBACK (rb_audiocd_plugin_source_deleted),
					 plugin, 0);

		if (plugin->ui_merge_id == 0) {
			char *filename;
			GtkUIManager *uimanager;

			g_object_get (plugin->shell, "ui-manager", &uimanager, NULL);

			filename = rb_plugin_find_file (RB_PLUGIN (plugin), "audiocd-ui.xml");
			if (filename != NULL) {
				plugin->ui_merge_id = gtk_ui_manager_add_ui_from_file (uimanager, filename, NULL);
				gtk_ui_manager_ensure_update (uimanager);
			} else {
				g_warning ("Unable to find file: audiocd-ui.xml");
			}

			g_free (filename);
			g_object_unref (uimanager);
		}
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
	GObject                 *shell_player;
	RBPlayer                *player_backend;

	pi->sources = g_hash_table_new_full (g_direct_hash,
					     g_direct_equal,
					     g_object_unref,
					     g_object_unref);

	pi->shell = shell;
	g_object_get (shell, "removable-media-manager", &rmm, NULL);


	/* watch for new removable media.  use connect_after so
	 * plugins for more specific device types can get in first.
	 */
	g_signal_connect_after (rmm,
				"create-source-mount", G_CALLBACK (create_source_cb),
				pi);

	/* only scan if we're being loaded after the initial scan has been done */
	g_object_get (G_OBJECT (rmm), "scanned", &scanned, NULL);
	if (scanned) {
		rb_removable_media_manager_scan (rmm);
	}

	g_object_unref (rmm);

	/* player backend hooks: specify the device, limit read speed, and disable paranoia
	 * in source elements, and when changing between tracks on the same CD, just seek
	 * between them, rather than closing and reopening the device.
	 */
	shell_player = rb_shell_get_player (shell);
	g_object_get (shell_player, "player", &player_backend, NULL);
	if (player_backend) {
		GObjectClass *klass = G_OBJECT_GET_CLASS (player_backend);
		if (g_signal_lookup ("prepare-source", G_OBJECT_CLASS_TYPE (klass)) != 0) {
			g_signal_connect_object (player_backend,
						 "prepare-source",
						 G_CALLBACK (rb_audiocd_plugin_prepare_player_source_cb),
						 plugin, 0);
		}
		if (g_signal_lookup ("reuse-stream", G_OBJECT_CLASS_TYPE (klass)) != 0) {
			g_signal_connect_object (player_backend,
						 "can-reuse-stream",
						 G_CALLBACK (rb_audiocd_plugin_can_reuse_stream_cb),
						 plugin, 0);
			g_signal_connect_object (player_backend,
						 "reuse-stream",
						 G_CALLBACK (rb_audiocd_plugin_reuse_stream_cb),
						 plugin, 0);
		}
	}

	/* encoder hooks: specify the device and set the paranoia level (if available) on
	 * source elements.
	 */
	g_signal_connect_object (rb_encoder_factory_get (),
				 "prepare-source",
				 G_CALLBACK (rb_audiocd_plugin_prepare_encoder_source_cb),
				 plugin, 0);

	g_signal_connect_object (shell_player, "playing-uri-changed",
				 G_CALLBACK (rb_audiocd_plugin_playing_uri_changed_cb),
				 plugin, 0);
}

static void
_delete_cb (GVolume         *volume,
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
	g_signal_handlers_disconnect_by_func (rmm, create_source_cb, plugin);

	g_hash_table_foreach (plugin->sources, (GHFunc)_delete_cb, plugin);
	g_hash_table_destroy (plugin->sources);
	plugin->sources = NULL;
	if (plugin->ui_merge_id) {
		gtk_ui_manager_remove_ui (uimanager, plugin->ui_merge_id);
		plugin->ui_merge_id = 0;
	}

	g_object_unref (uimanager);
	g_object_unref (rmm);
}
