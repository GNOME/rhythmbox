/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Implmentation of DAAP (iTunes Music Sharing) sharing
 *
 *  Copyright (C) 2005 Charles Schmidt <cschmidt2@emich.edu>
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

#include <gio/gio.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>

#include "rb-daap-sharing.h"
#include "rb-rhythmdb-dmap-db-adapter.h"
#include "rb-dmap-container-db-adapter.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-playlist-manager.h"

#include <libdmapsharing/dmap.h>

static DmapAvShare *share = NULL;
static GSettings *settings = NULL;

char *
rb_daap_sharing_default_share_name (void)
{
	const gchar *real_name;

	real_name = g_get_real_name ();
	if (strcmp (real_name, "Unknown") == 0) {
		real_name = g_get_user_name ();
	}

	return g_strdup_printf (_("%s's Music"), real_name);
}

static gboolean
share_name_get_mapping (GValue *value, GVariant *variant, gpointer data)
{
	const char *name;
	name = g_variant_get_string (variant, NULL);

	if (name != NULL || name[0] != '\0') {
		g_value_set_string (value, name);
	} else {
		g_value_take_string (value, rb_daap_sharing_default_share_name ());
	}
	return TRUE;
}

static void
create_share (RBShell *shell)
{
	RhythmDB *rdb;
	DmapDb *db;
	DmapContainerDb *container_db;
	RBPlaylistManager *playlist_manager;
	char *name;
	char *password;
	gboolean require_password;
	GError *error = NULL;

	g_assert (share == NULL);
	rb_debug ("initialize daap sharing");

	name = g_settings_get_string (settings, "share-name");
	if (name == NULL || *name == '\0') {
		g_free (name);
		name = rb_daap_sharing_default_share_name ();
	}

	g_object_get (shell,
		      "db", &rdb,
		      "playlist-manager", &playlist_manager, NULL);
	db = DMAP_DB (rb_rhythmdb_dmap_db_adapter_new (rdb, RHYTHMDB_ENTRY_TYPE_SONG));
	container_db = DMAP_CONTAINER_DB (rb_dmap_container_db_adapter_new (playlist_manager));

	require_password = g_settings_get_boolean (settings, "require-password");
	if (require_password) {
		password = g_settings_get_string (settings, "share-password");
	} else {
		password = NULL;
	}

	share = dmap_av_share_new (name, password, db, container_db, NULL);

	g_settings_bind_with_mapping (settings, "share-name",
				      share, "name",
				      G_SETTINGS_BIND_GET,
				      share_name_get_mapping, NULL,
				      NULL, NULL);
	if (g_settings_get_boolean (settings, "require-password")) {
		g_settings_bind (settings, "share-password", share, "password", G_SETTINGS_BIND_DEFAULT);
	}

	dmap_share_serve (DMAP_SHARE (share), &error);
	if (error == NULL)
		dmap_share_publish (DMAP_SHARE (share), &error);

	if (error != NULL)
		g_warning ("Unable to start DAAP sharing: %s", error->message);

	g_clear_error (&error);

	g_object_unref (db);
	g_object_unref (container_db);
	g_object_unref (rdb);
	g_object_unref (playlist_manager);

	g_free (name);
	g_free (password);
}

static void
sharing_settings_changed_cb (GSettings *settings, const char *key, RBShell *shell)
{
	if (g_strcmp0 (key, "enable-sharing") == 0) {
		gboolean enabled;

		enabled = g_settings_get_boolean (settings, key);
		if (enabled) {
			if (share == NULL) {
				create_share (shell);
			}
		} else {
			if (share != NULL) {
				rb_debug ("shutting down daap share");
				g_object_unref (share);
				share = NULL;
			}
		}
	} else if (g_strcmp0 (key, "require-password") == 0) {

		if (share != NULL) {
			if (g_settings_get_boolean (settings, key)) {
				g_settings_bind (settings, "share-password", share, "password", G_SETTINGS_BIND_DEFAULT);
			} else {
				g_settings_unbind (share, "password");
				g_object_set (share, "password", NULL, NULL);
			}
		}
	}
}

void
rb_daap_sharing_init (RBShell *shell)
{
	g_object_ref (shell);

	settings = g_settings_new ("org.gnome.rhythmbox.sharing");

	if (g_settings_get_boolean (settings, "enable-sharing")) {
		create_share (shell);
	}

	g_signal_connect_object (settings, "changed", G_CALLBACK (sharing_settings_changed_cb), shell, 0);
}

void
rb_daap_sharing_shutdown (RBShell *shell)
{
	if (share) {
		rb_debug ("shutdown daap sharing");

		g_object_unref (share);
		share = NULL;
	}

	if (settings) {
		g_object_unref (settings);
		settings = NULL;
	}

	g_object_unref (shell);
}
