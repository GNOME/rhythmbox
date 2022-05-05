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
#include <termios.h>
#include <math.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>

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

static gboolean repeat = FALSE;
static gboolean no_repeat = FALSE;
static gboolean shuffle = FALSE;
static gboolean no_shuffle = FALSE;

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

static gboolean interactive = FALSE;

static gchar **other_stuff = NULL;

static GMainLoop *mainloop = NULL;

G_GNUC_NORETURN static gboolean show_version_cb (const gchar *option_name, const gchar *value, gpointer data, GError **error);

static GOptionEntry args[] = {
	{ "debug", 0, 0, G_OPTION_ARG_NONE, &debug, NULL, NULL },
	{ "version", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, show_version_cb, N_("Show the version of the program"), NULL },

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
	{ "stop", 0, 0, G_OPTION_ARG_NONE, &stop, N_("Stop playback"), NULL },

	{ "play-uri", 0, 0, G_OPTION_ARG_FILENAME, &play_uri, N_("Play a specified URI, importing it if necessary"), N_("URI to play")},
	{ "enqueue", 0, 0, G_OPTION_ARG_NONE, &enqueue, N_("Add specified tracks to the play queue"), NULL },
	{ "clear-queue", 0, 0, G_OPTION_ARG_NONE, &clear_queue, N_("Empty the play queue before adding new tracks"), NULL },

	{ "print-playing", 0, 0, G_OPTION_ARG_NONE, &print_playing, N_("Print the title and artist of the playing song"), NULL },
	{ "print-playing-format", 0, 0, G_OPTION_ARG_STRING, &print_playing_format, N_("Print formatted details of the song"), NULL },
	{ "select-source", 0, 0, G_OPTION_ARG_STRING, &select_source, N_("Select the source matching the specified URI"), N_("Source to select")},
	{ "activate-source", 0, 0, G_OPTION_ARG_STRING, &activate_source, N_("Activate the source matching the specified URI"), N_("Source to activate")},
	{ "play-source", 0, 0, G_OPTION_ARG_STRING, &play_source, N_("Play from the source matching the specified URI"), N_("Source to play from")},

	{ "repeat", 0, 0, G_OPTION_ARG_NONE, &repeat, N_("Enable repeat playback order"), NULL },
	{ "no-repeat", 0, 0, G_OPTION_ARG_NONE, &no_repeat, N_("Disable repeat playback order"), NULL },
	{ "shuffle", 0, 0, G_OPTION_ARG_NONE, &shuffle, N_("Enable shuffle playback order"), NULL },
	{ "no-shuffle", 0, 0, G_OPTION_ARG_NONE, &no_shuffle, N_("Disable shuffle playback order"), NULL },

	{ "set-volume", 0, 0, G_OPTION_ARG_DOUBLE, &set_volume, N_("Set the playback volume"), NULL },
	{ "volume-up", 0, 0, G_OPTION_ARG_NONE, &volume_up, N_("Increase the playback volume"), NULL },
	{ "volume-down", 0, 0, G_OPTION_ARG_NONE, &volume_down, N_("Decrease the playback volume"), NULL },
	{ "print-volume", 0, 0, G_OPTION_ARG_NONE, &print_volume, N_("Print the current playback volume"), NULL },
/*	{ "mute", 0, 0, G_OPTION_ARG_NONE, &mute, N_("Mute playback"), NULL },
	{ "unmute", 0, 0, G_OPTION_ARG_NONE, &unmute, N_("Unmute playback"), NULL }, */
	{ "set-rating", 0, 0, G_OPTION_ARG_DOUBLE, &set_rating, N_("Set the rating of the current song"), NULL },
	{ "interactive", 'i', 0, G_OPTION_ARG_NONE, &interactive, N_("Start interactive mode"), NULL },

	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &other_stuff, NULL, NULL },

	{ NULL }
};

#if defined(HAVE_CFMAKERAW)

typedef struct {
	GApplication *app;
	GDBusProxy *mpris_player;
	GDBusProxy *mpris_playlists;

	char *playing_track;
	gdouble volume;
	gint64 position;
} InteractData;

static void interact_quit (InteractData *data, int ch);
static void interact_next (InteractData *data, int ch);
static void interact_prev (InteractData *data, int ch);
static void interact_playpause (InteractData *data, int ch);
static void interact_print_state (InteractData *data, int ch);
static void interact_volume (InteractData *data, int ch);
static void interact_help (InteractData *data, int ch);

struct {
	int ch;
	void (*handler)(InteractData *, int);
	const char *help;
} interact_keys[] = {
	{ 'n', 	interact_next,		N_("n - Next track") },
	{ 'p', 	interact_prev,		N_("p - Previous track") },
	{ ' ', 	interact_playpause,	N_("space - Play/pause") },
	{ 's', 	interact_print_state,	N_("s - Show playing track details") },
	{ 'v', 	interact_volume,	N_("v - Decrease volume") },
	{ 'V', 	interact_volume,	N_("V - Increase volume") },
	{ '?', 	interact_help,		NULL },
	{ 'h', 	interact_help,		N_("h/? - Help") },
	{ 'q', 	interact_quit,		N_("q - Quit") },
	{ 3, 	interact_quit, 		NULL },	/* ctrl-c */
	{ 4, 	interact_quit, 		NULL }, /* ctrl-d */
	/* seeking? */
	/* quit rhythmbox? */
};

#endif

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

G_GNUC_NORETURN static gboolean
show_version_cb (const gchar *option_name,
		 const gchar *value,
		 gpointer     data,
		 GError     **error)
{
	g_print ("%s %s\n", PACKAGE, VERSION);

	exit (0);
}

static char *
rb_make_duration_string (gint64 duration, gboolean show_zero)
{
	char *str;
	int hours, minutes, seconds;

	duration /= G_USEC_PER_SEC;
	hours = duration / (60 * 60);
	minutes = (duration - (hours * 60 * 60)) / 60;
	seconds = duration % 60;

	if (hours == 0 && minutes == 0 && seconds == 0 && show_zero)
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
parse_pattern (const char *pattern, GHashTable *properties, gint64 elapsed, gboolean bold)
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
				value = g_hash_table_lookup (properties, "xesam:albumArtist");
				if (value == NULL)
					value = g_hash_table_lookup (properties, "xesam:artist");
				if (value) {
					strv = g_variant_get_strv (value, NULL);
					string = g_strdup (strv[0]);
				}
				break;
			case 'A':
				value = g_hash_table_lookup (properties, "xesam:albumArtist");
				if (value == NULL)
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
					string = rb_make_duration_string (g_variant_get_int64 (value), TRUE);
				break;
			case 'e':
				/* Track elapsed time */
				string = rb_make_duration_string (elapsed, FALSE);
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

		if (string) {
			if (bold)
				g_string_append (s, "\033[1m");
			g_string_append (s, string);
			if (bold)
				g_string_append (s, "\033[0m");
		}
		g_free (string);

		++p;
	}

	temp = s->str;
	g_string_free (s, FALSE);
	return temp;
}

static GHashTable *
metadata_to_properties (GVariant *metadata)
{
	GHashTable *properties;
	GVariant *value;
	GVariantIter iter;
	char *key;

	properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_variant_unref);
	g_variant_iter_init (&iter, metadata);
	while (g_variant_iter_loop (&iter, "{sv}", &key, &value)) {
		g_hash_table_insert (properties, g_strdup (key), g_variant_ref (value));
	}

	return properties;
}

static GHashTable *
get_playing_song_info (GDBusProxy *mpris)
{
	GHashTable *properties;
	GVariant *prop;
	GVariant *metadata;
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
	properties = metadata_to_properties (metadata);
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

	string = parse_pattern (format, properties, elapsed, FALSE);
	g_print ("%s\n", string);
	g_hash_table_destroy (properties);
	g_free (string);
}

static void
print_playing_song_default (GDBusProxy *mpris)
{
	GHashTable *properties;
	char *string;
	GVariant *v;
	gint64 elapsed = 0;

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

	if (g_hash_table_lookup (properties, "rhythmbox:streamTitle") != NULL) {
		string = parse_pattern ("%st (%tt)", properties, elapsed, FALSE);
	} else {
		string = parse_pattern ("%ta - %tt", properties, elapsed, FALSE);
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

#if defined(HAVE_CFMAKERAW)

static void
interact_quit (InteractData *data, int ch)
{
	g_main_loop_quit (mainloop);
}

static void
interact_next (InteractData *data, int ch)
{
	GError *error = NULL;
	g_dbus_proxy_call_sync (data->mpris_player, "Next", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	annoy (&error);
}

static void
interact_prev (InteractData *data, int ch)
{
	GError *error = NULL;
	g_dbus_proxy_call_sync (data->mpris_player, "Previous", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	annoy (&error);
}

static void
interact_playpause (InteractData *data, int ch)
{
	GError *error = NULL;
	g_dbus_proxy_call_sync (data->mpris_player, "PlayPause", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	annoy (&error);
}

static void
interact_help (InteractData *data, int ch)
{
	int i;

	for (i = 0; i < G_N_ELEMENTS (interact_keys); i++) {
		if (interact_keys[i].help != NULL) {
			g_print ("%s\r\n", _(interact_keys[i].help));
		}
	}
	g_print ("\r\n");
}

static void
interact_volume (InteractData *data, int ch)
{
	gdouble set_volume;
	GError *error = NULL;

	if (data->volume < 0.0) {
		GVariant *prop;
		GVariant *val;
		prop = g_dbus_proxy_call_sync (data->mpris_player,
					       "org.freedesktop.DBus.Properties.Get",
					       g_variant_new ("(ss)", "org.mpris.MediaPlayer2.Player", "Volume"),
					       G_DBUS_CALL_FLAGS_NONE,
					       -1,
					       NULL,
					       &error);
		if (annoy (&error)) {
			return;
		}

		g_variant_get (prop, "(v)", &val);
		data->volume = g_variant_get_double (val);
		g_variant_unref (prop);
	}

	set_volume = data->volume + (ch == 'V' ? 0.1 : -0.1);
	g_dbus_proxy_call_sync (data->mpris_player,
				"org.freedesktop.DBus.Properties.Set",
				g_variant_new ("(ssv)", "org.mpris.MediaPlayer2.Player", "Volume", g_variant_new_double (set_volume)),
				G_DBUS_CALL_FLAGS_NONE,
				-1,
				NULL,
				&error);
	annoy (&error);
}

static gboolean
has_useful_value (GHashTable *properties, const char *property, gboolean multi)
{
	GVariant *var;
	const char *v;

	var = g_hash_table_lookup (properties, property);
	if (var == NULL)
		return FALSE;

	if (multi) {
		const char **strv = g_variant_get_strv (var, NULL);
		v = strv[0];
	} else {
		v = g_variant_get_string (var, NULL);
	}

	return (v != NULL && (g_str_equal (v, _("Unknown")) == FALSE));
}

static char *
now_playing_string (GHashTable *properties)
{
	gboolean has_artist = has_useful_value (properties, "xesam:artist", TRUE);
	gboolean has_album = has_useful_value (properties, "xesam:album", FALSE);

	if (g_hash_table_lookup (properties, "rhythmbox:streamTitle") != NULL) {
		return parse_pattern ("%st (%tt)", properties, 0, TRUE);
	} else if (has_artist && has_album) {
		/* Translators: title by artist from album */
		return parse_pattern (_("%tt by %ta from %at"), properties, 0, TRUE);
	} else if (has_artist) {
		/* Translators: title by artist */
		return parse_pattern (_("%tt by %ta"), properties, 0, TRUE);
	} else if (has_album) {
		/* Translators: title from album */
		return parse_pattern (_("%tt from %ta"), properties, 0, TRUE);
	} else {
		return parse_pattern ("%tt", properties, 0, TRUE);
	}
}

static char *
playing_time_string (GHashTable *properties, gint64 elapsed)
{
	if (g_hash_table_lookup (properties, "mpris:length") != NULL) {
		/* Translators: %te is replaced with elapsed time, %td is replaced with track duration */
		return parse_pattern (_("[%te of %td]"), properties, elapsed, FALSE);
	} else {
		return parse_pattern ("[%te]", properties, elapsed, FALSE);
	}
}

static void
interact_print_state (InteractData *data, int ch)
{
	GHashTable *properties;
	GVariant *prop;
	GVariant *val;
	char *string;
	char *timestr;
	const char *s;
	GError *error = NULL;
	gboolean playing;
	gint64 elapsed;

	prop = g_dbus_proxy_call_sync (data->mpris_player,
				       "org.freedesktop.DBus.Properties.Get",
				       g_variant_new ("(ss)", "org.mpris.MediaPlayer2.Player", "PlaybackStatus"),
				       G_DBUS_CALL_FLAGS_NONE,
				       -1,
				       NULL,
				       &error);
	if (annoy (&error)) {
		return;
	}

	g_variant_get (prop, "(v)", &val);
	s = g_variant_get_string (val, NULL);
	if (g_str_equal(s, "Stopped")) {
		g_print ("%s\r\n", _("Not playing"));
		return;
	}
	playing = g_str_equal (s, "Playing");
	g_variant_unref (prop);

	prop = g_dbus_proxy_call_sync (data->mpris_player,
				       "org.freedesktop.DBus.Properties.Get",
				       g_variant_new ("(ss)", "org.mpris.MediaPlayer2.Player", "Position"),
				       G_DBUS_CALL_FLAGS_NONE,
				       -1,
				       NULL,
				       &error);
	if (annoy (&error)) {
		return;
	}
	g_variant_get (prop, "(v)", &val);
	elapsed = g_variant_get_int64 (val);

	properties = get_playing_song_info (data->mpris_player);
	string = now_playing_string (properties);
	timestr = playing_time_string (properties, elapsed);

	g_print ("%s: %s %s\r\n", playing ? _("Playing") : _("Paused"), string, timestr);
	g_hash_table_destroy (properties);
	g_free (string);
	g_free (timestr);
}


static gboolean
interact_input (GObject *stream, gpointer user_data)
{
	GInputStream *in = G_INPUT_STREAM (stream);
	InteractData *data = user_data;
	GError *error = NULL;
	char b[2];
	int k;

	switch (g_pollable_input_stream_read_nonblocking (G_POLLABLE_INPUT_STREAM (in), b, 1, NULL, &error)) {
	case -1:
		annoy(&error);
		g_main_loop_quit (mainloop);
		break;
	case 0:
		break;
	default:
		for (k = 0; k < G_N_ELEMENTS (interact_keys); k++) {
			if (interact_keys[k].ch == b[0]) {
				interact_keys[k].handler(data, b[0]);
				break;
			}
		}
		break;
	}
	return TRUE;
}

static void
interact_mpris_player_signal (GDBusProxy *proxy, char *sender, char *signal_name, GVariant *parameters, InteractData *data)
{
	if (g_str_equal (signal_name, "Seeked")) {
		gint64 pos;
		char *str;
		g_variant_get (parameters, "(x)", &pos);
		if (llabs(pos - data->position) >= G_USEC_PER_SEC) {
			str = rb_make_duration_string (pos, FALSE);
			g_print (_("Seeked to %s"), str);
			g_print ("\r\n");
			g_free (str);
		}
		data->position = pos;
	}
}

static void
interact_mpris_player_properties (GDBusProxy *proxy, GVariant *changed, GStrv invalidated, InteractData *data)
{
	GVariant *metadata;
	char *status = NULL;
	gdouble volume;

	if (g_variant_lookup (changed, "PlaybackStatus", "s", &status)) {
		if (g_str_equal (status, "Stopped")) {
			g_print (_("Not playing"));
			g_print ("\r\n");
			return;
		}
	}

	metadata = g_variant_lookup_value (changed, "Metadata", NULL);
	if (metadata) {
		GHashTable *properties;
		char *string;

		properties = metadata_to_properties (metadata);
		string = now_playing_string (properties);

		if (data->playing_track == NULL || g_str_equal (string, data->playing_track) == FALSE) {
			char *timestr;
			timestr = playing_time_string (properties, 0);
			g_print (_("Now playing: %s %s"), string, timestr);
			g_print ("\r\n");
			g_free (timestr);
			g_free (data->playing_track);
			data->playing_track = string;
			data->position = 0;
		} else {
			g_free (string);
		}
	} else if (status != NULL) {
		/* include elapsed/duration here? */
		if (g_str_equal (status, "Playing")) {
			g_print (_("Playing"));
		} else if (g_str_equal (status, "Paused")) {
			g_print (_("Paused"));
		} else {
			g_print (_("Unknown playback state: %s"), status);
		}
		g_print ("\r\n");
		g_free (status);
	}

	if (g_variant_lookup (changed, "Volume", "d", &volume)) {
		if (data->volume < -0.1) {
			data->volume = volume;
		} else if (fabs(volume - data->volume) > 0.001) {
			g_print (_("Volume is now %.02f"), volume);
			g_print ("\r\n");
			data->volume = volume;
		}
	}
}

static gboolean
interact_print_state_idle (InteractData *data)
{
	interact_print_state (data, 's');
	return FALSE;
}

static void
interact (InteractData *data)
{
	GInputStream *in;
	GSource *insrc;
	struct termios orig_tt, tt;

	if (mainloop == NULL)
		mainloop = g_main_loop_new (NULL, FALSE);

	tcgetattr(0, &orig_tt);
	tt = orig_tt;
	cfmakeraw(&tt);
	tt.c_lflag &= ~ECHO;
	tcsetattr(0, TCSAFLUSH, &tt);

	g_signal_connect (data->mpris_player, "g-signal", G_CALLBACK (interact_mpris_player_signal), data);
	g_signal_connect (data->mpris_player, "g-properties-changed", G_CALLBACK (interact_mpris_player_properties), data);

	in = g_unix_input_stream_new (0, FALSE);
	insrc = g_pollable_input_stream_create_source (G_POLLABLE_INPUT_STREAM (in), NULL);
	g_source_set_callback (insrc, (GSourceFunc) interact_input, data, NULL);

	/* should print this before dbus setup, really */
	g_print (_("Press 'h' for help."));
	g_print ("\r\n\r\n");
	g_source_attach (insrc, g_main_loop_get_context (mainloop));

	g_idle_add ((GSourceFunc) interact_print_state_idle, data);

	g_main_loop_run (mainloop);

	g_signal_handlers_disconnect_by_func (data->mpris_player, G_CALLBACK (interact_mpris_player_signal), data);
	g_signal_handlers_disconnect_by_func (data->mpris_player, G_CALLBACK (interact_mpris_player_properties), data);

	tcsetattr(0, TCSAFLUSH, &orig_tt);
}

#endif /* HAVE_CFMAKERAW */

static void
check_loaded_state (GVariant *state)
{
	gboolean loaded, scanned;

	g_variant_get (state, "(bb)", &loaded, &scanned);
	if (loaded && scanned) {
		/* give it a tiny bit longer to populate sources etc. */
		g_timeout_add (1500, (GSourceFunc) g_main_loop_quit, mainloop);
	}
}

static void
state_changed_cb (GActionGroup *group, const char *action_name, GVariant *state, gpointer data)
{
	if (g_strcmp0 (action_name, "load-uri") == 0) {
		check_loaded_state (state);
	}
}

static void
action_added_cb (GActionGroup *group, const char *action_name, gpointer data)
{
	if (g_strcmp0 (action_name, "load-uri") == 0) {
		GVariant *state = g_action_group_get_action_state (group, "load-uri");
		check_loaded_state (state);
		g_variant_unref (state);
	}
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

	/* initialize i18n */
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* setup */
	setlocale (LC_ALL, "");
	g_set_prgname ("rhythmbox-client");

	/* parse arguments */
	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, args, NULL);
	g_option_context_parse (context, &argc, &argv, &error);
	if (annoy (&error))
		exit (1);

	rb_debug_init (debug);

	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	/* check if it's running before registering the application */
	if (no_start || check_running || quit) {
		GDBusProxy *app_proxy;
		app_proxy = g_dbus_proxy_new_sync (bus, G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START, NULL,
						   "org.gnome.Rhythmbox3",
						   "/org/gnome/Rhythmbox3",
						   "org.gtk.Actions",
						   NULL,
						   &error);
		if (app_proxy == NULL || proxy_has_name_owner (app_proxy) == FALSE) {
			rb_debug ("not running");
			if (check_running) {
				exit (2);
			}
			exit (0);
		} else if (check_running) {
			rb_debug ("running instance found");
			exit (0);
		}
		g_object_unref (app_proxy);
	}

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

	/* wait until it's ready to accept control */
	state = g_action_group_get_action_state (G_ACTION_GROUP (app), "load-uri");
	if (state == NULL) {
		rb_debug ("couldn't get app startup state");
		exit (0);
	}

	g_variant_get (state, "(bb)", &loaded, &scanned);
	if ((loaded && scanned) == FALSE) {
		GDBusActionGroup *group;

		rb_debug ("waiting for app startup");
		mainloop = g_main_loop_new (NULL, FALSE);
		group = g_dbus_action_group_get (bus, "org.gnome.Rhythmbox3", "/org/gnome/Rhythmbox3");
		/* make sure the group gets initialised and put in strict mode */
		g_action_group_has_action (G_ACTION_GROUP (group), "load-uri");
		g_signal_connect (group, "action-state-changed", G_CALLBACK (state_changed_cb), NULL);
		g_signal_connect (group, "action-added", G_CALLBACK (action_added_cb), NULL);
		g_main_loop_run (mainloop);
		rb_debug ("app is now started enough");
		g_object_unref (group);
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
		if (next   || previous  || (seek != 0) ||
		    play   || do_pause  || play_pause  ||
		    stop   || volume_up || volume_down ||
		    repeat || no_repeat || shuffle     ||
		    no_shuffle || (set_volume > -0.01) ||
		    interactive) {
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

	/* interactive mode takes precedence over anything else */
	if (interactive) {
#if defined(HAVE_CFMAKERAW)
		InteractData data;
		rb_debug ("entering interactive mode");
		if (!isatty(1)) {
			g_warning ("interactive mode only works on ttys");
			exit (1);
		}
		data.app = app;
		data.mpris_player = mpris;
		data.mpris_playlists = NULL;
		data.playing_track = NULL;
		data.volume = -1.0;
		/* more things? */

		interact (&data);
		exit (0);
#else
		g_warning ("interactive mode not available on this system");
		exit (1);
#endif
	}

	/* activate or quit */
	if (quit) {
		rb_debug ("quitting existing instance");
		g_action_group_activate_action (G_ACTION_GROUP (app), "quit", NULL);
		g_dbus_connection_flush_sync (bus, NULL, NULL);
		exit (0);
	}

	/* don't present if we're doing something else */
	if (next || previous || (seek != 0) ||
	    clear_queue ||
	    play_uri || other_stuff ||
	    play || do_pause || play_pause || stop ||
	    print_playing || print_playing_format ||
	    repeat || no_repeat || shuffle || no_shuffle ||
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

	if (repeat) {
		g_dbus_proxy_call_sync (mpris,
					"org.freedesktop.DBus.Properties.Set",
					g_variant_new ("(ssv)", "org.mpris.MediaPlayer2.Player", "LoopStatus", g_variant_new_string ("Playlist")),
					G_DBUS_CALL_FLAGS_NONE,
					-1,
					NULL,
					&error);
		annoy (&error);

	} else if (no_repeat) {
		g_dbus_proxy_call_sync (mpris,
					"org.freedesktop.DBus.Properties.Set",
					g_variant_new ("(ssv)", "org.mpris.MediaPlayer2.Player", "LoopStatus", g_variant_new_string ("None")),
					G_DBUS_CALL_FLAGS_NONE,
					-1,
					NULL,
					&error);
		annoy (&error);
	}

	if (shuffle) {
		g_dbus_proxy_call_sync (mpris,
					"org.freedesktop.DBus.Properties.Set",
					g_variant_new ("(ssv)", "org.mpris.MediaPlayer2.Player", "Shuffle", g_variant_new_boolean (TRUE)),
					G_DBUS_CALL_FLAGS_NONE,
					-1,
					NULL,
					&error);
		annoy (&error);

	} else if (no_shuffle) {
		g_dbus_proxy_call_sync (mpris,
					"org.freedesktop.DBus.Properties.Set",
					g_variant_new ("(ssv)", "org.mpris.MediaPlayer2.Player", "Shuffle", g_variant_new_boolean (FALSE)),
					G_DBUS_CALL_FLAGS_NONE,
					-1,
					NULL,
					&error);
		annoy (&error);
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
				gint64 useek;

				useek = seek * 1000000;
				g_dbus_proxy_call_sync (mpris,
							"SetPosition",
							g_variant_new ("(ox)", g_variant_get_string (v, NULL), useek),
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
				g_action_group_activate_action (G_ACTION_GROUP (app), "load-uri", g_variant_new ("(sb)", fileuri, FALSE));
			}
			g_free (fileuri);
			g_object_unref (file);
		}
	}

	/* select/activate/play source */
	if (select_source) {
		rb_debug ("selecting source %s", select_source);
		g_action_group_activate_action (G_ACTION_GROUP (app), "activate-source", g_variant_new ("(su)", select_source, 0));
	} else if (activate_source) {
		rb_debug ("activating source %s", activate_source);
		g_action_group_activate_action (G_ACTION_GROUP (app), "activate-source", g_variant_new ("(su)", activate_source, 1));
	} else if (play_source) {
		rb_debug ("playing source %s", play_source);
		g_action_group_activate_action (G_ACTION_GROUP (app), "activate-source", g_variant_new ("(su)", play_source, 2));
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
			g_action_group_activate_action (G_ACTION_GROUP (app), "load-uri", g_variant_new ("(sb)", fileuri, TRUE));
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
			g_dbus_proxy_call_sync (mpris, "Stop", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
			annoy (&error);
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
