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
#include <gtk/gtkhbox.h>
#include <bonobo/bonobo-control.h>

#include "gul-tbi-volume.h"
#include "gul-gobject-misc.h"

#define NOT_IMPLEMENTED g_warning ("not implemented: " G_STRLOC);
//#define DEBUG_MSG(x) g_print x
#define DEBUG_MSG(x)

/**
 * Private data
 */
struct _GulTbiVolumePrivate 
{
	GtkWidget *widget;
};

/**
 * Private functions, only availble from this file
 */
static void		gul_tbi_volume_class_init		(GulTbiVolumeClass *klass);
static void		gul_tbi_volume_init			(GulTbiVolume *tb);
static void		gul_tbi_volume_finalize_impl		(GObject *o);
static GtkWidget *	gul_tbi_volume_get_widget_impl		(GulTbItem *i);
static GdkPixbuf *	gul_tbi_volume_get_icon_impl		(GulTbItem *i);
static gchar *		gul_tbi_volume_get_name_human_impl	(GulTbItem *i);
static gchar *		gul_tbi_volume_to_string_impl		(GulTbItem *i);
static gboolean		gul_tbi_volume_is_unique_impl		(GulTbItem *i);
static GulTbItem *	gul_tbi_volume_clone_impl			(GulTbItem *i);
static void		gul_tbi_volume_parse_properties_impl	(GulTbItem *i, const gchar *props);
static void		gul_tbi_volume_add_to_bonobo_tb_impl	(GulTbItem *i, 
								 BonoboUIComponent *ui, 
								 const char *container_path,
								 guint index);

static gpointer gul_tb_item_class;

/**
 * Tbivolume object
 */

MAKE_GET_TYPE (gul_tbi_volume, "GulTbivolume", GulTbiVolume, gul_tbi_volume_class_init, 
	       gul_tbi_volume_init, GUL_TYPE_TB_ITEM);

static void
gul_tbi_volume_class_init (GulTbiVolumeClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = gul_tbi_volume_finalize_impl;
	
	GUL_TB_ITEM_CLASS (klass)->get_widget = gul_tbi_volume_get_widget_impl;
	GUL_TB_ITEM_CLASS (klass)->get_icon = gul_tbi_volume_get_icon_impl;
	GUL_TB_ITEM_CLASS (klass)->get_name_human = gul_tbi_volume_get_name_human_impl;
	GUL_TB_ITEM_CLASS (klass)->to_string = gul_tbi_volume_to_string_impl;
	GUL_TB_ITEM_CLASS (klass)->is_unique = gul_tbi_volume_is_unique_impl;
	GUL_TB_ITEM_CLASS (klass)->clone = gul_tbi_volume_clone_impl;
	GUL_TB_ITEM_CLASS (klass)->parse_properties = gul_tbi_volume_parse_properties_impl;
	GUL_TB_ITEM_CLASS (klass)->add_to_bonobo_tb = gul_tbi_volume_add_to_bonobo_tb_impl;
	
	gul_tb_item_class = g_type_class_peek_parent (klass);
}

static void 
gul_tbi_volume_init (GulTbiVolume *tb)
{
	GulTbiVolumePrivate *p = g_new0 (GulTbiVolumePrivate, 1);
	tb->priv = p;
}

GulTbiVolume *
gul_tbi_volume_new (void)
{
	GulTbiVolume *ret = g_object_new (GUL_TYPE_TBI_VOLUME, NULL);
	return ret;
}

static void
gul_tbi_volume_finalize_impl (GObject *o)
{
	GulTbiVolume *it = GUL_TBI_VOLUME (o);
	GulTbiVolumePrivate *p = it->priv;

	if (p->widget)
	{
		g_object_unref (p->widget);
	}

	g_free (p);
	
	DEBUG_MSG (("GulTbivolume finalized\n"));
	
	G_OBJECT_CLASS (gul_tb_item_class)->finalize (o);
}

static GtkWidget *
gul_tbi_volume_get_widget_impl (GulTbItem *i)
{
	GulTbiVolume *iz = GUL_TBI_VOLUME (i);
	GulTbiVolumePrivate *p = iz->priv;

	if (!p->widget)
	{
		/* The real widget is created in the shell */
		p->widget = gtk_hbox_new (FALSE, 0);
		g_object_ref (p->widget);
		gtk_object_sink (GTK_OBJECT (p->widget));
	}

	return p->widget;
}

static GdkPixbuf *
gul_tbi_volume_get_icon_impl (GulTbItem *i)
{
	/* need an icon for this */
	return NULL;
}

static gchar *
gul_tbi_volume_get_name_human_impl (GulTbItem *i)
{
	return g_strdup (_("Volume"));
}

static gchar *
gul_tbi_volume_to_string_impl (GulTbItem *i)
{
	/* if it had any properties, the string should include them */
	return g_strdup_printf ("%s=volume", i->id);
}

static gboolean
gul_tbi_volume_is_unique_impl (GulTbItem *i)
{
	return TRUE;
}

static GulTbItem *
gul_tbi_volume_clone_impl (GulTbItem *i)
{
	GulTbItem *ret = GUL_TB_ITEM (gul_tbi_volume_new ());
	
	gul_tb_item_set_id (ret, i->id);

	/* should copy properties too, if any */

	return ret;
}

static void
gul_tbi_volume_add_to_bonobo_tb_impl (GulTbItem *i, BonoboUIComponent *ui, 
				       const char *container_path, guint index)
{
	char *xml_string;
	char *path;
	BonoboControl *control;
	
	GtkWidget *w = gul_tb_item_get_widget (i);
	gtk_widget_show (w);

	xml_string = g_strdup_printf ("<control name=\"Volume%d\" "
				      "behavior=\"pack-end\"/>", index);
	path = g_strdup_printf ("%s/Volume%d", container_path, index);
	bonobo_ui_component_set (ui, container_path, xml_string, NULL);

	control = bonobo_control_new (w);
        bonobo_ui_component_object_set (ui, path, BONOBO_OBJREF (control), NULL);
        bonobo_object_unref (control);
        
        g_free (xml_string);
	g_free (path);
}

static void
gul_tbi_volume_parse_properties_impl (GulTbItem *it, const gchar *props)
{
	/* we have no properties */
}

