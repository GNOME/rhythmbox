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
#include <string.h>
#include "gul-gobject-misc.h"
#include "gul-toolbar.h"
#include "gul-toolbar-item-factory.h"
#include "eel-gconf-extensions.h"

#define NOT_IMPLEMENTED g_warning ("not implemented: " G_STRLOC);
//#define DEBUG_MSG(x) g_print x
#define DEBUG_MSG(x)

/**
 * Private data
 */
struct _GulToolbarPrivate 
{
	GSList *items;
	guint gconf_notification_id;

	gboolean check_unique;
	gboolean fixed_order;
	GSList *order; /* list of ids */
};

/**
 * Private functions, only availble from this file
 */
static void		gul_toolbar_class_init			(GulToolbarClass *klass);
static void		gul_toolbar_init			(GulToolbar *tb);
static void		gul_toolbar_finalize_impl		(GObject *o);
static void		gul_toolbar_listen_to_gconf_cb		(GConfClient* client,
								 guint cnxn_id,
								 GConfEntry *entry,
								 gpointer user_data);
static void		gul_toolbar_update_order		(GulToolbar *tb);


static gpointer g_object_class;

/* signals enums and ids */
enum GulToolbarSignalsEnum {
	GUL_TOOLBAR_CHANGED,
	GUL_TOOLBAR_LAST_SIGNAL
};
static gint GulToolbarSignals[GUL_TOOLBAR_LAST_SIGNAL];

/**
 * Toolbar object
 */

MAKE_GET_TYPE (gul_toolbar, "GulToolbar", GulToolbar, gul_toolbar_class_init, 
	       gul_toolbar_init, G_TYPE_OBJECT);

static void
gul_toolbar_class_init (GulToolbarClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = gul_toolbar_finalize_impl;
	
	GulToolbarSignals[GUL_TOOLBAR_CHANGED] = g_signal_new (
		"changed", G_OBJECT_CLASS_TYPE (klass),  
		G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST | G_SIGNAL_RUN_CLEANUP,
                G_STRUCT_OFFSET (GulToolbarClass, changed), 
		NULL, NULL, 
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
	
	g_object_class = g_type_class_peek_parent (klass);
}

static void 
gul_toolbar_init (GulToolbar *tb)
{
	GulToolbarPrivate *p = g_new0 (GulToolbarPrivate, 1);
	tb->priv = p;

	p->check_unique = TRUE;
}

static void
gul_toolbar_finalize_impl (GObject *o)
{
	GulToolbar *tb = GUL_TOOLBAR (o);
	GulToolbarPrivate *p = tb->priv;
	
	g_slist_foreach (p->items, (GFunc) g_object_unref, NULL);
	g_slist_free (p->items);

	if (p->gconf_notification_id)
	{
		eel_gconf_notification_remove (p->gconf_notification_id);
	}

	g_slist_foreach (p->order, (GFunc) g_free, NULL);
	g_slist_free (p->order);

	g_free (p);
	
	DEBUG_MSG (("GulToolbar finalized\n"));
	
	G_OBJECT_CLASS (g_object_class)->finalize (o);
}


GulToolbar *
gul_toolbar_new (void)
{
	GulToolbar *ret = g_object_new (GUL_TYPE_TOOLBAR, NULL);
	return ret;
}

gboolean
gul_toolbar_parse (GulToolbar *tb, const gchar *cfg)
{
	GulToolbarPrivate *p = tb->priv;
	GSList *list = NULL;
	gchar **items;
	int i;

	g_return_val_if_fail (GUL_IS_TOOLBAR (tb), FALSE);
	g_return_val_if_fail (cfg != NULL, FALSE);

	items = g_strsplit (cfg, ";", 0);
	if (!items) return FALSE;

	for (i = 0; items[i]; ++i)
	{
		if (items[i][0])
		{
			GulTbItem *it = gul_toolbar_item_create_from_string (items[i]);
			
			if (!it)
			{
				/* FIXME: this leaks everything... */
				return FALSE;
			}
			
			list = g_slist_prepend (list, it);
		}
	}
	
	g_strfreev (items);
	
	g_slist_foreach (p->items, (GFunc) g_object_unref, NULL);
	g_slist_free (p->items);
	p->items = g_slist_reverse (list);

	if (p->fixed_order)
	{
		gul_toolbar_update_order (tb);
	}

	g_signal_emit (tb, GulToolbarSignals[GUL_TOOLBAR_CHANGED], 0);

	return TRUE;
}

gchar *
gul_toolbar_to_string (GulToolbar *tb)
{
	GulToolbarPrivate *p = tb->priv;
	gchar *ret;
	GString *str = g_string_new ("");
	GSList *li;

	for (li = p->items; li; li = li->next)
	{
		GulTbItem *it = li->data;
		gchar *s = gul_tb_item_to_string (it);
		g_string_append (str, s);
		if (li->next)
		{
			g_string_append (str, ";");
		}
		g_free (s);
	}
	
	ret = str->str;
	g_string_free (str, FALSE);
	return ret;
}

static void
gul_toolbar_listen_to_gconf_cb (GConfClient* client,
				guint cnxn_id,
				GConfEntry *entry,
				gpointer user_data)
{
	GulToolbar *tb = user_data;
	GConfValue *value;
	const char *str;
	
	g_return_if_fail (GUL_IS_TOOLBAR (tb));
	
	value = gconf_entry_get_value (entry);
	str = gconf_value_get_string (value);

	DEBUG_MSG (("in gul_toolbar_listen_to_gconf_cb\n"));

	gul_toolbar_parse (tb, str);
}

/**
 * Listen to changes in the toolbar configuration. Returns TRUE if the
 * current configuration is valid.
 */
gboolean
gul_toolbar_listen_to_gconf (GulToolbar *tb, const gchar *gconf_key)
{
	GulToolbarPrivate *p = tb->priv;
	gchar *s;
	gboolean ret = FALSE;

	if (p->gconf_notification_id)
	{
		eel_gconf_notification_remove (p->gconf_notification_id);
	}

	s = eel_gconf_get_string (gconf_key);
	if (s)
	{
		ret = gul_toolbar_parse (tb, s);
		g_free (s);
	}

	p->gconf_notification_id = eel_gconf_notification_add (gconf_key, 
							       gul_toolbar_listen_to_gconf_cb,
							       tb);

	DEBUG_MSG (("listening to %s, %d (FIXME: does not seem to work)\n", 
		    gconf_key, p->gconf_notification_id));

	return ret;	
}

GulTbItem *
gul_toolbar_get_item_by_id (GulToolbar *tb, const gchar *id)
{
	GulToolbarPrivate *p = tb->priv;
	GSList *li;

	for (li = p->items; li; li = li->next)
	{
		GulTbItem *i = li->data;
		if (i->id && !strcmp (i->id, id))
		{
			return i;
		}
	}
	return NULL;
}

const GSList *
gul_toolbar_get_item_list (GulToolbar *tb)
{
	GulToolbarPrivate *p = tb->priv;
	return p->items;
}

void
gul_toolbar_add_item (GulToolbar *tb, GulTbItem *it, gint index)
{
	GulToolbarPrivate *p = tb->priv;
	GulTbItem *old_it;

	g_return_if_fail (g_slist_find (p->items, it) == NULL);

	if (p->check_unique && gul_tb_item_is_unique (it)
	    && (old_it = gul_toolbar_get_item_by_id (tb, it->id)) != NULL)
	{
		GSList *old_it_link;
		if (p->fixed_order)
		{
			return;
		}
		old_it_link = g_slist_find (p->items, old_it);
		p->items = g_slist_insert (p->items, old_it, index);
		p->items = g_slist_delete_link (p->items, old_it_link);
		
	}
	else
	{
		if (p->fixed_order)
		{
			GSList *li;
			if (gul_toolbar_get_item_by_id (tb, it->id) != NULL)
			{
				return;
			}
			index = 0;
			for (li = p->order; li && strcmp (li->data, it->id); li = li->next)
			{
				if (gul_toolbar_get_item_by_id (tb, li->data) != NULL)
				{
					++index;
				}
			}
		}
		
		p->items = g_slist_insert (p->items, it, index);
		g_object_ref (it);
	}
	g_signal_emit (tb, GulToolbarSignals[GUL_TOOLBAR_CHANGED], 0);
}

void
gul_toolbar_remove_item (GulToolbar *tb, GulTbItem *it)
{
	GulToolbarPrivate *p = tb->priv;
	
	g_return_if_fail (g_slist_find (p->items, it) != NULL);

	p->items = g_slist_remove (p->items, it);

	g_signal_emit (tb, GulToolbarSignals[GUL_TOOLBAR_CHANGED], 0);

	g_object_unref (it);
}

void
gul_toolbar_set_fixed_order (GulToolbar *tb, gboolean value)
{
	GulToolbarPrivate *p = tb->priv;
	p->fixed_order = value;

	if (value)
	{
		gul_toolbar_update_order (tb);
	}
}

void
gul_toolbar_set_check_unique (GulToolbar *tb, gboolean value)
{
	GulToolbarPrivate *p = tb->priv;
	p->check_unique = value;

	/* maybe it should remove duplicated items now, if any */
}

gboolean
gul_toolbar_get_check_unique (GulToolbar *tb)
{
	GulToolbarPrivate *p = tb->priv;
	return p->check_unique;
}

static void
gul_toolbar_update_order (GulToolbar *tb)
{
	GulToolbarPrivate *p = tb->priv;
	GSList *li;
	GSList *lj;
	GSList *new_order = NULL;

	lj = p->order;
	for (li = p->items; li; li = li->next)
	{
		GulTbItem *i = li->data;
		const gchar *id = i->id;

		if (g_slist_find_custom (lj, id, (GCompareFunc) strcmp))
		{
			for ( ; lj && strcmp (lj->data, id); lj = lj->next)
			{
				if (gul_toolbar_get_item_by_id (tb, lj->data) == NULL)
				{
					new_order = g_slist_prepend (new_order, g_strdup (lj->data));
				}
			}
		}

		new_order = g_slist_prepend (new_order, g_strdup (id));

	}

	for ( ; lj; lj = lj->next)
	{
		if (gul_toolbar_get_item_by_id (tb, lj->data) == NULL)
		{
			new_order = g_slist_prepend (new_order, g_strdup (lj->data));
		}
	}

	g_slist_foreach (p->order, (GFunc) g_free, NULL);
	g_slist_free (p->order);

	p->order = g_slist_reverse (new_order);

#ifdef DEBUG_ORDER
	DEBUG_MSG (("New order:\n"));
	for (lj = p->order; lj; lj = lj->next)
	{
		DEBUG_MSG (("%s\n", (char *) lj->data));
	}
#endif
}

