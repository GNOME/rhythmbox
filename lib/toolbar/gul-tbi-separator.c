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
#include <gtk/gtkstock.h>
#include "gul-tbi-separator.h"

#define NOT_IMPLEMENTED g_warning ("not implemented: " G_STRLOC);
//#define DEBUG_MSG(x) g_print x
#define DEBUG_MSG(x)

/**
 * Private data
 */
struct _GulTbiSeparatorPrivate 
{
	GtkWidget *widget;
};

/**
 * Private functions, only availble from this file
 */
static void		gul_tbi_separator_class_init		(GulTbiSeparatorClass *klass);
static void		gul_tbi_separator_init			(GulTbiSeparator *tb);
static void		gul_tbi_separator_finalize_impl		(GObject *o);
static GtkWidget *	gul_tbi_separator_get_widget_impl	(GulTbItem *i);
static GdkPixbuf *	gul_tbi_separator_get_icon_impl		(GulTbItem *i);
static gchar *		gul_tbi_separator_get_name_human_impl	(GulTbItem *i);
static gchar *		gul_tbi_separator_to_string_impl	(GulTbItem *i);
static gboolean		gul_tbi_separator_is_unique_impl	(GulTbItem *i);
static GulTbItem *	gul_tbi_separator_clone_impl		(GulTbItem *i);
static void		gul_tbi_separator_parse_properties_impl	(GulTbItem *i, const gchar *props);
static void		gul_tbi_separator_add_to_bonobo_tb_impl	(GulTbItem *i, 
								 BonoboUIComponent *ui, 
								 const char *container_path,
								 guint index);

static gpointer gul_tb_item_class;

/**
 * TbiSeparator object
 */

MAKE_GET_TYPE (gul_tbi_separator, "GulTbiSeparator", GulTbiSeparator, gul_tbi_separator_class_init, 
	       gul_tbi_separator_init, GUL_TYPE_TB_ITEM);

static void
gul_tbi_separator_class_init (GulTbiSeparatorClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = gul_tbi_separator_finalize_impl;
	
	GUL_TB_ITEM_CLASS (klass)->get_widget = gul_tbi_separator_get_widget_impl;
	GUL_TB_ITEM_CLASS (klass)->get_icon = gul_tbi_separator_get_icon_impl;
	GUL_TB_ITEM_CLASS (klass)->get_name_human = gul_tbi_separator_get_name_human_impl;
	GUL_TB_ITEM_CLASS (klass)->to_string = gul_tbi_separator_to_string_impl;
	GUL_TB_ITEM_CLASS (klass)->is_unique = gul_tbi_separator_is_unique_impl;
	GUL_TB_ITEM_CLASS (klass)->clone = gul_tbi_separator_clone_impl;
	GUL_TB_ITEM_CLASS (klass)->parse_properties = gul_tbi_separator_parse_properties_impl;
	GUL_TB_ITEM_CLASS (klass)->add_to_bonobo_tb = gul_tbi_separator_add_to_bonobo_tb_impl;
	
	gul_tb_item_class = g_type_class_peek_parent (klass);
}

static void 
gul_tbi_separator_init (GulTbiSeparator *tb)
{
	GulTbiSeparatorPrivate *p = g_new0 (GulTbiSeparatorPrivate, 1);
	tb->priv = p;
}

GulTbiSeparator *
gul_tbi_separator_new (void)
{
	GulTbiSeparator *ret = g_object_new (GUL_TYPE_TBI_SEPARATOR, NULL);
	return ret;
}

static void
gul_tbi_separator_finalize_impl (GObject *o)
{
	GulTbiSeparator *it = GUL_TBI_SEPARATOR (o);
	GulTbiSeparatorPrivate *p = it->priv;

	if (p->widget)
	{
		g_object_unref (p->widget);
	}

	g_free (p);
	
	DEBUG_MSG (("GulTbiSeparator finalized\n"));
	
	G_OBJECT_CLASS (gul_tb_item_class)->finalize (o);
}

static GtkWidget *
gul_tbi_separator_get_widget_impl (GulTbItem *i)
{
	return NULL;
}

static GdkPixbuf *
gul_tbi_separator_get_icon_impl (GulTbItem *i)
{
	return NULL;
}

static gchar *
gul_tbi_separator_get_name_human_impl (GulTbItem *i)
{
	return g_strdup (_("Separator"));
}

static gchar *
gul_tbi_separator_to_string_impl (GulTbItem *i)
{
	/* if it had any properties, the string should include them */
	return g_strdup_printf ("%s=separator", i->id);
}

static gboolean
gul_tbi_separator_is_unique_impl (GulTbItem *i)
{
	return FALSE;
}

static GulTbItem *
gul_tbi_separator_clone_impl (GulTbItem *i)
{
	GulTbItem *ret = GUL_TB_ITEM (gul_tbi_separator_new ());
	
	gul_tb_item_set_id (ret, i->id);

	/* should copy properties too, if any */

	return ret;
}

static void
gul_tbi_separator_add_to_bonobo_tb_impl (GulTbItem *i, BonoboUIComponent *ui, 
				    const char *container_path, guint index)
{
	static gint hack = 0;
	gchar *xml;

	xml = g_strdup_printf ("<separator name=\"sep%d\"/>", ++hack);
	bonobo_ui_component_set (ui, container_path, xml, NULL);
	g_free (xml);
	
}

static void
gul_tbi_separator_parse_properties_impl (GulTbItem *it, const gchar *props)
{
	/* we have no properties */
}

