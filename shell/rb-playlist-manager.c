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

	RBPlaylistSource *loading_playlist;

	char *firsturi;
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
}

static void
rb_playlist_manager_finalize (GObject *object)
{
	RBPlaylistManager *mgr;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PLAYLIST_MANAGER (object));

	mgr = RB_PLAYLIST_MANAGER (object);

	g_return_if_fail (mgr->priv != NULL);


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

		g_signal_connect (G_OBJECT (parser), "entry",
				  G_CALLBACK (handle_playlist_entry_into_playlist_cb),
				  mgr);

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
	g_signal_connect (G_OBJECT (source), "deleted",
			  G_CALLBACK (rb_playlist_manager_source_deleted_cb), mgr);
	g_signal_emit (G_OBJECT (mgr), rb_playlist_manager_signals[PLAYLIST_ADDED], 0,
		       source);
}

static void
rb_playlist_manager_load_legacy_playlist (RBPlaylistManager *mgr,
					  xmlNodePtr root,
					  RBPlaylistSource *source)
{
	xmlNodePtr child;
	for (child = root->children; child != NULL; child = child->next) {
		long id;
		char *tmp;
		RhythmDBEntry *entry;
		const char *location;

		tmp = xmlGetProp (child, "id");
		if (tmp == NULL)
			continue;
		id = atol (tmp);
		g_free (tmp);

		entry = rhythmdb_legacy_id_to_entry (mgr->priv->db, id);
		if (!entry) {
			rb_debug ("invalid legacy id %d", id);
			continue;
		}

		location = rhythmdb_entry_get_string (mgr->priv->db, entry,
						      RHYTHMDB_PROP_LOCATION);

		rb_playlist_source_add_location (source, location);
	}
}

void
rb_playlist_manager_load_legacy_playlists (RBPlaylistManager *mgr)
{
	char *oldpath, *path;
	GnomeVFSDirectoryHandle *handle;
	GnomeVFSResult result;
	GnomeVFSFileInfo *info;

	/* Backwards compatibility with old "group" name */
	oldpath = g_build_filename (rb_dot_dir (), "groups", NULL);
	path = g_build_filename (rb_dot_dir (), "playlists", NULL);

	if ((result = gnome_vfs_directory_open (&handle, oldpath, GNOME_VFS_FILE_INFO_FOLLOW_LINKS))
	    == GNOME_VFS_OK) {
		rb_debug ("Renaming legacy group dir");
		gnome_vfs_move (oldpath, path, TRUE);
	}

	g_free (oldpath);

	if ((result = gnome_vfs_directory_open (&handle, path, GNOME_VFS_FILE_INFO_FOLLOW_LINKS))
	    != GNOME_VFS_OK)
		goto out;

	info = gnome_vfs_file_info_new ();
	while ((result = gnome_vfs_directory_read_next (handle, info)) == GNOME_VFS_OK) {
		RBPlaylistSource *playlist;
		RhythmDBQueryModel *model;
		char *filepath, *name, *xml;
		xmlDocPtr doc;
		xmlNodePtr root;

		if (info->name[0] == '.')
			continue;

		filepath = g_build_filename (path, info->name, NULL);

		doc = xmlParseFile (filepath);
		
		if (doc == NULL)
			continue;

		root = xmlDocGetRootElement (doc);
		xml = xmlGetProp (root, "version");
		if (xml == NULL || strcmp (xml, "1.0") != 0) {
			g_free (xml);
			xmlFreeDoc (doc);
			continue;
		}
		g_free (xml);

		name = xmlGetProp (root, "name");
		if (name == NULL) {
			xmlFreeDoc (doc);
			continue;
		}

		rhythmdb_read_lock (mgr->priv->db);

		playlist = RB_PLAYLIST_SOURCE (rb_playlist_source_new (mgr->priv->db, FALSE));
		g_object_set (G_OBJECT (playlist), "name", name, NULL);
		g_free (name);

		model = rb_playlist_source_get_model (playlist);
		
		rb_playlist_manager_load_legacy_playlist (mgr, root,
							  playlist);

		append_new_playlist_source (mgr, playlist);

		rhythmdb_read_unlock (mgr->priv->db);
		xmlFreeDoc (doc);
		g_free (filepath);
	}

	gnome_vfs_file_info_unref (info);
out:
	g_free (path);
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

void
rb_playlist_manager_save_playlists (RBPlaylistManager *mgr)
{
	char *file = g_build_filename (rb_dot_dir (), "playlists.xml", NULL);
	char *tmpname = g_strconcat (file, ".tmp", NULL);
	GList *tmp;
	xmlDocPtr doc;
	xmlNodePtr root;

	rb_debug ("saving playlists");

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
}

RBSource *
rb_playlist_manager_new_playlist (RBPlaylistManager *mgr, gboolean automatic)
{
	RBSource *playlist = RB_SOURCE (rb_playlist_source_new (mgr->priv->db, automatic));
	g_object_set (G_OBJECT (playlist), "name", "", NULL);
	append_new_playlist_source (mgr, RB_PLAYLIST_SOURCE (playlist));
	rb_sourcelist_edit_source_name (mgr->priv->sourcelist, playlist);
	return playlist;
}

static void
rb_playlist_manager_cmd_new_playlist (BonoboUIComponent *component,
				      RBPlaylistManager *mgr,
				      const char *verbname)
{
	rb_playlist_manager_new_playlist (mgr, FALSE);
}

static void
rb_playlist_manager_cmd_new_automatic_playlist (BonoboUIComponent *component,
						RBPlaylistManager *mgr,
						const char *verbname)
{
	RBQueryCreator *creator = RB_QUERY_CREATOR (rb_query_creator_new (mgr->priv->db));
	RBSource *playlist;
	RBQueryCreatorLimitType type;
	guint limit, limit_count = 0, limit_size = 0;
	
	switch (gtk_dialog_run (GTK_DIALOG (creator)))
	{
	case GTK_RESPONSE_NONE:
	case GTK_RESPONSE_CLOSE:
		gtk_widget_destroy (GTK_WIDGET (creator));	
		return;
	}

	rb_query_creator_get_limit (creator, &type, &limit);
	if (type == RB_QUERY_CREATOR_LIMIT_COUNT)
		limit_count = limit;
	if (type == RB_QUERY_CREATOR_LIMIT_MB)
		limit_size = limit;
	playlist = rb_playlist_manager_new_playlist (mgr, TRUE);
	rb_playlist_source_set_query (RB_PLAYLIST_SOURCE (playlist),
				      rb_query_creator_get_query (creator),
				      limit_count, limit_size);
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

	g_signal_connect (G_OBJECT (dialog), "response",
			  G_CALLBACK (load_playlist_response_cb), mgr);
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

/* static gboolean */
/* rb_playlist_manager_validate_name (RBPlaylistManager *mgr, const char *name, gboolean error_dialog) */
/* { */
/* 	GList *tem; */
/* 	char *temname; */

/* 	for (tem = mgr->priv->playlists; tem; tem = g_list_next (tem)) { */
/* 		g_object_get (G_OBJECT (tem->data), "name", &temname, NULL); */
/* 		if (!strcmp (temname, name)) { */
/* 			if (error_dialog) */
/* 				rb_error_dialog (_("There is already a playlist with that name.")); */
/* 			return FALSE; */
/* 		} */
/* 	} */
/* 	return TRUE; */
/* } */

static void
rb_playlist_manager_cmd_save_playlist (BonoboUIComponent *component,
				       RBPlaylistManager *mgr,
				       const char *verbname)
{
	GtkWidget *dialog;
    
	dialog = rb_ask_file_multiple (_("Save playlist"), NULL,
			              GTK_WINDOW (mgr->priv->window));

	g_signal_connect (G_OBJECT (dialog), "response",
			  G_CALLBACK (save_playlist_response_cb),
			  mgr);
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
			RB_PLAYLIST_SOURCE (rb_playlist_manager_new_playlist (mgr, FALSE));
	}
	add_uri_to_playlist (mgr, mgr->priv->loading_playlist, uri, title);
}
