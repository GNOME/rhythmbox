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
#include "gul-toolbar-bonobo-view.h"

#define NOT_IMPLEMENTED g_warning ("not implemented: " G_STRLOC);
//#define DEBUG_MSG(x) g_print x
#define DEBUG_MSG(x)

/**
 * Private data
 */
struct _GulTbBonoboViewPrivate 
{
	GulToolbar *tb;
	BonoboUIComponent *ui;
	gchar *path;
};

/**
 * Private functions, only availble from this file
 */
static void		gul_tb_bonobo_view_class_init		(GulTbBonoboViewClass *klass);
static void		gul_tb_bonobo_view_init			(GulTbBonoboView *tb);
static void		gul_tb_bonobo_view_finalize_impl	(GObject *o);
static void		gul_tb_bonobo_view_rebuild		(GulTbBonoboView *tbv);
static void		gul_tb_bonobo_view_tb_changed		(GulToolbar *tb, GulTbBonoboView *tbv);

static gpointer g_object_class;

/**
 * TbBonoboView object
 */

MAKE_GET_TYPE (gul_tb_bonobo_view, "GulTbBonoboView", GulTbBonoboView, gul_tb_bonobo_view_class_init, 
	       gul_tb_bonobo_view_init, G_TYPE_OBJECT);

static void
gul_tb_bonobo_view_class_init (GulTbBonoboViewClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = gul_tb_bonobo_view_finalize_impl;
	
	
	g_object_class = g_type_class_peek_parent (klass);
}

static void 
gul_tb_bonobo_view_init (GulTbBonoboView *tb)
{
	GulTbBonoboViewPrivate *p = g_new0 (GulTbBonoboViewPrivate, 1);
	tb->priv = p;
}

static void
gul_tb_bonobo_view_finalize_impl (GObject *o)
{
	GulTbBonoboView *tbv = GUL_TB_BONOBO_VIEW (o);
	GulTbBonoboViewPrivate *p = tbv->priv;
	
	if (p->tb) 
	{
		g_signal_handlers_disconnect_matched (p->tb, G_SIGNAL_MATCH_DATA, 0, 0, 
						      NULL, NULL, tbv);
		g_object_unref (p->tb);
	}
	if (p->ui) 
	{
		g_object_unref (p->ui);
	}
	if (p->path) 
	{
		g_free (p->path);
	}

	g_free (p);
	
	DEBUG_MSG (("GulTbBonoboView finalized\n"));
	
	G_OBJECT_CLASS (g_object_class)->finalize (o);
}

GulTbBonoboView *
gul_tb_bonobo_view_new (void)
{
	GulTbBonoboView *ret = g_object_new (GUL_TYPE_TB_BONOBO_VIEW, NULL);
	return ret;
}

void
gul_tb_bonobo_view_set_toolbar (GulTbBonoboView *tbv, GulToolbar *tb)
{
	GulTbBonoboViewPrivate *p = tbv->priv;

	if (p->tb)
	{
		g_signal_handlers_disconnect_matched (p->tb, G_SIGNAL_MATCH_DATA, 0, 0, 
						      NULL, NULL, tbv);
		g_object_unref (p->tb);
	}

	p->tb = g_object_ref (tb);
	g_signal_connect (p->tb, "changed", G_CALLBACK (gul_tb_bonobo_view_tb_changed), tbv);

	if (p->ui) 
	{
		gul_tb_bonobo_view_rebuild (tbv);
	}
}

static void
gul_tb_bonobo_view_tb_changed (GulToolbar *tb, GulTbBonoboView *tbv)
{
	GulTbBonoboViewPrivate *p = tbv->priv;
	if (p->ui) 
	{
		gul_tb_bonobo_view_rebuild (tbv);
	}
}

void
gul_tb_bonobo_view_set_path (GulTbBonoboView *tbv, 
			     BonoboUIComponent *ui,
			     const gchar *path)
{
	GulTbBonoboViewPrivate *p = tbv->priv;

	if (p->ui) 
	{
		g_object_unref (p->ui);
	}

	if (p->path)
	{
		g_free (p->path);
	}

	p->ui = g_object_ref (ui);
	p->path = g_strdup (path);

	if (p->tb) 
	{
		gul_tb_bonobo_view_rebuild (tbv);
	}
}

static void
gul_bonobo_clear_path (BonoboUIComponent *uic,
                       const gchar *path)
{
        if (bonobo_ui_component_path_exists  (uic, path, NULL))
        {
                char *remove_wildcard = g_strdup_printf ("%s/*", path);
                bonobo_ui_component_rm (uic, remove_wildcard, NULL);
                g_free (remove_wildcard);
        }
}

static void
gul_tb_bonobo_view_rebuild (GulTbBonoboView *tbv)
{
	GulTbBonoboViewPrivate *p = tbv->priv;
	GSList *items;
	GSList *li;
	uint index = 0;

	g_return_if_fail (GUL_IS_TOOLBAR (p->tb));
	g_return_if_fail (BONOBO_IS_UI_COMPONENT (p->ui));
	g_return_if_fail (p->path);

	DEBUG_MSG (("Rebuilding GulTbBonoboView\n"));

	gul_bonobo_clear_path (p->ui, p->path);

	items = (GSList *) gul_toolbar_get_item_list (p->tb);
	for (li = items; li; li = li->next)
	{
		gul_tb_item_add_to_bonobo_tb (li->data, p->ui, p->path, index++);
	}

	DEBUG_MSG (("Rebuilt GulTbBonoboView\n"));
}

