/*
 *  arch-tag: Header with definitions of various GConf keys
 *
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#ifndef __RB_PREFERENCES_H
#define __RB_PREFERENCES_H

G_BEGIN_DECLS

#define CONF_PREFIX "/apps/rhythmbox"

#define CONF_FIRST_TIME CONF_PREFIX "/first_time_flag"
#define CONF_UI_DIR               CONF_PREFIX "/ui"
#define CONF_UI_STATUSBAR_HIDDEN  CONF_PREFIX "/ui/statusbar_hidden"
#define CONF_UI_SOURCELIST_HIDDEN CONF_PREFIX "/ui/sourcelist_hidden"
#define CONF_UI_SMALL_DISPLAY     CONF_PREFIX "/ui/small_display"
#define CONF_UI_OBSOLETE_COLUMNS_SETUP CONF_PREFIX "/ui/columns_setup"
#define CONF_UI_COLUMNS_SETUP CONF_PREFIX "/ui/rhythmdb_columns_setup"
#define CONF_UI_TIME_DISPLAY CONF_PREFIX "/ui/time_display"
#define CONF_UI_BROWSER_VIEWS CONF_PREFIX "/ui/browser_views"
#define CONF_STATE_PLAY_ORDER	CONF_PREFIX "/state/play_order"
#define CONF_STATE_REPEAT	CONF_PREFIX "/state/repeat"
#define CONF_STATE_VOLUME	CONF_PREFIX "/state/volume"

G_END_DECLS

#endif /* __RB_PREFERENCES_H */
