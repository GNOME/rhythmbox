/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  $Id$
 */

#include <monkey-media.h>

#ifndef __RB_STREAM_H
#define __RB_STREAM_H

G_BEGIN_DECLS

#define RB_TYPE_STREAM         (rb_stream_get_type ())
#define RB_STREAM(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_STREAM, RBStream))
#define RB_STREAM_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_STREAM, RBStreamClass))
#define RB_IS_STREAM(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_STREAM))
#define RB_IS_STREAM_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_STREAM))
#define RB_STREAM_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_STREAM, RBStreamClass))

typedef struct RBStreamPrivate RBStreamPrivate;

typedef struct
{
	MonkeyMediaAudioStream parent;

	RBStreamPrivate *priv;
} RBStream;

typedef struct
{
	MonkeyMediaAudioStreamClass parent_class;
} RBStreamClass;

GType     rb_stream_get_type  (void);

RBStream *rb_stream_new       (const char *location);

void      rb_stream_get_info  (RBStream *stream,
			       MonkeyMediaStreamInfoField field,
			       GValue *value);

char     *rb_stream_get_title (RBStream *stream); /* Build display string */

G_END_DECLS

#endif /* __RB_STREAM_H */
