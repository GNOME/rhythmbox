/*
 *  arch-tag: Header for iPod source object
 *
 *  Copyright (C) 2004 Christophe Fergeau  <teuf@gnome.org>
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

#ifndef __RB_IPOD_SOURCE_H
#define __RB_IPOD_SOURCE_H

#include "rb-shell.h"
#include "rb-library-source.h"
#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_IPOD_SOURCE         (rb_ipod_source_get_type ())
#define RB_IPOD_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IPOD_SOURCE, RBiPodSource))
#define RB_IPOD_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IPOD_SOURCE, RBiPodSourceClass))
#define RB_IS_IPOD_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IPOD_SOURCE))
#define RB_IS_IPOD_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_IPOD_SOURCE))
#define RB_IPOD_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_IPOD_SOURCE, RBiPodSourceClass))

#define RHYTHMDB_ENTRY_TYPE_IPOD (rhythmdb_entry_ipod_get_type ())

typedef struct RBiPodSourcePrivate RBiPodSourcePrivate;

typedef struct
{
	RBLibrarySource parent;

	RBiPodSourcePrivate *priv;
} RBiPodSource;

typedef struct
{
	RBLibrarySourceClass parent;
} RBiPodSourceClass;

RBSource *	rb_ipod_source_new		(RBShell *shell, RhythmDB *db, 
						 GtkActionGroup *actiongroup);

GType		rb_ipod_source_get_type	        (void);

RhythmDBEntryType rhythmdb_entry_ipod_get_type  (void);

G_END_DECLS

#endif /* __RB_IPOD_SOURCE_H */
