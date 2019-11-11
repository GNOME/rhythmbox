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

#ifndef RB_PODCAST_SEARCH_H
#define RB_PODCAST_SEARCH_H

#include <glib-object.h>

#include "rb-podcast-parse.h"

G_BEGIN_DECLS

#define RB_TYPE_PODCAST_SEARCH         (rb_podcast_search_get_type ())
#define RB_PODCAST_SEARCH(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PODCAST_SEARCH, RBPodcastSearch))
#define RB_PODCAST_SEARCH_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PODCAST_SEARCH, RBPodcastSearchClass))
#define RB_IS_PODCAST_SEARCH(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PODCAST_SEARCH))
#define RB_IS_PODCAST_SEARCH_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PODCAST_SEARCH))
#define RB_PODCAST_SEARCH_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PODCAST_SEARCH, RBPodcastSearchClass))

typedef struct _RBPodcastSearch RBPodcastSearch;
typedef struct _RBPodcastSearchClass RBPodcastSearchClass;

struct _RBPodcastSearch
{
	GObject parent;
};

struct _RBPodcastSearchClass
{
	GObjectClass parent;

	/* methods */
	void	(*start)	(RBPodcastSearch *search, const char *text, int max_results);
	void	(*cancel)	(RBPodcastSearch *search);
};

GType		rb_podcast_search_get_type		(void);

void		rb_podcast_search_start			(RBPodcastSearch *search, const char *text, int max_results);
void		rb_podcast_search_cancel		(RBPodcastSearch *search);

void		rb_podcast_search_result		(RBPodcastSearch *search, RBPodcastChannel *data);
void		rb_podcast_search_finished		(RBPodcastSearch *search, gboolean successful);

/* built in search types */

GType		rb_podcast_search_itunes_get_type	(void);

#endif /* RB_PODCAST_SEARCH_H */
