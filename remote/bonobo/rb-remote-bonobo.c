/*
 *  arch-tag: Implementation of Rhythmbox Bonobo remoting
 *
 *  Copyright (C) 2004 Colin Walters <walters@gnome.org>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "rb-remote-bonobo.h"
#include "rb-remote-client-proxy.h"
#include <Rhythmbox.h>
#include <bonobo/bonobo-arg.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-context.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-window.h>
#include <bonobo/bonobo-control-frame.h>
#include <bonobo-activation/bonobo-activation-register.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <config.h>
#include <string.h>
#include <libgnome/libgnome.h>
#include <libgnome/gnome-i18n.h>

#include "rb-debug.h"

static void rb_remote_bonobo_class_init (RBRemoteBonoboClass *klass);
static void rb_remote_bonobo_remote_client_proxy_init (RBRemoteClientProxyIface *iface);
static void rb_remote_bonobo_init (RBRemoteBonobo *shell);
static void rb_remote_bonobo_dispose (GObject *object);
static void rb_remote_bonobo_finalize (GObject *object);

/* Server methods */
static void rb_remote_bonobo_corba_quit (PortableServer_Servant _servant,
					 CORBA_Environment *ev);
static void rb_remote_bonobo_corba_handle_file (PortableServer_Servant _servant,
						const CORBA_char *uri,
						CORBA_Environment *ev);
static void rb_remote_bonobo_corba_add_to_library (PortableServer_Servant _servant,
						   const CORBA_char *uri,
						   CORBA_Environment *ev);
static void rb_remote_bonobo_corba_grab_focus (PortableServer_Servant _servant,
					       CORBA_Environment *ev);
static void rb_remote_bonobo_corba_playpause (PortableServer_Servant _servant,
					      CORBA_Environment *ev);
static void rb_remote_bonobo_corba_select (PortableServer_Servant _servant,
					   const CORBA_char *uri,
					   CORBA_Environment *ev);
static void rb_remote_bonobo_corba_play (PortableServer_Servant _servant,
					 const CORBA_char *uri,
					 CORBA_Environment *ev);
static void rb_remote_bonobo_corba_next (PortableServer_Servant _servant,
					 CORBA_Environment *ev);
static void rb_remote_bonobo_corba_previous (PortableServer_Servant _servant,
					     CORBA_Environment *ev);
static CORBA_long rb_remote_bonobo_corba_get_playing_time (PortableServer_Servant _servant,
						   CORBA_Environment *ev);
static void rb_remote_bonobo_corba_set_playing_time (PortableServer_Servant _servant,
						   CORBA_long time, CORBA_Environment *ev);
static Bonobo_PropertyBag rb_remote_bonobo_corba_get_player_properties (PortableServer_Servant _servant, CORBA_Environment *ev);

/* Client methods */
static void rb_remote_bonobo_client_handle_uri_impl (RBRemoteClientProxy *proxy, const char *uri);
static RBRemoteSong *rb_remote_bonobo_client_get_playing_song_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_grab_focus_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_toggle_shuffle_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_toggle_playing_impl (RBRemoteClientProxy *proxy);
static long rb_remote_bonobo_client_get_playing_time_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_set_playing_time_impl (RBRemoteClientProxy *proxy, long time);

static GObjectClass *parent_class;

enum
{
	PROP_NONE,
};

struct RBRemoteBonoboPrivate
{
	gboolean disposed;

	GNOME_Rhythmbox remote;
	
	RBRemoteProxy *proxy;

	BonoboPropertyBag *pb;
};

GType
rb_remote_bonobo_get_type (void)
{
	static GType type = 0;
                                                                              
	if (type == 0)
	{ 
		static GTypeInfo info =
		{
			sizeof (RBRemoteBonoboClass),
			NULL, 
			NULL,
			(GClassInitFunc) rb_remote_bonobo_class_init, 
			NULL,
			NULL, 
			sizeof (RBRemoteBonobo),
			0,
			(GInstanceInitFunc) rb_remote_bonobo_init
		};

		static const GInterfaceInfo rb_remote_client_proxy_info =
		{
			(GInterfaceInitFunc) rb_remote_bonobo_remote_client_proxy_init,
			NULL,
			NULL
		};
		
		type = bonobo_type_unique (BONOBO_TYPE_OBJECT,
					   POA_GNOME_Rhythmbox__init,
					   POA_GNOME_Rhythmbox__fini,
					   G_STRUCT_OFFSET (RBRemoteBonoboClass, epv),
					   &info,
					   "RBRemoteBonobo");

		g_type_add_interface_static (type,
					     RB_TYPE_REMOTE_CLIENT_PROXY,
					     &rb_remote_client_proxy_info);
	}

	return type;
}

static void
rb_remote_bonobo_class_init (RBRemoteBonoboClass *klass)
{
        GObjectClass *object_class = (GObjectClass *) klass;
        POA_GNOME_Rhythmbox__epv *epv = &klass->epv;

        parent_class = g_type_class_peek_parent (klass);

        object_class->dispose = rb_remote_bonobo_dispose;
        object_class->finalize = rb_remote_bonobo_finalize;

	epv->quit         = rb_remote_bonobo_corba_quit;
	epv->handleFile   = rb_remote_bonobo_corba_handle_file;
	epv->addToLibrary = rb_remote_bonobo_corba_add_to_library;
	epv->grabFocus    = rb_remote_bonobo_corba_grab_focus;
	epv->playPause = rb_remote_bonobo_corba_playpause;
	epv->select = rb_remote_bonobo_corba_select;
	epv->play = rb_remote_bonobo_corba_play;
	epv->previous = rb_remote_bonobo_corba_previous;
	epv->next = rb_remote_bonobo_corba_next;
	epv->getPlayingTime = rb_remote_bonobo_corba_get_playing_time;
	epv->setPlayingTime = rb_remote_bonobo_corba_set_playing_time;
	epv->getPlayerProperties = rb_remote_bonobo_corba_get_player_properties;

}

static void
rb_remote_bonobo_remote_client_proxy_init (RBRemoteClientProxyIface *iface)
{
	iface->handle_uri = rb_remote_bonobo_client_handle_uri_impl;
	iface->get_playing_song = rb_remote_bonobo_client_get_playing_song_impl;
	iface->grab_focus = rb_remote_bonobo_client_grab_focus_impl;
	iface->toggle_shuffle = rb_remote_bonobo_client_toggle_shuffle_impl;
	iface->toggle_playing = rb_remote_bonobo_client_toggle_playing_impl;
	iface->get_playing_time = rb_remote_bonobo_client_get_playing_time_impl;
	iface->set_playing_time = rb_remote_bonobo_client_set_playing_time_impl;
}

static void
rb_remote_bonobo_init (RBRemoteBonobo *bonobo) 
{
	bonobo->priv = g_new0 (RBRemoteBonoboPrivate, 1);
}

static void
rb_remote_bonobo_dispose (GObject *object)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (object);

	if (bonobo->priv->disposed)
		return;
	bonobo->priv->disposed = TRUE;
}

static void
rb_remote_bonobo_finalize (GObject *object)
{
        RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (object);

	g_free (bonobo->priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

RBRemoteBonobo *
rb_remote_bonobo_new (void)
{
	return g_object_new (RB_TYPE_REMOTE_BONOBO, NULL);
}


static void
rb_remote_bonobo_corba_quit (PortableServer_Servant _servant,
			     CORBA_Environment *ev)
{
#if 0
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));

	GDK_THREADS_ENTER ();
	rb_remote_proxy_quit (bonobo->priv->proxy);
	GDK_THREADS_LEAVE ();
#endif
}

static void
rb_remote_bonobo_corba_handle_file (PortableServer_Servant _servant,
				    const CORBA_char *uri,
				    CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));

	GDK_THREADS_ENTER ();
	rb_remote_proxy_load_uri (bonobo->priv->proxy, uri);
	GDK_THREADS_LEAVE ();
}

static void
rb_remote_bonobo_corba_add_to_library (PortableServer_Servant _servant,
				       const CORBA_char *uri,
				       CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));

	GDK_THREADS_ENTER ();
	rb_remote_proxy_load_song (bonobo->priv->proxy, uri);
	GDK_THREADS_LEAVE ();
}

static void
rb_remote_bonobo_corba_grab_focus (PortableServer_Servant _servant,
				   CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));

	GDK_THREADS_ENTER ();
	rb_remote_proxy_grab_focus (bonobo->priv->proxy);
	GDK_THREADS_LEAVE ();
}

static void
rb_remote_bonobo_corba_playpause (PortableServer_Servant _servant,
				  CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));

	GDK_THREADS_ENTER ();

	if (rb_remote_proxy_playing (bonobo->priv->proxy))
		rb_remote_proxy_pause (bonobo->priv->proxy);
	else
		rb_remote_proxy_play (bonobo->priv->proxy);

	GDK_THREADS_LEAVE ();
}

static void
rb_remote_bonobo_corba_select (PortableServer_Servant _servant,
			       const CORBA_char *uri,
			       CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));

	GDK_THREADS_ENTER ();
	rb_remote_proxy_select_uri (bonobo->priv->proxy, uri);
	GDK_THREADS_LEAVE ();
}

static void
rb_remote_bonobo_corba_play (PortableServer_Servant _servant,
			     const CORBA_char *uri,
			     CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));

	GDK_THREADS_ENTER ();
	rb_remote_proxy_play_uri (bonobo->priv->proxy, uri);
	GDK_THREADS_LEAVE ();
}

static void
rb_remote_bonobo_corba_next (PortableServer_Servant _servant,
		     CORBA_Environment *ev)
{
#if 0
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));
#endif
}

static void
rb_remote_bonobo_corba_previous (PortableServer_Servant _servant,
				 CORBA_Environment *ev)
{
#if 0
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));
#endif
}

static CORBA_long
rb_remote_bonobo_corba_get_playing_time (PortableServer_Servant _servant,
					 CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));
	long playing_time;

	GDK_THREADS_ENTER ();
	playing_time = rb_remote_proxy_get_playing_time (bonobo->priv->proxy);
	GDK_THREADS_LEAVE ();

	return (CORBA_long) playing_time;
}

static void
rb_remote_bonobo_corba_set_playing_time (PortableServer_Servant _servant,
					 CORBA_long time, CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));

	GDK_THREADS_ENTER ();
	rb_remote_proxy_set_playing_time (bonobo->priv->proxy, (long) time);
	GDK_THREADS_LEAVE ();
}

#if 0
static GNOME_Rhythmbox_SongInfo *
get_song_info_from_player (RBRemoteBonobo *bonobo)
{
	RhythmDBEntry *entry;
	RhythmDB *db = shell->priv->db;
	GNOME_Rhythmbox_SongInfo *song_info;
	RBEntryView *view;
	RBSource *playing_source;

	playing_source = rb_remote_bonobo_player_get_playing_source (shell->priv->player_shell);

	if (playing_source == NULL)
		goto lose;

	view = rb_source_get_entry_view (playing_source);
	g_object_get (G_OBJECT (view), "playing_entry", &entry, NULL);
	if (entry == NULL)
		goto lose;

	song_info = GNOME_Rhythmbox_SongInfo__alloc ();
	rhythmdb_read_lock (db);
	song_info->title = CORBA_string_dup (rhythmdb_entry_get_string (db, entry, RHYTHMDB_PROP_TITLE));
	song_info->artist = CORBA_string_dup (rhythmdb_entry_get_string (db, entry, RHYTHMDB_PROP_ARTIST));
	song_info->album = CORBA_string_dup (rhythmdb_entry_get_string (db, entry, RHYTHMDB_PROP_ALBUM));
	song_info->genre = CORBA_string_dup (rhythmdb_entry_get_string (db, entry, RHYTHMDB_PROP_GENRE));
	song_info->path = CORBA_string_dup (rhythmdb_entry_get_string (db, entry, RHYTHMDB_PROP_LOCATION));
	song_info->track_number = rhythmdb_entry_get_int (db, entry, RHYTHMDB_PROP_TRACK_NUMBER);
	song_info->duration = rhythmdb_entry_get_long (db, entry, RHYTHMDB_PROP_DURATION);
	song_info->bitrate = rhythmdb_entry_get_int (db, entry, RHYTHMDB_PROP_BITRATE);
	song_info->filesize = rhythmdb_entry_get_uint64 (db, entry, RHYTHMDB_PROP_FILE_SIZE);
	song_info->rating = rhythmdb_entry_get_double (db, entry, RHYTHMDB_PROP_RATING);
	song_info->play_count = rhythmdb_entry_get_int (db, entry, RHYTHMDB_PROP_PLAY_COUNT);
	song_info->last_played = rhythmdb_entry_get_long (db, entry, RHYTHMDB_PROP_LAST_PLAYED);
	rhythmdb_read_unlock (db);

	return song_info;
 lose:
	return NULL;
}
#endif

static void
bonobo_pb_get_prop (BonoboPropertyBag *bag,
		    BonoboArg         *arg,
		    guint              arg_id,
		    CORBA_Environment *ev,
		    gpointer           user_data)
{
#if 0
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (user_data);
	RBRemoteBonoboPlayer *player;

	player = RB_REMOTE_BONOBO_PLAYER (shell->priv->player_shell);

	switch (arg_id) {

	case PROP_VISIBILITY:
		BONOBO_ARG_SET_BOOLEAN (arg, FALSE);
		break;

	case PROP_SHUFFLE:
	{
		gboolean shuffle, repeat;
		rb_remote_bonobo_player_get_playback_state (player,
						    &shuffle, &repeat);
		BONOBO_ARG_SET_BOOLEAN (arg, shuffle);
		break;
	}

	case PROP_SONG: {
		GNOME_Rhythmbox_SongInfo *ret_val;
		
		ret_val = get_song_info_from_player (shell);
		arg->_value = (gpointer)ret_val;
		if (ret_val == NULL) {
			arg->_type = TC_null;
		} else {
			arg->_type = TC_GNOME_Rhythmbox_SongInfo;
		}
		break;		
	}

	default:
		bonobo_exception_set (ev, ex_Bonobo_PropertyBag_NotFound);
		break;
	}
#endif
}

static void
bonobo_pb_set_prop (BonoboPropertyBag *bag,
		   const BonoboArg   *arg,
		   guint              arg_id,
		   CORBA_Environment *ev,
		   gpointer           user_data)
{
#if 0
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (user_data);
	RBRemoteBonoboPlayer *player;

	player = RB_REMOTE_BONOBO_PLAYER (shell->priv->player_shell);
	
	switch (arg_id) {

	case PROP_VISIBILITY:
		break;

	case PROP_SONG:
		bonobo_exception_set (ev, ex_Bonobo_PropertyBag_ReadOnly);
		break;

	case PROP_SHUFFLE:
	{
		gboolean repeat;
		gboolean shuffle;


		rb_remote_bonobo_player_get_playback_state (player, &shuffle,
						    &repeat);
		shuffle = BONOBO_ARG_GET_BOOLEAN (arg);

		rb_remote_bonobo_player_set_playback_state (player,
						    shuffle,
						    repeat);
		break;
	}

	default:
		bonobo_exception_set (ev, ex_Bonobo_PropertyBag_NotFound);
		break;
	}
#endif
}


static Bonobo_PropertyBag
rb_remote_bonobo_corba_get_player_properties (PortableServer_Servant _servant, 
				      CORBA_Environment *ev)
{	
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));

	if (bonobo->priv->pb == NULL) {
#if 0
		gchar *params_to_map[] = {"repeat", "play-order", "playing"}; 
		GParamSpec **params;
		int i = 0;
		int total = 0;
#endif

		bonobo->priv->pb = bonobo_property_bag_new (bonobo_pb_get_prop, 
							    bonobo_pb_set_prop, 
							    bonobo);
		
		
#if 0
		params = malloc (G_N_ELEMENTS (params_to_map) * sizeof (GParamSpec *));
		for (i = 0; i < G_N_ELEMENTS (params_to_map); i++) {
			params[total] = g_object_class_find_property (G_OBJECT_CLASS (RB_REMOTE_BONOBO_PLAYER_GET_CLASS (shell->priv->player_shell)), params_to_map[i]);
			if (params[total])
				total++;
		}
		bonobo_property_bag_map_params (shell->priv->pb,
						G_OBJECT (shell->priv->player_shell),
						(const GParamSpec **)params, total);


		/* Manually install the other properties */
		bonobo_property_bag_add (shell->priv->pb, "visibility", 
					 PROP_VISIBILITY, BONOBO_ARG_BOOLEAN, NULL, 
					 _("Whether the main window is visible"), 0);

		/* Manually install the other properties */
		bonobo_property_bag_add (shell->priv->pb, "shuffle", 
					 PROP_SHUFFLE, BONOBO_ARG_BOOLEAN, NULL, 
					 _("Whether shuffle is enabled"), 0);

		bonobo_property_bag_add (shell->priv->pb, "song", 
					 PROP_SONG, TC_GNOME_Rhythmbox_SongInfo, NULL, 
					 _("Properties for the current song"), 0);
#endif
	}

	/* If the creation of the property bag failed, 
	 * return a corba exception
	 */
	
	return bonobo_object_dup_ref (BONOBO_OBJREF (bonobo->priv->pb), NULL);
}

#if 0
static void 
shell_notify_pb_changes (RBRemoteBonobo *bonobo, const gchar *property_name, 
			 BonoboArg *arg) 
{
	if (bonobo->priv->pb != NULL) {
		bonobo_event_source_notify_listeners_full (bonobo->priv->pb->es,
							   "Bonobo/Property",
							   "change",
							   property_name,
							   arg, NULL);
	}
}

static void
rb_remote_bonobo_property_changed_generic_cb (GObject *object,
					      GParamSpec *pspec, 
					      RBRemoteBonobo *bonobo)
{
	BonoboArg *arg = bonobo_arg_new (TC_CORBA_boolean);
	gboolean value;

	g_object_get (object, pspec->name, &value, NULL);
	BONOBO_ARG_SET_BOOLEAN (arg, value);
	shell_notify_pb_changes (shell, pspec->name, arg);
	bonobo_arg_release (arg);
}

static void
rb_remote_bonobo_entry_changed_cb (GObject *object, GParamSpec *pspec, RBRemoteBonobo *bonobo)
{
	GNOME_Rhythmbox_SongInfo *song_info;
	BonoboArg *arg;
	
	g_assert (strcmp (pspec->name, "playing-entry") == 0);
	song_info = get_song_info_from_player (shell);
	if (!song_info) {
		rb_debug ("no song info returned!");
		return;
	}
	arg = bonobo_arg_new (TC_GNOME_Rhythmbox_SongInfo);
	arg->_value = (gpointer)song_info;
	shell_notify_pb_changes (shell, "song", arg);
	bonobo_arg_release (arg);

#ifdef WITH_DASHBOARD
	if (song_info) {
        	char *cluepacket;
        	/* Send cluepacket to dashboard */
        	cluepacket =
			dashboard_build_cluepacket_then_free_clues ("Music Player",
							    	TRUE, 
							    	"", 
							    	dashboard_build_clue (song_info->title, "song_title", 10),
							    	dashboard_build_clue (song_info->artist, "artist", 10),
							    	dashboard_build_clue (song_info->album, "album", 10),
							    	NULL);
       		dashboard_send_raw_cluepacket (cluepacket);
       		g_free (cluepacket);
	}
#endif /* WITH_DASHBOARD */
}

#endif

GQuark
rb_remote_bonobo_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_remote_bonobo_error");

	return quark;
}

static char *
rb_shell_corba_exception_to_string (CORBA_Environment *ev)
{
	g_return_val_if_fail (ev != NULL, NULL);

	if ((CORBA_exception_id (ev) != NULL) &&
	    (strcmp (CORBA_exception_id (ev), ex_Bonobo_GeneralError) != 0))
		return bonobo_exception_get_text (ev); 
	else {
		const Bonobo_GeneralError *bonobo_general_error;

		bonobo_general_error = CORBA_exception_value (ev);
		if (bonobo_general_error != NULL) 
			return g_strdup (bonobo_general_error->description);
	}

	return NULL;
}

gboolean
rb_remote_bonobo_activate (RBRemoteBonobo *bonobo)
{
	bonobo->priv->remote =
		bonobo_activation_activate_from_id (RB_REMOTE_BONOBO_OAFIID,
						    Bonobo_ACTIVATION_FLAG_EXISTING_ONLY,
						    NULL, NULL);
	return bonobo->priv->remote != NULL;
}

static void
shell_weak_ref_cb (gpointer data, GObject *objptr)
{
	bonobo_object_unref (BONOBO_OBJECT (data));
}

gboolean
rb_remote_bonobo_acquire (RBRemoteBonobo *bonobo,
			  RBRemoteProxy *proxy,
			  GError **error)
{
	CORBA_Object corba_object;
	CORBA_Environment ev;
	gboolean registration_failed;

	rb_debug ("registering with bonobo"); 

	bonobo->priv->proxy = proxy;
	g_object_weak_ref (G_OBJECT (proxy), shell_weak_ref_cb, bonobo);

	corba_object = bonobo_object_corba_objref (BONOBO_OBJECT (bonobo));

	registration_failed = FALSE;
	if (bonobo_activation_active_server_register (RB_REMOTE_BONOBO_OAFIID, corba_object) != Bonobo_ACTIVATION_REG_SUCCESS)
		registration_failed = TRUE;
		
	if (bonobo_activation_active_server_register (RB_FACTORY_OAFIID, corba_object) != Bonobo_ACTIVATION_REG_SUCCESS)
		registration_failed = TRUE;
		
	CORBA_exception_init (&ev);

	if (registration_failed) {
		char *msg = rb_shell_corba_exception_to_string (&ev);
		g_set_error (error,
			     RB_REMOTE_BONOBO_ERROR,
			     RB_REMOTE_BONOBO_ERROR_ACQUISITION_FAILURE,
			     _("Failed to register the shell: %s\n"
			       "This probably means that you installed Rhythmbox in a "
			       "different prefix than bonobo-activation; this "
			       "warning is harmless, but IPC will not work."), msg);
		g_free (msg);
		rb_debug ("failed to register with bonobo activation"); 
	} else {
		rb_debug ("successfully registered with bonobo activation"); 
	}
	CORBA_exception_free (&ev);

	return !registration_failed;
}

/* Client methods */

static void
rb_remote_bonobo_client_handle_uri_impl (RBRemoteClientProxy *proxy, const char *uri)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	GNOME_Rhythmbox_handleFile (bonobo->priv->remote, uri, &ev);
	CORBA_exception_free (&ev);
}

static RBRemoteSong *
rb_remote_bonobo_client_get_playing_song_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	RBRemoteSong *song;
	Bonobo_PropertyBag pb;
	CORBA_any *any;
	CORBA_Environment ev;
	GNOME_Rhythmbox_SongInfo *song_info = NULL;

        /*
	 * Yes, this is insane.
	 */

	CORBA_exception_init (&ev);
	pb = GNOME_Rhythmbox_getPlayerProperties (bonobo->priv->remote, &ev);
	if (BONOBO_EX (&ev)) {
		char *err = bonobo_exception_get_text (&ev);
		g_warning (_("An exception occured '%s'"), err);
		return NULL;
	}

	any = bonobo_pbclient_get_value (pb, "song", 
					 TC_GNOME_Rhythmbox_SongInfo, 
					 &ev);
	if (BONOBO_EX (&ev)) {
		char *err = bonobo_exception_get_text (&ev);
		g_warning (_("An exception occured '%s'"), err);
		g_free (err);
		bonobo_object_release_unref ((Bonobo_Unknown) pb, &ev);
		return NULL;
	}
	
	if ((any == NULL) || (!CORBA_TypeCode_equivalent (any->_type, TC_GNOME_Rhythmbox_SongInfo, NULL))) {
		song_info = NULL;
	} else {
		song_info = (GNOME_Rhythmbox_SongInfo*)any->_value;
		any->_release = FALSE;
		CORBA_free (any);
	}

	song = g_new0 (RBRemoteSong, 1);
	song->title = g_strdup (song_info->title);
	song->artist = g_strdup (song_info->artist);
	song->genre = g_strdup (song_info->genre);
	song->album = g_strdup (song_info->album);
	song->uri = g_strdup (song_info->path);
	song->track_number = song_info->track_number;
	song->duration = song_info->duration;
	song->bitrate = song_info->bitrate;
	song->filesize = song_info->filesize;
	song->rating = song_info->rating;
	song->play_count = song_info->play_count;
	song->last_played = song_info->last_played;
								
	CORBA_free (song_info);

	bonobo_object_release_unref ((Bonobo_Unknown) pb, &ev);

	return song;
}

static void
rb_remote_bonobo_client_grab_focus_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	GNOME_Rhythmbox_grabFocus (bonobo->priv->remote, &ev);
	CORBA_exception_free (&ev);
}

static void
rb_remote_bonobo_client_toggle_shuffle_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	Bonobo_PropertyBag pb;
	gboolean shuffle;
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
		
	pb = GNOME_Rhythmbox_getPlayerProperties (bonobo->priv->remote, &ev);

	shuffle = bonobo_pbclient_get_boolean (pb,
					       "shuffle",
					       &ev);
	if (BONOBO_EX (&ev)) {
		return;
	}
		
	bonobo_pbclient_set_boolean (pb,
				     "shuffle",
				     shuffle ? FALSE : TRUE,
				     &ev);
	if (BONOBO_EX (&ev))
		return;

	bonobo_object_release_unref ((Bonobo_Unknown)pb, &ev);
}

static void
rb_remote_bonobo_client_toggle_playing_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	GNOME_Rhythmbox_playPause (bonobo->priv->remote, &ev);
	CORBA_exception_free (&ev);
}

static long
rb_remote_bonobo_client_get_playing_time_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	CORBA_Environment ev;
	long ret;

	CORBA_exception_init (&ev);

	ret = GNOME_Rhythmbox_getPlayingTime (bonobo->priv->remote, &ev);
	if (BONOBO_EX (&ev))
		ret = -1;

	CORBA_exception_free (&ev);

	return ret;
}

static void
rb_remote_bonobo_client_set_playing_time_impl (RBRemoteClientProxy *proxy, long time)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	GNOME_Rhythmbox_setPlayingTime (bonobo->priv->remote, time, &ev);
	CORBA_exception_free (&ev);
}

