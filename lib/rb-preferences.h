/*
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#ifndef __RB_PREFERENCES_H
#define __RB_PREFERENCES_H

G_BEGIN_DECLS

#define CONF_PREFIX "/apps/rhythmbox"

#define CONF_FIRST_TIME CONF_PREFIX   "/first_time_flag"
#define CONF_GRACE_PERIOD CONF_PREFIX "/grace_period"
#define CONF_UI_DIR               CONF_PREFIX "/ui"
#define CONF_UI_STATUSBAR_HIDDEN  CONF_PREFIX "/ui/statusbar_hidden"
#define CONF_UI_TOOLBAR_HIDDEN  CONF_PREFIX "/ui/toolbar_hidden"
#define CONF_UI_TOOLBAR_STYLE	CONF_PREFIX "/ui/toolbar_style"
#define CONF_UI_SONG_POSITION_SLIDER_HIDDEN  CONF_PREFIX "/ui/song_position_slider_hidden"
#define CONF_UI_SIDEPANE_HIDDEN CONF_PREFIX "/ui/sourcelist_hidden"
#define CONF_UI_RSIDEPANE_HIDDEN CONF_PREFIX "/ui/rsidebar_hidden"
#define CONF_UI_QUEUE_AS_SIDEBAR  CONF_PREFIX "/ui/queue_as_sidebar"
#define CONF_UI_SMALL_DISPLAY     CONF_PREFIX "/ui/small_display"
#define CONF_UI_FULLSCREEN     CONF_PREFIX "/ui/fullscreen"
#define CONF_UI_OBSOLETE_COLUMNS_SETUP CONF_PREFIX "/ui/columns_setup"
#define CONF_UI_COLUMNS_SETUP CONF_PREFIX "/ui/rhythmdb_columns_setup"
#define CONF_UI_TIME_DISPLAY CONF_PREFIX "/ui/time_display"
#define CONF_UI_BROWSER_VIEWS CONF_PREFIX "/ui/library/browser_views"
#define CONF_STATE_PLAY_ORDER	CONF_PREFIX "/state/play_order"
#define CONF_STATE_VOLUME	CONF_PREFIX "/state/volume"
#define CONF_STATE_BURN_DEVICE	CONF_PREFIX "/state/burn_device"

#define CONF_AUDIOSCROBBLER_USERNAME CONF_PREFIX "/audioscrobbler/username"
#define CONF_AUDIOSCROBBLER_PASSWORD CONF_PREFIX "/audioscrobbler/password"
#define CONF_AUDIOSCROBBLER_URL      CONF_PREFIX "/audioscrobbler/scrobbler_url"

#define CONF_DAAP_ENABLE_SHARING   CONF_PREFIX "/sharing/enable_sharing"
#define CONF_DAAP_SHARE_NAME       CONF_PREFIX "/sharing/share_name"
#define CONF_DAAP_SHARE_PASSWORD   CONF_PREFIX "/sharing/share_password"
#define CONF_DAAP_REQUIRE_PASSWORD CONF_PREFIX "/sharing/require_password"

#define CONF_LIBRARY_LOCATION	CONF_PREFIX "/library_locations"
#define CONF_MONITOR_LIBRARY	CONF_PREFIX "/monitor_library"
#define CONF_LIBRARY_STRIP_CHARS	CONF_PREFIX "/library_strip_chars"
#define CONF_LIBRARY_LAYOUT_PATH	CONF_PREFIX "/library_layout_path"
#define CONF_LIBRARY_LAYOUT_FILENAME	CONF_PREFIX "/library_layout_filename"
#define CONF_LIBRARY_PREFERRED_FORMAT	CONF_PREFIX "/library_preferred_format"

#define CONF_PLUGINS_PREFIX		CONF_PREFIX "/plugins"
#define CONF_PLUGIN_DISABLE_USER	CONF_PLUGINS_PREFIX "/no_user_plugins"
#define CONF_PLUGIN_ACTIVE_KEY		CONF_PLUGINS_PREFIX "/%s/active"
#define CONF_PLUGIN_HIDDEN_KEY		CONF_PLUGINS_PREFIX "/%s/hidden"

#define CONF_PLAYER_DIR			CONF_PREFIX "/player"
#define CONF_PLAYER_USE_XFADE_BACKEND 	CONF_PREFIX "/player/use_xfade_backend"
#define CONF_PLAYER_TRANSITION_ALBUM_CHECK CONF_PREFIX "/player/transition_album_check"
#define CONF_PLAYER_TRANSITION_TIME 	CONF_PREFIX "/player/transition_time"
#define CONF_PLAYER_NETWORK_BUFFER_SIZE	CONF_PREFIX "/player/network_buffer_size"

G_END_DECLS

#endif /* __RB_PREFERENCES_H */
