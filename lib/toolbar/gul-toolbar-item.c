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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libgnome/gnome-i18n.h>
#include "gul-gobject-misc.h"
#include "gul-toolbar-item.h"

#define NOT_IMPLEMENTED g_warning ("not implemented: " G_STRLOC);
//#define DEBUG_MSG(x) g_print x
#define DEBUG_MSG(x)

/**
 * Private data
 */
struct _GulTbItemPrivate 
{
};

/**
 * Private functions, only availble from this file
 */
static void		gul_tb_item_class_init			(GulTbItemClass *klass);
static void		gul_tb_item_init			(GulTbItem *tb);
static void		gul_tb_item_finalize_impl		(GObject *o);

static gpointer g_object_class;

/**
 * TbItem object
 */

MAKE_GET_TYPE (gul_tb_item, "GulTbItem", GulTbItem, gul_tb_item_class_init, 
	       gul_tb_item_init, G_TYPE_OBJECT);

static void
gul_tb_item_class_init (GulTbItemClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = gul_tb_item_finalize_impl;
	
	g_object_class = g_type_class_peek_parent (klass);
}

static void 
gul_tb_item_init (GulTbItem *it)
{
	GulTbItemPrivate *p = g_new0 (GulTbItemPrivate, 1);
	it->priv = p;
	it->id = g_strdup ("");
}

static void
gul_tb_item_finalize_impl (GObject *o)
{
	GulTbItem *it = GUL_TB_ITEM (o);
	GulTbItemPrivate *p = it->priv;

	g_free (it->id);
	g_free (p);
	
	DEBUG_MSG (("GulTbItem finalized\n"));
	
	G_OBJECT_CLASS (g_object_class)->finalize (o);
}

GtkWidget *
gul_tb_item_get_widget (GulTbItem *i)
{
	return GUL_TB_ITEM_GET_CLASS (i)->get_widget (i);
}

GdkPixbuf *
gul_tb_item_get_icon (GulTbItem *i)
{
	return GUL_TB_ITEM_GET_CLASS (i)->get_icon (i);
}

gchar *
gul_tb_item_get_name_human (GulTbItem *i)
{
	return GUL_TB_ITEM_GET_CLASS (i)->get_name_human (i);
}

gchar *
gul_tb_item_to_string (GulTbItem *i)
{
	return GUL_TB_ITEM_GET_CLASS (i)->to_string (i);
}

gboolean
gul_tb_item_is_unique (GulTbItem *i)
{
	return GUL_TB_ITEM_GET_CLASS (i)->is_unique (i);
}

GulTbItem *
gul_tb_item_clone (GulTbItem *i)
{
	return GUL_TB_ITEM_GET_CLASS (i)->clone (i);
}

void
gul_tb_item_add_to_bonobo_tb (GulTbItem *i, BonoboUIComponent *ui, 
			      const char *container_path, guint index)
{
	GUL_TB_ITEM_GET_CLASS (i)->add_to_bonobo_tb (i, ui, container_path, index);
}

void
gul_tb_item_set_id (GulTbItem *i, const gchar *id)
{
	g_return_if_fail (GUL_IS_TB_ITEM (i));

	g_free (i->id);
	i->id = g_strdup (id);
}

void
gul_tb_item_parse_properties (GulTbItem *i, const gchar *props)
{
	GUL_TB_ITEM_GET_CLASS (i)->parse_properties (i, props);
}
