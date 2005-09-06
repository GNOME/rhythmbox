/*
 *  Implementation of DAAP (iTunes Music Sharing) playlist source object
 *
 *  Copyright (C) 2005 Charles Schmidt <cschmidt2@emich.edu>
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

#include <gtk/gtktreeview.h>
#include <gtk/gtkicontheme.h>
#include <string.h>
#include "rhythmdb.h"
#include "rb-shell.h"
#include <libgnome/gnome-i18n.h>
#include "eel-gconf-extensions.h"
#include "rb-daap-playlist-source.h"
#include "rb-stock-icons.h"
#include "rb-debug.h"
#include "rb-util.h"

#include "rb-daap-connection.h"

enum {
	PROP_0,
	PROP_PLAYLIST
};

static void 
rb_daap_playlist_source_init (RBDAAPPlaylistSource *source);
static void 
rb_daap_playlist_source_finalize (GObject *object);
static void 
rb_daap_playlist_source_set_property (GObject *object, 
				      guint prop_id, 
				      const GValue *value, 
				      GParamSpec *pspec); 
static void 
rb_daap_playlist_source_get_property (GObject *object, 
				      guint prop_id, 
				      GValue *value, 
				      GParamSpec *pspec);
static void 
rb_daap_playlist_source_class_init (RBDAAPPlaylistSourceClass *klass);

struct RBDAAPPlaylistSourcePrivate
{
	RBDAAPPlaylist *playlist;
};

GType 
rb_daap_playlist_source_get_type (void)
{
	static GType rb_daap_playlist_source_type = 0;

	if (rb_daap_playlist_source_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBDAAPPlaylistSourceClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_daap_playlist_source_class_init,
			NULL,
			NULL,
			sizeof (RBDAAPPlaylistSource),
			0,
			(GInstanceInitFunc) rb_daap_playlist_source_init
		};

		rb_daap_playlist_source_type = g_type_register_static (RB_TYPE_PLAYLIST_SOURCE,
							      "RBDAAPPlaylistSource",
							      &our_info, 0);

	}

	return rb_daap_playlist_source_type;
}

static void 
rb_daap_playlist_source_class_init (RBDAAPPlaylistSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->set_property = rb_daap_playlist_source_set_property;
	object_class->get_property = rb_daap_playlist_source_get_property;	
	object_class->finalize = rb_daap_playlist_source_finalize;

	source_class->impl_can_rename = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;

	g_object_class_install_property (object_class,
					 PROP_PLAYLIST,
					 g_param_spec_pointer ("playlist",
						 	       "RBDAAPPlaylist",
							       "RBDAAPPlaylist structure",
							       G_PARAM_READWRITE));

	return;
}

static void 
rb_daap_playlist_source_init (RBDAAPPlaylistSource *source)
{
	source->priv = g_new0 (RBDAAPPlaylistSourcePrivate, 1);

	return;
}

static void 
rb_daap_playlist_source_set_property (GObject *object, 
				      guint prop_id, 
				      const GValue *value, 
				      GParamSpec *pspec)
{
	RBDAAPPlaylistSource *source = RB_DAAP_PLAYLIST_SOURCE (object);

	switch (prop_id) {
		case PROP_PLAYLIST:
		{
			GList *l;
			
			source->priv->playlist = g_value_get_pointer (value);
			
			for (l = source->priv->playlist->uris; l; l = l->next) {
				const gchar *uri = (const gchar *)l->data;

				rb_playlist_source_add_location (RB_PLAYLIST_SOURCE (source), uri);
			}
			
			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}

	return;
}
			
static void 
rb_daap_playlist_source_get_property (GObject *object, 
				      guint prop_id, 
				      GValue *value, 
				      GParamSpec *pspec)
{
	RBDAAPPlaylistSource *source = RB_DAAP_PLAYLIST_SOURCE (object);

	switch (prop_id) {
		case PROP_PLAYLIST:
			g_value_set_pointer (value, source->priv->playlist);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}

	return;
}
	
static void 
rb_daap_playlist_source_finalize (GObject *object)
{
	RBDAAPPlaylistSource *source = RB_DAAP_PLAYLIST_SOURCE (object);

	if (source->priv) {
	
		g_free (source->priv);
		source->priv = NULL;
	}

	return;
}
