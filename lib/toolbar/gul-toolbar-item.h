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

#ifndef __gul_toolbar_item_h
#define __gul_toolbar_item_h

#include <glib-object.h>

#include <bonobo/bonobo-ui-component.h>
#include <gtk/gtkwidget.h>

/* object forward declarations */

typedef struct _GulTbItem GulTbItem;
typedef struct _GulTbItemClass GulTbItemClass;
typedef struct _GulTbItemPrivate GulTbItemPrivate;

/**
 * TbItem object
 */

#define GUL_TYPE_TB_ITEM		(gul_tb_item_get_type())
#define GUL_TB_ITEM(object)		(G_TYPE_CHECK_INSTANCE_CAST((object), GUL_TYPE_TB_ITEM,\
					 GulTbItem))
#define GUL_TB_ITEM_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), GUL_TYPE_TB_ITEM,\
					 GulTbItemClass))
#define GUL_IS_TB_ITEM(object)		(G_TYPE_CHECK_INSTANCE_TYPE((object), GUL_TYPE_TB_ITEM))
#define GUL_IS_TB_ITEM_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), GUL_TYPE_TB_ITEM))
#define GUL_TB_ITEM_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GUL_TYPE_TB_ITEM,\
					 GulTbItemClass))

struct _GulTbItemClass 
{
	GObjectClass parent_class;
	
	/* virtual */
	GtkWidget *	(*get_widget)		(GulTbItem *it);
	GdkPixbuf *	(*get_icon)		(GulTbItem *it);
	gchar *		(*get_name_human)	(GulTbItem *it);
	gchar *		(*to_string)		(GulTbItem *it);
	gboolean	(*is_unique)		(GulTbItem *it);
	void		(*add_to_bonobo_tb)	(GulTbItem *it, BonoboUIComponent *ui, 
						 const char *container_path, guint index);
	GulTbItem *	(*clone)		(GulTbItem *it);
	void		(*parse_properties)	(GulTbItem *it, const gchar *props);
};

/* Remember: fields are public read-only */
struct _GulTbItem
{
	GObject parent_object;

	gchar *id;

	GulTbItemPrivate *priv;
};

/* this class is abstract */

GType		gul_tb_item_get_type		(void);
GtkWidget *	gul_tb_item_get_widget		(GulTbItem *i);
GdkPixbuf *	gul_tb_item_get_icon		(GulTbItem *i);
gchar *		gul_tb_item_get_name_human	(GulTbItem *i);
gchar *		gul_tb_item_to_string		(GulTbItem *i);
gboolean	gul_tb_item_is_unique		(GulTbItem *i);
void		gul_tb_item_add_to_bonobo_tb	(GulTbItem *i, BonoboUIComponent *ui, 
						 const char *container_path, guint index);
GulTbItem *	gul_tb_item_clone		(GulTbItem *i);
void		gul_tb_item_set_id		(GulTbItem *i, const gchar *id);
void		gul_tb_item_parse_properties	(GulTbItem *i, const gchar *props);

#endif
