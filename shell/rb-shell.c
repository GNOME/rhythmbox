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
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-init.h>
#include <libgnomeui/gnome-window-icon.h>
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
/* FIXME */
#include "testview.h"
#include "testview2.h"
#include "rb-library.h"
/* FIXME */

static void rb_shell_class_init (RBShellClass *klass);
static void rb_shell_init (RBShell *shell);
static void rb_shell_finalize (GObject *object);
static void rb_shell_quit (PortableServer_Servant _servant,
                           CORBA_Environment *ev);
static gboolean rb_shell_window_delete_cb (GtkWidget *win,
			                   GdkEventAny *event,
			                   RBShell *shell);
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

	epv->quit = rb_shell_quit;
}

static void
rb_shell_init (RBShell *shell) 
{
	char *dirname;
	
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

	gnome_window_icon_set_default_from_file (PIXMAP_DIR "/rhythmbox.png");
}

static void
rb_shell_finalize (GObject *object)
{
        RBShell *shell = RB_SHELL (object);

	bonobo_activation_active_server_unregister (RB_SHELL_OAFIID, bonobo_object_corba_objref (BONOBO_OBJECT (shell)));

	rb_debug ("Unregistered with Bonobo Activation");

	gtk_widget_hide (shell->priv->window);

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
rb_shell_quit (PortableServer_Servant _servant,
               CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));

	rb_debug ("Quitting");

	bonobo_object_unref (BONOBO_OBJECT (shell));
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
	RBView *testview;
	Library *library;/* FIXME */

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

	/* initialize shell services */
	shell->priv->player_shell = rb_shell_player_new (shell->priv->ui_component);
	g_signal_connect (G_OBJECT (shell->priv->player_shell),
			  "window_title_changed",
			  G_CALLBACK (rb_shell_player_window_title_changed_cb),
			  shell);
	shell->priv->status_shell = rb_shell_status_new ();
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

	library = library_new ();
	library_release_brakes (library);

	/* FIXME */
	testview = rb_test_view_new (shell->priv->ui_component,
				     library);
	rb_shell_append_view (shell, testview);
	rb_shell_select_view (shell, testview);
	testview = rb_test_view2_new (shell->priv->ui_component);
	rb_shell_append_view (shell, testview);
	rb_shell_select_view (shell, testview);
	testview = rb_test_view2_new (shell->priv->ui_component);
	rb_shell_append_view (shell, testview);
	rb_shell_select_view (shell, testview);
	/* FIXME */

	/* now that we restored all views we can restore the layout */
	rb_sidebar_load_layout (RB_SIDEBAR (shell->priv->sidebar),
				shell->priv->sidebar_layout_file);

	bonobo_ui_component_thaw (shell->priv->ui_component, NULL);

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
rb_shell_window_delete_cb (GtkWidget *win,
			   GdkEventAny *event,
			   RBShell *shell)
{
	bonobo_object_unref (BONOBO_OBJECT (shell));

	return TRUE;
};

static void
rb_shell_append_view (RBShell *shell,
		      RBView *view)
{
	RBSidebarButton *button;
	
	shell->priv->views = g_list_append (shell->priv->views, view);

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

	/* merge menus */
	rb_view_merge_menus (view);

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
