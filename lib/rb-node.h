/*  RhythmBox
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *                     Marco Pesenti Gritti <marco@it.gnome.org>
 *                     Bastien Nocera <hadess@hadess.net>
 *                     Seth Nickell <snickell@stanford.edu>
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

#ifndef __RB_NODE_H
#define __RB_NODE_H

#include <gtk/gtk.h>
#include <glib-object.h>
#include <libxml/tree.h>

G_BEGIN_DECLS

typedef struct _RBNode        RBNode;
typedef struct _RBNodeClass   RBNodeClass;

typedef struct _RBNodeDetails RBNodeDetails;

#define RB_TYPE_NODE             (rb_node_get_type ())
#define RB_NODE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_NODE, RBNode))
#define RB_NODE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_NODE, RBNodeClass))
#define RB_IS_NODE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_NODE))
#define RB_IS_NODE_CLASS(klass)  (G_TYPE_INSTANCE_GET_CLASS ((klass), RB_TYPE_NODE))

struct _RBNode
{
        GObject base;

        RBNodeDetails *details;
};

struct _RBNodeClass
{
        GObjectClass parent_class;

	/* Signals FIXME implement */
	void (*destroyed)     (RBNode *node);

	void (*changed)       (RBNode *node);

	void (*child_changed) (RBNode *node, RBNode *child);
	void (*child_added)   (RBNode *node, RBNode *child);
	void (*child_removed) (RBNode *node, RBNode *child);
};

typedef void (*ActionNotifier) (RBNode *node, gint level, gpointer data);

GType        rb_node_get_type            (void) G_GNUC_CONST;

RBNode      *rb_node_new                 (const char *xml_name);

RBNode      *rb_node_get_parent          (RBNode *node);
GList       *rb_node_get_parents         (RBNode *node);
void         rb_node_set_grandparent     (RBNode *node, RBNode *granny);
RBNode      *rb_node_get_grandparent     (RBNode *node);
int          rb_node_n_children          (RBNode *node);
RBNode      *rb_node_get_nth_child       (RBNode *node, int n);
int          rb_node_get_child_index     (RBNode *parent, RBNode *node);
GList       *rb_node_get_children        (RBNode *node);
const gchar *rb_node_get_string_property (RBNode *node, const gchar *property);
gint         rb_node_get_int_property    (RBNode *node, const gchar *property);
void         rb_node_append              (RBNode *parent, RBNode *node);
void         rb_node_remove              (RBNode *node,
				          gint h_level,
				          gint l_level,
				          ActionNotifier func,
				          gpointer data);
void         rb_node_set_string_property (RBNode *node,
				          const gchar *name,
				          gchar *value);
void         rb_node_set_int_property    (RBNode *node,
				          const gchar *name,
				          gint value);
gboolean     rb_node_has_child           (RBNode *node,
				          RBNode *child);
RBNode      *rb_node_next                (RBNode *node);

void         rb_node_set_xml_name        (RBNode *node, const char *save_name);
xmlNodePtr   rb_node_save_to_xml         (RBNode *node, RBNode *grandparent,
				          xmlNodePtr parent, gboolean save_children);

G_END_DECLS

#endif /* __RB_NODE_H */
