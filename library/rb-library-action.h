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

#ifndef __RB_LIBRARY_ACTION_H
#define __RB_LIBRARY_ACTION_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum
{
	RB_LIBRARY_ACTION_ADD_FILE,
	RB_LIBRARY_ACTION_ADD_DIRECTORY,
	RB_LIBRARY_ACTION_REMOVE_FILE,
	RB_LIBRARY_ACTION_UPDATE_FILE,
	RB_LIBRARY_ACTION_OPERATION_END,
} RBLibraryActionType;

#define RB_TYPE_LIBRARY_ACTION_TYPE (rb_library_action_type_get_type ())

GType rb_library_action_type_get_type (void);

#define RB_TYPE_LIBRARY_ACTION         (rb_library_action_get_type ())
#define RB_LIBRARY_ACTION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LIBRARY_ACTION, RBLibraryAction))
#define RB_LIBRARY_ACTION_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LIBRARY_ACTION, RBLibraryActionClass))
#define RB_IS_LIBRARY_ACTION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LIBRARY_ACTION))
#define RB_IS_LIBRARY_ACTION_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LIBRARY_ACTION))
#define RB_LIBRARY_ACTION_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LIBRARY_ACTION, RBLibraryActionClass))

typedef struct RBLibraryActionPrivate RBLibraryActionPrivate;

typedef struct
{
	GObject parent;

	RBLibraryActionPrivate *priv;
} RBLibraryAction;

typedef struct
{
	GObjectClass parent;

	void (*handled) (RBLibraryAction *action);
} RBLibraryActionClass;

GType               rb_library_action_get_type  (void);

RBLibraryAction    *rb_library_action_new       (RBLibraryActionType type,
						 const char *uri);

void                rb_library_action_get       (RBLibraryAction *action,
						 RBLibraryActionType *type,
						 char **uri);

G_END_DECLS

#endif /* __RB_LIBRARY_ACTION_H */
