/*
 *  Compatibility layer between libdmapsharing 3.0 and 4.0 APIs
 *
 *  Copyright (C) 2020 W. Michael Petullo <mike@flyn.org>
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

#ifndef __RB_DACP_COMPAT
#define __RB_DACP_COMPAT

#include "rb-dmap-compat.h"

#ifdef LIBDMAPSHARING_COMPAT

/* Building against libdmapsharing 3 API. */

#define DmapControlPlayer DACPPlayer
#define DMAP_CONTROL_PLAYER DACP_PLAYER
#define DmapControlPlayerInterface DACPPlayerIface
#define DMAP_CONTROL_PLAY_PAUSED DACP_PLAY_PAUSED
#define DMAP_CONTROL_PLAY_PLAYING DACP_PLAY_PLAYING
#define DMAP_CONTROL_PLAY_STOPPED DACP_PLAY_STOPPED
#define DMAP_CONTROL_REPEAT_ALL DACP_REPEAT_ALL
#define DMAP_CONTROL_REPEAT_NONE DACP_REPEAT_NONE
#define DmapControlShare DACPShare
#define dmap_control_share_new dacp_share_new
#define dmap_control_share_pair dacp_share_pair
#define dmap_control_share_player_updated dacp_share_player_updated
#define DMAP_TYPE_CONTROL_PLAYER DACP_TYPE_PLAYER

gchar *rb_dacp_player_now_playing_artwork (DmapControlPlayer *player, guint width, guint height);

static inline void
dmap_control_share_start_lookup_compat(DmapControlShare *share, GError **error)
{
	dacp_share_start_lookup(share);
}

static inline void
dmap_control_share_stop_lookup_compat(DmapControlShare *share, GError **error)
{
	dacp_share_stop_lookup(share);
}

static inline guchar *
rb_dacp_player_now_playing_artwork_compat(DmapControlPlayer *player, guint width, guint height)
{
	return (guchar *) rb_dacp_player_now_playing_artwork(player, width, height);
}

#else

/* Building against libdmapsharing 4 API. */

gchar *rb_dacp_player_now_playing_artwork (DmapControlPlayer *player, guint width, guint height);

static inline void
dmap_control_share_start_lookup_compat(DmapControlShare *share, GError **error)
{
	dmap_control_share_start_lookup(share, error);
}

static inline void
dmap_control_share_stop_lookup_compat(DmapControlShare *share, GError **error)
{
	dmap_control_share_stop_lookup(share, error);
}

static inline gchar *
rb_dacp_player_now_playing_artwork_compat(DmapControlPlayer *player, guint width, guint height)
{
	return rb_dacp_player_now_playing_artwork(player, width, height);
}

#endif

#endif /* __RB_DACP_COMPAT */
