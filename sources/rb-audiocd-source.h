/*
 *  arch-tag: Header for AudioCD source object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@debian.org>
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

#ifndef __RB_AUDIOCD_SOURCE_H
#define __RB_AUDIOCD_SOURCE_H

#include <bonobo/bonobo-ui-container.h>

#include "rb-source.h"
#include "rb-library.h"
#include "monkey-media.h"
#include "monkey-media-audio-cd.h"

G_BEGIN_DECLS

#define RB_TYPE_AUDIOCD_SOURCE         (rb_audiocd_view_get_type ())
#define RB_AUDIOCD_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOCD_SOURCE, RBAudiocdSource))
#define RB_AUDIOCD_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_AUDIOCD_SOURCE, RBAudiocdSourceClass))
#define RB_IS_AUDIOCD_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOCD_SOURCE))
#define RB_IS_AUDIOCD_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOCD_SOURCE))
#define RB_AUDIOCD_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOCD_SOURCE, RBAudiocdSourceClass))

typedef struct RBAudiocdSourcePrivate RBAudiocdSourcePrivate;

typedef struct
{
	RBSource parent;

	RBAudiocdSourcePrivate *priv;
} RBAudiocdSource;

typedef struct
{
	RBSourceClass parent;
} RBAudiocdSourceClass;

GType		rb_audiocd_view_get_type           (void);

RBSource *	rb_audiocd_view_new                (BonoboUIContainer *container,
						    MonkeyMediaAudioCD *cd);

void		rb_audiocd_view_set_name           (RBAudiocdSource *audiocd,
						    const char *name);

const char *	rb_audiocd_view_get_file           (RBAudiocdSource *audiocd);
void		rb_audiocd_view_remove_file        (RBAudiocdSource *audiocd);

void		rb_audiocd_view_save               (RBAudiocdSource *source);
void		rb_audiocd_view_load               (RBAudiocdSource *source);

void		rb_audiocd_view_add_node           (RBAudiocdSource *source,
						    RBNode *node);

void		rb_audiocd_refresh_cd              (RBAudiocdSource *source);
gboolean	rb_audiocd_is_cd_available         (RBAudiocdSource *source);
gboolean	rb_audiocd_is_any_device_available (void);

G_END_DECLS

#endif /* __RB_AUDIOCD_SOURCE_H */
