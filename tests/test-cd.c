/*
 *  arch-tag: Simple CD playback test program
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#include <glib.h>
#include <stdlib.h>
#include <monkey-media.h>

/*
 * eos callback
 */

MonkeyMediaAudioCD *cd;

void
eos (MonkeyMediaPlayer *player, gpointer data)
{
	g_warning ("Audio finished !\n");
	monkey_media_main_quit ();
}

int
main (int argc, char *argv[])
{
	gchar *filename = NULL;
	MonkeyMediaPlayer *player = NULL;
	GError *error = NULL;
	GList *l, *tracks;
	int track_no;

	monkey_media_init (&argc, &argv);

	/* check if the given argument exists */
	if (argc < 2) g_error ("Please give a track number!");

	cd = monkey_media_audio_cd_new (NULL);
	g_assert (cd != NULL);

	g_message ("Testing tray functionality ...");
//	monkey_media_audio_cd_open_tray (cd, NULL);
//	monkey_media_audio_cd_close_tray (cd, NULL);

	g_message ("Testing info functionality ...");
	g_print ("Disc in drive? %s\n\n",
		 monkey_media_audio_cd_available (cd, NULL) ? "Yes" : "No");

	tracks = monkey_media_audio_cd_list_tracks (cd, NULL);

	player = monkey_media_player_new (&error);

	if (error != NULL)
	{
	          /* Report error to user, and free error */
		  fprintf (stderr, "Unable to create player: %s\n", error->message);
		  g_error_free (error);
	}
	g_assert (player != NULL);

	g_print ("Listing tracks:\n");
	for (l = tracks, track_no = 1; l != NULL; l = g_list_next (l), track_no++)
	{
		MonkeyMediaStreamInfo *info;
		GValue artist = { 0, }, album = { 0, }, track = { 0, }, trackmax = { 0, }, title = { 0, }, duration = { 0, };

		info = monkey_media_stream_info_new ((char *) l->data, NULL);
		g_assert (info != NULL);

		monkey_media_stream_info_get_value (info,
						    MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,
						    0,
						    &artist);
		monkey_media_stream_info_get_value (info,
						    MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,
						    0,
						    &album);
		monkey_media_stream_info_get_value (info,
						    MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
						    0,
						    &track);
		monkey_media_stream_info_get_value (info,
						    MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER,
						    0,
						    &trackmax);
		monkey_media_stream_info_get_value (info,
						    MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
						    0,
						    &title);
		monkey_media_stream_info_get_value (info,
						    MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,
						    0,
						    &duration);

		g_print ("[%s/%s] (%.2d of %.2d) %s\t\t%ld:%.2ld\n",
			 g_value_get_string (&artist),
			 g_value_get_string (&album),
			 g_value_get_int (&track),
			 g_value_get_int (&trackmax),
			 g_value_get_string (&title),
			 g_value_get_long (&duration) / 60,
			 g_value_get_long (&duration) % 60);
		
		g_value_unset (&artist);
		g_value_unset (&album);
		g_value_unset (&track);
		g_value_unset (&trackmax);
		g_value_unset (&title);
		g_value_unset (&duration);
	}
	monkey_media_audio_cd_free_tracks (tracks);

	g_print ("\nMusicbrainz disc ID: %s\n", monkey_media_audio_cd_get_disc_id (cd, NULL));

	g_message ("Testing play functionaliy ...");
	/* load the file */ 
	error = NULL;
	filename = g_strdup_printf ("audiocd://%d", atoi (argv[1]));
	monkey_media_player_open (player, filename, &error);
	if (error != NULL)
	{
		fprintf (stderr, "failed to play %s, error was: %s\n",
			 filename, error->message);
		g_error_free (error);
	}
	g_free (filename);

	g_signal_connect (G_OBJECT (player), "eos", 
	                  G_CALLBACK (eos), NULL);

	/* set to play */
	monkey_media_player_play (player);

	/* go to main loop */
	monkey_media_main ();

	return 0;
}
  
