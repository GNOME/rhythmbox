/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2003,2004 Colin Walters <walters@gnome.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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

/**
 * SECTION:rbplaylistmanager
 * @short_description: Playlist management object
 *
 * The playlist manager loads and saves the on-disk playlist file, provides
 * UI actions and a DBus interface for dealing with playlists, and internal
 * interfaces for creating playlists.
 */

#include "config.h"

#include <string.h>
#include <stdio.h>      /* rename() */
#include <unistd.h>     /* unlink() */

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-playlist-manager.h"
#include "rb-playlist-source.h"
#include "rb-static-playlist-source.h"
#include "rb-auto-playlist-source.h"
#include "rb-play-queue-source.h"
#include "rb-query-creator.h"
#include "totem-pl-parser.h"

#include "rb-file-helpers.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rhythmdb.h"
#include "rb-builder-helpers.h"
#include "rb-util.h"
#include "rb-application.h"
#include "rb-display-page-menu.h"

#define RB_PLAYLIST_MGR_VERSION (xmlChar *) "1.0"
#define RB_PLAYLIST_MGR_PL (xmlChar *) "rhythmdb-playlists"

#define RB_PLAYLIST_MANAGER_IFACE_NAME "org.gnome.Rhythmbox3.PlaylistManager"
#define RB_PLAYLIST_MANAGER_DBUS_PATH "/org/gnome/Rhythmbox3/PlaylistManager"

static const char *rb_playlist_manager_dbus_spec =
"<node>"
"  <interface name='org.gnome.Rhythmbox3.PlaylistManager'>"
"    <method name='GetPlaylists'>"
"      <arg type='as' direction='out'/>"
"    </method>"
"    <method name='CreatePlaylist'>"
"      <arg type='s' name='name'/>"
"    </method>"
"    <method name='DeletePlaylist'>"
"      <arg type='s' name='name'/>"
"    </method>"
"    <method name='AddToPlaylist'>"
"      <arg type='s' name='playlist'/>"
"      <arg type='s' name='uri'/>"
"    </method>"
"    <method name='RemoveFromPlaylist'>"
"      <arg type='s' name='playlist'/>"
"      <arg type='s' name='uri'/>"
"    </method>"
"    <method name='ExportPlaylist'>"
"      <arg type='s' name='playlist'/>"
"      <arg type='s' name='uri'/>"
"      <arg type='b' name='mp3_format'/>"
"    </method>"
"    <method name='ImportPlaylist'>"
"      <arg type='s' name='uri'/>"
"    </method>"
"  </interface>"
"</node>";

static void rb_playlist_manager_class_init (RBPlaylistManagerClass *klass);
static void rb_playlist_manager_init (RBPlaylistManager *mgr);

static void new_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);
static void new_auto_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);
static void load_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);

static void edit_auto_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);
static void rename_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);
static void queue_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);
static void shuffle_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);
static void save_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);
static void add_to_new_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);
static void add_to_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);

struct RBPlaylistManagerPrivate
{
	RhythmDB *db;
	RBShell *shell;
	RBSource *selected_source;

	char *playlists_file;

	RBStaticPlaylistSource *loading_playlist;
	RBSource *new_playlist;

	gint dirty;
	gint saving;
	GMutex saving_mutex;
};

enum
{
	PROP_0,
	PROP_PLAYLIST_NAME,
	PROP_SHELL,
	PROP_SOURCE
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

typedef struct {
  const gchar *description;
  /* NULL terminated array of extensions for this file format.  The first
   * one is the prefered extension for files of this type. */
  const gchar **extensions;
  const char *mimetype;
  const RBPlaylistExportType type;
} RBPlaylistExportFilter;

static const char *m3u_extensions [] = {"m3u", NULL};
static const char *pls_extensions [] = {"pls", NULL};
static const char *xspf_extensions[] = {"xspf", NULL};

static RBPlaylistExportFilter playlist_formats[] = {
	{N_("MPEG Version 3.0 URL"), m3u_extensions, "audio/x-mpegurl", RB_PLAYLIST_EXPORT_TYPE_M3U},
	{N_("Shoutcast playlist"), pls_extensions, "audio/x-scpls", RB_PLAYLIST_EXPORT_TYPE_PLS},
	{N_("XML Shareable Playlist Format"), xspf_extensions, "application/xspf+xml", RB_PLAYLIST_EXPORT_TYPE_XSPF},
};


G_DEFINE_TYPE (RBPlaylistManager, rb_playlist_manager, G_TYPE_OBJECT)


/**
 * rb_playlist_manager_shutdown:
 * @mgr: the #RBPlaylistManager
 *
 * Shuts down the playlist manager, making sure any outstanding playlist save
 * operation finishes.
 */
void
rb_playlist_manager_shutdown (RBPlaylistManager *mgr)
{
	g_return_if_fail (RB_IS_PLAYLIST_MANAGER (mgr));

	g_mutex_lock (&mgr->priv->saving_mutex);
	g_mutex_unlock (&mgr->priv->saving_mutex);
}

/**
 * rb_playlist_manager_new:
 * @shell: the #RBShell
 * @playlists_file: the full path to the playlist file to load
 *
 * Creates the #RBPlaylistManager instance
 *
 * Return value: the #RBPlaylistManager
 */
RBPlaylistManager *
rb_playlist_manager_new (RBShell *shell,
			 const char *playlists_file)
{
	return g_object_new (RB_TYPE_PLAYLIST_MANAGER,
			     "shell", shell,
			     "playlists_file", playlists_file,
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
handle_playlist_entry_cb (TotemPlParser *playlist,
			  const char *uri_maybe,
			  GHashTable *metadata,
			  RBPlaylistManager *mgr)
{
	char *uri;
	const char *title, *genre;

	title = g_hash_table_lookup (metadata, TOTEM_PL_PARSER_FIELD_TITLE);
	genre = g_hash_table_lookup (metadata, TOTEM_PL_PARSER_FIELD_GENRE);

	uri = rb_canonicalise_uri (uri_maybe);
	g_return_if_fail (uri != NULL);

	rb_debug ("adding uri %s (title %s, genre %s) from playlist",
		  uri, title, genre);
	if (!rb_shell_add_uri (mgr->priv->shell,
			       uri,
			       title,
			       genre,
			       NULL))
		return;

	if (!mgr->priv->loading_playlist) {
		mgr->priv->loading_playlist =
			RB_STATIC_PLAYLIST_SOURCE (rb_playlist_manager_new_playlist (mgr, NULL, FALSE));
	}
	if (rb_source_want_uri (RB_SOURCE (mgr->priv->loading_playlist), uri) > 0) {
		rb_debug ("adding uri %s to playlist", uri);
		rb_static_playlist_source_add_location (mgr->priv->loading_playlist, uri, -1);
	}

	g_free (uri);
}

static void
playlist_load_started_cb (TotemPlParser *parser, const char *uri, GHashTable *metadata, RBPlaylistManager *mgr)
{
	const char *title;

	rb_debug ("loading new playlist %s", uri);

	title = g_hash_table_lookup (metadata, TOTEM_PL_PARSER_FIELD_TITLE);
	mgr->priv->loading_playlist =
			RB_STATIC_PLAYLIST_SOURCE (rb_playlist_manager_new_playlist (mgr, title, FALSE));
}

/**
 * rb_playlist_manager_parse_file:
 * @mgr: the #RBPlaylistManager
 * @uri: URI of the playlist to load
 * @error: returns a GError in case of error
 *
 * Parses a playlist file, adding entries to the database and to a new
 * static playlist.  If the playlist file includes a title, the static
 * playlist created will have the same title.
 *
 * Return value: TRUE on success
 **/
gboolean
rb_playlist_manager_parse_file (RBPlaylistManager *mgr, const char *uri, GError **error)
{
	rb_debug ("loading playlist from %s", uri);

	g_signal_emit (mgr, rb_playlist_manager_signals[PLAYLIST_LOAD_START], 0);

	{
		TotemPlParser *parser = totem_pl_parser_new ();

		g_signal_connect_object (parser, "entry-parsed",
					 G_CALLBACK (handle_playlist_entry_cb),
					 mgr, 0);

		g_signal_connect_object (parser, "playlist-started",
					 G_CALLBACK (playlist_load_started_cb),
					 mgr, 0);

		g_object_set (parser, "recurse", FALSE, NULL);

		if (totem_pl_parser_parse (parser, uri, TRUE) != TOTEM_PL_PARSER_RESULT_SUCCESS) {
			g_set_error (error,
				     RB_PLAYLIST_MANAGER_ERROR,
				     RB_PLAYLIST_MANAGER_ERROR_PARSE,
				     "%s",
				     _("The playlist file may be in an unknown format or corrupted."));
			return FALSE;
		}

		if (mgr->priv->loading_playlist != NULL) {
			char *name = NULL;

			/* totem-plparser may not have given us the playlist name */
			g_object_get (mgr->priv->loading_playlist, "name", &name, NULL);
			if (name == NULL || name[0] == '\0') {
				char *path;

				rb_debug ("setting playlist name from file name");
				path = g_filename_from_uri (uri, NULL, NULL);
				if (path) {
					name = g_path_get_basename (path);
					g_object_set (mgr->priv->loading_playlist, "name", name, NULL);
					g_free (path);
				}
			}

			g_free (name);
			mgr->priv->loading_playlist = NULL;
		}

		g_object_unref (parser);
	}

	g_signal_emit (mgr, rb_playlist_manager_signals[PLAYLIST_LOAD_FINISH], 0);
	return TRUE;
}

static void
append_new_playlist_source (RBPlaylistManager *mgr, RBPlaylistSource *source)
{
	g_signal_emit (mgr, rb_playlist_manager_signals[PLAYLIST_ADDED], 0,
		       source);
}

/**
 * rb_playlist_manager_load_playlists:
 * @mgr: the #RBPlaylistManager
 *
 * Loads the user's playlists, or if the playlist file does not exists,
 * reads the default playlist file.  Should be called only once on startup.
 **/
void
rb_playlist_manager_load_playlists (RBPlaylistManager *mgr)
{
	GBytes *data = NULL;
	xmlDocPtr doc;
	xmlNodePtr root;
	xmlNodePtr child;

	/* block saves until the playlists have loaded */
	g_mutex_lock (&mgr->priv->saving_mutex);

	if (g_file_test (mgr->priv->playlists_file, G_FILE_TEST_EXISTS) == FALSE) {
		rb_debug ("personal playlists not found, loading defaults");
		data = g_resources_lookup_data ("/org/gnome/Rhythmbox/playlists.xml",
						G_RESOURCE_LOOKUP_FLAGS_NONE,
						NULL);
		if (data == NULL) {
			rb_debug ("couldn't find default playlists resource");
			goto out;
		}
	}

	if (data != NULL)
		doc = xmlParseMemory (g_bytes_get_data (data, NULL), g_bytes_get_size (data));
	else
		doc = xmlParseFile (mgr->priv->playlists_file);
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
	g_mutex_unlock (&mgr->priv->saving_mutex);
}

static void
rb_playlist_manager_set_dirty (RBPlaylistManager *mgr, gboolean dirty)
{
	g_atomic_int_compare_and_exchange (&mgr->priv->dirty, dirty == FALSE, dirty == TRUE);
}

static gboolean
_is_dirty_playlist (GtkTreeModel *model,
		    GtkTreePath *path,
		    GtkTreeIter *iter,
		    gboolean *dirty)
{
	RBDisplayPage *page;
	gboolean local;
	gboolean ret;

	gtk_tree_model_get (model,
			    iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE,
			    &page,
			    -1);
	if (page == NULL) {
		return FALSE;
	}
	if (RB_IS_PLAYLIST_SOURCE (page) == FALSE) {
		g_object_unref (page);
		return FALSE;
	}

	ret = FALSE;
	g_object_get (page, "is-local", &local, NULL);
	if (local) {
		gboolean pdirty;

		g_object_get (page, "dirty", &pdirty, NULL);
		if (pdirty) {
			*dirty = TRUE;
			ret = TRUE;
		}
	}
	g_object_unref (page);

	return ret;
}

/* returns TRUE if a playlist has been created, modified, or deleted since last save */
static gboolean
rb_playlist_manager_is_dirty (RBPlaylistManager *mgr)
{
	gboolean dirty = FALSE;
	RBDisplayPageModel *page_model;

	g_object_get (mgr->priv->shell, "display-page-model", &page_model, NULL);

	gtk_tree_model_foreach (GTK_TREE_MODEL (page_model),
				(GtkTreeModelForeachFunc) _is_dirty_playlist,
				&dirty);
	g_object_unref (page_model);

	/* explicitly check the play queue */
	if (dirty == FALSE) {
		RBSource *queue_source;

		g_object_get (mgr->priv->shell, "queue-source", &queue_source, NULL);
		g_object_get (queue_source, "dirty", &dirty, NULL);
		g_object_unref (queue_source);
	}

	if (!dirty)
		dirty = g_atomic_int_get (&mgr->priv->dirty);

	return dirty;
}

struct RBPlaylistManagerSaveData
{
	RBPlaylistManager *mgr;
	xmlDocPtr doc;
};

static gpointer
rb_playlist_manager_save_data (struct RBPlaylistManagerSaveData *data)
{
	char *file;
	char *tmpname;

	g_mutex_lock (&data->mgr->priv->saving_mutex);

	file = g_strdup (data->mgr->priv->playlists_file);
	tmpname = g_strconcat (file, ".tmp", NULL);

	if (xmlSaveFormatFile (tmpname, data->doc, 1) != -1) {
		rename (tmpname, file);
	} else {
		rb_debug ("error in xmlSaveFormatFile(), not saving");
		unlink (tmpname);
		rb_playlist_manager_set_dirty (data->mgr, TRUE);
	}
	xmlFreeDoc (data->doc);
	g_free (tmpname);
	g_free (file);

	g_atomic_int_compare_and_exchange (&data->mgr->priv->saving, 1, 0);
	g_mutex_unlock (&data->mgr->priv->saving_mutex);

	g_object_unref (data->mgr);

	g_free (data);
	return NULL;
}

static gboolean
save_playlist_cb (GtkTreeModel *model,
		  GtkTreePath  *path,
		  GtkTreeIter  *iter,
		  xmlNodePtr    root)
{
	RBDisplayPage *page;
	gboolean  local;

	gtk_tree_model_get (model,
			    iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	if (page == NULL) {
		goto out;
	}
	if (RB_IS_PLAYLIST_SOURCE (page) == FALSE || RB_IS_PLAY_QUEUE_SOURCE (page)) {
		goto out;
	}

	g_object_get (page, "is-local", &local, NULL);
	if (local) {
		rb_playlist_source_save_to_xml (RB_PLAYLIST_SOURCE (page), root);
	}
 out:
	if (page != NULL) {
		g_object_unref (page);
	}

	return FALSE;
}

/**
 * rb_playlist_manager_save_playlists:
 * @mgr: the #RBPlaylistManager
 * @force: if TRUE, save playlists synchronously and unconditionally
 *
 * Saves the user's playlists.  If the force flag is
 * TRUE, the playlists will always be saved.  Otherwise, the playlists
 * will only be saved if a playlist has been created, modified, or deleted
 * since the last time the playlists were saved, and no save operation is
 * currently taking place.
 *
 * Return value: TRUE if a playlist save operation has been started
 **/
gboolean
rb_playlist_manager_save_playlists (RBPlaylistManager *mgr, gboolean force)
{
	xmlNodePtr root;
	struct RBPlaylistManagerSaveData *data;
	RBDisplayPageModel *page_model;
	RBSource *queue_source;

	if (!force && !rb_playlist_manager_is_dirty (mgr)) {
		/* playlists already in sync, so don't bother */
		return FALSE;
	}

	if (!g_atomic_int_compare_and_exchange (&mgr->priv->saving, 0, 1) && !force) {
		/* already saving, so don't bother */
		return FALSE;
	}

	data = g_new0 (struct RBPlaylistManagerSaveData, 1);
	data->mgr = mgr;
	data->doc = xmlNewDoc (RB_PLAYLIST_MGR_VERSION);
	g_object_ref (mgr);

	root = xmlNewDocNode (data->doc, NULL, RB_PLAYLIST_MGR_PL, NULL);
	xmlDocSetRootElement (data->doc, root);

	g_object_get (mgr->priv->shell,
		      "display-page-model", &page_model,
		      "queue-source", &queue_source,
		      NULL);
	gtk_tree_model_foreach (GTK_TREE_MODEL (page_model),
				(GtkTreeModelForeachFunc)save_playlist_cb,
				root);

	/* also save the play queue */
	rb_playlist_source_save_to_xml (RB_PLAYLIST_SOURCE (queue_source), root);

	g_object_unref (page_model);
	g_object_unref (queue_source);

	/* mark clean here.  if the save fails, we'll mark it dirty again */
	rb_playlist_manager_set_dirty (data->mgr, FALSE);

	if (force)
		rb_playlist_manager_save_data (data);
	else
		g_thread_new ("playlist-save", (GThreadFunc) rb_playlist_manager_save_data, data);

	return TRUE;
}

static void
new_playlist_deleted_cb (RBDisplayPage *page, RBPlaylistManager *mgr)
{
	if (RB_SOURCE (page) == mgr->priv->new_playlist) {
		g_clear_object (&mgr->priv->new_playlist);
	}
}

static gboolean
edit_new_playlist_name (RBPlaylistManager *mgr)
{
	RBDisplayPageTree *page_tree;
	if (mgr->priv->new_playlist != NULL) {
		g_object_get (mgr->priv->shell, "display-page-tree", &page_tree, NULL);
		rb_display_page_tree_edit_source_name (page_tree, mgr->priv->new_playlist);
		g_object_unref (page_tree);
		g_signal_handlers_disconnect_by_func (mgr->priv->new_playlist, new_playlist_deleted_cb, mgr);
		mgr->priv->new_playlist = NULL;
	}
	return FALSE;
}

/**
 * rb_playlist_manager_new_playlist:
 * @mgr: the #RBPlaylistManager
 * @suggested_name: optional name to use for the new playlist
 * @automatic: if TRUE, create an auto playlist
 *
 * Creates a new playlist and adds it to the source list.
 *
 * Return value: (transfer none): the new playlist object.
 */
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
							  NULL,
							  TRUE,
							  RHYTHMDB_ENTRY_TYPE_SONG);

	append_new_playlist_source (mgr, RB_PLAYLIST_SOURCE (playlist));

	rb_playlist_manager_set_dirty (mgr, TRUE);

	g_signal_emit (mgr, rb_playlist_manager_signals[PLAYLIST_CREATED], 0,
		       playlist);

	mgr->priv->new_playlist = playlist;
	g_signal_connect (playlist, "deleted", G_CALLBACK (new_playlist_deleted_cb), mgr);
	g_idle_add ((GSourceFunc)edit_new_playlist_name, mgr);

	return playlist;
}

static char *
create_name_from_selection_data (RBPlaylistManager *mgr,
				 GtkSelectionData *data)
{
	GdkAtom       type;
	char         *name = NULL;
	const guchar *selection_data_data;
	GList        *list;

	type = gtk_selection_data_get_data_type (data);
	selection_data_data = gtk_selection_data_get_data (data);

        if (type == gdk_atom_intern ("text/uri-list", TRUE) ||
	    type == gdk_atom_intern ("application/x-rhythmbox-entry", TRUE)) {
		gboolean is_id;
		list = rb_uri_list_parse ((const char *) selection_data_data);
		is_id = (type == gdk_atom_intern ("application/x-rhythmbox-entry", TRUE));

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
				const char    *e_artist;
				const char    *e_album;

				entry = rhythmdb_entry_lookup_from_string (mgr->priv->db,
									   (const char *)l->data,
									   is_id);
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
			rb_list_deep_free (list);
		}

	} else {
		char **names;

		names = g_strsplit ((char *) selection_data_data, "\r\n", 0);
		name = g_strjoinv (", ", names);
		g_strfreev (names);
	}

	if (name == NULL) {
		name = g_strdup (_("Untitled Playlist"));
	}

	return name;
}

/**
 * rb_playlist_manager_new_playlist_from_selection_data:
 * @mgr: the #RBPlaylistManager
 * @data: the #GtkSelectionData from which to create a playlist
 *
 * Creates a new playlist based on selection data from gtk.
 * Used to implement playlist creation through drag and drop
 * to the source list.
 *
 * Return value: (transfer none): the new playlist.
 **/
RBSource *
rb_playlist_manager_new_playlist_from_selection_data (RBPlaylistManager *mgr,
						      GtkSelectionData *data)
{
	RBSource *playlist;
	GdkAtom   type;
	gboolean  automatic = TRUE;
	char     *suggested_name;

	type = gtk_selection_data_get_data_type (data);

	if (type == gdk_atom_intern ("text/uri-list", TRUE) ||
	    type == gdk_atom_intern ("application/x-rhythmbox-entry", TRUE))
		automatic = FALSE;
	suggested_name = create_name_from_selection_data (mgr, data);

	playlist = rb_playlist_manager_new_playlist (mgr,
						     suggested_name,
						     automatic);
	g_free (suggested_name);

	return playlist;
}

static void
new_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	rb_playlist_manager_new_playlist (RB_PLAYLIST_MANAGER (data), _("New Playlist"), FALSE);
}

static void
rb_playlist_manager_set_automatic_playlist (RBPlaylistManager *mgr,
					    RBAutoPlaylistSource *playlist,
					    RBQueryCreator *creator)
{
	RhythmDBQueryModelLimitType limit_type;
	GVariant *limit_value = NULL;
	const char *sort_key;
	gint sort_direction;
	GPtrArray *query;

	rb_query_creator_get_limit (creator, &limit_type, &limit_value);
	rb_query_creator_get_sort_order (creator,
					 &sort_key,
					 &sort_direction);

	query = rb_query_creator_get_query (creator);
	rb_auto_playlist_source_set_query (RB_AUTO_PLAYLIST_SOURCE (playlist),
					   query,
					   limit_type,
					   limit_value,
					   sort_key,
					   sort_direction);
	rhythmdb_query_free (query);
	if (limit_value != NULL) {
		g_variant_unref (limit_value);
	}
}

static void
new_automatic_playlist_response_cb (GtkDialog *dialog, int response, RBPlaylistManager *mgr)
{
	RBSource *playlist;

	switch (response) {
	case GTK_RESPONSE_NONE:
	case GTK_RESPONSE_CLOSE:
		break;

	default:
		playlist = rb_playlist_manager_new_playlist (mgr, _("New Playlist"), TRUE);

		rb_playlist_manager_set_automatic_playlist (mgr,
							    RB_AUTO_PLAYLIST_SOURCE (playlist),
							    RB_QUERY_CREATOR (dialog));
		rb_playlist_manager_set_dirty (mgr, TRUE);
		break;
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
new_auto_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBPlaylistManager *mgr = RB_PLAYLIST_MANAGER (data);
	GtkWidget *creator;

	creator = rb_query_creator_new (mgr->priv->db);
	gtk_widget_show_all (creator);

	g_signal_connect (creator,
			  "response",
			  G_CALLBACK (new_automatic_playlist_response_cb),
			  mgr);
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
edit_auto_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBPlaylistManager *mgr = RB_PLAYLIST_MANAGER (data);
	RBQueryCreator *creator;
	RBAutoPlaylistSource *playlist;

	playlist = RB_AUTO_PLAYLIST_SOURCE (mgr->priv->selected_source);
	creator = g_object_get_data (G_OBJECT (playlist), "rhythmbox-playlist-editor");
	if (creator == NULL) {
		RhythmDBQueryModelLimitType limit_type;
		GVariant *limit_value = NULL;
		GPtrArray *query;
		char *sort_key;
		gint sort_direction;
		EditAutoPlaylistData *data;

		sort_key = NULL;
		rb_auto_playlist_source_get_query (playlist,
						   &query,
						   &limit_type,
						   &limit_value,
						   &sort_key,
						   &sort_direction);

		creator = RB_QUERY_CREATOR (rb_query_creator_new_from_query (mgr->priv->db,
									     query,
									     limit_type,
									     limit_value,
									     sort_key,
									     sort_direction));
		if (limit_value != NULL) {
			g_variant_unref (limit_value);
		}
		rhythmdb_query_free (query);
		g_free (sort_key);

		data = g_new0 (EditAutoPlaylistData, 1);
		data->mgr = mgr;
		data->playlist = playlist;
		data->creator = creator;
		data->creator_response_id =
			g_signal_connect (creator,
					  "response",
					  G_CALLBACK (edit_auto_playlist_response_cb),
					  data);

		g_object_set_data (G_OBJECT (playlist), "rhythmbox-playlist-editor", creator);
		data->playlist_deleted_id =
			g_signal_connect (playlist,
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
	rhythmdb_entry_unref (entry);

	return FALSE;
}

static void
queue_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBPlaylistManager *mgr = RB_PLAYLIST_MANAGER (data);
	RBSource *queue_source;
	RhythmDBQueryModel *model;

	g_object_get (mgr->priv->shell, "queue-source", &queue_source, NULL);
	g_object_get (mgr->priv->selected_source, "query-model", &model, NULL);

	gtk_tree_model_foreach (GTK_TREE_MODEL (model),
				(GtkTreeModelForeachFunc) _queue_track_cb,
				queue_source);

	g_object_unref (queue_source);
	g_object_unref (model);
}

static void
shuffle_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBPlaylistManager *mgr = RB_PLAYLIST_MANAGER (data);
	RhythmDBQueryModel *base_model;

	g_object_get (mgr->priv->selected_source, "base-query-model", &base_model, NULL);
	rhythmdb_query_model_shuffle_entries (base_model);
	g_object_unref (base_model);
}

static void
rename_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBPlaylistManager *mgr = RB_PLAYLIST_MANAGER (data);
	RBDisplayPageTree *page_tree;

	rb_debug ("Renaming playlist %p", mgr->priv->selected_source);

	g_object_get (mgr->priv->shell, "display-page-tree", &page_tree, NULL);
	rb_display_page_tree_edit_source_name (page_tree, mgr->priv->selected_source);
	g_object_unref (page_tree);

	rb_playlist_manager_set_dirty (mgr, TRUE);
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
				 "%s", error->message);
		g_error_free (error);
	}

	g_free (escaped_file);
	rb_playlist_manager_set_dirty (mgr, TRUE);
}

static void
load_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBPlaylistManager *mgr = RB_PLAYLIST_MANAGER (data);
	GtkWindow *window;
	GtkWidget *dialog;
	GtkFileFilter *filter;
	GtkFileFilter *filter_all;
	int i;

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("Playlists"));
	for (i = 0; i < G_N_ELEMENTS (playlist_formats); i++) {
		gtk_file_filter_add_mime_type (filter, playlist_formats[i].mimetype);
	}

	filter_all = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter_all, _("All Files"));
	gtk_file_filter_add_pattern (filter_all, "*");

	g_object_get (mgr->priv->shell, "window", &window, NULL);

	dialog = rb_file_chooser_new (_("Load Playlist"),
				      window,
				      GTK_FILE_CHOOSER_ACTION_OPEN,
				      FALSE);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter_all);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter);

	g_signal_connect_object (dialog, "response",
				 G_CALLBACK (load_playlist_response_cb), mgr, 0);

	g_object_unref (window);
}

static void
save_playlist_response_cb (GtkDialog *dialog,
			   int response_id,
			   RBSource *source)
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

		for (i = 0; i < G_N_ELEMENTS (playlist_formats); i++) {
			int j;

			/* determine the playlist type from the extension */
			for (j = 0; playlist_formats[i].extensions[j] != NULL; j++) {
				if (g_str_has_suffix (file, playlist_formats[i].extensions[j])) {
					export_type = playlist_formats[i].type;
					break;
				}
			}
		}
	} else {
		export_type = playlist_formats[index-1].type;
	}

	if (export_type == RB_PLAYLIST_EXPORT_TYPE_UNKNOWN) {
		rb_error_dialog (NULL, _("Couldn't save playlist"), _("Unsupported file extension given."));
	} else {
		rb_playlist_source_save_playlist (RB_PLAYLIST_SOURCE (source), file, export_type);
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

	extension = playlist_formats[index-1].extensions[0];
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

	for (i = 0; i < G_N_ELEMENTS (playlist_formats); i++) {
		gchar *filter_label;
		GtkTreeIter iter;

		filter_label = filter_get_export_filter_label (&playlist_formats[i]);
		gtk_list_store_insert_with_values (GTK_LIST_STORE (model), &iter, -1,
						   0, filter_label, -1);

		g_free (filter_label);
	}

	g_signal_connect_object (menu,
				 "changed", G_CALLBACK (export_set_extension_cb),
				 dialog, 0);
}

void
rb_playlist_manager_save_playlist_file (RBPlaylistManager *mgr, RBSource *source)
{
	GtkBuilder *builder;
	GtkWidget *dialog;
	GtkWidget *menu;
	char *name;
	char *tmp;

	g_return_if_fail (RB_IS_PLAYLIST_SOURCE (source));

	builder = rb_builder_load ("playlist-save.ui", mgr);
	dialog = GTK_WIDGET (gtk_builder_get_object (builder, "playlist_save_dialog"));

	menu = GTK_WIDGET (gtk_builder_get_object (builder, "playlist_format_menu"));
	setup_format_menu (menu, dialog);
	g_object_set_data (G_OBJECT (dialog), "export-menu", menu);

	g_object_get (source, "name", &name, NULL);
	tmp = g_strconcat (name, ".pls", NULL);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), tmp);
	g_free (tmp);
	g_free (name);

	/* FIXME: always has "by extension" as default (it should probably remember the last selection) */
	gtk_combo_box_set_active (GTK_COMBO_BOX (menu), 0);
	g_signal_connect_object (dialog, "response",
				 G_CALLBACK (save_playlist_response_cb),
				 source, 0);

	g_object_unref (builder);
}

static void
save_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBPlaylistManager *mgr = RB_PLAYLIST_MANAGER (data);
	rb_playlist_manager_save_playlist_file (mgr, mgr->priv->selected_source);
}

static void
add_to_new_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBPlaylistManager *mgr = RB_PLAYLIST_MANAGER (data);
	GList *entries;
	RBSource *playlist_source;

	rb_debug ("add to new playlist");

	entries = rb_source_copy (mgr->priv->selected_source);
	playlist_source = rb_playlist_manager_new_playlist (mgr, NULL, FALSE);
	rb_source_paste (playlist_source, entries);

	g_list_foreach (entries, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (entries);
}

static void
add_to_playlist_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBPlaylistManager *mgr = RB_PLAYLIST_MANAGER (data);
	RBDisplayPageModel *model;
	GList *entries;
	RBDisplayPage *playlist_source;

	g_object_get (mgr->priv->shell, "display-page-model", &model, NULL);
	playlist_source = rb_display_page_menu_get_page (model, parameter);
	if (playlist_source != NULL) {
		entries = rb_source_copy (mgr->priv->selected_source);
		rb_source_paste (RB_SOURCE (playlist_source), entries);

		g_list_foreach (entries, (GFunc)rhythmdb_entry_unref, NULL);
		g_list_free (entries);
	}

	g_object_unref (model);
	g_object_unref (playlist_source);
}

static gboolean
list_playlists_cb (GtkTreeModel *model,
		   GtkTreePath  *path,
		   GtkTreeIter  *iter,
		   GList **playlists)
{
	RBDisplayPage *page;
	gboolean  local;

	gtk_tree_model_get (model,
			    iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	if (page != NULL) {
		if (RB_IS_PLAYLIST_SOURCE (page) && !RB_IS_PLAY_QUEUE_SOURCE (page)) {
			g_object_get (page, "is-local", &local, NULL);
			if (local) {
				*playlists = g_list_prepend (*playlists, RB_SOURCE (page));
			}
		}

		g_object_unref (page);
	}

	return FALSE;
}


/**
 * rb_playlist_manager_get_playlists:
 * @mgr: the #RBPlaylistManager
 *
 * Returns a #GList containing all local playlist source objects.
 *
 * Return value: (element-type RB.Source) (transfer container): list of playlists
 **/
GList *
rb_playlist_manager_get_playlists (RBPlaylistManager *mgr)
{
	GList *playlists = NULL;
	RBDisplayPageModel *page_model;

	g_object_get (mgr->priv->shell, "display-page-model", &page_model, NULL);
	gtk_tree_model_foreach (GTK_TREE_MODEL (page_model),
				(GtkTreeModelForeachFunc)list_playlists_cb,
				&playlists);
	g_object_unref (page_model);

	return g_list_reverse (playlists);
}

/**
 * rb_playlist_manager_get_playlist_names:
 * @mgr: the #RBPlaylistManager
 * @playlists: (out callee-allocates) (transfer full): holds the array of playlist names on reutrn
 * @error: holds a #GError on return on failure
 *
 * Allocates and returns an array containing the names of all local
 * playlists.  This is part of the playlist manager dbus interface.
 *
 * Return value: TRUE if successful.
 **/
gboolean
rb_playlist_manager_get_playlist_names (RBPlaylistManager *mgr,
					gchar ***playlists,
					GError **error)
{
	GList *pl;
	GList *t;
	int i;

	pl = rb_playlist_manager_get_playlists (mgr);
	*playlists = g_new0 (char *, g_list_length (pl) + 1);
	if (!*playlists)
		return FALSE;

	i = 0;
	for (t = pl; t != NULL; t = t->next, i++) {
		const char *name;
		RBSource *source = (RBSource *)t->data;

		g_object_get (source, "name", &name, NULL);
		(*playlists)[i] = g_strdup (name);
	}

	return TRUE;
}

typedef struct {
	const char *name;
	RBSource *source;
} FindPlaylistData;

static gboolean
find_playlist_by_name_cb (GtkTreeModel *model,
			  GtkTreePath  *path,
			  GtkTreeIter  *iter,
			  FindPlaylistData *data)
{
	RBDisplayPage *page;

	gtk_tree_model_get (model,
			    iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	if (page != NULL) {
		if (RB_IS_PLAYLIST_SOURCE (page) && !RB_IS_PLAY_QUEUE_SOURCE (page)) {
			char *name;

			g_object_get (page, "name", &name, NULL);
			if (strcmp (name, data->name) == 0) {
				data->source = RB_SOURCE (page);
			}
			g_free (name);
		}

		g_object_unref (page);
	}

	return (data->source != NULL);
}

static RBSource *
_get_playlist_by_name (RBPlaylistManager *mgr,
		       const char *name)
{
	RBDisplayPageModel *page_model;
	FindPlaylistData d;

	d.name = name;
	d.source = NULL;

	g_object_get (mgr->priv->shell, "display-page-model", &page_model, NULL);
	gtk_tree_model_foreach (GTK_TREE_MODEL (page_model),
				(GtkTreeModelForeachFunc)find_playlist_by_name_cb,
				&d);
	g_object_unref (page_model);
	return d.source;
}

/**
 * rb_playlist_manager_create_static_playlist:
 * @mgr: the #RBPlaylistManager
 * @name: name of the new playlist
 * @error: holds a #GError on return on failure
 *
 * Creates a new static playlist source with the given name.
 * Will fail if a playlist with that name already exists.
 * This is part of the playlist manager dbus interface.
 *
 * Return value: TRUE if successful.
 **/
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

/**
 * rb_playlist_manager_delete_playlist:
 * @mgr: the #RBPlaylistManager
 * @name: name of the playlist to delete
 * @error: holds a #GError on return on failure
 *
 * Deletes the specified playlist.  Will fail if no playlist with
 * that name exists. This is part of the playlist manager dbus interface.
 *
 * Return value: TRUE if successful.
 */
gboolean
rb_playlist_manager_delete_playlist (RBPlaylistManager *mgr,
				     const gchar *name,
				     GError **error)
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
	rb_display_page_delete_thyself (RB_DISPLAY_PAGE (playlist));
	rb_playlist_manager_set_dirty (mgr, TRUE);
	return TRUE;
}

/**
 * rb_playlist_manager_add_to_playlist:
 * @mgr: the #RBPlaylistManager
 * @name: name of the playlist to add to
 * @uri: URI of the entry to add to the playlist
 * @error: holds a #GError on return on failure
 *
 * Adds an entry to the specified playlist.
 * Fails if no playlist with that name exists.
 * This is part of the playlist manager dbus interface.
 *
 * Return value: TRUE if successful.
 **/
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

/**
 * rb_playlist_manager_remove_from_playlist:
 * @mgr: the #RBPlaylistManager
 * @name: name of the playlist to remove from
 * @uri: URI of the entry to remove from the playlist
 * @error: holds a #GError on return on failure
 *
 * Removes an entry from the specified playlist.
 * Fails if no playlist with that name exists.
 * This is part of the playlist manager dbus interface.
 *
 * Return value: TRUE if successful.
 **/
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

/**
 * rb_playlist_manager_export_playlist:
 * @mgr: the #RBPlaylistManager
 * @name: name of the playlist to export
 * @uri: playlist save location
 * @m3u_format: if TRUE, save in M3U format, otherwise save in PLS format
 * @error: holds a #GError on return on failure
 *
 * Saves the specified playlist to a file in either M3U or PLS format.
 * This is part of the playlist manager dbus interface.
 *
 * Return value: TRUE if successful.
 **/
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

static void
playlist_manager_method_call (GDBusConnection *connection,
			      const char *sender,
			      const char *object_path,
			      const char *interface_name,
			      const char *method_name,
			      GVariant *parameters,
			      GDBusMethodInvocation *invocation,
			      RBPlaylistManager *mgr)
{
	GError *error = NULL;
	const char *name;
	const char *uri;

	if (g_strcmp0 (interface_name, RB_PLAYLIST_MANAGER_IFACE_NAME) != 0) {
		rb_debug ("method call on unexpected interface %s", interface_name);
		g_dbus_method_invocation_return_error (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_NOT_SUPPORTED,
						       "Method %s.%s not supported",
						       interface_name,
						       method_name);
		return;
	}

	if (g_strcmp0 (method_name, "GetPlaylists") == 0) {
		char **names;

		rb_playlist_manager_get_playlist_names (mgr, &names, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(^as)", names));
		g_strfreev (names);
	} else if (g_strcmp0 (method_name, "CreatePlaylist") == 0) {
		g_variant_get (parameters, "(&s)", &name);
		if (rb_playlist_manager_create_static_playlist (mgr, name, &error)) {
			g_dbus_method_invocation_return_value (invocation, NULL);
		} else {
			g_dbus_method_invocation_return_gerror (invocation, error);
			g_clear_error (&error);
		}
	} else if (g_strcmp0 (method_name, "DeletePlaylist") == 0) {
		g_variant_get (parameters, "(&s)", &name);
		if (rb_playlist_manager_delete_playlist (mgr, name, &error)) {
			g_dbus_method_invocation_return_value (invocation, NULL);
		} else {
			g_dbus_method_invocation_return_gerror (invocation, error);
			g_clear_error (&error);
		}
	} else if (g_strcmp0 (method_name, "AddToPlaylist") == 0) {
		g_variant_get (parameters, "(ss)", &name, &uri);
		if (rb_playlist_manager_add_to_playlist (mgr, name, uri, &error)) {
			g_dbus_method_invocation_return_value (invocation, NULL);
		} else {
			g_dbus_method_invocation_return_gerror (invocation, error);
			g_clear_error (&error);
		}
	} else if (g_strcmp0 (method_name, "RemoveFromPlaylist") == 0) {
		g_variant_get (parameters, "(ss)", &name, &uri);
		if (rb_playlist_manager_remove_from_playlist (mgr, name, uri, &error)) {
			g_dbus_method_invocation_return_value (invocation, NULL);
		} else {
			g_dbus_method_invocation_return_gerror (invocation, error);
			g_clear_error (&error);
		}
	} else if (g_strcmp0 (method_name, "ExportPlaylist") == 0) {
		gboolean m3u_format;
		g_variant_get (parameters, "(ssb)", &name, &uri, &m3u_format);
		if (rb_playlist_manager_export_playlist (mgr, name, uri, m3u_format, &error)) {
			g_dbus_method_invocation_return_value (invocation, NULL);
		} else {
			g_dbus_method_invocation_return_gerror (invocation, error);
			g_clear_error (&error);
		}
	} else if (g_strcmp0 (method_name, "ImportPlaylist") == 0) {
		g_variant_get (parameters, "(s)", &uri);
		if (rb_playlist_manager_parse_file (mgr, uri, &error)) {
			g_dbus_method_invocation_return_value (invocation, NULL);
		} else {
			g_dbus_method_invocation_return_gerror (invocation, error);
			g_clear_error (&error);
		}
	} else {
		g_dbus_method_invocation_return_error (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_NOT_SUPPORTED,
						       "Method %s.%s not supported",
						       interface_name,
						       method_name);
	}
}

static const GDBusInterfaceVTable playlist_manager_vtable = {
	(GDBusInterfaceMethodCallFunc) playlist_manager_method_call,
	NULL,
	NULL
};

static void
rb_playlist_manager_set_source (RBPlaylistManager *mgr,
				RBSource *source)
{
	GApplication *app;
	gboolean playlist_active;
	gboolean playlist_local = FALSE;
	gboolean can_save;
	gboolean can_edit;
	gboolean can_rename;
	gboolean can_shuffle;
	GAction *gaction;

	app = g_application_get_default ();

	if (mgr->priv->selected_source != NULL) {
		g_object_unref (mgr->priv->selected_source);
	}
	mgr->priv->selected_source = g_object_ref (source);

	playlist_active = RB_IS_PLAYLIST_SOURCE (mgr->priv->selected_source);
	if (playlist_active) {
		g_object_get (mgr->priv->selected_source, "is-local", &playlist_local, NULL);
	}

	can_save = playlist_local;
	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "playlist-save");
	g_object_set (gaction, "enabled", can_save, NULL);

	can_edit = (playlist_local && RB_IS_AUTO_PLAYLIST_SOURCE (mgr->priv->selected_source));
	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "playlist-edit");
	g_object_set (gaction, "enabled", can_edit, NULL);

	can_rename = playlist_local && rb_source_can_rename (mgr->priv->selected_source);
	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "playlist-rename");
	g_object_set (gaction, "enabled", can_rename, NULL);

	can_shuffle = RB_IS_STATIC_PLAYLIST_SOURCE (mgr->priv->selected_source);
	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "playlist-shuffle");
	g_object_set (gaction, "enabled", can_shuffle, NULL);
}

static void
rb_playlist_manager_set_shell_internal (RBPlaylistManager *mgr,
					RBShell           *shell)
{
	RhythmDB     *db = NULL;

	if (mgr->priv->db != NULL) {
		g_object_unref (mgr->priv->db);
	}

	mgr->priv->shell = shell;
	if (mgr->priv->shell != NULL) {
		g_object_get (mgr->priv->shell, "db", &db, NULL);
	}

	mgr->priv->db = db;
}

static void
rb_playlist_manager_set_property (GObject *object,
				  guint prop_id,
				  const GValue *value,
				  GParamSpec *pspec)
{
	RBPlaylistManager *mgr = RB_PLAYLIST_MANAGER (object);

	switch (prop_id) {
	case PROP_PLAYLIST_NAME:
		g_free (mgr->priv->playlists_file);
		mgr->priv->playlists_file = g_strdup (g_value_get_string (value));
                break;
	case PROP_SOURCE:
		rb_playlist_manager_set_source (mgr, g_value_get_object (value));
		break;
	case PROP_SHELL:
		rb_playlist_manager_set_shell_internal (mgr, g_value_get_object (value));
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
        case PROP_PLAYLIST_NAME:
                g_value_set_string (value, mgr->priv->playlists_file);
                break;
	case PROP_SOURCE:
		g_value_set_object (value, mgr->priv->selected_source);
		break;
	case PROP_SHELL:
		g_value_set_object (value, mgr->priv->shell);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_playlist_manager_constructed (GObject *object)
{
	GDBusConnection *bus;
	GApplication *app;
	RBPlaylistManager *mgr = RB_PLAYLIST_MANAGER (object);
	GtkBuilder *builder;
	GMenuModel *menu;

	GActionEntry actions[] = {
		{ "playlist-new", new_playlist_action_cb },
		{ "playlist-new-auto", new_auto_playlist_action_cb },
		{ "playlist-load", load_playlist_action_cb },
		{ "playlist-edit", edit_auto_playlist_action_cb },
		{ "playlist-rename", rename_playlist_action_cb },
		{ "playlist-queue", queue_playlist_action_cb },
		{ "playlist-shuffle", shuffle_playlist_action_cb },
		{ "playlist-save", save_playlist_action_cb },
		{ "playlist-add-to-new", add_to_new_playlist_action_cb },
		{ "playlist-add-to", add_to_playlist_action_cb, "s" }
	};

	RB_CHAIN_GOBJECT_METHOD(rb_playlist_manager_parent_class, constructed, G_OBJECT (mgr));

	app = g_application_get_default ();
	g_action_map_add_action_entries (G_ACTION_MAP (app), actions, G_N_ELEMENTS (actions), mgr);

	builder = rb_builder_load ("playlist-menu.ui", NULL);
	menu = G_MENU_MODEL (gtk_builder_get_object (builder, "playlist-menu"));
	rb_application_link_shared_menus (RB_APPLICATION (app), G_MENU (menu));
	rb_application_add_shared_menu (RB_APPLICATION (app), "playlist-menu", menu);
	g_object_unref (builder);

	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	if (bus) {
		GDBusNodeInfo *node_info;
		GError *error = NULL;

		node_info = g_dbus_node_info_new_for_xml (rb_playlist_manager_dbus_spec, &error);
		if (error != NULL) {
			g_warning ("Unable to parse playlist manager dbus spec: %s", error->message);
			g_clear_error (&error);
			return;
		}

		g_dbus_connection_register_object (bus,
						   RB_PLAYLIST_MANAGER_DBUS_PATH,
						   g_dbus_node_info_lookup_interface (node_info, RB_PLAYLIST_MANAGER_IFACE_NAME),
						   &playlist_manager_vtable,
						   g_object_ref (mgr),
						   g_object_unref,
						   &error);
		if (error != NULL) {
			g_warning ("Unable to register playlist manager dbus object: %s", error->message);
			g_clear_error (&error);
		}
	}
}

static void
rb_playlist_manager_init (RBPlaylistManager *mgr)
{
	mgr->priv = G_TYPE_INSTANCE_GET_PRIVATE (mgr,
						 RB_TYPE_PLAYLIST_MANAGER,
						 RBPlaylistManagerPrivate);

	mgr->priv->dirty = 0;
	mgr->priv->saving = 0;
}

static void
rb_playlist_manager_dispose (GObject *object)
{
	RBPlaylistManager *mgr;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PLAYLIST_MANAGER (object));

	rb_debug ("Disposing playlist manager");

	mgr = RB_PLAYLIST_MANAGER (object);

	g_return_if_fail (mgr->priv != NULL);

	g_clear_object (&mgr->priv->db);
	g_clear_object (&mgr->priv->selected_source);

	G_OBJECT_CLASS (rb_playlist_manager_parent_class)->dispose (object);
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

	g_free (mgr->priv->playlists_file);

	G_OBJECT_CLASS (rb_playlist_manager_parent_class)->finalize (object);
}


static void
rb_playlist_manager_class_init (RBPlaylistManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = rb_playlist_manager_constructed;
	object_class->dispose = rb_playlist_manager_dispose;
	object_class->finalize = rb_playlist_manager_finalize;

	object_class->set_property = rb_playlist_manager_set_property;
	object_class->get_property = rb_playlist_manager_get_property;

	g_object_class_install_property (object_class,
					 PROP_PLAYLIST_NAME,
                                         g_param_spec_string ("playlists_file",
                                                              "name",
                                                              "playlists file",
                                                              NULL,
                                                              G_PARAM_READWRITE));

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

	/**
	 * RBPlaylistManager::playlist-added:
	 * @manager: the #RBPlaylistManager
	 * @source: the new #RBSource
	 *
	 * Emitted when a playlist is added, including when being loaded
	 * from the user's playlist file.
	 */
	rb_playlist_manager_signals[PLAYLIST_ADDED] =
		g_signal_new ("playlist_added",
			      RB_TYPE_PLAYLIST_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlaylistManagerClass, playlist_added),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1, G_TYPE_OBJECT);

	/**
	 * RBPlaylistManager::playlist-created:
	 * @manager: the #RBPlaylistManager
	 * @source: the newly created playlist #RBSource
	 *
	 * Emitted when a new playlist is created.
	 */
	rb_playlist_manager_signals[PLAYLIST_CREATED] =
		g_signal_new ("playlist_created",
			      RB_TYPE_PLAYLIST_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlaylistManagerClass, playlist_created),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1, G_TYPE_OBJECT);

	/**
	 * RBPlaylistManager::load-start:
	 * @manager: the #RBPlaylistManager
	 *
	 * Emitted when the playlist manager starts loading the user's
	 * playlist file.
	 */
	rb_playlist_manager_signals[PLAYLIST_LOAD_START] =
		g_signal_new ("load_start",
			      RB_TYPE_PLAYLIST_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlaylistManagerClass, load_start),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0, G_TYPE_NONE);
	/**
	 * RBPlaylistManager::load-finish:
	 * @manager: the #RBPlaylistManager
	 *
	 * Emitted when the playlist manager finishes loading the user's
	 * playlist file.
	 */
	rb_playlist_manager_signals[PLAYLIST_LOAD_FINISH] =
		g_signal_new ("load_finish",
			      RB_TYPE_PLAYLIST_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlaylistManagerClass, load_finish),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0, G_TYPE_NONE);

	g_type_class_add_private (klass, sizeof (RBPlaylistManagerPrivate));
}
