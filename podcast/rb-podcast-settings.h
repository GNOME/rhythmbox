/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

#ifndef RB_PODCAST_SETTINGS__H
#define RB_PODCAST_SETTINGS__H

#define PODCAST_SETTINGS_SCHEMA			"org.gnome.rhythmbox.podcast"

#define PODCAST_DOWNLOAD_DIR_KEY		"download-location"
#define PODCAST_DOWNLOAD_INTERVAL		"download-interval"
#define PODCAST_PANED_POSITION			"paned-position"

typedef enum {
	PODCAST_INTERVAL_HOURLY = 0,
	PODCAST_INTERVAL_DAILY,
	PODCAST_INTERVAL_WEEKLY,
	PODCAST_INTERVAL_MANUAL
} RBPodcastInterval;


#endif /* RB_PODCAST_SETTINGS__H */
