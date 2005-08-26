/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  arch-tag: Implementation of main Rhythmbox shell
 *
 *  Copyright (C) 2002, 2003 Jorn Baayen
 *  Copyright (C) 2003, 2004 Colin Walters <walters@gnome.org>
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

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <config.h>
#include <libgnome/libgnome.h>
#include <libgnomeui/gnome-stock-icons.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-init.h>
#include <libgnome/gnome-program.h>
#include <libgnomeui/gnome-window-icon.h>
#include <libgnomeui/gnome-client.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/stat.h>

#include "rb-shell.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#ifdef WITH_RHYTHMDB_TREE
#include "rhythmdb-tree.h"
#elif defined(WITH_RHYTHMDB_GDA)
#include "rhythmdb-gda.h"
#else
#error "no database specified. configure broken?"
#endif
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
#include "rb-remote-proxy.h"
#include "rb-source-header.h"
#include "rb-tray-icon.h"
#include "rb-statusbar.h"
#include "rb-shell-preferences.h"
#include "rb-library-source.h"
#ifdef WITH_IPOD_SUPPORT
#include "rb-ipod-source.h"
#endif /* WITH_IPOD_SUPPORT */
#include "rb-load-failure-dialog.h"
#include "rb-new-station-dialog.h"
#include "rb-iradio-source.h"
#ifdef HAVE_AUDIOCD
#include "rb-audiocd-source.h"
#endif
#include "rb-shell-preferences.h"
#include "rb-playlist-source.h"
#include "eel-gconf-extensions.h"

#ifdef WITH_DASHBOARD
#include <glib.h>
#include <sys/time.h>
#include "dashboard.c"
#endif /* WITH_DASHBOARD */

static void rb_shell_class_init (RBShellClass *klass);
static void rb_shell_remote_proxy_init (RBRemoteProxyIface *iface);
static void rb_shell_init (RBShell *shell);
static GObject *rb_shell_constructor (GType type, guint n_construct_properties,
				      GObjectConstructParam *construct_properties);
static void rb_shell_finalize (GObject *object);
static void rb_shell_set_property (GObject *object,
				   guint prop_id,
				   const GValue *value,
				   GParamSpec *pspec);
static void rb_shell_get_property (GObject *object,
				   guint prop_id,
				   GValue *value,
				   GParamSpec *pspec);
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
static void rb_shell_select_source_internal (RBShell *shell, RBSource *source);
static void rb_shell_append_source (RBShell *shell, RBSource *source);
static RBSource *rb_shell_get_source_by_entry_type (RBShell *shell, 
						    RhythmDBEntryType type);
static void source_selected_cb (RBSourceList *sourcelist,
				RBSource *source,
				RBShell *shell);
static void rb_shell_playing_source_changed_cb (RBShellPlayer *player,
						RBSource *source,
						RBShell *shell);
static void rb_shell_playing_entry_changed_cb (RBShellPlayer *player,
					       RhythmDBEntry *entry,
					       RBShell *shell);
static void source_activated_cb (RBSourceList *sourcelist,
				 RBSource *source,
				 RBShell *shell);
static void rb_shell_db_error_cb (RhythmDB *db,
				  const char *uri, const char *msg,
				  RBShell *shell); 
static void rb_shell_db_entry_added_cb (RhythmDB *db,
					RhythmDBEntry *entry,
					RBShell *shell);
static void rb_shell_load_failure_dialog_response_cb (GtkDialog *dialog,
						      int response_id,
						      RBShell *shell);

static void rb_shell_playlist_added_cb (RBPlaylistManager *mgr, RBSource *source, RBShell *shell);
static void rb_shell_playlist_created_cb (RBPlaylistManager *mgr, RBSource *source, RBShell *shell);
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
static void rb_shell_cmd_about (GtkAction *action,
		                RBShell *shell);
static void rb_shell_cmd_contents (GtkAction *action,
				   RBShell *shell);
static void rb_shell_cmd_quit (GtkAction *action,
			       RBShell *shell);
static void rb_shell_cmd_preferences (GtkAction *action,
		                      RBShell *shell);
static void rb_shell_cmd_add_folder_to_library (GtkAction *action,
						RBShell *shell);
static void rb_shell_cmd_add_file_to_library (GtkAction *action,
					      RBShell *shell);
static void rb_shell_cmd_new_station (GtkAction *action,
				      RBShell *shell);
static void rb_shell_cmd_extract_cd (GtkAction *action,
				     RBShell *shell);
static void rb_shell_cmd_current_song (GtkAction *action,
				       RBShell *shell);
static void rb_shell_jump_to_current (RBShell *shell);
static void rb_shell_jump_to_entry (RBShell *shell, RhythmDBEntry *entry);
static void rb_shell_jump_to_entry_with_source (RBShell *shell, RBSource *source,
						RhythmDBEntry *entry);
static void rb_shell_play_entry (RBShell *shell, RhythmDBEntry *entry);
static void rb_shell_quit (RBShell *shell);
static void rb_shell_cmd_view_all (GtkAction *action,
 				   RBShell *shell);
static void rb_shell_view_sourcelist_changed_cb (GtkAction *action,
						 RBShell *shell);
static void rb_shell_view_smalldisplay_changed_cb (GtkAction *action,
						 RBShell *shell);
static void rb_shell_load_complete_cb (RhythmDB *db, RBShell *shell);
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
static void rb_shell_sync_selected_source (RBShell *shell);

static void selected_source_changed_cb (GConfClient *client,
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
static gboolean tray_destroy_cb (GtkObject *object, RBShell *shell);
static void rb_shell_load_uri_impl (RBRemoteProxy *proxy, const char *uri, gboolean play);
static void rb_shell_select_uri_impl (RBRemoteProxy *proxy, const char *uri);
static void rb_shell_play_uri_impl (RBRemoteProxy *proxy, const char *uri);
static void rb_shell_grab_focus_impl (RBRemoteProxy *proxy);
static gboolean rb_shell_get_visibility_impl (RBRemoteProxy *proxy);
static void rb_shell_set_visibility_impl (RBRemoteProxy *proxy, gboolean visible);
static gboolean rb_shell_get_shuffle_impl (RBRemoteProxy *proxy);
static void rb_shell_set_shuffle_impl (RBRemoteProxy *proxy, gboolean shuffle);
static gboolean rb_shell_get_repeat_impl (RBRemoteProxy *proxy);
static void rb_shell_set_repeat_impl (RBRemoteProxy *proxy, gboolean repeat);
static void rb_shell_play_impl (RBRemoteProxy *proxy);
static void rb_shell_pause_impl (RBRemoteProxy *proxy);
static gboolean rb_shell_playing_impl (RBRemoteProxy *proxy);
static long rb_shell_get_playing_time_impl (RBRemoteProxy *proxy);
static void rb_shell_set_playing_time_impl (RBRemoteProxy *proxy, long time);
static void rb_shell_seek_impl (RBRemoteProxy *proxy, long offset);
static gchar *rb_shell_get_playing_uri_impl (RBRemoteProxy *proxy);
static gboolean rb_shell_get_song_info_impl (RBRemoteProxy *proxy,
                                             const gchar *uri,
                                             RBRemoteSong *song);
static void rb_shell_set_rating_impl (RBRemoteProxy *proxy, double rating);
static void rb_shell_jump_next_impl (RBRemoteProxy *proxy);
static void rb_shell_jump_previous_impl (RBRemoteProxy *proxy);
static void rb_shell_remote_quit_impl (RBRemoteProxy *proxy);
static GParamSpec *rb_shell_find_player_property_impl (RBRemoteProxy *proxy,
						       const gchar *property);
static void rb_shell_player_notify_handler_impl (RBRemoteProxy *proxy, 
						 GCallback c_handler, 
						 gpointer gobject);
static void rb_shell_set_player_property_impl (RBRemoteProxy *proxy,
					       const gchar *property,
					       GValue *value);
static void rb_shell_get_player_property_impl (RBRemoteProxy *proxy,
					       const gchar *property,
					       GValue *value);
static gchar *rb_shell_get_playing_source_impl (RBRemoteProxy *proxy);

static void rb_shell_toggle_mute_impl (RBRemoteProxy *proxy);

typedef RBSource *(*SourceCreateFunc)(RBShell *);

static SourceCreateFunc known_sources[] = {
	rb_library_source_new,
	rb_iradio_source_new,
#ifdef WITH_IPOD_SUPPORT
	rb_ipod_source_new,	
#endif /* WITH_IPOD_SUPPORT */
	NULL,
};



static gboolean save_yourself_cb (GnomeClient *client, 
                                  gint phase,
                                  GnomeSaveStyle save_style,
                                  gboolean shutdown,
                                  GnomeInteractStyle interact_style,
                                  gboolean fast,
                                  RBShell *shell);

static void session_die_cb (GnomeClient *client, RBShell *shell);
static void rb_shell_session_init (RBShell *shell);

enum
{
	PROP_NONE,
	PROP_ARGC,
	PROP_ARGV,
	PROP_NO_REGISTRATION,
	PROP_NO_UPDATE,
	PROP_DRY_RUN,
	PROP_RHYTHMDB_FILE,
	PROP_SELECTED_SOURCE,
	PROP_DB,
	PROP_UI_MANAGER
};

/* prefs */
#define CONF_STATE_WINDOW_WIDTH     CONF_PREFIX "/state/window_width"
#define CONF_STATE_WINDOW_HEIGHT    CONF_PREFIX "/state/window_height"
#define CONF_STATE_SMALL_WIDTH      CONF_PREFIX "/state/small_width"
#define CONF_STATE_WINDOW_MAXIMIZED CONF_PREFIX "/state/window_maximized"
#define CONF_STATE_PANED_POSITION   CONF_PREFIX "/state/paned_position"
#define CONF_STATE_ADD_DIR          CONF_PREFIX "/state/add_dir"
#define CONF_STATE_SELECTED_SOURCE  CONF_PREFIX "/state/selected_source"

struct RBShellPrivate
{
	GtkWidget *window;
	gboolean visible;

	GtkUIManager *ui_manager;
	GtkActionGroup *actiongroup;

	GtkWidget *paned;
	GtkWidget *sourcelist;
	GtkWidget *notebook;
	GtkWidget *hsep;

	GList *sources;
	GHashTable *sources_hash;

	guint async_state_save_id;

	gboolean shutting_down;
	gboolean load_complete;

	int argc;
	char **argv;
	gboolean no_registration;
	gboolean no_update;
	gboolean dry_run;
	char *rhythmdb_file;

	RhythmDB *db;
	char *pending_entry;

	RBShellPlayer *player_shell;
	RBShellClipboard *clipboard_shell;
	RBSourceHeader *source_header;
	RBStatusbar *statusbar;
	RBPlaylistManager *playlist_manager;

	GtkWidget *load_error_dialog;
	GList *supported_media_extensions;
	gboolean show_db_errors;

#ifdef HAVE_AUDIOCD
 	MonkeyMediaAudioCD *cd;
#endif

	RBSource *selected_source;

	GtkWidget *prefs;

	RBTrayIcon *tray_icon;

	char *cached_title;
	char *cached_duration;
};

static GtkActionEntry rb_shell_actions [] =
{
	{ "Music", NULL, N_("_Music") },
	{ "Edit", NULL, N_("_Edit") },
	{ "View", NULL, N_("_View") },
	{ "Control", NULL, N_("_Control") },
	{ "Help", NULL, N_("_Help") },

	{ "MusicNewInternetRadioStation", GTK_STOCK_NEW, N_("New _Internet Radio Station"), "<control>I",
	  N_("Create a new Internet Radio station"),
	  G_CALLBACK (rb_shell_cmd_new_station) },

	{ "MusicImportFolder", GTK_STOCK_OPEN, N_("_Import Folder..."), "<control>O",
	  N_("Choose folder to be added to the Library"),
	  G_CALLBACK (rb_shell_cmd_add_folder_to_library) },
	{ "MusicImportFile", NULL, N_("Import _File..."), NULL,
	  N_("Choose file to be added to the Library"),
	  G_CALLBACK (rb_shell_cmd_add_file_to_library) },
	{ "MusicImportCD", GTK_STOCK_CDROM, N_("Import _Audio CD..."), "<control>E",
	  N_("Extract and import songs from a CD"),
	  G_CALLBACK (rb_shell_cmd_extract_cd) },
	{ "HelpAbout", GTK_STOCK_ABOUT, N_("_About"), NULL,
	  N_("Show information about the music player"),
	  G_CALLBACK (rb_shell_cmd_about) },
	{ "HelpContents", GTK_STOCK_HELP, N_("_Contents"), "F1",
	  N_("Display music player help"),
	  G_CALLBACK (rb_shell_cmd_contents) },
	{ "MusicQuit", GTK_STOCK_QUIT, N_("_Quit"), "<control>Q",
	  N_("Quit the music player"),
	  G_CALLBACK (rb_shell_cmd_quit) },
	{ "EditPreferences", GTK_STOCK_PREFERENCES, N_("Prefere_nces"), NULL,
	  N_("Edit music player preferences"),
	  G_CALLBACK (rb_shell_cmd_preferences) },
	{ "ViewAll", NULL, N_("Show _All"), "<control>Y",
	  N_("Show all items in this music source"),
	  G_CALLBACK (rb_shell_cmd_view_all) },
	{ "ViewJumpToPlaying", GTK_STOCK_JUMP_TO, N_("_Jump to Playing Song"), "<control>J",
	  N_("Scroll the view to the currently playing song"),
	  G_CALLBACK (rb_shell_cmd_current_song) },
};
static guint rb_shell_n_actions = G_N_ELEMENTS (rb_shell_actions);

static GtkToggleActionEntry rb_shell_toggle_entries [] =
{
	{ "ViewSourceList", NULL, N_("Source _List"), "<control>L",
	  N_("Change the visibility of the source list"),
	  G_CALLBACK (rb_shell_view_sourcelist_changed_cb), TRUE },
	{ "ViewSmallDisplay", NULL, N_("_Small Display"), "<control>D",
	  N_("Make the main window smaller"),
	  G_CALLBACK (rb_shell_view_smalldisplay_changed_cb),
	}
};
static guint rb_shell_n_toggle_entries = G_N_ELEMENTS (rb_shell_toggle_entries);

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
		
		static const GInterfaceInfo rb_remote_proxy_info =
		{
			(GInterfaceInitFunc) rb_shell_remote_proxy_init,
			NULL,
			NULL
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "RBShell",
					       &info, 0);

		g_type_add_interface_static (type,
					     RB_TYPE_REMOTE_PROXY,
					     &rb_remote_proxy_info);
	}

	return type;
}

static void
rb_shell_class_init (RBShellClass *klass)
{
        GObjectClass *object_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

	object_class->set_property = rb_shell_set_property;
	object_class->get_property = rb_shell_get_property;
        object_class->finalize = rb_shell_finalize;
	object_class->constructor = rb_shell_constructor;

	g_object_class_install_property (object_class,
					 PROP_ARGC,
					 g_param_spec_int ("argc", 
							   "argc", 
							   "Argument count", 
							   0, 128,
							   0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_ARGV,
					 g_param_spec_pointer ("argv", 
							       "argv", 
							       "Arguments", 
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_NO_REGISTRATION,
					 g_param_spec_boolean ("no-registration", 
							       "no-registration", 
							       "Whether or not to register", 
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_NO_UPDATE,
					 g_param_spec_boolean ("no-update", 
							       "no-update", 
							       "Whether or not to update the library", 
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_DRY_RUN,
					 g_param_spec_boolean ("dry-run", 
							       "dry-run", 
							       "Whether or not this is a dry run", 
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_RHYTHMDB_FILE,
					 g_param_spec_string ("rhythmdb-file", 
							      "rhythmdb-file", 
							      "The RhythmDB file to use", 
#ifdef WITH_RHYTHMDB_TREE
							      "rhythmdb.xml",
#elif defined(WITH_RHYTHMDB_GDA)
							      "rhythmdb.sqlite", /* FIXME: correct extension? */
#endif
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_SELECTED_SOURCE,
					 g_param_spec_object ("selected-source", 
							      "selected-source", 
							      "Source which is currently selected", 
							      RB_TYPE_SOURCE,
							      G_PARAM_READABLE));

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db", 
							      "RhythmDB", 
							      "RhythmDB object", 
							      RHYTHMDB_TYPE,
							       G_PARAM_READABLE));

	g_object_class_install_property (object_class,
					 PROP_UI_MANAGER,
					 g_param_spec_object ("ui-manager", 
							      "GtkUIManager", 
							      "GtkUIManager object", 
							      GTK_TYPE_UI_MANAGER,
							       G_PARAM_READABLE));


}

static void
rb_shell_remote_proxy_init (RBRemoteProxyIface *iface)
{
	iface->load_uri = rb_shell_load_uri_impl;
	iface->select_uri = rb_shell_select_uri_impl;
	iface->play_uri = rb_shell_play_uri_impl;
	iface->grab_focus = rb_shell_grab_focus_impl;

	iface->get_visibility = rb_shell_get_visibility_impl;
	iface->set_visibility = rb_shell_set_visibility_impl;
	iface->get_shuffle = rb_shell_get_shuffle_impl;
	iface->set_shuffle = rb_shell_set_shuffle_impl;
	iface->get_repeat = rb_shell_get_repeat_impl;
	iface->set_repeat = rb_shell_set_repeat_impl;

	iface->play = rb_shell_play_impl;
	iface->pause = rb_shell_pause_impl;
	iface->playing = rb_shell_playing_impl;

	iface->get_playing_time = rb_shell_get_playing_time_impl;
	iface->set_playing_time = rb_shell_set_playing_time_impl;
	iface->seek = rb_shell_seek_impl;

	iface->get_playing_uri = rb_shell_get_playing_uri_impl;
	iface->get_song_info = rb_shell_get_song_info_impl;
	iface->set_rating = rb_shell_set_rating_impl;

	iface->jump_next = rb_shell_jump_next_impl;
	iface->jump_previous = rb_shell_jump_previous_impl;

	iface->quit = rb_shell_remote_quit_impl;

	iface->find_player_property = rb_shell_find_player_property_impl;
	iface->player_notify_handler = rb_shell_player_notify_handler_impl;
	iface->set_player_property = rb_shell_set_player_property_impl;
	iface->get_player_property = rb_shell_get_player_property_impl;

	iface->get_playing_source = rb_shell_get_playing_source_impl;

	iface->toggle_mute = rb_shell_toggle_mute_impl;
}

static void
rb_shell_init (RBShell *shell) 
{
	char *file;
	
	shell->priv = g_new0 (RBShellPrivate, 1);

	rb_dot_dir ();

	file = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_APP_PIXMAP, "rhythmbox.png", TRUE, NULL);
	if (file) {
		gnome_window_icon_set_default_from_file (file);
		g_free (file);
	}

        rb_shell_session_init (shell);

	eel_gconf_monitor_add (CONF_PREFIX);

}

static void
rb_shell_set_property (GObject *object,
		       guint prop_id,
		       const GValue *value,
		       GParamSpec *pspec)
{
	RBShell *shell = RB_SHELL (object);

	switch (prop_id)
	{
	case PROP_ARGC:
		shell->priv->argc = g_value_get_int (value);
		break;
	case PROP_ARGV:
		shell->priv->argv = g_value_get_pointer (value);
		break;
	case PROP_NO_REGISTRATION:
		shell->priv->no_registration = g_value_get_boolean (value);
		break;
	case PROP_NO_UPDATE:
		shell->priv->no_update = g_value_get_boolean (value);
		break;
	case PROP_DRY_RUN:
		shell->priv->dry_run = g_value_get_boolean (value);
		if (shell->priv->dry_run)
			shell->priv->no_registration = TRUE;			
		break;
	case PROP_RHYTHMDB_FILE:
		shell->priv->rhythmdb_file = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_shell_get_property (GObject *object,
		       guint prop_id,
		       GValue *value,
		       GParamSpec *pspec)
{
	RBShell *shell = RB_SHELL (object);

	switch (prop_id)
	{
	case PROP_ARGC:
		g_value_set_int (value, shell->priv->argc);
		break;
	case PROP_ARGV:
		g_value_set_pointer (value, shell->priv->argv);
		break;
	case PROP_NO_REGISTRATION:
		g_value_set_boolean (value, shell->priv->no_registration);
		break;
	case PROP_NO_UPDATE:
		g_value_set_boolean (value, shell->priv->no_update);
		break;
	case PROP_DRY_RUN:
		g_value_set_boolean (value, shell->priv->dry_run);
		break;
	case PROP_RHYTHMDB_FILE:
		g_value_set_string (value, shell->priv->rhythmdb_file);
		break;
	case PROP_SELECTED_SOURCE:
		g_value_set_object (value, shell->priv->selected_source);
		break;
	case PROP_DB:
		g_value_set_object (value, shell->priv->db);
		break;
	case PROP_UI_MANAGER:
		g_value_set_object (value, shell->priv->ui_manager);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gboolean
rb_shell_sync_state (RBShell *shell)
{
	if (shell->priv->dry_run) {
		rb_debug ("in dry-run mode, not syncing state");
		return FALSE;
	}

	if (!shell->priv->load_complete) {
		rb_debug ("load incomplete, not syncing state");
		return FALSE;
	}
	
	rb_debug ("saving playlists");
	rb_playlist_manager_save_playlists (shell->priv->playlist_manager, TRUE);

	rb_debug ("saving db");
	rhythmdb_save (shell->priv->db);
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

static gboolean
idle_save_rhythmdb (RhythmDB *db)
{
	rhythmdb_save (db);
	
	return FALSE;
}

static gboolean
idle_save_playlist_manager (RBPlaylistManager *mgr)
{
	rb_playlist_manager_save_playlists (mgr, FALSE);

	return TRUE;
}

static void
rb_shell_shutdown (RBShell *shell)
{
	GdkDisplay *display;

	if (shell->priv->shutting_down)
		return;
	shell->priv->shutting_down = TRUE;

	/* Hide the main window and tray icon as soon as possible */
	display = gtk_widget_get_display (shell->priv->window);
	gtk_widget_hide (shell->priv->window);
	gtk_widget_hide (GTK_WIDGET (shell->priv->tray_icon));
	gdk_display_sync (display);
}

static void
rb_shell_finalize (GObject *object)
{
        RBShell *shell = RB_SHELL (object);

	gtk_widget_hide (shell->priv->window);
	gtk_widget_hide (GTK_WIDGET (shell->priv->tray_icon));
	rb_shell_player_stop (shell->priv->player_shell);

	eel_gconf_monitor_remove (CONF_PREFIX);

	gtk_widget_destroy (GTK_WIDGET (shell->priv->load_error_dialog));
	g_list_free (shell->priv->supported_media_extensions);

	gtk_widget_destroy (GTK_WIDGET (shell->priv->tray_icon));
	
	g_list_free (shell->priv->sources);
	g_hash_table_destroy (shell->priv->sources_hash);

	g_object_unref (G_OBJECT (shell->priv->playlist_manager));

	g_object_unref (G_OBJECT (shell->priv->clipboard_shell));

	gtk_widget_destroy (shell->priv->window);

	rb_debug ("shutting down DB");
	rhythmdb_shutdown (shell->priv->db);

	rb_debug ("unreffing DB");
	g_object_unref (G_OBJECT (shell->priv->db));

	if (shell->priv->prefs != NULL)
		gtk_widget_destroy (shell->priv->prefs);
	
	g_free (shell->priv->rhythmdb_file);
	g_free (shell->priv);

        parent_class->finalize (G_OBJECT (shell));

	rb_debug ("shell shutdown complete");
}

RBShell *
rb_shell_new (int argc, char **argv, gboolean no_registration,
	      gboolean no_update, gboolean dry_run,
	      char *rhythmdb)
{
	RBShell *s;

	s = g_object_new (RB_TYPE_SHELL, "argc", argc, "argv", argv,
			  "no-registration", no_registration,
			  "no-update", no_update,
			  "dry-run", dry_run, "rhythmdb-file", rhythmdb, NULL);

	return s;
}

static GObject *
rb_shell_constructor (GType type, guint n_construct_properties,
		      GObjectConstructParam *construct_properties)
{
	RBShell *shell;
	RBShellClass *klass;
	GObjectClass *parent_class;  
	GtkWindow *win;
	GtkWidget *menubar;
	GtkWidget *vbox;
	gboolean rhythmdb_exists;
	RBSource *iradio_source;
	RBSource *library_source;
	int i = 0;
	GError *error = NULL;

	klass = RB_SHELL_CLASS (g_type_class_peek (RB_TYPE_SHELL));

	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));

	shell = RB_SHELL (parent_class->constructor (type, n_construct_properties,
						      construct_properties));

	rb_debug ("Constructing shell");

	/* initialize UI */
	win = GTK_WINDOW (gtk_window_new (GTK_WINDOW_TOPLEVEL));
	gtk_window_set_title (win, _("Music Player"));

	shell->priv->window = GTK_WIDGET (win);
	shell->priv->visible = TRUE;

	g_signal_connect_object (G_OBJECT (win), "window-state-event",
				 G_CALLBACK (rb_shell_window_state_cb),
				 shell, 0);

	g_signal_connect_object (G_OBJECT (win), "delete_event",
				 G_CALLBACK (rb_shell_window_delete_cb),
				 shell, 0);
  
	shell->priv->ui_manager = gtk_ui_manager_new ();

	shell->priv->actiongroup = gtk_action_group_new ("MainActions");
	gtk_action_group_set_translation_domain (shell->priv->actiongroup,
						 GETTEXT_PACKAGE);
	gtk_action_group_add_actions (shell->priv->actiongroup,
				      rb_shell_actions,
				      rb_shell_n_actions, shell);
	gtk_action_group_add_toggle_actions (shell->priv->actiongroup,
					     rb_shell_toggle_entries,
					     rb_shell_n_toggle_entries,
					     shell);
	/* Initialize the database */
	rb_debug ("creating database object");
	{
		char *pathname;

		if (shell->priv->rhythmdb_file)
			pathname = g_strdup (shell->priv->rhythmdb_file);
		else
			pathname = g_build_filename (rb_dot_dir (), "rhythmdb.xml", NULL);

		rhythmdb_exists = g_file_test (pathname, G_FILE_TEST_EXISTS);
		
#ifdef WITH_RHYTHMDB_TREE
		shell->priv->db = rhythmdb_tree_new (pathname);
#elif defined(WITH_RHYTHMDB_GDA)
		shell->priv->db = rhythmdb_gda_new (pathname);
#endif
		g_free (pathname);

		if (shell->priv->dry_run)
			g_object_set (G_OBJECT (shell->priv->db), "dry-run", TRUE, NULL);
		if (shell->priv->no_update)
			g_object_set (G_OBJECT (shell->priv->db), "no-update", TRUE, NULL);

		if (rhythmdb_exists) {
			g_signal_connect_object (G_OBJECT (shell->priv->db), "load-complete",
						 G_CALLBACK (rb_shell_load_complete_cb), shell,
						 0);
		} else {
			shell->priv->load_complete = TRUE;
		}
	}

	rb_debug ("shell: setting up tray icon");
	tray_destroy_cb (NULL, shell);

	/* initialize shell services */
	rb_debug ("shell: initializing shell services");

	shell->priv->player_shell = rb_shell_player_new (shell->priv->db,
							 shell->priv->ui_manager,
							 shell->priv->actiongroup);
	g_signal_connect_object (G_OBJECT (shell->priv->player_shell),
				 "playing-source-changed",
				 G_CALLBACK (rb_shell_playing_source_changed_cb),
				 shell, 0);
	g_signal_connect_object (G_OBJECT (shell->priv->player_shell),
				 "playing-song-changed",
				 G_CALLBACK (rb_shell_playing_entry_changed_cb),
				 shell, 0);
	g_signal_connect_object (G_OBJECT (shell->priv->player_shell),
				 "window_title_changed",
				 G_CALLBACK (rb_shell_player_window_title_changed_cb),
				 shell, 0);
	g_signal_connect_object (G_OBJECT (shell->priv->player_shell),
				 "duration_changed",
				 G_CALLBACK (rb_shell_player_duration_changed_cb),
				 shell, 0);
	shell->priv->clipboard_shell = rb_shell_clipboard_new (shell->priv->actiongroup,
							       shell->priv->db);
	shell->priv->source_header = rb_source_header_new (shell->priv->actiongroup);

	shell->priv->paned = gtk_hpaned_new ();

	shell->priv->sourcelist = rb_sourcelist_new (shell);
	g_signal_connect_object (G_OBJECT (shell->priv->sourcelist), "drop_received",
				 G_CALLBACK (sourcelist_drag_received_cb), shell, 0);
	g_signal_connect_object (G_OBJECT (shell->priv->sourcelist), "source_activated",
				 G_CALLBACK (source_activated_cb), shell, 0);
	g_signal_connect_object (G_OBJECT (shell->priv->sourcelist), "show_popup",
				 G_CALLBACK (rb_shell_show_popup_cb), shell, 0);

	shell->priv->statusbar = rb_statusbar_new (shell->priv->db,
						   shell->priv->actiongroup,
						   shell->priv->player_shell);
	g_object_set (shell->priv->player_shell, "statusbar", shell->priv->statusbar, NULL);

	g_signal_connect_object (G_OBJECT (shell->priv->sourcelist), "selected",
				 G_CALLBACK (source_selected_cb), shell, 0);

	vbox = gtk_vbox_new (FALSE, 4);
	shell->priv->notebook = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (shell->priv->notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (shell->priv->notebook), FALSE);
	g_signal_connect_object (G_OBJECT (shell->priv->notebook),
				 "size_allocate",
				 G_CALLBACK (paned_size_allocate_cb),
				 shell, 0);

	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (shell->priv->source_header), FALSE, TRUE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), shell->priv->notebook);

	gtk_paned_pack1 (GTK_PANED (shell->priv->paned), 
			 shell->priv->sourcelist, TRUE, TRUE);
	gtk_paned_pack2 (GTK_PANED (shell->priv->paned), vbox, TRUE, TRUE);

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 0);
 	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (shell->priv->player_shell), FALSE, TRUE, 0);	
 	shell->priv->hsep = gtk_hseparator_new ();
 	gtk_box_pack_start (GTK_BOX (vbox), shell->priv->hsep, FALSE, FALSE, 5);
	gtk_box_pack_start (GTK_BOX (vbox), shell->priv->paned, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (shell->priv->statusbar), FALSE, TRUE, 0);

	gtk_container_add (GTK_CONTAINER (win), vbox);
	gtk_widget_show_all (vbox);

	rb_debug ("shell: adding gconf notification");
	/* sync state */
	eel_gconf_notification_add (CONF_STATE_SELECTED_SOURCE,
				    (GConfClientNotifyFunc) selected_source_changed_cb,
				    shell);
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

	g_signal_connect_object (G_OBJECT (shell->priv->db), "error",
				 G_CALLBACK (rb_shell_db_error_cb), shell, 0);
	g_signal_connect_object (G_OBJECT (shell->priv->db), "entry-added",
				 G_CALLBACK (rb_shell_db_entry_added_cb), shell, 0);

	g_signal_connect_object (G_OBJECT (shell->priv->load_error_dialog), "response",
				 G_CALLBACK (rb_shell_load_failure_dialog_response_cb), shell, 0);

	/* initialize sources */
	while (known_sources[i] != NULL) {
		RBSource *source;

		source = known_sources[i] (shell);
		rb_shell_append_source (shell, RB_SOURCE (source));
		i++;
	}

	library_source = rb_shell_get_source_by_entry_type (shell, RHYTHMDB_ENTRY_TYPE_SONG);
	g_assert (library_source != NULL);

	iradio_source  = rb_shell_get_source_by_entry_type (shell, RHYTHMDB_ENTRY_TYPE_IRADIO_STATION);
	g_assert (iradio_source != NULL);

	/* Initialize playlist manager */
	rb_debug ("shell: creating playlist manager");
	shell->priv->playlist_manager = rb_playlist_manager_new (shell,
								 RB_SOURCELIST (shell->priv->sourcelist),
								 RB_LIBRARY_SOURCE (library_source),
								 RB_IRADIO_SOURCE (iradio_source));

	g_signal_connect_object (G_OBJECT (shell->priv->playlist_manager), "playlist_added",
				 G_CALLBACK (rb_shell_playlist_added_cb), shell, 0);
	g_signal_connect_object (G_OBJECT (shell->priv->playlist_manager), "playlist_created",
				 G_CALLBACK (rb_shell_playlist_created_cb), shell, 0);
	g_signal_connect_object (G_OBJECT (shell->priv->playlist_manager), "load_start",
				 G_CALLBACK (rb_shell_playlist_load_start_cb), shell, 0);
	g_signal_connect_object (G_OBJECT (shell->priv->playlist_manager), "load_finish",
				 G_CALLBACK (rb_shell_playlist_load_finish_cb), shell, 0);

	rb_debug ("shell: loading ui");
	gtk_ui_manager_insert_action_group (shell->priv->ui_manager,
					    shell->priv->actiongroup, 0);
	gtk_ui_manager_add_ui_from_file (shell->priv->ui_manager,
					 rb_file ("rhythmbox-ui.xml"), &error);
#if !defined(WITH_CD_BURNER_SUPPORT)
	/* Hide CD burner related items */
	{
		GtkWidget *w = gtk_ui_manager_get_widget (shell->priv->ui_manager,
							  "/MenuBar/MusicMenu/PlaylistMenu/MusicPlaylistBurnPlaylistMenu");
		g_object_set (G_OBJECT (w), "visible", FALSE, NULL);
		w = gtk_ui_manager_get_widget (shell->priv->ui_manager,
					       "/PlaylistSourcePopup/MusicPlaylistBurnPlaylistMenu");
		g_object_set (G_OBJECT (w), "visible", FALSE, NULL);
	}
#endif
	gtk_ui_manager_ensure_update (shell->priv->ui_manager);
	gtk_window_add_accel_group (GTK_WINDOW (shell->priv->window),
				    gtk_ui_manager_get_accel_group (shell->priv->ui_manager));
	menubar = gtk_ui_manager_get_widget (shell->priv->ui_manager, "/MenuBar");
	gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, FALSE, 0);
	gtk_box_reorder_child (GTK_BOX (vbox), menubar, 0);

	if (error != NULL) {
		g_warning ("Couldn't merge %s: %s",
			   rb_file ("rhythmbox-ui.xml"), error->message);
		g_clear_error (&error);
	}

	g_timeout_add (10000, (GSourceFunc) idle_save_playlist_manager, shell->priv->playlist_manager);
	
	rb_shell_sync_window_state (shell);

	rb_shell_select_source_internal (shell, library_source);

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

		g_signal_connect_object (G_OBJECT (shell->priv->cd), "cd_changed",
					 G_CALLBACK (audiocd_changed_cb), shell, 0);
        }
	else
		rb_debug ("No AudioCD device is available!");
#endif
	
	/* GO GO GO! */
	if (rhythmdb_exists) {
		rb_debug ("loading database");
		rhythmdb_load (shell->priv->db);
/* Disabled for now */
#if 0
		rb_debug ("adding db save-when-needed thread");
		g_timeout_add (10000, (GSourceFunc) idle_save_rhythmdb, shell->priv->db);
#endif
	}
	
	rb_debug ("shell: syncing window state");
	rb_shell_sync_paned (shell);
	gtk_widget_show_all (GTK_WIDGET (shell->priv->tray_icon));

	/* Stop here if this is the first time. */
	if (!eel_gconf_get_boolean (CONF_FIRST_TIME)) {
		RBDruid *druid;
		shell->priv->show_db_errors = TRUE;
		druid = rb_druid_new (shell->priv->db);
		gtk_widget_hide (GTK_WIDGET (shell->priv->window));
		rb_druid_show (druid);
		g_object_unref (G_OBJECT (druid));

		g_timeout_add (5000, (GSourceFunc) idle_save_state, shell);
	}
	
	rb_statusbar_sync_state (shell->priv->statusbar);
	rb_shell_sync_smalldisplay (shell);
	gtk_widget_show (GTK_WIDGET (shell->priv->window));

	return G_OBJECT (shell);
}

static gboolean
rb_shell_window_state_cb (GtkWidget *widget,
			  GdkEvent *event,
			  RBShell *shell)
{
	gboolean visible;

	g_return_val_if_fail (widget != NULL, FALSE);
	rb_debug ("caught window state change");

	if (event->type == GDK_WINDOW_STATE) {
		visible = ! ((event->window_state.new_window_state & GDK_WINDOW_STATE_ICONIFIED) ||
			     (event->window_state.new_window_state & GDK_WINDOW_STATE_WITHDRAWN));
		if (visible != shell->priv->visible) {
			shell->priv->visible = visible;
			g_signal_emit_by_name (RB_REMOTE_PROXY (shell), "visibility_changed", visible);
		}
	}

	return FALSE;
}

static gboolean
rb_shell_window_delete_cb (GtkWidget *win,
			   GdkEventAny *event,
			   RBShell *shell)
{
	rb_debug ("window deleted");
	rb_shell_quit (shell);

	return TRUE;
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
		hints.min_height = -1;
		hints.min_width = -1;
		hints.max_height = -1;
		hints.max_width = 3000;
		gtk_window_unmaximize (GTK_WINDOW (shell->priv->window));
		gtk_window_set_default_size (GTK_WINDOW (shell->priv->window),
					     small_width, 0);
		gtk_window_resize (GTK_WINDOW (shell->priv->window),
				   small_width, 1);
		gtk_window_set_geometry_hints (GTK_WINDOW (shell->priv->window),
						NULL,
						&hints,
						GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
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
	/* Ignore error from here */
	rb_shell_player_playpause (shell->priv->player_shell, FALSE, NULL);
}

static void
rb_shell_db_error_cb (RhythmDB *db,
		      const char *uri, const char *msg,
		      RBShell *shell)
{
	rb_debug ("got db error, showing: %s",
		  shell->priv->show_db_errors ? "TRUE" : "FALSE");

	if (shell->priv->pending_entry && 
	    strcmp(uri, shell->priv->pending_entry) == 0) {
		g_free (shell->priv->pending_entry);
		shell->priv->pending_entry = NULL;
	}
	
	if (!shell->priv->show_db_errors)
		return;

	rb_load_failure_dialog_add (RB_LOAD_FAILURE_DIALOG (shell->priv->load_error_dialog),
				    uri, msg);
	gtk_widget_show (GTK_WIDGET (shell->priv->load_error_dialog));
}

static void
rb_shell_db_entry_added_cb (RhythmDB *db,
			    RhythmDBEntry *entry,
			    RBShell *shell)
{
	if (shell->priv->pending_entry == NULL)
		return;
	
	rb_debug ("got entry added for %s", entry->location);
	if (strcmp (entry->location, shell->priv->pending_entry) == 0) {
		rb_shell_play_entry (shell, entry);

		g_free (shell->priv->pending_entry);
		shell->priv->pending_entry = NULL;
	}
}

static void
rb_shell_load_failure_dialog_response_cb (GtkDialog *dialog,
					  int response_id,
					  RBShell *shell)
{
	rb_debug ("got response");
	shell->priv->show_db_errors = FALSE;
}

static RBSource *
rb_shell_get_source_by_entry_type (RBShell *shell, RhythmDBEntryType type)
{
	return g_hash_table_lookup (shell->priv->sources_hash, GINT_TO_POINTER (type));
}

void
rb_shell_register_entry_type_for_source (RBShell *shell,
					 RBSource *source,
					 RhythmDBEntryType type)
{
	if (shell->priv->sources_hash == NULL) {
		shell->priv->sources_hash = g_hash_table_new (g_direct_hash, 
							      g_direct_equal);
	}
	g_assert (g_hash_table_lookup (shell->priv->sources_hash, GINT_TO_POINTER (type)) == NULL);
	g_hash_table_insert (shell->priv->sources_hash, 
			     GINT_TO_POINTER (type), source);
}



static void
rb_shell_append_source (RBShell *shell,
			RBSource *source)
{
	char *search_text;
	
	shell->priv->sources
		= g_list_append (shell->priv->sources, source);

	g_signal_connect_object (G_OBJECT (source), "deleted",
				 G_CALLBACK (rb_shell_source_deleted_cb), shell, 0);

	gtk_notebook_append_page (GTK_NOTEBOOK (shell->priv->notebook),
				  GTK_WIDGET (source),
				  gtk_label_new (""));
	gtk_widget_show (GTK_WIDGET (source));

	rb_sourcelist_append (RB_SOURCELIST (shell->priv->sourcelist),
			      source);

	if (rb_source_can_search (source)) {
		search_text = eel_gconf_get_string (rb_source_get_search_key (source));
		rb_source_search (source, search_text);
		g_free (search_text);
	}
}

static void
rb_shell_playlist_added_cb (RBPlaylistManager *mgr, RBSource *source, RBShell *shell)
{
	rb_shell_append_source (shell, source);
}

static void
rb_shell_playlist_created_cb (RBPlaylistManager *mgr, RBSource *source, RBShell *shell)
{
	eel_gconf_set_boolean (CONF_UI_SMALL_DISPLAY, FALSE);
	eel_gconf_set_boolean (CONF_UI_SOURCELIST_HIDDEN, FALSE);
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
		RBSource *library_source;
		library_source = rb_shell_get_source_by_entry_type (shell, 
								    RHYTHMDB_ENTRY_TYPE_SONG);
		/* Set the gconf key */
		rb_shell_select_source (shell, library_source);
		/* Deal with it immediately, since we can't reference
		 * the old source anymore. */
		rb_shell_select_source_internal (shell, library_source);
	}

	shell->priv->sources = g_list_remove (shell->priv->sources, source);

	rb_sourcelist_remove (RB_SOURCELIST (shell->priv->sourcelist), source);

	gtk_notebook_remove_page (GTK_NOTEBOOK (shell->priv->notebook),
				  gtk_notebook_page_num (GTK_NOTEBOOK (shell->priv->notebook),
							 GTK_WIDGET (source)));
}

static void
rb_shell_playing_source_changed_cb (RBShellPlayer *player,
				    RBSource *source,
				    RBShell *shell)
{
	rb_debug ("playing source changed");
	rb_sourcelist_set_playing_source (RB_SOURCELIST (shell->priv->sourcelist),
					  source);
}

static void
rb_shell_playing_entry_changed_cb (RBShellPlayer *player,
				   RhythmDBEntry *entry,
				   RBShell *shell)
{
	RBRemoteSong song;
#ifdef WITH_DASHBOARD
	char *cluepacket;
#endif
	char *notifytitle;

	/* emit remote song_changed notification */
	song.title = g_strdup (rb_refstring_get (entry->title));
	song.artist = g_strdup (rb_refstring_get (entry->artist));
	song.genre = g_strdup (rb_refstring_get (entry->genre));
	song.album = g_strdup (rb_refstring_get (entry->album));
	song.uri = g_strdup (entry->location);

	song.track_number = entry->tracknum;
	song.disc_number = entry->discnum;
	song.duration = entry->duration;
	song.bitrate = entry->bitrate;
	song.filesize = entry->file_size;
	song.rating = entry->rating;
	song.play_count = entry->play_count;
	song.last_played = entry->last_played;

	g_signal_emit_by_name (RB_REMOTE_PROXY (shell), "song_changed", &song);

	notifytitle = g_strdup_printf ("%s by %s",
				       song.title, song.artist);
	rb_shell_hidden_notify (shell, 4000, _("Now Playing"), NULL, notifytitle);
	g_free (notifytitle);
				     
#ifdef WITH_DASHBOARD
	/* Send cluepacket to dashboard */
	cluepacket =
		dashboard_build_cluepacket_then_free_clues ("Music Player",
							TRUE, 
							"", 
							dashboard_build_clue (song.title, "song_title", 10),
							dashboard_build_clue (song.artist, "artist", 10),
							dashboard_build_clue (song.album, "album", 10),
							NULL);
	dashboard_send_raw_cluepacket (cluepacket);
	g_free (cluepacket);
#endif /* WITH_DASHBOARD */
}

static void
rb_shell_select_source (RBShell *shell,
			RBSource *source)
{
	char *internalname;

	g_object_get (G_OBJECT (source), "internal-name", &internalname, NULL);
	eel_gconf_set_string (CONF_STATE_SELECTED_SOURCE, internalname);
}

static void
rb_shell_select_source_internal (RBShell *shell,
				 RBSource *source)
{
	RBEntryView *view;

	if (shell->priv->selected_source == source)
		return;

	rb_debug ("selecting source %p", source);
	
	shell->priv->selected_source = source;
	
	view = rb_source_get_entry_view (shell->priv->selected_source);

	/* show source */
	gtk_notebook_set_current_page (GTK_NOTEBOOK (shell->priv->notebook),
				       gtk_notebook_page_num (GTK_NOTEBOOK (shell->priv->notebook), GTK_WIDGET (source)));

	g_signal_handlers_block_by_func (G_OBJECT (shell->priv->sourcelist),
					 G_CALLBACK (source_selected_cb),
					 shell);
	rb_sourcelist_select (RB_SOURCELIST (shell->priv->sourcelist),
			      source);
	g_signal_handlers_unblock_by_func (G_OBJECT (shell->priv->sourcelist),
					   G_CALLBACK (source_selected_cb),
					   shell);
	
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
	if (window_title == NULL) {
		rb_debug ("clearing title");

		g_free (shell->priv->cached_title);
		shell->priv->cached_title = NULL;
		
		gtk_window_set_title (GTK_WINDOW (shell->priv->window),
				      _("Music Player"));
	}
	else {
		gboolean playing = rb_shell_player_get_playing (shell->priv->player_shell);
		char *title;

		if (shell->priv->cached_title &&
		    !strcmp (shell->priv->cached_title, window_title)) {
			return;
		}
		g_free (shell->priv->cached_title);
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
rb_shell_view_sourcelist_changed_cb (GtkAction *action,
				     RBShell *shell)
{
	eel_gconf_set_boolean (CONF_UI_SOURCELIST_HIDDEN,
			       !gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}

static void
rb_shell_view_smalldisplay_changed_cb (GtkAction *action,
				       RBShell *shell)
{
	eel_gconf_set_boolean (CONF_UI_SMALL_DISPLAY,
			       gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}

static void
rb_shell_cmd_about (GtkAction *action,
		    RBShell *shell)
{
	const char **tem;
	GString *comment;
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

	pixbuf = gdk_pixbuf_new_from_file (rb_file ("about-logo.png"), NULL);

	authors[0] = _("Maintainers:");
	for (tem = authors; *tem != NULL; tem++)
		;
	*tem = _("Former Maintainers:");
	for (; *tem != NULL; tem++)
		;
	*tem = _("Contributors:");

	comment = g_string_new (_("Music management and playback software for GNOME."));

	gtk_show_about_dialog (GTK_WINDOW (shell->priv->window),
			       "name", "Rhythmbox",
			       "version", VERSION,
			       "copyright", "Copyright \xc2\xa9 2003, 2004 Colin Walters\nCopyright \xc2\xa9 2002, 2003 Jorn Baayen",
			       "comments", comment->str,
			       "authors", (const char **) authors,
			       "documenters", (const char **) documenters,
			       "translator-credits", strcmp (translator_credits, "translator_credits") != 0 ? translator_credits : NULL,
			       "logo", pixbuf,
			       NULL);
	g_string_free (comment, TRUE);
}

static void
rb_shell_cmd_quit (GtkAction *action,
		   RBShell *shell)
{
	rb_shell_quit (shell);
}

static void
rb_shell_cmd_contents (GtkAction *action,
		       RBShell *shell)
{
	GError *error = NULL;

	gnome_help_display ("rhythmbox.xml", NULL, &error);

	if (error != NULL) {
		rb_error_dialog (NULL, _("Couldn't display help"),
				 "%s", error->message);
		g_error_free (error);
	}
}

static void
rb_shell_cmd_preferences (GtkAction *action,
		          RBShell *shell)
{
	if (shell->priv->prefs == NULL) {
		shell->priv->prefs = rb_shell_preferences_new (shell->priv->sources);

		gtk_window_set_transient_for (GTK_WINDOW (shell->priv->prefs),
					      GTK_WINDOW (shell->priv->window));
	}

	gtk_widget_show_all (shell->priv->prefs);
}

static void
add_to_library_response_cb (GtkDialog *dialog,
			    int response_id,
			    RBShell *shell)
{
	char *current_dir = NULL;
	GSList *uri_list = NULL, *uris = NULL;

	if (response_id != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	current_dir = gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (dialog));
	eel_gconf_set_string (CONF_STATE_ADD_DIR, current_dir);

	uri_list = gtk_file_chooser_get_uris (GTK_FILE_CHOOSER (dialog));
	if (uri_list == NULL) {
		uri_list = g_slist_append (uri_list, g_strdup (current_dir));
	}

	shell->priv->show_db_errors = TRUE;

	for (uris = uri_list; uris; uris = uris->next) {
		rhythmdb_add_uri (shell->priv->db, (char *)uris->data);
	}
	g_slist_foreach (uri_list, (GFunc)g_free, NULL);
	g_slist_free (uri_list);
	g_free (current_dir);
	gtk_widget_destroy (GTK_WIDGET (dialog));
	g_timeout_add (10000, (GSourceFunc) idle_save_rhythmdb, shell->priv->db);
}

static void
rb_shell_cmd_add_folder_to_library (GtkAction *action,
				    RBShell *shell)
{
	char * dir = eel_gconf_get_string (CONF_STATE_ADD_DIR);
	GtkWidget *dialog;
    
	dialog = rb_file_chooser_new (_("Import Folder into Library"),
			              GTK_WINDOW (shell->priv->window),
				      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
				      TRUE);
	if (dir)
		gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (dialog),
							 dir);
	

	g_signal_connect_object (G_OBJECT (dialog),
				 "response",
				 G_CALLBACK (add_to_library_response_cb),
				 shell, 0);
}

static void
rb_shell_cmd_add_file_to_library (GtkAction *action,
				  RBShell *shell)
{
	char * dir = eel_gconf_get_string (CONF_STATE_ADD_DIR);
	GtkWidget *dialog;
    
	dialog = rb_file_chooser_new (_("Import File into Library"),
			              GTK_WINDOW (shell->priv->window),
				      GTK_FILE_CHOOSER_ACTION_OPEN,
				      TRUE);
	if (dir)
		gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (dialog),
							 dir);
	

	g_signal_connect_object (G_OBJECT (dialog),
				 "response",
				 G_CALLBACK (add_to_library_response_cb),
				 shell, 0);
}

static void
rb_shell_cmd_new_station (GtkAction *action,
			  RBShell *shell)
{
	GtkWidget *dialog;
	rb_debug ("Got new station command");
	dialog = rb_new_station_dialog_new (shell->priv->db);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

static void
rb_shell_cmd_extract_cd (GtkAction *action,
			 RBShell *shell)
{
	GError *error = NULL;

	if (g_find_program_in_path ("sound-juicer") == NULL) {
		rb_error_dialog (GTK_WINDOW (shell->priv->window),
				 _("CD Ripper not found"),
				 _("To extract CDs you must install the Sound Juicer CD Ripper package."));
		return;
	}

	g_spawn_command_line_async ("sound-juicer", &error);

	if (error != NULL)
		rb_error_dialog (GTK_WINDOW (shell->priv->window),
				 _("Couldn't run CD Ripper"),
				 _("An error occurred while running sound-juicer: %s"),
				 error->message);

	g_clear_error (&error);
}

static void
rb_shell_quit (RBShell *shell)
{
	rb_debug ("Quitting");

	rb_shell_shutdown (shell);
	rb_shell_sync_state (shell);
	g_object_unref (G_OBJECT (shell));
}

static gboolean
idle_handle_load_complete (RBShell *shell)
{
	GDK_THREADS_ENTER ();

	rb_debug ("load complete");

	rb_playlist_manager_load_playlists (shell->priv->playlist_manager);
	rb_shell_sync_selected_source (shell);
	shell->priv->load_complete = TRUE;

	GDK_THREADS_LEAVE ();

	return FALSE;
}

static void
rb_shell_load_complete_cb (RhythmDB *db, RBShell *shell)
{
	g_idle_add ((GSourceFunc) idle_handle_load_complete, shell);
}

static void
rb_shell_sync_sourcelist_visibility (RBShell *shell)
{
	gboolean visible;
	GtkAction *action;

	visible = !eel_gconf_get_boolean (CONF_UI_SOURCELIST_HIDDEN);

	if (visible)
		gtk_widget_show (GTK_WIDGET (shell->priv->sourcelist));
	else
		gtk_widget_hide (GTK_WIDGET (shell->priv->sourcelist));

	action = gtk_action_group_get_action (shell->priv->actiongroup,
					      "ViewSourceList");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
				      visible);
}

static void
rb_shell_sync_smalldisplay (RBShell *shell)
{
	gboolean smalldisplay;
	GtkAction *action;

	smalldisplay = eel_gconf_get_boolean (CONF_UI_SMALL_DISPLAY);

	action = gtk_action_group_get_action (shell->priv->actiongroup,
					      "ViewSourceList");

	if (smalldisplay) {
		g_object_set (G_OBJECT (action), "sensitive", FALSE, NULL);
  
		gtk_widget_hide (GTK_WIDGET (shell->priv->paned));
 		gtk_widget_hide (GTK_WIDGET (shell->priv->statusbar));
 		gtk_widget_hide (GTK_WIDGET (shell->priv->hsep));		
	} else {
		g_object_set (G_OBJECT (action), "sensitive", TRUE, NULL);
  
		gtk_widget_show (GTK_WIDGET (shell->priv->paned));
 		rb_statusbar_sync_state (shell->priv->statusbar);
 		gtk_widget_show (GTK_WIDGET (shell->priv->hsep));	
	}

	rb_source_header_sync_control_state (shell->priv->source_header);
	rb_shell_player_sync_buttons (shell->priv->player_shell);

	action = gtk_action_group_get_action (shell->priv->actiongroup,
					      "ViewSmallDisplay");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
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
rb_shell_sync_selected_source (RBShell *shell)
{
	char *internalname;
	GList *tmp;

	internalname = eel_gconf_get_string (CONF_STATE_SELECTED_SOURCE);
	g_return_if_fail (internalname);

	for (tmp = shell->priv->sources; tmp ; tmp = tmp->next) {
		const char *tmpname;
		g_object_get (G_OBJECT (tmp->data), "internal-name", &tmpname, NULL);
		if (!strcmp (internalname, tmpname)) {
			gboolean visible;
			g_assert (tmp->data != NULL);
			g_object_get (G_OBJECT (tmp->data), 
				      "visibility", &visible, 
				      NULL);
			if (visible != FALSE) {
				g_free (internalname);
				rb_shell_select_source_internal (shell,
								 tmp->data);
				return;

			}
		}
	}
	rb_shell_select_source_internal (shell, rb_shell_get_source_by_entry_type (shell, RHYTHMDB_ENTRY_TYPE_SONG));
	g_free (internalname);
}


static void
selected_source_changed_cb (GConfClient *client,
			    guint cnxn_id,
			    GConfEntry *entry,
			    RBShell *shell)
{
	rb_shell_sync_selected_source (shell);
}

static void
sourcelist_drag_received_cb (RBSourceList *sourcelist,
			     RBSource *source,
			     GtkSelectionData *data,
			     RBShell *shell)
{
        if (source == NULL) {
		gboolean smart = (data->type != gdk_atom_intern ("text/uri-list", TRUE));
		source = rb_playlist_manager_new_playlist (shell->priv->playlist_manager, 
							   NULL, smart);
        }

        if (source != NULL) {
                rb_source_receive_drag (source, data);
        }

}

static void
rb_shell_cmd_current_song (GtkAction *action,
			   RBShell *shell)
{
	rb_debug ("current song");

	rb_shell_jump_to_current (shell);
}

static void
rb_shell_cmd_view_all (GtkAction *action,
		       RBShell *shell)
{
	rb_debug ("view all");

	rb_source_reset_filters (shell->priv->selected_source);
	rb_source_header_clear_search (shell->priv->source_header);
}

static void
rb_shell_jump_to_entry_with_source (RBShell *shell, RBSource *source,
				    RhythmDBEntry *entry)
{
	RBEntryView *songs;

	g_return_if_fail (entry != NULL);

	if (source == NULL) {
		if (rb_uri_is_iradio (entry->location)) {
			RBSource *iradio_source;
			iradio_source = rb_shell_get_source_by_entry_type (shell, 
									    RHYTHMDB_ENTRY_TYPE_IRADIO_STATION);
			source = RB_SOURCE (iradio_source);
		} else {
			RBSource *library_source;
			library_source = rb_shell_get_source_by_entry_type (shell, 
									    RHYTHMDB_ENTRY_TYPE_SONG);			
			source = RB_SOURCE (library_source);
		}
	}

	songs = rb_source_get_entry_view (source);
	if (!rb_entry_view_get_entry_contained (songs, entry)) {
		rb_source_reset_filters (source);
		rb_source_header_clear_search (shell->priv->source_header);
	}

	rb_shell_select_source (shell, source);

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

static gboolean
tray_destroy_cb (GtkObject *object, RBShell *shell)
{
	if (shell->priv->tray_icon) {
		rb_debug ("caught destroy event for tray icon");
		gtk_object_sink (object);
		shell->priv->tray_icon = NULL;
	}

	rb_debug ("creating new tray icon");
	shell->priv->tray_icon = rb_tray_icon_new (shell->priv->ui_manager,
						   shell->priv->actiongroup,
						   RB_REMOTE_PROXY (shell));
	g_signal_connect_object (G_OBJECT (shell->priv->tray_icon), "destroy",
				 G_CALLBACK (tray_destroy_cb), shell, 0);
 
 	gtk_widget_show_all (GTK_WIDGET (shell->priv->tray_icon));
 
 	return TRUE;
}

void
rb_shell_hidden_notify (RBShell *shell,
			guint timeout,
			const char *primary,
			GtkWidget *icon,
			const char *secondary)
{

	if (shell->priv->visible) {
		rb_debug ("shell is visible, not notifying");
		return;
	}

	rb_tray_icon_notify (shell->priv->tray_icon,
			     timeout, primary,
			     icon, secondary);
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

        g_signal_connect_object (G_OBJECT (client), 
				 "save_yourself",
				 G_CALLBACK (save_yourself_cb),
				 shell, 0);

        g_signal_connect_object (G_OBJECT (client),
				 "die",
				 G_CALLBACK (session_die_cb),
				 shell, 0);
}

static void
rb_shell_load_uri_impl (RBRemoteProxy *proxy, const char *uri, gboolean play)
{
	RBShell *shell = RB_SHELL (proxy);
	rb_shell_load_uri (shell, uri, play, NULL);
}

gboolean
rb_shell_load_uri (RBShell *shell, const char *uri, gboolean play, GError **error)
{
	/* FIXME should be sync and return errors */
	rhythmdb_add_uri (shell->priv->db, uri);

	if (play) {
		/* wait for this entry to appear (or until we
		 * get a load error for it), then play it.
		 *
		 * we only handle one entry here because
		 * we don't have a playback queue (yet).
		 */
		if (shell->priv->pending_entry != NULL) {
			g_free (shell->priv->pending_entry);
		}
		shell->priv->pending_entry = g_strdup (uri);
	}

	return TRUE;
}

GObject *
rb_shell_get_player (RBShell *shell)
{
	return G_OBJECT (shell->priv->player_shell);
}

const char *
rb_shell_get_player_path (RBShell *shell)
{
	return "/org/gnome/Rhythmbox/Player";
}

static void
rb_shell_select_uri_impl (RBRemoteProxy *proxy, const char *uri)
{
	RBShell *shell = RB_SHELL (proxy);
	RhythmDBEntry *entry;

	entry = rhythmdb_entry_lookup_by_location (shell->priv->db, uri);
	if (entry != NULL) {
		rb_shell_jump_to_entry (shell, entry);
	}
}

static void
rb_shell_play_uri_impl (RBRemoteProxy *proxy, const char *uri)
{
	RBShell       *shell = RB_SHELL (proxy);
	RhythmDBEntry *entry;

	entry = rhythmdb_entry_lookup_by_location (shell->priv->db, uri);

	if (entry != NULL) {
		rb_shell_player_play_entry (RB_SHELL_PLAYER (shell->priv->player_shell),
					    entry);
	}
}

gboolean
rb_shell_present (RBShell *shell, guint32 timestamp, GError **error)
{
	rb_debug ("presenting with timestamp %u", timestamp);
	gtk_widget_show (GTK_WIDGET (shell->priv->window));
	gtk_window_present_with_time (GTK_WINDOW (shell->priv->window), timestamp);
	return TRUE;
}

static void
rb_shell_grab_focus_impl (RBRemoteProxy *proxy)
{
	rb_shell_present (RB_SHELL (proxy), gtk_get_current_event_time (), NULL);
}

static gboolean
rb_shell_get_visibility_impl (RBRemoteProxy *proxy)
{
	RBShell *shell = RB_SHELL (proxy);
	return shell->priv->visible;
}

static void
rb_shell_set_visibility_impl (RBRemoteProxy *proxy, gboolean visible)
{
	RBShell *shell = RB_SHELL (proxy);

	rb_debug ("setting visibility %s current visibility %s",
		  visible ? "TRUE" : "FALSE",
		  shell->priv->visible ? "TRUE" : "FALSE");

	if (visible == shell->priv->visible)
		return;

	if (visible) {
		gtk_window_present (GTK_WINDOW (shell->priv->window));
	} else {
		gtk_widget_hide (shell->priv->window);
	}

	shell->priv->visible = visible;
	g_signal_emit_by_name (proxy, "visibility_changed", visible);
}

static gboolean
rb_shell_get_shuffle_impl (RBRemoteProxy *proxy)
{
	RBShellPlayer *player = RB_SHELL (proxy)->priv->player_shell;
	gboolean       shuffle, repeat;

	rb_shell_player_get_playback_state (player,
					    &shuffle,
					    &repeat);

	return shuffle;
}

static void
rb_shell_set_shuffle_impl (RBRemoteProxy *proxy, gboolean shuffle)
{
	RBShellPlayer *player = RB_SHELL (proxy)->priv->player_shell;
	gboolean       shuffle_old, repeat;

	rb_shell_player_get_playback_state (player,
					    &shuffle_old,
					    &repeat);

	rb_shell_player_set_playback_state (player,
					    shuffle,
					    repeat);
}

static gboolean
rb_shell_get_repeat_impl (RBRemoteProxy *proxy)
{
	RBShellPlayer *player = RB_SHELL (proxy)->priv->player_shell;
	gboolean       shuffle, repeat;

	rb_shell_player_get_playback_state (player,
					    &shuffle,
					    &repeat);

	return repeat;
}

static void
rb_shell_set_repeat_impl (RBRemoteProxy *proxy, gboolean repeat)
{
	RBShellPlayer *player = RB_SHELL (proxy)->priv->player_shell;
	gboolean       shuffle, repeat_old;

	rb_shell_player_get_playback_state (player,
					    &shuffle,
					    &repeat_old);

	rb_shell_player_set_playback_state (player,
					    shuffle,
					    repeat);
}

static void
rb_shell_play_impl (RBRemoteProxy *proxy)
{
	RBShellPlayer *player = RB_SHELL (proxy)->priv->player_shell;
	/* These interfaces are busted...need a way to signal errors */
	rb_shell_player_playpause (player, TRUE, NULL);
}

static void
rb_shell_pause_impl (RBRemoteProxy *proxy)
{
	RBShellPlayer *player = RB_SHELL (proxy)->priv->player_shell;
	/* These interfaces are busted...need a way to signal errors */
	rb_shell_player_playpause (player, TRUE, NULL);
}

static gboolean
rb_shell_playing_impl (RBRemoteProxy *proxy)
{
	RBShellPlayer *player = RB_SHELL (proxy)->priv->player_shell;
	return rb_shell_player_get_playing (player);
}

static long
rb_shell_get_playing_time_impl (RBRemoteProxy *proxy)
{
	RBShellPlayer *player = RB_SHELL (proxy)->priv->player_shell;
	return rb_shell_player_get_playing_time (player);
}

static void
rb_shell_set_playing_time_impl (RBRemoteProxy *proxy, long time)
{
	RBShellPlayer *player = RB_SHELL (proxy)->priv->player_shell;
	rb_shell_player_set_playing_time (player, time);
}

static gchar*
rb_shell_get_playing_uri_impl (RBRemoteProxy *proxy)
{
	RBShellPlayer *player;
	RhythmDBEntry *entry;

	player = RB_SHELL (proxy)->priv->player_shell;

	entry = rb_shell_player_get_playing_entry (player);

	if (entry != NULL && entry->location != NULL)
	    return g_strdup (entry->location);
	else
	    return NULL;
}

static gboolean
rb_shell_get_song_info_impl (RBRemoteProxy *proxy, const gchar *uri, RBRemoteSong *song)
{
	RBShell       *shell;
	RhythmDBEntry *entry;

	shell = RB_SHELL (proxy);

	entry = rhythmdb_entry_lookup_by_location (shell->priv->db, uri);

	if (entry == NULL)
		return FALSE;

	song->title = g_strdup (rb_refstring_get (entry->title));
	song->artist = g_strdup (rb_refstring_get (entry->artist));
	song->album = g_strdup (rb_refstring_get (entry->album));
	song->genre = g_strdup (rb_refstring_get (entry->genre));

	/* Should be the same as parameter uri */
	song->uri = g_strdup (entry->location);

	song->track_number = entry->tracknum;
	song->disc_number = entry->discnum;
	song->duration = entry->duration;
	song->bitrate = entry->bitrate;
	song->filesize = entry->file_size;
	song->rating = entry->rating;
	song->play_count = entry->play_count;
	song->last_played = entry->last_played;

	song->track_gain = entry->track_gain;
	song->track_peak = entry->track_peak;
	song->album_gain = entry->album_gain;
	song->album_peak = entry->album_peak;

	return TRUE;
}

static void
rb_shell_jump_next_impl (RBRemoteProxy *proxy)
{
	rb_shell_player_do_next (RB_SHELL (proxy)->priv->player_shell, NULL);
}

static void
rb_shell_jump_previous_impl (RBRemoteProxy *proxy)
{
	rb_shell_player_do_previous (RB_SHELL (proxy)->priv->player_shell, NULL);
}

static void
rb_shell_remote_quit_impl (RBRemoteProxy *proxy)
{
	rb_shell_quit (RB_SHELL (proxy));
}

static GParamSpec *
rb_shell_find_player_property_impl (RBRemoteProxy *proxy,
				    const gchar *property)
{
	RBShell *shell = RB_SHELL (proxy);
	GObjectClass *klass = G_OBJECT_CLASS (RB_SHELL_PLAYER_GET_CLASS (shell->priv->player_shell));
	return g_object_class_find_property (klass, property);
}

static void
rb_shell_player_notify_handler_impl (RBRemoteProxy *proxy,
				     GCallback c_handler,
				     gpointer gobject)
{
	RBShell *shell = RB_SHELL (proxy);
	g_signal_connect_object (G_OBJECT (shell->priv->player_shell),
				 "notify",
				 c_handler,
				 gobject, 0);
}

static void
rb_shell_set_player_property_impl (RBRemoteProxy *proxy,
				   const gchar *property,
				   GValue *value)
{
	RBShell *shell = RB_SHELL (proxy);
	g_object_set_property (G_OBJECT (shell->priv->player_shell), property, value);
}

static void
rb_shell_get_player_property_impl (RBRemoteProxy *proxy,
				   const gchar *property,
				   GValue *value)
{
	RBShell *shell = RB_SHELL (proxy);
	g_object_get_property (G_OBJECT (shell->priv->player_shell), property, value);
}

static gchar *
rb_shell_get_playing_source_impl (RBRemoteProxy *proxy)
{
	RBShellPlayer *player;
	RBSource *source;
	gchar *source_name;

	player = RB_SHELL (proxy)->priv->player_shell;
	source = rb_shell_player_get_playing_source (player);
	g_object_get (G_OBJECT (source), "name", &source_name, NULL);
	return source_name;
}

static void
rb_shell_seek_impl (RBRemoteProxy *proxy, long offset)
{
	RBShellPlayer *player;
	player = RB_SHELL (proxy)->priv->player_shell;
	rb_shell_player_seek (player, offset);
}

static void
rb_shell_set_rating_impl (RBRemoteProxy *proxy, double rating)
{
	RBShell *shell = RB_SHELL (proxy);
	RhythmDBEntry *entry;
	RBEntryView *view;
	RBSource *playing_source;

	rb_debug ("setting rating of playing entry to %f", rating);

	playing_source = rb_shell_player_get_playing_source (shell->priv->player_shell);
	if (playing_source != NULL) {
		view = rb_source_get_entry_view (playing_source);
		entry = rb_entry_view_get_playing_entry (view);
		if (entry != NULL) {
			GValue value = {0, };
			g_value_init (&value, G_TYPE_DOUBLE);
			g_value_set_double (&value, rating);

			rhythmdb_entry_set (shell->priv->db, entry, RHYTHMDB_PROP_RATING, &value);

			g_value_unset (&value);
		}
	}
}

static void
rb_shell_toggle_mute_impl (RBRemoteProxy *proxy)
{
	RBShellPlayer *player;
	player = RB_SHELL (proxy)->priv->player_shell;
	rb_shell_player_toggle_mute (player);
}
