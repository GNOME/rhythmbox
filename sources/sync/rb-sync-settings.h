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

#ifndef RB_SYNC_SETTINGS__H
#define RB_SYNC_SETTINGS__H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_SYNC_SETTINGS         (rb_sync_settings_get_type ())
#define RB_SYNC_SETTINGS(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SYNC_SETTINGS, RBSyncSettings))
#define RB_SYNC_SETTINGS_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SYNC_SETTINGS, RBSyncSettingsClass))
#define RB_IS_SYNC_SETTINGS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SYNC_SETTINGS))
#define RB_IS_SYNC_SETTINGS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SYNC_SETTINGS))
#define RB_SYNC_SETTINGS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SYNC_SETTINGS, RBSyncSettingsClass))

/* defined sync categories */
#define		SYNC_CATEGORY_MUSIC		"music"
#define 	SYNC_CATEGORY_PODCAST		"podcast"

/* defined sync groups */
#define		SYNC_GROUP_ALL_MUSIC		"x-rb-all-music"

typedef struct
{
	GObject parent;
} RBSyncSettings;

typedef struct
{
	GObjectClass parent;

	/* signals */
	void	(*updated) (RBSyncSettings *settings);
} RBSyncSettingsClass;

GType			rb_sync_settings_get_type (void);

RBSyncSettings *	rb_sync_settings_new (const char *keyfile);

gboolean		rb_sync_settings_save (RBSyncSettings *settings);

/* sync categories */

void			rb_sync_settings_set_category (RBSyncSettings *settings,
						       const char *category,
						       gboolean enabled);
gboolean		rb_sync_settings_sync_category (RBSyncSettings *settings,
							const char *category);
GList *			rb_sync_settings_get_enabled_categories (RBSyncSettings *settings);

/* sync category groups */

void			rb_sync_settings_set_group (RBSyncSettings *settings,
						    const char *category,
						    const char *group,
						    gboolean enabled);
gboolean		rb_sync_settings_group_enabled (RBSyncSettings *settings,
							const char *category,
							const char *group);
gboolean		rb_sync_settings_sync_group (RBSyncSettings *settings,
						     const char *category,
						     const char *group);
gboolean		rb_sync_settings_has_enabled_groups (RBSyncSettings *settings,
							     const char *category);
GList *			rb_sync_settings_get_enabled_groups (RBSyncSettings *settings,
							     const char *category);
void			rb_sync_settings_clear_groups (RBSyncSettings *settings,
						       const char *category);

G_END_DECLS

#endif /* RB_SYNC_SETTINGS__H */
