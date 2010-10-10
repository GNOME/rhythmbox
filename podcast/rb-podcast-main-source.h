/*
 *  Copyright (C) 2010  Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef __RB_PODCAST_MAIN_SOURCE_H
#define __RB_PODCAST_MAIN_SOURCE_H

#include <podcast/rb-podcast-source.h>
#include <podcast/rb-podcast-manager.h>

#define RB_TYPE_PODCAST_MAIN_SOURCE         (rb_podcast_main_source_get_type ())
#define RB_PODCAST_MAIN_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PODCAST_MAIN_SOURCE, RBPodcastMainSource))
#define RB_PODCAST_MAIN_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PODCAST_MAIN_SOURCE, RBPodcastMainSourceClass))
#define RB_IS_PODCAST_MAIN_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PODCAST_MAIN_SOURCE))
#define RB_IS_PODCAST_MAIN_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PODCAST_MAIN_SOURCE))
#define RB_PODCAST_MAIN_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PODCAST_MAIN_SOURCE, RBPodcastMainSourceClass))

typedef struct _RBPodcastMainSource RBPodcastMainSource;
typedef struct _RBPodcastMainSourceClass RBPodcastMainSourceClass;

typedef struct _RBPodcastMainSourcePrivate RBPodcastMainSourcePrivate;

struct _RBPodcastMainSource
{
	RBPodcastSource parent;

	RBPodcastMainSourcePrivate *priv;
};

struct _RBPodcastMainSourceClass
{
	RBPodcastSourceClass parent;
};

GType		rb_podcast_main_source_get_type 	(void);

RBSource	*rb_podcast_main_source_new 		(RBShell *shell,
							 RBPodcastManager *podcast_manager);

void		rb_podcast_main_source_add_subsources	(RBPodcastMainSource *source);

#endif /* __RB_PODCAST_MAIN_SOURCE_H */
