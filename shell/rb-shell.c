/*
 *  Copyright (C) 2002, 2003 Jorn Baayen
 *  Copyright (C) 2003 Colin Walters <cwalters@gnome.org>
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
#include <gtk/gtk.h>
#include <config.h>
#include <libgnome/libgnome.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-init.h>
#include <libgnome/gnome-program.h>
#include <libgnomeui/gnome-window-icon.h>
#include <libgnomeui/gnome-about.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/stat.h>

#include "Rhythmbox.h"
#include "rb-shell.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-stock-icons.h"
#include "rb-sourcelist.h"
#include "rb-string-helpers.h"
#include "rb-file-helpers.h"
#include "rb-source.h"
#include "rb-preferences.h"
#include "rb-shell-player.h"
#include "rb-source-header.h"
#include "rb-statusbar.h"
#include "rb-shell-preferences.h"
#include "rb-playlist.h"
#include "rb-bonobo-helpers.h"
#include "rb-library.h"
#include "rb-library-source.h"
#include "rb-iradio-backend.h"
#include "rb-new-station-dialog.h"
#include "rb-iradio-source.h"
#include "rb-audiocd-source.h"
#include "rb-shell-preferences.h"
// #include "rb-group-source.h"
#include "rb-file-monitor.h"
#include "rb-windows-ini-file.h"
#include "rb-library-dnd-types.h"
#include "rb-volume.h"
#include "rb-thread-helpers.h"
#include "eel-gconf-extensions.h"
#include "eggtrayicon.h"

static void rb_shell_class_init (RBShellClass *klass);
static void rb_shell_init (RBShell *shell);
static void rb_shell_finalize (GObject *object);
static void rb_shell_corba_quit (PortableServer_Servant _servant,
                                 CORBA_Environment *ev);
static void rb_shell_corba_handle_file (PortableServer_Servant _servant,
					const CORBA_char *uri,
					CORBA_Environment *ev);
static void rb_shell_corba_add_to_library (PortableServer_Servant _servant,
					   const CORBA_char *uri,
					   CORBA_Environment *ev);
static void rb_shell_corba_grab_focus (PortableServer_Servant _servant,
				       CORBA_Environment *ev);
void rb_shell_handle_playlist_entry (RBShell *shell, GList *locations, const char *title);
static gboolean rb_shell_window_state_cb (GtkWidget *widget,
					  GdkEvent *event,
					  RBShell *shell);
static gboolean rb_shell_window_delete_cb (GtkWidget *win,
			                   GdkEventAny *event,
			                   RBShell *shell);
static void rb_shell_window_load_state (RBShell *shell);
static void rb_shell_window_save_state (RBShell *shell);
static void rb_shell_select_source (RBShell *shell, RBSource *source);
static void rb_shell_append_source (RBShell *shell, RBSource *source);
static void source_selected_cb (RBSourceList *sourcelist,
				RBSource *source,
				RBShell *shell);

/* static void rb_shell_remove_source (RBShell *shell, RBSource *source); */
/* static void rb_shell_source_deleted_cb (RBSource *source, */
/* 					RBShell *shell); */
static void rb_shell_set_window_title (RBShell *shell,
			               const char *window_title);
static void rb_shell_player_window_title_changed_cb (RBShellPlayer *player,
					             const char *window_title,
					             RBShell *shell);
static void rb_shell_cmd_about (BonoboUIComponent *component,
		                RBShell *shell,
		                const char *verbname);
static void rb_shell_cmd_contents (BonoboUIComponent *component,
		                RBShell *shell,
		                const char *verbname);
static void rb_shell_cmd_close (BonoboUIComponent *component,
		                RBShell *shell,
			        const char *verbname);
static void rb_shell_cmd_preferences (BonoboUIComponent *component,
		                      RBShell *shell,
		                      const char *verbname);
static void rb_shell_cmd_add_to_library (BonoboUIComponent *component,
			                 RBShell *shell,
			                 const char *verbname);
static void rb_shell_cmd_add_location (BonoboUIComponent *component,
				       RBShell *shell,
				       const char *verbname);
static void rb_shell_cmd_load_playlist (BonoboUIComponent *component,
					RBShell *shell,
					const char *verbname);
/* static void rb_shell_cmd_new_group (BonoboUIComponent *component, */
/* 			            RBShell *shell, */
/* 			            const char *verbname); */
/* static void rb_shell_cmd_new_station (BonoboUIComponent *component, */
/* 				      RBShell *shell, */
/* 				      const char *verbname); */
static void rb_shell_quit (RBShell *shell);
static void rb_shell_view_sourcelist_changed_cb (BonoboUIComponent *component,
						 const char *path,
						 Bonobo_UIComponent_EventType type,
						 const char *state,
						 RBShell *shell);
static void rb_shell_show_window_changed_cb (BonoboUIComponent *component,
				             const char *path,
				             Bonobo_UIComponent_EventType type,
				             const char *state,
				             RBShell *shell);
/* static void rb_shell_load_music_groups (RBShell *shell); */
/* static void rb_shell_save_music_groups (RBShell *shell); */
static void rb_shell_sync_sourcelist_visibility (RBShell *shell);
static void rb_shell_sync_window_visibility (RBShell *shell);
static gboolean rb_shell_update_source_status (RBShell *shell);
static void sourcelist_visibility_changed_cb (GConfClient *client,
					      guint cnxn_id,
					      GConfEntry *entry,
					      RBShell *shell);
static void window_visibility_changed_cb (GConfClient *client,
			                  guint cnxn_id,
			                  GConfEntry *entry,
			                  RBShell *shell);
#ifdef HAVE_AUDIOCD
/* static void audiocd_changed_cb (MonkeyMediaAudioCD *cd, */
/* 				gboolean available, */
/* 				gpointer data); */
#endif
/* REWRITEFIXME */
/* static void rb_sidebar_drag_finished_cb (RBSidebar *sidebar, */
/* 			                 GdkDragContext *context, */
/* 			                 int x, int y, */
/* 			                 GtkSelectionData *data, */
/* 			                 guint info, */
/* 			                 guint time, */
/* 			                 RBShell *shell); */
/* static void dnd_add_handled_cb (RBLibraryAction *action, */
/* 		                RBSource *source); */
static void setup_tray_icon (RBShell *shell);
static void sync_tray_menu (RBShell *shell);

static const GtkTargetEntry target_table[] =
	{
		{ RB_LIBRARY_DND_URI_LIST_TYPE, 0, RB_LIBRARY_DND_URI_LIST },
		{ RB_LIBRARY_DND_NODE_ID_TYPE,  0, RB_LIBRARY_DND_NODE_ID }
	};

static const GtkTargetEntry target_uri [] =
	{
		{ RB_LIBRARY_DND_URI_LIST_TYPE, 0, RB_LIBRARY_DND_URI_LIST }
	};

typedef enum
{
	CREATE_GROUP_WITH_URI_LIST,
	CREATE_GROUP_WITH_NODE_LIST,
	CREATE_GROUP_WITH_SELECTION
} CreateGroupType;

#define CMD_PATH_VIEW_SOURCELIST   "/commands/ShowSourceList"
#define CMD_PATH_SHOW_WINDOW    "/commands/ShowWindow"

/* prefs */
#define CONF_STATE_WINDOW_WIDTH     CONF_PREFIX "/state/window_width"
#define CONF_STATE_WINDOW_HEIGHT    CONF_PREFIX "/state/window_height"
#define CONF_STATE_WINDOW_MAXIMIZED CONF_PREFIX "/state/window_maximized"
#define CONF_STATE_WINDOW_HIDDEN    CONF_PREFIX "/state/window_hidden"
#define CONF_STATE_PANED_POSITION   CONF_PREFIX "/state/paned_position"
#define CONF_STATE_ADD_DIR          CONF_PREFIX "/state/add_dir"
#define CONF_MUSIC_GROUPS           CONF_PREFIX "/music_groups"

typedef struct
{
	int width;
	int height;
	gboolean maximized;
	int paned_position;
} RBShellWindowState;

struct RBShellPrivate
{
	GtkWidget *window;

	BonoboUIComponent *ui_component;
	BonoboUIContainer *container;

	GtkWidget *paned;
	GtkWidget *sourcelist;
	GtkWidget *notebook;

	GList *sources;

	RBShellPlayer *player_shell;
	RBSourceHeader *source_header;
	RBStatusbar *statusbar;

	RBLibrary *library;
	RBSource *library_source;

	RBIRadioBackend *iradio_backend;
 	RBIRadioSource *iradio_source;

	MonkeyMediaAudioCD *cd;

	RBSource *selected_source;

	RBShellWindowState *state;

	GtkWidget *prefs;

	GList *groups;

	EggTrayIcon *tray_icon;
	GtkTooltips *tray_icon_tooltip;
	BonoboControl *tray_icon_control;
	BonoboUIComponent *tray_icon_component;

	RBVolume *volume;
};

static BonoboUIVerb rb_shell_verbs[] =
{
	BONOBO_UI_VERB ("About",        (BonoboUIVerbFn) rb_shell_cmd_about),
	BONOBO_UI_VERB ("Contents",	(BonoboUIVerbFn) rb_shell_cmd_contents),
	BONOBO_UI_VERB ("Close",        (BonoboUIVerbFn) rb_shell_cmd_close),
	BONOBO_UI_VERB ("Preferences",  (BonoboUIVerbFn) rb_shell_cmd_preferences),
	BONOBO_UI_VERB ("AddToLibrary", (BonoboUIVerbFn) rb_shell_cmd_add_to_library),
	BONOBO_UI_VERB ("AddLocation",  (BonoboUIVerbFn) rb_shell_cmd_add_location),
	BONOBO_UI_VERB ("LoadPlaylist", (BonoboUIVerbFn) rb_shell_cmd_load_playlist),
/* 	BONOBO_UI_VERB ("NewGroup",     (BonoboUIVerbFn) rb_shell_cmd_new_group), */
/* 	BONOBO_UI_VERB ("NewStation",   (BonoboUIVerbFn) rb_shell_cmd_new_station), */
	BONOBO_UI_VERB_END
};

static RBBonoboUIListener rb_shell_listeners[] =
{
	RB_BONOBO_UI_LISTENER ("ShowSourceList",(BonoboUIListenerFn) rb_shell_view_sourcelist_changed_cb),
	RB_BONOBO_UI_LISTENER_END
};

static RBBonoboUIListener rb_tray_listeners[] =
{
	RB_BONOBO_UI_LISTENER ("ShowWindow", (BonoboUIListenerFn) rb_shell_show_window_changed_cb),
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
					   POA_GNOME_Rhythmbox__init,
					   POA_GNOME_Rhythmbox__fini,
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
        POA_GNOME_Rhythmbox__epv *epv = &klass->epv;

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize = rb_shell_finalize;

	epv->quit         = rb_shell_corba_quit;
	epv->handleFile   = rb_shell_corba_handle_file;
	epv->addToLibrary = rb_shell_corba_add_to_library;
	epv->grabFocus    = rb_shell_corba_grab_focus;
}

static void
rb_shell_init (RBShell *shell) 
{
	char *file;

	rb_thread_helpers_init ();
	
	shell->priv = g_new0 (RBShellPrivate, 1);

	rb_ensure_dir_exists (rb_dot_dir ());

	file = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_APP_PIXMAP, "rhythmbox.png", TRUE, NULL);
	gnome_window_icon_set_default_from_file (file);
	g_free (file);
	
	shell->priv->state = g_new0 (RBShellWindowState, 1);

	eel_gconf_monitor_add (CONF_PREFIX);

}

static void
rb_shell_finalize (GObject *object)
{
        RBShell *shell = RB_SHELL (object);

	gtk_widget_hide (shell->priv->window);
	gtk_widget_hide (GTK_WIDGET (shell->priv->tray_icon));
	rb_shell_player_stop (shell->priv->player_shell);

	/* FIXME? */
	while (gtk_events_pending ())
		gtk_main_iteration ();

	eel_gconf_monitor_remove (CONF_PREFIX);

	bonobo_activation_active_server_unregister (RB_SHELL_OAFIID, bonobo_object_corba_objref (BONOBO_OBJECT (shell)));

	rb_debug ("Unregistered with Bonobo Activation");
	
	/* REWRITEFIXME */
/* 	rb_shell_save_music_groups (shell); */

	gtk_widget_destroy (GTK_WIDGET (shell->priv->tray_icon));
	
	g_list_free (shell->priv->sources);

	g_list_free (shell->priv->groups);

	/* hack to make the gdk thread lock available for freeing
	 * the library.. evil */
	g_object_unref (G_OBJECT (shell->priv->library));

	g_object_unref (G_OBJECT (shell->priv->iradio_backend));

	g_free (shell->priv->state);

	if (shell->priv->prefs != NULL)
		gtk_widget_destroy (shell->priv->prefs);
	
	g_free (shell->priv);

	g_object_unref (G_OBJECT (rb_file_monitor_get ()));

        parent_class->finalize (G_OBJECT (shell));

	bonobo_main_quit ();
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

	GDK_THREADS_ENTER ();

	rb_shell_quit (shell);

	GDK_THREADS_LEAVE ();
}

static void
rb_shell_corba_handle_file (PortableServer_Servant _servant,
			    const CORBA_char *uri,
			    CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));

	GnomeVFSURI *vfsuri = gnome_vfs_uri_new (uri);
	if (!vfsuri)
	{
		rb_error_dialog (_("Unable to parse URI \"%s\"\n"), uri);
		return;
	}
	if (gnome_vfs_uri_is_local (vfsuri))
	{
		char *localpath = gnome_vfs_get_local_path_from_uri (uri);
 		char *mimetype = gnome_vfs_get_mime_type (localpath);
		if (mimetype && strncmp ("audio/x-scpls", mimetype, 13) == 0)
		{
			char *firsturi;
			firsturi = rb_playlist_load (shell->priv->library,
						     shell->priv->iradio_backend,
						     localpath);
			if (firsturi != NULL)
			{
/* 				rb_shell_player_play_search (shell->priv->player_shell, */
/* 							     firsturi); */
				g_free (firsturi);
			}
		}
		else
		{
			rb_library_add_uri (shell->priv->library, (char *) uri);
		}
	}
	else
	{
		const char *scheme = gnome_vfs_uri_get_scheme (vfsuri);
		if (strncmp ("http", scheme, 4) == 0)
		{
			rb_iradio_backend_add_station_from_uri (shell->priv->iradio_backend, uri);
		}
		else
		{
			rb_error_dialog (_("Unable to handle URI \"%s\"\n"), uri);
			return;
		}
	}
}

static void
rb_shell_corba_add_to_library (PortableServer_Servant _servant,
			       const CORBA_char *uri,
			       CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));

	rb_library_add_uri (shell->priv->library, (char *) uri);
}

static void
rb_shell_corba_grab_focus (PortableServer_Servant _servant,
			   CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));
	gboolean hidden;

	hidden = eel_gconf_get_boolean (CONF_STATE_WINDOW_HIDDEN);
	if (!hidden)
	{
		gtk_window_present (GTK_WINDOW (shell->priv->window));
		gtk_widget_grab_focus (shell->priv->window);
	}
	else
		eel_gconf_set_boolean (CONF_STATE_WINDOW_HIDDEN, TRUE);
}

void
rb_shell_construct (RBShell *shell)
{
	CORBA_Object corba_object;
	CORBA_Environment ev;
	BonoboWindow *win;
	Bonobo_UIContainer corba_container;
	GtkWidget *vbox;
	
	g_return_if_fail (RB_IS_SHELL (shell));

	rb_debug ("Constructing shell");

	/* register with CORBA */
	CORBA_exception_init (&ev);
	
	corba_object = bonobo_object_corba_objref (BONOBO_OBJECT (shell));
	if (bonobo_activation_active_server_register (RB_SHELL_OAFIID, corba_object) != Bonobo_ACTIVATION_REG_SUCCESS)
	{
		/* this is not critical, but worth a warning nevertheless */
		char *msg = rb_shell_corba_exception_to_string (&ev);
		g_message (_("Failed to register the shell: %s\n"
			     "This probably means that you installed RB in a\n"
			     "different prefix than bonobo-activation; this\n"
			     "warning is harmless, but IPC will not work.\n"), msg);
		g_free (msg);
	}

	CORBA_exception_free (&ev);

	rb_debug ("Registered with Bonobo Activation");

	/* initialize UI */
	win = BONOBO_WINDOW (bonobo_window_new ("Rhythmbox shell",
						_("Music Player")));

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

	rb_debug ("shell: creating container area");
	shell->priv->container = bonobo_window_get_ui_container (win);

	bonobo_ui_engine_config_set_path (bonobo_window_get_ui_engine (win),
					  "/apps/rhythmbox/UIConfig/kvps");

	corba_container = BONOBO_OBJREF (shell->priv->container);

	shell->priv->ui_component = bonobo_ui_component_new_default ();

	bonobo_ui_component_set_container (shell->priv->ui_component,
					   corba_container,
					   NULL);

	bonobo_ui_component_freeze (shell->priv->ui_component, NULL);
	
	rb_debug ("shell: loading bonobo ui");
	bonobo_ui_util_set_ui (shell->priv->ui_component,
			       DATADIR,
			       "rhythmbox-ui.xml",
			       "rhythmbox", NULL);

	rb_debug ("shell: setting up tray icon");
	/* tray icon */
	setup_tray_icon (shell);

	bonobo_ui_component_add_verb_list_with_data (shell->priv->ui_component,
						     rb_shell_verbs,
						     shell);
	rb_bonobo_add_listener_list_with_data (shell->priv->ui_component,
					       rb_shell_listeners,
					       shell);
	rb_bonobo_add_listener_list_with_data (shell->priv->tray_icon_component,
					       rb_tray_listeners,
					       shell);

	/* initialize shell services */
	rb_debug ("shell: initializing shell services");
	shell->priv->player_shell = rb_shell_player_new (shell->priv->ui_component,
							 shell->priv->tray_icon_component);
	g_signal_connect (G_OBJECT (shell->priv->player_shell),
			  "window_title_changed",
			  G_CALLBACK (rb_shell_player_window_title_changed_cb),
			  shell);
	shell->priv->source_header = rb_source_header_new ();

	shell->priv->paned = gtk_hpaned_new ();

	shell->priv->sourcelist = rb_sourcelist_new ();
	shell->priv->statusbar = rb_statusbar_new ();

	g_signal_connect (G_OBJECT (shell->priv->sourcelist), "selected",
			  G_CALLBACK (source_selected_cb), shell);

	vbox = gtk_vbox_new (FALSE, 4);
	shell->priv->notebook = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (shell->priv->notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (shell->priv->notebook), FALSE);

	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (shell->priv->source_header), FALSE, TRUE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), shell->priv->notebook);

	gtk_paned_pack1 (GTK_PANED (shell->priv->paned), shell->priv->sourcelist, FALSE, FALSE);
	gtk_paned_pack2 (GTK_PANED (shell->priv->paned), vbox, TRUE, FALSE);
	gtk_paned_set_position (GTK_PANED (shell->priv->paned),
				eel_gconf_get_integer (CONF_STATE_PANED_POSITION));

	vbox = gtk_vbox_new (FALSE, 5);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (shell->priv->player_shell), FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), gtk_hseparator_new (), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), shell->priv->paned, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (shell->priv->statusbar), FALSE, TRUE, 0);

	bonobo_window_set_contents (win, vbox);

	gtk_widget_show_all (vbox);

	rb_debug ("shell: adding gconf notification");
	/* sync state */
	eel_gconf_notification_add (CONF_UI_SOURCELIST_HIDDEN,
				    (GConfClientNotifyFunc) sourcelist_visibility_changed_cb,
				    shell);
	eel_gconf_notification_add (CONF_STATE_WINDOW_HIDDEN,
				    (GConfClientNotifyFunc) window_visibility_changed_cb,
				    shell);

	rb_debug ("shell: syncing with gconf");
	rb_shell_sync_sourcelist_visibility (shell);

	rb_debug ("shell: creating library");
	shell->priv->library = rb_library_new ();

	/* initialize sources */
	rb_debug ("shell: creating library source");
	shell->priv->library_source = rb_library_source_new (shell->priv->container,
							     shell->priv->library);
	rb_shell_append_source (shell, shell->priv->library_source);
	rb_shell_select_source (shell, shell->priv->library_source); /* select this one by default */

	rb_debug ("shell: creating iradio backend");
	shell->priv->iradio_backend = g_object_new (RB_TYPE_IRADIO_BACKEND, NULL);
	rb_debug ("shell: creating iradio source");
	shell->priv->iradio_source = RB_IRADIO_SOURCE (rb_iradio_source_new (shell->priv->container,
									     shell->priv->iradio_backend));
	rb_shell_append_source (shell, RB_SOURCE (shell->priv->iradio_source));

	bonobo_ui_component_thaw (shell->priv->ui_component, NULL);

	rb_shell_window_load_state (shell);

	/* load library */
	rb_debug ("shell: releasing library brakes");
	rb_library_release_brakes (shell->priv->library);

	rb_debug ("shell: calling iradio_backend_load");
 	rb_iradio_backend_load (shell->priv->iradio_backend);
	g_idle_add ((GSourceFunc) rb_shell_update_source_status,
		    shell);

/* REWRITEFIXME */
/*         if (rb_audiocd_is_any_device_available () == TRUE) { */
/* 		rb_debug ("AudioCD device is available"); */
/* 		shell->priv->cd = monkey_media_audio_cd_new (NULL); */

/* 		if (monkey_media_audio_cd_available (shell->priv->cd, NULL)) { */
/* 			rb_debug ("Calling audiocd_changed_cb"); */
/* 			audiocd_changed_cb (shell->priv->cd, */
/* 					    TRUE, */
/* 					    shell); */
/* 		} */
/* 		else { */
/* 			rb_debug("CD is not available"); */
/* 		} */
/* 		g_signal_connect (G_OBJECT (shell->priv->cd), "cd_changed", */
/* 				  G_CALLBACK (audiocd_changed_cb), shell); */
/*         } */
/* 	else */
/* 		rb_debug ("No AudioCD device is available!"); */

	/* now that the lib is loaded, we can load the music groups */
	/* REWRITEFIXME */
/* 	rb_debug ("shell: loading music groups"); */
/* 	rb_shell_load_music_groups (shell); */

	/* GO GO GO! */
	rb_debug ("shell: syncing window visibility");
	rb_shell_sync_window_visibility (shell);
	gtk_widget_show_all (GTK_WIDGET (shell->priv->tray_icon));

	GDK_THREADS_ENTER ();

	rb_debug ("shell: dropping into mainloop");
	while (gtk_events_pending ())
		gtk_main_iteration ();

	GDK_THREADS_LEAVE ();
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
	eel_gconf_set_integer (CONF_STATE_PANED_POSITION,
			       shell->priv->state->paned_position);
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
source_selected_cb (RBSourceList *sourcelist,
		    RBSource *source,
		    RBShell *shell)
{
	rb_shell_select_source (shell, source);
}


static void
rb_shell_append_source (RBShell *shell,
			RBSource *source)
{
	shell->priv->sources
		= g_list_append (shell->priv->sources, source);

	gtk_notebook_append_page (GTK_NOTEBOOK (shell->priv->notebook),
				  GTK_WIDGET (source),
				  gtk_label_new (""));
	gtk_widget_show (GTK_WIDGET (source));

	rb_sourcelist_append (RB_SOURCELIST (shell->priv->sourcelist),
			      source);
}

/* static void */
/* rb_shell_remove_source (RBShell *shell, */
/* 		      RBSource *source) */
/* { */
/* 	shell->priv->sources = g_list_remove (shell->priv->sources, source); */

/* 	rb_sidebar_remove (RB_SIDEBAR (shell->priv->sidebar), */
/* 			   rb_source_get_sidebar_button (source)); */

/* 	gtk_notebook_remove_page (GTK_NOTEBOOK (shell->priv->notebook), */
/* 				  gtk_notebook_page_num (GTK_NOTEBOOK (shell->priv->notebook), GTK_WIDGET (source))); */
/* } */

static gboolean
rb_shell_update_source_status (RBShell *shell)
{
	const char *text;
	if (shell->priv->selected_source != NULL)
	{
		text = rb_source_get_status (shell->priv->selected_source);
		/* rb_echo_area_msg_full (shell->priv->echoarea, text, 0); */
	}
	return FALSE;
}

static void
rb_shell_select_source (RBShell *shell,
			RBSource *source)
{
	rb_debug ("selecting source %p", source);

	shell->priv->selected_source = source;

	/* show source */
	gtk_notebook_set_current_page (GTK_NOTEBOOK (shell->priv->notebook),
				       gtk_notebook_page_num (GTK_NOTEBOOK (shell->priv->notebook), GTK_WIDGET (source)));

	rb_sourcelist_select (RB_SOURCELIST (shell->priv->sourcelist),
			      source);
	
	/* update services */
	rb_shell_player_set_source (shell->priv->player_shell,
				    RB_SOURCE (source));
	rb_source_header_set_source (shell->priv->source_header,
				     RB_SOURCE (source));
	rb_statusbar_set_source (shell->priv->statusbar,
				 RB_SOURCE (source));
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
		rb_debug ("clearing title");
		gtk_window_set_title (GTK_WINDOW (shell->priv->window),
				      _("Music Player"));

		gtk_tooltips_set_tip (shell->priv->tray_icon_tooltip,
				      GTK_WIDGET (shell->priv->tray_icon),
				      _("Not playing"),
				      NULL);
	}
	else
	{
		gboolean playing = rb_shell_player_get_playing (shell->priv->player_shell);
		char *title;

		if (!strcmp (gtk_window_get_title (GTK_WINDOW (shell->priv->window)),
			     window_title))
		    return;

		rb_debug ("setting title to \"%s\"", window_title);
		if (!playing)
		{
			char *tmp;
			title = g_strdup_printf (_("%s (Paused)"), window_title);
			gtk_window_set_title (GTK_WINDOW (shell->priv->window),
					      title);
			tmp = g_strdup_printf (_("%s\nPaused"), window_title);
			gtk_tooltips_set_tip (shell->priv->tray_icon_tooltip,
					      GTK_WIDGET (shell->priv->tray_icon),
					      tmp,
					      NULL);
			g_free (tmp);
		}
		else
		{
			title = g_strdup (window_title);
			gtk_window_set_title (GTK_WINDOW (shell->priv->window),
					      window_title);

			gtk_tooltips_set_tip (shell->priv->tray_icon_tooltip,
					      GTK_WIDGET (shell->priv->tray_icon),
					      window_title,
					      NULL);
		}
	}
}

static void
rb_shell_view_sourcelist_changed_cb (BonoboUIComponent *component,
				     const char *path,
				     Bonobo_UIComponent_EventType type,
				     const char *state,
				     RBShell *shell)
{
	eel_gconf_set_boolean (CONF_UI_SOURCELIST_HIDDEN,
			       !rb_bonobo_get_active (component, CMD_PATH_VIEW_SOURCELIST));
}

static void
rb_shell_show_window_changed_cb (BonoboUIComponent *component,
				 const char *path,
				 Bonobo_UIComponent_EventType type,
				 const char *state,
				 RBShell *shell)
{
	eel_gconf_set_boolean (CONF_STATE_WINDOW_HIDDEN,
			       !rb_bonobo_get_active (component, CMD_PATH_SHOW_WINDOW));
}

static void
rb_shell_cmd_about (BonoboUIComponent *component,
		    RBShell *shell,
		    const char *verbname)
{
	static GtkWidget *about = NULL;
	GdkPixbuf *pixbuf = NULL;

	const char *authors[] =
	{
 		"Lead developers:",
		"Jorn Baayen (jorn@nl.linux.org)",
		"Colin Walters <walters@verbum.org>",
 		"",
		"Contributors:",
		"Olivier Martin (oleevye@wanadoo.fr)",
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
				 _("Copyright 2002,2003 Jorn Baayen,Colin Walters"),
				 _("Music management and playback software for GNOME."),
				 (const char **) authors,
				 (const char **) documenters,
				 strcmp (translator_credits, "translator_credits") != 0 ? translator_credits : NULL,
				 pixbuf);
	gtk_window_set_transient_for (GTK_WINDOW (about), GTK_WINDOW (shell->priv->window));

	g_object_add_weak_pointer (G_OBJECT (about),
				   (gpointer) &about);

	gtk_widget_show (about);
}

static void
rb_shell_cmd_close (BonoboUIComponent *component,
		    RBShell *shell,
		    const char *verbname)
{
	rb_shell_quit (shell);
}

static void
rb_shell_cmd_contents (BonoboUIComponent *component,
		       RBShell *shell,
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
rb_shell_cmd_preferences (BonoboUIComponent *component,
		          RBShell *shell,
		          const char *verbname)
{
	if (shell->priv->prefs == NULL)
	{
		shell->priv->prefs = rb_shell_preferences_new (shell->priv->sources);

		gtk_window_set_transient_for (GTK_WINDOW (shell->priv->prefs),
					      GTK_WINDOW (shell->priv->window));
	}

	gtk_widget_show_all (shell->priv->prefs);
}

static void
ask_file_response_cb (GtkDialog *dialog,
		      int response_id,
		      RBShell *shell)
{
	char **files, **filecur, *stored;

	if (response_id != GTK_RESPONSE_OK)
	{
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	files = gtk_file_selection_get_selections (GTK_FILE_SELECTION (dialog));

	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (files == NULL)
		return;

	filecur = files;

	if (*filecur != NULL)
	{
		char *tmp;

		stored = g_path_get_dirname (*filecur);
		tmp = g_strconcat (stored, "/", NULL);
		eel_gconf_set_string (CONF_STATE_ADD_DIR, tmp);
		g_free (tmp);
		g_free (stored);
	}
    
	while (*filecur != NULL)
	{
		if (g_utf8_validate (*filecur, -1, NULL))
			rb_library_add_uri (shell->priv->library, *filecur);
		filecur++;
	}

	g_strfreev (files);
}

static void
load_playlist_response_cb (GtkDialog *dialog,
			   int response_id,
			   RBShell *shell)
{
	char **files, **filecur;

	if (response_id != GTK_RESPONSE_OK)
	{
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	files = gtk_file_selection_get_selections (GTK_FILE_SELECTION (dialog));

	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (files == NULL)
		return;

	filecur = files;

	while (*filecur != NULL)
	{
		if (g_utf8_validate (*filecur, -1, NULL))
			rb_playlist_load (shell->priv->library,
					  shell->priv->iradio_backend, *filecur);
		filecur++;
	}

	g_strfreev (files);
}

static void
rb_shell_cmd_add_to_library (BonoboUIComponent *component,
			     RBShell *shell,
			     const char *verbname)
{
	char *stored;
	GtkWidget *dialog;
    
	stored = eel_gconf_get_string (CONF_STATE_ADD_DIR);
	dialog = rb_ask_file_multiple (_("Choose Files or Directory"),
				      stored,
			              GTK_WINDOW (shell->priv->window));
	g_free (stored);

	g_signal_connect (G_OBJECT (dialog),
			  "response",
			  G_CALLBACK (ask_file_response_cb),
			  shell);
}

static void
rb_shell_cmd_add_location (BonoboUIComponent *component,
			   RBShell *shell,
			   const char *verbname)
{
	rb_library_source_add_location (RB_LIBRARY_SOURCE (shell->priv->library_source),
					GTK_WINDOW (shell->priv->window));
}

static void
rb_shell_cmd_load_playlist (BonoboUIComponent *component,
			    RBShell *shell,
			    const char *verbname)
{
	GtkWidget *dialog;
    
	dialog = rb_ask_file_multiple (_("Load playlist"),
				      NULL,
			              GTK_WINDOW (shell->priv->window));

	g_signal_connect (G_OBJECT (dialog),
			  "response",
			  G_CALLBACK (load_playlist_response_cb),
			  shell);
}

/* REWRITEFIXME */
/* static void */
/* ask_string_response_cb (GtkDialog *dialog, */
/* 			int response_id, */
/* 			RBShell *shell) */
/* { */
/* 	GtkWidget *entry, *checkbox; */
/* 	RBSource *group; */
/* 	char *name; */
/* 	gboolean add_selection; */
/* 	CreateGroupType type; */
/* 	GList *data, *l; */

/* 	type = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (dialog), "type")); */
/* 	data = g_object_get_data (G_OBJECT (dialog), "data"); */

/* 	if (response_id != GTK_RESPONSE_OK) */
/* 	{ */
/* 		gtk_widget_destroy (GTK_WIDGET (dialog)); */
/* 		if (type == CREATE_GROUP_WITH_URI_LIST) */
/* 			gnome_vfs_uri_list_free (data); */
/* 		return; */
/* 	} */

/* 	entry = g_object_get_data (G_OBJECT (dialog), "entry"); */
/* 	name = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry))); */

/* 	checkbox = g_object_get_data (G_OBJECT (dialog), "checkbox"); */
/* 	add_selection = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbox)); */

/* 	gtk_widget_destroy (GTK_WIDGET (dialog)); */
	
/* 	if (name == NULL) */
/* 	{ */
/* 		if (type == CREATE_GROUP_WITH_URI_LIST) */
/* 			gnome_vfs_uri_list_free (data); */
/* 		return; */
/* 	} */

/* 	group = rb_group_source_new (shell->priv->container, */
/* 				     shell->priv->library); */
/* 	rb_group_source_set_name (RB_GROUP_SOURCE (group), name); */
/* 	shell->priv->groups = g_list_append (shell->priv->groups, group); */
/* 	rb_shell_append_source (shell, group); */
/* 	g_free (name); */

/* 	switch (type) */
/* 	{ */
/* 	case CREATE_GROUP_WITH_NODE_LIST: */
/* 		for (l = data; l != NULL; l = g_list_next (l)) */
/* 		{ */
/* 			rb_group_view_add_node (RB_GROUP_VIEW (group), */
/* 						RB_NODE (l->data)); */
/* 		} */
/* 		break; */
/* 	case CREATE_GROUP_WITH_URI_LIST: */
/* 		for (l = data; l != NULL; l = g_list_next (l)) */
/* 		{ */
/* 			char *uri; */
/* 			RBNode *node; */
				
/* 			uri = gnome_vfs_uri_to_string ((GnomeVFSURI *) l->data, GNOME_VFS_URI_HIDE_NONE); */
/* 			node = rb_library_get_song_by_location (shell->priv->library, uri); */

/* 			if (node != NULL) */
/* 			{ */
/* 				/\* add this node to the newly created group *\/ */
/* 				rb_group_view_add_node (RB_GROUP_VIEW (group), node); */
/* 			} */
/* 			else */
/* 			{ */
/* 				/\* will add these nodes to the newly created group *\/ */
/* 				RBLibraryAction *action = rb_library_add_uri (shell->priv->library, uri); */
/* 				g_object_set_data (G_OBJECT (group), "library", shell->priv->library); */
/*                                 g_signal_connect_object (G_OBJECT (action), */
/*                                                          "handled",  */
/*                                                          G_CALLBACK (dnd_add_handled_cb), */
/*                                                          G_OBJECT (group), */
/*                                                          0); */
/* 			} */

/* 			g_free (uri); */
/* 		} */
/* 		gnome_vfs_uri_list_free (data); */
/* 		break; */
/* 	case CREATE_GROUP_WITH_SELECTION: */
/* 		{	 */
/* 			/\* add the current selection if the user checked *\/ */
/* 			if (add_selection) */
/* 			{ */
/* 				GList *i = NULL; */
/* 				GList *selection = rb_view_get_selection (shell->priv->selected_view); */
/* 				for (i  = selection; i != NULL; i = g_list_next (i)) */
/* 					rb_group_view_add_node (RB_GROUP_VIEW (group), i->data); */
/* 			} */
/* 		} */
/* 		break; */
/* 	} */

/* 	rb_shell_save_music_groups (shell); */
/* } */

/* static void */
/* create_group (RBShell *shell, CreateGroupType type, */
/* 	      GList *data) */
/* { */
/* 	GtkWidget *dialog; */
	
/* 	dialog = rb_group_source_create_dialog (shell); */

/* 	g_object_set_data (G_OBJECT (dialog), "type", GINT_TO_POINTER (type)); */
/* 	g_object_set_data (G_OBJECT (dialog), "data", data); */

/* 	g_signal_connect (G_OBJECT (dialog), */
/* 			  "response", */
/* 			  G_CALLBACK (ask_string_response_cb), */
/* 			  shell); */
/* } */

/* static void */
/* rb_shell_cmd_new_group (BonoboUIComponent *component, */
/* 			RBShell *shell, */
/* 			const char *verbname) */
/* { */
/* 	create_group (shell, CREATE_GROUP_WITH_SELECTION, NULL); */
/* } */

/* static void */
/* rb_shell_cmd_new_station (BonoboUIComponent *component, */
/* 			  RBShell *shell, */
/* 			  const char *verbname) */
/* { */
/* 	GtkWidget *dialog; */
/* 	rb_debug ("Got new station command"); */
/* 	dialog = rb_new_station_dialog_new (shell->priv->iradio_backend); */
/* 	gtk_dialog_run (GTK_DIALOG (dialog)); */
/* 	gtk_widget_destroy (dialog); */
/* } */


static void
rb_shell_quit (RBShell *shell)
{
	rb_debug ("Quitting");

	rb_shell_window_save_state (shell);

	bonobo_object_unref (BONOBO_OBJECT (shell));
}

/* REWRITEFIXME */
/* static void */
/* rb_shell_load_music_groups (RBShell *shell) */
/* { */
/* 	GSList *groups, *l; */

/* 	groups = eel_gconf_get_string_list (CONF_MUSIC_GROUPS); */

/* 	for (l = groups; l != NULL; l = g_slist_next (l)) */
/* 	{ */
/* 		RBView *group; */

/* 		group = rb_group_view_new_from_file (shell->priv->container, */
/* 						     shell->priv->library, */
/* 						     (char *) l->data); */
/* 		shell->priv->groups = g_list_append (shell->priv->groups, group); */

/* 		rb_shell_append_view (shell, group); */
/* 	} */

/* 	g_slist_foreach (groups, (GFunc) g_free, NULL); */
/* 	g_slist_free (groups); */
/* } */

/* static void */
/* rb_shell_save_music_groups (RBShell *shell) */
/* { */
/* 	GSList *groups = NULL; */
/* 	GList *l; */

/* 	for (l = shell->priv->groups; l != NULL; l = g_list_next (l)) */
/* 	{ */
/* 		RBGroupView *group = RB_GROUP_VIEW (l->data); */

/* 		groups = g_slist_append (groups, */
/* 					 (char *) rb_group_view_get_file (group)); */
/* 		rb_group_view_save (group); */
/* 	} */
	
/* 	eel_gconf_set_string_list (CONF_MUSIC_GROUPS, groups); */
	
/* 	g_slist_free (groups); */
/* } */

static void
rb_shell_sync_sourcelist_visibility (RBShell *shell)
{
	gboolean visible;

	visible = !eel_gconf_get_boolean (CONF_UI_SOURCELIST_HIDDEN);

	if (visible)
		gtk_widget_show (GTK_WIDGET (shell->priv->sourcelist));
	else
		gtk_widget_hide (GTK_WIDGET (shell->priv->sourcelist));

	rb_bonobo_set_active (shell->priv->ui_component,
			      CMD_PATH_VIEW_SOURCELIST,
			      visible);
}

/* REWRITEFIXME */
/* static void */
/* rb_shell_source_status_changed_cb (RBViewStatus *status, */
/* 				   gpointer data) */
/* { */
/* 	RBShell *shell = RB_SHELL (data); */
/* 	const char *text = rb_source_get_status (status); */
/* 	rb_echo_area_msg_full (shell->priv->echoarea, text, 0, FALSE); */
/* } */

static void
rb_shell_sync_window_visibility (RBShell *shell)
{
	gboolean visible;
	static int window_x = -1;
	static int window_y = -1;

	visible = !eel_gconf_get_boolean (CONF_STATE_WINDOW_HIDDEN);
	
	if (visible == TRUE)
	{
		if (window_x >= 0 && window_y >= 0)
		{
			gtk_window_move (GTK_WINDOW (shell->priv->window), window_x,
					 window_y);
		}
		gtk_widget_show (shell->priv->window);
	}
	else
	{
		gtk_window_get_position (GTK_WINDOW (shell->priv->window),
					 &window_x, &window_y);
		gtk_widget_hide (shell->priv->window);
	}
	
	rb_bonobo_set_active (shell->priv->ui_component,
			      CMD_PATH_SHOW_WINDOW,
			      visible);
}

static void
sourcelist_visibility_changed_cb (GConfClient *client,
				  guint cnxn_id,
				  GConfEntry *entry,
				  RBShell *shell)
{
	rb_debug ("sourcelist visibility changed"); 
	rb_shell_sync_sourcelist_visibility (shell);
}

static void
window_visibility_changed_cb (GConfClient *client,
			      guint cnxn_id,
			      GConfEntry *entry,
			      RBShell *shell)
{
	rb_shell_sync_window_visibility (shell);

	GDK_THREADS_ENTER ();
	while (gtk_events_pending ())
		gtk_main_iteration ();
	GDK_THREADS_LEAVE ();
}

/* REWRITEFIXME */
/* static void */
/* add_uri (const char *uri, */
/* 	 RBGroupSource *source) */
/* { */
/* 	RBNode *node; */

/* 	node = rb_library_get_song_by_location (g_object_get_data (G_OBJECT (source), "library"), */
/* 					        uri); */

/* 	if (node != NULL) */
/* 	{ */
/* 		rb_group_source_add_node (source, node); */
/* 	} */
/* } */

/* static void */
/* dnd_add_handled_cb (RBLibraryAction *action, */
/* 		    RBGroupSource *source) */
/* { */
/* 	char *uri; */
/* 	RBLibraryActionType type; */

/* 	rb_library_action_get (action, */
/* 			       &type, */
/* 			       &uri); */

/* 	switch (type) */
/* 	{ */
/* 	case RB_LIBRARY_ACTION_ADD_FILE: */
/* 		{ */
/* 			RBNode *node; */

/* 			node = rb_library_get_song_by_location (g_object_get_data (G_OBJECT (source), "library"), */
/* 								uri); */

/* 			if (node != NULL) */
/* 			{ */
/* 				rb_group_view_add_node (source, node); */
/* 			} */
/* 		} */
/* 		break; */
/* 	case RB_LIBRARY_ACTION_ADD_DIRECTORY: */
/* 		{ */
/* 			rb_uri_handle_recursively (uri, */
/* 						   (GFunc) add_uri, */
/* 						   view); */
/* 		} */
/* 		break; */
/* 	default: */
/* 		break; */
/* 	} */
/* } */

/* static void */
/* handle_songs_func (RBNode *node, */
/* 		   RBGroupSource *group) */
/* { */
/* 	rb_group_view_add_node (group, node); */
/* } */

/* static void */
/* rb_sidebar_drag_finished_cb (RBSidebar *sidebar, */
/* 			     GdkDragContext *context, */
/* 			     int x, int y, */
/* 			     GtkSelectionData *data, */
/* 			     guint info, */
/* 			     guint time, */
/* 			     RBShell *shell) */
/* { */
/* 	switch (info) */
/* 	{ */
/* 	case RB_LIBRARY_DND_NODE_ID: */
/* 		{ */
/* 			long id; */
/* 			RBNode *node; */
/* 			RBGroupSource *group; */

/* 			id = atol (data->data); */
/* 			node = rb_node_get_from_id (id); */

/* 			if (node == NULL) */
/* 				break; */
			
/* 			group = RB_GROUP_VIEW (rb_group_view_new (shell->priv->container, */
/* 						                  shell->priv->library)); */
					
/* 			rb_group_view_set_name (RB_GROUP_VIEW (group), */
/* 						rb_node_get_property_string (node, */
/* 								             RB_NODE_PROP_NAME)); */


/* 			rb_library_handle_songs (shell->priv->library, */
/* 						 node, */
/* 						 (GFunc) handle_songs_func, */
/* 						 group); */

/* 			shell->priv->groups = g_list_append (shell->priv->groups, group); */
/* 			rb_shell_append_view (shell, RB_VIEW (group)); */
/* 		} */
/* 		break; */
/* 	case RB_LIBRARY_DND_URI_LIST: */
/* 		{ */
/* 			GList *list; */

/* 			list = gnome_vfs_uri_list_parse (data->data); */
/*  			create_group (shell, CREATE_GROUP_WITH_URI_LIST, list);  */
/* 		} */
/* 		break; */
/* 	} */

/* 	gtk_drag_finish (context, TRUE, FALSE, time); */
/* } */

static void
tray_button_press_event_cb (GtkWidget *ebox,
			    GdkEventButton *event,
			    RBShell *shell)
{
	switch (event->button)
	{
	case 1:
		/* toggle mainwindow visibility */
		eel_gconf_set_boolean (CONF_STATE_WINDOW_HIDDEN,
				       !eel_gconf_get_boolean (CONF_STATE_WINDOW_HIDDEN));
		break;
	case 3:
		/* contextmenu */
		sync_tray_menu (shell);
		bonobo_control_do_popup (shell->priv->tray_icon_control,
					 event->button,
					 event->time);
		break;
	}
}

static void
tray_drop_cb (GtkWidget *widget,
	      GdkDragContext *context,
	      gint x,
	      gint y,
	      GtkSelectionData *data,
	      guint info,
	      guint time,
	      RBShell *shell)
{
	GList *list, *uri_list, *i;
	GtkTargetList *tlist;
	gboolean ret;

	tlist = gtk_target_list_new (target_uri, 1);
	ret = (gtk_drag_dest_find_target (widget, context, tlist) != GDK_NONE);
	gtk_target_list_unref (tlist);

	if (ret == FALSE)
		return;

	list = gnome_vfs_uri_list_parse (data->data);

	if (list == NULL)
	{
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	uri_list = NULL;

	for (i = list; i != NULL; i = g_list_next (i))
	{
		uri_list = g_list_append (uri_list, gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0));
	}
	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL)
	{
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	for (i = uri_list; i != NULL; i = i->next)
	{
		char *uri = i->data;

		if (uri != NULL)
		{
			rb_library_add_uri (shell->priv->library, uri);
		}

		g_free (uri);
	}

	g_list_free (uri_list);

	gtk_drag_finish (context, TRUE, FALSE, time);
}

static void
setup_tray_icon (RBShell *shell)
{
	GtkWidget *ebox, *image;
	BonoboControlFrame *frame;

	shell->priv->tray_icon_tooltip = gtk_tooltips_new ();

	shell->priv->tray_icon = egg_tray_icon_new ("Rhythmbox tray icon");
	gtk_tooltips_set_tip (shell->priv->tray_icon_tooltip,
			      GTK_WIDGET (shell->priv->tray_icon),
			      _("Not playing"),
			      NULL);
	ebox = gtk_event_box_new ();
	g_signal_connect (G_OBJECT (ebox),
			  "button_press_event",
			  G_CALLBACK (tray_button_press_event_cb),
			  shell);
	gtk_drag_dest_set (ebox, GTK_DEST_DEFAULT_ALL,			                                   target_uri, 1, GDK_ACTION_COPY);
	g_signal_connect (G_OBJECT (ebox), "drag_data_received",
			  G_CALLBACK (tray_drop_cb), shell);

	image = gtk_image_new_from_stock (RB_STOCK_TRAY_ICON,
					  GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_container_add (GTK_CONTAINER (ebox), image);
	
	shell->priv->tray_icon_control = bonobo_control_new (ebox);
	shell->priv->tray_icon_component =
		bonobo_control_get_popup_ui_component (shell->priv->tray_icon_control);

	frame = bonobo_control_frame_new (BONOBO_OBJREF (shell->priv->container));
	bonobo_control_frame_bind_to_control (frame, BONOBO_OBJREF (shell->priv->tray_icon_control),
					      NULL);
	gtk_container_add (GTK_CONTAINER (shell->priv->tray_icon),
			   bonobo_control_frame_get_widget (frame));

	gtk_widget_show_all (GTK_WIDGET (ebox));
}

static void
sync_tray_menu (RBShell *shell)
{
	BonoboUIComponent *pcomp;
	BonoboUINode *node;

	pcomp = bonobo_control_get_popup_ui_component (shell->priv->tray_icon_control);
	
	bonobo_ui_component_set (pcomp, "/", "<popups></popups>", NULL);

	node = bonobo_ui_component_get_tree (shell->priv->ui_component, "/popups/TrayPopup", TRUE, NULL);
	bonobo_ui_node_set_attr (node, "name", "button3");
	bonobo_ui_component_set_tree (pcomp, "/popups", node, NULL);
	bonobo_ui_node_free (node);

	node = bonobo_ui_component_get_tree (shell->priv->ui_component, "/commands", TRUE, NULL);
	bonobo_ui_component_set_tree (pcomp, "/", node, NULL);
	bonobo_ui_node_free (node);
}
