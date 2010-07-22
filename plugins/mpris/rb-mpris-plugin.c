/*
 * rb-mpris-plugin.c
 *
 *  Copyright (C) 2010  Jonathan Matthew  <jonathan@d14n.org>
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

#include <config.h>

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <lib/rb-util.h>
#include <lib/rb-debug.h>
#include <shell/rb-plugin.h>
#include <shell/rb-shell.h>
#include <shell/rb-shell-player.h>

#define RB_TYPE_MPRIS_PLUGIN		(rb_mpris_plugin_get_type ())
#define RB_MPRIS_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_MPRIS_PLUGIN, RBMprisPlugin))
#define RB_MPRIS_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_MPRIS_PLUGIN, RBMprisPluginClass))
#define RB_IS_MPRIS_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_MPRIS_PLUGIN))
#define RB_IS_MPRIS_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_MPRIS_PLUGIN))
#define RB_MPRIS_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_MPRIS_PLUGIN, RBMprisPluginClass))

#include "mpris-spec.h"

typedef struct
{
	RBPlugin parent;

	guint name_own_id;

	GDBusConnection *connection;
	guint root_id;
	guint tracklist_id;
	guint player_id;

	RBShell *shell;
	RBShellPlayer *player;
	RhythmDB *db;

} RBMprisPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBMprisPluginClass;


G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_mpris_plugin_get_type		(void) G_GNUC_CONST;

RB_PLUGIN_REGISTER(RBMprisPlugin, rb_mpris_plugin)
#define RB_MPRIS_PLUGIN_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), RB_TYPE_MPRIS_PLUGIN, RBMprisPluginPrivate))


static void
rb_mpris_plugin_init (RBMprisPlugin *plugin)
{
}

/* MPRIS root object */

static void
handle_root_method_call (GDBusConnection *connection,
			 const char *sender,
			 const char *object_path,
			 const char *interface_name,
			 const char *method_name,
			 GVariant *parameters,
			 GDBusMethodInvocation *invocation,
			 RBMprisPlugin *plugin)
{
	if (g_strcmp0 (object_path, "/") != 0 ||
	    g_strcmp0 (interface_name, mpris_iface_name) != 0) {
		rb_debug ("?!");
		return;
	}

	if (g_strcmp0 (method_name, "Identity") == 0) {
		g_dbus_method_invocation_return_value (invocation,
						       g_variant_new ("(s)", "Rhythmbox " VERSION));
	} else if (g_strcmp0 (method_name, "Quit") == 0) {
		rb_shell_quit (plugin->shell, NULL);
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else if (g_strcmp0 (method_name, "MprisVersion") == 0) {
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("((qq))", 1, 0));	/* what is the version number, anyway? */
	}
}

static const GDBusInterfaceVTable root_vtable =
{
	(GDBusInterfaceMethodCallFunc) handle_root_method_call,
	NULL,
	NULL
};

/* MPRIS tracklist object (not implemented) */

static void
handle_tracklist_call (GDBusConnection *connection,
		       const char *sender,
		       const char *object_path,
		       const char *interface_name,
		       const char *method_name,
		       GVariant *parameters,
		       GDBusMethodInvocation *invocation,
		       RBMprisPlugin *plugin)
{
	/* do nothing */
}

static const GDBusInterfaceVTable tracklist_vtable =
{
	(GDBusInterfaceMethodCallFunc) handle_tracklist_call,
	NULL,
	NULL
};


/* MPRIS player object */

static void
handle_result (GDBusMethodInvocation *invocation, gboolean ret, GError *error)
{
	if (ret) {
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else {
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);
	}
}

static void
add_string_property (GVariantBuilder *builder,
		     RhythmDBEntry *entry,
		     RhythmDBPropType prop,
		     const char *name)
{
	rb_debug ("adding %s = %s", name, rhythmdb_entry_get_string (entry, prop));
	g_variant_builder_add (builder,
			       "{sv}",
			       name,
			       g_variant_new ("s", rhythmdb_entry_get_string (entry, prop)));
}

static void
add_string_property_2 (GVariantBuilder *builder,
		       RhythmDB *db,
		       RhythmDBEntry *entry,
		       RhythmDBPropType prop,
		       const char *name,
		       const char *extra_field_name)
{
	GValue *v;
	const char *value;

	v = rhythmdb_entry_request_extra_metadata (db, entry, extra_field_name);
	if (v != NULL) {
		value = g_value_get_string (v);
	} else {
		value = rhythmdb_entry_get_string (entry, prop);
	}

	rb_debug ("adding %s = %s", name, value);
	g_variant_builder_add (builder, "{sv}", name, g_variant_new ("s", value));

	if (v != NULL) {
		g_value_unset (v);
		g_free (v);
	}

}

static void
add_ulong_property (GVariantBuilder *builder,
		    RhythmDBEntry *entry,
		    RhythmDBPropType prop,
		    const char *name)
{
	ulong v;
	v = rhythmdb_entry_get_ulong (entry, prop);
	rb_debug ("adding %s = %lu", name, v);
	g_variant_builder_add (builder,
			       "{sv}",
			       name,
			       g_variant_new ("u", v));
}

static void
add_ulong_string_property (GVariantBuilder *builder,
			   RhythmDBEntry *entry,
			   RhythmDBPropType prop,
			   const char *name)
{
	ulong v;
	char *str;

	v = rhythmdb_entry_get_ulong (entry, prop);
	rb_debug ("adding %s = %lu", name, v);

	str = g_strdup_printf ("%lu", v);
	g_variant_builder_add (builder,
			       "{sv}",
			       name,
			       g_variant_new ("s", str));
	g_free (str);
}

static void
add_double_property (GVariantBuilder *builder,
		     RhythmDBEntry *entry,
		     RhythmDBPropType prop,
		     const char *name)
{
	int v;
	v = (int)rhythmdb_entry_get_double (entry, prop);
	rb_debug ("adding %s = %i", name, v);
	g_variant_builder_add (builder,
			       "{sv}",
			       name,
			       g_variant_new ("i", v));
}

static void
build_track_metadata (RBMprisPlugin *plugin,
		      GVariantBuilder *builder,
		      RhythmDBEntry *entry)
{
	GValue *md;

	add_string_property (builder, entry, RHYTHMDB_PROP_LOCATION, "location");
	add_string_property_2 (builder, plugin->db, entry, RHYTHMDB_PROP_TITLE, "title", RHYTHMDB_PROP_STREAM_SONG_TITLE);
	add_string_property_2 (builder, plugin->db, entry, RHYTHMDB_PROP_ARTIST, "artist", RHYTHMDB_PROP_STREAM_SONG_ARTIST);
	add_string_property_2 (builder, plugin->db, entry, RHYTHMDB_PROP_ALBUM, "album", RHYTHMDB_PROP_STREAM_SONG_ALBUM);
	add_string_property (builder, entry, RHYTHMDB_PROP_GENRE, "genre");
	add_string_property (builder, entry, RHYTHMDB_PROP_COMMENT, "comment");

	add_string_property (builder, entry, RHYTHMDB_PROP_MUSICBRAINZ_TRACKID, "mb track id");
	add_string_property (builder, entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID, "mb album id");
	add_string_property (builder, entry, RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID, "mb artist id");
	add_string_property (builder, entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID, "mb album artist id");

	add_string_property (builder, entry, RHYTHMDB_PROP_ARTIST_SORTNAME, "mb artist sort name");
	add_string_property (builder, entry, RHYTHMDB_PROP_ALBUM_SORTNAME, "mb album sort name");	/* extension */

	add_ulong_property (builder, entry, RHYTHMDB_PROP_DURATION, "time");
	add_ulong_property (builder, entry, RHYTHMDB_PROP_BITRATE, "audio-bitrate");
	add_ulong_property (builder, entry, RHYTHMDB_PROP_YEAR, "year");
	/* missing: date */

	add_ulong_string_property (builder, entry, RHYTHMDB_PROP_TRACK_NUMBER, "tracknumber");
	add_ulong_string_property (builder, entry, RHYTHMDB_PROP_DISC_NUMBER, "discnumber");	/* extension */

	add_double_property (builder, entry, RHYTHMDB_PROP_RATING, "rating");

	md = rhythmdb_entry_request_extra_metadata (plugin->db, entry, RHYTHMDB_PROP_COVER_ART_URI);
	if (md != NULL) {
		const char *uri;
		uri = g_value_get_string (md);
		if (uri != NULL && uri[0] != '\0') {
			g_variant_builder_add (builder, "{sv}", "arturl", g_variant_new ("s", uri));
		}

		g_value_unset (md);
		g_free (md);
	}
}

static GVariant *
get_player_state (RBMprisPlugin *plugin, GError **error)
{
	RhythmDBEntry *entry;
	int playback_state;
	gboolean random;
	gboolean repeat;
	gboolean loop;

	entry = rb_shell_player_get_playing_entry (plugin->player);
	if (entry != NULL) {
		gboolean playing;

		rhythmdb_entry_unref (entry);
		playing = FALSE;
		if (rb_shell_player_get_playing (plugin->player, &playing, error) == FALSE) {
			return NULL;
		}

		if (playing) {
			playback_state = 0;
		} else {
			playback_state = 1;
		}
	} else {
		playback_state = 2;
	}

	random = FALSE;
	loop = FALSE;
	rb_shell_player_get_playback_state (plugin->player, &random, &loop);

	/* repeat is not supported */
	repeat = FALSE;

	return g_variant_new ("((iiii))", playback_state, random, repeat, loop);
}

static void
handle_player_method_call (GDBusConnection *connection,
			   const char *sender,
			   const char *object_path,
			   const char *interface_name,
			   const char *method_name,
			   GVariant *parameters,
			   GDBusMethodInvocation *invocation,
			   RBMprisPlugin *plugin)

{
	GError *error = NULL;
	gboolean ret;
	if (g_strcmp0 (object_path, "/Player") != 0 ||
	    g_strcmp0 (interface_name, mpris_iface_name) != 0) {
		rb_debug ("?!");
		return;
	}

	if (g_strcmp0 (method_name, "Next") == 0) {
		ret = rb_shell_player_do_next (plugin->player, &error);
		handle_result (invocation, ret, error);
	} else if (g_strcmp0 (method_name, "Prev") == 0) {
		ret = rb_shell_player_do_previous (plugin->player, &error);
		handle_result (invocation, ret, error);
	} else if ((g_strcmp0 (method_name, "Pause") == 0)
		  || (g_strcmp0 (method_name, "PlayPause") == 0)) {
		ret = rb_shell_player_playpause (plugin->player, TRUE, &error);
		handle_result (invocation, ret, error);
	} else if (g_strcmp0 (method_name, "Stop") == 0) {
		rb_shell_player_stop (plugin->player);
		handle_result (invocation, TRUE, NULL);
	} else if (g_strcmp0 (method_name, "Play") == 0) {
		gboolean playing;
		g_object_get (plugin->player, "playing", &playing, NULL);
		if (playing) {
			ret = rb_shell_player_set_playing_time (plugin->player, 0, &error);
		} else {
			ret = rb_shell_player_playpause (plugin->player, TRUE, &error);
		}
		handle_result (invocation, ret, error);
	} else if (g_strcmp0 (method_name, "Repeat") == 0) {
		/* not actually supported */
	} else if (g_strcmp0 (method_name, "GetStatus") == 0) {
		GVariant *state;

		state = get_player_state (plugin, &error);
		if (state == NULL) {
			handle_result (invocation, FALSE, error);
		} else {
			g_dbus_method_invocation_return_value (invocation, state);
		}

	} else if (g_strcmp0 (method_name, "GetCaps") == 0) {
		/* values here are:
		 * CAN_GO_NEXT: 1
		 * CAN_GO_PREV: 2
		 * CAN_PAUSE: 4
		 * CAN_PLAY: 8
		 * CAN_SEEK: 16
		 * CAN_PROVIDE_METADATA: 32
		 * CAN_HAS_TRACKLIST: 64
		 */
		g_dbus_method_invocation_return_value (invocation,
						       g_variant_new ("i", 63));
	} else if (g_strcmp0 (method_name, "GetMetadata") == 0) {
		RhythmDBEntry *entry;
		GVariantBuilder *builder;

		builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);

		entry = rb_shell_player_get_playing_entry (plugin->player);
		if (entry != NULL) {
			build_track_metadata (plugin, builder, entry);
		} else {
			g_variant_builder_add (builder,
					       "{sv}",
					       "location",
					       g_variant_new ("s", ""));
		}

		g_dbus_method_invocation_return_value (invocation,
						       g_variant_new ("(a{sv})", builder));
	} else if (g_strcmp0 (method_name, "VolumeGet") == 0) {
		gdouble v;
		ret = rb_shell_player_get_volume (plugin->player, &v, &error);
		if (ret == FALSE) {
			handle_result (invocation, ret, error);
		} else {
			int iv;
			iv = (int)(v * 100.0);
			g_dbus_method_invocation_return_value (invocation,
							       g_variant_new ("(i)", iv));
		}
	} else if (g_strcmp0 (method_name, "VolumeSet") == 0) {
		int iv;
		gdouble v;
		g_variant_get (parameters, "(i)", &iv);
		v = ((gdouble)iv / 100.0);
		ret = rb_shell_player_set_volume (plugin->player, v, &error);
		handle_result (invocation, ret, error);

	} else if (g_strcmp0 (method_name, "PositionGet") == 0) {
		guint t;
		ret = rb_shell_player_get_playing_time (plugin->player, &t, &error);
		if (ret == FALSE) {
			handle_result (invocation, ret, error);
		} else {
			g_dbus_method_invocation_return_value (invocation,
							       g_variant_new ("(i)", t * 1000));
		}
	} else if (g_strcmp0 (method_name, "PositionSet") == 0) {
		guint t;
		g_variant_get (parameters, "(i)", &t);
		ret = rb_shell_player_set_playing_time (plugin->player, t, &error);
		handle_result (invocation, ret, error);
	}
}

static const GDBusInterfaceVTable player_vtable =
{
	(GDBusInterfaceMethodCallFunc) handle_player_method_call,
	NULL,
	NULL
};

/* MPRIS signals */

static void
emit_status_change (RBMprisPlugin *plugin)
{
	GError *error = NULL;
	GVariant *state;
	state = get_player_state (plugin, &error);
	if (state == NULL) {
		g_warning ("Unable to emit MPRIS StatusChange signal: %s", error->message);
		g_error_free (error);
		return;
	}

	g_dbus_connection_emit_signal (plugin->connection,
				       NULL,
				       "/Player",
				       mpris_iface_name,
				       "StatusChange",
				       state,
				       &error);
	if (error != NULL) {
		g_warning ("Unable to emit MPRIS StatusChange signal: %s", error->message);
		g_error_free (error);
	}
}

static void
play_order_changed_cb (GObject *object, GParamSpec *pspec, RBMprisPlugin *plugin)
{
	emit_status_change (plugin);
}

static void
playing_changed_cb (RBShellPlayer *player, gboolean playing, RBMprisPlugin *plugin)
{
	emit_status_change (plugin);
}

static void
emit_track_change (RBMprisPlugin *plugin, RhythmDBEntry *entry)
{
	GError *error = NULL;
	GVariantBuilder *builder;
	if (entry == NULL) {
		return;
	}

	builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
	build_track_metadata (plugin, builder, entry);

	g_dbus_connection_emit_signal (plugin->connection,
				       NULL,
				       "/Player",
				       mpris_iface_name,
				       "TrackChange",
				       g_variant_new ("(a{sv})", builder),
				       &error);
	if (error != NULL) {
		g_warning ("Unable to emit MPRIS TrackChange signal: %s", error->message);
		g_error_free (error);
	}
}

static void
playing_entry_changed_cb (RBShellPlayer *player, RhythmDBEntry *entry, RBMprisPlugin *plugin)
{
	rb_debug ("emitting track change due to playing entry change");
	emit_track_change (plugin, entry);
}

static void
entry_extra_metadata_notify_cb (RhythmDB *db, RhythmDBEntry *entry, const char *field, GValue *metadata, RBMprisPlugin *plugin)
{
	if (entry == rb_shell_player_get_playing_entry (plugin->player)) {
		rb_debug ("emitting track change due to extra metadata field %s", field);
		emit_track_change (plugin, entry);
	}
}

static void
name_acquired_cb (GDBusConnection *connection, const char *name, RBMprisPlugin *plugin)
{
	GError *error = NULL;
	GDBusNodeInfo *nodeinfo;
	const GDBusInterfaceInfo *ifaceinfo;

	plugin->connection = g_object_ref (connection);

	/* register root object */
	nodeinfo = g_dbus_node_info_new_for_xml (mpris_root_spec, &error);
	if (error != NULL) {
		g_warning ("Unable to read MPRIS root object specificiation: %s", error->message);
		g_assert_not_reached ();
		return;
	}
	ifaceinfo = g_dbus_node_info_lookup_interface (nodeinfo, mpris_iface_name);

	plugin->root_id = g_dbus_connection_register_object (plugin->connection,
							     "/",
							     ifaceinfo,
							     &root_vtable,
							     plugin,
							     NULL,
							     &error);
	if (error != NULL) {
		g_warning ("unable to register MPRIS root object: %s", error->message);
		g_error_free (error);
	}

	/* register fake tracklist object */
	nodeinfo = g_dbus_node_info_new_for_xml (mpris_tracklist_spec, &error);
	if (error != NULL) {
		g_warning ("Unable to read MPRIS tracklist object specificiation: %s", error->message);
		g_assert_not_reached ();
		return;
	}
	ifaceinfo = g_dbus_node_info_lookup_interface (nodeinfo, mpris_iface_name);

	plugin->tracklist_id = g_dbus_connection_register_object (plugin->connection,
								  "/TrackList",
								  ifaceinfo,
								  &tracklist_vtable,
								  plugin,
								  NULL,
								  &error);
	if (error != NULL) {
		g_warning ("unable to register MPRIS tracklist object: %s", error->message);
		g_error_free (error);
	}

	/* register player object */
	nodeinfo = g_dbus_node_info_new_for_xml (mpris_player_spec, &error);
	if (error != NULL) {
		g_warning ("Unable to read MPRIS player object specificiation: %s", error->message);
		g_assert_not_reached ();
		return;
	}
	ifaceinfo = g_dbus_node_info_lookup_interface (nodeinfo, mpris_iface_name);
	plugin->player_id = g_dbus_connection_register_object (plugin->connection,
							       "/Player",
							       ifaceinfo,
							       &player_vtable,
							       plugin,
							       NULL,
							       &error);
	if (error != NULL) {
		g_warning ("Unable to register MPRIS player object: %s", error->message);
		g_error_free (error);
	}

	/* connect signal handlers for stuff */
	g_signal_connect_object (plugin->player,
				 "notify::play-order",
				 G_CALLBACK (play_order_changed_cb),
				 plugin, 0);
	g_signal_connect_object (plugin->player,
				 "playing-changed",
				 G_CALLBACK (playing_changed_cb),
				 plugin, 0);
	g_signal_connect_object (plugin->player,
				 "playing-song-changed",
				 G_CALLBACK (playing_entry_changed_cb),
				 plugin, 0);
	g_signal_connect_object (plugin->db,
				 "entry-extra-metadata-notify",
				 G_CALLBACK (entry_extra_metadata_notify_cb),
				 plugin, 0);
}

static void
name_lost_cb (GDBusConnection *connection, const char *name, RBMprisPlugin *plugin)
{
	if (plugin->root_id != 0) {
		g_dbus_connection_unregister_object (plugin->connection, plugin->root_id);
		plugin->root_id = 0;
	}
	if (plugin->tracklist_id != 0) {
		g_dbus_connection_unregister_object (plugin->connection, plugin->tracklist_id);
		plugin->tracklist_id = 0;
	}
	if (plugin->player_id != 0) {
		g_dbus_connection_unregister_object (plugin->connection, plugin->player_id);
		plugin->player_id = 0;
	}

	/* probably remove signal handlers? */

	if (plugin->connection != NULL) {
		g_object_unref (plugin->connection);
		plugin->connection = NULL;
	}
}

static void
impl_activate (RBPlugin *bplugin,
	       RBShell *shell)
{
	RBMprisPlugin *plugin;

	rb_debug ("activating MPRIS plugin");

	plugin = RB_MPRIS_PLUGIN (bplugin);
	g_object_get (shell,
		      "shell-player", &plugin->player,
		      "db", &plugin->db,
		      NULL);
	plugin->shell = g_object_ref (shell);

	plugin->name_own_id = g_bus_own_name (G_BUS_TYPE_SESSION,
					      "org.mpris.rhythmbox",
					      G_BUS_NAME_OWNER_FLAGS_NONE,
					      NULL,
					      (GBusNameAcquiredCallback) name_acquired_cb,
					      (GBusNameLostCallback) name_lost_cb,
					      g_object_ref (plugin),
					      g_object_unref);
}

static void
impl_deactivate	(RBPlugin *bplugin,
		 RBShell *shell)
{
	RBMprisPlugin *plugin;

	plugin = RB_MPRIS_PLUGIN (bplugin);
	if (plugin->player != NULL) {
		g_object_unref (plugin->player);
		plugin->player = NULL;
	}
	if (plugin->shell != NULL) {
		g_object_unref (plugin->shell);
		plugin->shell = NULL;
	}
	if (plugin->db != NULL) {
		g_object_unref (plugin->db);
		plugin->db = NULL;
	}

	if (plugin->name_own_id > 0) {
		g_bus_unown_name (plugin->name_own_id);
		plugin->name_own_id = 0;
	}
}


static void
rb_mpris_plugin_class_init (RBMprisPluginClass *klass)
{
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
}
