/*
 *  Copyright (C) 2002  Ricardo Fernández Pascual
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __gul_toolbar_h
#define __gul_toolbar_h

#include <glib-object.h>

#include "gul-toolbar-item.h"

/* object forward declarations */

typedef struct _GulToolbar GulToolbar;
typedef struct _GulToolbarClass GulToolbarClass;
typedef struct _GulToolbarPrivate GulToolbarPrivate;

/**
 * Toolbar object
 */

#define GUL_TYPE_TOOLBAR		(gul_toolbar_get_type())
#define GUL_TOOLBAR(object)		(G_TYPE_CHECK_INSTANCE_CAST((object), GUL_TYPE_TOOLBAR,\
					 GulToolbar))
#define GUL_TOOLBAR_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), GUL_TYPE_TOOLBAR,\
					 GulToolbarClass))
#define GUL_IS_TOOLBAR(object)		(G_TYPE_CHECK_INSTANCE_TYPE((object), GUL_TYPE_TOOLBAR))
#define GUL_IS_TOOLBAR_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), GUL_TYPE_TOOLBAR))
#define GUL_TOOLBAR_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GUL_TYPE_TOOLBAR,\
					 GulToolbarClass))

struct _GulToolbarClass 
{
	GObjectClass parent_class;
	
	/* signals */
	void	(*changed)	(GulToolbar *tb);

};

/* Remember: fields are public read-only */
struct _GulToolbar
{
	GObject parent_object;

	GulToolbarPrivate *priv;
};

GType		gul_toolbar_get_type		(void);
GulToolbar *	gul_toolbar_new			(void);
gboolean	gul_toolbar_parse		(GulToolbar *tb, const gchar *cfg);
gchar *		gul_toolbar_to_string		(GulToolbar *tb);
gboolean	gul_toolbar_listen_to_gconf	(GulToolbar *tb, const gchar *gconf_key);
GulTbItem *	gul_toolbar_get_item_by_id	(GulToolbar *tb, const gchar *id);
const GSList *	gul_toolbar_get_item_list	(GulToolbar *tb);
void		gul_toolbar_add_item		(GulToolbar *tb, GulTbItem *it, gint index);
void		gul_toolbar_remove_item		(GulToolbar *tb, GulTbItem *it);
void		gul_toolbar_set_fixed_order	(GulToolbar *tb, gboolean value);
void		gul_toolbar_set_check_unique	(GulToolbar *tb, gboolean value);
gboolean	gul_toolbar_get_check_unique	(GulToolbar *tb);

#endif
