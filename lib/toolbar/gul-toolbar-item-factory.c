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

#include "gul-toolbar-item-factory.h"
#include <string.h>

#include "gul-tbi-separator.h"
#include "gul-tbi-std-toolitem.h"
#include "gul-tbi-volume.h"

#define NOT_IMPLEMENTED g_warning ("not implemented: " G_STRLOC);
//#define DEBUG_MSG(x) g_print x
#define DEBUG_MSG(x)

typedef GulTbItem *(GulTbItemConstructor) (void);

typedef struct 
{
	const char *type_name;
	GulTbItemConstructor *constructor;
} GulTbItemTypeInfo;

static GulTbItemTypeInfo gul_tb_item_known_types[] = 
{
	{ "std_toolitem",		(GulTbItemConstructor *) gul_tbi_std_toolitem_new },
	{ "separator", 			(GulTbItemConstructor *) gul_tbi_separator_new },
	{ "volume", 			(GulTbItemConstructor *) gul_tbi_volume_new },
	{ NULL, 			NULL }
}; 

GulTbItem *
gul_toolbar_item_create_from_string (const gchar *str)
{
	GulTbItem *ret = NULL;
	gchar *type;
	gchar *props;
	gchar *id;
	const gchar *rest;
	const gchar *lpar;
	const gchar *rpar;
	const gchar *eq;
	int i;
	
	rest = str;

	eq = strchr (rest, '=');
	if (eq)
	{
		id = g_strndup (rest, eq - rest);
		rest = eq + 1;
	}
	else
	{
		id = NULL;
	}

	lpar = strchr (rest, '(');
	if (lpar)
	{
		type = g_strndup (rest, lpar - rest);
		rest = lpar + 1;
		
		rpar = strchr (rest, ')');
		if (rpar)
		{
			props = g_strndup (rest, rpar - rest);
			rest = rpar + 1;
		}
		else
		{
			props = g_strdup (rest);
		}
	}
	else
	{
		type = g_strdup (rest);
		props = NULL;
	}

	DEBUG_MSG (("gul_toolbar_item_create_from_string id=%s type=%s props=%s\n", id, type, props));

	for (i = 0; gul_tb_item_known_types[i].type_name; ++i)
	{
		if (!strcmp (type, gul_tb_item_known_types[i].type_name))
		{
			ret = gul_tb_item_known_types[i].constructor ();
			if (id)
			{
				gul_tb_item_set_id (ret, id);
			}
			if (props)
			{
				gul_tb_item_parse_properties (ret, props);	
			}
		}
	}

	if (!ret)
	{
		g_warning ("Error creating toolbar item of type %s", type);
	}

	if (id)
	{
		g_free (id);
	}
	if (type)
	{
		g_free (type);
	}
	if (props)
	{
		g_free (props);
	}

	return ret;
}

GSList *
gul_toolbar_list_item_types (void)
{
	int i;
	GSList *ret = NULL;
	for (i = 0; gul_tb_item_known_types[i].type_name; ++i)
	{
		ret = g_slist_prepend (ret,
				       (gchar *) gul_tb_item_known_types[i].type_name);
	}
	return ret;
}

