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

#include <libgnome/gnome-i18n.h>

#include "rb-stream.h"
#include "rb-dialog.h"

static void rb_stream_class_init (RBStreamClass *klass);
static void rb_stream_init (RBStream *stream);
static void rb_stream_finalize (GObject *object);

struct RBStreamPrivate
{
	MonkeyMediaStreamInfo *info;
};

static GObjectClass *parent_class = NULL;

GType
rb_stream_get_type (void)
{
	static GType rb_stream_type = 0;

	if (rb_stream_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBStreamClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_stream_class_init,
			NULL,
			NULL,
			sizeof (RBStream),
			0,
			(GInstanceInitFunc) rb_stream_init
		};

		rb_stream_type = g_type_register_static (MONKEY_MEDIA_TYPE_AUDIO_STREAM,
							 "RBStream",
							 &our_info, 0);
	}

	return rb_stream_type;
}

static void
rb_stream_class_init (RBStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_stream_finalize;
}

static void
rb_stream_init (RBStream *stream)
{
	stream->priv = g_new0 (RBStreamPrivate, 1);
}

static void
rb_stream_finalize (GObject *object)
{
	RBStream *stream;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_STREAM (object));

	stream = RB_STREAM (object);

	g_return_if_fail (stream->priv != NULL);
	
	g_free (stream->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBStream *
rb_stream_new (const char *location)
{
	RBStream *stream;
	GError *tmp;

	g_return_val_if_fail (location != NULL, NULL);

	stream = g_object_new (RB_TYPE_STREAM,
			       "location", location,
			       "volume", 1.0,
			       NULL);

	g_return_val_if_fail (stream->priv != NULL, NULL);

	g_object_get (G_OBJECT (stream), "error", &tmp, NULL);

	if (tmp != NULL)
	{
		rb_error_dialog (_("Failed to open %s for playing, error was:\n%s"), location, tmp->message);
		g_error_free (tmp);

		g_object_unref (G_OBJECT (stream));
		stream = NULL;
	}

	return stream;
}

void
rb_stream_get_info (RBStream *stream,
		    MonkeyMediaStreamInfoField field,
		    GValue *value)
{
}

char *
rb_stream_get_title (RBStream *stream)
{
	return NULL;
}
