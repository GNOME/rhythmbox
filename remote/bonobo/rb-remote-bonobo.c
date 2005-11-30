/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

#include <config.h>

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <libgnome/libgnome.h>

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
static void rb_remote_bonobo_corba_skip (PortableServer_Servant _servant,
					 CORBA_long offset, CORBA_Environment *ev);
static void rb_remote_bonobo_corba_set_rating (PortableServer_Servant _servant,
					       CORBA_double rating, CORBA_Environment *ev);
static void rb_remote_bonobo_corba_toggle_mute (PortableServer_Servant _servant,
						CORBA_Environment *ev);

static Bonobo_PropertyBag rb_remote_bonobo_corba_get_player_properties (PortableServer_Servant _servant, CORBA_Environment *ev);

/* Server signal handlers */
static void rb_remote_bonobo_song_changed_cb (RBRemoteProxy *proxy,
					      const RBRemoteSong *song,
					      RBRemoteBonobo *bonobo);
static void rb_remote_bonobo_visibility_changed_cb (RBRemoteProxy *proxy, 
						    gboolean visible,
						    RBRemoteBonobo *bonobo);
static void rb_remote_bonobo_player_notify_cb (GObject *object,
					       GParamSpec *param,
					       RBRemoteBonobo *bonobo);

/* Client methods */
static void rb_remote_bonobo_client_handle_uri_impl (RBRemoteClientProxy *proxy, const char *uri);
static RBRemoteSong *rb_remote_bonobo_client_get_playing_song_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_grab_focus_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_toggle_visibility_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_set_visibility_impl (RBRemoteClientProxy *proxy, gboolean visible);
static gboolean rb_remote_bonobo_client_get_visibility_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_toggle_shuffle_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_set_shuffle_impl (RBRemoteClientProxy *proxy, gboolean visible);
static gboolean rb_remote_bonobo_client_get_shuffle_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_toggle_repeat_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_set_repeat_impl (RBRemoteClientProxy *proxy, gboolean visible);
static gboolean rb_remote_bonobo_client_get_repeat_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_toggle_playing_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_play_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_pause_impl (RBRemoteClientProxy *proxy);
static long rb_remote_bonobo_client_get_playing_time_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_set_playing_time_impl (RBRemoteClientProxy *proxy, long time);
static void rb_remote_bonobo_client_jump_next_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_jump_previous_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_quit_impl (RBRemoteClientProxy *proxy);

static void rb_remote_bonobo_client_set_rating_impl (RBRemoteClientProxy *proxy, double rating);
static void rb_remote_bonobo_client_seek_impl (RBRemoteClientProxy *proxy, long offset);
static void rb_remote_bonobo_client_set_volume_impl (RBRemoteClientProxy *proxy, float volume);
static float rb_remote_bonobo_client_get_volume_impl (RBRemoteClientProxy *proxy);
static void rb_remote_bonobo_client_toggle_mute_impl (RBRemoteClientProxy *proxy);


static GObjectClass *parent_class;

enum
{
	PROP_0,
	PROP_VISIBILITY,
	PROP_SHUFFLE,
	PROP_REPEAT,
	PROP_SONG,
	PROP_LAST_STATIC
};

struct RBRemoteBonoboPrivate
{
	gboolean disposed;

	GNOME_Rhythmbox remote;
	
	RBRemoteProxy *proxy;

	BonoboPropertyBag *pb;

	guint next_property;
	GParamSpec *property_spec[16];
};

#define RB_REMOTE_BONOBO_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_REMOTE_BONOBO, RBRemoteBonoboPrivate))

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
	epv->setRating = rb_remote_bonobo_corba_set_rating;
	epv->skip = rb_remote_bonobo_corba_skip;
	epv->toggleMute = rb_remote_bonobo_corba_toggle_mute;

	g_type_class_add_private (klass, sizeof (RBRemoteBonoboPrivate));
}

static void
rb_remote_bonobo_remote_client_proxy_init (RBRemoteClientProxyIface *iface)
{
	iface->handle_uri = rb_remote_bonobo_client_handle_uri_impl;
	iface->get_playing_song = rb_remote_bonobo_client_get_playing_song_impl;
	iface->grab_focus = rb_remote_bonobo_client_grab_focus_impl;
	iface->toggle_shuffle = rb_remote_bonobo_client_toggle_shuffle_impl;
	iface->set_shuffle = rb_remote_bonobo_client_set_shuffle_impl;
	iface->get_shuffle = rb_remote_bonobo_client_get_shuffle_impl;
	iface->toggle_repeat = rb_remote_bonobo_client_toggle_repeat_impl;
	iface->set_repeat = rb_remote_bonobo_client_set_repeat_impl;
	iface->get_repeat = rb_remote_bonobo_client_get_repeat_impl;
	iface->toggle_playing = rb_remote_bonobo_client_toggle_playing_impl;
	iface->play = rb_remote_bonobo_client_play_impl;
	iface->pause = rb_remote_bonobo_client_pause_impl;
	iface->get_playing_time = rb_remote_bonobo_client_get_playing_time_impl;
	iface->set_playing_time = rb_remote_bonobo_client_set_playing_time_impl;
	iface->jump_next = rb_remote_bonobo_client_jump_next_impl;
	iface->jump_previous = rb_remote_bonobo_client_jump_previous_impl;
	iface->quit = rb_remote_bonobo_client_quit_impl;
	iface->set_rating = rb_remote_bonobo_client_set_rating_impl;
	iface->seek = rb_remote_bonobo_client_seek_impl;
	iface->set_volume = rb_remote_bonobo_client_set_volume_impl;
	iface->get_volume = rb_remote_bonobo_client_get_volume_impl;
	iface->toggle_mute = rb_remote_bonobo_client_toggle_mute_impl;
	iface->toggle_visibility = rb_remote_bonobo_client_toggle_visibility_impl;
	iface->set_visibility = rb_remote_bonobo_client_set_visibility_impl;
	iface->get_visibility = rb_remote_bonobo_client_get_visibility_impl;
}

/* hack to use a recursive mutex for gdk locking */
static GStaticRecMutex _rb_bonobo_mutex = G_STATIC_REC_MUTEX_INIT;
static gboolean set_lock_functions = FALSE;

static void
rb_bonobo_workaround_lock (void)
{
	g_static_rec_mutex_lock (&_rb_bonobo_mutex);
}

static void
rb_bonobo_workaround_unlock (void)
{
	g_static_rec_mutex_unlock (&_rb_bonobo_mutex);
}

void
rb_remote_bonobo_preinit (void)
{

	/*
	 * By default, GDK_THREADS_ENTER/GDK_THREADS_LEAVE uses a 
	 * non-recursive mutex; this leads to deadlock, as there are
	 * many code paths that lead to (for example) gconf operations
	 * with the gdk lock held.  While performing these gconf operations,
	 * ORBit will process incoming bonobo remote interface requests.
	 * The implementations of the bonobo request handlers attempt
	 * to acquire the gdk lock (as far as I know this is necessary, as
	 * some operations will result in UI updates etc.); if the mutex
	 * does not support recursive locks, this will deadlock.
	 *
	 * Dropping the gdk lock before all code paths that will possibly
	 * lead to a gconf operation is way too hard (they're *everywhere*),
	 * and unless someone can find a way of implementing the entire
	 * remote interface without needing to acquire the gdk lock, this
	 * is what we're stuck with.
	 *
	 ***
	 *
	 * Note: this is why CORBA sucks.  Arbitrary reentrancy is
	 * nearly impossible to get right in an application with
	 * significant global state (as nearly every GUI app has). The
	 * D-BUS approach of queueing requests may lead to deadlocks,
	 * but it's very obvious when this happens, and it's a lot
	 * easier to debug and fix.  The above approach of making the
	 * GDK lock recursive only partially helps; I am certain
	 * there are code paths in Rhythmbox which are not expecting
	 * to be reentered, particularly in RBShellPlayer.
	 */
	if (!set_lock_functions)
	  {
	    gdk_threads_set_lock_functions (G_CALLBACK (rb_bonobo_workaround_lock),
					    G_CALLBACK (rb_bonobo_workaround_unlock));
	    set_lock_functions = TRUE;
	  }
}

static void
rb_remote_bonobo_init (RBRemoteBonobo *bonobo) 
{
	bonobo->priv = RB_REMOTE_BONOBO_GET_PRIVATE (bonobo);
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
	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

RBRemoteBonobo *
rb_remote_bonobo_new (void)
{
	return g_object_new (RB_TYPE_REMOTE_BONOBO, NULL, NULL);
}


static void
rb_remote_bonobo_corba_quit (PortableServer_Servant _servant,
			     CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));

	GDK_THREADS_ENTER ();
	rb_remote_proxy_quit (bonobo->priv->proxy);
	GDK_THREADS_LEAVE ();
}

static void
rb_remote_bonobo_corba_handle_file (PortableServer_Servant _servant,
				    const CORBA_char *uri,
				    CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));

	GDK_THREADS_ENTER ();
	rb_remote_proxy_load_uri (bonobo->priv->proxy, uri, TRUE);
	GDK_THREADS_LEAVE ();
}

static void
rb_remote_bonobo_corba_add_to_library (PortableServer_Servant _servant,
				       const CORBA_char *uri,
				       CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));

	GDK_THREADS_ENTER ();
	rb_remote_proxy_load_uri (bonobo->priv->proxy, uri, FALSE);
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
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));
	GDK_THREADS_ENTER ();
	rb_remote_proxy_jump_next (bonobo->priv->proxy);
	GDK_THREADS_LEAVE ();
}

static void
rb_remote_bonobo_corba_previous (PortableServer_Servant _servant,
				 CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));
	GDK_THREADS_ENTER ();
	rb_remote_proxy_jump_previous (bonobo->priv->proxy);
	GDK_THREADS_LEAVE ();
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

static void
rb_remote_bonobo_corba_skip (PortableServer_Servant _servant,
			     CORBA_long offset, CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));
	GDK_THREADS_ENTER ();
	rb_remote_proxy_seek (bonobo->priv->proxy, (long) offset);
	GDK_THREADS_LEAVE ();
}

static void
rb_remote_bonobo_corba_set_rating (PortableServer_Servant _servant,
				   CORBA_double rating, CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));
	GDK_THREADS_ENTER ();
	rb_remote_proxy_set_rating (bonobo->priv->proxy, rating);
	GDK_THREADS_LEAVE ();
}

static void
rb_remote_bonobo_corba_toggle_mute (PortableServer_Servant _servant,
				    CORBA_Environment *ev)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));

	GDK_THREADS_ENTER ();
	rb_remote_proxy_toggle_mute (bonobo->priv->proxy);
	GDK_THREADS_LEAVE ();
}

static GNOME_Rhythmbox_SongInfo *
convert_from_rb_remote_song (const RBRemoteSong *song)
{
	GNOME_Rhythmbox_SongInfo *song_info = GNOME_Rhythmbox_SongInfo__alloc ();
	song_info->title = CORBA_string_dup (song->title);
	song_info->artist = CORBA_string_dup (song->artist);
	song_info->album = CORBA_string_dup (song->album);
	song_info->genre = CORBA_string_dup (song->genre);
	song_info->path = CORBA_string_dup (song->uri);
	song_info->track_number = song->track_number;
	song_info->duration = song->duration;
	song_info->bitrate = song->bitrate;
	song_info->filesize = song->filesize;
	song_info->rating = (long)song->rating;
	song_info->play_count = song->play_count;
	song_info->last_played = song->last_played;
	return song_info;
}

static GNOME_Rhythmbox_SongInfo *
get_song_info_from_player (RBRemoteProxy *proxy)
{
	gchar *uri;
	RBRemoteSong *song = NULL;
	GNOME_Rhythmbox_SongInfo *song_info = NULL;

	GDK_THREADS_ENTER ();
	uri = rb_remote_proxy_get_playing_uri (proxy);
	if (uri != NULL) {
		song = g_new0 (RBRemoteSong, 1);
		if (song != NULL) {
			if (rb_remote_proxy_get_song_info (proxy, uri, song) == TRUE) {
				song_info = convert_from_rb_remote_song (song);
			}
			rb_remote_song_free (song);
		}
	}
	GDK_THREADS_LEAVE ();
	g_free (uri);
	
	return song_info;
}

static void
bonobo_pb_get_prop (BonoboPropertyBag *bag,
		    BonoboArg         *arg,
		    guint              arg_id,
		    CORBA_Environment *ev,
		    gpointer           user_data)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (user_data);

	if ((arg_id >= PROP_LAST_STATIC) && 
	    (arg_id < (bonobo->priv->next_property + PROP_LAST_STATIC))) {
		GValue value = {0};
		RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (user_data);
		GParamSpec *param = bonobo->priv->property_spec[arg_id - PROP_LAST_STATIC];

		g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (param));
		GDK_THREADS_ENTER ();
		rb_remote_proxy_get_player_property (bonobo->priv->proxy, 
						     param->name,
						     &value);
		GDK_THREADS_LEAVE ();
		bonobo_arg_from_gvalue (arg, &value);
		g_value_unset (&value);
		return;
	}
	
	switch (arg_id) {

	case PROP_VISIBILITY:
	{
		gboolean visibility;
		GDK_THREADS_ENTER ();
		visibility = rb_remote_proxy_get_visibility (bonobo->priv->proxy);
		GDK_THREADS_LEAVE ();
		BONOBO_ARG_SET_BOOLEAN (arg, visibility);
		break;
	}

	case PROP_SHUFFLE:
	{
		gboolean shuffle;
		GDK_THREADS_ENTER ();
		shuffle = rb_remote_proxy_get_shuffle (bonobo->priv->proxy);
		GDK_THREADS_LEAVE ();
		BONOBO_ARG_SET_BOOLEAN (arg, shuffle);
		break;
	}

	case PROP_REPEAT:
	{
		gboolean repeat;
		GDK_THREADS_ENTER ();
		repeat = rb_remote_proxy_get_repeat (bonobo->priv->proxy);
		GDK_THREADS_LEAVE ();
		BONOBO_ARG_SET_BOOLEAN (arg, repeat);
		break;
	}

	case PROP_SONG: 
	{
		GNOME_Rhythmbox_SongInfo *ret_val;
		
		ret_val = get_song_info_from_player (bonobo->priv->proxy);
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
}

static void
bonobo_pb_set_prop (BonoboPropertyBag *bag,
		   const BonoboArg   *arg,
		   guint              arg_id,
		   CORBA_Environment *ev,
		   gpointer           user_data)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (user_data);

	if ((arg_id >= PROP_LAST_STATIC) && 
	    (arg_id < (bonobo->priv->next_property + PROP_LAST_STATIC))) {
		GValue value = {0};
		RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (user_data);
		GParamSpec *param = bonobo->priv->property_spec[arg_id - PROP_LAST_STATIC];

		g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (param));
		bonobo_arg_to_gvalue (&value, arg);
		GDK_THREADS_ENTER ();
		rb_remote_proxy_set_player_property (bonobo->priv->proxy, 
						     param->name, 
						     &value);
		GDK_THREADS_LEAVE ();
		g_value_unset (&value);
		return;
	}
	
	switch (arg_id) {

	case PROP_VISIBILITY:
	{
		gboolean visible = BONOBO_ARG_GET_BOOLEAN (arg);
		GDK_THREADS_ENTER ();
		rb_remote_proxy_set_visibility (bonobo->priv->proxy, visible);
		GDK_THREADS_LEAVE ();
		break;
	}

	case PROP_SONG:
		bonobo_exception_set (ev, ex_Bonobo_PropertyBag_ReadOnly);
		break;

	case PROP_SHUFFLE:
	{
		gboolean shuffle = BONOBO_ARG_GET_BOOLEAN (arg);
		GDK_THREADS_ENTER ();
		rb_remote_proxy_set_shuffle (bonobo->priv->proxy, shuffle);
		GDK_THREADS_LEAVE ();
		break;
	}

	case PROP_REPEAT:
	{
		gboolean repeat = BONOBO_ARG_GET_BOOLEAN (arg);
		GDK_THREADS_ENTER ();
		rb_remote_proxy_set_repeat (bonobo->priv->proxy, repeat);
		GDK_THREADS_LEAVE ();
		break;
	}

	default:
		bonobo_exception_set (ev, ex_Bonobo_PropertyBag_NotFound);
		break;
	}
}

static void
rb_remote_bonobo_add_player_property (RBRemoteBonobo *bonobo,
				      const gchar *property,
				      const gchar *description)
{
	guint prop_id;
	GParamSpec *param;

	g_assert (bonobo->priv->next_property < G_N_ELEMENTS (bonobo->priv->property_spec));
	prop_id = bonobo->priv->next_property++;
	param = rb_remote_proxy_find_player_property (bonobo->priv->proxy,
								  property);
	
	bonobo->priv->property_spec[prop_id] = param;
	bonobo_property_bag_add (bonobo->priv->pb, property, 
				 prop_id + PROP_LAST_STATIC, 
				 bonobo_arg_type_from_gtype (G_PARAM_SPEC_VALUE_TYPE (param)), 
				 NULL,
				 description, 0);

}


static Bonobo_PropertyBag
rb_remote_bonobo_corba_get_player_properties (PortableServer_Servant _servant, 
				      CORBA_Environment *ev)
{	
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (bonobo_object (_servant));

	GDK_THREADS_ENTER ();
	if (bonobo->priv->pb == NULL) {
		bonobo->priv->pb = bonobo_property_bag_new (bonobo_pb_get_prop, 
							    bonobo_pb_set_prop, 
							    bonobo);
		
		
		bonobo_property_bag_add (bonobo->priv->pb, "visibility", 
					 PROP_VISIBILITY, BONOBO_ARG_BOOLEAN, NULL, 
					 _("Whether the main window is visible"), 0);

		bonobo_property_bag_add (bonobo->priv->pb, "shuffle", 
					 PROP_SHUFFLE, BONOBO_ARG_BOOLEAN, NULL, 
					 _("Whether shuffle is enabled"), 0);
		
		bonobo_property_bag_add (bonobo->priv->pb, "repeat", 
					 PROP_REPEAT, BONOBO_ARG_BOOLEAN, NULL, 
					 _("Whether repeat is enabled"), 0);

		bonobo_property_bag_add (bonobo->priv->pb, "song", 
					 PROP_SONG, TC_GNOME_Rhythmbox_SongInfo, NULL, 
					 _("Properties for the current song"), 0);

		rb_remote_proxy_player_notify_handler (bonobo->priv->proxy,
						       G_CALLBACK (rb_remote_bonobo_player_notify_cb),
						       bonobo);

		rb_remote_bonobo_add_player_property (bonobo,
						      "playing",
						      _("Whether Rhythmbox is currently playing"));

		rb_remote_bonobo_add_player_property (bonobo,
						      "play-order",
						      _("What play order to use"));

		rb_remote_bonobo_add_player_property (bonobo,
						      "volume",
						      _("Current playback volume"));

	}
	GDK_THREADS_LEAVE ();

	/* If the creation of the property bag failed, 
	 * return a corba exception
	 */
	
	return bonobo_object_dup_ref (BONOBO_OBJREF (bonobo->priv->pb), NULL);
}

static void
rb_remote_bonobo_song_changed_cb (RBRemoteProxy *proxy,
				  const RBRemoteSong *song,
				  RBRemoteBonobo *bonobo)
{
	GNOME_Rhythmbox_SongInfo *song_info;
	BonoboArg *arg;

	if (bonobo->priv->pb == NULL)
		return;
	
	arg = bonobo_arg_new (TC_GNOME_Rhythmbox_SongInfo);
	song_info = convert_from_rb_remote_song (song);
	arg->_value = (gpointer)song_info;
	if (bonobo->priv->pb != NULL) {
		bonobo_event_source_notify_listeners_full (bonobo->priv->pb->es,
							   "Bonobo/Property",
							   "change",
							   "song",
							   arg, NULL);
	}
	
	bonobo_arg_release (arg);
}

static void
rb_remote_bonobo_visibility_changed_cb (RBRemoteProxy *proxy,
					gboolean visible,
					RBRemoteBonobo *bonobo)
{
	BonoboArg *arg;

	if (bonobo->priv->pb == NULL)
		return;
	
	arg = bonobo_arg_new (TC_CORBA_boolean);
	BONOBO_ARG_SET_BOOLEAN (arg, visible);
	bonobo_event_source_notify_listeners_full (bonobo->priv->pb->es,
						   "Bonobo/Property",
						   "change",
						   "visibility",
						   arg, NULL);
	
	bonobo_arg_release (arg);
}
					

static void
rb_remote_bonobo_player_notify_cb (GObject *object,
				   GParamSpec *param,
				   RBRemoteBonobo *bonobo)
{
	GValue value = {0};
	BonoboArg *arg;
	BonoboArgType arg_type;

	if (bonobo->priv->pb == NULL)
		return;

	arg_type = bonobo_arg_type_from_gtype (G_PARAM_SPEC_VALUE_TYPE (param));
	if (arg_type == 0)
		return;
	
	arg = bonobo_arg_new (arg_type);
	g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (param));
	GDK_THREADS_ENTER ();
	rb_remote_proxy_get_player_property (bonobo->priv->proxy,
					     param->name,
					     &value);
	GDK_THREADS_LEAVE ();
	bonobo_arg_from_gvalue (arg, &value);
	bonobo_event_source_notify_listeners_full (bonobo->priv->pb->es,
						   "Bonobo/Property",
						   "change",
						   param->name,
						   arg, NULL);
	g_value_unset (&value);
	bonobo_arg_release (arg);
}

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

	g_signal_connect_object (G_OBJECT (proxy),
				 "song_changed",
				 G_CALLBACK (rb_remote_bonobo_song_changed_cb),
				 bonobo, 0);
				 
	g_signal_connect_object (G_OBJECT (proxy),
				 "visibility_changed",
				 G_CALLBACK (rb_remote_bonobo_visibility_changed_cb),
				 bonobo, 0);
				 

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
	RBRemoteSong *song = NULL;
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

	if (song_info != NULL) {
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
	}

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
_rb_remote_bonobo_client_toggle_property (RBRemoteBonobo *bonobo,
					  const char *property)
{
	Bonobo_PropertyBag pb;
	gboolean v;
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	pb = GNOME_Rhythmbox_getPlayerProperties (bonobo->priv->remote, &ev);

	v  = bonobo_pbclient_get_boolean (pb,
					  property,
					  &ev);
	if (BONOBO_EX (&ev)) {
		return;
	}
		
	bonobo_pbclient_set_boolean (pb,
				     property,
				     v ? FALSE : TRUE,
				     &ev);
	if (BONOBO_EX (&ev))
		return;

	bonobo_object_release_unref ((Bonobo_Unknown)pb, &ev);
}

static gboolean
_rb_remote_bonobo_client_get_boolean_property (RBRemoteBonobo *bonobo,
					       const char *property)
{
	Bonobo_PropertyBag pb;
	gboolean v;
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	pb = GNOME_Rhythmbox_getPlayerProperties (bonobo->priv->remote, &ev);

	v  = bonobo_pbclient_get_boolean (pb,
					  property,
					  &ev);
	bonobo_object_release_unref ((Bonobo_Unknown)pb, &ev);
	return v;
}

static void
_rb_remote_bonobo_client_set_boolean_property (RBRemoteBonobo *bonobo,
					       const char *property,
					       gboolean value)
{
	Bonobo_PropertyBag pb;
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	pb = GNOME_Rhythmbox_getPlayerProperties (bonobo->priv->remote, &ev);

	bonobo_pbclient_set_boolean (pb,
				     property,
				     value,
				     &ev);
	bonobo_object_release_unref ((Bonobo_Unknown)pb, &ev);
}

static void
rb_remote_bonobo_client_toggle_visibility_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	_rb_remote_bonobo_client_toggle_property (bonobo, "visibility");
}

static gboolean 
rb_remote_bonobo_client_get_visibility_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	return _rb_remote_bonobo_client_get_boolean_property (bonobo, "visibility");
}

static void
rb_remote_bonobo_client_set_visibility_impl (RBRemoteClientProxy *proxy,
					     gboolean visible)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	_rb_remote_bonobo_client_set_boolean_property (bonobo, "visibility", visible);
}


static void
rb_remote_bonobo_client_toggle_shuffle_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	_rb_remote_bonobo_client_toggle_property (bonobo, "shuffle");
}

static gboolean 
rb_remote_bonobo_client_get_shuffle_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	return _rb_remote_bonobo_client_get_boolean_property (bonobo, "shuffle");
}

static void
rb_remote_bonobo_client_set_shuffle_impl (RBRemoteClientProxy *proxy,
					  gboolean shuffle)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	_rb_remote_bonobo_client_set_boolean_property (bonobo, "shuffle", shuffle);
}

static void
rb_remote_bonobo_client_toggle_repeat_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	_rb_remote_bonobo_client_toggle_property (bonobo, "repeat");
}

static gboolean 
rb_remote_bonobo_client_get_repeat_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	return _rb_remote_bonobo_client_get_boolean_property (bonobo, "repeat");
}

static void
rb_remote_bonobo_client_set_repeat_impl (RBRemoteClientProxy *proxy,
					 gboolean repeat)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	_rb_remote_bonobo_client_set_boolean_property (bonobo, "repeat", repeat);
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

static void
_rb_remote_bonobo_client_set_playing (RBRemoteClientProxy *proxy, gboolean play)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	gboolean is_playing;
	CORBA_Environment ev;

	is_playing = _rb_remote_bonobo_client_get_boolean_property (bonobo, "playing");
	
	if (is_playing != play) {
		CORBA_exception_init (&ev);
		rb_remote_bonobo_client_toggle_playing_impl (proxy);
		CORBA_exception_free (&ev);
	}

}

static void
rb_remote_bonobo_client_play_impl (RBRemoteClientProxy *proxy)
{
	_rb_remote_bonobo_client_set_playing (proxy, TRUE);
}

static void
rb_remote_bonobo_client_pause_impl (RBRemoteClientProxy *proxy)
{
	_rb_remote_bonobo_client_set_playing (proxy, FALSE);
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

static void
rb_remote_bonobo_client_jump_next_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	GNOME_Rhythmbox_next (bonobo->priv->remote, &ev);
	CORBA_exception_free (&ev);
}

static void
rb_remote_bonobo_client_jump_previous_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	GNOME_Rhythmbox_previous (bonobo->priv->remote, &ev);
	CORBA_exception_free (&ev);
}


static void
rb_remote_bonobo_client_quit_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	GNOME_Rhythmbox_quit (bonobo->priv->remote, &ev);
	CORBA_exception_free (&ev);
}

static void
rb_remote_bonobo_client_set_rating_impl (RBRemoteClientProxy *proxy, double rating)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	GNOME_Rhythmbox_setRating (bonobo->priv->remote, rating, &ev);
	CORBA_exception_free (&ev);
}

static void
rb_remote_bonobo_client_seek_impl (RBRemoteClientProxy *proxy, long offset)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	GNOME_Rhythmbox_skip (bonobo->priv->remote, offset, &ev);
	CORBA_exception_free (&ev);
}

static void
rb_remote_bonobo_client_set_volume_impl (RBRemoteClientProxy *proxy, float volume)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	Bonobo_PropertyBag pb;
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	pb = GNOME_Rhythmbox_getPlayerProperties (bonobo->priv->remote, &ev);

	bonobo_pbclient_set_float (pb,
				   "volume",
				   volume,
				   &ev);
	bonobo_object_release_unref ((Bonobo_Unknown)pb, &ev);
}

static float 
rb_remote_bonobo_client_get_volume_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	Bonobo_PropertyBag pb;
	CORBA_Environment ev;
	float v = 1.0f;

	CORBA_exception_init (&ev);

	pb = GNOME_Rhythmbox_getPlayerProperties (bonobo->priv->remote, &ev);

	v = bonobo_pbclient_get_float (pb,
				       "volume",
				       &ev);
	bonobo_object_release_unref ((Bonobo_Unknown)pb, &ev);
	CORBA_exception_free (&ev);

	return v;
}

static void
rb_remote_bonobo_client_toggle_mute_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteBonobo *bonobo = RB_REMOTE_BONOBO (proxy);
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	GNOME_Rhythmbox_toggleMute (bonobo->priv->remote, &ev);
	CORBA_exception_free (&ev);
}


