/*
 *  Implmentation of DAAP (iTunes Music Sharing) sharing
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

#include "rb-daap-sharing.h"
#include "rb-daap-share.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-playlist-manager.h"
#include "eel-gconf-extensions.h"
#include <libgnome/gnome-i18n.h>
#include <string.h>

#define CONF_ENABLE_SHARING "/apps/rhythmbox/sharing/enable_sharing"
#define CONF_SHARE_NAME "/apps/rhythmbox/sharing/share_name"

static RBDAAPShare *share = NULL;

static void 
create_share (RBShell *shell)
{
	RhythmDB *db;
	RBPlaylistManager *playlist_manager;
	
	gchar *name;
	
	rb_debug ("initialize daap sharing\n");
		
	name = eel_gconf_get_string (CONF_SHARE_NAME);

	if (name == NULL || *name == '\0') {
		const gchar *real_name;

		g_free (name);

		real_name = g_get_real_name ();
		if (strcmp (real_name, "Unknown") == 0) {
			real_name = g_get_user_name ();
		}
		
		name = g_strconcat (real_name, "'s Music", NULL);
		eel_gconf_set_string (CONF_SHARE_NAME, name);
	}
	
	g_object_get (G_OBJECT (shell), "db", &db, "playlist-manager", &playlist_manager, NULL);
	
	share = rb_daap_share_new (name, db, playlist_manager);

	g_object_unref (db);
	g_object_unref (playlist_manager);
	g_free (name);

	return;
}

static void 
enable_sharing_changed_cb (GConfClient *client,
			   guint cnxn_id,
		     	   GConfEntry *entry,
		  	   RBShell *shell)
{
	gboolean enabled;

	enabled = eel_gconf_get_boolean (CONF_ENABLE_SHARING);

	if (enabled) {
		create_share (shell);
	} else {
		rb_debug ("shutdown daap sharing\n");

		g_object_unref (share);
		share = NULL;
	}

	return;
}

static void 
share_name_changed_cb (GConfClient *client, 
		       guint cnxn_id, 
		       GConfEntry *entry, 
		       RBShell *shell)
{
	gchar *name;

	name = eel_gconf_get_string (CONF_SHARE_NAME);

	if (share) {
		g_object_set (G_OBJECT (share), "name", name, NULL);
	}

	g_free (name);
	
	return;
}


void 
rb_daap_sharing_init (RBShell *shell)
{
	gboolean enabled;

	g_object_ref (shell);
	
	enabled = eel_gconf_get_boolean (CONF_ENABLE_SHARING);
	
	if (enabled) {
		create_share (shell);
	}
	
	eel_gconf_notification_add (CONF_ENABLE_SHARING,
				    (GConfClientNotifyFunc) enable_sharing_changed_cb,
				    shell);
	eel_gconf_notification_add (CONF_SHARE_NAME,
				    (GConfClientNotifyFunc) share_name_changed_cb,
				    shell);

	return;
}

void 
rb_daap_sharing_shutdown (RBShell *shell)
{
	g_object_unref (shell);
	
	if (share) {
		rb_debug ("shutdown daap sharing\n");

		g_object_unref (share);
		share = NULL;
	}
	
	return;
}

