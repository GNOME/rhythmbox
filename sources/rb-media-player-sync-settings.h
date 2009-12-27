/*
 *  Copyright (C) 2009 Paul Bellamy <paul.a.bellamy@gmail.com>
 *  Copyright (C) 2009 Jonathan Matthew <jonathan@d14n.org>
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

#ifndef RB_MEDIA_PLAYER_SYNC_SETTINGS__H
#define RB_MEDIA_PLAYER_SYNC_SETTINGS__H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_MEDIA_PLAYER_SYNC_SETTINGS         (rb_media_player_sync_settings_get_type ())
#define RB_MEDIA_PLAYER_SYNC_SETTINGS(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_MEDIA_PLAYER_SYNC_SETTINGS, RBMediaPlayerSyncSettings))
#define RB_MEDIA_PLAYER_SYNC_SETTINGS_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_MEDIA_PLAYER_SYNC_SETTINGS, RBMediaPlayerSyncSettingsClass))
#define RB_IS_MEDIA_PLAYER_SYNC_SETTINGS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_MEDIA_PLAYER_SYNC_SETTINGS))
#define RB_IS_MEDIA_PLAYER_SYNC_SETTINGS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_MEDIA_PLAYER_SYNC_SETTINGS))
#define RB_MEDIA_PLAYER_SYNC_SETTINGS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_MEDIA_PLAYER_SYNC_SETTINGS, RBMediaPlayerSyncSettingsClass))

/* defined sync categories */
#define 	SYNC_CATEGORY_MUSIC		"music"
#define 	SYNC_CATEGORY_PLAYLIST		"playlist"
#define		SYNC_CATEGORY_ALL_PODCASTS	"all-podcasts"		/* XXX seems a bit meaningless */
#define 	SYNC_CATEGORY_PODCAST		"podcast"

typedef struct
{
	GObject parent;
} RBMediaPlayerSyncSettings;

typedef struct
{
	GObjectClass parent;

	/* signals */
	void	(*updated) (void);
} RBMediaPlayerSyncSettingsClass;

GType				rb_media_player_sync_settings_get_type (void);

RBMediaPlayerSyncSettings *	rb_media_player_sync_settings_new (const char *keyfile);

gboolean			rb_media_player_sync_settings_save (RBMediaPlayerSyncSettings *settings);

/* sync categories */

void				rb_media_player_sync_settings_set_category (RBMediaPlayerSyncSettings *settings,
									    const char *category,
									    gboolean enabled);
gboolean			rb_media_player_sync_settings_sync_category (RBMediaPlayerSyncSettings *settings,
									     const char *category);
GList *				rb_media_player_sync_settings_get_enabled_categories (RBMediaPlayerSyncSettings *settings);

/* sync category groups */

void				rb_media_player_sync_settings_set_group (RBMediaPlayerSyncSettings *settings,
									 const char *category,
									 const char *group,
									 gboolean enabled);
gboolean			rb_media_player_sync_settings_group_enabled (RBMediaPlayerSyncSettings *settings,
									     const char *category,
									     const char *group);
gboolean			rb_media_player_sync_settings_sync_group (RBMediaPlayerSyncSettings *settings,
									  const char *category,
									  const char *group);
GList *				rb_media_player_sync_settings_get_enabled_groups (RBMediaPlayerSyncSettings *settings,
										  const char *category);

G_END_DECLS

#endif /* RB_MEDIA_PLAYER_SYNC_SETTINGS__H */
