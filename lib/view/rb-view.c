/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#include <bonobo/bonobo-ui-util.h>

#include "rb-view.h"

static void rb_view_class_init (RBViewClass *klass);
static void rb_view_init (RBView *view);
static void rb_view_finalize (GObject *object);
static void rb_view_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rb_view_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);

struct RBViewPrivate
{
	char *ui_file;
	char *ui_name;

	RBSidebarButton *button;

	BonoboUIComponent *component;
};

enum
{
	PROP_0,
	PROP_UI_FILE,
	PROP_UI_NAME,
	PROP_SIDEBAR_BUTTON,
	PROP_COMPONENT
};

static GObjectClass *parent_class = NULL;

GType
rb_view_get_type (void)
{
	static GType rb_view_type = 0;

	if (rb_view_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBViewClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_view_class_init,
			NULL,
			NULL,
			sizeof (RBView),
			0,
			(GInstanceInitFunc) rb_view_init
		};

		rb_view_type = g_type_register_static (GTK_TYPE_FRAME,
						       "RBView",
						       &our_info, 0);
	}

	return rb_view_type;
}

static void
rb_view_class_init (RBViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_view_finalize;

	object_class->set_property = rb_view_set_property;
	object_class->get_property = rb_view_get_property;

	g_object_class_install_property (object_class,
					 PROP_UI_FILE,
					 g_param_spec_string ("ui-file",
							      "UI file",
							      "Bonobo UI file",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_UI_NAME,
					 g_param_spec_string ("ui-name",
							      "UI name",
							      "Bonobo UI name",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_SIDEBAR_BUTTON,
					 g_param_spec_object ("sidebar-button",
							      "Sidebar button",
							      "Sidebar button object",
							      RB_TYPE_SIDEBAR_BUTTON,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_COMPONENT,
					 g_param_spec_object ("component",
							      "BonoboUIComponent",
							      "BonoboUIComponent object",
							      BONOBO_TYPE_UI_COMPONENT,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_view_init (RBView *view)
{
	view->priv = g_new0 (RBViewPrivate, 1);

	gtk_frame_set_shadow_type (GTK_FRAME (view), GTK_SHADOW_NONE);
}

static void
rb_view_finalize (GObject *object)
{
	RBView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_VIEW (object));

	view = RB_VIEW (object);

	g_return_if_fail (view->priv != NULL);
	
	g_free (view->priv->ui_file);
	g_free (view->priv->ui_name);

	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_view_set_property (GObject *object,
		      guint prop_id,
		      const GValue *value,
		      GParamSpec *pspec)
{
	RBView *view = RB_VIEW (object);

	switch (prop_id)
	{
	case PROP_UI_FILE:
		view->priv->ui_file = g_strdup (g_value_get_string (value));
		break;
	case PROP_UI_NAME:
		view->priv->ui_name = g_strdup (g_value_get_string (value));
		break;
	case PROP_SIDEBAR_BUTTON:
		view->priv->button = g_value_get_object (value);
		break;
	case PROP_COMPONENT:
		view->priv->component = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_view_get_property (GObject *object,
		      guint prop_id,
		      GValue *value,
		      GParamSpec *pspec)
{
	RBView *view = RB_VIEW (object);

	switch (prop_id)
	{
	case PROP_UI_FILE:
		g_value_set_string (value, view->priv->ui_file);
		break;
	case PROP_UI_NAME:
		g_value_set_string (value, view->priv->ui_name);
		break;
	case PROP_SIDEBAR_BUTTON:
		g_value_set_object (value, view->priv->button);
		break;
	case PROP_COMPONENT:
		g_value_set_object (value, view->priv->component);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_view_merge_menus (RBView *view)
{
	bonobo_ui_util_set_ui (view->priv->component,
			       DATADIR,
			       view->priv->ui_file,
			       view->priv->ui_name,
			       NULL);
}

RBSidebarButton *
rb_view_get_sidebar_button (RBView *view)
{
	RBSidebarButton *button;

	g_return_val_if_fail (RB_IS_VIEW (view), NULL);

	g_object_get (G_OBJECT (view),
		      "sidebar-button", &button,
		      NULL);

	return button;
}
