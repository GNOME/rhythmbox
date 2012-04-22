/*
 *  Copyright (C) 2012  Jonathan Matthew <jonathan@d14n.org>
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

#include <gtk/gtk.h>

#ifndef RB_OBJECT_PROPERTY_EDITOR_H
#define RB_OBJECT_PROPERTY_EDITOR_H

G_BEGIN_DECLS

#define RB_TYPE_OBJECT_PROPERTY_EDITOR         (rb_object_property_editor_get_type ())
#define RB_OBJECT_PROPERTY_EDITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_OBJECT_PROPERTY_EDITOR, RBObjectPropertyEditor))
#define RB_OBJECT_PROPERTY_EDITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_OBJECT_PROPERTY_EDITOR, RBObjectPropertyEditorClass))
#define RB_IS_OBJECT_PROPERTY_EDITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_OBJECT_PROPERTY_EDITOR))
#define RB_IS_OBJECT_PROPERTY_EDITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_OBJECT_PROPERTY_EDITOR))
#define RB_OBJECT_PROPERTY_EDITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_OBJECT_PROPERTY_EDITOR, RBObjectPropertyEditorClass))

typedef struct _RBObjectPropertyEditor RBObjectPropertyEditor;
typedef struct _RBObjectPropertyEditorClass RBObjectPropertyEditorClass;
typedef struct _RBObjectPropertyEditorPrivate RBObjectPropertyEditorPrivate;

struct _RBObjectPropertyEditor
{
	GtkGrid parent;

	RBObjectPropertyEditorPrivate *priv;
};

struct _RBObjectPropertyEditorClass
{
	GtkGridClass parent_class;

	void (*changed) (RBObjectPropertyEditor *editor);
};

GType		rb_object_property_editor_get_type (void);

GtkWidget *	rb_object_property_editor_new (GObject *object, char **properties);


G_END_DECLS

#endif /* RB_OBJECT_PROPERTY_EDITOR_H */
