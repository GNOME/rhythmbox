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
#include <bonobo/bonobo-ui-toolbar-button-item.h>
#include <bonobo/bonobo-property-bag.h>
#include <gtk/gtkstock.h>
#include "gul-tbi-std-toolitem.h"
#include <string.h>

#define NOT_IMPLEMENTED g_warning ("not implemented: " G_STRLOC);
//#define DEBUG_MSG(x) g_print x
#define DEBUG_MSG(x)

/**
 * Private data
 */
struct _GulTbiStdToolitemPrivate 
{
	GtkWidget *widget;
	
	GulTbiStdToolitemItem item;
};


/**
 * Private functions, only availble from this file
 */
static void		gul_tbi_std_toolitem_class_init	(GulTbiStdToolitemClass *klass);
static void		gul_tbi_std_toolitem_init		(GulTbiStdToolitem *tb);
static void		gul_tbi_std_toolitem_finalize_impl (GObject *o);
static GtkWidget *	gul_tbi_std_toolitem_get_widget_impl (GulTbItem *i);
static GdkPixbuf *	gul_tbi_std_toolitem_get_icon_impl (GulTbItem *i);
static gchar *		gul_tbi_std_toolitem_get_name_human_impl (GulTbItem *i);
static gchar *		gul_tbi_std_toolitem_to_string_impl (GulTbItem *i);
static gboolean		gul_tbi_std_toolitem_is_unique_impl (GulTbItem *i);
static GulTbItem *	gul_tbi_std_toolitem_clone_impl	(GulTbItem *i);
static void		gul_tbi_std_toolitem_parse_properties_impl (GulTbItem *i, const gchar *props);
static void		gul_tbi_std_toolitem_add_to_bonobo_tb_impl (GulTbItem *i, 
								    BonoboUIComponent *ui, 
								    const char *container_path,
								    guint index);

static gpointer gul_tb_item_class;

/**
 * TbiStdToolitem object
 */

MAKE_GET_TYPE (gul_tbi_std_toolitem, "GulTbiStdToolitem", GulTbiStdToolitem, 
	       gul_tbi_std_toolitem_class_init, 
	       gul_tbi_std_toolitem_init, GUL_TYPE_TB_ITEM);

static void
gul_tbi_std_toolitem_class_init (GulTbiStdToolitemClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = gul_tbi_std_toolitem_finalize_impl;
	
	GUL_TB_ITEM_CLASS (klass)->get_widget = gul_tbi_std_toolitem_get_widget_impl;
	GUL_TB_ITEM_CLASS (klass)->get_icon = gul_tbi_std_toolitem_get_icon_impl;
	GUL_TB_ITEM_CLASS (klass)->get_name_human = gul_tbi_std_toolitem_get_name_human_impl;
	GUL_TB_ITEM_CLASS (klass)->to_string = gul_tbi_std_toolitem_to_string_impl;
	GUL_TB_ITEM_CLASS (klass)->is_unique = gul_tbi_std_toolitem_is_unique_impl;
	GUL_TB_ITEM_CLASS (klass)->clone = gul_tbi_std_toolitem_clone_impl;
	GUL_TB_ITEM_CLASS (klass)->parse_properties = gul_tbi_std_toolitem_parse_properties_impl;
	GUL_TB_ITEM_CLASS (klass)->add_to_bonobo_tb = gul_tbi_std_toolitem_add_to_bonobo_tb_impl;
	
	gul_tb_item_class = g_type_class_peek_parent (klass);
}

static void 
gul_tbi_std_toolitem_init (GulTbiStdToolitem *tb)
{
	GulTbiStdToolitemPrivate *p = g_new0 (GulTbiStdToolitemPrivate, 1);
	tb->priv = p;

	p->item = GUL_TBI_STD_TOOLITEM_PREVIOUS;
}

GulTbiStdToolitem *
gul_tbi_std_toolitem_new (void)
{
	GulTbiStdToolitem *ret = g_object_new (GUL_TYPE_TBI_STD_TOOLITEM, NULL);
	return ret;
}

static void
gul_tbi_std_toolitem_finalize_impl (GObject *o)
{
	GulTbiStdToolitem *it = GUL_TBI_STD_TOOLITEM (o);
	GulTbiStdToolitemPrivate *p = it->priv;

	if (p->widget)
	{
		g_object_unref (p->widget);
	}

	g_free (p);
	
	DEBUG_MSG (("GulTbiStdToolitem finalized\n"));
	
	G_OBJECT_CLASS (gul_tb_item_class)->finalize (o);
}

static GtkWidget *
gul_tbi_std_toolitem_get_widget_impl (GulTbItem *i)
{
	/* no widget avaible ... */
	return NULL;
}

static GdkPixbuf *
gul_tbi_std_toolitem_get_icon_impl (GulTbItem *i)
{
	GulTbiStdToolitemPrivate *p = GUL_TBI_STD_TOOLITEM (i)->priv;

	static GdkPixbuf *pb_prev = NULL;
	static GdkPixbuf *pb_play = NULL;
	static GdkPixbuf *pb_next = NULL;
	static GdkPixbuf *pb_shuffle = NULL;
	static GdkPixbuf *pb_repeat = NULL;

	if (!pb_prev)
	{
		/* what's the easier way? */
		GtkWidget *b = gtk_spin_button_new_with_range (0, 1, 0.5);
		pb_prev = gtk_widget_render_icon (b,
						  "rhythmbox-previous",
						  GTK_ICON_SIZE_SMALL_TOOLBAR,
						  NULL);
		pb_play = gtk_widget_render_icon (b,
						  "rhythmbox-play",
						  GTK_ICON_SIZE_SMALL_TOOLBAR,
						  NULL);
		pb_next = gtk_widget_render_icon (b,
						  "rhythmbox-next",
						  GTK_ICON_SIZE_SMALL_TOOLBAR,
						  NULL);
		pb_shuffle = gtk_widget_render_icon (b,
						     "rhythmbox-shuffle",
						     GTK_ICON_SIZE_SMALL_TOOLBAR,
						     NULL);
		pb_repeat = gtk_widget_render_icon (b,
						    "rhythmbox-repeat",
						    GTK_ICON_SIZE_SMALL_TOOLBAR,
						    NULL);

		gtk_widget_destroy (b);
	}

	switch (p->item)
	{
	case GUL_TBI_STD_TOOLITEM_PREVIOUS:
		return g_object_ref (pb_prev);
		break;
	case GUL_TBI_STD_TOOLITEM_PLAY:
		return g_object_ref (pb_play);
		break;
	case GUL_TBI_STD_TOOLITEM_NEXT:
		return g_object_ref (pb_next);
		break;
	case GUL_TBI_STD_TOOLITEM_SHUFFLE:
		return g_object_ref (pb_shuffle);
		break;
	case GUL_TBI_STD_TOOLITEM_REPEAT:
		return g_object_ref (pb_repeat);
		break;

	default:
		g_assert_not_reached ();
		return NULL;
	}
}

static gchar *
gul_tbi_std_toolitem_get_name_human_impl (GulTbItem *i)
{
	GulTbiStdToolitemPrivate *p = GUL_TBI_STD_TOOLITEM (i)->priv;
	const gchar *ret;

	switch (p->item)
	{
	case GUL_TBI_STD_TOOLITEM_PREVIOUS:
		ret = _("Previous");
		break;
	case GUL_TBI_STD_TOOLITEM_PLAY:
		ret = _("Play");
		break;
	case GUL_TBI_STD_TOOLITEM_NEXT:
		ret = _("Next");
		break;
	case GUL_TBI_STD_TOOLITEM_SHUFFLE:
		ret = _("Shuffle");
		break;
	case GUL_TBI_STD_TOOLITEM_REPEAT:
		ret = _("Repeat");
		break;

	default:
		g_assert_not_reached ();
		ret = "unknown";
	}

	return g_strdup (ret);
}

static gchar *
gul_tbi_std_toolitem_to_string_impl (GulTbItem *i)
{
	GulTbiStdToolitemPrivate *p = GUL_TBI_STD_TOOLITEM (i)->priv;

	/* if it had any properties, the string should include them */
	const char *sitem;

	switch (p->item)
	{
	case GUL_TBI_STD_TOOLITEM_PREVIOUS:
		sitem = "previous";
		break;
	case GUL_TBI_STD_TOOLITEM_PLAY:
		sitem = "play";
		break;
	case GUL_TBI_STD_TOOLITEM_NEXT:
		sitem = "next";
		break;
	case GUL_TBI_STD_TOOLITEM_SHUFFLE:
		sitem = "shuffle";
		break;
	case GUL_TBI_STD_TOOLITEM_REPEAT:
		sitem = "repeat";
		break;

	default:
		g_assert_not_reached ();
		sitem = "unknown";
	}

	return g_strdup_printf ("%s=std_toolitem(item=%s)", i->id, sitem);
}

static gboolean
gul_tbi_std_toolitem_is_unique_impl (GulTbItem *i)
{
	return TRUE;
}

static GulTbItem *
gul_tbi_std_toolitem_clone_impl (GulTbItem *i)
{
	GulTbiStdToolitemPrivate *p = GUL_TBI_STD_TOOLITEM (i)->priv;

	GulTbItem *ret = GUL_TB_ITEM (gul_tbi_std_toolitem_new ());
	
	gul_tb_item_set_id (ret, i->id);

	/* should copy properties too, if any */
	gul_tbi_std_toolitem_set_item (GUL_TBI_STD_TOOLITEM (ret), p->item);

	return ret;
}


static void
gul_tbi_std_toolitem_add_to_bonobo_tb_impl (GulTbItem *i, BonoboUIComponent *ui, 
					    const char *container_path, guint index)
{
	GulTbiStdToolitemPrivate *p = GUL_TBI_STD_TOOLITEM (i)->priv;
	gchar *xml_item;

	switch (p->item)
	{
	case GUL_TBI_STD_TOOLITEM_PREVIOUS:
		xml_item = g_strdup_printf
			("<toolitem name=\"Previous\" label=\"%s\" "
			 "verb=\"Previous\"/>", _("Previous"));
		break;
	case GUL_TBI_STD_TOOLITEM_PLAY:
		xml_item = g_strdup_printf
			("<toolitem name=\"Play\" label=\"%s\" " 
			 "verb=\"Play\"/>", _("Play"));
		break;
	case GUL_TBI_STD_TOOLITEM_NEXT:
		xml_item = g_strdup_printf
			("<toolitem name=\"Next\" label=\"%s\" "
			 "verb=\"Next\"/>", _("Next"));
		break;
	case GUL_TBI_STD_TOOLITEM_SHUFFLE:
		xml_item = g_strdup_printf
			("<toolitem name=\"Shuffle\" label=\"%s\" "
			 "verb=\"Shuffle\"/>", _("Shuffle")); 
		break;

	case GUL_TBI_STD_TOOLITEM_REPEAT:
		xml_item = g_strdup_printf
			("<toolitem name=\"Repeat\" label=\"%s\" "
			 "verb=\"Repeat\"/>", _("Repeat")); 
		break;
	default:
		g_assert_not_reached ();
		xml_item = g_strdup ("");
	}

	bonobo_ui_component_set (ui, container_path, xml_item, NULL);
	g_free (xml_item);
}

static void
gul_tbi_std_toolitem_parse_properties_impl (GulTbItem *it, const gchar *props)
{
	GulTbiStdToolitem *a = GUL_TBI_STD_TOOLITEM (it);

	/* yes, this is quite hacky, but works */

	/* we have one property */
	const gchar *item_prop;

	item_prop = strstr (props, "item=");
	if (item_prop)
	{
		item_prop += strlen ("item=");
		if (!strncmp (item_prop, "previous", 8))
		{
			gul_tbi_std_toolitem_set_item (a, GUL_TBI_STD_TOOLITEM_PREVIOUS);
		}
		else if (!strncmp (item_prop, "play", 4))
		{
			gul_tbi_std_toolitem_set_item (a, GUL_TBI_STD_TOOLITEM_PLAY);
		}
		else if (!strncmp (item_prop, "next", 4))
		{
			gul_tbi_std_toolitem_set_item (a, GUL_TBI_STD_TOOLITEM_NEXT);
		}
		else if (!strncmp (item_prop, "shuffle", 7))
		{
			gul_tbi_std_toolitem_set_item (a, GUL_TBI_STD_TOOLITEM_SHUFFLE);
		}
		else if (!strncmp (item_prop, "repeat", 6))
		{
			gul_tbi_std_toolitem_set_item (a, GUL_TBI_STD_TOOLITEM_REPEAT);
		}

	}
}

void
gul_tbi_std_toolitem_set_item (GulTbiStdToolitem *a, GulTbiStdToolitemItem i)
{
	GulTbiStdToolitemPrivate *p = a->priv;

	g_return_if_fail (i == GUL_TBI_STD_TOOLITEM_PREVIOUS 
			  || i == GUL_TBI_STD_TOOLITEM_PLAY
			  || i == GUL_TBI_STD_TOOLITEM_NEXT
			  || i == GUL_TBI_STD_TOOLITEM_SHUFFLE
			  || i == GUL_TBI_STD_TOOLITEM_REPEAT);

	p->item = i;
}

