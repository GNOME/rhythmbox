/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of Rhythmbox playlist management object
 *
 *  Copyright (C) 2003,2004 Colin Walters <walters@gnome.org>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <config.h>

#include <string.h>
#include <stdio.h>      /* rename() */
#include <unistd.h>     /* unlink() */

#include <libxml/tree.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>

#include "rb-playlist-manager.h"
#include "rb-playlist-source.h"
#include "rb-static-playlist-source.h"
#include "rb-auto-playlist-source.h"
#include "rb-play-queue-source.h"
#include "rb-recorder.h"
#include "rb-sourcelist.h"
#include "rb-sourcelist-model.h"
#include "rb-query-creator.h"
#include "totem-pl-parser.h"

#include "rb-file-helpers.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rhythmdb.h"
#include "rb-stock-icons.h"
#include "eel-gconf-extensions.h"
#include "rb-glade-helpers.h"

#define RB_PLAYLIST_MGR_VERSION (xmlChar *) "1.0"
#define RB_PLAYLIST_MGR_PL (xmlChar *) "rhythmdb-playlists"

static void rb_playlist_manager_class_init (RBPlaylistManagerClass *klass);
static void rb_playlist_manager_init (RBPlaylistManager *mgr);
static void rb_playlist_manager_finalize (GObject *object);
static void rb_playlist_manager_set_property (GObject *object,
					      guint prop_id,
					      const GValue *value,
					      GParamSpec *pspec);
static void rb_playlist_manager_get_property (GObject *object,
					      guint prop_id,
					      GValue *value,
					      GParamSpec *pspec);
static void rb_playlist_manager_cmd_load_playlist (GtkAction *action,
						   RBPlaylistManager *mgr);
static void rb_playlist_manager_cmd_save_playlist (GtkAction *action,
						   RBPlaylistManager *mgr);
static void rb_playlist_manager_cmd_burn_playlist (GtkAction *action,
						   RBPlaylistManager *mgr);
static void rb_playlist_manager_cmd_new_playlist (GtkAction *action,
						  RBPlaylistManager *mgr);
static void rb_playlist_manager_cmd_new_automatic_playlist (GtkAction *action,
							    RBPlaylistManager *mgr);
static void rb_playlist_manager_cmd_rename_playlist (GtkAction *action,
						     RBPlaylistManager *mgr);
static void rb_playlist_manager_cmd_delete_playlist (GtkAction *action,
						     RBPlaylistManager *mgr);
static void rb_playlist_manager_cmd_edit_automatic_playlist (GtkAction *action,
							     RBPlaylistManager *mgr);
static void rb_playlist_manager_cmd_queue_playlist (GtkAction *action,
						    RBPlaylistManager *mgr);
static gboolean reap_dead_playlist_threads (RBPlaylistManager *mgr);
static void rb_playlist_manager_playlist_entries_changed (GtkTreeModel *entry_view,
							  RhythmDBEntry *entry,
							  RBPlaylistManager *mgr);

struct RBPlaylistManagerPrivate
{
	RhythmDB *db;
	RBShell *shell;
	RBSource *selected_source;

	RBSourceList *sourcelist;

	GtkActionGroup *actiongroup;
	GtkUIManager *uimanager;

	RBLibrarySource *libsource;
	RBIRadioSource *iradio_source;
	GtkWindow *window;

	guint playlist_serial;

	RBStaticPlaylistSource *loading_playlist;

	char *firsturi;

	guint thread_reaper_id;

	GAsyncQueue *status_queue;
	gint outstanding_threads;

	GCond *saving_condition;
	GMutex *saving_mutex;

	gboolean exiting;
	gboolean saving;
	gboolean dirty;
};

#define RB_PLAYLIST_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PLAYLIST_MANAGER, RBPlaylistManagerPrivate))

enum
{
	PROP_0,
	PROP_SHELL,
	PROP_SOURCE,
	PROP_SOURCELIST,
	PROP_LIBRARY_SOURCE,
	PROP_IRADIO_SOURCE,
};

enum
{
	PLAYLIST_ADDED,
	PLAYLIST_CREATED,
	PLAYLIST_LOAD_START,
	PLAYLIST_LOAD_FINISH,
	LAST_SIGNAL,
};

static guint rb_playlist_manager_signals[LAST_SIGNAL] = { 0 };

static GtkActionEntry rb_playlist_manager_actions [] =
{
	/* Submenu of Music */
	{ "Playlist", NULL, N_("_Playlist") },

	{ "MusicPlaylistNewPlaylist", GNOME_MEDIA_PLAYLIST, N_("_New Playlist"), "<control>N",
	  N_("Create a new playlist"),
	  G_CALLBACK (rb_playlist_manager_cmd_new_playlist) },
	{ "MusicPlaylistNewAutomaticPlaylist", GNOME_MEDIA_AUTO_PLAYLIST, N_("New _Automatic Playlist..."), NULL,
	  N_("Create a new automatically updating playlist"),
	  G_CALLBACK (rb_playlist_manager_cmd_new_automatic_playlist) },
	{ "MusicPlaylistLoadPlaylist", NULL, N_("_Load from File..."), NULL,
	  N_("Choose a playlist to be loaded"),
	  G_CALLBACK (rb_playlist_manager_cmd_load_playlist) },
	{ "MusicPlaylistSavePlaylist", GTK_STOCK_SAVE_AS, N_("_Save to File..."), NULL,
	  N_("Save a playlist to a file"),
	  G_CALLBACK (rb_playlist_manager_cmd_save_playlist) },
	{ "MusicPlaylistBurnPlaylist", GTK_STOCK_CDROM, N_("_Create Audio CD..."), NULL,
	  N_("Create an audio CD from playlist"),
	  G_CALLBACK (rb_playlist_manager_cmd_burn_playlist) },
	{ "MusicPlaylistRenamePlaylist", NULL, N_("_Rename"), NULL,
	  N_("Rename playlist"),
	  G_CALLBACK (rb_playlist_manager_cmd_rename_playlist) },
	{ "MusicPlaylistDeletePlaylist", GTK_STOCK_REMOVE, N_("_Delete"), NULL,
	  N_("Delete playlist"),
	  G_CALLBACK (rb_playlist_manager_cmd_delete_playlist) },
	{ "EditAutomaticPlaylist", GTK_STOCK_PROPERTIES, N_("_Edit..."), NULL,
	  N_("Change this automatic playlist"),
	  G_CALLBACK (rb_playlist_manager_cmd_edit_automatic_playlist) },
	{ "QueuePlaylist", NULL, N_("_Queue All Tracks"), NULL,
	  N_("Add all tracks in this playlist to the queue"),
	  G_CALLBACK (rb_playlist_manager_cmd_queue_playlist) },
};
static guint rb_playlist_manager_n_actions = G_N_ELEMENTS (rb_playlist_manager_actions);

G_DEFINE_TYPE (RBPlaylistManager, rb_playlist_manager, G_TYPE_OBJECT)

static void
rb_playlist_manager_class_init (RBPlaylistManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_playlist_manager_finalize;

	object_class->set_property = rb_playlist_manager_set_property;
	object_class->get_property = rb_playlist_manager_get_property;

	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "RBSource",
							      "RBSource object",
							      RB_TYPE_SOURCE,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_SHELL,
					 g_param_spec_object ("shell",
							      "RBShell",
							      "RBShell object",
							      RB_TYPE_SHELL,
							      G_PARAM_READWRITE));


	g_object_class_install_property (object_class,
					 PROP_LIBRARY_SOURCE,
					 g_param_spec_object ("library_source",
							      "Library source",
							      "Library source",
							      RB_TYPE_LIBRARY_SOURCE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_IRADIO_SOURCE,
					 g_param_spec_object ("iradio-source",
							      "IRadioSource",
							      "IRadioSource",
							      RB_TYPE_IRADIO_SOURCE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_SOURCELIST,
					 g_param_spec_object ("sourcelist",
							      "RBSourceList",
							      "RBSourceList",
							      RB_TYPE_SOURCELIST,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	rb_playlist_manager_signals[PLAYLIST_ADDED] =
		g_signal_new ("playlist_added",
			      RB_TYPE_PLAYLIST_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlaylistManagerClass, playlist_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, G_TYPE_OBJECT);
	
	rb_playlist_manager_signals[PLAYLIST_CREATED] =
		g_signal_new ("playlist_created",
			      RB_TYPE_PLAYLIST_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlaylistManagerClass, playlist_created),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, G_TYPE_OBJECT);

	rb_playlist_manager_signals[PLAYLIST_LOAD_START] =
		g_signal_new ("load_start",
			      RB_TYPE_PLAYLIST_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlaylistManagerClass, load_start),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0, G_TYPE_NONE);
	rb_playlist_manager_signals[PLAYLIST_LOAD_FINISH] =
		g_signal_new ("load_finish",
			      RB_TYPE_PLAYLIST_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlaylistManagerClass, load_finish),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0, G_TYPE_NONE);

	g_type_class_add_private (klass, sizeof (RBPlaylistManagerPrivate));
}

static void
rb_playlist_manager_init (RBPlaylistManager *mgr)
{
	mgr->priv = RB_PLAYLIST_MANAGER_GET_PRIVATE (mgr);

	mgr->priv->status_queue = g_async_queue_new ();

	mgr->priv->saving_condition = g_cond_new ();
	mgr->priv->saving_mutex = g_mutex_new ();

	mgr->priv->saving = FALSE;
	mgr->priv->dirty = FALSE;

	mgr->priv->thread_reaper_id = g_idle_add ((GSourceFunc) reap_dead_playlist_threads, mgr);
}

static gboolean
reap_dead_playlist_threads (RBPlaylistManager *mgr)
{
	GObject *obj;

	while ((obj = g_async_queue_try_pop (mgr->priv->status_queue)) != NULL) {
		GDK_THREADS_ENTER ();
		g_object_unref (obj);
		g_atomic_int_add (&mgr->priv->outstanding_threads, -1);
		GDK_THREADS_LEAVE ();
	}

	GDK_THREADS_ENTER ();
	mgr->priv->thread_reaper_id = g_timeout_add (5000, (GSourceFunc) reap_dead_playlist_threads, mgr);
	GDK_THREADS_LEAVE ();
	
	return FALSE;
}

void
rb_playlist_manager_shutdown (RBPlaylistManager *mgr)
{
	g_return_if_fail (RB_IS_PLAYLIST_MANAGER (mgr));

	if (mgr->priv->exiting) {
		return;
	}
		
	mgr->priv->exiting = TRUE;

	g_source_remove (mgr->priv->thread_reaper_id);

	rb_debug ("%d outstanding threads", g_atomic_int_get (&mgr->priv->outstanding_threads));
	while (g_atomic_int_get (&mgr->priv->outstanding_threads) > 0) {
		GObject *obj = g_async_queue_pop (mgr->priv->status_queue);
		g_object_unref (obj);
		g_atomic_int_add (&mgr->priv->outstanding_threads, -1);
	}
}

static void
rb_playlist_manager_finalize (GObject *object)
{
	RBPlaylistManager *mgr;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PLAYLIST_MANAGER (object));

	rb_debug ("Finalizing playlist manager");

	mgr = RB_PLAYLIST_MANAGER (object);

	g_return_if_fail (mgr->priv != NULL);

	g_source_remove (mgr->priv->thread_reaper_id);
	
	g_async_queue_unref (mgr->priv->status_queue);

	g_mutex_free (mgr->priv->saving_mutex);
	g_cond_free (mgr->priv->saving_condition);

	G_OBJECT_CLASS (rb_playlist_manager_parent_class)->finalize (object);
}

static void
rb_playlist_manager_set_uimanager (RBPlaylistManager *mgr, 
				   GtkUIManager *uimanager)
{

	if (mgr->priv->uimanager != NULL) {
		if (mgr->priv->actiongroup != NULL) {
			gtk_ui_manager_remove_action_group (mgr->priv->uimanager,
							    mgr->priv->actiongroup);
		}
		g_object_unref (G_OBJECT (mgr->priv->uimanager));
		mgr->priv->uimanager = NULL;
	}

	mgr->priv->uimanager = uimanager;

	if (mgr->priv->actiongroup == NULL) {
		mgr->priv->actiongroup = gtk_action_group_new ("PlaylistActions");
		gtk_action_group_set_translation_domain (mgr->priv->actiongroup,
							 GETTEXT_PACKAGE);
		gtk_action_group_add_actions (mgr->priv->actiongroup,
					      rb_playlist_manager_actions,
					      rb_playlist_manager_n_actions,
					      mgr);
	}
	
	gtk_ui_manager_insert_action_group (mgr->priv->uimanager,
					    mgr->priv->actiongroup,
					    0);
}

static void
rb_playlist_manager_playlist_row_inserted_cb (GtkTreeModel *model,
					      GtkTreePath *path,
					      GtkTreeIter *iter,
					      RBPlaylistManager *mgr)
{
	RhythmDBEntry *entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (model), iter);

	rb_playlist_manager_playlist_entries_changed (model, entry, mgr);
}


static void
rb_playlist_manager_set_source_internal (RBPlaylistManager *mgr,
					 RBSource *source)
{
	gboolean playlist_active;
	gboolean playlist_local = FALSE;
	gboolean party_mode;
	gboolean can_save;
	gboolean can_delete;
	gboolean can_edit;
	gboolean can_rename;
	GtkAction *action;

	if (mgr->priv->selected_source != NULL) {
		RhythmDBQueryModel *model;

		g_object_get (G_OBJECT (mgr->priv->selected_source), "query-model", &model, NULL);
		
		g_signal_handlers_disconnect_by_func (G_OBJECT (model),
						      G_CALLBACK (rb_playlist_manager_playlist_entries_changed),
						      mgr);
		g_signal_handlers_disconnect_by_func (G_OBJECT (model),
						      G_CALLBACK (rb_playlist_manager_playlist_row_inserted_cb),
						      mgr);
		g_object_unref (G_OBJECT (model));
	}

	party_mode = rb_shell_get_party_mode (mgr->priv->shell);

	mgr->priv->selected_source = source;
	playlist_active = RB_IS_PLAYLIST_SOURCE (mgr->priv->selected_source);
	if (playlist_active) {
		g_object_get (G_OBJECT (mgr->priv->selected_source), "is-local", &playlist_local, NULL);
	}

	can_save = playlist_local && !party_mode;
	action = gtk_action_group_get_action (mgr->priv->actiongroup,
					      "MusicPlaylistSavePlaylist");
	g_object_set (G_OBJECT (action), "sensitive", can_save, NULL);

	can_delete = (playlist_local && !party_mode &&
		      !RB_IS_PLAY_QUEUE_SOURCE (mgr->priv->selected_source));
	action = gtk_action_group_get_action (mgr->priv->actiongroup,
					      "MusicPlaylistDeletePlaylist");
	g_object_set (G_OBJECT (action), "sensitive", can_delete, NULL);

	can_edit = !party_mode && RB_IS_AUTO_PLAYLIST_SOURCE (mgr->priv->selected_source);
	action = gtk_action_group_get_action (mgr->priv->actiongroup,
					      "EditAutomaticPlaylist");
	g_object_set (G_OBJECT (action), "sensitive", can_edit, NULL);

	can_rename = rb_source_can_rename (mgr->priv->selected_source);
	action = gtk_action_group_get_action (mgr->priv->actiongroup,
					      "MusicPlaylistRenamePlaylist");
	g_object_set (G_OBJECT (action), "sensitive", playlist_local && can_rename, NULL);

	/* FIXME should base this on the query model so the entry-added and entry-deleted
	 * signals can be removed from RBEntryView (where they don't belong).
	 */
	if (playlist_active && rb_recorder_enabled ()) {
		RhythmDBQueryModel *model;

		g_object_get (G_OBJECT (mgr->priv->selected_source), "query-model", &model, NULL);
		/* monitor for changes, to enable/disable the burn menu item */
		g_signal_connect_object (G_OBJECT (model),
					 "row_inserted",
					 G_CALLBACK (rb_playlist_manager_playlist_row_inserted_cb),
					 mgr, 0);
		g_signal_connect_object (G_OBJECT (model),
					 "entry-removed",
					 G_CALLBACK (rb_playlist_manager_playlist_entries_changed),
					 mgr, 0);

		rb_playlist_manager_playlist_entries_changed (GTK_TREE_MODEL (model), NULL, mgr);
		g_object_unref (model);
	} else {
		action = gtk_action_group_get_action (mgr->priv->actiongroup,
						      "MusicPlaylistBurnPlaylist");
		g_object_set (G_OBJECT (action), "sensitive", FALSE, NULL);
	}
}

static void
rb_playlist_manager_set_property (GObject *object,
				  guint prop_id,
				  const GValue *value,
				  GParamSpec *pspec)
{
	RBPlaylistManager *mgr = RB_PLAYLIST_MANAGER (object);

	switch (prop_id) {
	case PROP_SOURCE:
		rb_playlist_manager_set_source_internal (mgr, g_value_get_object (value));

		break;
	case PROP_SHELL:
	{
		GtkUIManager *uimanager;
		mgr->priv->shell = g_value_get_object (value);
		g_object_get (G_OBJECT (mgr->priv->shell), 
			      "ui-manager", &uimanager, 
			      "db", &mgr->priv->db,
			      NULL);
		rb_playlist_manager_set_uimanager (mgr, uimanager);
			      
		break;
	}
	case PROP_SOURCELIST:
		mgr->priv->sourcelist = g_value_get_object (value);
		mgr->priv->window = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (mgr->priv->sourcelist)));
		break;
	case PROP_LIBRARY_SOURCE:
		mgr->priv->libsource = g_value_get_object (value);
		break;
	case PROP_IRADIO_SOURCE:
		mgr->priv->iradio_source = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_playlist_manager_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBPlaylistManager *mgr = RB_PLAYLIST_MANAGER (object);

	switch (prop_id) {
	case PROP_SOURCE:
		g_value_set_object (value, mgr->priv->selected_source);
		break;
	case PROP_SHELL:
		g_value_set_object (value, mgr->priv->shell);
		break;
	case PROP_SOURCELIST:
		g_value_set_object (value, mgr->priv->sourcelist);
		break;
	case PROP_LIBRARY_SOURCE:
		g_value_set_object (value, mgr->priv->libsource);
		break;
	case PROP_IRADIO_SOURCE:
		g_value_set_object (value, mgr->priv->iradio_source);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_playlist_manager_set_source (RBPlaylistManager *mgr, RBSource *source)
{
	g_return_if_fail (RB_IS_PLAYLIST_MANAGER (mgr));
	g_return_if_fail (RB_IS_SOURCE (source));

	g_object_set (G_OBJECT (mgr),
		      "source", source,
		      NULL);
}

RBPlaylistManager *
rb_playlist_manager_new (RBShell *shell,
			 RBSourceList *sourcelist,
			 RBLibrarySource *libsource,
			 RBIRadioSource *iradio_source)
{
	return g_object_new (RB_TYPE_PLAYLIST_MANAGER,
			     "shell", shell,
			     "sourcelist", sourcelist,
			     "library_source", libsource,
			     "iradio_source", iradio_source,
			     NULL);
}

GQuark
rb_playlist_manager_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_playlist_manager_error");

	return quark;
}

static void
handle_playlist_entry_cb (TotemPlParser *playlist, const char *uri_maybe,
			  const char *title,
			  const char *genre, RBPlaylistManager *mgr)
{
	char *uri = rb_canonicalise_uri (uri_maybe);
	RhythmDBEntryType entry_type;

	g_return_if_fail (uri != NULL);

	entry_type = rb_shell_guess_type_for_uri (mgr->priv->shell, uri);
	if (entry_type == RHYTHMDB_ENTRY_TYPE_INVALID) {
		return;
	}

	rb_shell_add_uri (mgr->priv->shell,
			  entry_type,
			  uri,
			  title,
			  genre,
			  NULL);

	if (entry_type == RHYTHMDB_ENTRY_TYPE_SONG) {
		if (!mgr->priv->loading_playlist) {
			mgr->priv->loading_playlist =
				RB_STATIC_PLAYLIST_SOURCE (rb_playlist_manager_new_playlist (mgr, NULL, FALSE));
		}
		rb_static_playlist_source_add_location (mgr->priv->loading_playlist, uri, -1);
	}

	g_free (uri);
}

static void
playlist_load_start_cb (TotemPlParser *parser, const char *title, RBPlaylistManager *mgr)
{
	rb_debug ("loading new playlist %s", title);

	if (!mgr->priv->loading_playlist) {
		mgr->priv->loading_playlist =
			RB_STATIC_PLAYLIST_SOURCE (rb_playlist_manager_new_playlist (mgr, title, FALSE));
	}
}

static void
playlist_load_end_cb (TotemPlParser *parser, const char *title, RBPlaylistManager *mgr)
{
	rb_debug ("finished loading playlist %s", title);
	mgr->priv->loading_playlist = NULL;
}

gboolean
rb_playlist_manager_parse_file (RBPlaylistManager *mgr, const char *uri, GError **error)
{
	rb_debug ("loading playlist from %s", uri);

	g_free (mgr->priv->firsturi);
	mgr->priv->firsturi = NULL;

	g_signal_emit (G_OBJECT (mgr), rb_playlist_manager_signals[PLAYLIST_LOAD_START], 0);

	{
		TotemPlParser *parser = totem_pl_parser_new ();

		g_signal_connect_object (G_OBJECT (parser), "entry",
					 G_CALLBACK (handle_playlist_entry_cb),
					 mgr, 0);

		g_signal_connect_object (G_OBJECT (parser), "playlist-start",
					 G_CALLBACK (playlist_load_start_cb),
					 mgr, 0);

		g_signal_connect_object (G_OBJECT (parser), "playlist-end",
					 G_CALLBACK (playlist_load_end_cb),
					 mgr, 0);
		
		if (g_object_class_find_property (G_OBJECT_GET_CLASS (parser), "recurse"))
			g_object_set (G_OBJECT (parser), "recurse", FALSE, NULL);

		if (totem_pl_parser_parse (parser, uri, TRUE) != TOTEM_PL_PARSER_RESULT_SUCCESS) {
			g_set_error (error,
				     RB_PLAYLIST_MANAGER_ERROR,
				     RB_PLAYLIST_MANAGER_ERROR_PARSE,
				     "%s",
				     _("The playlist file may be in an unknown format or corrupted."));
			return FALSE;
		}
		mgr->priv->loading_playlist = NULL;

		g_object_unref (G_OBJECT (parser));
	}

	g_signal_emit (G_OBJECT (mgr), rb_playlist_manager_signals[PLAYLIST_LOAD_FINISH], 0);
	return TRUE;
}

static void
append_new_playlist_source (RBPlaylistManager *mgr, RBPlaylistSource *source)
{
	g_signal_emit (G_OBJECT (mgr), rb_playlist_manager_signals[PLAYLIST_ADDED], 0,
		       source);
}

void
rb_playlist_manager_load_playlists (RBPlaylistManager *mgr)
{
	char *file;
	xmlDocPtr doc;
	xmlNodePtr root;
	xmlNodePtr child;
	gboolean exists;
	
	exists = FALSE;
	file = g_build_filename (rb_dot_dir (), "playlists.xml", NULL);

	/* block saves until the playlists have loaded */
	g_mutex_lock (mgr->priv->saving_mutex);

	exists = g_file_test (file, G_FILE_TEST_EXISTS);
	if (! exists) {
		rb_debug ("personal playlists not found, loading defaults");

		/* try global playlists */
		g_free (file);
		file = g_strdup (rb_file ("playlists.xml"));
		exists = g_file_test (file, G_FILE_TEST_EXISTS);
	}

	if (! exists) {
		rb_debug ("default playlists file not found");

		goto out;
	}

	doc = xmlParseFile (file);
	
	if (doc == NULL)
		goto out;

	root = xmlDocGetRootElement (doc);
	
	for (child = root->children; child; child = child->next) {
		RBSource *playlist;

		if (xmlNodeIsText (child))
			continue;

		playlist = rb_playlist_source_new_from_xml (mgr->priv->shell,
							    child);
		if (playlist)
			append_new_playlist_source (mgr, RB_PLAYLIST_SOURCE (playlist));
	}

	xmlFreeDoc (doc);
out:
	g_mutex_unlock (mgr->priv->saving_mutex);
	g_free (file);
}

struct RBPlaylistManagerSaveThreadData
{
	RBPlaylistManager *mgr;
	xmlDocPtr doc;
};

static gpointer
rb_playlist_manager_save_thread_main (struct RBPlaylistManagerSaveThreadData *data)
{
	char *file;
	char *tmpname;

	rb_debug ("entering save thread");

	file = g_build_filename (rb_dot_dir (), "playlists.xml", NULL);
	tmpname = g_strconcat (file, ".tmp", NULL);
	
	if (xmlSaveFormatFile (tmpname, data->doc, 1) != -1) {
		rename (tmpname, file);
	} else {
		rb_debug ("error in xmlSaveFormatFile(), not saving");
		unlink (tmpname);
	}
	xmlFreeDoc (data->doc);
	g_free (tmpname);
	g_free (file);

	g_mutex_lock (data->mgr->priv->saving_mutex);

	data->mgr->priv->saving = FALSE;
	data->mgr->priv->dirty = FALSE;

	g_cond_broadcast (data->mgr->priv->saving_condition);
	g_mutex_unlock (data->mgr->priv->saving_mutex);

	g_async_queue_push (data->mgr->priv->status_queue, data->mgr);

	g_free (data);
	
	return NULL;
}

void
rb_playlist_manager_save_playlists_async (RBPlaylistManager *mgr, gboolean force)
{
	xmlNodePtr root;
	struct RBPlaylistManagerSaveThreadData *data;
	GtkTreeIter iter;
	GtkTreeModel *fmodel;
	GtkTreeModel *model;

	rb_debug ("saving the playlists");

	g_object_get (G_OBJECT (mgr->priv->sourcelist), "model", &fmodel, NULL);
	model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (fmodel));
	g_object_unref (fmodel);
	
	if (!force) {
		gboolean dirty = FALSE;

		if (gtk_tree_model_get_iter_first (model, &iter)) {
			do {
				RBSource *source;
				gboolean local;
				GValue v = {0,};
				gtk_tree_model_get_value (model,
							  &iter,
							  RB_SOURCELIST_MODEL_COLUMN_SOURCE,
							  &v);
				source = g_value_get_pointer (&v);
				if (RB_IS_PLAYLIST_SOURCE (source) == FALSE)
					continue;

				g_object_get (G_OBJECT (source), 
					      "is-local", &local,
					      NULL);
				if (local) {
					g_object_get (G_OBJECT (source),
						      "dirty", &dirty,
						      NULL);
					if (dirty)
						break;
				}
			} while (gtk_tree_model_iter_next (model, &iter));
		}

		if (!dirty) {
			/* hmm, don't like taking saving_mutex twice like this */
			g_mutex_lock (mgr->priv->saving_mutex);
			dirty = mgr->priv->dirty;
			g_mutex_unlock (mgr->priv->saving_mutex);
		}

		if (!dirty) {
			rb_debug ("no save needed, ignoring");
			return;
		}
	}
	
	g_mutex_lock (mgr->priv->saving_mutex);

	while (mgr->priv->saving)
		g_cond_wait (mgr->priv->saving_condition, mgr->priv->saving_mutex);

	mgr->priv->saving = TRUE;

	g_mutex_unlock (mgr->priv->saving_mutex);

	data = g_new0 (struct RBPlaylistManagerSaveThreadData, 1);
	data->mgr = mgr;
	data->doc = xmlNewDoc (RB_PLAYLIST_MGR_VERSION);

	root = xmlNewDocNode (data->doc, NULL, RB_PLAYLIST_MGR_PL, NULL);
	xmlDocSetRootElement (data->doc, root);
	
	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			RBSource *source;
			GValue v = {0,};
			gboolean local;

			gtk_tree_model_get_value (model,
						  &iter,
						  RB_SOURCELIST_MODEL_COLUMN_SOURCE,
						  &v);
			source = g_value_get_pointer (&v);
			if (RB_IS_PLAYLIST_SOURCE (source) == FALSE)
				continue;

			g_object_get (G_OBJECT (source), "is-local", &local, NULL);
			if (local)
				rb_playlist_source_save_to_xml (RB_PLAYLIST_SOURCE (source), root);
		} while (gtk_tree_model_iter_next (model, &iter));
	}

	g_object_ref (G_OBJECT (mgr));
	g_atomic_int_inc (&mgr->priv->outstanding_threads);

	g_thread_create ((GThreadFunc) rb_playlist_manager_save_thread_main, data, FALSE, NULL);
}

void
rb_playlist_manager_save_playlists (RBPlaylistManager *mgr, gboolean force)
{
	rb_debug("saving the playlists and blocking");
	
	rb_playlist_manager_save_playlists_async (mgr, force);
	
	g_mutex_lock (mgr->priv->saving_mutex);

	while (mgr->priv->saving)
		g_cond_wait (mgr->priv->saving_condition, mgr->priv->saving_mutex);

	g_mutex_unlock (mgr->priv->saving_mutex);
}

static void
rb_playlist_manager_set_dirty (RBPlaylistManager *mgr)
{
	g_mutex_lock (mgr->priv->saving_mutex);
	mgr->priv->dirty = TRUE;
	g_mutex_unlock (mgr->priv->saving_mutex);
}

RBSource *
rb_playlist_manager_new_playlist (RBPlaylistManager *mgr,
				  const char *suggested_name, 
				  gboolean automatic)
{
	RBSource *playlist;
	if (automatic)
		playlist = rb_auto_playlist_source_new (mgr->priv->shell, 
							suggested_name, 
							TRUE);
	else
		playlist = rb_static_playlist_source_new (mgr->priv->shell, 
							  suggested_name, 
							  TRUE, 
							  RHYTHMDB_ENTRY_TYPE_SONG);
	
	append_new_playlist_source (mgr, RB_PLAYLIST_SOURCE (playlist));
	rb_sourcelist_edit_source_name (mgr->priv->sourcelist, playlist);
	rb_playlist_manager_set_dirty (mgr);
	
	g_signal_emit (G_OBJECT (mgr), rb_playlist_manager_signals[PLAYLIST_CREATED], 0,
		       playlist);

	return playlist;
}

static char *
create_name_from_selection_data (RBPlaylistManager *mgr,
				 GtkSelectionData *data)
{
	char  *name = NULL;
	GList *list;

        if (data->type == gdk_atom_intern ("text/uri-list", TRUE)) {
                list = gnome_vfs_uri_list_parse ((char *) data->data);

		if (list != NULL) {
			GList   *l;
			char    *artist;
			char    *album;
			gboolean mixed_artists;
			gboolean mixed_albums;

			artist = NULL;
			album  = NULL;
			mixed_artists = FALSE;
			mixed_albums  = FALSE;
			for (l = list; l != NULL; l = g_list_next (l)) {
				RhythmDBEntry *entry;
				char          *location;
				const char    *e_artist;
				const char    *e_album;

				location = gnome_vfs_uri_to_string ((const GnomeVFSURI *) l->data, 0);
				if (location == NULL) {
					continue;
				}

				entry = rhythmdb_entry_lookup_by_location (mgr->priv->db, location);
				if (entry == NULL) {
					continue;
				}

				e_artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
				e_album = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);

				/* get value of first non-NULL artist */
				if (e_artist != NULL && artist == NULL) {
					artist = g_strdup (e_artist);
				}

				/* get value of first non-NULL album */
				if (e_album != NULL && album == NULL) {
					album = g_strdup (e_album);
				}

				/* pretend that NULL fields always match */
				if (artist != NULL && e_artist != NULL
				    && strcmp (artist, e_artist) != 0) {
					mixed_artists = TRUE;
				}

				/* pretend that NULL fields always match */
				if (album != NULL && e_album != NULL
				    && strcmp (album, e_album) != 0) {
					mixed_albums = TRUE;
				}

				/* if there is a mix of both then stop */
				if (mixed_artists && mixed_albums) {
					break;
				}
			}

			if (! mixed_artists && ! mixed_albums) {
				name = g_strdup_printf ("%s - %s", artist, album);
			} else if (! mixed_artists) {
				name = g_strdup_printf ("%s", artist);
			} else if (! mixed_albums) {
				name = g_strdup_printf ("%s", album);
			}

			g_free (artist);
			g_free (album);
			gnome_vfs_uri_list_free (list);
		}

	} else {
		char **names;

		names = g_strsplit ((char *)data->data, "\r\n", 0);
		name = g_strjoinv (", ", names);
		g_strfreev (names);
	}

	if (name == NULL) {
		name = g_strdup (_("Untitled Playlist"));
	}

	return name;
}

RBSource *
rb_playlist_manager_new_playlist_from_selection_data (RBPlaylistManager *mgr,
						      GtkSelectionData *data)
{
	RBSource *playlist;
	gboolean  automatic;
	char     *suggested_name;

	automatic = (data->type != gdk_atom_intern ("text/uri-list", TRUE));
	suggested_name = create_name_from_selection_data (mgr, data);

	playlist = rb_playlist_manager_new_playlist (mgr,
						     suggested_name, 
						     automatic);
	g_free (suggested_name);

	return playlist;
}

static void
rb_playlist_manager_cmd_new_playlist (GtkAction *action,
				      RBPlaylistManager *mgr)
{
	rb_playlist_manager_new_playlist (mgr, NULL, FALSE);
}

static void
rb_playlist_manager_set_automatic_playlist (RBPlaylistManager *mgr,
					    RBAutoPlaylistSource *playlist,
					    RBQueryCreator *creator)
{
	RBQueryCreatorLimitType type;
	guint limit, limit_count = 0, limit_size = 0, limit_time = 0;
	const char *sort_key;
	gint sort_direction;

	rb_query_creator_get_limit (creator, &type, &limit);
	if (type == RB_QUERY_CREATOR_LIMIT_COUNT)
		limit_count = limit;
	else if (type == RB_QUERY_CREATOR_LIMIT_MB)
		limit_size = limit;
	else if (type == RB_QUERY_CREATOR_LIMIT_SECONDS)
		limit_time = limit;
	else
		g_assert_not_reached ();

	rb_query_creator_get_sort_order (creator, &sort_key, &sort_direction);

	rb_auto_playlist_source_set_query (RB_AUTO_PLAYLIST_SOURCE (playlist),
					   rb_query_creator_get_query (creator),
					   limit_count, limit_size, limit_time,
					   sort_key, sort_direction);
}

static void
rb_playlist_manager_cmd_new_automatic_playlist (GtkAction *action,
						RBPlaylistManager *mgr)
{
	RBQueryCreator *creator = RB_QUERY_CREATOR (rb_query_creator_new (mgr->priv->db));
	RBSource *playlist;
	
	switch (gtk_dialog_run (GTK_DIALOG (creator))) {
	case GTK_RESPONSE_NONE:
	case GTK_RESPONSE_CLOSE:
		gtk_widget_destroy (GTK_WIDGET (creator));	
		return;
	}

	playlist = rb_playlist_manager_new_playlist (mgr, NULL, TRUE);

	rb_playlist_manager_set_automatic_playlist (mgr, RB_AUTO_PLAYLIST_SOURCE (playlist), creator); 

	rb_playlist_manager_set_dirty (mgr);

	gtk_widget_destroy (GTK_WIDGET (creator));	
}

typedef struct {
	RBAutoPlaylistSource *playlist;
	RBPlaylistManager *mgr;
	RBQueryCreator *creator;
	gint playlist_deleted_id;
	gint creator_response_id;
} EditAutoPlaylistData;

static void
cleanup_edit_data (EditAutoPlaylistData *data)
{
	g_signal_handler_disconnect (data->playlist, data->playlist_deleted_id);
	g_signal_handler_disconnect (data->creator, data->creator_response_id);
	gtk_widget_destroy (GTK_WIDGET (data->creator));
	g_free (data);
}

static void
edit_auto_playlist_response_cb (RBQueryCreator *dialog,
				gint response,
				EditAutoPlaylistData *data)
{
	rb_playlist_manager_set_automatic_playlist (data->mgr, data->playlist, dialog);
	g_object_set_data (G_OBJECT (data->playlist), "rhythmbox-playlist-editor", NULL);

	cleanup_edit_data (data);
}

static void
edit_auto_playlist_deleted_cb (RBAutoPlaylistSource *playlist, EditAutoPlaylistData *data)
{
	g_object_set_data (G_OBJECT (playlist), "rhythmbox-playlist-editor", NULL);

	cleanup_edit_data (data);
}

static void
rb_playlist_manager_cmd_edit_automatic_playlist (GtkAction *action,
						 RBPlaylistManager *mgr)
{
	RBQueryCreator *creator;
	RBAutoPlaylistSource *playlist;
	GPtrArray *query;
	guint limit_count = 0, limit_size = 0, limit_time = 0;
	const char *sort_key;
	gint sort_direction;
	EditAutoPlaylistData *data;

	playlist = RB_AUTO_PLAYLIST_SOURCE (mgr->priv->selected_source);
	creator = g_object_get_data (G_OBJECT (playlist), "rhythmbox-playlist-editor");
	if (creator == NULL) {
		rb_auto_playlist_source_get_query (playlist, &query, &limit_count, &limit_size, &limit_time, &sort_key, &sort_direction);

		creator = RB_QUERY_CREATOR (rb_query_creator_new_from_query (mgr->priv->db,
									     query,
									     limit_count, limit_size, limit_time,
									     sort_key, sort_direction));

		data = g_new0 (EditAutoPlaylistData, 1);
		data->mgr = mgr;
		data->playlist = playlist;
		data->creator = creator;
		data->creator_response_id = 
			g_signal_connect (G_OBJECT (creator),
					  "response",
					  G_CALLBACK (edit_auto_playlist_response_cb),
					  data);
		
		g_object_set_data (G_OBJECT (playlist), "rhythmbox-playlist-editor", creator);
		data->playlist_deleted_id = 
			g_signal_connect (G_OBJECT (playlist),
					  "deleted",
					  G_CALLBACK (edit_auto_playlist_deleted_cb),
					  data);
	}
	gtk_window_present (GTK_WINDOW (creator));
}

static gboolean
_queue_track_cb (RhythmDBQueryModel *model,
		 GtkTreePath *path,
		 GtkTreeIter *iter,
		 RBStaticPlaylistSource *queue_source)
{
	RhythmDBEntry *entry;
	
	entry = rhythmdb_query_model_iter_to_entry (model, iter);
	rb_static_playlist_source_add_entry (queue_source, entry, -1);
	
	return FALSE;
}

static void
rb_playlist_manager_cmd_queue_playlist (GtkAction *action,
					RBPlaylistManager *mgr)
{
	RBSource *queue_source;
	RhythmDBQueryModel *model;

	g_object_get (G_OBJECT (mgr->priv->shell), "queue-source", &queue_source, NULL);
	g_object_get (G_OBJECT (mgr->priv->selected_source), "query-model", &model, NULL);

	gtk_tree_model_foreach (GTK_TREE_MODEL (model),
				(GtkTreeModelForeachFunc) _queue_track_cb,
				queue_source);

	g_object_unref (G_OBJECT (queue_source));
	g_object_unref (G_OBJECT (model));
}

static void
rb_playlist_manager_cmd_rename_playlist (GtkAction *action,
					 RBPlaylistManager *mgr)
{
	rb_debug ("Renaming playlist %p", mgr->priv->selected_source);

	rb_sourcelist_edit_source_name (mgr->priv->sourcelist, mgr->priv->selected_source);
	rb_playlist_manager_set_dirty (mgr);
}

static void
rb_playlist_manager_cmd_delete_playlist (GtkAction *action,
					 RBPlaylistManager *mgr)
{
	rb_debug ("Deleting playlist %p", mgr->priv->selected_source);
	
	rb_source_delete_thyself (mgr->priv->selected_source);
	rb_playlist_manager_set_dirty (mgr);
}

static void
load_playlist_response_cb (GtkDialog *dialog,
			   int response_id,
			   RBPlaylistManager *mgr)
{
	char *escaped_file = NULL;
	GError *error = NULL;

	if (response_id != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	escaped_file = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog));

	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (escaped_file == NULL)
		return;

	if (!rb_playlist_manager_parse_file (mgr, escaped_file, &error)) {
		rb_error_dialog (NULL, _("Couldn't read playlist"),
				 error->message);
		g_error_free (error);
	}

	g_free (escaped_file);
	rb_playlist_manager_set_dirty (mgr);
}

static void
rb_playlist_manager_cmd_load_playlist (GtkAction *action,
				       RBPlaylistManager *mgr)
{
	GtkWidget *dialog;

	dialog = rb_file_chooser_new (_("Load Playlist"),
				      GTK_WINDOW (mgr->priv->window),
				      GTK_FILE_CHOOSER_ACTION_OPEN,
				      FALSE);

	g_signal_connect_object (G_OBJECT (dialog), "response",
				 G_CALLBACK (load_playlist_response_cb), mgr, 0);
}



typedef struct {
  const gchar *description;
  /* NULL terminated array of extensions for this file format.  The first
   * one is the prefered extension for files of this type. */
  const gchar **extensions;
  const RBPlaylistExportType type;
} RBPlaylistExportFilter;

static const char *m3u_extensions [] = {"m3u", NULL};
static const char *pls_extensions [] = {"pls", NULL};

static RBPlaylistExportFilter playlist_export_formats[] = {
	{N_("MPEG Version 3.0 URL"), m3u_extensions, RB_PLAYLIST_EXPORT_TYPE_M3U},
	{N_("Shoutcast playlist"), pls_extensions, RB_PLAYLIST_EXPORT_TYPE_PLS},
};

static void
save_playlist_response_cb (GtkDialog *dialog,
			   int response_id,
			   RBPlaylistManager *mgr)
{
	char *file = NULL;
	GtkWidget *menu;
	gint index;
	RBPlaylistExportType export_type = RB_PLAYLIST_EXPORT_TYPE_UNKNOWN;

	if (response_id != GTK_RESPONSE_OK) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	file = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog));
	if (file == NULL || file[0] == '\0')
		return;

	menu = g_object_get_data (G_OBJECT(dialog), "export-menu");
	index = gtk_combo_box_get_active (GTK_COMBO_BOX (menu));

	/* by extension selected */
	if (index <= 0) {
		int i;

		for (i = 0; i < G_N_ELEMENTS (playlist_export_formats); i++) {
			int j;

			/* determine the playlist type from the extension */
			for (j = 0; playlist_export_formats[i].extensions[j] != NULL; j++) {
				if (g_str_has_suffix (file, playlist_export_formats[i].extensions[j])) {
					export_type = playlist_export_formats[i].type;
					break;
				}
			}
		}
	} else {
		export_type = playlist_export_formats[index-1].type;
	}

	if (export_type == RB_PLAYLIST_EXPORT_TYPE_UNKNOWN) {
			rb_error_dialog (NULL, _("Couldn't save playlist"), _("Unsupported file extension given."));
	} else {
		rb_playlist_source_save_playlist (RB_PLAYLIST_SOURCE (mgr->priv->selected_source), 
						  file, (export_type == RB_PLAYLIST_EXPORT_TYPE_M3U));
		gtk_widget_destroy (GTK_WIDGET (dialog));
	}

	g_free (file);
}

static void
export_set_extension_cb (GtkWidget* widget, GtkDialog *dialog)
{
	gint index;
	gchar *text;
	gchar *last_dot;
	const char *extension;
	gchar *basename;
	GString *basename_str;
  
	index = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));
	if (index <= 0)
		return;

	extension = playlist_export_formats[index-1].extensions[0];
	if (extension == NULL)
		return;
	
	text = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
	if (text == NULL || text[0] == '\0') {
		g_free (text);
		return;
	}

	basename = g_path_get_basename (text);
	basename_str = g_string_new (basename);
	last_dot = g_utf8_strrchr (basename, -1, '.');
	if (last_dot)
		g_string_truncate (basename_str, (last_dot-basename));
	g_free (basename);
	g_free (text);
  
	g_string_append_printf (basename_str, ".%s", extension);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), basename_str->str);
	g_string_free (basename_str, TRUE);
}

static gchar *
filter_get_export_filter_label (RBPlaylistExportFilter *efilter)
{
	GString *str;
	gint ext;

	str = g_string_new (_(efilter->description));
	for (ext = 0; efilter->extensions[ext] != NULL; ext++) {
		if (ext == 0)
			g_string_append (str, " (*.");
		else
			g_string_append (str, ", *.");
		g_string_append (str, efilter->extensions[ext]);
	}
    
	if (ext > 0)
		g_string_append (str, ")");
    
	return g_string_free (str, FALSE);
}

static void
setup_format_menu (GtkWidget* menu, GtkWidget *dialog)
{
	GtkTreeModel *model;
	int i;

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (menu));
	gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (menu), rb_combo_box_hyphen_separator_func,
					      NULL, NULL);

	for (i = 0; i < G_N_ELEMENTS (playlist_export_formats); i++) {
		gchar *filter_label;
		GtkTreeIter iter;
    
		filter_label = filter_get_export_filter_label (&playlist_export_formats[i]);
		gtk_list_store_insert_with_values (GTK_LIST_STORE (model), &iter, -1,
						   0, filter_label, -1);

		g_free (filter_label);
	}
    
	g_signal_connect_object (GTK_OBJECT (menu),
				 "changed", G_CALLBACK (export_set_extension_cb),
				 dialog, 0);
}

static void
rb_playlist_manager_cmd_save_playlist (GtkAction *action,
				       RBPlaylistManager *mgr)
{
	GladeXML *xml;
	GtkWidget *dialog, *menu;

	xml = rb_glade_xml_new ("playlist-save.glade",
				"playlist_save_dialog",
				mgr);
	dialog = glade_xml_get_widget (xml, "playlist_save_dialog");

	menu = glade_xml_get_widget (xml, "playlist_format_menu");
	setup_format_menu (menu, dialog);
	g_object_set_data (G_OBJECT (dialog), "export-menu", menu);
	
	/* FIXME: always has "by extension" as default (it should probably remember the last selection) */
	gtk_combo_box_set_active (GTK_COMBO_BOX (menu), 0);
	g_signal_connect_object (G_OBJECT (dialog), "response",
				 G_CALLBACK (save_playlist_response_cb),
				 mgr, 0);

	g_object_unref (G_OBJECT (xml));
}

static void
rb_playlist_manager_cmd_burn_playlist (GtkAction *action,
				       RBPlaylistManager *mgr)
{
	rb_playlist_source_burn_playlist (RB_PLAYLIST_SOURCE (mgr->priv->selected_source));
}

static void
rb_playlist_manager_playlist_entries_changed (GtkTreeModel *model, RhythmDBEntry *entry, RBPlaylistManager *mgr)
{
	int num_tracks;
	GtkAction *action;

	num_tracks = gtk_tree_model_iter_n_children (model, NULL);

	action = gtk_action_group_get_action (mgr->priv->actiongroup, "MusicPlaylistBurnPlaylist");
	g_object_set (G_OBJECT (action), "sensitive", (num_tracks > 0), NULL);
}

GList *
rb_playlist_manager_get_playlists (RBPlaylistManager *mgr)
{
	GList *playlists = NULL;
	GtkTreeIter iter;
	GtkTreeModel *fmodel;
	GtkTreeModel *model;
	
	g_object_get (G_OBJECT (mgr->priv->sourcelist), "model", &fmodel, NULL);
	model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (fmodel));
	g_object_unref (fmodel);
	
	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			RBSource *source;
			GValue v = {0,};
			gboolean local;

			gtk_tree_model_get_value (model,
						  &iter,
						  RB_SOURCELIST_MODEL_COLUMN_SOURCE,
						  &v);
			source = g_value_get_pointer (&v);
			if (RB_IS_PLAYLIST_SOURCE (source) == FALSE)
				continue;
			if (RB_IS_PLAY_QUEUE_SOURCE (source) == TRUE)
				continue;
			g_object_get (G_OBJECT (source), "is-local", &local, 
				      NULL);
			if (local) {
				playlists = g_list_prepend (playlists, source);
			}

		} while (gtk_tree_model_iter_next (model, &iter));
	}
	
	return playlists;
}

gboolean
rb_playlist_manager_get_playlist_names (RBPlaylistManager *mgr,
					gchar ***playlists,
					GError **error)
{
	GtkTreeIter iter;
	GtkTreeModel *fmodel;
	GtkTreeModel *model;
	int i;
	
	g_object_get (G_OBJECT (mgr->priv->sourcelist), "model", &fmodel, NULL);
	model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (fmodel));
	g_object_unref (fmodel);
	
	if (!gtk_tree_model_get_iter_first (model, &iter)) {
		*playlists = NULL;
		return TRUE;
	}

	*playlists = g_new0 (char *, gtk_tree_model_iter_n_children (model, NULL) + 1);
	if (!*playlists)
		return FALSE;

	i = 0;
	do {
		RBSource *source;
		GValue v = {0,};
		char *source_name;

		gtk_tree_model_get_value (model, &iter,
					  RB_SOURCELIST_MODEL_COLUMN_SOURCE,
					  &v);
		source = g_value_get_pointer (&v);
		if (!RB_IS_PLAYLIST_SOURCE (source))
			continue;
		if (RB_IS_PLAY_QUEUE_SOURCE (source))
			continue;

		g_object_get (G_OBJECT (source), "name", &source_name, NULL);
		(*playlists)[i++] = source_name;
	} while (gtk_tree_model_iter_next (model, &iter));

	return TRUE;	
}

static RBSource *
_get_playlist_by_name (RBPlaylistManager *mgr,
		       const char *name)
{
	GtkTreeIter iter;
	GtkTreeModel *fmodel;
	GtkTreeModel *model;
	RBSource *playlist = NULL;

	g_object_get (G_OBJECT (mgr->priv->sourcelist), "model", &fmodel, NULL);
	model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (fmodel));
	g_object_unref (G_OBJECT (fmodel));

	if (!gtk_tree_model_get_iter_first (model, &iter))
		return NULL;

	do {
		RBSource *source;
		GValue v = {0,};
		char *source_name;
		
		gtk_tree_model_get_value (model, &iter,
					  RB_SOURCELIST_MODEL_COLUMN_SOURCE,
					  &v);
		source = g_value_get_pointer (&v);
		if (!RB_IS_PLAYLIST_SOURCE (source))
			continue;
		g_object_get (G_OBJECT (source), "name", &source_name, NULL);
		if (strcmp (name, source_name) == 0)
			playlist = source;

		g_free (source_name);

	} while (gtk_tree_model_iter_next (model, &iter) && playlist == NULL);

	return playlist;
}

gboolean
rb_playlist_manager_create_static_playlist (RBPlaylistManager *mgr,
					    const gchar *name,
					    GError **error)
{
	if (_get_playlist_by_name (mgr, name)) {
		g_set_error (error,
			     RB_PLAYLIST_MANAGER_ERROR,
			     RB_PLAYLIST_MANAGER_ERROR_PLAYLIST_EXISTS,
			     _("Playlist %s already exists"),
			     name);
		return FALSE;
	}

	rb_playlist_manager_new_playlist (mgr, name, FALSE);
	return TRUE;
}

gboolean
rb_playlist_manager_delete_playlist (RBPlaylistManager *mgr, const gchar *name, GError **error)
{
	RBSource *playlist = _get_playlist_by_name (mgr, name);
	if (!playlist) {
		g_set_error (error,
			     RB_PLAYLIST_MANAGER_ERROR,
			     RB_PLAYLIST_MANAGER_ERROR_PLAYLIST_NOT_FOUND,
			     _("Unknown playlist: %s"),
			     name);
		return FALSE;
	}
	rb_source_delete_thyself (playlist);
	rb_playlist_manager_set_dirty (mgr);
	return TRUE;
}

gboolean
rb_playlist_manager_add_to_playlist (RBPlaylistManager *mgr,
				     const gchar *playlist,
				     const gchar *uri,
				     GError **error)
{
	RBSource *source = _get_playlist_by_name (mgr, playlist);;
	if (!source) {
		g_set_error (error,
			     RB_PLAYLIST_MANAGER_ERROR,
			     RB_PLAYLIST_MANAGER_ERROR_PLAYLIST_NOT_FOUND,
			     _("Unknown playlist: %s"),
			     playlist);
		return FALSE;
	}
	if (RB_IS_AUTO_PLAYLIST_SOURCE (source)) {
		g_set_error (error,
			     RB_PLAYLIST_MANAGER_ERROR,
			     RB_PLAYLIST_MANAGER_ERROR_PLAYLIST_NOT_FOUND,
			     _("Playlist %s is an automatic playlist"),
			     playlist);
		return FALSE;
	}
	rb_static_playlist_source_add_location (RB_STATIC_PLAYLIST_SOURCE (source), uri, -1);
	return TRUE;
}

gboolean
rb_playlist_manager_remove_from_playlist (RBPlaylistManager *mgr,
					  const gchar *playlist,
					  const gchar *uri,
					  GError **error)
{
	RBSource *source = _get_playlist_by_name (mgr, playlist);;
	if (!source) {
		g_set_error (error,
			     RB_PLAYLIST_MANAGER_ERROR,
			     RB_PLAYLIST_MANAGER_ERROR_PLAYLIST_NOT_FOUND,
			     _("Unknown playlist: %s"),
			     playlist);
		return FALSE;
	}
	if (RB_IS_AUTO_PLAYLIST_SOURCE (source)) {
		g_set_error (error,
			     RB_PLAYLIST_MANAGER_ERROR,
			     RB_PLAYLIST_MANAGER_ERROR_PLAYLIST_NOT_FOUND,
			     _("Playlist %s is an automatic playlist"),
			     playlist);
		return FALSE;
	}

	if (rb_playlist_source_location_in_map (RB_PLAYLIST_SOURCE (source), uri))
		rb_static_playlist_source_remove_location (RB_STATIC_PLAYLIST_SOURCE (source), uri);
	return TRUE;
}

gboolean
rb_playlist_manager_export_playlist (RBPlaylistManager *mgr,
				     const gchar *playlist,
				     const gchar *uri,
				     gboolean m3u_format,
				     GError **error)
{
	RBSource *source = _get_playlist_by_name (mgr, playlist);
	if (!source) {
		g_set_error (error,
			     RB_PLAYLIST_MANAGER_ERROR,
			     RB_PLAYLIST_MANAGER_ERROR_PLAYLIST_NOT_FOUND,
			     _("Unknown playlist: %s"),
			     playlist);
		return FALSE;
	}

	rb_playlist_source_save_playlist (RB_PLAYLIST_SOURCE (source),
					  uri,
					  m3u_format);
	return TRUE;
}

