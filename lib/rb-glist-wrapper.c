/* 
 *  Copyright (C) 2002 Colin Walters <walters@gnu.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  $Id$
 */

#include "rb-glist-wrapper.h"

#include <glib/glist.h>

static void rb_glist_wrapper_class_init (RBGListWrapperClass *klass);
static void rb_glist_wrapper_init (RBGListWrapper *view);
static void rb_glist_wrapper_finalize (GObject *object);
static void rb_glist_wrapper_set_property (GObject *object,
					   guint prop_id,
					   const GValue *value,
					   GParamSpec *pspec);
static void rb_glist_wrapper_get_property (GObject *object,
					   guint prop_id,
					   GValue *value,
					   GParamSpec *pspec);

struct _RBGListWrapperPrivate
{
	GList *value;
};

static GObjectClass *parent_class = NULL;

enum
{
	PROP_NONE,
	PROP_LIST,
};

GType
rb_glist_wrapper_get_type (void)
{
	static GType rb_glist_wrapper_type = 0;

	if (rb_glist_wrapper_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBGListWrapperClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_glist_wrapper_class_init,
			NULL,
			NULL,
			sizeof (RBGListWrapper),
			0,
			(GInstanceInitFunc) rb_glist_wrapper_init
		};
		
		rb_glist_wrapper_type = g_type_register_static (G_TYPE_OBJECT,
								"RBGListWrapper",
								&our_info, 0);
		
	}

	return rb_glist_wrapper_type;
}

static void
rb_glist_wrapper_class_init (RBGListWrapperClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_peek_parent (klass);
	
	object_class->finalize = rb_glist_wrapper_finalize;
	object_class->set_property = rb_glist_wrapper_set_property;
	object_class->get_property = rb_glist_wrapper_get_property;

	g_object_class_install_property (object_class,
					 PROP_LIST,
					 g_param_spec_pointer ("list",
							       "GList",
							       "Wrapped GList value",
							       G_PARAM_READWRITE));
}

static void
rb_glist_wrapper_init (RBGListWrapper *listwrapper)
{
	listwrapper->priv = g_new0(RBGListWrapperPrivate, 1);
	listwrapper->priv->value = NULL;
}

static void
rb_glist_wrapper_finalize (GObject *object)
{
	RBGListWrapper *listwrapper;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_GLIST_WRAPPER (object));

	listwrapper = RB_GLIST_WRAPPER (object);

	g_return_if_fail (listwrapper->priv != NULL);

	g_list_free (listwrapper->priv->value);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_glist_wrapper_set_property (GObject *object,
			       guint prop_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	RBGListWrapper *listwrapper = RB_GLIST_WRAPPER (object);

	switch (prop_id)
	{
	case PROP_LIST:
	{
		rb_glist_wrapper_set_list (listwrapper, (GList *) g_value_get_pointer (value));
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_glist_wrapper_get_property (GObject *object,
			       guint prop_id,
			       GValue *value,
			       GParamSpec *pspec)
{
	RBGListWrapper *listwrapper = RB_GLIST_WRAPPER (object);

	switch (prop_id)
	{
	case PROP_LIST:
	{
		g_value_set_pointer (value, rb_glist_wrapper_get_list (listwrapper));
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GList *
rb_glist_wrapper_get_list (RBGListWrapper *listwrapper)
{
	return listwrapper->priv->value;
}

void
rb_glist_wrapper_set_list (RBGListWrapper *listwrapper, GList *val)
{
	listwrapper->priv->value = val;
}

