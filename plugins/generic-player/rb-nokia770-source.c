/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006 James Livingston  <doclivingston@gmail.com>
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

#include "config.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "mediaplayerid.h"

#include "rb-nokia770-source.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rhythmdb.h"


static char * impl_uri_from_playlist_uri (RBGenericPlayerSource *source, const char *uri);

G_DEFINE_DYNAMIC_TYPE (RBNokia770Source, rb_nokia770_source, RB_TYPE_GENERIC_PLAYER_SOURCE)

#define NOKIA_INTERNAL_MOUNTPOINT "file:///media/mmc1/"

static void
rb_nokia770_source_class_init (RBNokia770SourceClass *klass)
{
	RBGenericPlayerSourceClass *generic_class = RB_GENERIC_PLAYER_SOURCE_CLASS (klass);

	generic_class->uri_from_playlist_uri = impl_uri_from_playlist_uri;
}

static void
rb_nokia770_source_class_finalize (RBNokia770SourceClass *klass)
{
}

static void
rb_nokia770_source_init (RBNokia770Source *source)
{

}

static char *
impl_uri_from_playlist_uri (RBGenericPlayerSource *source, const char *uri)
{
	const char *path;
	char *local_uri;
	char *mount_uri;

	if (!g_str_has_prefix (uri, NOKIA_INTERNAL_MOUNTPOINT)) {
		rb_debug ("found playlist uri with unexpected mountpoint");
		return NULL;
	}

	path = uri + strlen (NOKIA_INTERNAL_MOUNTPOINT);
	mount_uri = rb_generic_player_source_get_mount_path (source);
	local_uri = rb_uri_append_uri (mount_uri, path);
	g_free (mount_uri);
	return local_uri;
}

gboolean
rb_nokia770_is_mount_player (GMount *mount, MPIDDevice *device_info)
{
	gboolean result;
	char *vendor;
	char *model;

	g_object_get (device_info, "vendor", &vendor, "model", &model, NULL);
	result = FALSE;
	if (vendor != NULL && g_str_equal (vendor, "Nokia")) {
		if (model != NULL && (g_str_equal (model, "770") || g_str_equal (model, "N800") || g_str_equal (model, "N810"))) {
			result = TRUE;
		}
	}

	g_free (vendor);
	g_free (model);
	return result;
}

void
_rb_nokia770_source_register_type (GTypeModule *module)
{
	rb_nokia770_source_register_type (module);
}
