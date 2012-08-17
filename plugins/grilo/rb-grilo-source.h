/*
 *  Copyright (C) 2011 Jonathan Matthew
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

#ifndef __RB_GRILO_SOURCE_H
#define __RB_GRILO_SOURCE_H

#include <grilo.h>

#include "rhythmdb.h"
#include "rb-source.h"

G_BEGIN_DECLS

#define RB_TYPE_GRILO_SOURCE         (rb_grilo_source_get_type ())
#define RB_GRILO_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_GRILO_SOURCE, RBGriloSource))
#define RB_GRILO_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_GRILO_SOURCE, RBGriloSourceClass))
#define RB_IS_GRILO_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_GRILO_SOURCE))
#define RB_IS_GRILO_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_GRILO_SOURCE))
#define RB_GRILO_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_GRILO_SOURCE, RBGriloSourceClass))

#define RB_TYPE_GRILO_ENTRY_TYPE         (rb_grilo_entry_type_get_type ())
#define RB_GRILO_ENTRY_TYPE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_GRILO_ENTRY_TYPE, RBGriloEntryType))
#define RB_GRILO_ENTRY_TYPE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_GRILO_ENTRY_TYPE, RBGriloEntryTypeClass))
#define RB_IS_GRILO_ENTRY_TYPE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_GRILO_ENTRY_TYPE))
#define RB_IS_GRILO_ENTRY_TYPE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_GRILO_ENTRY_TYPE))
#define RB_GRILO_ENTRY_TYPE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_GRILO_ENTRY_TYPE, RBGriloEntryTypeClass))

typedef struct _RBGriloSourcePrivate RBGriloSourcePrivate;

typedef struct
{
	RBSource parent;
	RBGriloSourcePrivate *priv;
} RBGriloSource;

typedef struct
{
	RBSourceClass parent;
} RBGriloSourceClass;

typedef struct _RhythmDBEntryType RBGriloEntryType;
typedef struct _RhythmDBEntryTypeClass RBGriloEntryTypeClass;

typedef struct
{
	GrlData *grilo_data;
	GrlData *grilo_container;
} RBGriloEntryData;

RBSource *		rb_grilo_source_new	(GObject *plugin,
						 GrlSource *grilo_source);
GType			rb_grilo_source_get_type (void);
void			_rb_grilo_source_register_type	(GTypeModule *module);


GType 			rb_grilo_entry_type_get_type (void);

G_END_DECLS

#endif /* __RB_GRILO_SOURCE_H */
