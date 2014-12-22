/*
 *  Copyright (C) 2010 Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef __RB_SYNC_SETTINGS_UI_H
#define __RB_SYNC_SETTINGS_UI_H

#include <gtk/gtk.h>

#include "rb-media-player-source.h"
#include "rb-sync-settings.h"

G_BEGIN_DECLS

#define RB_TYPE_SYNC_SETTINGS_UI         (rb_sync_settings_ui_get_type ())
#define RB_SYNC_SETTINGS_UI(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SYNC_SETTINGS_UI, RBSyncSettingsUI))
#define RB_SYNC_SETTINGS_UI_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SYNC_SETTINGS_UI, RBSyncSettingsUIClass))
#define RB_IS_SYNC_SETTINGS_UI(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SYNC_SETTINGS_UI))
#define RB_IS_SYNC_SETTINGS_UI_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SYNC_SETTINGS_UI))
#define RB_SYNC_SETTINGS_UI_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SYNC_SETTINGS_UI, RBSyncSettingsUIClass))

typedef struct _RBSyncSettingsUI RBSyncSettingsUI;
typedef struct _RBSyncSettingsUIClass RBSyncSettingsUIClass;
typedef struct _RBSyncSettingsUIPrivate RBSyncSettingsUIPrivate;

struct _RBSyncSettingsUI
{
	GtkBox parent;

	RBSyncSettingsUIPrivate *priv;
};

struct _RBSyncSettingsUIClass
{
	GtkBoxClass parent_class;
};

GType			rb_sync_settings_ui_get_type (void);

GtkWidget *		rb_sync_settings_ui_new (RBMediaPlayerSource *source, RBSyncSettings *settings);

G_END_DECLS

#endif /* __RB_SYNC_SETTINGS_UI_H */
