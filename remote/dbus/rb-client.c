/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006 Jonathan Matthew
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#include <locale.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "rb-debug.h"

static gboolean debug = FALSE;

static gboolean no_start = FALSE;
static gboolean quit = FALSE;
static gboolean check_running = FALSE;

static gboolean no_present = FALSE;

static gboolean next = FALSE;
static gboolean previous = FALSE;
static gint32 seek = 0;

static gboolean play = FALSE;
static gboolean do_pause = FALSE;
static gboolean play_pause = FALSE;
static gboolean stop = FALSE;

static gboolean enqueue = FALSE;

static gboolean clear_queue = FALSE;

static gchar *select_source = NULL;
static gchar *activate_source = NULL;
static gchar *play_source = NULL;
static gchar *play_uri = NULL;
static gboolean print_playing = FALSE;
static gchar *print_playing_format = NULL;

static gdouble set_volume = -1.0;
static gboolean volume_up = FALSE;
static gboolean volume_down = FALSE;
static gboolean print_volume = FALSE;
/*static gboolean mute = FALSE;
static gboolean unmute = FALSE; */
static gdouble set_rating = -1.0;

static gchar **other_stuff = NULL;

static GOptionEntry args[] = {
	{ "debug", 0, 0, G_OPTION_ARG_NONE, &debug, NULL, NULL },

	{ "no-start", 0, 0, G_OPTION_ARG_NONE, &no_start, N_("Don't start a new instance of Rhythmbox"), NULL },
	{ "quit", 0, 0, G_OPTION_ARG_NONE, &quit, N_("Quit Rhythmbox"), NULL },
	{ "check-running", 0, 0, G_OPTION_ARG_NONE, &check_running, N_("Check if Rhythmbox is already running"), NULL },

	{ "no-present", 0, 0, G_OPTION_ARG_NONE, &no_present, N_("Don't present an existing Rhythmbox window"), NULL },

	{ "next", 0, 0, G_OPTION_ARG_NONE, &next, N_("Jump to next song"), NULL },
	{ "previous", 0, 0, G_OPTION_ARG_NONE, &previous, N_("Jump to previous song"), NULL },
	{ "seek", 0, 0, G_OPTION_ARG_INT64, &seek, N_("Seek in current track"), NULL },

	{ "play", 0, 0, G_OPTION_ARG_NONE, &play, N_("Resume playback if currently paused"), NULL },
	{ "pause", 0, 0, G_OPTION_ARG_NONE, &do_pause, N_("Pause playback if currently playing"), NULL },
	{ "play-pause", 0, 0, G_OPTION_ARG_NONE, &play_pause, N_("Toggle play/pause mode"), NULL },
/*	{ "stop", 0, 0, G_OPTION_ARG_NONE, &stop, N_("Stop playback"), NULL }, */

	{ "play-uri", 0, 0, G_OPTION_ARG_FILENAME, &play_uri, N_("Play a specified URI, importing it if necessary"), N_("URI to play")},
	{ "enqueue", 0, 0, G_OPTION_ARG_NONE, &enqueue, N_("Add specified tracks to the play queue"), NULL },
	{ "clear-queue", 0, 0, G_OPTION_ARG_NONE, &clear_queue, N_("Empty the play queue before adding new tracks"), NULL },

	{ "print-playing", 0, 0, G_OPTION_ARG_NONE, &print_playing, N_("Print the title and artist of the playing song"), NULL },
	{ "print-playing-format", 0, 0, G_OPTION_ARG_STRING, &print_playing_format, N_("Print formatted details of the song"), NULL },
	{ "select-source", 0, 0, G_OPTION_ARG_STRING, &select_source, N_("Select the source matching the specified URI"), N_("Source to select")},
	{ "activate-source", 0, 0, G_OPTION_ARG_STRING, &activate_source, N_("Activate the source matching the specified URI"), N_("Source to activate")},
	{ "play-source", 0, 0, G_OPTION_ARG_STRING, &play_source, N_("Play from the source matching the specified URI"), N_("Source to play from")},

	{ "set-volume", 0, 0, G_OPTION_ARG_DOUBLE, &set_volume, N_("Set the playback volume"), NULL },
	{ "volume-up", 0, 0, G_OPTION_ARG_NONE, &volume_up, N_("Increase the playback volume"), NULL },
	{ "volume-down", 0, 0, G_OPTION_ARG_NONE, &volume_down, N_("Decrease the playback volume"), NULL },
	{ "print-volume", 0, 0, G_OPTION_ARG_NONE, &print_volume, N_("Print the current playback volume"), NULL },
/*	{ "mute", 0, 0, G_OPTION_ARG_NONE, &mute, N_("Mute playback"), NULL },
	{ "unmute", 0, 0, G_OPTION_ARG_NONE, &unmute, N_("Unmute playback"), NULL }, */
	{ "set-rating", 0, 0, G_OPTION_ARG_DOUBLE, &set_rating, N_("Set the rating of the current song"), NULL },

	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &other_stuff, NULL, NULL },

	{ NULL }
};

static gboolean
annoy (GError **error)
{
	if (*error) {
		g_warning ("%s", (*error)->message);
		g_clear_error (error);
		return TRUE;
	}

	return FALSE;
}


static char *
rb_make_duration_string (gint64 duration)
{
	char *str;
	int hours, minutes, seconds;

	duration /= G_USEC_PER_SEC;
	hours = duration / (60 * 60);
	minutes = (duration - (hours * 60 * 60)) / 60;
	seconds = duration % 60;

	if (hours == 0 && minutes == 0 && seconds == 0)
		str = g_strdup (_("Unknown"));
	else if (hours == 0)
		str = g_strdup_printf (_("%d:%02d"), minutes, seconds);
	else
		str = g_strdup_printf (_("%d:%02d:%02d"), hours, minutes, seconds);

	return str;
}

/*
 * Parse a filename pattern and replace markers with values from a TrackDetails
 * structure.
 *
 * Valid markers so far are:
 * %at -- album title
 * %aa -- album artist
 * %aA -- album artist (lowercase)
 * %as -- album artist sortname
 * %aS -- album artist sortname (lowercase)
 * %ay -- album year
 * %ag -- album genre
 * %aG -- album genre (lowercase)
 * %an -- album disc number
 * %aN -- album disc number, zero padded
 * %tn -- track number (i.e 8)
 * %tN -- track number, zero padded (i.e 08)
 * %tt -- track title
 * %ta -- track artist
 * %tA -- track artist (lowercase)
 * %ts -- track artist sortname
 * %tS -- track artist sortname (lowercase)
 * %td -- track duration
 * %te -- track elapsed time
 * %tb -- track bitrate
 * %st -- stream title
 */
static char *
parse_pattern (const char *pattern, GHashTable *properties, gint64 elapsed)
{
	/* p is the pattern iterator, i is a general purpose iterator */
	const char *p;
	char *temp;
	GString *s;

	if (pattern == NULL || pattern[0] == 0)
		return g_strdup (" ");

	s = g_string_new (NULL);

	p = pattern;
	while (*p) {
		char *string = NULL;
		const char **strv = NULL;
		GVariant *value = NULL;

		/* If not a % marker, copy and continue */
		if (*p != '%') {
			g_string_append_c (s, *p++);
			/* Explicit increment as we continue past the increment */
			continue;
		}

		/* Is a % marker, go to next and see what to do */
		switch (*++p) {
		case '%':
			/*
			 * Literal %
			 */
			g_string_append_c (s, '%');
			break;
		case 'a':
			/*
			 * Album tag
			 */
			switch (*++p) {
			case 't':
				value = g_hash_table_lookup (properties, "xesam:album");
				if (value)
					string = g_variant_dup_string (value, NULL);
				break;
			case 'T':
				value = g_hash_table_lookup (properties, "xesam:album");
				if (value)
					string = g_utf8_strdown (g_variant_get_string (value, NULL), -1);
				break;
			case 'a':
				value = g_hash_table_lookup (properties, "xesam:artist");
				if (value) {
					strv = g_variant_get_strv (value, NULL);
					string = g_strdup (strv[0]);
				}
				break;
			case 'A':
				value = g_hash_table_lookup (properties, "xesam:artist");
				if (value) {
					strv = g_variant_get_strv (value, NULL);
					string = g_utf8_strdown (strv[0], -1);
				}
				break;
			case 's':
				value = g_hash_table_lookup (properties, "rhythmbox:albumArtistSortname");
				if (value)
					string = g_variant_dup_string (value, NULL);
				break;
			case 'S':
				value = g_hash_table_lookup (properties, "rhythmbox:albumArtistSortname");
				if (value)
					string = g_utf8_strdown (g_variant_get_string (value, NULL), -1);
				break;
			case 'y':
				/* Release year */
				value = g_hash_table_lookup (properties, "xesam:contentCreated");
				if (value) {
					const char *iso8601;
					GTimeVal tv;

					iso8601 = g_variant_get_string (value, NULL);
					if (g_time_val_from_iso8601 (iso8601, &tv)) {
						GDate d;
						g_date_set_time_val (&d, &tv);

						string = g_strdup_printf ("%u", g_date_get_year (&d));
					}
				}
				break;
				/* Disc number */
			case 'n':
				value = g_hash_table_lookup (properties, "xesam:discNumber");
				if (value)
					string = g_strdup_printf ("%u", g_variant_get_int32 (value));
				break;
			case 'N':
				value = g_hash_table_lookup (properties, "xesam:discNumber");
				if (value)
					string = g_strdup_printf ("%02u", g_variant_get_int32 (value));
				break;
				/* genre */
			case 'g':
				value = g_hash_table_lookup (properties, "xesam:genre");
				if (value) {
					strv = g_variant_get_strv (value, NULL);
					string = g_strdup (strv[0]);
				}
				break;
			case 'G':
				value = g_hash_table_lookup (properties, "xesam:genre");
				if (value) {
					strv = g_variant_get_strv (value, NULL);
					string = g_utf8_strdown (strv[0], -1);
				}
				break;
			default:
				string = g_strdup_printf ("%%a%c", *p);
			}

			break;

		case 't':
			/*
			 * Track tag
			 */
			switch (*++p) {
			case 't':
				value = g_hash_table_lookup (properties, "rhythmbox:streamTitle");
				if (value == NULL)
					value = g_hash_table_lookup (properties, "xesam:title");
				if (value)
					string = g_variant_dup_string (value, NULL);
				break;
			case 'T':
				value = g_hash_table_lookup (properties, "rhythmbox:streamTitle");
				if (value == NULL)
					value = g_hash_table_lookup (properties, "xesam:title");
				if (value)
					string = g_utf8_strdown (g_variant_get_string (value, NULL), -1);
				break;
			case 'a':
				value = g_hash_table_lookup (properties, "xesam:artist");
				if (value) {
					strv = g_variant_get_strv (value, NULL);
					string = g_strdup (strv[0]);
				}
				break;
			case 'A':
				value = g_hash_table_lookup (properties, "xesam:artist");
				if (value) {
					strv = g_variant_get_strv (value, NULL);
					string = g_utf8_strdown (strv[0], -1);
				}
				break;
			case 's':
				value = g_hash_table_lookup (properties, "rhythmbox:artistSortname");
				if (value)
					string = g_variant_dup_string (value, NULL);
				break;
			case 'S':
				value = g_hash_table_lookup (properties, "rhythmbox:artistSortname");
				if (value)
					string = g_utf8_strdown (g_variant_get_string (value, NULL), -1);
				break;
			case 'n':
				/* Track number */
				value = g_hash_table_lookup (properties, "xesam:trackNumber");
				if (value)
					string = g_strdup_printf ("%u", g_variant_get_int32 (value));
				break;
			case 'N':
				/* Track number, zero-padded */
				value = g_hash_table_lookup (properties, "xesam:trackNumber");
				if (value)
					string = g_strdup_printf ("%02u", g_variant_get_int32 (value));
				break;
			case 'd':
				/* Track duration */
				value = g_hash_table_lookup (properties, "mpris:length");
				if (value)
					string = rb_make_duration_string (g_variant_get_int64 (value));
				break;
			case 'e':
				/* Track elapsed time */
				string = rb_make_duration_string (elapsed);
				break;
			case 'b':
				/* Track bitrate */
				value = g_hash_table_lookup (properties, "xesam:audioBitrate");
				if (value)
					string = g_strdup_printf ("%u", g_variant_get_int32 (value) / 1024);
				break;

			default:
				string = g_strdup_printf ("%%t%c", *p);
 			}

			break;

		case 's':
			/*
			 * Stream tag
			 */
			switch (*++p) {
			case 't':
				value = g_hash_table_lookup (properties, "rhythmbox:streamTitle");
				if (value) {
					value = g_hash_table_lookup (properties, "xesam:title");
					if (value) {
						string = g_variant_dup_string (value, NULL);
					}
				}
				break;
			default:
				string = g_strdup_printf ("%%s%c", *p);
 			}
			break;

		default:
			string = g_strdup_printf ("%%%c", *p);
		}

		if (string)
			g_string_append (s, string);
		g_free (string);

		++p;
	}

	temp = s->str;
	g_string_free (s, FALSE);
	return temp;
}


static GHashTable *
get_playing_song_info (GDBusProxy *mpris)
{
	GHashTable *properties;
	GVariant *prop;
	GVariant *metadata;
	GVariantIter iter;
	GVariant *value;
	char *key;
	GError *error = NULL;

	prop = g_dbus_proxy_call_sync (mpris,
				       "org.freedesktop.DBus.Properties.Get",
				       g_variant_new ("(ss)", "org.mpris.MediaPlayer2.Player", "Metadata"),
				       G_DBUS_CALL_FLAGS_NONE,
				       -1,
				       NULL,
				       &error);
	if (annoy (&error)) {
		return NULL;
	}

	g_variant_get (prop, "(v)", &metadata);

	properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_variant_unref);
	g_variant_iter_init (&iter, metadata);
	while (g_variant_iter_loop (&iter, "{sv}", &key, &value)) {
		g_hash_table_insert (properties, g_strdup (key), g_variant_ref (value));
	}

	g_variant_unref (prop);
	return properties;
}

static void
print_playing_song (GDBusProxy *mpris, const char *format)
{
	GHashTable *properties;
	GVariant *v;
	gint64 elapsed = 0;
	char *string;

	properties = get_playing_song_info (mpris);
	if (properties == NULL) {
		g_print ("%s\n", _("Not playing"));
		return;
	}

	v = g_dbus_proxy_get_cached_property (mpris, "Position");
	if (v != NULL) {
		elapsed = g_variant_get_int64 (v);
		g_variant_unref (v);
	}

	string = parse_pattern (format, properties, elapsed);
	g_print ("%s\n", string);
	g_hash_table_destroy (properties);
	g_free (string);
}

static void
print_playing_song_default (GDBusProxy *mpris)
{
	GHashTable *properties;
	char *string;

	properties = get_playing_song_info (mpris);
	if (properties == NULL) {
		g_print ("%s\n", _("Not playing"));
		return;
	}

	if (g_hash_table_lookup (properties, "rhythmbox:streamTitle") != NULL) {
		string = parse_pattern ("%st (%tt)", properties, 0);
	} else {
		string = parse_pattern ("%ta - %tt", properties, 0);
	}

	g_print ("%s\n", string);
	g_hash_table_destroy (properties);
	g_free (string);
}

static void
rate_song (GDBusProxy *mpris, gdouble song_rating)
{
	GHashTable *properties;
	GVariantBuilder props;
	GVariant *v;
	GError *error = NULL;

	properties = get_playing_song_info (mpris);
	if (properties == NULL) {
		rb_debug ("can't set rating when not playing");
		return;
	}

	v = g_hash_table_lookup (properties, "xesam:url");
	if (v == NULL) {
		rb_debug ("can't set rating, no url");
		return;
	}

	g_variant_builder_init (&props, G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_add (&props, "{sv}", "rating", g_variant_new_double (song_rating));

	g_dbus_connection_call_sync (g_dbus_proxy_get_connection (mpris),
				     "org.gnome.Rhythmbox3",
				     "/org/gnome/Rhythmbox3/RhythmDB",
				     "org.gnome.Rhythmbox3.RhythmDB",
				     "SetEntryProperties",
				     g_variant_new ("(sa{sv})", g_variant_get_string (v, NULL), &props),
				     NULL,
				     G_DBUS_CALL_FLAGS_NONE,
				     -1,
				     NULL,
				     &error);
	if (error != NULL) {
		g_warning ("Error setting rating on %s: %s",
			   g_variant_get_string (v, NULL),
			   error->message);
		g_clear_error (&error);
	}
	g_hash_table_destroy (properties);
}

static void
state_changed_cb (GActionGroup *action, const char *action_name, GVariant *state, GMainLoop *loop)
{
	if (g_strcmp0 (action_name, "LoadURI") == 0) {
		gboolean loaded, scanned;

		g_variant_get (state, "(bb)", &loaded, &scanned);
		if (loaded && scanned) {
			/* give it a tiny bit longer to populate sources etc. */
			g_timeout_add (1500, (GSourceFunc) g_main_loop_quit, loop);
		}
	}
}

static void
state_changed_signal_cb (GDBusProxy *proxy, const char *sender_name, const char *signal_name, GVariant *parameters, GMainLoop *loop)
{
	const char *action;
	GVariant *state;
	if (g_strcmp0 (signal_name, "StateChanged") != 0) {
		return;
	}

	g_variant_get (parameters, "(sv)", &action, &state);
	if (g_strcmp0 (action, "LoadURI") == 0) {
		GApplication *app;
		app = g_object_get_data (G_OBJECT (proxy), "actual-app");
		state_changed_cb (G_ACTION_GROUP (app), action, state, loop);
	}
	g_variant_unref (state);
}

static gboolean
proxy_has_name_owner (GDBusProxy *proxy)
{
	gboolean has_owner;
	char *owner = g_dbus_proxy_get_name_owner (proxy);

	has_owner = (owner != NULL);
	g_free (owner);
	return has_owner;
}


int
main (int argc, char **argv)
{
	GOptionContext *context;
	GError *error = NULL;
	GDBusConnection *bus;
	GDBusProxy *mpris;
	GDBusProxy *queue;
	GApplication *app;
	gboolean loaded;
	gboolean scanned;
	GVariant *state;

#ifdef ENABLE_NLS
	/* initialize i18n */
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif
	/* setup */
	setlocale (LC_ALL, "");
	g_type_init ();
	g_set_prgname ("rhythmbox-client");

	/* parse arguments */
	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, args, NULL);
	g_option_context_parse (context, &argc, &argv, &error);
	if (annoy (&error))
		exit (1);

	rb_debug_init (debug);

	app = g_application_new ("org.gnome.Rhythmbox3", G_APPLICATION_IS_LAUNCHER);
	if (g_application_register (app, NULL, &error) == FALSE) {
		if (check_running) {
			rb_debug ("no running instance found");
			exit (2);
		} else if (quit) {
			rb_debug ("no existing instance to quit");
			exit (0);
		}

		rb_debug ("uh.. what?");
		exit (0);
	}


	/* are we just checking if it's running? */
	if (check_running) {
		rb_debug ("running instance found");
		exit (0);
	}

	/* wait until it's ready to accept control */
	state = g_action_group_get_action_state (G_ACTION_GROUP (app), "LoadURI");
	if (state == NULL) {
		rb_debug ("couldn't get app startup state");
		exit (0);
	}

	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	g_variant_get (state, "(bb)", &loaded, &scanned);
	if ((loaded && scanned) == FALSE) {
		GMainLoop *loop;
		GDBusProxy *app_proxy;

		rb_debug ("waiting for app startup");
		loop = g_main_loop_new (NULL, FALSE);
		g_signal_connect (app, "action-state-changed", G_CALLBACK (state_changed_cb), loop);

		/* dbus implementation of GApplication doesn't do action state updates yet */
		app_proxy = g_dbus_proxy_new_sync (bus, G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START, NULL,
						   "org.gnome.Rhythmbox3",
						   "/org/gnome/Rhythmbox3",
						   "org.gtk.Actions",
						   NULL,
						   &error);
		if (app_proxy == NULL || proxy_has_name_owner (app_proxy) == FALSE) {
			g_warning ("unable to wait for app startup: %s", error->message);
			g_clear_error (&error);
		} else {
			g_object_set_data (G_OBJECT (app_proxy), "actual-app", app);
			g_signal_connect (app_proxy, "g-signal", G_CALLBACK (state_changed_signal_cb), loop);
			g_main_loop_run (loop);
			rb_debug ("app is now started enough");
		}
	}

	/* create proxies */
	mpris = g_dbus_proxy_new_sync (bus,
				       G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
				       NULL,
				       "org.mpris.MediaPlayer2.rhythmbox",
				       "/org/mpris/MediaPlayer2",
				       "org.mpris.MediaPlayer2.Player",
				       NULL,
				       &error);
	if (mpris == NULL || proxy_has_name_owner (mpris) == FALSE) {
		g_warning ("MPRIS D-Bus interface not available, some things won't work");
		if (next || previous || (seek != 0) || play || do_pause || play_pause || stop || volume_up || volume_down || (set_volume > -0.01)) {
			exit (1);
		}
	}

	queue = g_dbus_proxy_new_sync (bus,
				       G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
				       NULL,
				       "org.gnome.Rhythmbox3",
				       "/org/gnome/Rhythmbox3/PlayQueue",
				       "org.gnome.Rhythmbox3.PlayQueue",
				       NULL,
				       &error);
	if (queue == NULL || proxy_has_name_owner (queue) == FALSE) {
		g_warning ("Play queue interface not available, some things won't work");
		if (enqueue || clear_queue) {
			exit (1);
		}
	}

	/* activate or quit */
	if (quit) {
		rb_debug ("quitting existing instance");
		g_action_group_activate_action (G_ACTION_GROUP (app), "Quit", NULL);
		exit (0);
	}

	/* don't present if we're doing something else */
	if (next || previous || (seek != 0) ||
	    clear_queue ||
	    play_uri || other_stuff ||
	    play || do_pause || play_pause || stop ||
	    print_playing || print_playing_format ||
	    (set_volume > -0.01) || volume_up || volume_down || print_volume /*|| mute || unmute*/ || (set_rating > -0.01))
		no_present = TRUE;

	/* present */
	if (!no_present) {
		g_application_activate (app);
	}

	/* set song rating */
	if (set_rating >= 0.0 && set_rating <= 5.0) {
		rb_debug ("rate song");

		rate_song (mpris, set_rating);
	}

	/* skip to next or previous track */
	if (next) {
		rb_debug ("next track");
		g_dbus_proxy_call_sync (mpris, "Next", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
		annoy (&error);
	} else if (previous) {
		rb_debug ("previous track");
		g_dbus_proxy_call_sync (mpris, "Previous", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
		annoy (&error);
	}

	/* seek in track */
	if (seek != 0) {
		GHashTable *properties;
		rb_debug ("seek");

		properties = get_playing_song_info (mpris);
		if (properties != NULL) {
			GVariant *v = g_hash_table_lookup (properties, "mpris:trackid");
			if (v != NULL) {
				g_dbus_proxy_call_sync (mpris,
							"SetPosition",
							g_variant_new ("(ox)", g_variant_get_string (v, NULL), seek),
							G_DBUS_CALL_FLAGS_NONE,
							-1,
							NULL,
							&error);
				annoy (&error);
			}
		}
	}

	/* add/enqueue */
	if (clear_queue) {
		g_dbus_proxy_call_sync (queue, "ClearQueue", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
		annoy (&error);
	}
	if (other_stuff) {
		int i;
		for (i = 0; other_stuff[i] != NULL; i++) {
			GFile *file;
			char *fileuri;

			file = g_file_new_for_commandline_arg (other_stuff[i]);
			fileuri = g_file_get_uri (file);
			if (fileuri == NULL) {
				g_warning ("couldn't convert \"%s\" to a URI", other_stuff[i]);
				continue;
			}

			if (enqueue) {
				rb_debug ("enqueueing %s", fileuri);
				g_dbus_proxy_call_sync (queue, "AddToQueue", g_variant_new ("(s)", fileuri), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
				annoy (&error);
			} else {
				rb_debug ("importing %s", fileuri);
				g_action_group_activate_action (G_ACTION_GROUP (app), "LoadURI", g_variant_new ("(sb)", fileuri, FALSE));
			}
			g_free (fileuri);
			g_object_unref (file);
		}
	}

	/* select/activate/play source */
	if (select_source) {
		rb_debug ("selecting source %s", select_source);
		g_action_group_activate_action (G_ACTION_GROUP (app), "ActivateSource", g_variant_new ("(su)", select_source, 0));
	} else if (activate_source) {
		rb_debug ("activating source %s", activate_source);
		g_action_group_activate_action (G_ACTION_GROUP (app), "ActivateSource", g_variant_new ("(su)", activate_source, 1));
	} else if (play_source) {
		rb_debug ("playing source %s", play_source);
		g_action_group_activate_action (G_ACTION_GROUP (app), "ActivateSource", g_variant_new ("(su)", play_source, 2));
	}

	/* play uri */
	if (play_uri) {
		GFile *file;
		char *fileuri;

		file = g_file_new_for_commandline_arg (play_uri);
		fileuri = g_file_get_uri (file);
		if (fileuri == NULL) {
			g_warning ("couldn't convert \"%s\" to a URI", play_uri);
		} else {
			rb_debug ("loading and playing %s", fileuri);
			g_action_group_activate_action (G_ACTION_GROUP (app), "LoadURI", g_variant_new ("(sb)", fileuri, TRUE));
			annoy (&error);
		}
		g_free (fileuri);
		g_object_unref (file);
	}

	/* play/pause/stop */
	if (mpris) {
		GVariant *v;
		gboolean is_playing = FALSE;

		v = g_dbus_proxy_get_cached_property (mpris, "PlaybackStatus");
		if (v != NULL) {
			is_playing = (g_strcmp0 (g_variant_get_string (v, NULL), "Playing") == 0);
			g_variant_unref (v);
		}

		if (play || do_pause || play_pause) {
			if (is_playing != play || play_pause) {
				rb_debug ("calling PlayPause to change playback state");
				g_dbus_proxy_call_sync (mpris, "PlayPause", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
				annoy (&error);
			} else {
				rb_debug ("no need to change playback state");
			}
		} else if (stop) {
			g_warning ("not implemented yet");
		}
	}

	/* get/set volume, mute/unmute */
	if (set_volume > -0.01) {
		g_dbus_proxy_call_sync (mpris,
					"org.freedesktop.DBus.Properties.Set",
					g_variant_new ("(ssv)", "org.mpris.MediaPlayer2.Player", "Volume", g_variant_new_double (set_volume)),
					G_DBUS_CALL_FLAGS_NONE,
					-1,
					NULL,
					&error);
		annoy (&error);
	} else if (volume_up || volume_down) {
		GVariant *v;

		v = g_dbus_proxy_get_cached_property (mpris, "Volume");
		if (v != NULL) {

			set_volume = g_variant_get_double (v) + (volume_up ? 0.1 : -0.1);
			g_dbus_proxy_call_sync (mpris,
						"org.freedesktop.DBus.Properties.Set",
						g_variant_new ("(ssv)", "org.mpris.MediaPlayer2.Player", "Volume", g_variant_new_double (set_volume)),
						G_DBUS_CALL_FLAGS_NONE,
						-1,
						NULL,
						&error);
			annoy (&error);

			g_variant_unref (v);
		}
	}
	/* no mute for now? */
	/*
	} else if (unmute || mute) {
		org_gnome_Rhythmbox_Player_set_mute (player_proxy, unmute ? FALSE : TRUE, &error);
		annoy (&error);
	}
	*/

	if (print_volume) {
		gdouble volume = 1.0;
		GVariant *v = g_dbus_proxy_get_cached_property (mpris, "Volume");
		if (v != NULL) {
			volume = g_variant_get_double (v);
			g_variant_unref (v);
		}
		g_print (_("Playback volume is %f.\n"), volume);
	}

	/* print playing song */
	if (print_playing_format) {
		print_playing_song (mpris, print_playing_format);
	} else if (print_playing) {
		print_playing_song_default (mpris);
	}

	if (mpris) {
		g_object_unref (mpris);
	}

	g_dbus_connection_flush_sync (bus, NULL, NULL);
	g_option_context_free (context);

	return 0;
}
