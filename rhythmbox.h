/*
 *  Copyright (C) 2010 Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef __RHYTHMBOX_H
#define __RHYTHMBOX_H

#include <backends/rb-encoder.h>
#include <backends/rb-player-gst-filter.h>
#include <backends/rb-player-gst-tee.h>
#include <backends/rb-player.h>
#include <lib/rb-builder-helpers.h>
#include <lib/rb-debug.h>
#include <lib/rb-file-helpers.h>
#include <lib/rb-preferences.h>
#include <lib/rb-stock-icons.h>
#include <lib/rb-util.h>
#include <lib/libmediaplayerid/mediaplayerid.h>
#include <metadata/rb-metadata.h>
#include <podcast/rb-podcast-manager.h>
#include <podcast/rb-podcast-parse.h>
#include <shell/rb-shell.h>
#include <shell/rb-shell-player.h>
#include <shell/rb-shell-preferences.h>
#include <shell/rb-playlist-manager.h>
#include <shell/rb-removable-media-manager.h>
#include <shell/rb-history.h>
#include <shell/rb-play-order.h>
#include <shell/rb-plugin.h>
#include <sources/rb-display-page.h>
#include <sources/rb-display-page-group.h>
#include <sources/rb-display-page-model.h>
#include <sources/rb-display-page-tree.h>
#include <sources/rb-source.h>
#include <sources/rb-streaming-source.h>
#include <sources/rb-source-search.h>
#include <sources/rb-browser-source.h>
#include <sources/rb-removable-media-source.h>
#include <sources/rb-media-player-source.h>
#include <sources/rb-playlist-source.h>
#include <sources/rb-playlist-xml.h>
#include <sources/rb-auto-playlist-source.h>
#include <sources/rb-static-playlist-source.h>
#include <sources/rb-source-search-basic.h>
#include <widgets/rb-entry-view.h>
#include <widgets/rb-property-view.h>
#include <widgets/rb-dialog.h>
#include <widgets/rb-cell-renderer-pixbuf.h>
#include <widgets/rb-cell-renderer-rating.h>
#include <widgets/rb-rating.h>
#include <widgets/rb-library-browser.h>
#include <widgets/rb-segmented-bar.h>
#include <widgets/rb-song-info.h>
#include <widgets/rb-uri-dialog.h>
#include <lib/rb-string-value-map.h>
#include <rhythmdb/rhythmdb.h>
#include <rhythmdb/rhythmdb-property-model.h>
#include <rhythmdb/rhythmdb-query-model.h>
#include <rhythmdb/rhythmdb-query-results.h>
#include <rhythmdb/rhythmdb-import-job.h>
#include <rhythmdb/rb-refstring.h>

#endif /* __RHYTHMBOX_H */
