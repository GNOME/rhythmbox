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
#include <libgnomevfs/gnome-vfs-uri.h>
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
#include "rb-group-view.h"
#include "rb-file-monitor.h"
#include "rb-library-dnd-types.h"
#include "rb-volume.h"
#include "rb-remote.h"
#include "rb-thread-helpers.h"
#include "eel-gconf-extensions.h"
#include "eggtrayicon.h"
#include "gul-toolbar.h"
#include "gul-toolbar-bonobo-view.h"
#include "gul-toolbar-editor.h"

static void rb_shell_class_init (RBShellClass *klass);
static void rb_shell_init (RBShell *shell);
static void rb_shell_finalize (GObject *object);
static void rb_shell_corba_quit (PortableServer_Servant _servant,
                                 CORBA_Environment *ev);
static void rb_shell_corba_add_to_library (PortableServer_Servant _servant,
					   const CORBA_char *uri,
					   CORBA_Environment *ev);
static void rb_shell_corba_grab_focus (PortableServer_Servant _servant,
				       CORBA_Environment *ev);
static gboolean rb_shell_window_state_cb (GtkWidget *widget,
					  GdkEvent *event,
					  RBShell *shell);
static gboolean rb_shell_window_delete_cb (GtkWidget *win,
			                   GdkEventAny *event,
			                   RBShell *shell);
static void rb_shell_window_load_state (RBShell *shell);
static void rb_shell_window_save_state (RBShell *shell);
static void rb_shell_select_view (RBShell *shell, RBView *view);
static void rb_shell_append_view (RBShell *shell, RBView *view);
static void rb_shell_remove_view (RBShell *shell, RBView *view);
static void rb_shell_sidebar_button_toggled_cb (GtkToggleButton *widget,
				                RBShell *shell);
static void rb_shell_view_deleted_cb (RBView *view,
				      RBShell *shell);
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
static void rb_shell_cmd_quit (BonoboUIComponent *component,
		               RBShell *shell,
			       const char *verbname);
static void rb_shell_cmd_preferences (BonoboUIComponent *component,
		                      RBShell *shell,
		                      const char *verbname);
static void rb_shell_cmd_edit_toolbar (BonoboUIComponent *component,
		           	       RBShell *shell,
		           	       const char *verbname);
static void rb_shell_cmd_add_to_library (BonoboUIComponent *component,
			                 RBShell *shell,
			                 const char *verbname);
static void rb_shell_cmd_new_group (BonoboUIComponent *component,
			            RBShell *shell,
			            const char *verbname);
static void rb_shell_cmd_dummy (BonoboUIComponent *component,
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
static void rb_shell_view_toolbar_changed_cb (BonoboUIComponent *component,
			                      const char *path,
			                      Bonobo_UIComponent_EventType type,
			                      const char *state,
			                      RBShell *shell);
static void rb_shell_view_statusbar_changed_cb (BonoboUIComponent *component,
			                        const char *path,
			                        Bonobo_UIComponent_EventType type,
			                        const char *state,
			                        RBShell *shell);
static void rb_shell_view_sidebar_changed_cb (BonoboUIComponent *component,
			                      const char *path,
			                      Bonobo_UIComponent_EventType type,
			                      const char *state,
			                      RBShell *shell);
static void rb_shell_show_window_changed_cb (BonoboUIComponent *component,
				             const char *path,
				             Bonobo_UIComponent_EventType type,
				             const char *state,
				             RBShell *shell);
static void rb_shell_load_music_groups (RBShell *shell);
static void rb_shell_save_music_groups (RBShell *shell);
static void rb_shell_sidebar_size_allocate_cb (GtkWidget *sidebar,
				               GtkAllocation *allocation,
				               RBShell *shell);
static void rb_shell_sync_toolbar_visibility (RBShell *shell);
static void rb_shell_sync_statusbar_visibility (RBShell *shell);
static void rb_shell_sync_sidebar_visibility (RBShell *shell);
static void rb_shell_sync_toolbar_style (RBShell *shell);
static void rb_shell_sync_window_visibility (RBShell *shell);
static void toolbar_visibility_changed_cb (GConfClient *client,
			                   guint cnxn_id,
			                   GConfEntry *entry,
			                   RBShell *shell);
static void statusbar_visibility_changed_cb (GConfClient *client,
				             guint cnxn_id,
				             GConfEntry *entry,
				             RBShell *shell);
static void sidebar_visibility_changed_cb (GConfClient *client,
			                   guint cnxn_id,
			                   GConfEntry *entry,
			                   RBShell *shell);
static void toolbar_style_changed_cb (GConfClient *client,
			              guint cnxn_id,
			              GConfEntry *entry,
			              RBShell *shell);
static void window_visibility_changed_cb (GConfClient *client,
			                  guint cnxn_id,
			                  GConfEntry *entry,
			                  RBShell *shell);
static void rb_sidebar_drag_finished_cb (RBSidebar *sidebar,
			                 GdkDragContext *context,
			                 int x, int y,
			                 GtkSelectionData *data,
			                 guint info,
			                 guint time,
			                 RBShell *shell);
static void dnd_add_handled_cb (RBLibraryAction *action,
		                RBGroupView *view);
GtkWidget *rb_shell_new_group_dialog (RBShell *shell);
static void setup_tray_icon (RBShell *shell);
static void sync_tray_menu (RBShell *shell);

#ifdef HAVE_REMOTE
static void rb_shell_remote_cb (RBRemote *remote, RBRemoteCommand cmd,
				RBShell *shell);
#endif

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

#define CMD_PATH_SHUFFLE        "/commands/Shuffle"
#define CMD_PATH_REPEAT         "/commands/Repeat"
#define CMD_PATH_VIEW_TOOLBAR   "/commands/ShowToolbar"
#define CMD_PATH_VIEW_STATUSBAR "/commands/ShowStatusbar"
#define CMD_PATH_VIEW_SIDEBAR   "/commands/ShowSidebar"
#define CMD_PATH_SHOW_WINDOW    "/commands/ShowWindow"

#define PATH_VOLUME "/Toolbar/Volume"

/* prefs */
#define CONF_STATE_WINDOW_WIDTH     "/apps/rhythmbox/state/window_width"
#define CONF_STATE_WINDOW_HEIGHT    "/apps/rhythmbox/state/window_height"
#define CONF_STATE_WINDOW_MAXIMIZED "/apps/rhythmbox/state/window_maximized"
#define CONF_STATE_WINDOW_VISIBLE   "/apps/rhythmbox/state/window_visible"
#define CONF_STATE_SHUFFLE          "/apps/rhythmbox/state/shuffle"
#define CONF_STATE_REPEAT           "/apps/rhythmbox/state/repeat"
#define CONF_STATE_PANED_POSITION   "/apps/rhythmbox/state/paned_position"
#define CONF_STATE_ADD_DIR          "/apps/rhythmbox/state/add_dir"
#define CONF_MUSIC_GROUPS           "/apps/rhythmbox/music_groups"
#define CONF_TOOLBAR_SETUP	    "/apps/rhythmbox/ui/toolbar_setup"

#define RB_SHELL_REMOTE_VOLUME_INTERVAL 0.4

#define DEFAULT_TOOLBAR_SETUP \
        "previous=std_toolitem(item=previous);" \
        "play=std_toolitem(item=play);" \
        "next=std_toolitem(item=next);" \
	"shuffle_separator=separator;" \
        "shuffle=std_toolitem(item=shuffle);"\
	"volume=volume;"

#define AVAILABLE_TOOLBAR_ITEMS \
	"previous=std_toolitem(item=previous);" \
        "play=std_toolitem(item=play);" \
        "next=std_toolitem(item=next);" \
	"shuffle_separator=separator;" \
        "shuffle=std_toolitem(item=shuffle);"\
	"volume=volume;"\
	"repeat=std_toolitem(item=repeat);"\
	"cut=std_toolitem(item=cut);"\
	"copy=std_toolitem(item=copy);"\
	"paste=std_toolitem(item=paste);"\
	"properties=std_toolitem(item=properties);"\
	"add_to_library=std_toolitem(item=add_to_library);"

	
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
	BonoboUIContainer *container;

	GtkWidget *paned;
	GtkWidget *sidebar;
	GtkWidget *notebook;

	GList *views;

	char *sidebar_layout_file;

	RBShellPlayer *player_shell;
	RBShellStatus *status_shell;
	RBShellClipboard *clipboard_shell;

	RBLibrary *library;

	RBView *selected_view;

	RBShellWindowState *state;

	GtkWidget *prefs;

	gboolean shuffle;
	gboolean repeat;

	GList *groups;

	GulToolbar *toolbar;
	GulTbEditor *tb_editor;
	EggTrayIcon *tray_icon;
	GtkTooltips *tray_icon_tooltip;
	BonoboControl *tray_icon_control;
	BonoboUIComponent *tray_icon_component;

	RBRemote *remote;
	RBVolume *volume;
};

static BonoboUIVerb rb_shell_verbs[] =
{
	BONOBO_UI_VERB ("About",        (BonoboUIVerbFn) rb_shell_cmd_about),
	BONOBO_UI_VERB ("Contents",	(BonoboUIVerbFn) rb_shell_cmd_contents),
	BONOBO_UI_VERB ("Quit",         (BonoboUIVerbFn) rb_shell_cmd_quit),
	BONOBO_UI_VERB ("Preferences",  (BonoboUIVerbFn) rb_shell_cmd_preferences),
	BONOBO_UI_VERB ("AddToLibrary", (BonoboUIVerbFn) rb_shell_cmd_add_to_library),
	BONOBO_UI_VERB ("NewGroup",     (BonoboUIVerbFn) rb_shell_cmd_new_group),
	BONOBO_UI_VERB ("Shuffle",      (BonoboUIVerbFn) rb_shell_cmd_dummy),
	BONOBO_UI_VERB ("Repeat",       (BonoboUIVerbFn) rb_shell_cmd_dummy),
	BONOBO_UI_VERB ("EditToolbar",  (BonoboUIVerbFn) rb_shell_cmd_edit_toolbar),
	BONOBO_UI_VERB_END
};

static RBBonoboUIListener rb_shell_listeners[] =
{
	RB_BONOBO_UI_LISTENER ("Shuffle",       (BonoboUIListenerFn) rb_shell_shuffle_changed_cb),
	RB_BONOBO_UI_LISTENER ("Repeat",        (BonoboUIListenerFn) rb_shell_repeat_changed_cb),
	RB_BONOBO_UI_LISTENER ("ShowToolbar",   (BonoboUIListenerFn) rb_shell_view_toolbar_changed_cb),
	RB_BONOBO_UI_LISTENER ("ShowStatusbar", (BonoboUIListenerFn) rb_shell_view_statusbar_changed_cb),
	RB_BONOBO_UI_LISTENER ("ShowSidebar",   (BonoboUIListenerFn) rb_shell_view_sidebar_changed_cb),
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

	epv->quit         = rb_shell_corba_quit;
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

	shell->priv->sidebar_layout_file = g_build_filename (rb_dot_dir (),
							     "sidebar_layout.xml",
							     NULL);

	file = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_APP_PIXMAP, "rhythmbox.png", TRUE, NULL);
	gnome_window_icon_set_default_from_file (file);
	g_free (file);
	
	shell->priv->state = g_new0 (RBShellWindowState, 1);

	eel_gconf_monitor_add ("/apps/rhythmbox");

#ifdef HAVE_REMOTE
	shell->priv->remote = rb_remote_new ();
	g_signal_connect (shell->priv->remote, "button_pressed",
			  G_CALLBACK (rb_shell_remote_cb), shell);
#else
	shell->priv->remote = NULL;
#endif

	shell->priv->tb_editor = NULL;
}

static void
rb_shell_finalize (GObject *object)
{
        RBShell *shell = RB_SHELL (object);

	gtk_widget_hide (shell->priv->window);
	gtk_widget_hide (GTK_WIDGET (shell->priv->tray_icon));
	rb_shell_player_stop (shell->priv->player_shell);

	while (gtk_events_pending ())
		gtk_main_iteration ();

	eel_gconf_monitor_remove ("/apps/rhythmbox");

	bonobo_activation_active_server_unregister (RB_SHELL_OAFIID, bonobo_object_corba_objref (BONOBO_OBJECT (shell)));

	rb_debug ("Unregistered with Bonobo Activation");
	
	rb_shell_save_music_groups (shell);

	rb_sidebar_save_layout (RB_SIDEBAR (shell->priv->sidebar),
				shell->priv->sidebar_layout_file);

	gtk_widget_destroy (shell->priv->window);
	gtk_widget_destroy (GTK_WIDGET (shell->priv->tray_icon));
	
	g_list_free (shell->priv->views);

	g_list_free (shell->priv->groups);

	g_free (shell->priv->sidebar_layout_file);

	g_object_unref (G_OBJECT (shell->priv->clipboard_shell));
	/* hack to make the gdk thread lock available for freeing
	 * the library.. evil */
	g_object_unref (G_OBJECT (shell->priv->library));

	if (shell->priv->remote != NULL)
		g_object_unref (G_OBJECT (shell->priv->remote));

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
	gboolean visible;

	visible = eel_gconf_get_boolean (CONF_STATE_WINDOW_VISIBLE);
	if (visible)
	{
		gtk_window_present (GTK_WINDOW (shell->priv->window));
		gtk_widget_grab_focus (shell->priv->window);
	}
	else
		eel_gconf_set_boolean (CONF_STATE_WINDOW_VISIBLE, TRUE);
}

static RBVolume *
setup_volume_control (RBShell *shell)
{
	RBVolume *volume;
	GulTbItem *it;
	
	volume = rb_volume_new (rb_shell_player_get_mixer (shell->priv->player_shell));
	gtk_widget_show_all (GTK_WIDGET (volume));
	it = gul_toolbar_get_item_by_id (shell->priv->toolbar, "volume");
        if (it != NULL)
        {
		GtkWidget *box;
        	box = gul_tb_item_get_widget (it);
		gtk_container_add (GTK_CONTAINER (box), 
				   GTK_WIDGET (volume));
	}

	return volume;
}

static void
rb_shell_toolbar_changed_cb (GulToolbar *gt, RBShell *shell)
{
        if (shell->priv->toolbar != NULL)
        {
                shell->priv->volume = setup_volume_control (shell);
        }
}

void
rb_shell_construct (RBShell *shell)
{
	CORBA_Object corba_object;
	CORBA_Environment ev;
	BonoboWindow *win;
	Bonobo_UIContainer corba_container;
	GtkWidget *vbox;
	RBView *library_view;
	GulToolbar *toolbar;
	GulTbBonoboView *bview;
	
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

	shell->priv->container = bonobo_window_get_ui_container (win);

	bonobo_ui_engine_config_set_path (bonobo_window_get_ui_engine (win),
					  "/apps/rhythmbox/UIConfig/kvps");

	corba_container = BONOBO_OBJREF (shell->priv->container);

	shell->priv->ui_component = bonobo_ui_component_new_default ();

	bonobo_ui_component_set_container (shell->priv->ui_component,
					   corba_container,
					   NULL);

	bonobo_ui_component_freeze (shell->priv->ui_component, NULL);
	
	bonobo_ui_util_set_ui (shell->priv->ui_component,
			       DATADIR,
			       "rhythmbox-ui.xml",
			       "rhythmbox", NULL);

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
	shell->priv->player_shell = rb_shell_player_new (shell->priv->ui_component,
							 shell->priv->tray_icon_component);
	g_signal_connect (G_OBJECT (shell->priv->player_shell),
			  "window_title_changed",
			  G_CALLBACK (rb_shell_player_window_title_changed_cb),
			  shell);
	shell->priv->status_shell = rb_shell_status_new (bonobo_window_get_ui_engine (win));
	shell->priv->clipboard_shell = rb_shell_clipboard_new (shell->priv->ui_component);

	shell->priv->paned = gtk_hpaned_new ();
	
	shell->priv->sidebar = rb_sidebar_new ();
	rb_sidebar_add_dnd_targets (RB_SIDEBAR (shell->priv->sidebar),
				    target_table,
				    G_N_ELEMENTS (target_table));
	g_signal_connect (G_OBJECT (shell->priv->sidebar),
			  "drag_finished",
			  G_CALLBACK (rb_sidebar_drag_finished_cb),
			  shell);
	
	vbox = gtk_vbox_new (FALSE, 5);
	
	shell->priv->notebook = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (shell->priv->notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (shell->priv->notebook), FALSE);

	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (shell->priv->player_shell), FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), shell->priv->notebook, TRUE, TRUE, 0);
	
	gtk_paned_pack1 (GTK_PANED (shell->priv->paned), shell->priv->sidebar, FALSE, FALSE);
	gtk_paned_pack2 (GTK_PANED (shell->priv->paned), vbox, FALSE, FALSE);
	g_signal_connect (G_OBJECT (shell->priv->sidebar),
			  "size_allocate",
			  G_CALLBACK (rb_shell_sidebar_size_allocate_cb),
			  shell);
	gtk_paned_set_position (GTK_PANED (shell->priv->paned),
				eel_gconf_get_integer (CONF_STATE_PANED_POSITION));

	vbox = gtk_vbox_new (FALSE, 5);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
	gtk_box_pack_start (GTK_BOX (vbox), shell->priv->paned, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (shell->priv->status_shell), FALSE, TRUE, 0);

	bonobo_window_set_contents (win, vbox);

	gtk_widget_show_all (vbox);

	/* Toolbar */
	toolbar = gul_toolbar_new ();
	shell->priv->toolbar = toolbar;
	g_signal_connect (toolbar, "changed", G_CALLBACK (rb_shell_toolbar_changed_cb), shell);
	if (gul_toolbar_listen_to_gconf (toolbar, CONF_TOOLBAR_SETUP) == FALSE)
        {
                /* FIXME: make this a dialog? */
                g_warning ("An incorrect toolbar configuration has been found,"
			    "resetting to the default");

                /* this is to make sure we get a toolbar, even if the
                   setup is wrong or there is no schema */
                eel_gconf_set_string (CONF_TOOLBAR_SETUP, DEFAULT_TOOLBAR_SETUP);
        }
	bview = gul_tb_bonobo_view_new ();
	gul_tb_bonobo_view_set_path (bview, shell->priv->ui_component, "/Toolbar");
	gul_tb_bonobo_view_set_toolbar (bview, toolbar);

	/* sync state */
	eel_gconf_notification_add (CONF_UI_TOOLBAR_VISIBLE,
				    (GConfClientNotifyFunc) toolbar_visibility_changed_cb,
				    shell);
	eel_gconf_notification_add (CONF_UI_STATUSBAR_VISIBLE,
				    (GConfClientNotifyFunc) statusbar_visibility_changed_cb,
				    shell);
	eel_gconf_notification_add (CONF_UI_SIDEBAR_VISIBLE,
				    (GConfClientNotifyFunc) sidebar_visibility_changed_cb,
				    shell);
	eel_gconf_notification_add (CONF_UI_TOOLBAR_STYLE,
				    (GConfClientNotifyFunc) toolbar_style_changed_cb,
				    shell);
	eel_gconf_notification_add (CONF_STATE_WINDOW_VISIBLE,
				    (GConfClientNotifyFunc) window_visibility_changed_cb,
				    shell);

	rb_shell_sync_toolbar_visibility (shell);
	rb_shell_sync_statusbar_visibility (shell);
	rb_shell_sync_sidebar_visibility (shell);
	rb_shell_sync_toolbar_style (shell);

	shell->priv->library = rb_library_new ();

	/* initialize views */
	library_view = rb_library_view_new (shell->priv->container,
				            shell->priv->library);
	rb_shell_append_view (shell, library_view);
	rb_shell_select_view (shell, library_view); /* select this one by default */

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

	/* load library */
	rb_library_release_brakes (shell->priv->library);
	
	/* now that the lib is loaded, we can load the music groups */
	rb_shell_load_music_groups (shell);

	/* GO GO GO! */
	rb_shell_sync_window_visibility (shell);
	gtk_widget_show_all (GTK_WIDGET (shell->priv->tray_icon));

	GDK_THREADS_ENTER ();

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
	
	g_signal_connect (G_OBJECT (view),
			  "deleted",
			  G_CALLBACK (rb_shell_view_deleted_cb),
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
rb_shell_view_deleted_cb (RBView *view,
		          RBShell *shell)
{
	if (g_list_find (shell->priv->groups, view) != NULL)
	{
		/* so, this is a group */
		rb_group_view_remove_file (RB_GROUP_VIEW (view));
		shell->priv->groups = g_list_remove (shell->priv->groups, view);

		rb_shell_save_music_groups (shell);
	}
	
	rb_shell_remove_view (shell, view);
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
				      _("Music Player"));

		gtk_tooltips_set_tip (shell->priv->tray_icon_tooltip,
				      GTK_WIDGET (shell->priv->tray_icon),
				      _("Not playing"),
				      NULL);
	}
	else
	{
		MonkeyMediaMixerState state;

		state = rb_shell_player_get_state (shell->priv->player_shell);

		if (state == MONKEY_MEDIA_MIXER_STATE_PAUSED)
		{
			char *tmp;

			tmp = g_strdup_printf (_("%s (Paused)"), window_title);
			gtk_window_set_title (GTK_WINDOW (shell->priv->window),
					      tmp);
			g_free (tmp);

			tmp = g_strdup_printf (_("%s\nPaused"), window_title);
			gtk_tooltips_set_tip (shell->priv->tray_icon_tooltip,
					      GTK_WIDGET (shell->priv->tray_icon),
					      tmp,
					      NULL);
			g_free (tmp);
		}
		else
		{
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
rb_shell_view_toolbar_changed_cb (BonoboUIComponent *component,
			          const char *path,
			          Bonobo_UIComponent_EventType type,
			          const char *state,
			          RBShell *shell)
{
	eel_gconf_set_boolean (CONF_UI_TOOLBAR_VISIBLE,
			       rb_bonobo_get_active (component, CMD_PATH_VIEW_TOOLBAR));
}

static void
rb_shell_view_statusbar_changed_cb (BonoboUIComponent *component,
			            const char *path,
			            Bonobo_UIComponent_EventType type,
			            const char *state,
			            RBShell *shell)
{
	eel_gconf_set_boolean (CONF_UI_STATUSBAR_VISIBLE,
			       rb_bonobo_get_active (component, CMD_PATH_VIEW_STATUSBAR));
}

static void
rb_shell_view_sidebar_changed_cb (BonoboUIComponent *component,
			          const char *path,
			          Bonobo_UIComponent_EventType type,
			          const char *state,
			          RBShell *shell)
{
	eel_gconf_set_boolean (CONF_UI_SIDEBAR_VISIBLE,
			       rb_bonobo_get_active (component, CMD_PATH_VIEW_SIDEBAR));
}

static void
rb_shell_show_window_changed_cb (BonoboUIComponent *component,
				 const char *path,
				 Bonobo_UIComponent_EventType type,
				 const char *state,
				 RBShell *shell)
{
	eel_gconf_set_boolean (CONF_STATE_WINDOW_VISIBLE,
			       rb_bonobo_get_active (component, CMD_PATH_SHOW_WINDOW));
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
		shell->priv->prefs = rb_shell_preferences_new ();

		gtk_window_set_transient_for (GTK_WINDOW (shell->priv->prefs),
					      GTK_WINDOW (shell->priv->window));
	}

	gtk_widget_show_all (shell->priv->prefs);
}

static void
rb_shell_toolbar_editor_revert_clicked_cb (GtkButton *b, GulTbEditor *tbe)
{
        gchar *def;

        g_return_if_fail (GUL_IS_TB_EDITOR (tbe));
        
        eel_gconf_unset (CONF_TOOLBAR_SETUP);
        def = eel_gconf_get_string (CONF_TOOLBAR_SETUP);
        if (def != NULL)
        {
                GulToolbar *current;
                GulToolbar *avail;
                current = gul_tb_editor_get_toolbar (tbe);
                gul_toolbar_parse (current, def);
                g_free (def);

                avail = gul_tb_editor_get_available (tbe);
                g_object_ref (avail);
                gul_toolbar_parse (avail, AVAILABLE_TOOLBAR_ITEMS);
                gul_tb_editor_set_available (tbe, avail);
                g_object_unref (avail);
        }
}

static void
rb_shell_toolbar_editor_current_changed_cb (GulToolbar *tb, gpointer data)
{
        gchar *current_str;

        g_return_if_fail (GUL_IS_TOOLBAR (tb));

        current_str = gul_toolbar_to_string (tb);
        eel_gconf_set_string (CONF_TOOLBAR_SETUP, current_str);
        g_free (current_str);
}

static void
rb_shell_cmd_edit_toolbar (BonoboUIComponent *component,
		           RBShell *shell,
		           const char *verbname)
{
	RBShellPrivate *p = shell->priv;
        GulToolbar *avail;
        GulToolbar *current;
        gchar *current_str;
        GtkButton *revert_button;

        avail = gul_toolbar_new ();
        gul_toolbar_parse (avail, AVAILABLE_TOOLBAR_ITEMS);

        current_str = eel_gconf_get_string (CONF_TOOLBAR_SETUP);
        current = gul_toolbar_new ();
        if (current_str != NULL)
        {
                gul_toolbar_parse (current, current_str);
                g_free (current_str);
        }

	if (!p->tb_editor) 
	{
		p->tb_editor = gul_tb_editor_new ();
		g_object_add_weak_pointer (G_OBJECT (p->tb_editor),
					   (void **) &p->tb_editor);
	}
	else
	{
		gul_tb_editor_show (p->tb_editor);
		return;
	}
	
	gul_tb_editor_set_parent (p->tb_editor, 
				  shell->priv->window); 
        gul_tb_editor_set_toolbar (p->tb_editor, current);
        gul_tb_editor_set_available (p->tb_editor, avail);

        g_object_unref (avail);
        g_object_unref (current);

        g_signal_connect (current, "changed", 
                          G_CALLBACK (rb_shell_toolbar_editor_current_changed_cb), NULL);

        revert_button = gul_tb_editor_get_revert_button (p->tb_editor);
        gtk_widget_show (GTK_WIDGET (revert_button));

        g_signal_connect (revert_button, "clicked", 
                          G_CALLBACK (rb_shell_toolbar_editor_revert_clicked_cb),
			  p->tb_editor);
        
        gul_tb_editor_show (p->tb_editor);
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
    		rb_library_add_uri (shell->priv->library, *filecur);
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
ask_string_response_cb (GtkDialog *dialog,
			int response_id,
			RBShell *shell)
{
	GtkWidget *entry, *checkbox;
	RBView *group;
	char *name;
	gboolean add_selection;
	CreateGroupType type;
	GList *data, *l;

	type = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (dialog), "type"));
	data = g_object_get_data (G_OBJECT (dialog), "data");

	if (response_id != GTK_RESPONSE_OK)
	{
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
	
	if (name == NULL)
	{
		if (type == CREATE_GROUP_WITH_URI_LIST)
			gnome_vfs_uri_list_free (data);
		return;
	}

	group = rb_group_view_new (shell->priv->container,
				   shell->priv->library);
	rb_group_view_set_name (RB_GROUP_VIEW (group), name);
	shell->priv->groups = g_list_append (shell->priv->groups, group);
	rb_shell_append_view (shell, group);
	g_free (name);

	switch (type)
	{
	case CREATE_GROUP_WITH_NODE_LIST:
		for (l = data; l != NULL; l = g_list_next (l))
		{
			rb_group_view_add_node (RB_GROUP_VIEW (group),
						RB_NODE (l->data));
		}
		break;
	case CREATE_GROUP_WITH_URI_LIST:
		for (l = data; l != NULL; l = g_list_next (l))
		{
			char *uri;
			RBNode *node;
				
			uri = gnome_vfs_uri_to_string ((GnomeVFSURI *) l->data, GNOME_VFS_URI_HIDE_NONE);
			node = rb_library_get_song_by_location (shell->priv->library, uri);

			if (node != NULL)
			{
				/* add this node to the newly created group */
				rb_group_view_add_node (RB_GROUP_VIEW (group), node);
			}
			else
			{
				/* will add these nodes to the newly created group */
				RBLibraryAction *action = rb_library_add_uri (shell->priv->library, uri);
				g_object_set_data (G_OBJECT (group), "library", shell->priv->library);
                                g_signal_connect_object (G_OBJECT (action),
                                                         "handled", 
                                                         G_CALLBACK (dnd_add_handled_cb),
                                                         G_OBJECT (group),
                                                         0);
			}

			g_free (uri);
		}
		gnome_vfs_uri_list_free (data);
		break;
	case CREATE_GROUP_WITH_SELECTION:
		{	
			/* add the current selection if the user checked */
			if (add_selection)
			{
				GList *i = NULL;
				GList *selection = rb_view_get_selection (shell->priv->selected_view);
				for (i  = selection; i != NULL; i = g_list_next (i))
					rb_group_view_add_node (RB_GROUP_VIEW (group), i->data);
			}
		}
		break;
	}

	rb_shell_save_music_groups (shell);
}

static void
create_group (RBShell *shell, CreateGroupType type,
	      GList *data)
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
rb_shell_cmd_new_group (BonoboUIComponent *component,
			RBShell *shell,
			const char *verbname)
{
	create_group (shell, CREATE_GROUP_WITH_SELECTION, NULL);
}

static void
rb_shell_cmd_dummy (BonoboUIComponent *component,
		    RBShell *shell,
		    const char *verbname)
{
	/* to work around bonoboui bug */
}

static void
rb_shell_quit (RBShell *shell)
{
	rb_debug ("Quitting");

	rb_shell_window_save_state (shell);

	bonobo_object_unref (BONOBO_OBJECT (shell));
}

static void
rb_shell_load_music_groups (RBShell *shell)
{
	GSList *groups, *l;

	groups = eel_gconf_get_string_list (CONF_MUSIC_GROUPS);

	for (l = groups; l != NULL; l = g_slist_next (l))
	{
		RBView *group;

		group = rb_group_view_new_from_file (shell->priv->container,
						     shell->priv->library,
						     (char *) l->data);
		shell->priv->groups = g_list_append (shell->priv->groups, group);

		rb_shell_append_view (shell, group);
	}

	g_slist_foreach (groups, (GFunc) g_free, NULL);
	g_slist_free (groups);
}

static void
rb_shell_save_music_groups (RBShell *shell)
{
	GSList *groups = NULL;
	GList *l;

	for (l = shell->priv->groups; l != NULL; l = g_list_next (l))
	{
		RBGroupView *group = RB_GROUP_VIEW (l->data);

		groups = g_slist_append (groups,
					 (char *) rb_group_view_get_file (group));
		rb_group_view_save (group);
	}
	
	eel_gconf_set_string_list (CONF_MUSIC_GROUPS, groups);
	
	g_slist_free (groups);
}

static void
rb_shell_sidebar_size_allocate_cb (GtkWidget *sidebar,
				   GtkAllocation *allocation,
				   RBShell *shell)
{
	eel_gconf_set_integer (CONF_STATE_PANED_POSITION,
			       gtk_paned_get_position (GTK_PANED (shell->priv->paned)));
}

static void
rb_shell_sync_toolbar_visibility (RBShell *shell)
{
	gboolean visible;

	visible = eel_gconf_get_boolean (CONF_UI_TOOLBAR_VISIBLE);

	rb_bonobo_set_visible (shell->priv->ui_component,
			       "/Toolbar",
			       visible);

	rb_bonobo_set_active (shell->priv->ui_component,
			      CMD_PATH_VIEW_TOOLBAR,
			      visible);
}

static void
rb_shell_sync_statusbar_visibility (RBShell *shell)
{
	gboolean visible;

	visible = eel_gconf_get_boolean (CONF_UI_STATUSBAR_VISIBLE);
	
	if (visible)
		gtk_widget_show (GTK_WIDGET (shell->priv->status_shell));
	else
		gtk_widget_hide (GTK_WIDGET (shell->priv->status_shell));

	rb_bonobo_set_active (shell->priv->ui_component,
			      CMD_PATH_VIEW_STATUSBAR,
			      visible);
}

static void
rb_shell_sync_sidebar_visibility (RBShell *shell)
{
	gboolean visible;

	visible = eel_gconf_get_boolean (CONF_UI_SIDEBAR_VISIBLE);
	
	if (visible)
		gtk_widget_show (GTK_WIDGET (shell->priv->sidebar));
	else
		gtk_widget_hide (GTK_WIDGET (shell->priv->sidebar));
	
	rb_bonobo_set_active (shell->priv->ui_component,
			      CMD_PATH_VIEW_SIDEBAR,
			      visible);
}

static void
rb_shell_sync_toolbar_style (RBShell *shell)
{
	char *style;

	style = eel_gconf_get_string (CONF_UI_TOOLBAR_STYLE);
	
	if (style == NULL || strcmp (style, "desktop_default") == 0)
	{
		g_free (style);
		style = g_strdup ("");
	}
	
	rb_bonobo_set_look (shell->priv->ui_component,
			    "/Toolbar",
			    style);

	g_free (style);
}

static void
rb_shell_sync_window_visibility (RBShell *shell)
{
	gboolean visible;
	static int window_x = -1;
	static int window_y = -1;

	visible = eel_gconf_get_boolean (CONF_STATE_WINDOW_VISIBLE);
	
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
toolbar_visibility_changed_cb (GConfClient *client,
			       guint cnxn_id,
			       GConfEntry *entry,
			       RBShell *shell)
{
	rb_shell_sync_toolbar_visibility (shell);
}

static void
statusbar_visibility_changed_cb (GConfClient *client,
				 guint cnxn_id,
				 GConfEntry *entry,
				 RBShell *shell)
{
	rb_shell_sync_statusbar_visibility (shell);
}

static void
sidebar_visibility_changed_cb (GConfClient *client,
			       guint cnxn_id,
			       GConfEntry *entry,
			       RBShell *shell)
{
	rb_shell_sync_sidebar_visibility (shell);
}

static void
toolbar_style_changed_cb (GConfClient *client,
			  guint cnxn_id,
			  GConfEntry *entry,
			  RBShell *shell)
{
	rb_shell_sync_toolbar_style (shell);
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

static void
add_uri (const char *uri,
	 RBGroupView *view)
{
	RBNode *node;

	node = rb_library_get_song_by_location (g_object_get_data (G_OBJECT (view), "library"),
					        uri);

	if (node != NULL)
	{
		rb_group_view_add_node (view, node);
	}
}

static void
dnd_add_handled_cb (RBLibraryAction *action,
		    RBGroupView *view)
{
	char *uri;
	RBLibraryActionType type;

	rb_library_action_get (action,
			       &type,
			       &uri);

	switch (type)
	{
	case RB_LIBRARY_ACTION_ADD_FILE:
		{
			RBNode *node;

			node = rb_library_get_song_by_location (g_object_get_data (G_OBJECT (view), "library"),
								uri);

			if (node != NULL)
			{
				rb_group_view_add_node (view, node);
			}
		}
		break;
	case RB_LIBRARY_ACTION_ADD_DIRECTORY:
		{
			rb_uri_handle_recursively (uri,
						   (GFunc) add_uri,
						   view);
		}
		break;
	default:
		break;
	}
}

static void
handle_songs_func (RBNode *node,
		   RBGroupView *group)
{
	rb_group_view_add_node (group, node);
}

static void
rb_sidebar_drag_finished_cb (RBSidebar *sidebar,
			     GdkDragContext *context,
			     int x, int y,
			     GtkSelectionData *data,
			     guint info,
			     guint time,
			     RBShell *shell)
{
	switch (info)
	{
	case RB_LIBRARY_DND_NODE_ID:
		{
			long id;
			RBNode *node;
			RBGroupView *group;

			id = atol (data->data);
			node = rb_node_get_from_id (id);

			if (node == NULL)
				break;
			
			group = RB_GROUP_VIEW (rb_group_view_new (shell->priv->container,
						                  shell->priv->library));
					
			rb_group_view_set_name (RB_GROUP_VIEW (group),
						rb_node_get_property_string (node,
								             RB_NODE_PROP_NAME));


			rb_library_handle_songs (shell->priv->library,
						 node,
						 (GFunc) handle_songs_func,
						 group);

			shell->priv->groups = g_list_append (shell->priv->groups, group);
			rb_shell_append_view (shell, RB_VIEW (group));
		}
		break;
	case RB_LIBRARY_DND_URI_LIST:
		{
			GList *list;

			list = gnome_vfs_uri_list_parse (data->data);
			create_group (shell, CREATE_GROUP_WITH_URI_LIST, list);
		}
		break;
	}

	gtk_drag_finish (context, TRUE, FALSE, time);
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
	GtkWidget *dialog, *hbox, *image, *entry, *label, *vbox, *cbox, *align, *vbox2;
	GList *selection;
	char *tmp;
	
	dialog = gtk_dialog_new_with_buttons ("",
					      NULL,
					      0,
					      GTK_STOCK_CANCEL,
					      GTK_RESPONSE_CANCEL,
					      _("Create"),
					      GTK_RESPONSE_OK,
					      NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);
	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 12);

	gtk_window_set_transient_for (GTK_WINDOW (dialog), 
				      GTK_WINDOW (shell->priv->window));
	gtk_window_set_modal (GTK_WINDOW (dialog), FALSE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE); 

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 6);
	image = gtk_image_new_from_stock (RB_STOCK_GROUP,
					  GTK_ICON_SIZE_DIALOG);
	align = gtk_alignment_new (0.5, 0.0, 0.0, 0.0);
	gtk_container_add (GTK_CONTAINER (align), image);
	gtk_box_pack_start (GTK_BOX (hbox), align, TRUE, TRUE, 0);
	vbox = gtk_vbox_new (FALSE, 0);

	tmp = g_strdup_printf ("%s\n", _("Please enter a name for the new music group."));
	label = gtk_label_new (tmp);
	g_free (tmp);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);

	vbox2 = gtk_vbox_new (FALSE, 6);
	gtk_box_pack_start (GTK_BOX (vbox), vbox2, FALSE, TRUE, 0);
	
	entry = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (entry), _("Untitled"));
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
	gtk_box_pack_start (GTK_BOX (vbox2), entry, FALSE, TRUE, 0);

	cbox = gtk_check_button_new_with_mnemonic (_("Add the _selected songs to the new group"));
	selection = rb_view_get_selection (shell->priv->selected_view);
	if (selection == NULL)
		gtk_widget_set_sensitive (cbox, FALSE);
	gtk_box_pack_start (GTK_BOX (vbox2), cbox, FALSE, TRUE, 0);

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

static void
tray_button_press_event_cb (GtkWidget *ebox,
			    GdkEventButton *event,
			    RBShell *shell)
{
	switch (event->button)
	{
	case 1:
		/* toggle mainwindow visibility */
		eel_gconf_set_boolean (CONF_STATE_WINDOW_VISIBLE,
				       !eel_gconf_get_boolean (CONF_STATE_WINDOW_VISIBLE));
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

#ifdef HAVE_REMOTE
static void
rb_shell_remote_cb (RBRemote *remote, RBRemoteCommand cmd, RBShell *shell)
{
	gboolean shuffle;
	gboolean repeat;
	int volume;
	gboolean mute;

	switch (cmd) {
		case RB_REMOTE_COMMAND_SHUFFLE:
			shuffle = rb_bonobo_get_active (shell->priv->ui_component,
							CMD_PATH_SHUFFLE);
			shuffle ^= 1;

			rb_bonobo_set_active (shell->priv->ui_component,
					      CMD_PATH_SHUFFLE,
					      shuffle);
			break;
		case RB_REMOTE_COMMAND_REPEAT:
			repeat = rb_bonobo_get_active (shell->priv->ui_component,
							CMD_PATH_REPEAT);
			repeat ^= 1;

			rb_bonobo_set_active (shell->priv->ui_component,
					      CMD_PATH_REPEAT,
					      repeat);
			break;
		case RB_REMOTE_COMMAND_VOLUME_UP:
			volume = rb_volume_get (shell->priv->volume);

			volume += RB_SHELL_REMOTE_VOLUME_INTERVAL;

			rb_volume_set (shell->priv->volume, volume);
			break;
		case RB_REMOTE_COMMAND_VOLUME_DOWN:
			volume = rb_volume_get (shell->priv->volume);

			volume -= RB_SHELL_REMOTE_VOLUME_INTERVAL;

			rb_volume_set (shell->priv->volume, volume);
			break;
		case RB_REMOTE_COMMAND_MUTE:
			mute = rb_volume_get_mute (shell->priv->volume);
			mute ^= 1;
			rb_volume_set_mute (shell->priv->volume, mute);
			break;
		case RB_REMOTE_COMMAND_QUIT:
			/* FIXME: this is ridiculously broken */
			rb_shell_quit (shell);
			break;
		default:
			break;
	}
}
#endif
