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
#include <gdk/gdk.h>
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
#include "rb-node-db.h"
#include "rb-stock-icons.h"
#include "rb-sourcelist.h"
#include "rb-string-helpers.h"
#include "rb-file-helpers.h"
#include "rb-source.h"
#include "rb-preferences.h"
#include "rb-druid.h"
#include "rb-shell-clipboard.h"
#include "rb-shell-player.h"
#include "rb-source-header.h"
#include "rb-statusbar.h"
#include "rb-shell-preferences.h"
#include "rb-playlist.h"
#include "rb-bonobo-helpers.h"
#include "rb-library.h"
#include "rb-library-source.h"
#include "rb-iradio-backend.h"
#include "rb-load-failure-dialog.h"
#include "rb-new-station-dialog.h"
#include "rb-iradio-source.h"
#ifdef HAVE_AUDIOCD
#include "rb-audiocd-source.h"
#endif
#include "rb-shell-preferences.h"
#include "rb-group-source.h"
#include "rb-file-monitor.h"
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
static char * rb_shell_corba_get_playing_title (PortableServer_Servant _servant,
						CORBA_Environment *ev);
static char * rb_shell_corba_get_playing_path (PortableServer_Servant _servant,
					       CORBA_Environment *ev);

void rb_shell_handle_playlist_entry (RBShell *shell, GList *locations, const char *title,
				     const char *genre);
static gboolean rb_shell_window_state_cb (GtkWidget *widget,
					  GdkEvent *event,
					  RBShell *shell);
static gboolean rb_shell_window_delete_cb (GtkWidget *win,
			                   GdkEventAny *event,
			                   RBShell *shell);
static void rb_shell_sync_window_state (RBShell *shell);
static void rb_shell_sync_paned (RBShell *shell);
static void paned_size_allocate_cb (GtkWidget *widget,
				    GtkAllocation *allocation,
				    RBShell *shell);
static void rb_shell_select_source (RBShell *shell, RBSource *source);
static void rb_shell_append_source (RBShell *shell, RBSource *source);
static void source_selected_cb (RBSourceList *sourcelist,
				RBSource *source,
				RBShell *shell);
static void rb_shell_library_error_cb (RBLibrary *library,
				       const char *uri, const char *msg,
				       RBShell *shell); 
static void rb_shell_library_operation_end_cb (RBLibrary *library,
					       RBShell *shell); 
static void rb_shell_start_operation (RBShell *shell);
static void rb_shell_load_failure_dialog_response_cb (GtkDialog *dialog,
						      int response_id,
						      RBShell *shell);

static void rb_shell_remove_source (RBShell *shell, RBSource *source);
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
static void rb_shell_cmd_save_playlist (BonoboUIComponent *component,
					RBShell *shell,
					const char *verbname);
static void rb_shell_cmd_new_playlist (BonoboUIComponent *component,
				       RBShell *shell,
				       const char *verbname);
static void rb_shell_cmd_new_station (BonoboUIComponent *component,
				      RBShell *shell,
				      const char *verbname);
static void rb_shell_cmd_delete_playlist (BonoboUIComponent *component,
				       RBShell *shell,
				       const char *verbname);
static void rb_shell_cmd_rename_playlist (BonoboUIComponent *component,
				       RBShell *shell,
				       const char *verbname);
static void rb_shell_cmd_extract_cd (BonoboUIComponent *component,
				       RBShell *shell,
				       const char *verbname);
static void rb_shell_cmd_current_song (BonoboUIComponent *component,
				       RBShell *shell,
				       const char *verbname);
static void rb_shell_jump_to_current (RBShell *shell);
GtkWidget * rb_shell_new_group_dialog (RBShell *shell);
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
static void rb_shell_load_playlists (RBShell *shell);
static void rb_shell_sync_sourcelist_visibility (RBShell *shell);
static void rb_shell_sync_window_visibility (RBShell *shell);
static void sourcelist_visibility_changed_cb (GConfClient *client,
					      guint cnxn_id,
					      GConfEntry *entry,
					      RBShell *shell);
static void window_visibility_changed_cb (GConfClient *client,
			                  guint cnxn_id,
			                  GConfEntry *entry,
			                  RBShell *shell);
static void paned_changed_cb (GConfClient *client,
			      guint cnxn_id,
			      GConfEntry *entry,
			      RBShell *shell);
#ifdef HAVE_AUDIOCD
static void audiocd_changed_cb (MonkeyMediaAudioCD *cd,
				gboolean available,
				gpointer data);
#endif
static void sourcelist_drag_received_cb (RBSourceList *sourcelist,
					 RBSource *source,
					 GtkSelectionData *data,
					 RBShell *shell);
static gboolean rb_shell_show_popup_cb (RBSourceList *sourcelist,
					RBSource *target,
					RBShell *shell);
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
	CREATE_GROUP_WITH_FILE,
	CREATE_GROUP_WITH_SELECTION
} CreateGroupType;

static void create_group (RBShell *shell, CreateGroupType type, GList *data);

#define CMD_PATH_VIEW_SOURCELIST   "/commands/ShowSourceList"
#define CMD_PATH_SHOW_WINDOW    "/commands/ShowWindow"
#define CMD_PATH_PLAYLIST_DELETE   "/commands/FileDeletePlaylist"
#define CMD_PATH_PLAYLIST_SAVE   "/commands/SavePlaylist"
#define CMD_PATH_EXTRACT_CD     "/commands/ExtractCD"
#define CMD_PATH_CURRENT_SONG	"/commands/CurrentSong"

/* prefs */
#define CONF_STATE_WINDOW_WIDTH     CONF_PREFIX "/state/window_width"
#define CONF_STATE_WINDOW_HEIGHT    CONF_PREFIX "/state/window_height"
#define CONF_STATE_WINDOW_MAXIMIZED CONF_PREFIX "/state/window_maximized"
#define CONF_STATE_WINDOW_HIDDEN    CONF_PREFIX "/state/window_hidden"
#define CONF_STATE_PANED_POSITION   CONF_PREFIX "/state/paned_position"
#define CONF_STATE_ADD_DIR          CONF_PREFIX "/state/add_dir"

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
	RBShellClipboard *clipboard_shell;
	RBSourceHeader *source_header;
	RBStatusbar *statusbar;

	RBLibrary *library;
	RBSource *library_source;
	GtkWidget *load_error_dialog;
	GList *supported_media_extensions;
	gboolean show_library_errors;

	RBIRadioBackend *iradio_backend;
 	RBIRadioSource *iradio_source;

#ifdef HAVE_AUDIOCD
 	MonkeyMediaAudioCD *cd;
#endif

	RBSource *selected_source;

	GtkWidget *prefs;

	GList *groups;

	EggTrayIcon *tray_icon;
	GtkTooltips *tray_icon_tooltip;
	BonoboControl *tray_icon_control;
	BonoboUIComponent *tray_icon_component;

	RBVolume *volume;

	guint operation_count;

	RBGroupSource *loading_group;
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
	BONOBO_UI_VERB ("SavePlaylist", (BonoboUIVerbFn) rb_shell_cmd_save_playlist),
 	BONOBO_UI_VERB ("NewPlaylist",  (BonoboUIVerbFn) rb_shell_cmd_new_playlist),
	BONOBO_UI_VERB ("NewStation",   (BonoboUIVerbFn) rb_shell_cmd_new_station),
	BONOBO_UI_VERB ("FileDeletePlaylist",(BonoboUIVerbFn) rb_shell_cmd_delete_playlist),
	BONOBO_UI_VERB ("RenamePlaylist",(BonoboUIVerbFn) rb_shell_cmd_rename_playlist),
	BONOBO_UI_VERB ("DeletePlaylist",  (BonoboUIVerbFn) rb_shell_cmd_delete_playlist),
	BONOBO_UI_VERB ("ExtractCD",  (BonoboUIVerbFn) rb_shell_cmd_extract_cd),
	BONOBO_UI_VERB ("CurrentSong",	(BonoboUIVerbFn) rb_shell_cmd_current_song),
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
	epv->getPlayingTitle = rb_shell_corba_get_playing_title;
	epv->getPlayingPath = rb_shell_corba_get_playing_path;
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
	
	eel_gconf_monitor_add (CONF_PREFIX);

}

static void
rb_shell_finalize (GObject *object)
{
        RBShell *shell = RB_SHELL (object);

	gtk_widget_hide (shell->priv->window);
	gtk_widget_hide (GTK_WIDGET (shell->priv->tray_icon));
	rb_shell_player_stop (shell->priv->player_shell);

	eel_gconf_monitor_remove (CONF_PREFIX);

	bonobo_activation_active_server_unregister (RB_SHELL_OAFIID, bonobo_object_corba_objref (BONOBO_OBJECT (shell)));
	
	bonobo_activation_active_server_unregister (RB_FACTORY_OAFIID, bonobo_object_corba_objref (BONOBO_OBJECT (shell)));


	rb_debug ("Unregistered with Bonobo Activation");
	
	gtk_widget_destroy (GTK_WIDGET (shell->priv->load_error_dialog));
	g_list_free (shell->priv->supported_media_extensions);

	gtk_widget_destroy (GTK_WIDGET (shell->priv->tray_icon));
	
	g_list_free (shell->priv->sources);

	g_list_free (shell->priv->groups);

	g_object_unref (G_OBJECT (shell->priv->clipboard_shell));

	gtk_widget_destroy (shell->priv->window);

	/* hack to make the gdk thread lock available for freeing
	 * the library.. evil */
	g_object_unref (G_OBJECT (shell->priv->library));

	g_object_unref (G_OBJECT (shell->priv->iradio_backend));

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
handle_playlist_entry_cb (RBPlaylist *playlist, const char *uri, const char *title,
			  const char *genre, RBShell *shell)
{
	/* We assume all HTTP is iradio.  This is probably a broken assumption,
	 * but it's very difficult to really fix...
	 */
	if (rb_uri_is_iradio (uri) != FALSE)
		rb_iradio_backend_add_station_full (shell->priv->iradio_backend, uri, title, genre);
	else
		rb_library_add_uri (shell->priv->library, (char *) uri);
}

static void
rb_shell_corba_handle_file (PortableServer_Servant _servant,
			    const CORBA_char *uri,
			    CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));
	RBPlaylist *parser;

	GnomeVFSURI *vfsuri = gnome_vfs_uri_new (uri);
	if (!vfsuri) {
		rb_error_dialog (_("Unable to parse URI \"%s\"\n"), uri);
		return;
	}

	parser = rb_playlist_new ();	
	g_signal_connect (G_OBJECT (parser), "entry",
			  G_CALLBACK (handle_playlist_entry_cb), shell);

	/* Try parsing it as a playlist; otherwise just try adding it to
	 * the library.
	 */
	if (!rb_playlist_parse (parser, uri))
		rb_library_add_uri (shell->priv->library, uri);
	g_object_unref (G_OBJECT (parser));
}

static void
rb_shell_corba_add_to_library (PortableServer_Servant _servant,
			       const CORBA_char *uri,
			       CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));

	rb_library_add_uri (shell->priv->library, uri);
}

static void
rb_shell_corba_grab_focus (PortableServer_Servant _servant,
			   CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));
	eel_gconf_set_boolean (CONF_STATE_WINDOW_HIDDEN, FALSE);

	gtk_window_present (GTK_WINDOW (shell->priv->window));
	gtk_widget_grab_focus (shell->priv->window);
}

static CORBA_char *
rb_shell_corba_get_playing_title (PortableServer_Servant _servant,
				  CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));
	const char *str;
	CORBA_char *ret;

	GDK_THREADS_ENTER ();
	str = gtk_window_get_title (GTK_WINDOW (shell->priv->window));
	ret = CORBA_string_alloc (strlen (str));
	strcpy (ret, str);
	GDK_THREADS_LEAVE ();
	return ret;
}

static CORBA_char *
rb_shell_corba_get_playing_path (PortableServer_Servant _servant,
				 CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));
	const char *str;
	CORBA_char *ret;
	
	GDK_THREADS_ENTER ();
	str = rb_shell_player_get_playing_path (shell->priv->player_shell);
	if (str == NULL)
		str = "";
	ret = CORBA_string_alloc (strlen (str));
	strcpy (ret, str);
	GDK_THREADS_LEAVE ();
	return ret;
}

static gboolean
async_library_release_brakes (RBShell *shell)
{
	rb_debug ("async releasing library brakes");

	GDK_THREADS_ENTER ();

	rb_shell_start_operation (shell);
	rb_library_release_brakes (shell->priv->library);

	GDK_THREADS_LEAVE ();

	return FALSE;
}

void
rb_shell_construct (RBShell *shell)
{
	gboolean registration_failed = FALSE;
	CORBA_Object corba_object;
	CORBA_Environment ev;
	BonoboWindow *win;
	Bonobo_UIContainer corba_container;
	GtkWidget *vbox;
	
	g_return_if_fail (RB_IS_SHELL (shell));

	rb_debug ("Constructing shell");

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
	shell->priv->clipboard_shell = rb_shell_clipboard_new (shell->priv->ui_component);
	shell->priv->source_header = rb_source_header_new (shell->priv->ui_component);

	shell->priv->paned = gtk_hpaned_new ();

	shell->priv->sourcelist = rb_sourcelist_new ();
	g_signal_connect (G_OBJECT (shell->priv->sourcelist), "drop_received",
			  G_CALLBACK (sourcelist_drag_received_cb), shell);
	g_signal_connect (G_OBJECT (shell->priv->sourcelist), "show_popup",
			  G_CALLBACK (rb_shell_show_popup_cb), shell);
	
	shell->priv->statusbar = rb_statusbar_new ();

	rb_sourcelist_set_dnd_targets (RB_SOURCELIST (shell->priv->sourcelist), target_table,
				       G_N_ELEMENTS (target_table));

	g_signal_connect (G_OBJECT (shell->priv->sourcelist), "selected",
			  G_CALLBACK (source_selected_cb), shell);

	vbox = gtk_vbox_new (FALSE, 4);
	shell->priv->notebook = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (shell->priv->notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (shell->priv->notebook), FALSE);
	g_signal_connect (G_OBJECT (shell->priv->notebook),
			  "size_allocate",
			  G_CALLBACK (paned_size_allocate_cb),
			  shell);

	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (shell->priv->source_header), FALSE, TRUE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), shell->priv->notebook);

	if (gtk_widget_get_default_direction () != GTK_TEXT_DIR_RTL) {
		gtk_paned_pack1 (GTK_PANED (shell->priv->paned), shell->priv->sourcelist, TRUE, TRUE);
		gtk_paned_pack2 (GTK_PANED (shell->priv->paned), vbox, TRUE, TRUE);
	} else {
		gtk_paned_pack1 (GTK_PANED (shell->priv->paned), vbox, TRUE, TRUE);
		gtk_paned_pack2 (GTK_PANED (shell->priv->paned), shell->priv->sourcelist, TRUE, TRUE);
	}

	vbox = gtk_vbox_new (FALSE, 5);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (shell->priv->player_shell), FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), gtk_hseparator_new (), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), shell->priv->paned, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (shell->priv->statusbar), FALSE, TRUE, 0);

	bonobo_window_set_contents (win, vbox);

	gtk_widget_show_all (vbox);

	rb_debug ("shell: creating library");
	shell->priv->library = rb_library_new ();

	rb_debug ("shell: adding gconf notification");
	/* sync state */
	eel_gconf_notification_add (CONF_UI_SOURCELIST_HIDDEN,
				    (GConfClientNotifyFunc) sourcelist_visibility_changed_cb,
				    shell);
	eel_gconf_notification_add (CONF_STATE_WINDOW_HIDDEN,
				    (GConfClientNotifyFunc) window_visibility_changed_cb,
				    shell);
	eel_gconf_notification_add (CONF_STATE_PANED_POSITION,
				    (GConfClientNotifyFunc) paned_changed_cb,
				    shell);

	rb_debug ("shell: syncing with gconf");
	rb_shell_sync_sourcelist_visibility (shell);

	shell->priv->load_error_dialog = rb_load_failure_dialog_new ();
	shell->priv->show_library_errors = FALSE;
	gtk_widget_hide (shell->priv->load_error_dialog);

	shell->priv->supported_media_extensions = monkey_media_get_supported_filename_extensions ();

	g_signal_connect (G_OBJECT (shell->priv->library), "error",
			  G_CALLBACK (rb_shell_library_error_cb), shell);
	g_signal_connect (G_OBJECT (shell->priv->library), "operation-end",
			  G_CALLBACK (rb_shell_library_operation_end_cb), shell);
	g_signal_connect (G_OBJECT (shell->priv->load_error_dialog), "response",
			  G_CALLBACK (rb_shell_load_failure_dialog_response_cb), shell);

	/* initialize sources */
	rb_debug ("shell: creating library source");
	shell->priv->library_source = rb_library_source_new (shell->priv->container,
							     shell->priv->library);
	rb_shell_append_source (shell, shell->priv->library_source);

	rb_debug ("shell: creating iradio backend");
	shell->priv->iradio_backend = g_object_new (RB_TYPE_IRADIO_BACKEND, NULL);
	rb_debug ("shell: creating iradio source");
	shell->priv->iradio_source = RB_IRADIO_SOURCE (rb_iradio_source_new (shell->priv->container,
									     shell->priv->iradio_backend));
	rb_shell_append_source (shell, RB_SOURCE (shell->priv->iradio_source));

	rb_shell_sync_window_state (shell);

	bonobo_ui_component_thaw (shell->priv->ui_component, NULL);

	rb_shell_select_source (shell, shell->priv->library_source); /* select this one by default */

	/* Look for Sound Juicer */
	rb_bonobo_set_sensitive (shell->priv->ui_component, CMD_PATH_EXTRACT_CD, 
			g_find_program_in_path ("sound-juicer") != NULL);


	/* load library */
	rb_debug ("shell: loading library");
	rb_library_load (shell->priv->library);

	rb_debug ("shell: loading iradio");
 	rb_iradio_backend_load (shell->priv->iradio_backend);

#ifdef HAVE_AUDIOCD
        if (rb_audiocd_is_any_device_available () == TRUE) {
		rb_debug ("AudioCD device is available");
		shell->priv->cd = monkey_media_audio_cd_new (NULL);

		if (monkey_media_audio_cd_available (shell->priv->cd, NULL)) {
			rb_debug ("Calling audiocd_changed_cb");
			audiocd_changed_cb (shell->priv->cd,
					    TRUE,
					    shell);
		}
		else
			rb_debug("CD is not available");

		g_signal_connect (G_OBJECT (shell->priv->cd), "cd_changed",
				  G_CALLBACK (audiocd_changed_cb), shell);
        }
	else
		rb_debug ("No AudioCD device is available!");
#endif
	
	/* register with CORBA */
	CORBA_exception_init (&ev);
	
	corba_object = bonobo_object_corba_objref (BONOBO_OBJECT (shell));
	if (bonobo_activation_active_server_register (RB_SHELL_OAFIID, corba_object) != Bonobo_ACTIVATION_REG_SUCCESS)
		registration_failed = TRUE;

	if (bonobo_activation_active_server_register (RB_FACTORY_OAFIID, corba_object) != Bonobo_ACTIVATION_REG_SUCCESS)
		registration_failed = TRUE;

	if (registration_failed) {
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

	/* now that the lib is loaded, we can load the music groups */
	rb_debug ("shell: loading playlists");
	rb_shell_load_playlists (shell);

	/* GO GO GO! */
	rb_debug ("shell: syncing window state");
	rb_shell_sync_paned (shell);
	gtk_widget_show_all (GTK_WIDGET (shell->priv->tray_icon));

	/* Stop here if this is the first time. */
	if (!eel_gconf_get_boolean(CONF_FIRST_TIME)) {
		RBDruid *druid = rb_druid_new (shell->priv->library);
		gtk_widget_hide (GTK_WIDGET (shell->priv->window));
		rb_druid_show (druid);
		g_object_unref (G_OBJECT (druid));
		rb_shell_sync_window_visibility (shell);
		
	}
	rb_shell_sync_window_visibility (shell);
	
	g_idle_add ((GSourceFunc) async_library_release_brakes, shell);
}

char *
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

static gboolean
rb_shell_window_state_cb (GtkWidget *widget,
			  GdkEvent *event,
			  RBShell *shell)
{
	g_return_val_if_fail (widget != NULL, FALSE);
	rb_debug ("caught window state change");

	switch (event->type)
	{
	case GDK_WINDOW_STATE:
		eel_gconf_set_boolean (CONF_STATE_WINDOW_MAXIMIZED,
				       event->window_state.new_window_state &
				       GDK_WINDOW_STATE_MAXIMIZED);
		break;
	case GDK_CONFIGURE:
		if (!eel_gconf_get_boolean (CONF_STATE_WINDOW_MAXIMIZED)) {
			eel_gconf_set_integer (CONF_STATE_WINDOW_WIDTH, event->configure.width);
			eel_gconf_set_integer (CONF_STATE_WINDOW_HEIGHT, event->configure.height);
		}
		break;
	default:
		break;
	}

	return FALSE;
}

static void
rb_shell_sync_window_state (RBShell *shell)
{
	int width = eel_gconf_get_integer (CONF_STATE_WINDOW_WIDTH); 
	int height = eel_gconf_get_integer (CONF_STATE_WINDOW_HEIGHT);
	gboolean maximized = eel_gconf_get_boolean (CONF_STATE_WINDOW_MAXIMIZED);

	gtk_window_set_default_size (GTK_WINDOW (shell->priv->window),
				     width, height);

	if (maximized)
		gtk_window_maximize (GTK_WINDOW (shell->priv->window));
	else
		gtk_window_unmaximize (GTK_WINDOW (shell->priv->window));
}

static gboolean
rb_shell_window_delete_cb (GtkWidget *win,
			   GdkEventAny *event,
			   RBShell *shell)
{
	rb_debug ("window deleted");
	rb_shell_quit (shell);

	return TRUE;
};

static void
source_selected_cb (RBSourceList *sourcelist,
		    RBSource *source,
		    RBShell *shell)
{
	rb_debug ("source selected");
	rb_shell_select_source (shell, source);
}

static void
rb_shell_start_operation (RBShell *shell)
{
	GdkCursor *cursor;
	rb_debug ("starting operation");

	cursor = gdk_cursor_new (GDK_WATCH);
	gdk_window_set_cursor (shell->priv->window->window, cursor);
	gdk_cursor_unref (cursor);
	shell->priv->operation_count++;
}

static void
rb_shell_library_operation_end_cb (RBLibrary *library, RBShell *shell)
{

	shell->priv->operation_count--;

	rb_debug ("operation ended");
	if (shell->priv->operation_count <= 0) {
		rb_debug ("nulling cursor");
		gdk_window_set_cursor (shell->priv->window->window, NULL);
		gdk_flush ();
	}
}

static void
rb_shell_library_error_cb (RBLibrary *library,
			   const char *uri, const char *msg,
			   RBShell *shell)
{
	GList *tem;
	GnomeVFSURI *vfsuri;
	gchar *basename;
	gssize baselen;

	rb_debug ("got library error, showing: %s",
		  shell->priv->show_library_errors ? "TRUE" : "FALSE");
	
	if (!shell->priv->show_library_errors)
		return;

	vfsuri = gnome_vfs_uri_new (uri);
	basename = gnome_vfs_uri_extract_short_name (vfsuri);
	baselen = strlen (basename);
	
	for (tem = shell->priv->supported_media_extensions; tem != NULL; tem = g_list_next (tem)) {
		gssize extlen = strlen (tem->data);
		if (extlen >= baselen)
			continue;
		if (!strncmp (basename + (baselen - extlen), tem->data, extlen)) {
			rb_debug ("\"%s\" matches \"%s\"", basename, tem->data);
			rb_load_failure_dialog_add (RB_LOAD_FAILURE_DIALOG (shell->priv->load_error_dialog),
						    uri, msg);
			gtk_widget_show (GTK_WIDGET (shell->priv->load_error_dialog));
			goto out;
		}
	}
	rb_debug ("\"%s\" has unknown extension, ignoring", basename);
out:
	g_free (basename);
	gnome_vfs_uri_unref (vfsuri);
}

static void
rb_shell_load_failure_dialog_response_cb (GtkDialog *dialog,
					  int response_id,
					  RBShell *shell)
{
	rb_debug ("got response");
	shell->priv->show_library_errors = FALSE;
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

static void
rb_shell_remove_source (RBShell *shell,
			RBSource *source)
{
	if (source == rb_shell_player_get_source (shell->priv->player_shell)) {
		rb_shell_player_set_playing_source (shell->priv->player_shell, NULL);
	}
	if (source == shell->priv->selected_source) {
		rb_shell_select_source (shell, shell->priv->library_source);
	}

	shell->priv->sources = g_list_remove (shell->priv->sources, source);

	if (g_list_find (shell->priv->groups, source) != NULL) {
		shell->priv->groups = g_list_remove (shell->priv->groups, source);
		rb_group_source_delete (RB_GROUP_SOURCE (source));
	}

	rb_sourcelist_remove (RB_SOURCELIST (shell->priv->sourcelist), source);

	gtk_notebook_remove_page (GTK_NOTEBOOK (shell->priv->notebook),
				  gtk_notebook_page_num (GTK_NOTEBOOK (shell->priv->notebook),
							 GTK_WIDGET (source)));
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
	rb_shell_clipboard_set_source (shell->priv->clipboard_shell,
				       RB_SOURCE (source));
	rb_shell_player_set_source (shell->priv->player_shell,
				    RB_SOURCE (source));
	rb_source_header_set_source (shell->priv->source_header,
				     RB_SOURCE (source));
	rb_statusbar_set_source (shell->priv->statusbar,
				 RB_SOURCE (source));
	rb_bonobo_set_sensitive (shell->priv->ui_component, CMD_PATH_PLAYLIST_DELETE,
				 g_list_find (shell->priv->groups,
					      shell->priv->selected_source) != NULL);
	rb_bonobo_set_sensitive (shell->priv->ui_component, CMD_PATH_PLAYLIST_SAVE,
				 g_list_find (shell->priv->groups,
					      shell->priv->selected_source) != NULL);
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
	if (window_title == NULL) {
		rb_debug ("clearing title");
		gtk_window_set_title (GTK_WINDOW (shell->priv->window),
				      _("Music Player"));

		gtk_tooltips_set_tip (shell->priv->tray_icon_tooltip,
				      GTK_WIDGET (shell->priv->tray_icon),
				      _("Not playing"),
				      NULL);
	}
	else {
		gboolean playing = rb_shell_player_get_playing (shell->priv->player_shell);
		char *title;

		if (!strcmp (gtk_window_get_title (GTK_WINDOW (shell->priv->window)),
			     window_title))
		    return;

		rb_debug ("setting title to \"%s\"", window_title);
		if (!playing) {
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
		} else {
			title = g_strdup (window_title);
			gtk_window_set_title (GTK_WINDOW (shell->priv->window),
					      window_title);
#if 0			
			egg_tray_icon_send_message (EGG_TRAY_ICON (shell->priv->tray_icon),
						    3000, window_title, -1);
#endif

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
	const char **tem;
	char *comment;
	static GtkWidget *about = NULL;
	GdkPixbuf *pixbuf = NULL;

	const char *authors[] = {
		"",
#include "MAINTAINERS.tab"
		"",
		NULL,
#include "AUTHORS.tab"
		NULL
	};

	const char *documenters[] = {
#include "DOCUMENTERS.tab"
		NULL
	};

	const char *translator_credits = _("translator_credits");

	if (about != NULL) {
		gtk_window_present (GTK_WINDOW (about));
		return;
	}

	pixbuf = gdk_pixbuf_new_from_file (rb_file ("about-logo.png"), NULL);

	authors[0] = _("Maintainers:");
	tem = authors;
	while (1) {
		if (*tem == NULL) {
			*tem = _("Contributors:");
			break;
		}
		tem++;
	}

	{
		const char *backend;
		GString *formats = g_string_new ("");
#ifdef HAVE_GSTREAMER
		backend = "GStreamer";
#else
		backend = "Xine";
#endif		
#ifdef HAVE_MP3
		g_string_append (formats, "mp3 ");
#endif
#ifdef HAVE_VORBIS
		g_string_append (formats, "vorbis ");
#endif
#ifdef HAVE_FLAC
		g_string_append (formats, "FLAC ");
#endif
		
		comment = g_strdup_printf (_("Music management and playback software for GNOME.\nAudio backend: %s\nAudio formats: %s\n"), backend, formats->str);

		g_string_free (formats, TRUE);
	}
	about = gnome_about_new ("Rhythmbox", VERSION,
				 "Copyright \xc2\xa9 2002, 2003 Jorn Baayen, Colin Walters",
				 comment,
				 (const char **) authors,
				 (const char **) documenters,
				 strcmp (translator_credits, "translator_credits") != 0 ? translator_credits : NULL,
				 pixbuf);
	g_free (comment);
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

	if (error != NULL) {
		g_warning (error->message);

		g_error_free (error);
	}
}

static void
rb_shell_cmd_preferences (BonoboUIComponent *component,
		          RBShell *shell,
		          const char *verbname)
{
	if (shell->priv->prefs == NULL) {
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

	if (response_id != GTK_RESPONSE_OK) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	files = gtk_file_selection_get_selections (GTK_FILE_SELECTION (dialog));

	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (files == NULL)
		return;

	filecur = files;

	if (*filecur != NULL) {
		char *tmp;

		stored = g_path_get_dirname (*filecur);
		tmp = g_strconcat (stored, "/", NULL);
		eel_gconf_set_string (CONF_STATE_ADD_DIR, tmp);
		g_free (tmp);
		g_free (stored);
	}

	shell->priv->show_library_errors = TRUE;
    
	while (*filecur != NULL) {
		if (g_utf8_validate (*filecur, -1, NULL)) {
			char *uri = gnome_vfs_get_uri_from_local_path (*filecur);
			rb_library_add_uri (shell->priv->library, uri);
			g_free (uri);
		}
		filecur++;
	}

	g_strfreev (files);
}

static void
load_playlist_response_cb (GtkDialog *dialog,
			   int response_id,
			   RBShell *shell)
{
	char *file;
	GList *tem;

	if (response_id != GTK_RESPONSE_OK) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	file = g_strdup (gtk_file_selection_get_filename (GTK_FILE_SELECTION (dialog)));

	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (file == NULL)
		return;

	rb_debug ("loading playlist from %s", file);

	shell->priv->show_library_errors = TRUE;

	tem = g_list_append (NULL, file);
	create_group (shell, CREATE_GROUP_WITH_FILE, tem);

	shell->priv->show_library_errors = FALSE;
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
	shell->priv->show_library_errors = TRUE;

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

static void
save_playlist_response_cb (GtkDialog *dialog,
			   int response_id,
			   RBShell *shell)
{
	char *file;

	if (response_id != GTK_RESPONSE_OK) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	file = g_strdup (gtk_file_selection_get_filename (GTK_FILE_SELECTION (dialog)));

	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (file == NULL)
		return;

	rb_group_source_save_playlist (RB_GROUP_SOURCE (shell->priv->selected_source), file);
	g_free (file);
}

static void
rb_shell_cmd_save_playlist (BonoboUIComponent *component,
			    RBShell *shell,
			    const char *verbname)
{
	GtkWidget *dialog;
    
	dialog = rb_ask_file_multiple (_("Save playlist"),
				      NULL,
			              GTK_WINDOW (shell->priv->window));

	g_signal_connect (G_OBJECT (dialog),
			  "response",
			  G_CALLBACK (save_playlist_response_cb),
			  shell);
}

static RBGroupSource *
create_group_with_name (RBShell *shell, const char *name)
{
	RBGroupSource *ret;
	GList *tem;
	char *temname;

	for (tem = shell->priv->groups; tem; tem = g_list_next (tem)) {
		g_object_get (G_OBJECT (tem->data), "name", &temname, NULL);
		if (!strcmp (temname, name)) {
			rb_error_dialog (_("There is already a playlist with that name."));
			return NULL;
		}
	}
	ret = RB_GROUP_SOURCE (rb_group_source_new (shell->priv->container,
						    shell->priv->library,
						    RB_LIBRARY_SOURCE (shell->priv->library_source)));
	rb_group_source_set_name (RB_GROUP_SOURCE (ret), name);
	return ret;
}

static void
add_uri_to_group (RBShell *shell, RBGroupSource *group, const char *uri, const char *title)
{
	RBNode *node;
	GError *error = NULL;
	GnomeVFSURI *vfsuri = gnome_vfs_uri_new (uri);
	const char *scheme = gnome_vfs_uri_get_scheme (vfsuri);

	if (strncmp ("http", scheme, 4) == 0) {
		rb_iradio_backend_add_station_full (shell->priv->iradio_backend, uri, title, NULL);
		goto out;
	}

	rb_library_add_uri_sync (shell->priv->library, uri, &error);
	if (error) {
		rb_debug ("error loading URI %s", uri);
		goto out; /* FIXME */
	}

	node = rb_library_get_song_by_location (shell->priv->library, uri);

	g_return_if_fail (node != NULL);

	/* add this node to the newly created group */
	rb_group_source_add_node (group, node);
out:
	gnome_vfs_uri_unref (vfsuri);
}

static void
handle_playlist_entry_into_group_cb (RBPlaylist *playlist, const char *uri, const char *title,
				     const char *genre, RBShell *shell)
{
	add_uri_to_group (shell, RB_GROUP_SOURCE (shell->priv->loading_group), uri, title);
}

static void
ask_string_response_cb (GtkDialog *dialog,
			int response_id,
			RBShell *shell)
{
	GtkWidget *entry, *checkbox;
	RBSource *group;
	char *name;
	gboolean add_selection;
	CreateGroupType type;
	GList *data, *l;

	type = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (dialog), "type"));
	data = g_object_get_data (G_OBJECT (dialog), "data");

	if (response_id != GTK_RESPONSE_OK) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		if (type == CREATE_GROUP_WITH_URI_LIST)
			gnome_vfs_uri_list_free (data);
		return;
	}

	entry = g_object_get_data (G_OBJECT (dialog), "entry");
	name = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));

	checkbox = g_object_get_data (G_OBJECT (dialog), "checkbox");
	add_selection = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbox));

	gtk_widget_destroy (GTK_WIDGET (dialog));
	
	if (name == NULL) {
		if (type == CREATE_GROUP_WITH_URI_LIST)
			gnome_vfs_uri_list_free (data);
		return;
	}

	group = RB_SOURCE (create_group_with_name (shell, name));
	if (group == NULL)
		return;

	shell->priv->groups = g_list_append (shell->priv->groups, group);
	rb_shell_append_source (shell, group);
	g_free (name);

	switch (type)
	{
	case CREATE_GROUP_WITH_NODE_LIST:
		for (l = data; l != NULL; l = g_list_next (l))
			rb_group_source_add_node (RB_GROUP_SOURCE (group),
						  l->data);
		break;
	case CREATE_GROUP_WITH_FILE:
	{
		RBPlaylist *parser = rb_playlist_new ();
		g_signal_connect (G_OBJECT (parser), "entry",
				  G_CALLBACK (handle_playlist_entry_into_group_cb),
				  shell);
		
		shell->priv->loading_group = RB_GROUP_SOURCE (group);
		if (!rb_playlist_parse (parser, g_list_first (data)->data))
			rb_error_dialog (_("Couldn't parse playlist"));
		shell->priv->loading_group = NULL;
		g_free (g_list_first (data)->data);
		g_list_free (data);
		g_object_unref (G_OBJECT (parser));
	}
	break;
	case CREATE_GROUP_WITH_URI_LIST:
		for (l = data; l != NULL; l = g_list_next (l)) {
			char *uri;
			uri = gnome_vfs_uri_to_string ((GnomeVFSURI *) l->data, GNOME_VFS_URI_HIDE_NONE);
			add_uri_to_group (shell, RB_GROUP_SOURCE (group), uri, NULL);
			g_free (uri);
		}
		gnome_vfs_uri_list_free (data);
		break;
	case CREATE_GROUP_WITH_SELECTION:
		/* add the current selection if the user checked */
		if (add_selection) {
			RBNodeView *nodeview = rb_source_get_node_view (shell->priv->selected_source);
			GList *i = NULL;
			GList *selection = rb_node_view_get_selection (nodeview);
			for (i  = selection; i != NULL; i = g_list_next (i))
				rb_group_source_add_node (RB_GROUP_SOURCE (group), i->data);
		}
	break;
	}
}

static void
create_group (RBShell *shell, CreateGroupType type, GList *data)
{
	GtkWidget *dialog;
	
	dialog = rb_shell_new_group_dialog (shell);

	g_object_set_data (G_OBJECT (dialog), "type", GINT_TO_POINTER (type));
	g_object_set_data (G_OBJECT (dialog), "data", data);

	g_signal_connect (G_OBJECT (dialog),
			  "response",
			  G_CALLBACK (ask_string_response_cb),
			  shell);
}

static void
rb_shell_cmd_new_playlist (BonoboUIComponent *component,
			RBShell *shell,
			const char *verbname)
{
	create_group (shell, CREATE_GROUP_WITH_SELECTION, NULL);
}

static void
rb_shell_cmd_rename_playlist (BonoboUIComponent *component,
			      RBShell *shell,
			      const char *verbname)
{
	rb_debug ("FIXME");
}

static void
rb_shell_cmd_delete_playlist (BonoboUIComponent *component,
			   RBShell *shell,
			   const char *verbname)
{
	rb_debug ("Deleting source %p", shell->priv->selected_source);
	
	rb_shell_remove_source (shell, shell->priv->selected_source);
}


static void
rb_shell_cmd_new_station (BonoboUIComponent *component,
			  RBShell *shell,
			  const char *verbname)
{
	GtkWidget *dialog;
	rb_debug ("Got new station command");
	dialog = rb_new_station_dialog_new (shell->priv->iradio_backend);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

static void
rb_shell_cmd_extract_cd (BonoboUIComponent *component,
		    RBShell *shell,
		    const char *verbname)
{
	GError *err = NULL;
	g_spawn_command_line_async ("sound-juicer", &err);
	g_clear_error(&err);
}

static void
rb_shell_quit (RBShell *shell)
{
	rb_debug ("Quitting");

	bonobo_object_unref (BONOBO_OBJECT (shell));
}

static void
rb_shell_load_playlists (RBShell *shell)
{
	char *path;
	GnomeVFSDirectoryHandle *handle;
	GnomeVFSResult result;
	GnomeVFSFileInfo *info;

	path = g_build_filename (rb_dot_dir (), "groups", NULL);

	if ((result = gnome_vfs_directory_open (&handle, path, GNOME_VFS_FILE_INFO_FOLLOW_LINKS))
	    != GNOME_VFS_OK)
		goto out;

	info = gnome_vfs_file_info_new ();
	while ((result = gnome_vfs_directory_read_next (handle, info)) == GNOME_VFS_OK) {
		RBSource *group;
		char *filepath;

		if (info->name[0] == '.')
			continue;

		filepath = g_build_filename (path, info->name, NULL);

		group = rb_group_source_new_from_file (shell->priv->container,
						       shell->priv->library,
						       RB_LIBRARY_SOURCE (shell->priv->library_source),
						       filepath);
		if (group != NULL) {
			shell->priv->groups = g_list_append (shell->priv->groups, group);
			
			rb_shell_append_source (shell, group);
		}
		g_free (filepath);
	}

	gnome_vfs_file_info_unref (info);
out:
	g_free (path);
}

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

static void
rb_shell_sync_window_visibility (RBShell *shell)
{
	gboolean visible;
	static int window_x = -1;
	static int window_y = -1;

	rb_debug ("syncing visibility");
	visible = !eel_gconf_get_boolean (CONF_STATE_WINDOW_HIDDEN);
	
	if (visible == TRUE) {
		if (window_x >= 0 && window_y >= 0) {
			gtk_window_move (GTK_WINDOW (shell->priv->window), window_x,
					 window_y);
		}
		gtk_widget_show (shell->priv->window);
	} else {
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
	rb_debug ("window visiblity changed");
	rb_shell_sync_window_visibility (shell);
}

static void
rb_shell_sync_paned (RBShell *shell)
{
	int position = eel_gconf_get_integer (CONF_STATE_PANED_POSITION);
	rb_debug ("syncing paned to %d", position);
	if (position > 0)
		gtk_paned_set_position (GTK_PANED (shell->priv->paned),
					position);
}

static void
paned_size_allocate_cb (GtkWidget *widget,
			GtkAllocation *allocation,
		        RBShell *shell)
{
	rb_debug ("paned size allocate");
	eel_gconf_set_integer (CONF_STATE_PANED_POSITION,
			       gtk_paned_get_position (GTK_PANED (shell->priv->paned)));
}

static void
paned_changed_cb (GConfClient *client,
		  guint cnxn_id,
		  GConfEntry *entry,
		  RBShell *shell)
{
	rb_debug ("paned changed");
	rb_shell_sync_paned (shell);
}


static void
handle_songs_func (RBNode *node,
		   RBGroupSource *group)
{
	rb_group_source_add_node (group, node);
}

static void
sourcelist_drag_received_cb (RBSourceList *sourcelist,
			     RBSource *source,
			     GtkSelectionData *data,
			     RBShell *shell)
{
	if (source != NULL) {
		rb_source_receive_drag (source, data);
		return;
	}

	if (data->type == gdk_atom_intern (RB_LIBRARY_DND_NODE_ID_TYPE, TRUE)) {
		long id;
		RBNode *node;
		RBGroupSource *group;


		id = atol (data->data);
		rb_debug ("got node id %d", id);
		node = rb_node_db_get_node_from_id (rb_library_get_node_db (shell->priv->library), id);

		if (node == NULL)
			return;
			
		
		group = create_group_with_name (shell, rb_node_get_property_string (node, RB_NODE_PROP_NAME));
		if (group == NULL)
			return;

		rb_library_handle_songs (shell->priv->library,
					 node,
					 (GFunc) handle_songs_func,
					 group);

		shell->priv->groups = g_list_append (shell->priv->groups, group);
		rb_shell_append_source (shell, RB_SOURCE (group));
	} else {
		GList *list;

		rb_debug ("got vfs data, len: %d", data->length);
		list = gnome_vfs_uri_list_parse (data->data);
		create_group (shell, CREATE_GROUP_WITH_URI_LIST, list);
	}
}

static void
rb_shell_cmd_current_song (BonoboUIComponent *component,
			   RBShell *shell,
			   const char *verbname)
{
	rb_debug ("current song");

	rb_shell_jump_to_current (shell);
}

static void
rb_shell_jump_to_current (RBShell *shell)
{
	RBSource *source = rb_shell_player_get_source (shell->priv->player_shell);

	g_return_if_fail (source != NULL);

	rb_source_header_clear_search (shell->priv->source_header);
	rb_shell_select_source (shell, source);
	rb_source_search (shell->priv->selected_source, NULL);
	rb_shell_player_jump_to_current (shell->priv->player_shell);
}

/* rb_shell_new_group_dialog: create a dialog for creating a new
 * group.
 *
 * TODO Make this a gobject that could hold more functionality
 * like multi criteria search.
 */
GtkWidget *
rb_shell_new_group_dialog (RBShell *shell)
{
	RBNodeView *nodeview;
	GtkWidget *dialog, *hbox, *image, *entry, *label, *vbox, *cbox, *align;
	GList *selection;
	
	dialog = gtk_dialog_new_with_buttons ("",
					      NULL,
					      0,
					      GTK_STOCK_CANCEL,
					      GTK_RESPONSE_CANCEL,
					      _("C_reate"),
					      GTK_RESPONSE_OK,
					      NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);
	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);

	gtk_window_set_transient_for (GTK_WINDOW (dialog), 
				      GTK_WINDOW (shell->priv->window));
	gtk_window_set_modal (GTK_WINDOW (dialog), FALSE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	gtk_window_set_title (GTK_WINDOW (dialog), _("New Playlist"));

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);
	image = gtk_image_new_from_stock (RB_STOCK_GROUP,
					  GTK_ICON_SIZE_DIALOG);
	align = gtk_alignment_new (0.5, 0.0, 0.0, 0.0);
	gtk_container_add (GTK_CONTAINER (align), image);
	gtk_box_pack_start (GTK_BOX (hbox), align, TRUE, TRUE, 0);
	vbox = gtk_vbox_new (FALSE, 6);

	label = gtk_label_new (_("Please enter a name for the new playlist:"));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);

	entry = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (entry), _("Untitled"));
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
	gtk_box_pack_start (GTK_BOX (vbox), entry, FALSE, TRUE, 0);

	cbox = gtk_check_button_new_with_mnemonic (_("Add the _selected songs to the new playlist"));
	nodeview = rb_source_get_node_view (shell->priv->selected_source);
	selection = rb_node_view_get_selection (nodeview);
	if (selection == NULL)
		gtk_widget_set_sensitive (cbox, FALSE);
	gtk_box_pack_start (GTK_BOX (vbox), cbox, FALSE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);
	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), hbox);
	gtk_widget_show_all (hbox);
	gtk_widget_grab_focus (entry);

	/* we need this fields to be retrieved later */
	g_object_set_data (G_OBJECT (dialog), "entry", entry);
	g_object_set_data (G_OBJECT (dialog), "checkbox", cbox);

	gtk_widget_show_all (dialog);

	return dialog;
}

static gboolean
rb_shell_show_popup_cb (RBSourceList *sourcelist,
			RBSource *target,
			RBShell *shell)
{
	rb_debug ("popup");
	return rb_source_show_popup (target);
}


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

	if (list == NULL) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	uri_list = NULL;

	for (i = list; i != NULL; i = g_list_next (i))
		uri_list = g_list_append (uri_list, gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0));

	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	for (i = uri_list; i != NULL; i = i->next) {
		char *uri = i->data;

		if (uri != NULL)
			rb_library_add_uri (shell->priv->library, uri);

		g_free (uri);
	}

	g_list_free (uri_list);

	gtk_drag_finish (context, TRUE, FALSE, time);
}

static void
tray_deleted_cb (GtkWidget *win, GdkEventAny *event, RBShell *shell)
{
	rb_debug ("caught delete_event for tray icon");
	gtk_object_sink (GTK_OBJECT (shell->priv->tray_icon));
	
	setup_tray_icon (shell);
}

static void
setup_tray_icon (RBShell *shell)
{
	GtkWidget *ebox, *image;
	BonoboControlFrame *frame;

	rb_debug ("setting up tray icon");

	shell->priv->tray_icon_tooltip = gtk_tooltips_new ();

	shell->priv->tray_icon = egg_tray_icon_new ("Rhythmbox tray icon");

	g_signal_connect (G_OBJECT (shell->priv->tray_icon), "delete_event",
			  G_CALLBACK (tray_deleted_cb), shell);
	gtk_tooltips_set_tip (shell->priv->tray_icon_tooltip,
			      GTK_WIDGET (shell->priv->tray_icon),
			      _("Not playing"),
			      NULL);
	ebox = gtk_event_box_new ();
	g_signal_connect (G_OBJECT (ebox),
			  "button_press_event",
			  G_CALLBACK (tray_button_press_event_cb),
			  shell);
	gtk_drag_dest_set (ebox, GTK_DEST_DEFAULT_ALL, target_uri, 1, GDK_ACTION_COPY);
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
