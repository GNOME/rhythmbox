/*
 *  arch-tag: Implementation of Rhythmbox LIRC remote control object
 *
 *  Copyright (C) 2002 James Willcox  <jwillcox@gnome.org>
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
 *
 */


#include <config.h>
#include <glib.h>
#include <string.h>

#include "rb-remote.h"

#ifdef HAVE_REMOTE

#include <stdio.h>
#include <lirc/lirc_client.h>

/* strings that we recognize as commands from lirc */
#define RB_IR_COMMAND_PLAY "play"
#define RB_IR_COMMAND_PAUSE "pause"
#define RB_IR_COMMAND_SHUFFLE "shuffle"
#define RB_IR_COMMAND_REPEAT "repeat"
#define RB_IR_COMMAND_NEXT "next"
#define RB_IR_COMMAND_PREVIOUS "previous"
#define RB_IR_COMMAND_SEEK_FORWARD "seek_forward"
#define RB_IR_COMMAND_SEEK_BACKWARD "seek_backward"
#define RB_IR_COMMAND_VOLUME_UP "volume_up"
#define RB_IR_COMMAND_VOLUME_DOWN "volume_down"
#define RB_IR_COMMAND_MUTE "mute"
#define RB_IR_COMMAND_QUIT "quit"

struct _RBRemote {
	GObject parent;

};

enum
{
	BUTTON_PRESSED,
	LAST_SIGNAL
};

static guint rb_remote_signals[LAST_SIGNAL] = { 0 };
static GIOChannel *lirc_channel = NULL;
static GList *listeners = NULL;

static RBRemoteCommand
rb_lirc_to_command (const gchar *str)
{
	if (strcmp (str, RB_IR_COMMAND_PLAY) == 0)
		return RB_REMOTE_COMMAND_PLAY;
	else if (strcmp (str, RB_IR_COMMAND_PAUSE) == 0)
		return RB_REMOTE_COMMAND_PAUSE;
	else if (strcmp (str, RB_IR_COMMAND_SHUFFLE) == 0)
		return RB_REMOTE_COMMAND_SHUFFLE;
	else if (strcmp (str, RB_IR_COMMAND_REPEAT) == 0)
		return RB_REMOTE_COMMAND_REPEAT;
	else if (strcmp (str, RB_IR_COMMAND_NEXT) == 0)
		return RB_REMOTE_COMMAND_NEXT;
	else if (strcmp (str, RB_IR_COMMAND_PREVIOUS) == 0)
		return RB_REMOTE_COMMAND_PREVIOUS;
	else if (strcmp (str, RB_IR_COMMAND_SEEK_FORWARD) == 0)
		return RB_REMOTE_COMMAND_SEEK_FORWARD;
	else if (strcmp (str, RB_IR_COMMAND_SEEK_BACKWARD) == 0)
		return RB_REMOTE_COMMAND_SEEK_BACKWARD;
	else if (strcmp (str, RB_IR_COMMAND_VOLUME_UP) == 0)
		return RB_REMOTE_COMMAND_VOLUME_UP;
	else if (strcmp (str, RB_IR_COMMAND_VOLUME_DOWN) == 0)
		return RB_REMOTE_COMMAND_VOLUME_DOWN;
	else if (strcmp (str, RB_IR_COMMAND_MUTE) == 0)
		return RB_REMOTE_COMMAND_MUTE;
	else if (strcmp (str, RB_IR_COMMAND_QUIT) == 0)
		return RB_REMOTE_COMMAND_QUIT;
	else
		return RB_REMOTE_COMMAND_UNKNOWN;
}

static gboolean
rb_remote_read_code (GIOChannel *source, GIOCondition condition,
		   gpointer user_data)
{
	struct lirc_config *config;
	char *code;
	char *str = NULL;
	GList *tmp;
	RBRemoteCommand cmd;


	/* this _could_ block, but it shouldn't */
	lirc_nextcode (&code);

	if (code == NULL) {
		/* the code was incomplete or something */
		return TRUE;
	}
	
	/* FIXME:  we really should only do this once, but there appears to be
	 * a bug in lirc where it will drop every other key press if we keep
	 * a config struct around for more than one use.
	 */
	if (lirc_readconfig (NULL, &config, NULL) != 0) {
		g_warning ("Couldn't read lirc config.");
		return FALSE;
	}

	if (lirc_code2char (config, code, &str) != 0) {
		g_warning ("Couldn't convert lirc code to string.");
		lirc_freeconfig (config);
		return TRUE;
	}

	if (str == NULL) {
		/* there was no command associated with the code */
		lirc_freeconfig (config);
		g_free (code);
		return TRUE;
	}

	cmd = rb_lirc_to_command (str);

	tmp = listeners;
	while (tmp) {
		RBRemote *remote = tmp->data;

		g_signal_emit (remote, rb_remote_signals[BUTTON_PRESSED], 0,
		       cmd);

		tmp = tmp->next;
	}

	lirc_freeconfig (config);
	g_free (code);

	/* this causes a crash, so I guess I'm not supposed to free it?
	 * g_free (str);
	 */

	return TRUE;
}

static void
rb_remote_finalize (GObject *object)
{
	GError *error = NULL;
	RBRemote *remote;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_REMOTE (object));

	remote = RB_REMOTE (object);

	listeners = g_list_remove (listeners, remote);

	if (listeners == NULL && lirc_channel != NULL) {
		g_io_channel_shutdown (lirc_channel, FALSE, &error);
		if (error != NULL) {
			g_warning ("Couldn't destroy lirc connection: %s",
				   error->message);
			g_error_free (error);
		}

		lirc_deinit ();
	}
}

static void
rb_remote_class_init (RBRemote *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_remote_finalize;

	rb_remote_signals[BUTTON_PRESSED] =
		g_signal_new ("button_pressed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRemoteClass, button_pressed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_INT);
}


static void
rb_remote_init (RBRemote *remote)
{
	int fd;
	
	if (lirc_channel == NULL) {
		fd = lirc_init ("Rhythmbox", 1);

		if (fd < 0) {
			g_message ("Couldn't initialize lirc.\n");
			return;
		}
			
		lirc_channel = g_io_channel_unix_new (fd);

		g_io_add_watch (lirc_channel, G_IO_IN,
				(GIOFunc) rb_remote_read_code, NULL);	
	}

	listeners = g_list_prepend (listeners, remote);
}

GType
rb_remote_get_type (void)
{
	static GType type = 0;
                                                                              
	if (type == 0)
	{ 
		static GTypeInfo info =
		{
			sizeof (RBRemoteClass),
			NULL, 
			NULL,
			(GClassInitFunc) rb_remote_class_init, 
			NULL,
			NULL, 
			sizeof (RBRemote),
			0,
			(GInstanceInitFunc) rb_remote_init
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "RBRemote",
					       &info, 0);
	}

	return type;
}

RBRemote *
rb_remote_new (void)
{
	return g_object_new (RB_TYPE_REMOTE, NULL);
}

#endif /* HAVE_REMOTE */
