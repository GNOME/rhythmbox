/*
 *  arch-tag: Simple program to test bonobo notification from rhythmbox
 */

#include <stdlib.h>
#include <libbonobo.h>
#include "Rhythmbox.h"

#define RB_IID "OAFIID:GNOME_Rhythmbox"


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
		g_error (_("I could not initialize Bonobo"));
	
	CORBA_exception_init (&ev);
	rb = bonobo_activation_activate_from_id (RB_IID, 0, NULL, &ev);


	if (rb == CORBA_OBJECT_NIL) {
		g_warning (_("Could not create an instance of the sample echo component"));
		return bonobo_debug_shutdown ();
	}
	CORBA_exception_init (&ev);

	pb = GNOME_Rhythmbox_getPlayerProperties (rb, &ev);
	if (BONOBO_EX (&ev)) {
		char *err = bonobo_exception_get_text (&ev);
		g_warning (_("An exception occured '%s'"), err);
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
		g_warning (_("An exception occured '%s'"), err);
		g_free (err);
		exit (1);
	}


	bonobo_main ();
	
	CORBA_exception_free (&ev);

	bonobo_object_release_unref (rb, NULL);

	return bonobo_debug_shutdown ();
}
