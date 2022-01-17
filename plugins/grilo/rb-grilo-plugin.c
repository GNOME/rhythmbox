/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Jonathan Matthew
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

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>

#include <grilo.h>

#include "rb-plugin-macros.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-grilo-source.h"
#include "rb-display-page-group.h"
#include "rb-ext-db.h"

#define RB_TYPE_GRILO_PLUGIN		(rb_grilo_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBGriloPlugin, rb_grilo_plugin, RB, GRILO_PLUGIN, PeasExtensionBase)

static const char *ignored_plugins[] = {
	"grl-bookmarks",
	"grl-dmap",
	"grl-filesystem",
	"grl-magnatune",
	"grl-optical-media",
	"grl-podcasts",
	"grl-tracker"
};

struct _RBGriloPlugin
{
	PeasExtensionBase parent;

	GrlRegistry *registry;
	GHashTable *sources;
	RBShellPlayer *shell_player;
	gulong emit_cover_art_id;
	RBExtDB *art_store;
	gulong handler_id_source_added;
	gulong handler_id_source_removed;
};

struct _RBGriloPluginClass
{
	PeasExtensionBaseClass parent_class;
};

G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);
GType	rb_grilo_plugin_get_type		(void) G_GNUC_CONST;

static void rb_grilo_plugin_init (RBGriloPlugin *plugin);

RB_DEFINE_PLUGIN(RB_TYPE_GRILO_PLUGIN, RBGriloPlugin, rb_grilo_plugin,)

static void
rb_grilo_plugin_init (RBGriloPlugin *plugin)
{
	rb_debug ("RBGriloPlugin initialising");
}

static void
grilo_source_added_cb (GrlRegistry *registry, GrlSource *grilo_source, RBGriloPlugin *plugin)
{
	GrlPlugin *grilo_plugin;
	GrlSupportedOps ops;
	const GList *keys;
	RBSource *source;
	RBShell *shell;
	int i;

	if (!(grl_source_get_supported_media (grilo_source) & GRL_MEDIA_TYPE_AUDIO)) {
		rb_debug ("grilo source %s doesn't support audio",
			  grl_source_get_name (grilo_source));
		goto ignore;
	}

	grilo_plugin = grl_source_get_plugin (grilo_source);
	for (i = 0; i < G_N_ELEMENTS (ignored_plugins); i++) {
		if (g_str_equal (ignored_plugins[i], grl_plugin_get_id (grilo_plugin))) {
			rb_debug ("grilo source %s is blacklisted",
				  grl_source_get_name (grilo_source));
			goto ignore;
		}
	}

	ops = grl_source_supported_operations (grilo_source);
	if (((ops & GRL_OP_BROWSE) == 0) && ((ops & GRL_OP_SEARCH) == 0)) {
		rb_debug ("grilo source %s is not interesting",
			  grl_source_get_name (grilo_source));
		goto ignore;
	}

	keys = grl_source_supported_keys (grilo_source);
	if (g_list_find ((GList *)keys, GINT_TO_POINTER (GRL_METADATA_KEY_URL)) == NULL) {
		rb_debug ("grilo source %s doesn't do urls", grl_source_get_name (grilo_source));
		goto ignore;
	}

	rb_debug ("new grilo source: %s", grl_source_get_name (grilo_source));

	source = rb_grilo_source_new (G_OBJECT (plugin), grilo_source);
	g_hash_table_insert (plugin->sources, g_object_ref (grilo_source), g_object_ref_sink (source));

	/* probably put some sources under 'shared', some under 'stores'? */
	g_object_get (plugin, "object", &shell, NULL);
	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (source), RB_DISPLAY_PAGE_GROUP_SHARED);
	g_object_unref (shell);

	return;

ignore:
	grl_registry_unregister_source (registry, grilo_source, NULL);
}

static void
grilo_source_removed_cb (GrlRegistry *registry, GrlSource *grilo_source, RBGriloPlugin *plugin)
{
	RBSource *source;

	source = g_hash_table_lookup (plugin->sources, grilo_source);

	if (source) {
		rb_display_page_delete_thyself (RB_DISPLAY_PAGE (source));
		g_hash_table_remove (plugin->sources, grilo_source);
	}
}

static void
playing_song_changed_cb (RBShellPlayer *player, RhythmDBEntry *entry, RBGriloPlugin *plugin)
{
	const char *uri;
	RhythmDBEntryType *entry_type;
	RBGriloEntryData *data;

	if (entry == NULL)
		return;

	entry_type = rhythmdb_entry_get_entry_type (entry);
	if (RB_IS_GRILO_ENTRY_TYPE (entry_type) == FALSE) {
		return;
	}

	data = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RBGriloEntryData);
	uri = grl_data_get_string (data->grilo_data, GRL_METADATA_KEY_THUMBNAIL);
	if (uri != NULL) {
		RBExtDBKey *key;

		key = rb_ext_db_key_create_storage ("album", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM));
		rb_ext_db_key_add_field (key, "artist", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST));

		rb_ext_db_store_uri (plugin->art_store,
				     key,
				     RB_EXT_DB_SOURCE_SEARCH,
				     uri);
		rb_ext_db_key_free (key);
	}
}

static void
impl_activate (PeasActivatable *plugin)
{
	RBGriloPlugin *pi = RB_GRILO_PLUGIN (plugin);
	GError *error = NULL;
	RBShell *shell;

	pi->sources = g_hash_table_new_full (g_direct_hash,
					     g_direct_equal,
					     g_object_unref,
					     g_object_unref);

	grl_init (0, NULL);
	pi->registry = grl_registry_get_default ();
	pi->handler_id_source_added =
		g_signal_connect (pi->registry, "source-added", G_CALLBACK (grilo_source_added_cb), pi);
	pi->handler_id_source_removed =
		g_signal_connect (pi->registry, "source-removed", G_CALLBACK (grilo_source_removed_cb), pi);
	if (grl_registry_load_all_plugins (pi->registry, TRUE, &error) == FALSE) {
		g_warning ("Failed to load Grilo plugins: %s", error->message);
		g_clear_error (&error);
	}

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell, "shell-player", &pi->shell_player, NULL);
	g_object_unref (shell);

	g_signal_connect (pi->shell_player, "playing-song-changed", G_CALLBACK (playing_song_changed_cb), pi);

	pi->art_store = rb_ext_db_new ("album-art");
}

static void
impl_deactivate	(PeasActivatable *bplugin)
{
	RBGriloPlugin *plugin = RB_GRILO_PLUGIN (bplugin);
	GHashTableIter iter;
	gpointer key, value;

	g_signal_handler_disconnect (plugin->registry, plugin->handler_id_source_added);
	g_signal_handler_disconnect (plugin->registry, plugin->handler_id_source_removed);

	g_hash_table_iter_init (&iter, plugin->sources);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		grl_registry_unregister_source (plugin->registry, GRL_SOURCE (key), NULL);
		rb_display_page_delete_thyself (RB_DISPLAY_PAGE (value));
	}
	g_hash_table_destroy (plugin->sources);
	plugin->sources = NULL;
	plugin->registry = NULL;

	if (plugin->emit_cover_art_id != 0) {
		g_source_remove (plugin->emit_cover_art_id);
		plugin->emit_cover_art_id = 0;
	}
	g_signal_handlers_disconnect_by_func (plugin->shell_player, G_CALLBACK (playing_song_changed_cb), plugin);
	g_object_unref (plugin->shell_player);
	plugin->shell_player = NULL;

	g_object_unref (plugin->art_store);
	plugin->art_store = NULL;
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_grilo_plugin_register_type (G_TYPE_MODULE (module));
	_rb_grilo_source_register_type (G_TYPE_MODULE (module));
	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_GRILO_PLUGIN);
}
