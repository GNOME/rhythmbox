/*
 *  arch-tag: Header for Internet Radio source object
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho <renato.filho@indt.org.br>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#ifndef __RB_PODCAST_SOURCE_H
#define __RB_PODCAST_SOURCE_H

#include "rb-shell.h"
#include "rb-source.h"

G_BEGIN_DECLS

#define RB_TYPE_PODCAST_SOURCE         (rb_podcast_source_get_type ())
#define RB_PODCAST_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PODCAST_SOURCE, RBPodcastSource))
#define RB_PODCAST_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PODCAST_SOURCE, RBPodcastSourceClass))
#define RB_IS_PODCAST_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PODCAST_SOURCE))
#define RB_IS_PODCAST_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PODCAST_SOURCE))
#define RB_PODCAST_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PODCAST_SOURCE, RBPodcastSourceClass))

typedef struct RBPodcastSourcePrivate RBPodcastSourcePrivate;

typedef struct
{
	RBSource parent;

	RBPodcastSourcePrivate *priv;
} RBPodcastSource;

typedef struct
{
	RBSourceClass parent;
} RBPodcastSourceClass;

GType		rb_podcast_source_get_type	(void);
RBSource *	rb_podcast_source_new		(RBShell *shell);
void 		rb_podcast_source_add_feed	(RBPodcastSource *source, const gchar *uri);
void 		rb_podcast_source_shutdown	(RBPodcastSource *source);

G_END_DECLS

#endif /* __RB_PODCAST_SOURCE_H */
