/*
 *  Copyright © 2002 Jorn Baayen.  All rights reserved.
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
 *  $Id$
 */

#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-context.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-ui-util.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-window.h>
#include <bonobo/bonobo-control-frame.h>
#include <bonobo-activation/bonobo-activation-register.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtkhseparator.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkmain.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkvbox.h>
#include <config.h>
#include <libgnome/libgnome.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-init.h>
#include <libgnome/gnome-program.h>
#include <libgnomeui/gnome-window-icon.h>
#include <libgnomeui/gnome-about.h>
#include <string.h>
#include <sys/stat.h>

#include "Rhythmbox.h"
#include "rb.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-file-helpers.h"
#include "rb-bonobo-helpers.h"
#include "rb-library.h"
#include "rb-thread-helpers.h"
#include "eel-gconf-extensions.h"
#include "rb-library-view.h"

static void rb_class_init (RBClass *klass);
static void rb_init (RB *rb);
static void rb_finalize (GObject *object);
static void rb_corba_quit (PortableServer_Servant _servant,
                                 CORBA_Environment *ev);
static void rb_corba_add_to_library (PortableServer_Servant _servant,
					   const CORBA_char *uri,
					   CORBA_Environment *ev);
static void rb_corba_grab_focus (PortableServer_Servant _servant,
				       CORBA_Environment *ev);
static gboolean rb_window_state_cb (GtkWidget *widget,
					  GdkEvent *event,
					  RB *rb);
static gboolean rb_window_delete_cb (GtkWidget *win,
			                   GdkEventAny *event,
			                   RB *rb);
static void rb_window_load_state (RB *rb);
static void rb_window_save_state (RB *rb);
static void rb_cmd_about (BonoboUIComponent *component,
		                RB *rb,
		                const char *verbname);
static void rb_cmd_contents (BonoboUIComponent *component,
		                RB *rb,
		                const char *verbname);
static void rb_cmd_close (BonoboUIComponent *component,
		                RB *rb,
			        const char *verbname);
static void rb_quit (RB *rb);

/* prefs */
#define CONF_STATE_WINDOW_WIDTH     "/apps/rhythmbox/state/window_width"
#define CONF_STATE_WINDOW_HEIGHT    "/apps/rhythmbox/state/window_height"
#define CONF_STATE_WINDOW_MAXIMIZED "/apps/rhythmbox/state/window_maximized"

typedef struct
{
	int width;
	int height;
	gboolean maximized;
} RBWindowState;

struct RBPrivate
{
	GtkWidget *window;

	BonoboUIComponent *ui_component;
	BonoboUIContainer *container;

	RBCommander *commander;
	RBPlayer *player;
	RBLibrary *library;

	GtkWidget *notebook;

	RBWindowState *state;
};

static BonoboUIVerb rb_verbs[] =
{
	BONOBO_UI_VERB ("About",        (BonoboUIVerbFn) rb_cmd_about),
	BONOBO_UI_VERB ("Contents",	(BonoboUIVerbFn) rb_cmd_contents),
	BONOBO_UI_VERB ("Close",        (BonoboUIVerbFn) rb_cmd_close),
	BONOBO_UI_VERB_END
};

static GObjectClass *parent_class;

GType
rb_get_type (void)
{
	static GType type = 0;

	if (type == 0)
	{
		static GTypeInfo info =
		{
			sizeof (RBClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_class_init,
			NULL,
			NULL,
			sizeof (RB),
			0,
			(GInstanceInitFunc) rb_init
		};

		type = bonobo_type_unique (BONOBO_TYPE_OBJECT,
					   POA_GNOME_Rhythmbox__init,
					   POA_GNOME_Rhythmbox__fini,
					   G_STRUCT_OFFSET (RBClass, epv),
					   &info,
					   "Rhythmbox");
	}

	return type;
}

static void
rb_class_init (RBClass *klass)
{
        GObjectClass *object_class = (GObjectClass *) klass;
        POA_GNOME_Rhythmbox__epv *epv = &klass->epv;

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize = rb_finalize;

	epv->quit         = rb_corba_quit;
	epv->addToLibrary = rb_corba_add_to_library;
	epv->grabFocus    = rb_corba_grab_focus;
}

static void
rb_init (RB *rb)
{
	char *file;

	rb_thread_helpers_init ();

	rb->priv = g_new0 (RBPrivate, 1);

	rb_ensure_dir_exists (rb_dot_dir ());

	file = gnome_program_locate_file (NULL,
					  GNOME_FILE_DOMAIN_APP_PIXMAP,
					  "rhythmbox.png", TRUE, NULL);
	gnome_window_icon_set_default_from_file (file);
	g_free (file);

	rb->priv->state = g_new0 (RBWindowState, 1);

	eel_gconf_monitor_add ("/apps/rhythmbox");
}

static void
rb_finalize (GObject *object)
{
        RB *rb = RB (object);

	gtk_widget_hide (rb->priv->window);

	while (gtk_events_pending ())
		gtk_main_iteration ();

	eel_gconf_monitor_remove ("/apps/rhythmbox");

	bonobo_activation_active_server_unregister (RB_OAFIID,
						    bonobo_object_corba_objref (BONOBO_OBJECT (rb)));

	rb_debug ("Unregistered with Bonobo Activation");

	gtk_widget_destroy (rb->priv->window);

	g_object_unref (G_OBJECT (rb->priv->commander));
	g_object_unref (G_OBJECT (rb->priv->library));

	g_free (rb->priv->state);

	g_free (rb->priv);

        parent_class->finalize (G_OBJECT (rb));

	bonobo_main_quit ();
}

RB *
rb_new (void)
{
	RB *s;

	s = g_object_new (RB_TYPE, NULL);

	return s;
}

static void
rb_corba_quit (PortableServer_Servant _servant,
               CORBA_Environment *ev)
{
	RB *rb = RB (bonobo_object (_servant));

	GDK_THREADS_ENTER ();

	rb_quit (rb);

	GDK_THREADS_LEAVE ();
}

static void
rb_corba_add_to_library (PortableServer_Servant _servant,
			 const CORBA_char *uri,
			 CORBA_Environment *ev)
{
	RB *rb = RB (bonobo_object (_servant));

	rb_library_add_uri (rb->priv->library, (char *) uri);
}

static void
rb_corba_grab_focus (PortableServer_Servant _servant,
		     CORBA_Environment *ev)
{
	RB *rb = RB (bonobo_object (_servant));

	gtk_window_present (GTK_WINDOW (rb->priv->window));
	gtk_widget_grab_focus (rb->priv->window);
}

void
rb_construct (RB *rb)
{
	CORBA_Object corba_object;
	CORBA_Environment ev;
	BonoboWindow *win;
	Bonobo_UIContainer corba_container;
	GtkWidget *vbox, *sep;

	g_return_if_fail (RB_IS (rb));

	rb_debug ("Constructing rb");

	/* register with CORBA */
	CORBA_exception_init (&ev);

	corba_object = bonobo_object_corba_objref (BONOBO_OBJECT (rb));
	if (bonobo_activation_active_server_register (RB_OAFIID, corba_object) != Bonobo_ACTIVATION_REG_SUCCESS)
	{
		/* this is not critical, but worth a warning nevertheless */
		g_message (_("Failed to register Rhythmbox with bonobo activation:\n"
			     "This probably means that you installed RB in a\n"
			     "different prefix than bonobo-activation; this\n"
			     "warning is harmless, but IPC will not work.\n"));
	}

	CORBA_exception_free (&ev);

	rb_debug ("Registered with Bonobo Activation");

	/* initialize UI */
	win = BONOBO_WINDOW (bonobo_window_new ("Rhythmbox",
						_("Music Player")));

	rb->priv->window = GTK_WIDGET (win);

	g_signal_connect (G_OBJECT (win), "window_state_event",
			  G_CALLBACK (rb_window_state_cb),
			  rb);
	g_signal_connect (G_OBJECT (win), "configure_event",
			  G_CALLBACK (rb_window_state_cb),
			  rb);
	g_signal_connect (G_OBJECT (win), "delete_event",
			  G_CALLBACK (rb_window_delete_cb),
			  rb);

	rb->priv->container = bonobo_window_get_ui_container (win);

	bonobo_ui_engine_config_set_path (bonobo_window_get_ui_engine (win),
					  "/apps/rhythmbox/UIConfig/kvps");

	corba_container = BONOBO_OBJREF (rb->priv->container);

	rb->priv->ui_component = bonobo_ui_component_new_default ();

	bonobo_ui_component_set_container (rb->priv->ui_component,
					   corba_container,
					   NULL);

	bonobo_ui_component_freeze (rb->priv->ui_component, NULL);

	bonobo_ui_util_set_ui (rb->priv->ui_component,
			       DATADIR,
			       "rhythmbox-ui.xml",
			       "rhythmbox", NULL);

	bonobo_ui_component_add_verb_list_with_data (rb->priv->ui_component,
						     rb_verbs,
						     rb);

	/* initialize rb services */
	rb->priv->library = rb_library_new ();
	rb->priv->player = rb_player_new (rb);
	gtk_container_set_border_width (GTK_CONTAINER (rb->priv->player), 5);
	rb->priv->commander = rb_commander_new (rb);

	rb->priv->notebook = gtk_notebook_new ();
	gtk_container_set_border_width (GTK_CONTAINER (rb->priv->notebook), 5);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (rb->priv->notebook), FALSE);
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (rb->priv->notebook), FALSE);

	gtk_notebook_append_page (GTK_NOTEBOOK (rb->priv->notebook),
				  GTK_WIDGET (rb_library_view_new (rb)), NULL);

	vbox = gtk_vbox_new (FALSE, 0);

	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (rb->priv->player),
			    FALSE, TRUE, 0);
	gtk_widget_show (vbox);

	sep = gtk_hseparator_new ();
	gtk_box_pack_start (GTK_BOX (vbox), sep,
			    FALSE, TRUE, 0);
	gtk_widget_show (sep);

	gtk_box_pack_start (GTK_BOX (vbox), rb->priv->notebook,
			    TRUE, TRUE, 0);
	gtk_widget_show (rb->priv->notebook);

	bonobo_window_set_contents (win, vbox);

	bonobo_ui_component_thaw (rb->priv->ui_component, NULL);

	rb_window_load_state (rb);

	/* load library */
	rb_library_release_brakes (rb->priv->library);

	/* GO GO GO! */
	gtk_widget_show (rb->priv->window);

	GDK_THREADS_ENTER ();

	while (gtk_events_pending ())
		gtk_main_iteration ();

	GDK_THREADS_LEAVE ();
}

static gboolean
rb_window_state_cb (GtkWidget *widget,
		    GdkEvent *event,
		    RB *rb)
{
	g_return_val_if_fail (widget != NULL, FALSE);

	switch (event->type)
	{
	case GDK_WINDOW_STATE:
		rb->priv->state->maximized = event->window_state.new_window_state &
			GDK_WINDOW_STATE_MAXIMIZED;
		break;
	case GDK_CONFIGURE:
		if (rb->priv->state->maximized == FALSE)
		{
			rb->priv->state->width = event->configure.width;
			rb->priv->state->height = event->configure.height;
		}
		break;
	default:
		break;
	}

	rb_window_save_state (rb);

	return FALSE;
}

static void
rb_window_load_state (RB *rb)
{
	/* Restore window state. */
	rb->priv->state->width = eel_gconf_get_integer (CONF_STATE_WINDOW_WIDTH);
	rb->priv->state->height = eel_gconf_get_integer (CONF_STATE_WINDOW_HEIGHT);
	rb->priv->state->maximized = eel_gconf_get_boolean (CONF_STATE_WINDOW_MAXIMIZED);

	gtk_window_set_default_size (GTK_WINDOW (rb->priv->window),
				     rb->priv->state->width,
				     rb->priv->state->height);

	if (rb->priv->state->maximized == TRUE)
		gtk_window_maximize (GTK_WINDOW (rb->priv->window));
}

static void
rb_window_save_state (RB *rb)
{
	/* Save the window state. */
	eel_gconf_set_integer (CONF_STATE_WINDOW_WIDTH,
			       rb->priv->state->width);
	eel_gconf_set_integer (CONF_STATE_WINDOW_HEIGHT,
			       rb->priv->state->height);
	eel_gconf_set_boolean (CONF_STATE_WINDOW_MAXIMIZED,
			       rb->priv->state->maximized);
}

static gboolean
rb_window_delete_cb (GtkWidget *win,
		     GdkEventAny *event,
		     RB *rb)
{
	rb_quit (rb);

	return TRUE;
};

static void
rb_cmd_about (BonoboUIComponent *component,
	      RB *rb,
	      const char *verbname)
{
	static GtkWidget *about = NULL;
	GdkPixbuf *pixbuf = NULL;

	const char *authors[] =
	{
		"Lead developers:",
		"Jorn Baayen (jorn@nl.linux.org)",
		"Olivier Martin (oleevye@wanadoo.fr)",
		"",
		"Contributors:",
		"Kenneth Christiansen (kenneth@gnu.org)",
		"Mark Finlay (sisob@eircom.net)",
		"Marco Pesenti Gritti (marco@it.gnome.org)",
		"Mark Humphreys (marquee@users.sourceforge.net)",
		"Laurens Krol (laurens.krol@planet.nl)",
		"Xan Lopez (xan@dimensis.com)",
		"Seth Nickell (snickell@stanford.edu)",
		"Bastien Nocera (hadess@hadess.net)",
		"Jan Arne Petersen (jpetersen@gnome-de.org)",
		"Kristian Rietveld (kris@gtk.org)",
		"Christian Schaller (uraeus@linuxrising.org)",
		"Dennis Smit (synap@yourbase.nl)",
		"Colin Walters (walters@gnu.org)",
		"James Willcox (jwillcox@gnome.org)",
		NULL
	};

	const char *documenters[] =
	{
		"Luca Ferretti (elle.uca@libero.it)",
		"Mark Finlay (sisob@eircom.net)",
		"Mark Humphreys (marquee@users.sourceforge.net)",
		NULL
	};

	const char *translator_credits = _("translator_credits");

	if (about != NULL)
	{
		gtk_window_present (GTK_WINDOW (about));
		return;
	}

	pixbuf = gdk_pixbuf_new_from_file (rb_file ("about-logo.png"), NULL);

	about = gnome_about_new ("Rhythmbox", VERSION,
				 _("Copyright © 2002 Jorn Baayen. All rights reserved."),
				 _("Music management and playback software for GNOME."),
				 (const char **) authors,
				 (const char **) documenters,
				 strcmp (translator_credits, "translator_credits") != 0 ? translator_credits : NULL,
				 pixbuf);
	gtk_window_set_transient_for (GTK_WINDOW (about), GTK_WINDOW (rb->priv->window));

	g_object_add_weak_pointer (G_OBJECT (about),
				   (void **) &about);

	gtk_widget_show (about);
}

static void
rb_cmd_close (BonoboUIComponent *component,
	      RB *rb,
	      const char *verbname)
{
	rb_quit (rb);
}

static void
rb_cmd_contents (BonoboUIComponent *component,
		 RB *rb,
		 const char *verbname)
{
	GError *error = NULL;

	gnome_help_display ("rhythmbox.xml", NULL, &error);

	if (error != NULL)
	{
		g_warning (error->message);

		g_error_free (error);
	}
}

static void
rb_quit (RB *rb)
{
	rb_debug ("Quitting");

	rb_window_save_state (rb);

	bonobo_object_unref (BONOBO_OBJECT (rb));
}

RBLibrary *
rb_get_library (RB *rb)
{
	return rb->priv->library;
}

RBPlayer *
rb_get_player (RB *rb)
{
	return rb->priv->player;
}

void
rb_set_title (RB *rb,
	      const char *title)
{
	if (title != NULL)
		gtk_window_set_title (GTK_WINDOW (rb->priv->window), title);
	else
		gtk_window_set_title (GTK_WINDOW (rb->priv->window), _("Music Player"));
}
