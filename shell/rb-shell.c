/*
 *  arch-tag: Implementation of main Rhythmbox shell
 *
 *  Copyright (C) 2002, 2003 Jorn Baayen
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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

#include <bonobo/bonobo-arg.h>
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
#include <libgnomeui/gnome-client.h>
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
#include "rhythmdb-tree.h"
#include "rb-stock-icons.h"
#include "rb-sourcelist.h"
#include "rb-string-helpers.h"
#include "rb-file-helpers.h"
#include "rb-source.h"
#include "rb-playlist-manager.h"
#include "rb-preferences.h"
#include "rb-druid.h"
#include "rb-shell-clipboard.h"
#include "rb-shell-player.h"
#include "rb-source-header.h"
#include "rb-tray-icon.h"
#include "rb-statusbar.h"
#include "rb-shell-preferences.h"
#include "rb-playlist.h"
#include "rb-bonobo-helpers.h"
#include "rb-library-source.h"
#include "rb-load-failure-dialog.h"
#include "rb-new-station-dialog.h"
#include "rb-iradio-source.h"
#ifdef HAVE_AUDIOCD
#include "rb-audiocd-source.h"
#endif
#include "rb-shell-preferences.h"
#include "rb-playlist-source.h"
#include "rb-file-monitor.h"
#include "rb-thread-helpers.h"
#include "eel-gconf-extensions.h"

#ifdef WITH_DASHBOARD
#include <glib.h>
#include <sys/time.h>
#include "dashboard.c"
#endif /* WITH_DASHBOARD */

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
static void rb_shell_corba_playpause (PortableServer_Servant _servant,
					       CORBA_Environment *ev);
static void rb_shell_corba_select (PortableServer_Servant _servant,
				   const CORBA_char *uri,
				   CORBA_Environment *ev);
static void rb_shell_corba_play (PortableServer_Servant _servant,
				 const CORBA_char *uri,
				 CORBA_Environment *ev);
static void rb_shell_corba_next (PortableServer_Servant _servant,
					       CORBA_Environment *ev);
static void rb_shell_corba_previous (PortableServer_Servant _servant,
					       CORBA_Environment *ev);
static CORBA_long rb_shell_corba_get_playing_time (PortableServer_Servant _servant,
						   CORBA_Environment *ev);
static void rb_shell_corba_set_playing_time (PortableServer_Servant _servant,
						   CORBA_long time, CORBA_Environment *ev);

static Bonobo_PropertyBag rb_shell_corba_get_player_properties (PortableServer_Servant _servant, CORBA_Environment *ev);
static void rb_shell_db_changed_cb (RhythmDB *db, RhythmDBEntry *entry,
				    RBShell *shell);

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
static void source_activated_cb (RBSourceList *sourcelist,
				 RBSource *source,
				 RBShell *shell);
static void rb_shell_db_error_cb (RhythmDB *db,
				  const char *uri, const char *msg,
				  RBShell *shell); 
static void rb_shell_load_failure_dialog_response_cb (GtkDialog *dialog,
						      int response_id,
						      RBShell *shell);

static void rb_shell_playlist_added_cb (RBPlaylistManager *mgr, RBSource *source, RBShell *shell);
static void rb_shell_playlist_load_start_cb (RBPlaylistManager *mgr, RBShell *shell);
static void rb_shell_playlist_load_finish_cb (RBPlaylistManager *mgr, RBShell *shell);
static void rb_shell_source_deleted_cb (RBSource *source, RBShell *shell);
static void rb_shell_set_window_title (RBShell *shell, const char *window_title);
static void rb_shell_set_duration (RBShell *shell, const char *duration);
static void rb_shell_player_window_title_changed_cb (RBShellPlayer *player,
					             const char *window_title,
					             RBShell *shell);
static void rb_shell_player_duration_changed_cb (RBShellPlayer *player,
						 const char *duration,
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
static void rb_shell_cmd_add_to_library (BonoboUIComponent *component,
			                 RBShell *shell,
			                 const char *verbname);
static void rb_shell_cmd_add_location (BonoboUIComponent *component,
				       RBShell *shell,
				       const char *verbname);
static void rb_shell_cmd_new_station (BonoboUIComponent *component,
				      RBShell *shell,
				      const char *verbname);
static void rb_shell_cmd_extract_cd (BonoboUIComponent *component,
				       RBShell *shell,
				       const char *verbname);
static void rb_shell_cmd_current_song (BonoboUIComponent *component,
				       RBShell *shell,
				       const char *verbname);
static void rb_shell_jump_to_current (RBShell *shell);
static void rb_shell_jump_to_entry (RBShell *shell, RhythmDBEntry *entry);
static void rb_shell_jump_to_entry_with_source (RBShell *shell, RBSource *source,
						RhythmDBEntry *entry);
static void rb_shell_play_entry (RBShell *shell, RhythmDBEntry *entry);
static void rb_shell_quit (RBShell *shell);
static void rb_shell_view_sourcelist_changed_cb (BonoboUIComponent *component,
						 const char *path,
						 Bonobo_UIComponent_EventType type,
						 const char *state,
						 RBShell *shell);
static void rb_shell_view_smalldisplay_changed_cb (BonoboUIComponent *component,
						 const char *path,
						 Bonobo_UIComponent_EventType type,
						 const char *state,
						 RBShell *shell);
static void rb_shell_load_complete_cb (RhythmDB *db, RBShell *shell);
static void rb_shell_legacy_load_complete_cb (RhythmDB *db, RBShell *shell);
static void rb_shell_sync_sourcelist_visibility (RBShell *shell);
static void rb_shell_sync_smalldisplay (RBShell *shell);
static void sourcelist_visibility_changed_cb (GConfClient *client,
					      guint cnxn_id,
					      GConfEntry *entry,
					      RBShell *shell);
static void smalldisplay_changed_cb (GConfClient *client,
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
static void tray_deleted_cb (GtkWidget *win, GdkEventAny *event, RBShell *shell);

static gboolean save_yourself_cb (GnomeClient *client, 
                                  gint phase,
                                  GnomeSaveStyle save_style,
                                  gboolean shutdown,
                                  GnomeInteractStyle interact_style,
                                  gboolean fast,
                                  RBShell *shell);

static void session_die_cb (GnomeClient *client, RBShell *shell);
static void rb_shell_session_init (RBShell *shell);


static const GtkTargetEntry target_table[] = { { "text/uri-list", 0,0 } };

#define CMD_PATH_VIEW_SMALLDISPLAY "/commands/ToggleSmallDisplay"
#define CMD_PATH_VIEW_SOURCELIST   "/commands/ShowSourceList"
#define CMD_PATH_EXTRACT_CD     "/commands/ExtractCD"
#define CMD_PATH_CURRENT_SONG	"/commands/CurrentSong"

/* prefs */
#define CONF_STATE_WINDOW_WIDTH     CONF_PREFIX "/state/window_width"
#define CONF_STATE_WINDOW_HEIGHT    CONF_PREFIX "/state/window_height"
#define CONF_STATE_SMALL_WIDTH      CONF_PREFIX "/state/small_width"
#define CONF_STATE_WINDOW_MAXIMIZED CONF_PREFIX "/state/window_maximized"
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
	GtkWidget *hsep;

	GList *sources;

	gboolean play_queued;

	gboolean db_dirty;
	guint async_state_save_id;

	RhythmDB *db;

	RBShellPlayer *player_shell;
	RBShellClipboard *clipboard_shell;
	RBSourceHeader *source_header;
	RBStatusbar *statusbar;
	RBPlaylistManager *playlist_manager;

	RBSource *library_source;
	GtkWidget *load_error_dialog;
	GList *supported_media_extensions;
	gboolean show_db_errors;

 	RBIRadioSource *iradio_source;

#ifdef HAVE_AUDIOCD
 	MonkeyMediaAudioCD *cd;
#endif

	RBSource *selected_source;

	GtkWidget *prefs;

	RBTrayIcon *tray_icon;

	BonoboPropertyBag *pb;

	char *cached_title;
	char *cached_duration;
};

static BonoboUIVerb rb_shell_verbs[] =
{
	BONOBO_UI_VERB ("About",        (BonoboUIVerbFn) rb_shell_cmd_about),
	BONOBO_UI_VERB ("Contents",	(BonoboUIVerbFn) rb_shell_cmd_contents),
	BONOBO_UI_VERB ("Quit",		(BonoboUIVerbFn) rb_shell_cmd_quit),
	BONOBO_UI_VERB ("Preferences",  (BonoboUIVerbFn) rb_shell_cmd_preferences),
	BONOBO_UI_VERB ("AddToLibrary", (BonoboUIVerbFn) rb_shell_cmd_add_to_library),
	BONOBO_UI_VERB ("AddLocation",  (BonoboUIVerbFn) rb_shell_cmd_add_location),
	BONOBO_UI_VERB ("NewStation",   (BonoboUIVerbFn) rb_shell_cmd_new_station),
	BONOBO_UI_VERB ("ExtractCD",    (BonoboUIVerbFn) rb_shell_cmd_extract_cd),
	BONOBO_UI_VERB ("CurrentSong",	(BonoboUIVerbFn) rb_shell_cmd_current_song),
	BONOBO_UI_VERB_END
};

static RBBonoboUIListener rb_shell_listeners[] =
{
	RB_BONOBO_UI_LISTENER ("ShowSourceList",(BonoboUIListenerFn) rb_shell_view_sourcelist_changed_cb),
	RB_BONOBO_UI_LISTENER ("ToggleSmallDisplay",(BonoboUIListenerFn) rb_shell_view_smalldisplay_changed_cb),
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
	epv->playPause = rb_shell_corba_playpause;
	epv->select = rb_shell_corba_select;
	epv->play = rb_shell_corba_play;
	epv->previous = rb_shell_corba_previous;
	epv->next = rb_shell_corba_next;
	epv->getPlayingTime = rb_shell_corba_get_playing_time;
	epv->setPlayingTime = rb_shell_corba_set_playing_time;
	epv->getPlayerProperties = rb_shell_corba_get_player_properties;
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
	
        rb_shell_session_init (shell);

	eel_gconf_monitor_add (CONF_PREFIX);

}

static gboolean
rb_shell_sync_state (RBShell *shell)
{
	if (g_object_get_data (G_OBJECT (shell), "rb-shell-dry-run"))
		return FALSE;
	
	rb_debug ("saving playlists");
	rb_playlist_manager_save_playlists (shell->priv->playlist_manager);

	rb_debug ("saving db");
	rhythmdb_read_lock (shell->priv->db);
	rhythmdb_save (shell->priv->db);
	rhythmdb_read_unlock (shell->priv->db);
	return FALSE;
}

static gboolean
idle_save_state (RBShell *shell)
{
	GDK_THREADS_ENTER ();

	rb_shell_sync_state (shell);

	GDK_THREADS_LEAVE ();
	return FALSE;
}

static void
rb_shell_db_changed_cb (RhythmDB *db, RhythmDBEntry *entry,
			RBShell *shell)
{
	shell->priv->db_dirty = TRUE;
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

	g_object_unref (G_OBJECT (shell->priv->playlist_manager));

	g_object_unref (G_OBJECT (shell->priv->clipboard_shell));

	gtk_widget_destroy (shell->priv->window);

	rb_debug ("shutting down DB");
	rhythmdb_shutdown (shell->priv->db);

	rb_debug ("unreffing DB");
	g_object_unref (G_OBJECT (shell->priv->db));

	if (shell->priv->prefs != NULL)
		gtk_widget_destroy (shell->priv->prefs);
	
	g_free (shell->priv);

	g_object_unref (G_OBJECT (rb_file_monitor_get ()));

        parent_class->finalize (G_OBJECT (shell));

	rb_debug ("THE END");
	bonobo_main_quit ();
}

RBShell *
rb_shell_new (void)
{
	RBShell *s;

	s = g_object_new (RB_TYPE_SHELL, NULL);

	return s;
}

struct RBShellAction
{
	RBShell *shell;
	char *uri;
	guint tries;
	enum {
		RB_SHELL_ACTION_JUMP,
		RB_SHELL_ACTION_PLAY,
	} type;
};

static gboolean
idle_do_action (struct RBShellAction *data)
{
	RhythmDBEntry *entry;
	char *unquoted = NULL;
	
	GDK_THREADS_ENTER ();
	rb_debug ("entering idle_do_action");

	data->tries++;

	rhythmdb_read_lock (data->shell->priv->db);
	entry = rhythmdb_entry_lookup_by_location (data->shell->priv->db, data->uri);
	rhythmdb_read_unlock (data->shell->priv->db);

	if (entry) {
		switch (data->type) {
		case RB_SHELL_ACTION_PLAY:
			rb_debug ("doing play action");
			rb_shell_play_entry (data->shell, entry);
			/* fall through */ 
		case RB_SHELL_ACTION_JUMP:
			rb_debug ("doing jump action");
			rb_shell_jump_to_entry (data->shell, entry);
			break;
		}
	} else if (data->tries < 4) {
		rb_debug ("entry not added yet, queueing retry");
		g_timeout_add (500 + data->tries*200, (GSourceFunc) idle_do_action, data);
		goto out_unlock;
	} else
		g_warning ("No entry %s in db", data->uri);

	data->shell->priv->play_queued = FALSE;

	g_free (unquoted);
	g_free (data->uri);
	g_free (data);

 out_unlock:
	GDK_THREADS_LEAVE ();
	return FALSE;
}

static void
rb_shell_queue_jump (RBShell *shell, const char *uri)
{
	struct RBShellAction *data;

	rb_debug ("queueing jump");

	data = g_new0 (struct RBShellAction, 1);
	data->shell = shell;
	data->uri = g_strdup (uri);
	data->type = RB_SHELL_ACTION_JUMP;
	g_idle_add ((GSourceFunc) idle_do_action, data);
}

static void
rb_shell_queue_play (RBShell *shell, const char *uri)
{
	struct RBShellAction *data;

	if (shell->priv->play_queued) {
		rb_debug ("file already queued for playback");
		return;
	}
	rb_debug ("queueing play");

	data = g_new0 (struct RBShellAction, 1);
	data->shell = shell;
	data->uri = g_strdup (uri);
	data->type = RB_SHELL_ACTION_PLAY;
	g_idle_add ((GSourceFunc) idle_do_action, data);
}

static void
rb_shell_corba_quit (PortableServer_Servant _servant,
                     CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));

	rb_debug ("corba quit");

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
	RBPlaylist *parser;

	rb_debug ("handling uri: %s", uri);

	GnomeVFSURI *vfsuri = gnome_vfs_uri_new (uri);
	if (!vfsuri) {
		rb_error_dialog (_("Unable to parse URI \"%s\"\n"), uri);
		return;
	}

	parser = rb_playlist_new ();
	if (rb_playlist_can_handle (uri)) {
		rb_debug ("parsing uri as playlist: %s", uri);
		uri = rb_playlist_manager_parse_file (shell->priv->playlist_manager, uri);
	} else {
		rb_debug ("async adding uri: %s", uri);
		rhythmdb_add_uri_async (shell->priv->db, uri);
	}

	rb_shell_queue_play (shell, uri);
	g_object_unref (G_OBJECT (parser));
}

static void
rb_shell_corba_add_to_library (PortableServer_Servant _servant,
			       const CORBA_char *uri,
			       CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));

	rb_debug ("async adding uri: %s", uri);
	rhythmdb_add_uri_async (shell->priv->db, uri);
}

static void
rb_shell_corba_grab_focus (PortableServer_Servant _servant,
			   CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));

	rb_debug ("grabbing focus");
	gtk_window_present (GTK_WINDOW (shell->priv->window));
	gtk_widget_grab_focus (shell->priv->window);
}

enum PlayerProperties {
	PROP_VISIBILITY,
	PROP_SONG,
	PROP_SHUFFLE,
};


static void 
shell_notify_pb_changes (RBShell *shell, const gchar *property_name, 
			 BonoboArg *arg) 
{
	if (shell->priv->pb != NULL) {
		bonobo_event_source_notify_listeners_full (shell->priv->pb->es,
							   "Bonobo/Property",
							   "change",
							   property_name,
							   arg, NULL);
	}
}

static GNOME_Rhythmbox_SongInfo *
get_song_info_from_player (RBShell *shell)
{
	RhythmDBEntry *entry;
	RhythmDB *db = shell->priv->db;
	GNOME_Rhythmbox_SongInfo *song_info;
	RBEntryView *view;
	RBSource *playing_source;

	playing_source = rb_shell_player_get_playing_source (shell->priv->player_shell);

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
	song_info->bitrate = rhythmdb_entry_get_int (db, entry, RHYTHMDB_PROP_QUALITY);
	song_info->filesize = rhythmdb_entry_get_long (db, entry, RHYTHMDB_PROP_FILE_SIZE);
	song_info->rating = rhythmdb_entry_get_int (db, entry, RHYTHMDB_PROP_RATING);
	song_info->play_count = rhythmdb_entry_get_int (db, entry, RHYTHMDB_PROP_PLAY_COUNT);
	song_info->last_played = rhythmdb_entry_get_long (db, entry, RHYTHMDB_PROP_LAST_PLAYED);
	rhythmdb_read_unlock (db);

	return song_info;
 lose:
	return NULL;
}

static void
shell_pb_get_prop (BonoboPropertyBag *bag,
		   BonoboArg         *arg,
		   guint              arg_id,
		   CORBA_Environment *ev,
		   gpointer           user_data)
{
	RBShell *shell = RB_SHELL (user_data);
	RBShellPlayer *player;

	player = RB_SHELL_PLAYER (shell->priv->player_shell);

	switch (arg_id) {

	case PROP_VISIBILITY:
		BONOBO_ARG_SET_BOOLEAN (arg, FALSE);
		break;

	case PROP_SHUFFLE:
	{
		char *play_order;
		g_object_get (G_OBJECT (player), "play-order", &play_order, NULL);
		BONOBO_ARG_SET_BOOLEAN (arg, !strcmp ("shuffle", play_order));
		break;
	}

	case PROP_SONG: {
		GNOME_Rhythmbox_SongInfo *ret_val;
		ret_val = get_song_info_from_player (shell);
		(GNOME_Rhythmbox_SongInfo*)arg->_value = ret_val;
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
shell_pb_set_prop (BonoboPropertyBag *bag,
		   const BonoboArg   *arg,
		   guint              arg_id,
		   CORBA_Environment *ev,
		   gpointer           user_data)
{
	switch (arg_id) {

	case PROP_VISIBILITY:
		break;

	case PROP_SONG:
		bonobo_exception_set (ev, ex_Bonobo_PropertyBag_ReadOnly);
		break;

	default:
		bonobo_exception_set (ev, ex_Bonobo_PropertyBag_NotFound);
		break;
	}
}


static Bonobo_PropertyBag
rb_shell_corba_get_player_properties (PortableServer_Servant _servant, 
				      CORBA_Environment *ev)
{	
	RBShell *shell = RB_SHELL (bonobo_object (_servant));

	rb_debug ("getting player properties");
	if (shell->priv->pb == NULL) {
		gchar *params_to_map[] = {"repeat", "play-order", "playing"}; 
		GParamSpec **params;
		int i = 0;
		int total = 0;

		shell->priv->pb = bonobo_property_bag_new (shell_pb_get_prop, 
							   shell_pb_set_prop, 
							   shell);
		
		
		params = malloc (G_N_ELEMENTS (params_to_map) * sizeof (GParamSpec *));
		for (i = 0; i < G_N_ELEMENTS (params_to_map); i++) {
			params[total] = g_object_class_find_property (G_OBJECT_CLASS (RB_SHELL_PLAYER_GET_CLASS (shell->priv->player_shell)), params_to_map[i]);
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
	}
	/* If the creation of the property bag failed, 
	 * return a corba exception
	 */
	
	return bonobo_object_dup_ref (BONOBO_OBJREF (shell->priv->pb), NULL);
}

static void
rb_shell_corba_playpause (PortableServer_Servant _servant,
			  CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));
	rb_debug ("got playpause");
	rb_shell_player_playpause (shell->priv->player_shell);
}

static void
rb_shell_corba_select (PortableServer_Servant _servant,
		       const CORBA_char *uri,
		       CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));
	rb_debug ("got select");
	rb_shell_queue_jump (shell, uri);
}

static void
rb_shell_corba_play (PortableServer_Servant _servant,
		     const CORBA_char *uri,
		     CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));
	rb_debug ("got play");
	rb_shell_queue_play (shell, uri);
}

static void
rb_shell_corba_next (PortableServer_Servant _servant,
		     CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));
	rb_debug ("got next");
	rb_shell_player_do_next (shell->priv->player_shell);
}

static void
rb_shell_corba_previous (PortableServer_Servant _servant,
			 CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));
	rb_debug ("got previous");
	rb_shell_player_do_previous (shell->priv->player_shell);
}

static CORBA_long
rb_shell_corba_get_playing_time (PortableServer_Servant _servant,
				 CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));
	rb_debug ("got playing time");
	return rb_shell_player_get_playing_time (shell->priv->player_shell);
}

static void
rb_shell_corba_set_playing_time (PortableServer_Servant _servant,
				 CORBA_long time, CORBA_Environment *ev)
{
	RBShell *shell = RB_SHELL (bonobo_object (_servant));
	rb_debug ("got set playing time");
	rb_debug ("got milk?");
	rb_shell_player_set_playing_time (shell->priv->player_shell, time);
}


static void
rb_shell_property_changed_generic_cb (GObject *object,
				      GParamSpec *pspec, 
				      RBShell *shell)
{
	BonoboArg *arg = bonobo_arg_new (TC_CORBA_boolean);
	gboolean value;

	g_object_get (object, pspec->name, &value, NULL);
	BONOBO_ARG_SET_BOOLEAN (arg, value);
	shell_notify_pb_changes (shell, pspec->name, arg);
	/* FIXME: arg should be released somehow */
}

static void
rb_shell_entry_changed_cb (GObject *object, GParamSpec *pspec, RBShell *shell)
{
	GNOME_Rhythmbox_SongInfo *song_info;
	BonoboArg *arg;

	g_assert (strcmp (pspec->name, "playing-entry") == 0);
	song_info = get_song_info_from_player (shell);
	arg = bonobo_arg_new (TC_GNOME_Rhythmbox_SongInfo);
	(GNOME_Rhythmbox_SongInfo*)arg->_value = song_info;
	shell_notify_pb_changes (shell, "song", arg);
	/* FIXME: arg should be released somehow */

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
#endif //WITH_DASHBOARD
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
	gboolean rhythmdb_exists;
	
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

	/* Initialize the database */
	rb_debug ("creating database object");
	{
		char *fname = g_build_filename (rb_dot_dir (), "rhythmdb.xml", NULL);

		rhythmdb_exists = g_file_test (fname, G_FILE_TEST_EXISTS);
		
		shell->priv->db = rhythmdb_tree_new (fname);
		g_free (fname);

		if (rhythmdb_exists) {
			g_signal_connect_object (G_OBJECT (shell->priv->db), "load-complete",
						 G_CALLBACK (rb_shell_load_complete_cb), shell,
						 0);
			rhythmdb_load (shell->priv->db);
		}
	}
	g_signal_connect_object (G_OBJECT (shell->priv->db),
				 "entry_added",
				 G_CALLBACK (rb_shell_db_changed_cb),
				 shell, 0);
	g_signal_connect_object (G_OBJECT (shell->priv->db),
				 "entry_changed",
				 G_CALLBACK (rb_shell_db_changed_cb),
				 shell, 0);
	g_signal_connect_object (G_OBJECT (shell->priv->db),
				 "entry_deleted",
				 G_CALLBACK (rb_shell_db_changed_cb),
				 shell, 0);

	if (!rhythmdb_exists && eel_gconf_get_boolean (CONF_FIRST_TIME)) {
		rb_debug ("loading legacy library db");
		g_signal_connect_object (G_OBJECT (shell->priv->db), "legacy-load-complete",
					 G_CALLBACK (rb_shell_legacy_load_complete_cb), shell,
					 0);
		rhythmdb_load_legacy (shell->priv->db);
	}

	rb_debug ("shell: setting up tray icon");
	tray_deleted_cb (NULL, NULL, shell);

	bonobo_ui_component_add_verb_list_with_data (shell->priv->ui_component,
						     rb_shell_verbs,
						     shell);
	rb_bonobo_add_listener_list_with_data (shell->priv->ui_component,
					       rb_shell_listeners,
					       shell);

	/* initialize shell services */
	rb_debug ("shell: initializing shell services");

	{
		BonoboUIComponent *tray_component;
		g_object_get (G_OBJECT (shell->priv->tray_icon), "tray_component",
			      &tray_component, NULL);

		shell->priv->player_shell = rb_shell_player_new (shell->priv->ui_component,
								 tray_component);
	}
	g_signal_connect (G_OBJECT (shell->priv->player_shell), 
			  "notify::repeat", 
			  G_CALLBACK (rb_shell_property_changed_generic_cb), 
			  shell);
	g_signal_connect (G_OBJECT (shell->priv->player_shell), 
			  "notify::shuffle", 
			  G_CALLBACK (rb_shell_property_changed_generic_cb), 
			  shell);
	g_signal_connect (G_OBJECT (shell->priv->player_shell), 
			  "notify::playing", 
			  G_CALLBACK (rb_shell_property_changed_generic_cb), 
			  shell);
	g_signal_connect (G_OBJECT (shell->priv->player_shell),
			  "window_title_changed",
			  G_CALLBACK (rb_shell_player_window_title_changed_cb),
			  shell);
	g_signal_connect (G_OBJECT (shell->priv->player_shell),
			  "duration_changed",
			  G_CALLBACK (rb_shell_player_duration_changed_cb),
			  shell);
	shell->priv->clipboard_shell = rb_shell_clipboard_new (shell->priv->ui_component,
							       shell->priv->db);
	shell->priv->source_header = rb_source_header_new (shell->priv->ui_component);

	shell->priv->paned = gtk_hpaned_new ();

	shell->priv->sourcelist = rb_sourcelist_new ();
	g_signal_connect (G_OBJECT (shell->priv->sourcelist), "drop_received",
			  G_CALLBACK (sourcelist_drag_received_cb), shell);
	g_signal_connect (G_OBJECT (shell->priv->sourcelist), "source_activated",
			  G_CALLBACK (source_activated_cb), shell);
	g_signal_connect (G_OBJECT (shell->priv->sourcelist), "show_popup",
			  G_CALLBACK (rb_shell_show_popup_cb), shell);

	shell->priv->statusbar = rb_statusbar_new (shell->priv->db,
						   shell->priv->ui_component,
						   shell->priv->player_shell);

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
 	shell->priv->hsep = gtk_hseparator_new ();
 	gtk_box_pack_start (GTK_BOX (vbox), shell->priv->hsep, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), shell->priv->paned, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (shell->priv->statusbar), FALSE, TRUE, 0);

	bonobo_window_set_contents (win, vbox);

	gtk_widget_show_all (vbox);

	rb_debug ("shell: adding gconf notification");
	/* sync state */
	eel_gconf_notification_add (CONF_UI_SOURCELIST_HIDDEN,
				    (GConfClientNotifyFunc) sourcelist_visibility_changed_cb,
				    shell);
	eel_gconf_notification_add (CONF_UI_SMALL_DISPLAY,
				    (GConfClientNotifyFunc) smalldisplay_changed_cb,
				    shell);
	eel_gconf_notification_add (CONF_STATE_PANED_POSITION,
				    (GConfClientNotifyFunc) paned_changed_cb,
				    shell);

	rb_debug ("shell: syncing with gconf");
	rb_shell_sync_sourcelist_visibility (shell);

	shell->priv->load_error_dialog = rb_load_failure_dialog_new ();
	shell->priv->show_db_errors = FALSE;
	gtk_widget_hide (shell->priv->load_error_dialog);

	shell->priv->supported_media_extensions = monkey_media_get_supported_filename_extensions ();

	g_signal_connect (G_OBJECT (shell->priv->db), "error",
			  G_CALLBACK (rb_shell_db_error_cb), shell);

	g_signal_connect (G_OBJECT (shell->priv->load_error_dialog), "response",
			  G_CALLBACK (rb_shell_load_failure_dialog_response_cb), shell);

	/* initialize sources */
	rb_debug ("shell: creating library source");
	shell->priv->library_source = rb_library_source_new (shell->priv->db);
	rb_shell_append_source (shell, shell->priv->library_source);

	rb_debug ("shell: creating iradio source");
	shell->priv->iradio_source = RB_IRADIO_SOURCE (rb_iradio_source_new (shell->priv->db));
	
	rb_shell_append_source (shell, RB_SOURCE (shell->priv->iradio_source));

	shell->priv->playlist_manager = rb_playlist_manager_new (shell->priv->ui_component,
								 GTK_WINDOW (shell->priv->window),
								 shell->priv->db,
								 RB_SOURCELIST (shell->priv->sourcelist),
								 RB_LIBRARY_SOURCE (shell->priv->library_source),
								 RB_IRADIO_SOURCE (shell->priv->iradio_source));

	g_signal_connect (G_OBJECT (shell->priv->playlist_manager), "playlist_added",
			  G_CALLBACK (rb_shell_playlist_added_cb), shell);
	g_signal_connect (G_OBJECT (shell->priv->playlist_manager), "load_start",
			  G_CALLBACK (rb_shell_playlist_load_start_cb), shell);
	g_signal_connect (G_OBJECT (shell->priv->playlist_manager), "load_finish",
			  G_CALLBACK (rb_shell_playlist_load_finish_cb), shell);

	rb_shell_sync_window_state (shell);

	bonobo_ui_component_thaw (shell->priv->ui_component, NULL);

	rb_shell_select_source (shell, shell->priv->library_source); /* select this one by default */

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
	
	if (!g_object_get_data (G_OBJECT (shell), "rb-shell-no-registration")
	    && !g_object_get_data (G_OBJECT (shell), "rb-shell-dry-run")) {
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
				     "This probably means that you installed Rhythmbox in a "
				     "different prefix than bonobo-activation; this "
				     "warning is harmless, but IPC will not work."), msg);
			g_free (msg);
		
		}
		CORBA_exception_free (&ev);
		
		rb_debug ("Registered with Bonobo Activation");
	}

	/* GO GO GO! */
	rb_debug ("shell: syncing window state");
	rb_shell_sync_paned (shell);
	gtk_widget_show_all (GTK_WIDGET (shell->priv->tray_icon));

	/* Stop here if this is the first time. */
	if (!eel_gconf_get_boolean (CONF_FIRST_TIME)) {
		RBDruid *druid = rb_druid_new (shell->priv->db);
		gtk_widget_hide (GTK_WIDGET (shell->priv->window));
		rb_druid_show (druid);
		g_object_unref (G_OBJECT (druid));

		g_timeout_add (5000, (GSourceFunc) idle_save_state, shell);
	}
	
	rb_statusbar_sync_state (shell->priv->statusbar);
	rb_shell_sync_smalldisplay (shell);
	gtk_widget_show (GTK_WIDGET (shell->priv->window));
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
	gboolean small = eel_gconf_get_boolean (CONF_UI_SMALL_DISPLAY);

	g_return_val_if_fail (widget != NULL, FALSE);
	rb_debug ("caught window state change");

	switch (event->type)
	{
	case GDK_WINDOW_STATE:
		if (small == TRUE)
			gtk_window_unmaximize (GTK_WINDOW (shell->priv->window));
		else
			eel_gconf_set_boolean (CONF_STATE_WINDOW_MAXIMIZED,
					       event->window_state.new_window_state &
					       GDK_WINDOW_STATE_MAXIMIZED);
		break;
	case GDK_CONFIGURE:
		if (small == TRUE)
			eel_gconf_set_integer (CONF_STATE_SMALL_WIDTH, event->configure.width);
		else
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
	int small_width = eel_gconf_get_integer (CONF_STATE_SMALL_WIDTH);
	int width = eel_gconf_get_integer (CONF_STATE_WINDOW_WIDTH); 
	int height = eel_gconf_get_integer (CONF_STATE_WINDOW_HEIGHT);
	gboolean maximized = eel_gconf_get_boolean (CONF_STATE_WINDOW_MAXIMIZED);
	gboolean small = eel_gconf_get_boolean (CONF_UI_SMALL_DISPLAY);
	GdkGeometry hints;
	if (small == TRUE)
	{
		hints.max_height = 0;
		hints.max_width = 3000;
		gtk_window_unmaximize (GTK_WINDOW (shell->priv->window));
		gtk_window_set_default_size (GTK_WINDOW (shell->priv->window),
					     small_width, 0);
		gtk_window_resize (GTK_WINDOW (shell->priv->window),
				   small_width, 1);
		gtk_window_set_geometry_hints (GTK_WINDOW (shell->priv->window),
						NULL,
						&hints,
						GDK_HINT_MAX_SIZE);
	} else {
		gtk_window_set_default_size (GTK_WINDOW (shell->priv->window),
					     width, height);
		gtk_window_resize (GTK_WINDOW (shell->priv->window),
				   width, height);
		gtk_window_set_geometry_hints (GTK_WINDOW (shell->priv->window),
						NULL,
						&hints,
						0);

		if (maximized)
			gtk_window_maximize (GTK_WINDOW (shell->priv->window));
		else
			gtk_window_unmaximize (GTK_WINDOW (shell->priv->window));
	}
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
source_activated_cb (RBSourceList *sourcelist,
		     RBSource *source,
		     RBShell *shell)
{
	rb_debug ("source activated");

	/* Stop the playing source, if any */
	rb_shell_player_set_playing_source (shell->priv->player_shell, NULL);

	/* Select the new one, and start it playing */
	rb_shell_select_source (shell, source);
	rb_shell_player_set_playing_source (shell->priv->player_shell, source);
	rb_shell_player_playpause (shell->priv->player_shell);
}

static void
rb_shell_db_error_cb (RhythmDB *db,
		      const char *uri, const char *msg,
		      RBShell *shell)
{
	GList *tem;
	GnomeVFSURI *vfsuri;
	gchar *basename;
	gssize baselen;

	rb_debug ("got db error, showing: %s",
		  shell->priv->show_db_errors ? "TRUE" : "FALSE");
	
	if (!shell->priv->show_db_errors)
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
	shell->priv->show_db_errors = FALSE;
}

static void
rb_shell_append_source (RBShell *shell,
			RBSource *source)
{
	shell->priv->sources
		= g_list_append (shell->priv->sources, source);

	g_signal_connect (G_OBJECT (source), "deleted",
			  G_CALLBACK (rb_shell_source_deleted_cb), shell);

	gtk_notebook_append_page (GTK_NOTEBOOK (shell->priv->notebook),
				  GTK_WIDGET (source),
				  gtk_label_new (""));
	gtk_widget_show (GTK_WIDGET (source));

	rb_sourcelist_append (RB_SOURCELIST (shell->priv->sourcelist),
			      source);
}

static void
rb_shell_playlist_added_cb (RBPlaylistManager *mgr, RBSource *source, RBShell *shell)
{
	rb_shell_append_source (shell, source);
}

static void
rb_shell_playlist_load_start_cb (RBPlaylistManager *mgr, RBShell *shell)
{
	shell->priv->show_db_errors = TRUE;
}

static void
rb_shell_playlist_load_finish_cb (RBPlaylistManager *mgr, RBShell *shell)
{
	shell->priv->show_db_errors = FALSE;
}

static void
rb_shell_source_deleted_cb (RBSource *source,
			    RBShell *shell)
{
	if (source == rb_shell_player_get_playing_source (shell->priv->player_shell)) {
		rb_shell_player_set_playing_source (shell->priv->player_shell, NULL);
	}
	if (source == shell->priv->selected_source) {
		rb_shell_select_source (shell, shell->priv->library_source);
	}

	shell->priv->sources = g_list_remove (shell->priv->sources, source);

	rb_sourcelist_remove (RB_SOURCELIST (shell->priv->sourcelist), source);

	gtk_notebook_remove_page (GTK_NOTEBOOK (shell->priv->notebook),
				  gtk_notebook_page_num (GTK_NOTEBOOK (shell->priv->notebook),
							 GTK_WIDGET (source)));
}

static void
rb_shell_select_source (RBShell *shell,
			RBSource *source)
{
	RBEntryView *view;

	if (shell->priv->selected_source == source)
		return;

	rb_debug ("selecting source %p", source);
	
	if (shell->priv->selected_source) {
		view = rb_source_get_entry_view (shell->priv->selected_source);
		g_signal_handlers_disconnect_by_func (view, 
		                                      G_CALLBACK (rb_shell_entry_changed_cb),
						      shell);
	}
	shell->priv->selected_source = source;
	
	view = rb_source_get_entry_view (shell->priv->selected_source);
	g_signal_connect (view, "notify::playing-entry", 
			  G_CALLBACK(rb_shell_entry_changed_cb), shell);

	/* show source */
	gtk_notebook_set_current_page (GTK_NOTEBOOK (shell->priv->notebook),
				       gtk_notebook_page_num (GTK_NOTEBOOK (shell->priv->notebook), GTK_WIDGET (source)));

	rb_sourcelist_select (RB_SOURCELIST (shell->priv->sourcelist),
			      source);
	
	/* update services */
	rb_shell_clipboard_set_source (shell->priv->clipboard_shell,
				       RB_SOURCE (source));
	rb_shell_player_set_selected_source (shell->priv->player_shell,
					     RB_SOURCE (source));
	rb_source_header_set_source (shell->priv->source_header,
				     RB_SOURCE (source));
	rb_statusbar_set_source (shell->priv->statusbar,
				 RB_SOURCE (source));
	rb_playlist_manager_set_source (shell->priv->playlist_manager,
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
rb_shell_player_duration_changed_cb (RBShellPlayer *player,
				     const char *duration,
				     RBShell *shell)
{
	rb_shell_set_duration (shell, duration);
}

static void
rb_shell_set_duration (RBShell *shell, const char *duration)
{
	gboolean playing = rb_shell_player_get_playing (shell->priv->player_shell);
	char *tooltip;
		
	if (shell->priv->cached_title == NULL) 
		tooltip = g_strdup (_("Not playing"));
	else if (!playing) {
		/* Translators: the first %s is substituted by the song name, the second one is the elapsed and total time */
		tooltip = g_strdup_printf (_("%s\nPaused, %s"),
					 shell->priv->cached_title, duration);
	} else {
		/* Translators: the first %s is substituted by the song name, the second one is the elapsed and total time */
		tooltip = g_strdup_printf (_("%s\n%s"),
					   shell->priv->cached_title, duration);
	}
	
	rb_tray_icon_set_tooltip (shell->priv->tray_icon, tooltip);
	g_free (tooltip);
}

static void
rb_shell_set_window_title (RBShell *shell, const char *window_title)
{
	g_free (shell->priv->cached_title);
	
	if (window_title == NULL) {
		rb_debug ("clearing title");

		shell->priv->cached_title = NULL;
		
		gtk_window_set_title (GTK_WINDOW (shell->priv->window),
				      _("Music Player"));
	}
	else {
		gboolean playing = rb_shell_player_get_playing (shell->priv->player_shell);
		char *title;

		if (shell->priv->cached_title &&
		    !strcmp (shell->priv->cached_title, window_title)) {
			shell->priv->cached_title = g_strdup (window_title);
			return;
		}
		shell->priv->cached_title = g_strdup (window_title);

		rb_debug ("setting title to \"%s\"", window_title);
		if (!playing) {
			/* Translators: %s is the song name */
			title = g_strdup_printf (_("%s (Paused)"), window_title);
			gtk_window_set_title (GTK_WINDOW (shell->priv->window),
					      title);
			g_free (title);
		} else {
			gtk_window_set_title (GTK_WINDOW (shell->priv->window),
					      window_title);

/* This simply doesn't do anything at the moment, because the notification area
 * is broken.
 */
#if 0			
			egg_tray_icon_send_message (EGG_TRAY_ICON (shell->priv->tray_icon),
						    3000, window_title, -1);
#endif
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
rb_shell_view_smalldisplay_changed_cb (BonoboUIComponent *component,
				     const char *path,
				     Bonobo_UIComponent_EventType type,
				     const char *state,
				     RBShell *shell)
{
	eel_gconf_set_boolean (CONF_UI_SMALL_DISPLAY,
			       rb_bonobo_get_active (component, CMD_PATH_VIEW_SMALLDISPLAY));
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
#include "MAINTAINERS.old.tab"
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
	for (tem = authors; *tem != NULL; tem++)
		;
	*tem = _("Former Maintainers:");
	for (; *tem != NULL; tem++)
		;
	*tem = _("Contributors:");

	{
		const char *backend;
		GString *formats = g_string_new ("");
#ifdef HAVE_GSTREAMER
		backend = "GStreamer";
#else
		backend = "xine-lib";
#endif		
#ifdef HAVE_MP3
		g_string_append (formats, " MP3");
#endif
#ifdef HAVE_VORBIS
		g_string_append (formats, " Vorbis");
#endif
#ifdef HAVE_FLAC
		g_string_append (formats, " FLAC");
#endif
#ifdef HAVE_MP4
		g_string_append (formats, " MPEG-4");
#endif

		comment = g_strdup_printf (_("Music management and playback software for GNOME.\nAudio backend: %s\nAudio formats:%s\n"), backend, formats->str);

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

	shell->priv->show_db_errors = TRUE;
    
	while (*filecur != NULL) {
		if (g_utf8_validate (*filecur, -1, NULL)) {
			char *uri = gnome_vfs_get_uri_from_local_path (*filecur);
			rhythmdb_add_uri_async (shell->priv->db, uri);
			g_free (uri);
		}
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
	shell->priv->show_db_errors = TRUE;

	rb_library_source_add_location (RB_LIBRARY_SOURCE (shell->priv->library_source),
					GTK_WINDOW (shell->priv->window));
}

static void
rb_shell_cmd_new_station (BonoboUIComponent *component,
			  RBShell *shell,
			  const char *verbname)
{
	GtkWidget *dialog;
	rb_debug ("Got new station command");
	dialog = rb_new_station_dialog_new (shell->priv->db);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

static void
rb_shell_cmd_extract_cd (BonoboUIComponent *component,
			 RBShell *shell,
			 const char *verbname)
{
	GError *error = NULL;

	if (g_find_program_in_path ("sound-juicer") == NULL) {
		rb_error_dialog (_("To extract CDs you must install the Sound Juicer package."));
		return;
	}

	g_spawn_command_line_async ("sound-juicer", &error);

	if (error != NULL)
		rb_error_dialog (_("Couldn't run sound-juicer: %s"), error->message);

	g_clear_error (&error);
}

static void
rb_shell_quit (RBShell *shell)
{
	rb_debug ("Quitting");

	rb_shell_sync_state (shell);
	bonobo_object_unref (BONOBO_OBJECT (shell));
}

static void
rb_shell_load_complete_cb (RhythmDB *db, RBShell *shell)
{
	rb_debug ("load complete");
	GDK_THREADS_ENTER ();
	rb_playlist_manager_load_playlists (shell->priv->playlist_manager);
	GDK_THREADS_LEAVE ();
}

static void
rb_shell_legacy_load_complete_cb (RhythmDB *db, RBShell *shell)
{
	rb_debug ("legacy load complete");
	rb_playlist_manager_load_legacy_playlists (shell->priv->playlist_manager);
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
rb_shell_sync_smalldisplay (RBShell *shell)
{
	gboolean smalldisplay;

	smalldisplay = eel_gconf_get_boolean (CONF_UI_SMALL_DISPLAY);

	if (smalldisplay)
	{
		rb_bonobo_set_sensitive (shell->priv->ui_component,
					CMD_PATH_VIEW_SOURCELIST, FALSE);
  
		gtk_widget_hide (GTK_WIDGET (shell->priv->paned));
 		gtk_widget_hide (GTK_WIDGET (shell->priv->statusbar));
 		gtk_widget_hide (GTK_WIDGET (shell->priv->hsep));		
	} else {
		rb_bonobo_set_sensitive (shell->priv->ui_component,
					CMD_PATH_VIEW_SOURCELIST, TRUE);
  
		gtk_widget_show (GTK_WIDGET (shell->priv->paned));
 		rb_statusbar_sync_state (shell->priv->statusbar);
 		gtk_widget_show (GTK_WIDGET (shell->priv->hsep));	
	}

	rb_source_header_sync_control_state (shell->priv->source_header);
	rb_shell_player_sync_buttons (shell->priv->player_shell);

	rb_bonobo_set_active (shell->priv->ui_component,
			      CMD_PATH_VIEW_SMALLDISPLAY,
			      smalldisplay);

	rb_shell_sync_window_state (shell);
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
smalldisplay_changed_cb (GConfClient *client,
				  guint cnxn_id,
				  GConfEntry *entry,
				  RBShell *shell)
{
	rb_debug ("small display mode changed");
	rb_shell_sync_smalldisplay (shell);
}

static void
rb_shell_sync_paned (RBShell *shell)
{
	int actual_width, default_width, pos;
	gboolean maximized;

	maximized = eel_gconf_get_boolean (CONF_STATE_WINDOW_MAXIMIZED);
	pos = eel_gconf_get_integer (CONF_STATE_PANED_POSITION);

	rb_debug ("syncing paned to %d", pos);

	if (pos > 0) {
		if (maximized) {
			gtk_window_get_size (GTK_WINDOW (shell->priv->window), &actual_width, NULL);
			default_width =  eel_gconf_get_integer (CONF_STATE_WINDOW_WIDTH);
			if (actual_width != default_width)
				pos = pos * (float)actual_width/(float)default_width + 1;            
		}
		gtk_paned_set_position (GTK_PANED (shell->priv->paned),
					pos);
	}
}

static void
paned_size_allocate_cb (GtkWidget *widget,
			GtkAllocation *allocation,
		        RBShell *shell)
{
	int actual_width, default_width, pos;
	gboolean maximized;

	rb_debug ("paned size allocate");

	maximized = eel_gconf_get_boolean (CONF_STATE_WINDOW_MAXIMIZED);
	pos = gtk_paned_get_position (GTK_PANED (shell->priv->paned));
    
	if (maximized) {
		gtk_window_get_size (GTK_WINDOW (shell->priv->window), &actual_width, NULL);
		default_width =  eel_gconf_get_integer (CONF_STATE_WINDOW_WIDTH);
		pos = pos * (float)default_width/(float)actual_width;
	}
    
	eel_gconf_set_integer (CONF_STATE_PANED_POSITION, pos); 
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
sourcelist_drag_received_cb (RBSourceList *sourcelist,
			     RBSource *source,
			     GtkSelectionData *data,
			     RBShell *shell)
{
	if (source == NULL) 
		source = rb_playlist_manager_new_playlist (shell->priv->playlist_manager, 
							   FALSE);
	
	if (source != NULL) {
		rb_source_receive_drag (source, data);
		return;
	} 


/* 	if (data->type == gdk_atom_intern (RB_LIBRARY_DND_NODE_ID_TYPE, TRUE)) { */
/* 		long id; */
/* 		RBNode *node; */
/* 		RBPlaylistSource *playlist; */


/* 		id = atol (data->data); */
/* 		rb_debug ("got node id %d", id); */
/* 		node = rb_node_db_get_node_from_id (rb_library_get_node_db (shell->priv->library), id); */

/* 		if (node == NULL) */
/* 			return; */
			
		
/* 		playlist = create_playlist_with_name (shell, rb_node_get_property_string (node, RB_NODE_PROP_NAME)); */
/* 		if (playlist == NULL) */
/* 			return; */

/* 		rb_library_handle_songs (shell->priv->library, */
/* 					 node, */
/* 					 (GFunc) handle_songs_func, */
/* 					 playlist); */

/* 		shell->priv->playlists = g_list_append (shell->priv->playlists, playlist); */
/* 		rb_shell_append_source (shell, RB_SOURCE (playlist)); */
/* 	} else { */
/* 		GList *list; */

/* 		rb_debug ("got vfs data, len: %d", data->length); */
/* 		list = gnome_vfs_uri_list_parse (data->data); */
/* 		create_playlist (shell, CREATE_PLAYLIST_WITH_URI_LIST, list); */
/* 	} */
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
rb_shell_jump_to_entry_with_source (RBShell *shell, RBSource *source,
				    RhythmDBEntry *entry)
{
	RBEntryView *songs;

	g_return_if_fail (entry != NULL);

	if (source == NULL) {
		const char *location;
		rhythmdb_read_lock (shell->priv->db);
		location = rhythmdb_entry_get_string (shell->priv->db, entry,
						      RHYTHMDB_PROP_LOCATION);
		rhythmdb_read_unlock (shell->priv->db);
		if (rb_uri_is_iradio (location))
			source = RB_SOURCE (shell->priv->iradio_source);
		else
			source = RB_SOURCE (shell->priv->library_source);
	}

	songs = rb_source_get_entry_view (source);
	if (!rb_entry_view_get_entry_contained (songs, entry)) {
		rb_source_reset_filters (source);
		rb_source_header_clear_search (shell->priv->source_header);
	}

	rb_shell_select_source (shell, source);

	while (gtk_events_pending ())
		gtk_main_iteration ();

	if (!rb_entry_view_get_entry_contained (songs, entry)) {
		rb_source_search (shell->priv->selected_source, NULL);
	}

	rb_entry_view_scroll_to_entry (songs, entry);
	rb_entry_view_select_entry (songs, entry);
}

static void
rb_shell_jump_to_entry (RBShell *shell, RhythmDBEntry *entry)
{
	rb_shell_jump_to_entry_with_source (shell, NULL, entry);
}

static void
rb_shell_play_entry (RBShell *shell, RhythmDBEntry *entry)
{
	rb_shell_player_stop (shell->priv->player_shell);
	rb_shell_jump_to_entry_with_source (shell, NULL, entry);
	rb_shell_player_play_entry (shell->priv->player_shell, entry);
}

static void
rb_shell_jump_to_current (RBShell *shell)
{
	RBSource *source = rb_shell_player_get_playing_source (shell->priv->player_shell);
	RBEntryView *songs;
	RhythmDBEntry *playing;

	g_return_if_fail (source != NULL);

	songs = rb_source_get_entry_view (source);
	playing = rb_entry_view_get_playing_entry (songs);

	rb_shell_jump_to_entry_with_source (shell, source, playing);
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
tray_deleted_cb (GtkWidget *win, GdkEventAny *event, RBShell *shell)
{
	if (shell->priv->tray_icon) {
		rb_debug ("caught delete_event for tray icon");
		gtk_object_sink (GTK_OBJECT (shell->priv->tray_icon));
	}

	rb_debug ("creating new tray icon");
	shell->priv->tray_icon = rb_tray_icon_new (shell->priv->container,
						   shell->priv->ui_component,
						   shell->priv->db,
						   GTK_WINDOW (shell->priv->window));
	g_signal_connect (G_OBJECT (shell->priv->tray_icon), "delete_event",
			  G_CALLBACK (tray_deleted_cb), shell);
}

static void 
session_die_cb (GnomeClient *client, 
                RBShell *shell)
{
        rb_debug ("session die");
        rb_shell_quit (shell);
}

static gboolean
save_yourself_cb (GnomeClient *client, 
                  gint phase,
                  GnomeSaveStyle save_style,
                  gboolean shutdown,
                  GnomeInteractStyle interact_style,
                  gboolean fast,
                  RBShell *shell)
{
        rb_debug ("session save yourself");
        rb_shell_sync_state (shell);
        return TRUE;
}

static void
rb_shell_session_init (RBShell *shell)
{
        GnomeClient *client;

        client = gnome_master_client ();

        g_signal_connect (G_OBJECT (client), 
                          "save_yourself",
                          G_CALLBACK (save_yourself_cb),
                          shell);

        g_signal_connect (G_OBJECT (client),
                          "die",
                          G_CALLBACK (session_die_cb),
                          shell);
}
