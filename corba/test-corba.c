/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 *  arch-tag: Simple program to test bonobo notification from rhythmbox
 */

#include <stdlib.h>
#include <string.h>
#include <libbonobo.h>
#include "Rhythmbox.h"

#define RB_IID "OAFIID:GNOME_Rhythmbox"

#define COMMAND_PLAY "play"
#define COMMAND_PAUSE "pause"
#define COMMAND_SHUFFLE "shuffle"
#define COMMAND_REPEAT "repeat"
#define COMMAND_NEXT "next"
#define COMMAND_PREVIOUS "previous"
#define COMMAND_SEEK_FORWARD "seek_forward"
#define COMMAND_SEEK_BACKWARD "seek_backward"
#define COMMAND_VOLUME_UP "volume_up"
#define COMMAND_VOLUME_DOWN "volume_down"
#define COMMAND_RATE_UP "rate_up"
#define COMMAND_RATE_DOWN "rate_down"
#define COMMAND_MUTE "mute"
#define COMMAND_QUIT "quit"
#define COMMAND_POWER "power"

static void
on_bonobo_event (BonoboListener *listener, const char *event_name,
		 const CORBA_any *any, CORBA_Environment *ev,
		 gpointer user_data)
{
	g_print ("event received: %s\n", event_name);
}

static void
on_song_change (BonoboListener *listener, const char *event_name,
		const CORBA_any *any, CORBA_Environment *ev,
		gpointer user_data)
{
	GNOME_Rhythmbox_SongInfo *song_info;

	if (!CORBA_TypeCode_equivalent (any->_type, 
					TC_GNOME_Rhythmbox_SongInfo, 
					NULL)) { 
		g_warning ("Unexpected type\n");
	}
	song_info = (GNOME_Rhythmbox_SongInfo *)any->_value;
	if (song_info == NULL) {
		g_warning ("Unexpected error\n");
	}
	g_print ("Now Playing:\n");
	g_print ("Title: %s\n", song_info->title);
	g_print ("Artist: %s\n", song_info->artist);
	g_print ("Album: %s\n", song_info->album);
	g_print ("Bitrate: %u bps\n", song_info->bitrate);
	g_print ("Length: %u\n", song_info->duration);
	g_print ("Rating: %u\n", song_info->rating);
}

static void 
changeRating (GNOME_Rhythmbox rb, int ratingChange)
{
        CORBA_Environment  ev;
	Bonobo_PropertyBag pb;
        GNOME_Rhythmbox_SongInfo *song_info;
        CORBA_any *any;

        CORBA_exception_init (&ev);
	pb = GNOME_Rhythmbox_getPlayerProperties (rb, &ev);

        any = Bonobo_PropertyBag_getValue (pb, "song", &ev);

	if (!CORBA_TypeCode_equivalent (any->_type, 
					TC_GNOME_Rhythmbox_SongInfo, 
					NULL)) { 
		g_warning ("Unexpected type\n");
	}
	song_info = (GNOME_Rhythmbox_SongInfo *)any->_value;
	if (song_info == NULL) {
		g_warning ("Unexpected error\n");
	}

        GNOME_Rhythmbox_setRating (rb, song_info->rating + ratingChange, &ev);

        CORBA_exception_free (&ev);
}

int 
main (int argc, char *argv [])
{
	GNOME_Rhythmbox rb;
	CORBA_Environment  ev;
	Bonobo_PropertyBag pb;

	/*
	 * Initialize bonobo.
	 */
	if (!bonobo_init (&argc, argv))
		g_error ("Could not initialize Bonobo");
	
	CORBA_exception_init (&ev);
	rb = bonobo_activation_activate_from_id (RB_IID, 0, NULL, &ev);
	if (rb == CORBA_OBJECT_NIL) {
		g_warning ("Could not create an instance of Rhythmbox");
		return bonobo_debug_shutdown ();
	}

	if (argc > 1) {
                int arg;
                for (arg = 0; arg < argc; arg++) {
                        CORBA_exception_init (&ev);
                        if (strcmp (argv[arg], COMMAND_PLAY) == 0) {
                                GNOME_Rhythmbox_playPause (rb, &ev);
                        } else if (strcmp (argv[arg], COMMAND_PAUSE) == 0) {
                                GNOME_Rhythmbox_playPause (rb, &ev);
                        } else if (strcmp (argv[arg], COMMAND_SHUFFLE) == 0) {
                        } else if (strcmp (argv[arg], COMMAND_REPEAT) == 0) {
                        } else if (strcmp (argv[arg], COMMAND_NEXT) == 0) {
                                GNOME_Rhythmbox_next (rb, &ev);
                        } else if (strcmp (argv[arg], COMMAND_PREVIOUS) == 0) {
                                GNOME_Rhythmbox_previous (rb, &ev);
                        } else if (strcmp (argv[arg], COMMAND_SEEK_FORWARD) == 0) {
                                GNOME_Rhythmbox_skip (rb, 5, &ev);
                        } else if (strcmp (argv[arg], COMMAND_SEEK_BACKWARD) == 0) {
                                GNOME_Rhythmbox_skip (rb, -5, &ev);
                        } else if (strcmp (argv[arg], COMMAND_VOLUME_UP) == 0) {
                                GNOME_Rhythmbox_volumeUp (rb, &ev);
                        } else if (strcmp (argv[arg], COMMAND_VOLUME_DOWN) == 0) {
                                GNOME_Rhythmbox_volumeDown (rb, &ev);
                        } else if (strcmp (argv[arg], COMMAND_RATE_UP) == 0) {
                                changeRating (rb, 1);
                        } else if (strcmp (argv[arg], COMMAND_RATE_DOWN) == 0) {
                                changeRating (rb, -1);
                        } else if (strcmp (argv[arg], COMMAND_MUTE) == 0) {
                                GNOME_Rhythmbox_toggleMute (rb, &ev);
                        } else if (strcmp (argv[arg], COMMAND_QUIT) == 0) {
                                GNOME_Rhythmbox_quit (rb, &ev);
                        } else if (strcmp (argv[arg], COMMAND_POWER) == 0) {
                                GNOME_Rhythmbox_toggleHide (rb, &ev);
                        }
                        CORBA_exception_free (&ev);
                }
                return bonobo_debug_shutdown ();
	} 

	CORBA_exception_init (&ev);
	pb = GNOME_Rhythmbox_getPlayerProperties (rb, &ev);
	if (BONOBO_EX (&ev)) {
		char *err = bonobo_exception_get_text (&ev);
		g_warning ("An exception occured '%s'", err);
		g_free (err);
		exit (1);
	}

	bonobo_event_source_client_add_listener (pb, on_bonobo_event,
						 "Bonobo/Property:change:repeat",
						 &ev, NULL);
	bonobo_event_source_client_add_listener (pb, on_bonobo_event,
						 "Bonobo/Property:change:shuffle",
						 &ev, NULL);
	bonobo_event_source_client_add_listener (pb, on_bonobo_event,
						 "Bonobo/Property:change:playing",
						 &ev, NULL);
	bonobo_event_source_client_add_listener (pb, on_song_change,
						 "Bonobo/Property:change:song",
						 &ev, NULL);

	if (BONOBO_EX (&ev)) {
		char *err = bonobo_exception_get_text (&ev);
		g_warning ("An exception occured '%s'", err);
		g_free (err);
		exit (1);
	}


	bonobo_main ();
	
	CORBA_exception_free (&ev);

	bonobo_object_release_unref (rb, NULL);

	return bonobo_debug_shutdown ();
}
