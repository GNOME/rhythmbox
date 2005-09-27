/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>
#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>
#include <libxml/tree.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <string.h>
#include <stdio.h>      /* rename() */
#include <unistd.h>     /* unlink() */

#include "rb-playlist-manager.h"
#include "rb-playlist-source.h"
#if defined(WITH_CD_BURNER_SUPPORT)
#include "rb-recorder.h"
#endif
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
static gboolean reap_dead_playlist_threads (RBPlaylistManager *mgr);
static void rb_playlist_manager_playlist_entries_changed (RBEntryView *entry_view,
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

	RBPlaylistSource *loading_playlist;

	char *firsturi;

	guint thread_reaper_id;

	GAsyncQueue *status_queue;
	gint outstanding_threads;

	GCond *saving_condition;
	GMutex *saving_mutex;

	gboolean saving;
	gboolean dirty;
};

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

	{ "MusicPlaylistNewPlaylist", "gtk-media-playlist", N_("_New Playlist..."), "<control>N",
	  N_("Create a new playlist"),
	  G_CALLBACK (rb_playlist_manager_cmd_new_playlist) },
	{ "MusicPlaylistNewAutomaticPlaylist", "gtk-media-automatic-playlist", N_("New _Automatic Playlist..."), NULL,
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
	{ "MusicPlaylistRenamePlaylist", NULL, N_("_Rename..."), NULL,
	  N_("Rename playlist"),
	  G_CALLBACK (rb_playlist_manager_cmd_rename_playlist) },
	{ "MusicPlaylistDeletePlaylist", GTK_STOCK_REMOVE, N_("_Delete"), NULL,
	  N_("Delete playlist"),
	  G_CALLBACK (rb_playlist_manager_cmd_delete_playlist) },
	{ "EditAutomaticPlaylist", GTK_STOCK_PROPERTIES, N_("_Edit"), NULL,
	  N_("Change this automatic playlist"),
	  G_CALLBACK (rb_playlist_manager_cmd_edit_automatic_playlist) },
};
static guint rb_playlist_manager_n_actions = G_N_ELEMENTS (rb_playlist_manager_actions);

static GObjectClass *parent_class = NULL;

GType
rb_playlist_manager_get_type (void)
{
	static GType rb_playlist_manager_type = 0;

	if (rb_playlist_manager_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBPlaylistManagerClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_playlist_manager_class_init,
			NULL,
			NULL,
			sizeof (RBPlaylistManager),
			0,
			(GInstanceInitFunc) rb_playlist_manager_init
		};

		rb_playlist_manager_type = g_type_register_static (G_TYPE_OBJECT,
								   "RBPlaylistManager",
								   &our_info, 0);
	}

	return rb_playlist_manager_type;
}

static void
rb_playlist_manager_class_init (RBPlaylistManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

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
}

static void
rb_playlist_manager_init (RBPlaylistManager *mgr)
{
	mgr->priv = g_new0 (RBPlaylistManagerPrivate, 1);

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
		mgr->priv->outstanding_threads--;
		GDK_THREADS_LEAVE ();
	}

	GDK_THREADS_ENTER ();
	mgr->priv->thread_reaper_id = g_timeout_add (5000, (GSourceFunc) reap_dead_playlist_threads, mgr);
	GDK_THREADS_LEAVE ();
	
	return FALSE;
}


static void
rb_playlist_manager_finalize (GObject *object)
{
	RBPlaylistManager *mgr;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PLAYLIST_MANAGER (object));

	mgr = RB_PLAYLIST_MANAGER (object);

	g_return_if_fail (mgr->priv != NULL);

	g_source_remove (mgr->priv->thread_reaper_id);
	
	while (mgr->priv->outstanding_threads > 0) {
		GObject *obj = g_async_queue_pop (mgr->priv->status_queue);
		g_object_unref (obj);
		mgr->priv->outstanding_threads--;
	}
	
	g_async_queue_unref (mgr->priv->status_queue);

	g_mutex_free (mgr->priv->saving_mutex);
	g_cond_free (mgr->priv->saving_condition);

	g_free (mgr->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
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
rb_playlist_manager_set_property (GObject *object,
				  guint prop_id,
				  const GValue *value,
				  GParamSpec *pspec)
{
	RBPlaylistManager *mgr = RB_PLAYLIST_MANAGER (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
	{
		gboolean playlist_active;
		GtkAction *action;

		if (mgr->priv->selected_source != NULL)
			g_signal_handlers_disconnect_by_func (G_OBJECT (rb_source_get_entry_view (mgr->priv->selected_source)),
							      G_CALLBACK (rb_playlist_manager_playlist_entries_changed),
							      mgr);

		mgr->priv->selected_source = g_value_get_object (value);
		playlist_active = RB_IS_PLAYLIST_SOURCE (mgr->priv->selected_source);

		action = gtk_action_group_get_action (mgr->priv->actiongroup,
						      "MusicPlaylistSavePlaylist");
		g_object_set (G_OBJECT (action), "sensitive", playlist_active, NULL);
		action = gtk_action_group_get_action (mgr->priv->actiongroup,
						      "MusicPlaylistDeletePlaylist");
		g_object_set (G_OBJECT (action), "sensitive", playlist_active, NULL);
#if defined(WITH_CD_BURNER_SUPPORT)
		{
			gboolean recorder_active;
			int num_tracks = rb_entry_view_get_num_entries (rb_source_get_entry_view (mgr->priv->selected_source));

			recorder_active = playlist_active && rb_recorder_enabled ()
					&& (num_tracks > 0);
			action = gtk_action_group_get_action (mgr->priv->actiongroup, "MusicPlaylistBurnPlaylist");
			g_object_set (G_OBJECT (action), "sensitive", recorder_active, NULL);
			
			if (playlist_active && rb_recorder_enabled ()) {
				/* monitor for changes, to enable/disable the burn menu item */
				g_signal_connect_object (G_OBJECT (rb_source_get_entry_view (mgr->priv->selected_source)),
							 "entry-added", G_CALLBACK (rb_playlist_manager_playlist_entries_changed), mgr, 0);
				g_signal_connect_object (G_OBJECT (rb_source_get_entry_view (mgr->priv->selected_source)),
							 "entry-deleted", G_CALLBACK (rb_playlist_manager_playlist_entries_changed), mgr, 0);
			}
		}
#endif
		break;
	}
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

	switch (prop_id)
	{
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
	char *uri;
	gint entry_type;

	if (uri_maybe[0] == '/') {
		uri = gnome_vfs_get_uri_from_local_path (uri_maybe);
		if (uri == NULL) {
			rb_debug ("Error processing absolute filename %s", uri_maybe);
			return;
		}
	} else {
		GnomeVFSURI *vfsuri = gnome_vfs_uri_new (uri_maybe);
		if (!vfsuri) {
			rb_debug ("Error processing probable URI %s", uri_maybe);
			return;
		}
		gnome_vfs_uri_unref (vfsuri);
	}

	entry_type = rb_shell_guess_type_for_uri (mgr->priv->shell, uri_maybe);
	if (entry_type < 0) {
		return;
	}

	rb_shell_add_uri (mgr->priv->shell,
			  entry_type,
			  uri_maybe,
			  title,
			  genre,
			  NULL);

	if (entry_type == RHYTHMDB_ENTRY_TYPE_SONG) {
		if (!mgr->priv->loading_playlist) {
			mgr->priv->loading_playlist =
				RB_PLAYLIST_SOURCE (rb_playlist_manager_new_playlist (mgr, NULL, FALSE));
		}
		rb_playlist_source_add_location (mgr->priv->loading_playlist,
						 uri_maybe);
	}
}

static void
playlist_load_start_cb (TotemPlParser *parser, const char *title, RBPlaylistManager *mgr)
{
	rb_debug ("loading new playlist %s", title);

	if (!mgr->priv->loading_playlist) {
		mgr->priv->loading_playlist =
			RB_PLAYLIST_SOURCE (rb_playlist_manager_new_playlist (mgr, title, FALSE));
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
		
		if (totem_pl_parser_parse (parser, uri, FALSE) != TOTEM_PL_PARSER_RESULT_SUCCESS) {
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
	char *file = g_build_filename (rb_dot_dir (), "playlists.xml", NULL);
	xmlDocPtr doc;
	xmlNodePtr root;
	xmlNodePtr child;
	
	if (!g_file_test (file, G_FILE_TEST_EXISTS))
		goto out;

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
		append_new_playlist_source (mgr, RB_PLAYLIST_SOURCE (playlist));
	}

	xmlFreeDoc (doc);
out:
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

	g_cond_signal (data->mgr->priv->saving_condition);
	g_mutex_unlock (data->mgr->priv->saving_mutex);

	g_async_queue_push (data->mgr->priv->status_queue, data->mgr);

	g_free (data);
	
	return NULL;
}

void
rb_playlist_manager_save_playlists (RBPlaylistManager *mgr, gboolean force)
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
					      "dirty", &dirty, 
					      "is-local", &local, 
					      NULL);
				if (local && dirty) {
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

			g_object_get (G_OBJECT (source), "is-local", &local, 
				      NULL);
			if (local) {

				rb_playlist_source_save_to_xml (RB_PLAYLIST_SOURCE (source), root);
			}
		} while (gtk_tree_model_iter_next (model, &iter));
	}

	g_object_ref (G_OBJECT (mgr));
	mgr->priv->outstanding_threads++;
	
	g_thread_create ((GThreadFunc) rb_playlist_manager_save_thread_main, data, FALSE, NULL);
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
	RBSource *playlist = RB_SOURCE (rb_playlist_source_new (mgr->priv->shell, automatic, TRUE));
	
	g_object_set (G_OBJECT (playlist),
		      "name", suggested_name ? suggested_name : "",
		      NULL);
	append_new_playlist_source (mgr, RB_PLAYLIST_SOURCE (playlist));
	rb_sourcelist_edit_source_name (mgr->priv->sourcelist, playlist);
	rb_playlist_manager_set_dirty (mgr);
	
	g_signal_emit (G_OBJECT (mgr), rb_playlist_manager_signals[PLAYLIST_CREATED], 0,
		       playlist);

	return playlist;
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
			g_object_get (G_OBJECT (source), "is-local", &local, 
				      NULL);
			if (local) {
				playlists = g_list_prepend (playlists, source);
			}

		} while (gtk_tree_model_iter_next (model, &iter));
	}
	
	return playlists;
}

static void
rb_playlist_manager_cmd_new_playlist (GtkAction *action,
				      RBPlaylistManager *mgr)
{
	rb_playlist_manager_new_playlist (mgr, NULL, FALSE);
}

static void
rb_playlist_manager_set_automatic_playlist (RBPlaylistManager *mgr,
					    RBPlaylistSource *playlist,
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

	rb_playlist_source_set_query (playlist,
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
	
	switch (gtk_dialog_run (GTK_DIALOG (creator)))
	{
	case GTK_RESPONSE_NONE:
	case GTK_RESPONSE_CLOSE:
		gtk_widget_destroy (GTK_WIDGET (creator));	
		return;
	}

	playlist = rb_playlist_manager_new_playlist (mgr, NULL, TRUE);

	rb_playlist_manager_set_automatic_playlist (mgr, RB_PLAYLIST_SOURCE (playlist), creator); 

	rb_playlist_manager_set_dirty (mgr);

	gtk_widget_destroy (GTK_WIDGET (creator));	
}

static void
rb_playlist_manager_cmd_edit_automatic_playlist (GtkAction *action,
						 RBPlaylistManager *mgr)
{
	RBQueryCreator *creator;
	RBPlaylistSource *playlist;
	GPtrArray *query;
	guint limit_count = 0, limit_size = 0, limit_time = 0;
	const char *sort_key;
	gint sort_direction;
	

	playlist = RB_PLAYLIST_SOURCE (mgr->priv->selected_source);
	rb_playlist_source_get_query (playlist, &query, &limit_count, &limit_size, &limit_time, &sort_key, &sort_direction);

	creator = RB_QUERY_CREATOR (rb_query_creator_new_from_query (mgr->priv->db,
								     query,
								     limit_count, limit_size, limit_time,
								     sort_key, sort_direction));

	gtk_dialog_run (GTK_DIALOG (creator));

	rb_playlist_manager_set_automatic_playlist (mgr, playlist, creator);
	gtk_widget_destroy (GTK_WIDGET (creator));	
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
				      TRUE);

	g_signal_connect_object (G_OBJECT (dialog), "response",
				 G_CALLBACK (load_playlist_response_cb), mgr, 0);
}

static void
save_playlist_response_cb (GtkDialog *dialog,
			   int response_id,
			   RBPlaylistManager *mgr)
{
	char *file = NULL;

	if (response_id != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	file = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog));

	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (file == NULL)
		return;

	rb_playlist_source_save_playlist (RB_PLAYLIST_SOURCE (mgr->priv->selected_source), file);
	g_free (file);
}

static void
rb_playlist_manager_cmd_save_playlist (GtkAction *action,
				       RBPlaylistManager *mgr)
{
	GtkWidget *dialog;
    
	dialog = rb_file_chooser_new (_("Save Playlist"),
				      GTK_WINDOW (mgr->priv->window),
				      GTK_FILE_CHOOSER_ACTION_SAVE,
				      TRUE);

	g_signal_connect_object (G_OBJECT (dialog), "response",
				 G_CALLBACK (save_playlist_response_cb),
				 mgr, 0);
}

static void
rb_playlist_manager_cmd_burn_playlist (GtkAction *action,
				       RBPlaylistManager *mgr)
{
	rb_playlist_source_burn_playlist (RB_PLAYLIST_SOURCE (mgr->priv->selected_source));
}

static void
rb_playlist_manager_playlist_entries_changed (RBEntryView *entry_view, RhythmDBEntry *entry, RBPlaylistManager *mgr)
{
	int num_tracks = rb_entry_view_get_num_entries (entry_view);
	GtkAction *action = gtk_action_group_get_action (mgr->priv->actiongroup, "MusicPlaylistBurnPlaylist");

	g_object_set (G_OBJECT (action), "sensitive", (num_tracks > 0), NULL);
}
