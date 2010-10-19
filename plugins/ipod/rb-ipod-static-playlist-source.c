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

#include "rb-plugin.h"
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

static gboolean impl_show_popup (RBSource *source);
static void impl_delete_thyself (RBSource *source);

static void source_name_changed_cb (RBIpodStaticPlaylistSource *source,
				    GParamSpec *spec,
				    gpointer data);

typedef struct
{
	RbIpodDb	*ipod_db;
	Itdb_Playlist	*itdb_playlist;
	RBiPodSource	*ipod_source;
} RBIpodStaticPlaylistSourcePrivate;

RB_PLUGIN_DEFINE_TYPE(RBIpodStaticPlaylistSource,
		      rb_ipod_static_playlist_source,
		      RB_TYPE_STATIC_PLAYLIST_SOURCE)

#define IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_IPOD_STATIC_PLAYLIST_SOURCE, RBIpodStaticPlaylistSourcePrivate))

enum {
	PROP_0,
	PROP_IPOD_SOURCE,
	PROP_IPOD_DB,
	PROP_ITDB_PLAYLIST
};


static void
rb_ipod_static_playlist_source_class_init (RBIpodStaticPlaylistSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->constructed = rb_ipod_static_playlist_source_constructed;
	object_class->dispose = rb_ipod_static_playlist_source_dispose;
	object_class->get_property = rb_ipod_static_playlist_source_get_property;
	object_class->set_property = rb_ipod_static_playlist_source_set_property;

	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_delete_thyself = impl_delete_thyself;
	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;

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
rb_ipod_static_playlist_source_init (RBIpodStaticPlaylistSource *source)
{

}

static void
rb_ipod_static_playlist_source_constructed (GObject *object)
{
	RB_CHAIN_GOBJECT_METHOD (rb_ipod_static_playlist_source_parent_class, constructed, object);
	g_signal_connect (object, "notify::name", (GCallback)source_name_changed_cb, NULL);
}

static void
rb_ipod_static_playlist_source_dispose (GObject *object)
{
	G_OBJECT_CLASS (rb_ipod_static_playlist_source_parent_class)->dispose (object);
}

static void
impl_delete_thyself (RBSource *source)
{
	RBIpodStaticPlaylistSourcePrivate *priv = IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);

	if (priv->ipod_source) {
		g_object_unref (priv->ipod_source);
		priv->ipod_source = NULL;
	}
	if (priv->ipod_db) {
		g_object_unref (priv->ipod_db);
		priv->ipod_db = NULL;
	}
	
	RB_SOURCE_CLASS (rb_ipod_static_playlist_source_parent_class)->impl_delete_thyself (source);
}

RBIpodStaticPlaylistSource *
rb_ipod_static_playlist_source_new (RBShell *shell,
				    RBiPodSource *ipod_source,
				    RbIpodDb *ipod_db,
				    Itdb_Playlist *playlist,
				    RhythmDBEntryType *entry_type)
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


Itdb_Playlist*
rb_ipod_static_playlist_source_get_itdb_playlist (RBIpodStaticPlaylistSource *playlist)
{
	RBIpodStaticPlaylistSourcePrivate *priv = IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (playlist);

	return priv->itdb_playlist;
}

RBiPodSource*
rb_ipod_static_playlist_source_get_ipod_source (RBIpodStaticPlaylistSource *playlist)
{
	RBIpodStaticPlaylistSourcePrivate *priv = IPOD_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (playlist);

	return priv->ipod_source;
}

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/iPodPlaylistSourcePopup");
	return TRUE;
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
