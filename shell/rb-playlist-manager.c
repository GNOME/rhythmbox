/*
 *  arch-tag: Implementation of Rhythmbox playlist management object
 *
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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
#include <bonobo/bonobo-ui-util.h>
#include <libxml/tree.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <string.h>

#include "rb-playlist-manager.h"
#include "rb-playlist-source.h"
#include "rb-sourcelist.h"
#include "rb-query-creator.h"
#include "rb-playlist.h"

#include "rb-bonobo-helpers.h"
#include "rb-file-helpers.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rhythmdb.h"
#include "rb-stock-icons.h"
#include "eel-gconf-extensions.h"

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
static void rb_playlist_manager_cmd_load_playlist (BonoboUIComponent *component,
						   RBPlaylistManager *mgr,
						   const char *verbname);
static void rb_playlist_manager_cmd_save_playlist (BonoboUIComponent *component,
						   RBPlaylistManager *mgr,
						   const char *verbname);
static void rb_playlist_manager_cmd_new_playlist (BonoboUIComponent *component,
						  RBPlaylistManager *mgr,
						  const char *verbname);
static void rb_playlist_manager_cmd_new_automatic_playlist (BonoboUIComponent *component,
							    RBPlaylistManager *mgr,
							    const char *verbname);
static void rb_playlist_manager_cmd_delete_playlist (BonoboUIComponent *component,
						     RBPlaylistManager *mgr,
						     const char *verbname);
static void rb_playlist_manager_cmd_edit_automatic_playlist (BonoboUIComponent *component,
							     RBPlaylistManager *mgr,
							     const char *verbname);
static void handle_playlist_entry_into_playlist_cb (RBPlaylist *playlist, const char *uri, const char *title,
						    const char *genre, RBPlaylistManager *mgr);

struct RBPlaylistManagerPrivate
{
	RhythmDB *db;
	RBSource *selected_source;

	RBSourceList *sourcelist;

	BonoboUIComponent *component;
	RBLibrarySource *libsource;
	RBIRadioSource *iradio_source;
	GtkWindow *window;

	GList *playlists;

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
	PROP_WINDOW,
	PROP_COMPONENT,
	PROP_SOURCE,
	PROP_DB,
	PROP_SOURCELIST,
	PROP_LIBRARY_SOURCE,
	PROP_IRADIO_SOURCE,
};

enum
{
	PLAYLIST_ADDED,
	PLAYLIST_LOAD_START,
	PLAYLIST_LOAD_FINISH,
	LAST_SIGNAL,
};

static guint rb_playlist_manager_signals[LAST_SIGNAL] = { 0 };

#define CMD_PATH_PLAYLIST_DELETE   "/commands/FileDeletePlaylist"
#define CMD_PATH_PLAYLIST_SAVE   "/commands/SavePlaylist"

static BonoboUIVerb rb_playlist_manager_verbs[] =
{
 	BONOBO_UI_VERB ("NewPlaylist",  (BonoboUIVerbFn) rb_playlist_manager_cmd_new_playlist),
 	BONOBO_UI_VERB ("NewAutomaticPlaylist",  (BonoboUIVerbFn) rb_playlist_manager_cmd_new_automatic_playlist),
	BONOBO_UI_VERB ("LoadPlaylist", (BonoboUIVerbFn) rb_playlist_manager_cmd_load_playlist),
	BONOBO_UI_VERB ("SavePlaylist", (BonoboUIVerbFn) rb_playlist_manager_cmd_save_playlist),
	BONOBO_UI_VERB ("FileDeletePlaylist",(BonoboUIVerbFn) rb_playlist_manager_cmd_delete_playlist),
	BONOBO_UI_VERB ("DeletePlaylist",  (BonoboUIVerbFn) rb_playlist_manager_cmd_delete_playlist),
 	BONOBO_UI_VERB ("EditAutomaticPlaylist",  (BonoboUIVerbFn) rb_playlist_manager_cmd_edit_automatic_playlist),
	BONOBO_UI_VERB_END
};

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
					 PROP_WINDOW,
					 g_param_spec_object ("window",
							      "window",
							      "toplevel window",
							      GTK_TYPE_WINDOW,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_COMPONENT,
					 g_param_spec_object ("component",
							      "BonoboUIComponent",
							      "BonoboUIComponent object",
							      BONOBO_TYPE_UI_COMPONENT,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
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

	g_list_free (mgr->priv->playlists);

	g_free (mgr->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
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

		mgr->priv->selected_source = g_value_get_object (value);

		playlist_active = g_list_find (mgr->priv->playlists,
					       mgr->priv->selected_source) != NULL;
		rb_bonobo_set_sensitive (mgr->priv->component, CMD_PATH_PLAYLIST_SAVE,
					 playlist_active);
		rb_bonobo_set_sensitive (mgr->priv->component, CMD_PATH_PLAYLIST_DELETE,
					 playlist_active);
		break;
	}
	case PROP_COMPONENT:
		mgr->priv->component = g_value_get_object (value);
		bonobo_ui_component_add_verb_list_with_data (mgr->priv->component,
							     rb_playlist_manager_verbs,
							     mgr);
		break;
	case PROP_DB:
		mgr->priv->db = g_value_get_object (value);
		break;
	case PROP_SOURCELIST:
		mgr->priv->sourcelist = g_value_get_object (value);
		break;
	case PROP_LIBRARY_SOURCE:
		mgr->priv->libsource = g_value_get_object (value);
		break;
	case PROP_IRADIO_SOURCE:
		mgr->priv->iradio_source = g_value_get_object (value);
		break;
	case PROP_WINDOW:
		mgr->priv->window = g_value_get_object (value);
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
	case PROP_COMPONENT:
		g_value_set_object (value, mgr->priv->component);
		break;
	case PROP_DB:
		g_value_set_object (value, mgr->priv->db);
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
	case PROP_WINDOW:
		g_value_set_object (value, mgr->priv->window);
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
rb_playlist_manager_new (BonoboUIComponent *component, GtkWindow *window,
			 RhythmDB *db, RBSourceList *sourcelist,
			 RBLibrarySource *libsource,
			 RBIRadioSource *iradio_source)
{
	RBPlaylistManager *mgr = g_object_new (RB_TYPE_PLAYLIST_MANAGER,
					       "component", component,
					       "window", window,
					       "db", db,
					       "sourcelist", sourcelist,
					       "library_source", libsource,
					       "iradio_source", iradio_source,
					       NULL);

	g_return_val_if_fail (mgr->priv != NULL, NULL);

	return mgr;
}

const char *
rb_playlist_manager_parse_file (RBPlaylistManager *mgr, const char *uri)
{
	rb_debug ("loading playlist from %s", uri);

	g_free (mgr->priv->firsturi);
	mgr->priv->firsturi = NULL;

	g_signal_emit (G_OBJECT (mgr), rb_playlist_manager_signals[PLAYLIST_LOAD_START], 0);

	{
		RBPlaylist *parser = rb_playlist_new ();

		g_signal_connect_object (G_OBJECT (parser), "entry",
					 G_CALLBACK (handle_playlist_entry_into_playlist_cb),
					 mgr, 0);

		if (!rb_playlist_parse (parser, uri))
			rb_error_dialog (_("Couldn't parse playlist"));
		mgr->priv->loading_playlist = NULL;

		g_object_unref (G_OBJECT (parser));
	}

	g_signal_emit (G_OBJECT (mgr), rb_playlist_manager_signals[PLAYLIST_LOAD_FINISH], 0);
	return mgr->priv->firsturi;
}

static void
rb_playlist_manager_source_deleted_cb (RBSource *source, RBPlaylistManager *mgr)
{
	rb_debug ("removing playlist %p", source);
	mgr->priv->playlists = g_list_remove (mgr->priv->playlists, source);
}

static void
append_new_playlist_source (RBPlaylistManager *mgr, RBPlaylistSource *source)
{
	mgr->priv->playlists = g_list_append (mgr->priv->playlists, source);
	g_signal_connect_object (G_OBJECT (source), "deleted",
				 G_CALLBACK (rb_playlist_manager_source_deleted_cb), mgr, 0);
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

		playlist = rb_playlist_source_new_from_xml (mgr->priv->db,
							    child);
		append_new_playlist_source (mgr, RB_PLAYLIST_SOURCE (playlist));
	}

	xmlFreeDoc (doc);
out:
	g_free (file);
}

static void
rb_playlist_manager_save_playlists_worker (RBPlaylistManager *mgr)
{
	char *file;
	char *tmpname;
	GList *tmp;
	xmlDocPtr doc;
	xmlNodePtr root;

	g_mutex_lock (mgr->priv->saving_mutex);

	if (mgr->priv->saving) {
		rb_debug ("already saving, ignoring");	
		g_mutex_unlock (mgr->priv->saving_mutex);
		return;
	}

	if (!mgr->priv->dirty) {
		rb_debug ("no save needed, ignoring");
		g_mutex_unlock (mgr->priv->saving_mutex);
		return;
	}

	mgr->priv->saving = TRUE;

	g_mutex_unlock (mgr->priv->saving_mutex);

	rb_debug ("saving playlists");
			
	file = g_build_filename (rb_dot_dir (), "playlists.xml", NULL);
	tmpname = g_strconcat (file, ".tmp", NULL);

	doc = xmlNewDoc ("1.0");

	root = xmlNewDocNode (doc, NULL, "rhythmdb-playlists", NULL);
	xmlDocSetRootElement (doc, root);
	
	for (tmp = mgr->priv->playlists; tmp; tmp = tmp->next)
		rb_playlist_source_save_to_xml (RB_PLAYLIST_SOURCE (tmp->data), root);

	xmlSaveFormatFile (tmpname, doc, 1);
	rename (tmpname, file);
	xmlFreeDoc (doc);
	g_free (tmpname);
	g_free (file);

	g_mutex_lock (mgr->priv->saving_mutex);

	mgr->priv->saving = FALSE;
	mgr->priv->dirty = FALSE;

	g_mutex_unlock (mgr->priv->saving_mutex);

	g_cond_broadcast (mgr->priv->saving_condition);
}

static gpointer
rb_playlist_manager_save_thread_main (RBPlaylistManager *mgr)
{
	rb_debug ("entering save thread");
	
	rb_playlist_manager_save_playlists_worker (mgr);

	g_async_queue_push (mgr->priv->status_queue, mgr);
	
	return NULL;
}

void
rb_playlist_manager_save_playlists (RBPlaylistManager *mgr)
{
	rb_debug ("saving the playlists in the background");

	g_object_ref (G_OBJECT (mgr));
	mgr->priv->outstanding_threads++;
	
	g_thread_create ((GThreadFunc) rb_playlist_manager_save_thread_main, mgr, FALSE, NULL);
}

void
rb_playlist_manager_save_playlists_blocking (RBPlaylistManager *mgr)
{
	rb_debug("saving the playlists and blocking");
	
	rb_playlist_manager_save_playlists_worker (mgr);
	
	g_mutex_lock (mgr->priv->saving_mutex);

	while (mgr->priv->saving)
		g_cond_wait (mgr->priv->saving_condition, mgr->priv->saving_mutex);

	g_mutex_unlock (mgr->priv->saving_mutex);
}

RBSource *
rb_playlist_manager_new_playlist (RBPlaylistManager *mgr,
				  const char *suggested_name, gboolean automatic)
{
	RBSource *playlist = RB_SOURCE (rb_playlist_source_new (mgr->priv->db, automatic));
	GTimeVal serial;
	char *internal;

	g_get_current_time (&serial);
	internal = g_strdup_printf ("<playlist:%ld:%ld>", serial.tv_sec,
				    serial.tv_usec);
	
	g_object_set (G_OBJECT (playlist),
		      "name", suggested_name ? suggested_name : "",
		      "internal-name", internal,
		      NULL);
	g_free (internal);
	append_new_playlist_source (mgr, RB_PLAYLIST_SOURCE (playlist));
	rb_sourcelist_edit_source_name (mgr->priv->sourcelist, playlist);
	return playlist;
}

static void
rb_playlist_manager_cmd_new_playlist (BonoboUIComponent *component,
				      RBPlaylistManager *mgr,
				      const char *verbname)
{
	rb_playlist_manager_new_playlist (mgr, NULL, FALSE);
}

static void
rb_playlist_manager_set_automatic_playlist (RBPlaylistManager *mgr,
					    RBPlaylistSource *playlist,
					    RBQueryCreator *creator)
{
	RBQueryCreatorLimitType type;
	guint limit, limit_count = 0, limit_size = 0;

	rb_query_creator_get_limit (creator, &type, &limit);
	if (type == RB_QUERY_CREATOR_LIMIT_COUNT)
		limit_count = limit;
	if (type == RB_QUERY_CREATOR_LIMIT_MB)
		limit_size = limit;
	rb_playlist_source_set_query (playlist,
				      rb_query_creator_get_query (creator),
				      limit_count, limit_size);
}

static void
rb_playlist_manager_cmd_new_automatic_playlist (BonoboUIComponent *component,
						RBPlaylistManager *mgr,
						const char *verbname)
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

	gtk_widget_destroy (GTK_WIDGET (creator));	
}

static void
rb_playlist_manager_cmd_edit_automatic_playlist (BonoboUIComponent *component,
						 RBPlaylistManager *mgr,
						 const char *verbname)
{
	RBQueryCreator *creator;
	RBPlaylistSource *playlist;
	GPtrArray *query;
	guint limit_count = 0, limit_size = 0;
	

	playlist = RB_PLAYLIST_SOURCE (mgr->priv->selected_source);
	rb_playlist_source_get_query (playlist, &query, &limit_count, &limit_size);

	creator = RB_QUERY_CREATOR (rb_query_creator_new_from_query (mgr->priv->db,
								     query,
								     limit_count,
								     limit_size));	

	gtk_dialog_run (GTK_DIALOG (creator));

	rb_playlist_manager_set_automatic_playlist (mgr, playlist, creator);
	gtk_widget_destroy (GTK_WIDGET (creator));	
}

static void
rb_playlist_manager_cmd_delete_playlist (BonoboUIComponent *component,
					 RBPlaylistManager *mgr,
					 const char *verbname)
{
	rb_debug ("Deleting playlist %p", mgr->priv->selected_source);
	
	rb_source_delete_thyself (mgr->priv->selected_source);
}

static void
load_playlist_response_cb (GtkDialog *dialog,
			   int response_id,
			   RBPlaylistManager *mgr)
{
	char *file, *escaped_file;

	if (response_id != GTK_RESPONSE_OK) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	file = g_strdup (gtk_file_selection_get_filename (GTK_FILE_SELECTION (dialog)));

	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (file == NULL)
		return;

	escaped_file = gnome_vfs_get_uri_from_local_path (file);
	g_free (file);

	rb_playlist_manager_parse_file (mgr, escaped_file);
	g_free (escaped_file);
}

static void
rb_playlist_manager_cmd_load_playlist (BonoboUIComponent *component,
				       RBPlaylistManager *mgr,
				       const char *verbname)
{
	GtkWidget *dialog;

	dialog = rb_ask_file_multiple (_("Load playlist"), NULL,
			              GTK_WINDOW (mgr->priv->window));

	g_signal_connect_object (G_OBJECT (dialog), "response",
				 G_CALLBACK (load_playlist_response_cb), mgr, 0);
}

static void
save_playlist_response_cb (GtkDialog *dialog,
			   int response_id,
			   RBPlaylistManager *mgr)
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

	rb_playlist_source_save_playlist (RB_PLAYLIST_SOURCE (mgr->priv->selected_source), file);
	g_free (file);
}

static void
rb_playlist_manager_cmd_save_playlist (BonoboUIComponent *component,
				       RBPlaylistManager *mgr,
				       const char *verbname)
{
	GtkWidget *dialog;
    
	dialog = rb_ask_file_multiple (_("Save playlist"), NULL,
			              GTK_WINDOW (mgr->priv->window));

	g_signal_connect_object (G_OBJECT (dialog), "response",
				 G_CALLBACK (save_playlist_response_cb),
				 mgr, 0);
}

static void
add_uri_to_playlist (RBPlaylistManager *mgr, RBPlaylistSource *playlist, const char *uri, const char *title)
{
	GError *error = NULL;
	GnomeVFSURI *vfsuri = gnome_vfs_uri_new (uri);
	const char *scheme = gnome_vfs_uri_get_scheme (vfsuri);

	if (rb_uri_is_iradio (scheme)) {
		rb_iradio_source_add_station (mgr->priv->iradio_source, uri, title, NULL);
		goto out;
	}

	rhythmdb_add_song (mgr->priv->db, uri, &error);
	if (error) {
		rb_debug ("error loading URI %s", uri);
		goto out; /* FIXME */
	}

	rb_playlist_source_add_location (playlist, uri);
out:
	gnome_vfs_uri_unref (vfsuri);
}

static void
handle_playlist_entry_into_playlist_cb (RBPlaylist *playlist, const char *uri, const char *title,
					const char *genre, RBPlaylistManager *mgr)
{
	if (!mgr->priv->firsturi)
		mgr->priv->firsturi = g_strdup (uri);
	if (rb_uri_is_iradio (uri)) {
		rb_iradio_source_add_station (mgr->priv->iradio_source,
					      uri, title, genre);
		return;
	}

	if (!mgr->priv->loading_playlist) {
		mgr->priv->loading_playlist =
			RB_PLAYLIST_SOURCE (rb_playlist_manager_new_playlist (mgr, NULL, FALSE));
	}
	add_uri_to_playlist (mgr, mgr->priv->loading_playlist, uri, title);
}
