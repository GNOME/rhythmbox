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

#include "config.h"

#include <string.h> /* For strlen */

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>

#include <gst/gst.h>

#include "rb-plugin-macros.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-dialog.h"
#include "rb-removable-media-manager.h"
#include "rb-audiocd-source.h"
#include "rb-player.h"
#include "rb-encoder.h"
#include "rb-file-helpers.h"


#define RB_TYPE_AUDIOCD_PLUGIN		(rb_audiocd_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBAudioCdPlugin, rb_audiocd_plugin, RB, AUDIOCD_PLUGIN, PeasExtensionBase)

struct _RBAudioCdPlugin
{
	PeasExtensionBase parent;

	RBShell    *shell;

	GHashTable *sources;
	char       *playing_uri;
};

struct _RBAudioCdPluginClass
{
	PeasExtensionBaseClass parent_class;
};


G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);
GType	rb_audiocd_plugin_get_type		(void) G_GNUC_CONST;

static void rb_audiocd_plugin_init (RBAudioCdPlugin *plugin);

RB_DEFINE_PLUGIN(RB_TYPE_AUDIOCD_PLUGIN, RBAudioCdPlugin, rb_audiocd_plugin,)

static void
rb_audiocd_plugin_init (RBAudioCdPlugin *plugin)
{
	rb_debug ("RBAudioCdPlugin initialising");
}

static gboolean
parse_cdda_uri (const char *uri, char **device, gulong *track)
{
	const char *fragment;
	const char *device_name;

	if (g_str_has_prefix (uri, "cdda://") == FALSE)
		return FALSE;

	fragment = g_utf8_strrchr (uri, -1, '#');
	if (fragment == NULL)
		return FALSE;

	if (track != NULL) {
		*track = strtoul (fragment + 1, NULL, 0);
	}

	if (device != NULL) {
		device_name = uri + strlen("cdda://");
		*device = g_malloc0 ((fragment - device_name) + 1);
		memcpy (*device, device_name, (fragment - device_name));
	}
	return TRUE;
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
	g_return_if_fail (GST_IS_URI_HANDLER (source));
	gst_uri_handler_set_uri (GST_URI_HANDLER (source), uri, NULL);

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

		/* trick cdparanoiasrc into resetting the device speed in case we've
		 * previously set it to 1 for playback
		 */
		if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "read-speed"))
			g_object_set (source, "read-speed", 0xffff, NULL);
	}
}

static void
rb_audiocd_plugin_prepare_player_source_cb (RBPlayer *player,
					    const char *stream_uri,
					    GstElement *source,
					    RBAudioCdPlugin *plugin)
{
	if (g_str_has_prefix (stream_uri, "cdda://") == FALSE)
		return;

	set_source_properties (source, stream_uri, TRUE);
}

static void
rb_audiocd_plugin_prepare_encoder_source_cb (RBEncoderFactory *factory,
					     const char *stream_uri,
					     GObject *source,
					     RBAudioCdPlugin *plugin)
{
	if (g_str_has_prefix (stream_uri, "cdda://") == FALSE)
		return;

	set_source_properties (GST_ELEMENT (source), stream_uri, FALSE);
}

static gboolean
rb_audiocd_plugin_can_reuse_stream_cb (RBPlayer *player,
				       const char *new_uri,
				       const char *stream_uri,
				       GstElement *stream_bin,
				       RBAudioCdPlugin *plugin)
{
	char *new_device = NULL;
	char *old_device = NULL;
	gboolean result = FALSE;

	if (parse_cdda_uri (new_uri, &new_device, NULL) &&
	    parse_cdda_uri (stream_uri, &old_device, NULL)) {
		result = (g_strcmp0 (old_device, new_device) == 0);
	}

	g_free (new_device);
	g_free (old_device);
	return result;
}

static void
rb_audiocd_plugin_reuse_stream_cb (RBPlayer *player,
				   const char *new_uri,
				   const char *stream_uri,
				   GstElement *element,
				   RBAudioCdPlugin *plugin)
{
	GstFormat track_format = gst_format_get_by_nick ("track");
	gulong track;
	char *device = NULL;

	if (parse_cdda_uri (new_uri, &device, &track) == FALSE) {
		g_assert_not_reached ();
	}

	rb_debug ("seeking to track %lu on CD device %s", track, device);
	g_free (device);

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
	RBShell *shell;

	g_object_get (plugin, "object", &shell, NULL);

	if (rb_audiocd_is_mount_audiocd (mount)) {

		volume = g_mount_get_volume (mount);
		if (volume != NULL) {
			source = rb_audiocd_source_new (G_OBJECT (plugin), shell, volume);
			g_object_unref (volume);
		}
	}

	if (source != NULL) {
		g_hash_table_insert (plugin->sources, g_object_ref (volume), g_object_ref (source));
		g_signal_connect_object (G_OBJECT (source),
					 "deleted", G_CALLBACK (rb_audiocd_plugin_source_deleted),
					 plugin, 0);
	}

	g_object_unref (shell);
	return source;
}

static void
impl_activate (PeasActivatable *plugin)
{
	RBAudioCdPlugin         *pi = RB_AUDIOCD_PLUGIN (plugin);
	RBRemovableMediaManager *rmm;
	gboolean                 scanned;
	GObject                 *shell_player;
	RBPlayer                *player_backend;
	RBShell                 *shell;

	pi->sources = g_hash_table_new_full (g_direct_hash,
					     g_direct_equal,
					     g_object_unref,
					     g_object_unref);

	g_object_get (plugin, "object", &shell, NULL);
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
	g_object_get (shell, "shell-player", &shell_player, NULL);
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
	g_object_unref (shell_player);

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

	g_object_unref (shell);
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
	rb_display_page_delete_thyself (RB_DISPLAY_PAGE (source));
}

static void
impl_deactivate	(PeasActivatable *bplugin)
{
	RBAudioCdPlugin         *plugin = RB_AUDIOCD_PLUGIN (bplugin);
	RBRemovableMediaManager *rmm = NULL;
	RBShell                 *shell;

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell,
		      "removable-media-manager", &rmm,
		      NULL);
	g_signal_handlers_disconnect_by_func (rmm, create_source_cb, plugin);

	g_hash_table_foreach (plugin->sources, (GHFunc)_delete_cb, plugin);
	g_hash_table_destroy (plugin->sources);
	plugin->sources = NULL;

	g_object_unref (rmm);
	g_object_unref (shell);
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_audiocd_plugin_register_type (G_TYPE_MODULE (module));
	_rb_audiocd_source_register_type (G_TYPE_MODULE (module));
	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_AUDIOCD_PLUGIN);
}
