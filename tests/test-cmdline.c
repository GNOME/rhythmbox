/*
 *  arch-tag: sample command-line player using monkey-sound
 *
 *  Copyright (C) 2002 Thomas Vander Stichele <thomas@apestaart.org>
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
 */

#include <stdlib.h>
#include <glib.h>
#include <monkey-media.h>

static int exitval = 0;

void
buffering_begin_cb (MonkeyMediaPlayer *player, gpointer data)
{
	printf ("Buffering beginning...\n");
}

void
buffering_end_cb (MonkeyMediaPlayer *player, gpointer data)
{
	printf ("Buffering done!\n");
}

void
eos (MonkeyMediaPlayer *player, gpointer data)
{
	printf ("Audio finished !\n");
	monkey_media_main_quit ();
}

void
info (MonkeyMediaPlayer *player,
      MonkeyMediaStreamInfoField field,
      GValue *value,
      gpointer data)
{
	GEnumValue *enumvalue = g_enum_get_value(g_type_class_peek(MONKEY_MEDIA_TYPE_STREAM_INFO_FIELD),
						 field);
	char *str = g_strdup_value_contents(value);
	fprintf (stdout, "info: %s: %s\n", enumvalue->value_name, str);
	g_free(str);
}

void
error_cb (MonkeyMediaPlayer *player, GError *error, gpointer data)
{
	fprintf (stderr, "Error: %s\n", error->message);
	monkey_media_main_quit ();
	exitval = 1;
}

int
main (int argc, char **argv)
{
	MonkeyMediaPlayer *player = NULL;
	GError *error = NULL;

	monkey_media_init (&argc, &argv);

	/* check if the given argument exists */
	if (argc < 2) g_error ("Please give a URL !");

	/* load the file */
	player = monkey_media_player_new (&error);
	if (error != NULL) {
		  fprintf (stderr, "Unable to create player: %s\n", error->message);
		  g_error_free (error);
	}
	error = NULL;
	monkey_media_player_open (player, argv[1], &error);
	if (error != NULL) {
		  fprintf (stderr, "Unable to load %s: %s\n", argv[1], error->message);
		  g_error_free (error);
	}

	g_signal_connect (G_OBJECT (player), "error",
	                  G_CALLBACK (error_cb), NULL);

	g_signal_connect (G_OBJECT (player), "buffering_begin",
	                  G_CALLBACK (buffering_begin_cb), NULL);

	g_signal_connect (G_OBJECT (player), "buffering_end",
	                  G_CALLBACK (buffering_end_cb), NULL);

	g_signal_connect (G_OBJECT (player), "info",
	                  G_CALLBACK (info), NULL);

	g_signal_connect (G_OBJECT (player), "eos",
	                  G_CALLBACK (eos), NULL);

	/* set to play */
	monkey_media_player_play (player);

	/* go to main loop */
	monkey_media_main ();

	g_object_unref (G_OBJECT (player));

	exit (exitval);
}
