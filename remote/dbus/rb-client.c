/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006 Jonathan Matthew
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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
#include <dbus/dbus-glib.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rb-debug.h"
#include "rb-shell-binding.h"
#include "rb-shell-player-binding.h"

static gboolean debug = FALSE;

static gboolean no_start = FALSE;
static gboolean quit = FALSE;

static gboolean no_present = FALSE;
static gboolean hide = FALSE;

static gboolean next = FALSE;
static gboolean previous = FALSE;

static gboolean notify = FALSE;

static gboolean play = FALSE;
static gboolean pause = FALSE;
static gboolean play_pause = FALSE;
static gboolean stop = FALSE;

static gboolean enqueue = FALSE;

static gboolean clear_queue = FALSE;

static gchar *play_uri = NULL;
static gboolean print_playing = FALSE;
static gchar *print_playing_format = NULL;

static gdouble set_volume = -1.0;
static gboolean volume_up = FALSE;
static gboolean volume_down = FALSE;
static gboolean print_volume = FALSE;
static gboolean mute = FALSE;
static gboolean unmute = FALSE;

static gchar **other_stuff = NULL;

static GOptionEntry args[] = {
	{ "debug", 0, 0, G_OPTION_ARG_NONE, &debug, NULL, NULL },

	{ "no-start", 0, 0, G_OPTION_ARG_NONE, &no_start, N_("Don't start a new instance of Rhythmbox"), NULL },
	{ "quit", 0, 0, G_OPTION_ARG_NONE, &quit, N_("Quit Rhythmbox"), NULL },

	{ "no-present", 0, 0, G_OPTION_ARG_NONE, &no_present, N_("Don't present an existing Rhythmbox window"), NULL },
	{ "hide", 0, 0, G_OPTION_ARG_NONE, &hide, N_("Hide the Rhythmbox window"), NULL },

	{ "next", 0, 0, G_OPTION_ARG_NONE, &next, N_("Jump to next song"), NULL },
	{ "previous", 0, 0, G_OPTION_ARG_NONE, &previous, N_("Jump to previous song"), NULL },

	{ "notify", 0, 0, G_OPTION_ARG_NONE, &notify, N_("Show notification of the playing song"), NULL },

	{ "play", 0, 0, G_OPTION_ARG_NONE, &play, N_("Resume playback if currently paused"), NULL },
	{ "pause", 0, 0, G_OPTION_ARG_NONE, &pause, N_("Pause playback if currently playing"), NULL },
	{ "play-pause", 0, 0, G_OPTION_ARG_NONE, &play_pause, N_("Toggle play/pause mode"), NULL },
/*	{ "stop", 0, 0, G_OPTION_ARG_NONE, &stop, N_("Stop playback"), NULL }, */

	{ "play-uri", 0, 0, G_OPTION_ARG_FILENAME, &play_uri, N_("Play a specified URI, importing it if necessary"), N_("URI to play")},
	{ "enqueue", 0, 0, G_OPTION_ARG_NONE, &enqueue, N_("Add specified tracks to the play queue"), NULL },
	{ "clear-queue", 0, 0, G_OPTION_ARG_NONE, &clear_queue, N_("Empty the play queue before adding new tracks"), NULL },

	{ "print-playing", 0, 0, G_OPTION_ARG_NONE, &print_playing, N_("Print the title and artist of the playing song"), NULL },
	{ "print-playing-format", 0, 0, G_OPTION_ARG_STRING, &print_playing_format, N_("Print formatted details of the song"), NULL },

#if GLIB_CHECK_VERSION(2,12,0)
	{ "set-volume", 0, 0, G_OPTION_ARG_DOUBLE, &set_volume, N_("Set the playback volume"), NULL },
#endif
	{ "volume-up", 0, 0, G_OPTION_ARG_NONE, &volume_up, N_("Increase the playback volume"), NULL },
	{ "volume-down", 0, 0, G_OPTION_ARG_NONE, &volume_down, N_("Decrease the playback volume"), NULL },
	{ "print-volume", 0, 0, G_OPTION_ARG_NONE, &print_volume, N_("Print the current playback volume"), NULL },
	{ "mute", 0, 0, G_OPTION_ARG_NONE, &mute, N_("Mute playback"), NULL },
	{ "unmute", 0, 0, G_OPTION_ARG_NONE, &unmute, N_("Unmute playback"), NULL },

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
rb_make_duration_string (guint duration)
{
	char *str;
	int hours, minutes, seconds;

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

/**
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
 */
static char *
parse_pattern (const char *pattern, GHashTable *properties, guint elapsed)
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
		const GValue *value = NULL;

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
				value = g_hash_table_lookup (properties, "album");
				if (value)
					string = g_value_dup_string (value);
				break;
			case 'T':
				value = g_hash_table_lookup (properties, "album-folded");
				if (value)
					string = g_value_dup_string (value);
				break;
			case 'a':
				value = g_hash_table_lookup (properties, "artist");
				if (value)
					string = g_value_dup_string (value);
				break;
			case 'A':
				value = g_hash_table_lookup (properties, "artist-folded");
				if (value)
					string = g_value_dup_string (value);
				break;
			/*case 's':
				string = sanitize_path (album sort name);
				break;
			case 'S':
				char *t = g_utf8_strdown (album sort name)
				string = sanitize_path (t);
				g_free (t);
				break;*/
			case 'y':
				/* Release year */
				value = g_hash_table_lookup (properties, "year");
				if (value)
					string = g_strdup_printf ("%u", g_value_get_uint (value));
				break;
				/* Disc number */
			case 'n':
				value = g_hash_table_lookup (properties, "disc-number");
				if (value)
					string = g_strdup_printf ("%u", g_value_get_uint (value));
				break;
			case 'N':
				value = g_hash_table_lookup (properties, "disc-number");
				if (value)
					string = g_strdup_printf ("%02u", g_value_get_uint (value));
				break;
				/* genre */
			case 'g':
				value = g_hash_table_lookup (properties, "genre");
				if (value)
					string = g_value_dup_string (value);
				break;
			case 'G':
				value = g_hash_table_lookup (properties, "genre-folded");
				if (value)
					string = g_value_dup_string (value);
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
				value = g_hash_table_lookup (properties, "title");
				if (value)
					string = g_value_dup_string (value);
				break;
			case 'T':
				value = g_hash_table_lookup (properties, "title-folded");
				if (value)
					string = g_value_dup_string (value);
				break;
			case 'a':
				value = g_hash_table_lookup (properties, "artist");
				if (value)
					string = g_value_dup_string (value);
				break;
			case 'A':
				value = g_hash_table_lookup (properties, "artist-folded");
				if (value)
					string = g_value_dup_string (value);
				break;
			/*case 's':
				string = sanitize_path (artist sort name);
				break;
			case 'S':
				char *t = g_utf8_strdown (artist sort name)
				string = sanitize_path (t);
				g_free (t);
				break;*/
			case 'n':
				/* Track number */
				value = g_hash_table_lookup (properties, "track-number");
				if (value)
					string = g_strdup_printf ("%u", g_value_get_uint (value));
				break;
			case 'N':
				/* Track number, zero-padded */
				value = g_hash_table_lookup (properties, "track-number");
				if (value)
					string = g_strdup_printf ("%02u", g_value_get_uint (value));
				break;
			case 'd':
				/* Track duration */
				value = g_hash_table_lookup (properties, "duration");
				if (value)
					string = rb_make_duration_string (g_value_get_uint (value));
				break;
			case 'e':
				/* Track elapsed time */
				string = rb_make_duration_string (elapsed);
				break;
			default:
				string = g_strdup_printf ("%%t%c", *p);
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



static gboolean
create_rb_shell_proxies (DBusGConnection *bus, DBusGProxy **shell_proxy, DBusGProxy **player_proxy, GError **error)
{
	DBusGProxy *sp;

	rb_debug ("creating shell proxy");
	sp = dbus_g_proxy_new_for_name_owner (bus,
					      "org.gnome.Rhythmbox",
					      "/org/gnome/Rhythmbox/Shell",
					      "org.gnome.Rhythmbox.Shell",
					      error);
	if (*error) {
		*shell_proxy = NULL;
		return ((*error)->code == DBUS_GERROR_NAME_HAS_NO_OWNER);
	}

	rb_debug ("creating player proxy");
	*player_proxy = dbus_g_proxy_new_from_proxy (sp,
						     "org.gnome.Rhythmbox.Player",
						     "/org/gnome/Rhythmbox/Player");
	if (*player_proxy == NULL) {
		g_object_unref (G_OBJECT (sp));
		*shell_proxy = NULL;
		*player_proxy = NULL;
		return FALSE;
	}

	*shell_proxy = sp;
	return TRUE;
}

static void
print_playing_song (DBusGProxy *shell_proxy, DBusGProxy *player_proxy, const char *format)
{
	char *playing_uri;
	GError *error = NULL;
	GHashTable *properties;
	char *string;
	guint elapsed = 0;

	org_gnome_Rhythmbox_Player_get_playing_uri (player_proxy, &playing_uri, &error);
	if (annoy (&error))
		return;

	org_gnome_Rhythmbox_Player_get_elapsed (player_proxy, &elapsed, &error);
	annoy (&error);

	if (!playing_uri) {
		g_print (_("Not playing"));
		return;
	}
	rb_debug ("playing song is %s", playing_uri);

	org_gnome_Rhythmbox_Shell_get_song_properties (shell_proxy, playing_uri, &properties, &error);
	if (annoy (&error))
		return;

	string = parse_pattern (format, properties, elapsed);
	g_print ("%s\n", string);
	g_hash_table_destroy (properties);
	g_free (string);
}

int
main (int argc, char **argv)
{
	GOptionContext *context;
	gboolean ok;
	GError *error = NULL;
	DBusGConnection *bus;
	DBusGProxy *shell_proxy = NULL;
	DBusGProxy *player_proxy = NULL;
	gboolean is_playing;

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
	ok = g_option_context_parse (context, &argc, &argv, &error);
	if (annoy (&error))
		exit (1);

	rb_debug_init (debug);

	/* get dbus connection and proxy for rhythmbox shell */
	bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (annoy (&error))
		exit (1);

	if (!create_rb_shell_proxies (bus, &shell_proxy, &player_proxy, &error)) {
		annoy (&error);
		exit (1);
	}
	g_clear_error (&error);

	/* 1. activate or quit */
	if (quit) {
		if (shell_proxy) {
			rb_debug ("quitting existing instance");
			dbus_g_proxy_call_no_reply (shell_proxy, "quit", G_TYPE_INVALID);
		} else {
			rb_debug ("no existing instance to quit");
		}

		exit (0);
	}
	if (shell_proxy == NULL) {
		DBusGProxy *bus_proxy;
		guint start_service_reply;

		if (no_start) {
			rb_debug ("no existing instance, and can't start one");
			exit (0);
		}

		rb_debug ("starting new instance");
		bus_proxy = dbus_g_proxy_new_for_name (bus,
						       "org.freedesktop.DBus",
						       "/org/freedesktop/DBus",
						       "org.freedesktop.DBus");

		if (!dbus_g_proxy_call (bus_proxy, "StartServiceByName", &error,
					G_TYPE_STRING, "org.gnome.Rhythmbox",
					G_TYPE_UINT, 0,
					G_TYPE_INVALID,
					G_TYPE_UINT, &start_service_reply,
					G_TYPE_INVALID)) {
			g_warning ("%s", error->message);
			exit (1);
		}

		/* hopefully we can get a proxy for the rb shell now.. */
		if (!create_rb_shell_proxies (bus, &shell_proxy, &player_proxy, &error)) {
			annoy (&error);
			exit (1);
		}
		g_clear_error (&error);
	}

	/* don't present if we're doing something else */
	if (next || previous ||
	    clear_queue ||
	    play_uri || other_stuff ||
	    play || pause || play_pause || stop ||
	    print_playing || print_playing_format || notify ||
	    (set_volume > -0.01) || volume_up || volume_down || print_volume || mute || unmute)
		no_present = TRUE;

	/* 2. present or hide */
	if (hide || !no_present) {
		DBusGProxy *properties_proxy;
		GValue value = {0,};

		rb_debug ("setting visibility property");
		g_value_init (&value, G_TYPE_BOOLEAN);
		g_value_set_boolean (&value, !hide);
		properties_proxy = dbus_g_proxy_new_from_proxy (shell_proxy,
								"org.freedesktop.DBus.Properties",
							        "/org/gnome/Rhythmbox/Shell");
		dbus_g_proxy_call_no_reply (properties_proxy, "Set",
					    G_TYPE_STRING, "org.gnome.Rhythmbox.Shell",
					    G_TYPE_STRING, "visibility",
					    G_TYPE_VALUE, &value,
					    G_TYPE_INVALID);
		g_object_unref (G_OBJECT (properties_proxy));
	}

	/* 3. skip to next or previous track */
	if (next) {
		rb_debug ("next track");
		org_gnome_Rhythmbox_Player_next (player_proxy, &error);
		annoy (&error);
	} else if (previous) {
		rb_debug ("previous track");
		org_gnome_Rhythmbox_Player_previous (player_proxy, &error);
		annoy (&error);
	}

	/* 4. add/enqueue */
	if (clear_queue) {
		org_gnome_Rhythmbox_Shell_clear_queue (shell_proxy, &error);
		annoy (&error);
	}
	if (other_stuff) {
		int i;
		for (i = 0; other_stuff[i] != NULL; i++) {
			char *uri = gnome_vfs_make_uri_from_shell_arg (other_stuff[i]);
			if (uri == NULL) {
				g_warning ("couldn't convert \"%s\" to a URI", other_stuff[i]);
				continue;
			}

			if (enqueue) {
				rb_debug ("enqueueing %s", uri);
				org_gnome_Rhythmbox_Shell_add_to_queue (shell_proxy, uri, &error);
			} else {
				rb_debug ("importing %s", uri);
				org_gnome_Rhythmbox_Shell_load_ur_i (shell_proxy, uri, FALSE, &error);
			}
			annoy (&error);
			g_free (uri);
		}
	}

	/* play uri */
	if (play_uri) {
		char *uri = gnome_vfs_make_uri_from_shell_arg (play_uri);
		if (uri == NULL) {
			g_warning ("couldn't convert \"%s\" to a URI", play_uri);
		} else {
			rb_debug ("loading and playing %s", uri);
			org_gnome_Rhythmbox_Shell_load_ur_i (shell_proxy, uri, TRUE, &error);
			annoy (&error);
		}
		g_free (uri);
	}

	/* 5. play/pause/stop */
	org_gnome_Rhythmbox_Player_get_playing (player_proxy, &is_playing, &error);
	if (!annoy (&error)) {
		rb_debug ("playback state: %d", is_playing);
		if (play || pause || play_pause) {
			if (is_playing != play || play_pause) {
				rb_debug ("calling playPause to change playback state");
				org_gnome_Rhythmbox_Player_play_pause (player_proxy, FALSE, &error);
				annoy (&error);
			} else {
				rb_debug ("no need to change playback state");
			}
		} else if (stop) {
			g_warning ("not implemented yet");
		}
	}

	/* 6. get/set volume, mute/unmute */
#if GLIB_CHECK_VERSION(2,12,0)
	if (set_volume > -0.01) {
		org_gnome_Rhythmbox_Player_set_volume (player_proxy, set_volume, &error);
		annoy (&error);
	} else
#endif
	if (volume_up || volume_down) {
		org_gnome_Rhythmbox_Player_set_volume_relative (player_proxy, volume_up ? 0.1 : -0.1, &error);
		annoy (&error);
	} else if (unmute || mute) {
		org_gnome_Rhythmbox_Player_set_mute (player_proxy, unmute ? FALSE : TRUE, &error);
		annoy (&error);
	}

	if (print_volume) {
		gboolean mute = FALSE;
		gdouble volume = 1.0;

		org_gnome_Rhythmbox_Player_get_mute (player_proxy, &mute, &error);
		annoy (&error);
		org_gnome_Rhythmbox_Player_get_volume (player_proxy, &volume, &error);
		annoy (&error);

		if (mute)
			g_print (_("Playback is muted.\n"));
		g_print (_("Playback volume is %f.\n"), volume);
	}

	/* 7. print playing song */
	if (print_playing_format) {
		print_playing_song (shell_proxy, player_proxy, print_playing_format);
	} else if (print_playing) {
		print_playing_song (shell_proxy, player_proxy, "%ta - %tt");
	}

	/* 8. display notification about playing song */
	if (notify) {
		rb_debug ("show notification");
		org_gnome_Rhythmbox_Shell_notify (shell_proxy, TRUE, &error);
		annoy (&error);
	}

	g_object_unref (shell_proxy);
	g_object_unref (player_proxy);
	g_option_context_free (context);

	return 0;
}

