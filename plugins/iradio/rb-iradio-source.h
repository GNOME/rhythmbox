/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2002,2003 Colin Walters <walters@debian.org>
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

#ifndef __RB_IRADIO_SOURCE_H
#define __RB_IRADIO_SOURCE_H

#include "rb-shell.h"
#include "rb-streaming-source.h"

G_BEGIN_DECLS

#define RB_TYPE_IRADIO_SOURCE         (rb_iradio_source_get_type ())
#define RB_IRADIO_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IRADIO_SOURCE, RBIRadioSource))
#define RB_IRADIO_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IRADIO_SOURCE, RBIRadioSourceClass))
#define RB_IS_IRADIO_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IRADIO_SOURCE))
#define RB_IS_IRADIO_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_IRADIO_SOURCE))
#define RB_IRADIO_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_IRADIO_SOURCE, RBIRadioSourceClass))

typedef struct RBIRadioSourcePrivate RBIRadioSourcePrivate;

typedef struct
{
	RBStreamingSource parent;

	RBIRadioSourcePrivate *priv;
} RBIRadioSource;

typedef struct
{
	RBStreamingSourceClass parent;
} RBIRadioSourceClass;

GType		rb_iradio_source_get_type	(void);

RBSource *	rb_iradio_source_new		(RBShell *shell, GObject *plugin);

void		rb_iradio_source_add_station	(RBIRadioSource *source,
						 const char *uri, const char *title, const char *genre);

void		rb_iradio_source_add_from_playlist (RBIRadioSource *source,
                                                    const char *uri);

void		_rb_iradio_source_register_type (GTypeModule *module);

G_END_DECLS

#endif /* __RB_IRADIO_SOURCE_H */
