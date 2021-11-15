/*
 *  Copyright (C) 2007 James Livingston  <doclivingston@gmail.com>
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

#include "config.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gpod/itdb.h>

#include "rb-debug.h"
#include "rb-util.h"
#include "rhythmdb.h"

#include "rb-ipod-static-playlist-source.h"
#include "rb-media-player-source.h"
#include "rb-ipod-source.h"

static void rb_ipod_static_playlist_source_constructed (GObject *object);
static void rb_ipod_static_playlist_source_dispose (GObject *object);
static void rb_ipod_static_playlist_source_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_ipod_static_playlist_source_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);


typedef struct
{
	RbIpodDb	*ipod_db;
	Itdb_Playlist	*itdb_playlist;
	RBiPodSource	*ipod_source;
	gboolean	was_reordered;
} RBIpodStaticPlaylistSourcePrivate;

G_DEFINE_DYNAMIC_TYPE(RBIpodStaticPlaylistSource, rb_ipod_static_playlist_source, RB_TYPE_STATIC_PLAYLIST_SOURCE)

#define IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_IPOD_STATIC_PLAYLIST_SOURCE, RBIpodStaticPlaylistSourcePrivate))

enum {
	PROP_0,
	PROP_IPOD_SOURCE,
	PROP_IPOD_DB,
	PROP_ITDB_PLAYLIST
};

static void
playlist_track_removed (RhythmDBQueryModel *m,
			RhythmDBEntry *entry,
			gpointer data)
{
	RBIpodStaticPlaylistSourcePrivate *priv = IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (data);
	Itdb_Track *track;

	track = rb_ipod_source_lookup_track (priv->ipod_source, entry);
	g_return_if_fail (track != NULL);
	rb_ipod_db_remove_from_playlist (priv->ipod_db, priv->itdb_playlist, track);
}

static void
playlist_track_added (GtkTreeModel *model,
		      GtkTreePath *path,
		      GtkTreeIter *iter,
		      gpointer data)
{
	RBIpodStaticPlaylistSourcePrivate *priv = IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (data);
	Itdb_Track *track;
	RhythmDBEntry *entry;

	gtk_tree_model_get (model, iter, 0, &entry, -1);
	track = rb_ipod_source_lookup_track (priv->ipod_source, entry);
	g_return_if_fail (track != NULL);

	rb_ipod_db_add_to_playlist (priv->ipod_db, priv->itdb_playlist, track);
}

static void
playlist_before_save (RbIpodDb *ipod_db, gpointer data)
{
	RBIpodStaticPlaylistSourcePrivate *priv = IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (data);
	RhythmDBQueryModel *model;
	GtkTreeIter iter;

	if (priv->was_reordered == FALSE)
		return;
	priv->was_reordered = FALSE;

	/* Coherence check that all tracks are in entry_map */

	g_object_get (G_OBJECT (data), "base-query-model", &model, NULL);
	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter)) {
		do {
			RhythmDBEntry *entry;
			Itdb_Track *track;

			gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, 0, &entry, -1);
			track = rb_ipod_source_lookup_track (priv->ipod_source, entry);

			g_return_if_fail (track != NULL);
		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (model), &iter));
	}

	/* Remove all tracks then re-add in correct order */

	while (priv->itdb_playlist->members != NULL) {
		Itdb_Track *track;

		track = (Itdb_Track *)priv->itdb_playlist->members->data;

		rb_debug ("removing \"%s\" from \"%s\"", track->title, priv->itdb_playlist->name);

		/* Call directly to itdb to avoid scheduling another save */
		itdb_playlist_remove_track (priv->itdb_playlist, track);
	}

	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter)) {
		do {
			RhythmDBEntry *entry;
			Itdb_Track *track;

			gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, 0, &entry, -1);
			track = rb_ipod_source_lookup_track (priv->ipod_source, entry);

			rb_debug ("adding \"%s\" to \"%s\"", track->title, priv->itdb_playlist->name);

			/* Call directly to itdb to avoid scheduling another save */
			itdb_playlist_add_track (priv->itdb_playlist, track, -1);
		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (model), &iter));
	}

	g_object_unref (model);
}

static void
playlist_rows_reordered (GtkTreeModel *model,
			 GtkTreePath *path,
			 GtkTreeIter *iter,
			 gint *order,
			 gpointer data)
{
	RBIpodStaticPlaylistSourcePrivate *priv = IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (data);
	priv->was_reordered = TRUE;

	rb_ipod_db_save_async (priv->ipod_db);
}

static void
playlist_source_model_disconnect_signals (RBIpodStaticPlaylistSource *source)
{
	RhythmDBQueryModel *model;

	g_return_if_fail (RB_IS_IPOD_STATIC_PLAYLIST_SOURCE (source));

	g_object_get (source, "base-query-model", &model, NULL);

	g_signal_handlers_disconnect_by_func (model,
					      G_CALLBACK (playlist_track_added),
					      source);
	g_signal_handlers_disconnect_by_func (model,
					      G_CALLBACK (playlist_track_removed),
					      source);
	g_signal_handlers_disconnect_by_func (model,
					      G_CALLBACK (playlist_rows_reordered),
					      source);

	g_object_unref (model);
}

static void
playlist_source_model_connect_signals (RBIpodStaticPlaylistSource *playlist_source)
{
	RhythmDBQueryModel *model;

	g_return_if_fail (RB_IS_IPOD_STATIC_PLAYLIST_SOURCE (playlist_source));

	g_object_get (playlist_source, "base-query-model", &model, NULL);
	g_signal_connect (model, "row-inserted",
			  G_CALLBACK (playlist_track_added),
			  playlist_source);
	g_signal_connect (model, "entry-removed",
			  G_CALLBACK (playlist_track_removed),
			  playlist_source);
	g_signal_connect (model, "rows-reordered",
			  G_CALLBACK (playlist_rows_reordered),
			  playlist_source);
	g_object_unref (model);
}

static void
source_name_changed_cb (RBIpodStaticPlaylistSource *source,
			GParamSpec *spec,
			gpointer data)
{
	RBIpodStaticPlaylistSourcePrivate *priv = IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);
	char *name;

	g_object_get (source, "name", &name, NULL);

	if ((priv->itdb_playlist != NULL) && (strcmp (name, priv->itdb_playlist->name) != 0)) {
		rb_debug ("changing playlist name to %s", name);
		rb_ipod_db_rename_playlist (priv->ipod_db, priv->itdb_playlist, name);
	}
	g_free (name);
}

static gboolean
impl_can_remove (RBDisplayPage *page)
{
	return TRUE;
}

static void
impl_remove (RBDisplayPage *page)
{
	RBIpodStaticPlaylistSourcePrivate *priv = IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (page);
	rb_ipod_source_remove_playlist (priv->ipod_source, RB_SOURCE (page));
}

static void
rb_ipod_static_playlist_source_class_init (RBIpodStaticPlaylistSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);

	object_class->constructed = rb_ipod_static_playlist_source_constructed;
	object_class->dispose = rb_ipod_static_playlist_source_dispose;
	object_class->get_property = rb_ipod_static_playlist_source_get_property;
	object_class->set_property = rb_ipod_static_playlist_source_set_property;

	page_class->can_remove = impl_can_remove;
	page_class->remove = impl_remove;

	source_class->can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_delete = (RBSourceFeatureFunc) rb_true_function;

	/* Don't override impl_delete here -- it's provided by RBStaticPlaylistSource */

	g_object_class_install_property (object_class,
					 PROP_IPOD_SOURCE,
					 g_param_spec_object ("ipod-source",
							      "ipod-source",
							      "ipod-source",
							      RB_TYPE_IPOD_SOURCE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_IPOD_DB,
					 g_param_spec_object ("ipod-db",
							      "ipod-db",
							      "ipod-db",
							      RB_TYPE_IPOD_DB,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_ITDB_PLAYLIST,
					 g_param_spec_pointer ("itdb-playlist",
							      "itdb-playlist",
							      "itdb-playlist",
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBIpodStaticPlaylistSourcePrivate));
}

static void
rb_ipod_static_playlist_source_class_finalize (RBIpodStaticPlaylistSourceClass *klass)
{
}

static void
rb_ipod_static_playlist_source_init (RBIpodStaticPlaylistSource *source)
{

}

static void
rb_ipod_static_playlist_source_constructed (GObject *object)
{
	RBIpodStaticPlaylistSourcePrivate *priv = IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (object);
	RhythmDBQueryModel *model;

	RB_CHAIN_GOBJECT_METHOD (rb_ipod_static_playlist_source_parent_class, constructed, object);

	g_signal_connect (object, "notify::name", (GCallback)source_name_changed_cb, NULL);

	g_object_get (object, "base-query-model", &model, NULL);
	g_signal_connect (priv->ipod_db, "before-save",
			  G_CALLBACK (playlist_before_save),
			  object);
	g_object_unref (model);
	playlist_source_model_connect_signals (RB_IPOD_STATIC_PLAYLIST_SOURCE (object));

}

static void
rb_ipod_static_playlist_source_dispose (GObject *object)
{
	RBIpodStaticPlaylistSource *source = RB_IPOD_STATIC_PLAYLIST_SOURCE (object);
	RBIpodStaticPlaylistSourcePrivate *priv = IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (object);

	if (priv->ipod_source) {
		g_object_unref (priv->ipod_source);
		priv->ipod_source = NULL;
	}
	if (priv->ipod_db) {
		g_signal_handlers_disconnect_by_func (priv->ipod_db,
						      G_CALLBACK (playlist_before_save),
						      source);

		g_object_unref (priv->ipod_db);
		priv->ipod_db = NULL;
	}

	playlist_source_model_disconnect_signals (source);

	G_OBJECT_CLASS (rb_ipod_static_playlist_source_parent_class)->dispose (object);
}

RBIpodStaticPlaylistSource *
rb_ipod_static_playlist_source_new (RBShell *shell,
				    RBiPodSource *ipod_source,
				    RbIpodDb *ipod_db,
				    Itdb_Playlist *playlist,
				    RhythmDBEntryType *entry_type,
				    GMenuModel *playlist_menu)
{
	RBIpodStaticPlaylistSource *source;

	g_assert (RB_IS_IPOD_SOURCE (ipod_source));

	source = RB_IPOD_STATIC_PLAYLIST_SOURCE (g_object_new (RB_TYPE_IPOD_STATIC_PLAYLIST_SOURCE,
							       "entry-type", entry_type,
							       "shell", shell,
							       "is-local", FALSE,
							       "name", playlist->name,
							       "ipod-source", ipod_source,
							       "ipod-db", ipod_db,
							       "itdb-playlist", playlist,
							       "playlist-menu", playlist_menu,
							       NULL));

	return source;
}


static void
rb_ipod_static_playlist_source_set_property (GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	RBIpodStaticPlaylistSourcePrivate *priv = IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_IPOD_SOURCE:
		priv->ipod_source = g_value_dup_object (value);
		break;
	case PROP_IPOD_DB:
		priv->ipod_db = g_value_dup_object (value);
		break;
	case PROP_ITDB_PLAYLIST:
		priv->itdb_playlist = g_value_get_pointer (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_ipod_static_playlist_source_get_property (GObject *object,
				guint prop_id,
				GValue *value,
				GParamSpec *pspec)
{
	RBIpodStaticPlaylistSourcePrivate *priv = IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_IPOD_SOURCE:
		g_value_set_object (value, priv->ipod_source);
		break;
	case PROP_IPOD_DB:
		g_value_set_object (value, priv->ipod_db);
		break;
	case PROP_ITDB_PLAYLIST:
		g_value_set_pointer (value, priv->itdb_playlist);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
_rb_ipod_static_playlist_source_register_type (GTypeModule *module)
{
	rb_ipod_static_playlist_source_register_type (module);
}
