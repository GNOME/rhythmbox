/* 
 *  arch-tag: Header for Internet Radio source object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2002,2003 Colin Walters <walters@debian.org>
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
 */

#ifndef __RB_IRADIO_SOURCE_H
#define __RB_IRADIO_SOURCE_H

#include "rb-source.h"

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
	RBSource parent;

	RBIRadioSourcePrivate *priv;
} RBIRadioSource;

typedef struct
{
	RBSourceClass parent;
} RBIRadioSourceClass;

GType		rb_iradio_source_get_type	(void);

RBSource *	rb_iradio_source_new		(RhythmDB *db);

void		rb_iradio_source_add_station	(RBIRadioSource *source,
						 const char *uri, const char *title, const char *genre);

G_END_DECLS

#endif /* __RB_IRADIO_SOURCE_H */
