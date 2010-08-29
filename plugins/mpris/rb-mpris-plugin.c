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
#include <lib/eggdesktopfile.h>
#include <shell/rb-plugin.h>
#include <shell/rb-shell.h>
#include <shell/rb-shell-player.h>
#include <backends/rb-player.h>

#define RB_TYPE_MPRIS_PLUGIN		(rb_mpris_plugin_get_type ())
#define RB_MPRIS_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_MPRIS_PLUGIN, RBMprisPlugin))
#define RB_MPRIS_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_MPRIS_PLUGIN, RBMprisPluginClass))
#define RB_IS_MPRIS_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_MPRIS_PLUGIN))
#define RB_IS_MPRIS_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_MPRIS_PLUGIN))
#define RB_MPRIS_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_MPRIS_PLUGIN, RBMprisPluginClass))

#define ENTRY_OBJECT_PATH_PREFIX 	"/org/mpris/MediaPlayer2/Track/"

#include "mpris-spec.h"

typedef struct
{
	RBPlugin parent;

	GDBusConnection *connection;
	GDBusNodeInfo *node_info;
	guint name_own_id;
	guint root_id;
	guint player_id;

	RBShell *shell;
	RBShellPlayer *player;
	RhythmDB *db;
	GtkAction *next_action;
	GtkAction *prev_action;

	GHashTable *player_property_changes;
	guint player_property_emit_id;

	gint64 last_elapsed;
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

/* property change stuff */

static gboolean
emit_player_properties_idle (RBMprisPlugin *plugin)
{
	GError *error = NULL;
	GVariantBuilder *properties;
	GVariant *parameters;
	const char *invalidated[] = { NULL };
	gpointer propname, propvalue;
	GHashTableIter iter;

	/* build property changes */
	properties = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
	g_hash_table_iter_init (&iter, plugin->player_property_changes);
	while (g_hash_table_iter_next (&iter, &propname, &propvalue)) {
		g_variant_builder_add (properties,
				       "{sv}",
				       propname,
				       propvalue);
	}
	g_hash_table_destroy (plugin->player_property_changes);
	plugin->player_property_changes = NULL;

	/* build set of invalidated properties (not supported yet) */

	parameters = g_variant_new ("(sa{sv}^as)",
				    MPRIS_PLAYER_INTERFACE,
				    properties,
				    invalidated);
	g_variant_builder_unref (properties);
	g_dbus_connection_emit_signal (plugin->connection,
				       NULL,
				       MPRIS_OBJECT_NAME,
				       "org.freedesktop.DBus.Properties",
				       "PropertiesChanged",
				       parameters,
				       &error);
	if (error != NULL) {
		g_warning ("Unable to send MPRIS property changes: %s",
			   error->message);
		g_clear_error (&error);
	}

	plugin->player_property_emit_id = 0;
	return FALSE;
}

static void
add_player_property_change (RBMprisPlugin *plugin,
			    const char *property,
			    GVariant *value)
{
	if (plugin->player_property_changes == NULL) {
		plugin->player_property_changes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	}
	g_hash_table_insert (plugin->player_property_changes, g_strdup (property), value);

	if (plugin->player_property_emit_id == 0) {
		plugin->player_property_emit_id = g_idle_add ((GSourceFunc)emit_player_properties_idle, plugin);
	}
}

/* MPRIS root interface */

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
	if (g_strcmp0 (object_path, MPRIS_OBJECT_NAME) != 0 ||
	    g_strcmp0 (interface_name, MPRIS_ROOT_INTERFACE) != 0) {
		g_dbus_method_invocation_return_error (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_NOT_SUPPORTED,
						       "Method %s.%s not supported",
						       interface_name,
						       method_name);
		return;
	}

	if (g_strcmp0 (method_name, "Raise") == 0) {
		rb_shell_present (plugin->shell, GDK_CURRENT_TIME, NULL);
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else if (g_strcmp0 (method_name, "Quit") == 0) {
		rb_shell_quit (plugin->shell, NULL);
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else {
		g_dbus_method_invocation_return_error (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_NOT_SUPPORTED,
						       "Method %s.%s not supported",
						       interface_name,
						       method_name);
	}
}

static GVariant *
get_root_property (GDBusConnection *connection,
		   const char *sender,
		   const char *object_path,
		   const char *interface_name,
		   const char *property_name,
		   GError **error,
		   RBMprisPlugin *plugin)
{
	if (g_strcmp0 (object_path, MPRIS_OBJECT_NAME) != 0 ||
	    g_strcmp0 (interface_name, MPRIS_ROOT_INTERFACE) != 0) {
		g_set_error (error,
			     G_DBUS_ERROR,
			     G_DBUS_ERROR_NOT_SUPPORTED,
			     "Property %s.%s not supported",
			     interface_name,
			     property_name);
		return NULL;
	}

	if (g_strcmp0 (property_name, "CanQuit") == 0) {
		return g_variant_new_boolean (TRUE);
	} else if (g_strcmp0 (property_name, "CanRaise") == 0) {
		return g_variant_new_boolean (TRUE);
	} else if (g_strcmp0 (property_name, "HasTrackList") == 0) {
		return g_variant_new_boolean (FALSE);
	} else if (g_strcmp0 (property_name, "Identity") == 0) {
		EggDesktopFile *desktop_file;
		desktop_file = egg_get_desktop_file ();
		return g_variant_new_string (egg_desktop_file_get_name (desktop_file));
	} else if (g_strcmp0 (property_name, "DesktopEntry") == 0) {
		EggDesktopFile *desktop_file;
		GVariant *v = NULL;
		char *path;

		desktop_file = egg_get_desktop_file ();
		path = g_filename_from_uri (egg_desktop_file_get_source (desktop_file), NULL, error);
		if (path != NULL) {
			v = g_variant_new_string (path);
			g_free (path);
		} else {
			g_warning ("Unable to return desktop file path to MPRIS client: %s", (*error)->message);
		}

		return v;
	} else if (g_strcmp0 (property_name, "SupportedUriSchemes") == 0) {
		/* not planning to support this seriously */
		const char *fake_supported_schemes[] = {
			"file", "http", "cdda", "smb", "sftp", NULL
		};
		return g_variant_new_strv (fake_supported_schemes, -1);
	} else if (g_strcmp0 (property_name, "SupportedMimeTypes") == 0) {
		/* nor this */
		const char *fake_supported_mimetypes[] = {
			"application/ogg", "audio/x-vorbis+ogg", "audio/x-flac", "audio/mpeg", NULL
		};
		return g_variant_new_strv (fake_supported_mimetypes, -1);
	}

	g_set_error (error,
		     G_DBUS_ERROR,
		     G_DBUS_ERROR_NOT_SUPPORTED,
		     "Property %s.%s not supported",
		     interface_name,
		     property_name);
	return NULL;
}

static const GDBusInterfaceVTable root_vtable =
{
	(GDBusInterfaceMethodCallFunc) handle_root_method_call,
	(GDBusInterfaceGetPropertyFunc) get_root_property,
	NULL
};

/* MPRIS player interface */

static void
handle_result (GDBusMethodInvocation *invocation, gboolean ret, GError *error)
{
	if (ret) {
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else {
		if (error != NULL) {
			rb_debug ("returning error: %s", error->message);
			g_dbus_method_invocation_return_gerror (invocation, error);
			g_error_free (error);
		} else {
			rb_debug ("returning unknown error");
			g_dbus_method_invocation_return_error_literal (invocation,
								       G_DBUS_ERROR,
								       G_DBUS_ERROR_FAILED,
								       "Unknown error");
		}
	}
}

static GVariant *
variant_for_metadata (const char *value, gboolean as_list)
{
	if (as_list) {
		const char *strv[] = {
			value, NULL
		};
		return g_variant_new_strv (strv, -1);
	} else {
		return g_variant_new_string (value);
	}
}

static void
add_string_property (GVariantBuilder *builder,
		     RhythmDBEntry *entry,
		     RhythmDBPropType prop,
		     const char *name,
		     gboolean as_list)
{
	const char *value = rhythmdb_entry_get_string (entry, prop);
	if (value != NULL && value[0] != '\0') {
		rb_debug ("adding %s = %s", name, value);
		g_variant_builder_add (builder, "{sv}", name, variant_for_metadata (value, as_list));
	}
}

static void
add_string_property_2 (GVariantBuilder *builder,
		       RhythmDB *db,
		       RhythmDBEntry *entry,
		       RhythmDBPropType prop,
		       const char *name,
		       const char *extra_field_name,
		       gboolean as_list)
{
	GValue *v;
	const char *value;

	v = rhythmdb_entry_request_extra_metadata (db, entry, extra_field_name);
	if (v != NULL) {
		value = g_value_get_string (v);
	} else {
		value = rhythmdb_entry_get_string (entry, prop);
	}

	if (value != NULL && value[0] != '\0') {
		rb_debug ("adding %s = %s", name, value);
		g_variant_builder_add (builder, "{sv}", name, variant_for_metadata (value, as_list));
	}

	if (v != NULL) {
		g_value_unset (v);
		g_free (v);
	}
}

static void
add_ulong_property (GVariantBuilder *builder,
		    RhythmDBEntry *entry,
		    RhythmDBPropType prop,
		    const char *name,
		    int scale,
		    gboolean zero_is_valid)
{
	ulong v;
	v = rhythmdb_entry_get_ulong (entry, prop);
	if (zero_is_valid || v != 0) {
		rb_debug ("adding %s = %lu", name, v);
		g_variant_builder_add (builder,
				       "{sv}",
				       name,
				       g_variant_new_int32 (v * scale));
	}
}

static void
add_ulong_property_as_int64 (GVariantBuilder *builder,
			     RhythmDBEntry *entry,
			     RhythmDBPropType prop,
			     const char *name,
			     gint64 scale)
{
	gint64 v;
	v = rhythmdb_entry_get_ulong (entry, prop);
	rb_debug ("adding %s = %" G_GINT64_FORMAT, name, v * scale);
	g_variant_builder_add (builder,
			       "{sv}",
			       name,
			       g_variant_new_int64 (v * scale));
}

static void
add_double_property (GVariantBuilder *builder,
		     RhythmDBEntry *entry,
		     RhythmDBPropType prop,
		     const char *name,
		     gdouble scale)
{
	gdouble v;
	v = rhythmdb_entry_get_double (entry, prop);
	rb_debug ("adding %s = %f", name, v * scale);
	g_variant_builder_add (builder,
			       "{sv}",
			       name,
			       g_variant_new_double (v * scale));
}

static void
add_double_property_as_int (GVariantBuilder *builder,
			    RhythmDBEntry *entry,
			    RhythmDBPropType prop,
			    const char *name,
			    gdouble scale,
			    gboolean zero_is_valid)
{
	int v;
	v = (int)(rhythmdb_entry_get_double (entry, prop) * scale);
	if (zero_is_valid || v != 0) {
		rb_debug ("adding %s = %d", name, v);
		g_variant_builder_add (builder,
				       "{sv}",
				       name,
				       g_variant_new_int32 (v));
	}
}

static void
add_year_date_property (GVariantBuilder *builder,
			RhythmDBEntry *entry,
			RhythmDBPropType prop,
			const char *name)
{
	gulong year = rhythmdb_entry_get_ulong (entry, prop);

	if (year != 0) {
		char *iso8601;
		iso8601 = g_strdup_printf ("%4d-%02d-%02dT%02d:%02d:%02dZ",
					   (int)year, 1, 1, 0, 0, 0);

		g_variant_builder_add (builder,
				       "{sv}",
				       name,
				       g_variant_new_string (iso8601));
		g_free (iso8601);
	}
}

static void
add_time_t_date_property (GVariantBuilder *builder,
			  RhythmDBEntry *entry,
			  RhythmDBPropType prop,
			  const char *name)
{
	GTimeVal tv;

	tv.tv_sec = rhythmdb_entry_get_ulong (entry, prop);
	tv.tv_usec = 0;

	if (tv.tv_sec != 0) {
		char *iso8601 = g_time_val_to_iso8601 (&tv);
		g_variant_builder_add (builder,
				       "{sv}",
				       name,
				       g_variant_new_string (iso8601));
		g_free (iso8601);
	}
}

static void
build_track_metadata (RBMprisPlugin *plugin,
		      GVariantBuilder *builder,
		      RhythmDBEntry *entry)
{
	GValue *md;
	char *trackid_str;

	trackid_str = g_strdup_printf(ENTRY_OBJECT_PATH_PREFIX "%lu",
				      rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_ENTRY_ID));
	g_variant_builder_add (builder,
			       "{sv}",
			       "mpris:trackid",
			       g_variant_new ("s", trackid_str));
	g_free (trackid_str);

	add_string_property (builder, entry, RHYTHMDB_PROP_LOCATION, "xesam:url", FALSE);
	add_string_property_2 (builder, plugin->db, entry, RHYTHMDB_PROP_TITLE, "xesam:title", RHYTHMDB_PROP_STREAM_SONG_TITLE, FALSE);
	add_string_property_2 (builder, plugin->db, entry, RHYTHMDB_PROP_ARTIST, "xesam:artist", RHYTHMDB_PROP_STREAM_SONG_ARTIST, TRUE);
	add_string_property_2 (builder, plugin->db, entry, RHYTHMDB_PROP_ALBUM, "xesam:album", RHYTHMDB_PROP_STREAM_SONG_ALBUM, FALSE);
	add_string_property (builder, entry, RHYTHMDB_PROP_GENRE, "xesam:genre", TRUE);
	add_string_property (builder, entry, RHYTHMDB_PROP_COMMENT, "xesam:comment", TRUE);
	add_string_property (builder, entry, RHYTHMDB_PROP_ALBUM_ARTIST, "xesam:albumArtist", TRUE);

	add_string_property (builder, entry, RHYTHMDB_PROP_MUSICBRAINZ_TRACKID, "xesam:musicBrainzTrackID", TRUE);
	add_string_property (builder, entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID, "xesam:musicBrainzAlbumID", TRUE);
	add_string_property (builder, entry, RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID, "xesam:musicBrainzArtistID", TRUE);
	add_string_property (builder, entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID, "xesam:musicBrainzAlbumArtistID", TRUE);

	/* would be nice to have mpris: names for these. */
	add_string_property (builder, entry, RHYTHMDB_PROP_ARTIST_SORTNAME, "rhythmbox:artistSortname", FALSE);
	add_string_property (builder, entry, RHYTHMDB_PROP_ALBUM_SORTNAME, "rhythmbox:albumSortname", FALSE);
	add_string_property (builder, entry, RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME, "rhythmbox:albumArtistSortname", FALSE);

	add_ulong_property (builder, entry, RHYTHMDB_PROP_BITRATE, "xesam:audioBitrate", 1024, FALSE);	/* scale to bits per second */

	add_year_date_property (builder, entry, RHYTHMDB_PROP_YEAR, "xesam:contentCreated");
	add_time_t_date_property (builder, entry, RHYTHMDB_PROP_LAST_PLAYED, "xesam:lastUsed");

	add_ulong_property_as_int64 (builder, entry, RHYTHMDB_PROP_DURATION, "mpris:length", G_USEC_PER_SEC);
	add_ulong_property (builder, entry, RHYTHMDB_PROP_TRACK_NUMBER, "xesam:trackNumber", 1, TRUE);
	add_ulong_property (builder, entry, RHYTHMDB_PROP_DISC_NUMBER, "xesam:discNumber", 1, FALSE);
	add_ulong_property (builder, entry, RHYTHMDB_PROP_PLAY_COUNT, "xesam:useCount", 1, TRUE);

	add_double_property (builder, entry, RHYTHMDB_PROP_RATING, "xesam:userRating", 0.2);	/* scale to 0..1 */
	add_double_property_as_int (builder, entry, RHYTHMDB_PROP_BPM, "xesam:audioBPM", 1.0, FALSE);

	md = rhythmdb_entry_request_extra_metadata (plugin->db, entry, RHYTHMDB_PROP_COVER_ART_URI);
	if (md != NULL) {
		const char *uri;
		uri = g_value_get_string (md);
		if (uri != NULL && uri[0] != '\0') {
			g_variant_builder_add (builder, "{sv}", "mpris:artUrl", g_variant_new ("s", uri));
		}

		g_value_unset (md);
		g_free (md);
	}

	/* maybe do lyrics? */
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
	if (g_strcmp0 (object_path, MPRIS_OBJECT_NAME) != 0 ||
	    g_strcmp0 (interface_name, MPRIS_PLAYER_INTERFACE) != 0) {
		g_dbus_method_invocation_return_error (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_NOT_SUPPORTED,
						       "Method %s.%s not supported",
						       interface_name,
						       method_name);
		return;
	}

	if (g_strcmp0 (method_name, "Next") == 0) {
		ret = rb_shell_player_do_next (plugin->player, &error);
		handle_result (invocation, ret, error);
	} else if (g_strcmp0 (method_name, "Previous") == 0) {
		ret = rb_shell_player_do_previous (plugin->player, &error);
		handle_result (invocation, ret, error);
	} else if (g_strcmp0 (method_name, "Pause") == 0) {
		ret = rb_shell_player_pause (plugin->player, &error);
		handle_result (invocation, ret, error);
	} else if (g_strcmp0 (method_name, "PlayPause") == 0) {
		ret = rb_shell_player_playpause (plugin->player, TRUE, &error);
		handle_result (invocation, ret, error);
	} else if (g_strcmp0 (method_name, "Stop") == 0) {
		rb_shell_player_stop (plugin->player);
		handle_result (invocation, TRUE, NULL);
	} else if (g_strcmp0 (method_name, "Play") == 0) {
		ret = rb_shell_player_play (plugin->player, &error);
		handle_result (invocation, ret, error);
	} else if (g_strcmp0 (method_name, "Seek") == 0) {
		gint64 offset;
		g_variant_get (parameters, "(x)", &offset);
		rb_shell_player_seek (plugin->player, offset / G_USEC_PER_SEC, NULL);
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else if (g_strcmp0 (method_name, "SetPosition") == 0) {
		RhythmDBEntry *playing_entry;
		RhythmDBEntry *client_entry;
		gint64 position;
		const char *client_entry_path;

		playing_entry = rb_shell_player_get_playing_entry (plugin->player);
		if (playing_entry == NULL) {
			/* not playing, so we can't seek */
			g_dbus_method_invocation_return_value (invocation, NULL);
			return;
		}

		g_variant_get (parameters, "(&ox)", &client_entry_path, &position);

		if (g_str_has_prefix (client_entry_path, ENTRY_OBJECT_PATH_PREFIX) == FALSE) {
			/* this can't possibly be the current playing track, so ignore it */
			g_dbus_method_invocation_return_value (invocation, NULL);
			rhythmdb_entry_unref (playing_entry);
			return;
		}

		client_entry_path += strlen (ENTRY_OBJECT_PATH_PREFIX);
		client_entry = rhythmdb_entry_lookup_from_string (plugin->db, client_entry_path, TRUE);
		if (client_entry == NULL) {
			/* ignore it */
			g_dbus_method_invocation_return_value (invocation, NULL);
			rhythmdb_entry_unref (playing_entry);
			return;
		}

		if (playing_entry != client_entry) {
			/* client got the wrong entry, ignore it */
			g_dbus_method_invocation_return_value (invocation, NULL);
			rhythmdb_entry_unref (playing_entry);
			return;
		}
		rhythmdb_entry_unref (playing_entry);

		ret = rb_shell_player_set_playing_time (plugin->player, position / G_USEC_PER_SEC, &error);
		handle_result (invocation, ret, error);
	} else if (g_strcmp0 (method_name, "OpenUri") == 0) {
		const char *uri;
		g_variant_get (parameters, "(&s)", &uri);
		ret = rb_shell_load_uri (plugin->shell, uri, TRUE, &error);
		handle_result (invocation, ret, error);
	} else {
		g_dbus_method_invocation_return_error (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_NOT_SUPPORTED,
						       "Method %s.%s not supported",
						       interface_name,
						       method_name);
	}
}

static GVariant *
get_playback_status (RBMprisPlugin *plugin)
{
	RhythmDBEntry *entry;

	entry = rb_shell_player_get_playing_entry (plugin->player);
	if (entry == NULL) {
		return g_variant_new_string ("Stopped");
	} else {
		GVariant *v;
		gboolean playing;
		if (rb_shell_player_get_playing (plugin->player, &playing, NULL)) {
			if (playing) {
				v = g_variant_new_string ("Playing");
			} else {
				v = g_variant_new_string ("Paused");
			}
		} else {
			v = NULL;
		}
		rhythmdb_entry_unref (entry);
		return v;
	}
}

static GVariant *
get_loop_status (RBMprisPlugin *plugin)
{
	gboolean loop = FALSE;
	rb_shell_player_get_playback_state (plugin->player, NULL, &loop);
	if (loop) {
		return g_variant_new_string ("Playlist");
	} else {
		return g_variant_new_string ("None");
	}
}

static GVariant *
get_shuffle (RBMprisPlugin *plugin)
{
	gboolean random = FALSE;

	rb_shell_player_get_playback_state (plugin->player, &random, NULL);
	return g_variant_new_boolean (random);
}

static GVariant *
get_volume (RBMprisPlugin *plugin)
{
	gdouble vol;
	if (rb_shell_player_get_volume (plugin->player, &vol, NULL)) {
		return g_variant_new_double (vol);
	} else {
		return NULL;
	}
}

static GVariant *
get_can_pause (RBMprisPlugin *plugin)
{
	RBSource *source;
	source = rb_shell_player_get_playing_source (plugin->player);
	if (source != NULL) {
		return g_variant_new_boolean (rb_source_can_pause (source));
	} else {
		return g_variant_new_boolean (TRUE);
	}
}

static GVariant *
get_can_seek (RBMprisPlugin *plugin)
{
	RBPlayer *player;
	GVariant *v;

	g_object_get (plugin->player, "player", &player, NULL);
	if (player != NULL) {
		v = g_variant_new_boolean (rb_player_seekable (player));
		g_object_unref (player);
	} else {
		v = g_variant_new_boolean (FALSE);
	}
	return v;
}

static GVariant *
get_player_property (GDBusConnection *connection,
		     const char *sender,
		     const char *object_path,
		     const char *interface_name,
		     const char *property_name,
		     GError **error,
		     RBMprisPlugin *plugin)
{
	gboolean ret;

	if (g_strcmp0 (object_path, MPRIS_OBJECT_NAME) != 0 ||
	    g_strcmp0 (interface_name, MPRIS_PLAYER_INTERFACE) != 0) {
		g_set_error (error,
			     G_DBUS_ERROR,
			     G_DBUS_ERROR_NOT_SUPPORTED,
			     "Property %s.%s not supported",
			     interface_name,
			     property_name);
		return NULL;
	}

	if (g_strcmp0 (property_name, "PlaybackStatus") == 0) {
		return get_playback_status (plugin);
	} else if (g_strcmp0 (property_name, "LoopStatus") == 0) {
		return get_loop_status (plugin);
	} else if (g_strcmp0 (property_name, "Rate") == 0) {
		return g_variant_new_double (1.0);
	} else if (g_strcmp0 (property_name, "Shuffle") == 0) {
		return get_shuffle (plugin);
	} else if (g_strcmp0 (property_name, "Metadata") == 0) {
		RhythmDBEntry *entry;
		GVariantBuilder *builder;
		GVariant *v;

		builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
		entry = rb_shell_player_get_playing_entry (plugin->player);
		if (entry != NULL) {
			build_track_metadata (plugin, builder, entry);
			rhythmdb_entry_unref (entry);
		}

		v = g_variant_builder_end (builder);
		g_variant_builder_unref (builder);
		return v;
	} else if (g_strcmp0 (property_name, "Volume") == 0) {
		return get_volume (plugin);
	} else if (g_strcmp0 (property_name, "Position") == 0) {
		guint t;
		ret = rb_shell_player_get_playing_time (plugin->player, &t, error);
		if (ret) {
			return g_variant_new_int64 (t * G_USEC_PER_SEC);
		} else {
			return NULL;
		}
	} else if (g_strcmp0 (property_name, "MinimumRate") == 0) {
		return g_variant_new_double (1.0);
	} else if (g_strcmp0 (property_name, "MaximumRate") == 0) {
		return g_variant_new_double (1.0);
	} else if (g_strcmp0 (property_name, "CanGoNext") == 0) {
		return g_variant_new_boolean (gtk_action_get_sensitive (plugin->next_action));
	} else if (g_strcmp0 (property_name, "CanGoPrevious") == 0) {
		return g_variant_new_boolean (gtk_action_get_sensitive (plugin->prev_action));
	} else if (g_strcmp0 (property_name, "CanPlay") == 0) {
		/* uh.. under what conditions can we not play?  nothing in the source? */
		return g_variant_new_boolean (TRUE);
	} else if (g_strcmp0 (property_name, "CanPause") == 0) {
		return get_can_pause (plugin);
	} else if (g_strcmp0 (property_name, "CanSeek") == 0) {
		return get_can_seek (plugin);
	} else if (g_strcmp0 (property_name, "CanControl") == 0) {
		return g_variant_new_boolean (TRUE);
	}

	g_set_error (error,
		     G_DBUS_ERROR,
		     G_DBUS_ERROR_NOT_SUPPORTED,
		     "Property %s.%s not supported",
		     interface_name,
		     property_name);
	return NULL;
}

static gboolean
set_player_property (GDBusConnection *connection,
		     const char *sender,
		     const char *object_path,
		     const char *interface_name,
		     const char *property_name,
		     GVariant *value,
		     GError **error,
		     RBMprisPlugin *plugin)
{
	if (g_strcmp0 (object_path, MPRIS_OBJECT_NAME) != 0 ||
	    g_strcmp0 (interface_name, MPRIS_PLAYER_INTERFACE) != 0) {
		g_set_error (error,
			     G_DBUS_ERROR,
			     G_DBUS_ERROR_NOT_SUPPORTED,
			     "%s:%s not supported",
			     object_path,
			     interface_name);
		return FALSE;
	}

	if (g_strcmp0 (property_name, "LoopStatus") == 0) {
		gboolean shuffle;
		gboolean repeat;
		const char *status;

		rb_shell_player_get_playback_state (plugin->player, &shuffle, &repeat);

		status = g_variant_get_string (value, NULL);
		if (g_strcmp0 (status, "None") == 0) {
			repeat = FALSE;
		} else if (g_strcmp0 (status, "Playlist") == 0) {
			repeat = TRUE;
		} else {
			repeat = FALSE;
		}
		rb_shell_player_set_playback_state (plugin->player, shuffle, repeat);
		return TRUE;
	} else if (g_strcmp0 (property_name, "Rate") == 0) {
		/* not supported */
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "Can't modify playback rate");
		return FALSE;
	} else if (g_strcmp0 (property_name, "Shuffle") == 0) {
		gboolean shuffle;
		gboolean repeat;

		rb_shell_player_get_playback_state (plugin->player, &shuffle, &repeat);
		shuffle = g_variant_get_boolean (value);
		rb_shell_player_set_playback_state (plugin->player, shuffle, repeat);
		return TRUE;
	} else if (g_strcmp0 (property_name, "Volume") == 0) {
		rb_shell_player_set_volume (plugin->player, g_variant_get_double (value), error);
		return TRUE;
	}

	g_set_error (error,
		     G_DBUS_ERROR,
		     G_DBUS_ERROR_NOT_SUPPORTED,
		     "Property %s.%s not supported",
		     interface_name,
		     property_name);
	return FALSE;
}

static const GDBusInterfaceVTable player_vtable =
{
	(GDBusInterfaceMethodCallFunc) handle_player_method_call,
	(GDBusInterfaceGetPropertyFunc) get_player_property,
	(GDBusInterfaceSetPropertyFunc) set_player_property,
};

static void
play_order_changed_cb (GObject *object, GParamSpec *pspec, RBMprisPlugin *plugin)
{
	rb_debug ("emitting LoopStatus and Shuffle change");
	add_player_property_change (plugin, "LoopStatus", get_loop_status (plugin));
	add_player_property_change (plugin, "Shuffle", get_shuffle (plugin));
}

static void
volume_changed_cb (GObject *object, GParamSpec *pspec, RBMprisPlugin *plugin)
{
	rb_debug ("emitting Volume change");
	add_player_property_change (plugin, "Volume", get_volume (plugin));
}

static void
playing_changed_cb (RBShellPlayer *player, gboolean playing, RBMprisPlugin *plugin)
{
	rb_debug ("emitting PlaybackStatus change");
	add_player_property_change (plugin, "PlaybackStatus", get_playback_status (plugin));
}

static void
metadata_changed (RBMprisPlugin *plugin, RhythmDBEntry *entry)
{
	GVariantBuilder *builder;

	builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
	if (entry != NULL) {
		build_track_metadata (plugin, builder, entry);
	}
	add_player_property_change (plugin, "Metadata", g_variant_builder_end (builder));
	g_variant_builder_unref (builder);
}

static void
playing_entry_changed_cb (RBShellPlayer *player, RhythmDBEntry *entry, RBMprisPlugin *plugin)
{
	rb_debug ("emitting Metadata and CanSeek changed");
	plugin->last_elapsed = 0;
	metadata_changed (plugin, entry);
	add_player_property_change (plugin, "CanSeek", get_can_seek (plugin));
}

static void
entry_extra_metadata_notify_cb (RhythmDB *db, RhythmDBEntry *entry, const char *field, GValue *metadata, RBMprisPlugin *plugin)
{
	RhythmDBEntry *playing_entry = rb_shell_player_get_playing_entry (plugin->player);
	if (entry == playing_entry) {
		rb_debug ("emitting Metadata change due to extra metadata field %s", field);
		metadata_changed (plugin, entry);
	}
	if (playing_entry != NULL) {
		rhythmdb_entry_unref (playing_entry);
	}
}

static void
entry_changed_cb (RhythmDB *db, RhythmDBEntry *entry, GValueArray *changes, RBMprisPlugin *plugin)
{
	RhythmDBEntry *playing_entry = rb_shell_player_get_playing_entry (plugin->player);
	if (playing_entry == NULL) {
		return;
	}
	if (playing_entry == entry) {
		int i;
		gboolean emit = FALSE;

		/* make sure there's an interesting property change in there */
		for (i = 0; i < changes->n_values; i++) {
			RhythmDBEntryChange *change = g_value_get_boxed (g_value_array_get_nth (changes, i));
			switch (change->prop) {
				/* probably not complete */
				case RHYTHMDB_PROP_MOUNTPOINT:
				case RHYTHMDB_PROP_MTIME:
				case RHYTHMDB_PROP_FIRST_SEEN:
				case RHYTHMDB_PROP_LAST_SEEN:
				case RHYTHMDB_PROP_LAST_PLAYED:
				case RHYTHMDB_PROP_MIMETYPE:
				case RHYTHMDB_PROP_PLAYBACK_ERROR:
					break;

				default:
					emit = TRUE;
					break;
			}
		}

		if (emit) {
			rb_debug ("emitting Metadata change due to property changes");
			metadata_changed (plugin, playing_entry);
		}
	}
	rhythmdb_entry_unref (playing_entry);
}

static void
playing_source_changed_cb (RBShellPlayer *player,
			   RBSource *source,
			   RBMprisPlugin *plugin)
{
	rb_debug ("emitting CanPause change");
	add_player_property_change (plugin, "CanPause", get_can_pause (plugin));
}

static void
next_action_sensitive_cb (GObject *object, GParamSpec *pspec, RBMprisPlugin *plugin)
{
	GVariant *v;
	rb_debug ("emitting CanGoNext change");
	v = g_variant_new_boolean (gtk_action_get_sensitive (plugin->next_action));
	add_player_property_change (plugin, "CanGoNext", v);
}

static void
prev_action_sensitive_cb (GObject *object, GParamSpec *pspec, RBMprisPlugin *plugin)
{
	GVariant *v;
	rb_debug ("emitting CanGoPrevious change");
	v = g_variant_new_boolean (gtk_action_get_sensitive (plugin->prev_action));
	add_player_property_change (plugin, "CanGoPrevious", v);
}

static void
elapsed_nano_changed_cb (RBShellPlayer *player, gint64 elapsed, RBMprisPlugin *plugin)
{
	GError *error = NULL;

	/* interpret any change in the elapsed time other than an
	 * increase of less than one second as a seek.  this includes
	 * the seek back that we do after pausing (with crossfading),
	 * which we intentionally report as a seek to help clients get
	 * their time displays right.
	 */
	if (elapsed >= plugin->last_elapsed &&
	    (elapsed - plugin->last_elapsed < (G_USEC_PER_SEC * 1000))) {
		plugin->last_elapsed = elapsed;
		return;
	}

	rb_debug ("emitting Seeked; new time %" G_GINT64_FORMAT, elapsed/1000);
	g_dbus_connection_emit_signal (plugin->connection,
				       NULL,
				       MPRIS_OBJECT_NAME,
				       MPRIS_PLAYER_INTERFACE,
				       "Seeked",
				       g_variant_new ("(x)", elapsed / 1000),
				       &error);
	if (error != NULL) {
		g_warning ("Unable to set MPRIS Seeked signal: %s", error->message);
		g_clear_error (&error);
	}
	plugin->last_elapsed = elapsed;
}



static void
name_acquired_cb (GDBusConnection *connection, const char *name, RBMprisPlugin *plugin)
{
	rb_debug ("successfully acquired dbus name %s", name);
}

static void
name_lost_cb (GDBusConnection *connection, const char *name, RBMprisPlugin *plugin)
{
	rb_debug ("lost dbus name %s", name);
}

static void
impl_activate (RBPlugin *bplugin,
	       RBShell *shell)
{
	RBMprisPlugin *plugin;
	GtkUIManager *ui_manager;
	GDBusInterfaceInfo *ifaceinfo;
	GError *error = NULL;

	rb_debug ("activating MPRIS plugin");

	plugin = RB_MPRIS_PLUGIN (bplugin);
	g_object_get (shell,
		      "shell-player", &plugin->player,
		      "ui-manager", &ui_manager,
		      "db", &plugin->db,
		      NULL);
	plugin->shell = g_object_ref (shell);

	plugin->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	if (error != NULL) {
		g_warning ("Unable to connect to D-Bus session bus: %s", error->message);
		return;
	}

	/* parse introspection data */
	plugin->node_info = g_dbus_node_info_new_for_xml (mpris_introspection_xml, &error);
	if (error != NULL) {
		g_warning ("Unable to read MPRIS interface specificiation: %s", error->message);
		return;
	}

	/* register root interface */
	ifaceinfo = g_dbus_node_info_lookup_interface (plugin->node_info, MPRIS_ROOT_INTERFACE);
	plugin->root_id = g_dbus_connection_register_object (plugin->connection,
							     MPRIS_OBJECT_NAME,
							     ifaceinfo,
							     &root_vtable,
							     plugin,
							     NULL,
							     &error);
	if (error != NULL) {
		g_warning ("unable to register MPRIS root interface: %s", error->message);
		g_error_free (error);
	}

	/* register player interface */
	ifaceinfo = g_dbus_node_info_lookup_interface (plugin->node_info, MPRIS_PLAYER_INTERFACE);
	plugin->player_id = g_dbus_connection_register_object (plugin->connection,
							       MPRIS_OBJECT_NAME,
							       ifaceinfo,
							       &player_vtable,
							       plugin,
							       NULL,
							       &error);
	if (error != NULL) {
		g_warning ("Unable to register MPRIS player interface: %s", error->message);
		g_error_free (error);
	}

	/* connect signal handlers for stuff */
	g_signal_connect_object (plugin->player,
				 "notify::play-order",
				 G_CALLBACK (play_order_changed_cb),
				 plugin, 0);
	g_signal_connect_object (plugin->player,
				 "notify::volume",
				 G_CALLBACK (volume_changed_cb),
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
	g_signal_connect_object (plugin->db,
				 "entry-changed",
				 G_CALLBACK (entry_changed_cb),
				 plugin, 0);
	g_signal_connect_object (plugin->player,
				 "playing-source-changed",
				 G_CALLBACK (playing_source_changed_cb),
				 plugin, 0);
	g_signal_connect_object (plugin->player,
				 "elapsed-nano-changed",
				 G_CALLBACK (elapsed_nano_changed_cb),
				 plugin, 0);

	/*
	 * This is a pretty awful hack.  The shell player should expose this
	 * information as properties, and we should bind those to the actions
	 * elsewhere.
	 */

	plugin->next_action = gtk_ui_manager_get_action (ui_manager, "/MenuBar/ControlMenu/ControlNextMenu");
	plugin->prev_action = gtk_ui_manager_get_action (ui_manager, "/MenuBar/ControlMenu/ControlPreviousMenu");

	g_signal_connect_object (plugin->next_action,
				 "notify::sensitive",
				 G_CALLBACK (next_action_sensitive_cb),
				 plugin, 0);
	g_signal_connect_object (plugin->prev_action,
				 "notify::sensitive",
				 G_CALLBACK (prev_action_sensitive_cb),
				 plugin, 0);
	g_object_unref (ui_manager);

	plugin->name_own_id = g_bus_own_name (G_BUS_TYPE_SESSION,
					      MPRIS_BUS_NAME_PREFIX ".rhythmbox",
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

	if (plugin->root_id != 0) {
		g_dbus_connection_unregister_object (plugin->connection, plugin->root_id);
		plugin->root_id = 0;
	}
	if (plugin->player_id != 0) {
		g_dbus_connection_unregister_object (plugin->connection, plugin->player_id);
		plugin->player_id = 0;
	}

	/* probably remove signal handlers? */
	if (plugin->player_property_emit_id != 0) {
		g_source_remove (plugin->player_property_emit_id);
		plugin->player_property_emit_id = 0;
	}
	if (plugin->player_property_changes != NULL) {
		g_hash_table_destroy (plugin->player_property_changes);
		plugin->player_property_changes = NULL;
	}

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

	if (plugin->connection != NULL) {
		g_object_unref (plugin->connection);
		plugin->connection = NULL;
	}
}


static void
rb_mpris_plugin_class_init (RBMprisPluginClass *klass)
{
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
}
