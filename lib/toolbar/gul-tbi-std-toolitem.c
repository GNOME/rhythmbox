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
	static GdkPixbuf *pb_cut = NULL;
	static GdkPixbuf *pb_copy = NULL;
	static GdkPixbuf *pb_paste = NULL;
	static GdkPixbuf *pb_delete = NULL;
	static GdkPixbuf *pb_properties = NULL;
	static GdkPixbuf *pb_add_to_library = NULL;
	static GdkPixbuf *pb_new_group = NULL;

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
		pb_cut = gtk_widget_render_icon (b,
						 "gtk-cut",
						 GTK_ICON_SIZE_SMALL_TOOLBAR,
						 NULL);
		pb_copy = gtk_widget_render_icon (b,
						  "gtk-copy",
						  GTK_ICON_SIZE_SMALL_TOOLBAR,
						  NULL);
		pb_paste = gtk_widget_render_icon (b,
						   "gtk-paste",
						   GTK_ICON_SIZE_SMALL_TOOLBAR,
						   NULL);
		pb_delete = gtk_widget_render_icon (b,
						    "gtk-delete",
						    GTK_ICON_SIZE_SMALL_TOOLBAR,
						    NULL);
		pb_properties = gtk_widget_render_icon (b,
						        "gtk-properties",
						        GTK_ICON_SIZE_SMALL_TOOLBAR,
						        NULL);
		pb_add_to_library = gtk_widget_render_icon (b,
						            "gtk-open",
						            GTK_ICON_SIZE_SMALL_TOOLBAR,
						            NULL);
		pb_new_group = gtk_widget_render_icon (b,
						       "gtk-new",
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
	case GUL_TBI_STD_TOOLITEM_CUT:
		return g_object_ref (pb_cut);
		break;
	case GUL_TBI_STD_TOOLITEM_COPY:
		return g_object_ref (pb_copy);
		break;
	case GUL_TBI_STD_TOOLITEM_PASTE:
		return g_object_ref (pb_paste);
		break;
	case GUL_TBI_STD_TOOLITEM_PROPERTIES:
		return g_object_ref (pb_properties);
		break;
	case GUL_TBI_STD_TOOLITEM_ADD_TO_LIBRARY:
		return g_object_ref (pb_add_to_library);
		break;
	case GUL_TBI_STD_TOOLITEM_NEW_GROUP:
		return g_object_ref (pb_new_group);
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
	case GUL_TBI_STD_TOOLITEM_CUT:
		ret = _("Cut");
		break;
	case GUL_TBI_STD_TOOLITEM_COPY:
		ret = _("Copy");
		break;
	case GUL_TBI_STD_TOOLITEM_PASTE:
		ret = _("Paste");
		break;
	case GUL_TBI_STD_TOOLITEM_PROPERTIES:
		ret = _("Properties");
		break;
	case GUL_TBI_STD_TOOLITEM_ADD_TO_LIBRARY:
		ret = _("Add to Library");
		break;
	case GUL_TBI_STD_TOOLITEM_NEW_GROUP:
		ret = _("New Group");
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
	case GUL_TBI_STD_TOOLITEM_CUT:
		sitem = "cut";
		break;
	case GUL_TBI_STD_TOOLITEM_COPY:
		sitem = "copy";
		break;
	case GUL_TBI_STD_TOOLITEM_PASTE:
		sitem = "paste";
		break;
	case GUL_TBI_STD_TOOLITEM_PROPERTIES:
		sitem = "properties";
		break;
	case GUL_TBI_STD_TOOLITEM_ADD_TO_LIBRARY:
		sitem = "add_to_library";
		break;
	case GUL_TBI_STD_TOOLITEM_NEW_GROUP:
		sitem = "new_group";
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
	case GUL_TBI_STD_TOOLITEM_CUT:
		xml_item = g_strdup_printf
			("<toolitem name=\"Cut\" label=\"%s\" "
			 "verb=\"Cut\"/>", _("Cut")); 
		break;
	case GUL_TBI_STD_TOOLITEM_COPY:
		xml_item = g_strdup_printf
			("<toolitem name=\"Copy\" label=\"%s\" "
			 "verb=\"Copy\"/>", _("Copy")); 
		break;
	case GUL_TBI_STD_TOOLITEM_PASTE:
		xml_item = g_strdup_printf
			("<toolitem name=\"Paste\" label=\"%s\" "
			 "verb=\"Paste\"/>", _("Paste")); 
		break;
	case GUL_TBI_STD_TOOLITEM_PROPERTIES:
		xml_item = g_strdup_printf
			("<toolitem name=\"SongInfo\" label=\"%s\" "
			 "verb=\"SongInfo\"/>", _("Properties")); 
		break;
	case GUL_TBI_STD_TOOLITEM_ADD_TO_LIBRARY:
		xml_item = g_strdup_printf
			("<toolitem name=\"AddToLibrary\" label=\"%s\" "
			 "verb=\"AddToLibrary\"/>", _("Add to Library")); 
		break;
	case GUL_TBI_STD_TOOLITEM_NEW_GROUP:
		xml_item = g_strdup_printf
			("<toolitem name=\"NewGroup\" label=\"%s\" "
			 "verb=\"NewGroup\"/>", _("New Group")); 
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
		else if (!strncmp (item_prop, "cut", 3))
		{
			gul_tbi_std_toolitem_set_item (a, GUL_TBI_STD_TOOLITEM_CUT);
		}
		else if (!strncmp (item_prop, "copy", 4))
		{
			gul_tbi_std_toolitem_set_item (a, GUL_TBI_STD_TOOLITEM_COPY);
		}
		else if (!strncmp (item_prop, "paste", 5))
		{
			gul_tbi_std_toolitem_set_item (a, GUL_TBI_STD_TOOLITEM_PASTE);
		}
		else if (!strncmp (item_prop, "properties", 10))
		{
			gul_tbi_std_toolitem_set_item (a, GUL_TBI_STD_TOOLITEM_PROPERTIES);
		}
		else if (!strncmp (item_prop, "add_to_library", 14))
		{
			gul_tbi_std_toolitem_set_item (a, GUL_TBI_STD_TOOLITEM_ADD_TO_LIBRARY);
		}
		else if (!strncmp (item_prop, "new_group", 9))
		{
			gul_tbi_std_toolitem_set_item (a, GUL_TBI_STD_TOOLITEM_NEW_GROUP);
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
			  || i == GUL_TBI_STD_TOOLITEM_REPEAT
			  || i == GUL_TBI_STD_TOOLITEM_CUT
			  || i == GUL_TBI_STD_TOOLITEM_COPY
			  || i == GUL_TBI_STD_TOOLITEM_PASTE
			  || i == GUL_TBI_STD_TOOLITEM_PROPERTIES
			  || i == GUL_TBI_STD_TOOLITEM_ADD_TO_LIBRARY
			  || i == GUL_TBI_STD_TOOLITEM_NEW_GROUP);

	p->item = i;
}

