/*
 *  Copyright (C) 2002 Jorn Baayen
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
#include <bonobo-activation/bonobo-activation-register.h>
#include <gtk/gtk.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-init.h>
#include <libgnome/gnome-program.h>
#include <libgnomeui/gnome-window-icon.h>
#include <libgnomeui/gnome-about.h>
#include <string.h>
#include <sys/stat.h>

#include "RhythmboxShell.h"
#include "rb-shell.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-stock-icons.h"
#include "rb-sidebar.h"
#include "rb-file-helpers.h"
#include "rb-view.h"
#include "rb-shell-player.h"
#include "rb-shell-status.h"
#include "rb-shell-clipboard.h"
#include "rb-bonobo-helpers.h"
#include "rb-library.h"
#include "rb-library-view.h"
#include "rb-shell-preferences.h"
#include "eel-gconf-extensions.h"
/* FIXME */
#include "testview2.h"
/* FIXME */

static void rb_shell_class_init (RBShellClass *klass);
static void rb_shell_init (RBShell *shell);
static void rb_shell_finalize (GObject *object);
static void rb_shell_corba_quit (PortableServer_Servant _servant,
                                 CORBA_Environment *ev);
static gboolean rb_shell_window_state_cb (GtkWidget *widget,
					  GdkEvent *event,
					  RBShell *shell);
static gboolean rb_shell_window_delete_cb (GtkWidget *win,
			                   GdkEventAny *event,
			                   RBShell *shell);
static void rb_shell_window_load_state (RBShell *shell);
static void rb_shell_window_save_state (RBShell *shell);
static void rb_shell_views_foreach_cb (RBView *view, RBShell *shell);
static void rb_shell_select_view (RBShell *shell, RBView *view);
static void rb_shell_append_view (RBShell *shell, RBView *view);
static void rb_shell_remove_view (RBShell *shell, RBView *view);
static void rb_shell_sidebar_button_toggled_cb (GtkToggleButton *widget,
				                RBShell *shell);
static void rb_shell_sidebar_button_deleted_cb (GtkWidget *widget,
				                RBShell *shell);
static void rb_shell_set_window_title (RBShell *shell,
			               const char *window_title);
static void rb_shell_player_window_title_changed_cb (RBShellPlayer *player,
					             const char *window_title,
					             RBShell *shell);
static void rb_shell_cmd_about (BonoboUIComponent *component,
		                RBShell *shell,
		                const char *verbname);
static void rb_shell_cmd_quit (BonoboUIComponent *component,
		               RBShell *shell,
			       const char *verbname);
static void rb_shell_cmd_music_folders (BonoboUIComponent *component,
		                        RBShell *shell,
		                        const char *verbname);
static void rb_shell_quit (RBShell *shell);
static void rb_shell_repeat_changed_cb (BonoboUIComponent *component,
			                const char *path,
			                Bonobo_UIComponent_EventType type,
			                const char *state,
			                RBShell *shell);
static void rb_shell_shuffle_changed_cb (BonoboUIComponent *component,
			                 const char *path,
			                 Bonobo_UIComponent_EventType type,
			                 const char *state,
			                 RBShell *shell);

#define CMD_PATH_SHUFFLE "/commands/Shuffle"
#define CMD_PATH_REPEAT  "/commands/Repeat"

/* prefs */
#define CONF_STATE_WINDOW_WIDTH     "/apps/rhythmbox/state/window_width"
#define CONF_STATE_WINDOW_HEIGHT    "/apps/rhythmbox/state/window_height"
#define CONF_STATE_WINDOW_MAXIMIZED "/apps/rhythmbox/state/window_maximized"
#define CONF_STATE_SHUFFLE          "/apps/rhythmbox/state/shuffle"
#define CONF_STATE_REPEAT           "/apps/rhythmbox/state/repeat"

typedef struct
{
	int width;
	int height;
	gboolean maximized;
} RBShellWindowState;

struct RBShellPrivate
{
	GtkWidget *window;

	BonoboUIComponent *ui_component;

	GtkWidget *sidebar;
	GtkWidget *notebook;

	GList *views;

	char *sidebar_layout_file;

	RBShellPlayer *player_shell;
	RBShellStatus *status_shell;
	RBShellClipboard *clipboard_shell;

	Library *library;

	RBView *selected_view;

	RBShellWindowState *state;

	GtkWidget *prefs;

	gboolean shuffle;
	gboolean repeat;
};

static BonoboUIVerb rb_shell_verbs[] =
{
	BONOBO_UI_VERB ("About",        (BonoboUIVerbFn) rb_shell_cmd_about),
	BONOBO_UI_VERB ("Quit",         (BonoboUIVerbFn) rb_shell_cmd_quit),
	BONOBO_UI_VERB ("MusicFolders", (BonoboUIVerbFn) rb_shell_cmd_music_folders),
	BONOBO_UI_VERB_END
};

static RBBonoboUIListener rb_shell_listeners[] =
{
	RB_BONOBO_UI_LISTENER ("Shuffle", (BonoboUIListenerFn) rb_shell_shuffle_changed_cb),
	RB_BONOBO_UI_LISTENER ("Repeat",  (BonoboUIListenerFn) rb_shell_repeat_changed_cb),
	RB_BONOBO_UI_LISTENER_END
};

static GObjectClass *parent_class;

GType
rb_shell_get_type (void)
{
	static GType type = 0;
                                                                              
	if (type == 0)
	{ 
		static GTypeInfo info =
		{
			sizeof (RBShellClass),
			NULL, 
			NULL,
			(GClassInitFunc) rb_shell_class_init, 
			NULL,
			NULL, 
			sizeof (RBShell),
			0,
			(GInstanceInitFunc) rb_shell_init
		};
		
		type = bonobo_type_unique (BONOBO_TYPE_OBJECT,
					   POA_GNOME_RhythmboxShell__init,
					   POA_GNOME_RhythmboxShell__fini,
					   G_STRUCT_OFFSET (RBShellClass, epv),
					   &info,
					   "RBShell");
	}

	return type;
}

static void
rb_shell_class_init (RBShellClass *klass)
{
        GObjectClass *object_class = (GObjectClass *) klass;
        POA_GNOME_RhythmboxShell__epv *epv = &klass->epv;

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize = rb_shell_finalize;

	epv->quit = rb_shell_corba_quit;
}

static void
rb_shell_init (RBShell *shell) 
{
	char *dirname, *file;
	
	shell->priv = g_new0 (RBShellPrivate, 1);

	dirname = g_build_filename (g_get_home_dir (),
				    GNOME_DOT_GNOME,
				    "rhythmbox",
				    NULL);

	if (g_file_test (dirname, G_FILE_TEST_IS_DIR) == FALSE)
	{
		if (g_file_test (dirname, G_FILE_TEST_EXISTS) == TRUE)
		{
			rb_error_dialog (_("%s exists, please move it out of the way."), dirname);
		}
		
		if (mkdir (dirname, 488) != 0)
		{
			rb_error_dialog (_("Failed to create directory %s."), dirname);
		}
	}

	shell->priv->sidebar_layout_file = g_build_filename (dirname,
							     "sidebar_layout.xml",
							     NULL);

	g_free (dirname);

	file = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_PIXMAP, "rhythmbox.png", TRUE, NULL);
	gnome_window_icon_set_default_from_file (file);
	g_free (file);
	
	shell->priv->state = g_new0 (RBShellWindowState, 1);

	eel_gconf_monitor_add ("/apps/rhythmbox");
}

static void
rb_shell_finalize (GObject *object)
{
        RBShell *shell = RB_SHELL (object);

	gtk_widget_hide (shell->priv->window);

	eel_gconf_monitor_remove ("/apps/rhythmbox");

	bonobo_activation_active_server_unregister (RB_SHELL_OAFIID, bonobo_object_corba_objref (BONOBO_OBJECT (shell)));

	rb_debug ("Unregistered with Bonobo Activation");

	rb_sidebar_save_layout (RB_SIDEBAR (shell->priv->sidebar),
				shell->priv->sidebar_layout_file);

	g_list_foreach (shell->priv->views,
			(GFunc) rb_shell_views_foreach_cb,
			shell);

	gtk_widget_destroy (shell->priv->window);
	
	g_list_free (shell->priv->views);

	g_free (shell->priv->sidebar_layout_file);

	g_object_unref (G_OBJECT (shell->priv->player_shell));
	g_object_unref (G_OBJECT (shell->priv->clipboard_shell));
	g_object_unref (G_OBJECT (shell->priv->library));

	g_free (shell->priv->state);

	if (shell->priv->prefs != NULL)
		gtk_widget_destroy (shell->priv->prefs);

	g_free (shell->priv);

        parent_class->finalize (G_OBJECT (shell));
}

RBShell *
rb_shell_new (void)
{
	RBShell *s;

	s = g_object_new (RB_TYPE_SHELL, NULL);

	return s;
}

static void
rb_shell_corba_quit (PortableServer_Servant _servant,
                     CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));

	rb_shell_quit (shell);
}

void
rb_shell_construct (RBShell *shell)
{
	CORBA_Object corba_object;
	CORBA_Environment ev;
	BonoboWindow *win;
	BonoboUIContainer *container;
	Bonobo_UIContainer corba_container;
	GtkWidget *hbox, *vbox;
	RBView *testview, *library_view;

	g_return_if_fail (RB_IS_SHELL (shell));

	rb_debug ("Constructing shell");

	/* register with CORBA */
	CORBA_exception_init (&ev);
	
	corba_object = bonobo_object_corba_objref (BONOBO_OBJECT (shell));
	if (bonobo_activation_active_server_register (RB_SHELL_OAFIID, corba_object) != Bonobo_ACTIVATION_REG_SUCCESS)
	{
		/* this is not critical, but worth a warning nevertheless */
		char *msg = rb_shell_corba_exception_to_string (&ev);
		rb_warning_dialog (_("Failed to register the shell:\n%s"), msg);
		g_free (msg);
	}

	CORBA_exception_free (&ev);

	rb_debug ("Registered with Bonobo Activation");

	/* initialize UI */
	win = BONOBO_WINDOW (bonobo_window_new ("Rhythmbox shell",
						"Rhythmbox"));

	shell->priv->window = GTK_WIDGET (win);

	g_signal_connect (G_OBJECT (win), "window-state-event",
			  G_CALLBACK (rb_shell_window_state_cb),
			  shell);
	g_signal_connect (G_OBJECT (win), "configure-event",
			  G_CALLBACK (rb_shell_window_state_cb),
			  shell);
	g_signal_connect (G_OBJECT (win), "delete_event",
			  G_CALLBACK (rb_shell_window_delete_cb),
			  shell);

	container = bonobo_window_get_ui_container (win);

	bonobo_ui_engine_config_set_path (bonobo_window_get_ui_engine (win),
					  "/apps/rhythmbox/UIConfig/kvps");

	corba_container = BONOBO_OBJREF (container);

	shell->priv->ui_component = bonobo_ui_component_new_default ();

	bonobo_ui_component_set_container (shell->priv->ui_component,
					   corba_container,
					   NULL);

	bonobo_ui_component_freeze (shell->priv->ui_component, NULL);
	
	bonobo_ui_util_set_ui (shell->priv->ui_component,
			       DATADIR,
			       "rhythmbox-ui.xml",
			       "rhythmbox", NULL);

	bonobo_ui_component_add_verb_list_with_data (shell->priv->ui_component,
						     rb_shell_verbs,
						     shell);
	rb_bonobo_add_listener_list_with_data (shell->priv->ui_component,
					       rb_shell_listeners,
					       shell);

	/* initialize shell services */
	shell->priv->player_shell = rb_shell_player_new (shell->priv->ui_component);
	g_signal_connect (G_OBJECT (shell->priv->player_shell),
			  "window_title_changed",
			  G_CALLBACK (rb_shell_player_window_title_changed_cb),
			  shell);
	shell->priv->status_shell = rb_shell_status_new (bonobo_window_get_ui_engine (win));
	shell->priv->clipboard_shell = rb_shell_clipboard_new (shell->priv->ui_component);

	hbox = gtk_hbox_new (FALSE, 5);
	shell->priv->sidebar = rb_sidebar_new ();
	gtk_widget_set_size_request (shell->priv->sidebar, 80, -1);
	shell->priv->notebook = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (shell->priv->notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (shell->priv->notebook), FALSE);
	gtk_box_pack_start (GTK_BOX (hbox), shell->priv->sidebar, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), shell->priv->notebook, TRUE, TRUE, 0);

	vbox = gtk_vbox_new (FALSE, 5);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (shell->priv->status_shell), FALSE, TRUE, 0);

	bonobo_window_set_contents (win, vbox);

	gtk_widget_show_all (vbox);

	/* initialize views */

	shell->priv->library = library_new ();
	library_release_brakes (shell->priv->library);

	/* FIXME */
	library_view = rb_library_view_new (container,
				            shell->priv->library);
	rb_shell_append_view (shell, library_view);
	rb_shell_select_view (shell, library_view);
	testview = rb_test_view2_new (container);
	rb_shell_append_view (shell, testview);
	rb_shell_select_view (shell, testview);
	testview = rb_test_view2_new (container);
	rb_shell_append_view (shell, testview);
	rb_shell_select_view (shell, testview);
	/* FIXME */

	/* now that we restored all views we can restore the layout */
	rb_sidebar_load_layout (RB_SIDEBAR (shell->priv->sidebar),
				shell->priv->sidebar_layout_file);

	bonobo_ui_component_thaw (shell->priv->ui_component, NULL);

	rb_shell_window_load_state (shell);

	/* restore shuffle/repeat */
	rb_bonobo_set_active (shell->priv->ui_component,
			      CMD_PATH_SHUFFLE,
			      eel_gconf_get_boolean (CONF_STATE_SHUFFLE));
	rb_bonobo_set_active (shell->priv->ui_component,
			      CMD_PATH_REPEAT,
			      eel_gconf_get_boolean (CONF_STATE_REPEAT));

	gtk_widget_show (shell->priv->window);
}

char *
rb_shell_corba_exception_to_string (CORBA_Environment *ev)
{
	g_return_val_if_fail (ev != NULL, NULL);

	if ((CORBA_exception_id (ev) != NULL) &&
	    (strcmp (CORBA_exception_id (ev), ex_Bonobo_GeneralError) != 0))
	{
		return bonobo_exception_get_text (ev); 
	}
	else
	{
		const Bonobo_GeneralError *bonobo_general_error;

		bonobo_general_error = CORBA_exception_value (ev);
		if (bonobo_general_error != NULL) 
		{
			return g_strdup (bonobo_general_error->description);
		}
	}

	return NULL;
}

static gboolean
rb_shell_window_state_cb (GtkWidget *widget,
			  GdkEvent *event,
			  RBShell *shell)
{
	g_return_val_if_fail (widget != NULL, FALSE);

	switch (event->type)
	{
	case GDK_WINDOW_STATE:
		shell->priv->state->maximized = event->window_state.new_window_state &
			GDK_WINDOW_STATE_MAXIMIZED;
		break;
	case GDK_CONFIGURE:
		if (shell->priv->state->maximized == FALSE)
		{
			shell->priv->state->width = event->configure.width;
			shell->priv->state->height = event->configure.height;
		}
		break;
	default:
		break;
	}

	rb_shell_window_save_state (shell);

	return FALSE;
}

static void
rb_shell_window_load_state (RBShell *shell)
{
	/* Restore window state. */
	shell->priv->state->width = eel_gconf_get_integer (CONF_STATE_WINDOW_WIDTH); 
	shell->priv->state->height = eel_gconf_get_integer (CONF_STATE_WINDOW_HEIGHT);
	shell->priv->state->maximized = eel_gconf_get_boolean (CONF_STATE_WINDOW_MAXIMIZED);

	gtk_window_set_default_size (GTK_WINDOW (shell->priv->window),
				     shell->priv->state->width,
				     shell->priv->state->height);

	if (shell->priv->state->maximized == TRUE)
		gtk_window_maximize (GTK_WINDOW (shell->priv->window));
}

static void
rb_shell_window_save_state (RBShell *shell)
{
	/* Save the window state. */
	eel_gconf_set_integer (CONF_STATE_WINDOW_WIDTH,
			       shell->priv->state->width);
	eel_gconf_set_integer (CONF_STATE_WINDOW_HEIGHT,
			       shell->priv->state->height);
	eel_gconf_set_boolean (CONF_STATE_WINDOW_MAXIMIZED,
			       shell->priv->state->maximized);
}

static gboolean
rb_shell_window_delete_cb (GtkWidget *win,
			   GdkEventAny *event,
			   RBShell *shell)
{
	rb_shell_quit (shell);

	return TRUE;
};

static void
rb_shell_append_view (RBShell *shell,
		      RBView *view)
{
	RBSidebarButton *button;
	
	shell->priv->views = g_list_append (shell->priv->views, view);

	rb_view_player_set_shuffle (RB_VIEW_PLAYER (view), shell->priv->shuffle);
	rb_view_player_set_repeat (RB_VIEW_PLAYER (view), shell->priv->repeat);

	gtk_notebook_append_page (GTK_NOTEBOOK (shell->priv->notebook),
				  GTK_WIDGET (view),
				  gtk_label_new (""));
	gtk_widget_show_all (GTK_WIDGET (view));

	button = rb_view_get_sidebar_button (view);
	rb_sidebar_append (RB_SIDEBAR (shell->priv->sidebar),
			   button);

	g_signal_connect (G_OBJECT (button),
			  "toggled",
			  G_CALLBACK (rb_shell_sidebar_button_toggled_cb),
			  shell);
	g_signal_connect (G_OBJECT (button),
			  "deleted",
			  G_CALLBACK (rb_shell_sidebar_button_deleted_cb),
			  shell);
}

static void
rb_shell_remove_view (RBShell *shell,
		      RBView *view)
{
	shell->priv->views = g_list_remove (shell->priv->views, view);

	rb_sidebar_remove (RB_SIDEBAR (shell->priv->sidebar),
			   rb_view_get_sidebar_button (view));

	gtk_notebook_remove_page (GTK_NOTEBOOK (shell->priv->notebook),
				  gtk_notebook_page_num (GTK_NOTEBOOK (shell->priv->notebook), GTK_WIDGET (view)));
}

static void
rb_shell_select_view (RBShell *shell,
		      RBView *view)
{
	RBSidebarButton *button;

	/* remove old menus */
	if (shell->priv->selected_view != NULL)
		rb_view_unmerge_ui (shell->priv->selected_view);
	shell->priv->selected_view = view;

	/* merge menus */
	rb_view_merge_ui (view);

	/* show view */
	gtk_notebook_set_current_page (GTK_NOTEBOOK (shell->priv->notebook),
				       gtk_notebook_page_num (GTK_NOTEBOOK (shell->priv->notebook), GTK_WIDGET (view)));

	/* ensure sidebar button is selected */
	button = rb_view_get_sidebar_button (view);
	if (GTK_TOGGLE_BUTTON (button)->active == FALSE)
	{
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
	}

	/* update services */
	rb_shell_player_set_player (shell->priv->player_shell,
				    RB_VIEW_PLAYER (view));
	rb_shell_status_set_status (shell->priv->status_shell,
				    RB_VIEW_STATUS (view));
	rb_shell_clipboard_set_clipboard (shell->priv->clipboard_shell,
				          RB_VIEW_CLIPBOARD (view));
}

static void
rb_shell_views_foreach_cb (RBView *view, RBShell *shell)
{
	rb_shell_remove_view (shell, view);
}

static void
rb_shell_sidebar_button_toggled_cb (GtkToggleButton *widget,
				    RBShell *shell)
{
	if (widget->active == TRUE)
	{
		rb_shell_select_view (shell,
				      g_object_get_data (G_OBJECT (widget), "view"));
	}
}

static void
rb_shell_sidebar_button_deleted_cb (GtkWidget *widget,
				    RBShell *shell)
{
	rb_shell_remove_view (shell,
			      g_object_get_data (G_OBJECT (widget), "view"));
}

static void
rb_shell_player_window_title_changed_cb (RBShellPlayer *player,
					 const char *window_title,
					 RBShell *shell)
{
	rb_shell_set_window_title (shell, window_title);
}

static void
rb_shell_set_window_title (RBShell *shell,
			   const char *window_title)
{
	if (window_title == NULL)
	{
		gtk_window_set_title (GTK_WINDOW (shell->priv->window),
				      "Rhythmbox");
	}
	else
	{
		gtk_window_set_title (GTK_WINDOW (shell->priv->window),
				      window_title);
	}
}

static void
rb_shell_shuffle_changed_cb (BonoboUIComponent *component,
			     const char *path,
			     Bonobo_UIComponent_EventType type,
			     const char *state,
			     RBShell *shell)
{
	gboolean shuffle = rb_bonobo_get_active (component, CMD_PATH_SHUFFLE);
	GList *l;

	for (l = shell->priv->views; l != NULL; l = g_list_next (l))
	{
		RBViewPlayer *player = RB_VIEW_PLAYER (l->data);

		rb_view_player_set_shuffle (player, shuffle);
	}
	
	shell->priv->shuffle = shuffle;
	eel_gconf_set_boolean (CONF_STATE_SHUFFLE, shuffle);
}

static void
rb_shell_repeat_changed_cb (BonoboUIComponent *component,
			    const char *path,
			    Bonobo_UIComponent_EventType type,
			    const char *state,
			    RBShell *shell)
{
	gboolean repeat = rb_bonobo_get_active (component, CMD_PATH_REPEAT);
	GList *l;

	for (l = shell->priv->views; l != NULL; l = g_list_next (l))
	{
		RBViewPlayer *player = RB_VIEW_PLAYER (l->data);

		rb_view_player_set_repeat (player, repeat);
	}

	shell->priv->repeat = repeat;
	eel_gconf_set_boolean (CONF_STATE_REPEAT, repeat);
}

static void
rb_shell_cmd_about (BonoboUIComponent *component,
		    RBShell *shell,
		    const char *verbname)
{
	static GtkWidget *about = NULL;
	GdkPixbuf *pixbuf = NULL;
	char *file;

	const char *authors[] =
	{
		"Jorn Baayen <jorn@nl.linux.org>",
		"Marco Pesenti Gritti <mpeseng@tin.it>",
		"Bastien Nocera <hadess@hadess.net>",
		"Seth Nickell <snickell@stanford.edu>",
		"Olivier Martin <omartin@ifrance.com>",
		NULL
	};

	const char *documenters[] =
	{
		NULL
	};

	const char *translator_credits = _("translator_credits");

	if (about != NULL)
	{
		gtk_window_present (GTK_WINDOW (about));
		return;
	}

	file = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_PIXMAP, "rhythmbox.png", TRUE, NULL);
	pixbuf = gdk_pixbuf_new_from_file (file, NULL);
	g_free (file);

	about = gnome_about_new ("Rhythmbox", VERSION,
				 _("Copyright 2002 Jorn Baayen"),
				 _("Music management and playback software for GNOME."),
				 (const char **) authors,
				 (const char **) documenters,
				 strcmp (translator_credits, "translator_credits") != 0 ? translator_credits : NULL,
				 pixbuf);
	gtk_window_set_transient_for (GTK_WINDOW (about), GTK_WINDOW (shell->priv->window));

	g_object_add_weak_pointer (G_OBJECT (about),
				   (void **) &about);

	gtk_widget_show (about);
}

static void
rb_shell_cmd_quit (BonoboUIComponent *component,
		   RBShell *shell,
		   const char *verbname)
{
	rb_shell_quit (shell);
}

static void
rb_shell_cmd_music_folders (BonoboUIComponent *component,
		            RBShell *shell,
		            const char *verbname)
{
	if (shell->priv->prefs == NULL)
	{
		shell->priv->prefs = rb_shell_preferences_new ();

		gtk_window_set_transient_for (GTK_WINDOW (shell->priv->prefs),
					      GTK_WINDOW (shell->priv->window));
	}

	gtk_widget_show_all (shell->priv->prefs);
}

static void
rb_shell_quit (RBShell *shell)
{
	rb_debug ("Quitting");

	rb_shell_window_save_state (shell);

	bonobo_object_unref (BONOBO_OBJECT (shell));
}
