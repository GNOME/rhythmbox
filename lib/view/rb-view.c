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

#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-ui-container.h>
#include <bonobo/bonobo-ui-util.h>

#include "rb-view.h"
#include "rb-bonobo-helpers.h"

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
static void set_sensitive_foreach_func (char *key,
			                gpointer value,
			                RBView *view);
static void set_active_foreach_func (char *key,
			             gpointer value,
			             RBView *view);
static void get_sensitive_foreach_func (char *key,
			                gpointer value,
			                RBView *view);
static void get_active_foreach_func (char *key,
			             gpointer value,
			             RBView *view);
static void sidebar_button_deleted_cb (RBSidebarButton *button,
			               RBView *view);

struct RBViewPrivate
{
	char *ui_file;
	char *ui_name;

	RBSidebarButton *button;

	BonoboUIContainer *container;
	BonoboUIComponent *component;

	BonoboUIVerb *verbs;
	RBBonoboUIListener *listeners;

	GHashTable *sensitive_paths;
	GHashTable *active_paths;
};

enum
{
	PROP_0,
	PROP_UI_FILE,
	PROP_UI_NAME,
	PROP_SIDEBAR_BUTTON,
	PROP_CONTAINER,
	PROP_VERBS,
	PROP_LISTENERS
};

enum
{
	DELETED,
	LAST_SIGNAL
};

static guint rb_view_signals[LAST_SIGNAL] = { 0 };

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

		rb_view_type = g_type_register_static (GTK_TYPE_HBOX,
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
					 PROP_CONTAINER,
					 g_param_spec_object ("container",
							      "BonoboUIContainer",
							      "BonoboUIContainer object",
							      BONOBO_TYPE_UI_CONTAINER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_VERBS,
					 g_param_spec_pointer ("verbs",
							       "BonoboUI verb list",
							       "BonoboUI verb list",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_LISTENERS,
					 g_param_spec_pointer ("listeners",
							       "BonoboUI listener list",
							       "BonoboUI listener list",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	rb_view_signals[DELETED] =
		g_signal_new ("deleted",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBViewClass, deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static void
rb_view_init (RBView *view)
{
	view->priv = g_new0 (RBViewPrivate, 1);

	view->priv->active_paths = g_hash_table_new_full (g_str_hash,
							  g_str_equal,
							  (GDestroyNotify) g_free,
							  NULL);
	view->priv->sensitive_paths = g_hash_table_new_full (g_str_hash,
							     g_str_equal,
							     (GDestroyNotify) g_free,
							     NULL);
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

	g_hash_table_destroy (view->priv->active_paths);
	g_hash_table_destroy (view->priv->sensitive_paths);

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
		if (view->priv->button != NULL)
		{
			g_signal_handlers_disconnect_by_func (G_OBJECT (view->priv->button),
							      G_CALLBACK (sidebar_button_deleted_cb),
							      view);
		}
		
		view->priv->button = g_value_get_object (value);

		g_signal_connect_object (G_OBJECT (view->priv->button),
				         "deleted",
				         G_CALLBACK (sidebar_button_deleted_cb),
				         G_OBJECT (view),
					 0);
		break;
	case PROP_CONTAINER:
		view->priv->container = g_value_get_object (value);
		break;
	case PROP_VERBS:
		view->priv->verbs = g_value_get_pointer (value);
		break;
	case PROP_LISTENERS:
		view->priv->listeners = g_value_get_pointer (value);
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
	case PROP_CONTAINER:
		g_value_set_object (value, view->priv->container);
		break;
	case PROP_VERBS:
		g_value_set_pointer (value, view->priv->verbs);
		break;
	case PROP_LISTENERS:
		g_value_set_pointer (value, view->priv->listeners);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_view_merge_ui (RBView *view)
{
	view->priv->component = bonobo_ui_component_new (view->priv->ui_name);

	bonobo_ui_component_set_container (view->priv->component,
					   BONOBO_OBJREF (view->priv->container),
					   NULL);
		
	bonobo_ui_util_set_ui (view->priv->component,
			       DATADIR,
			       view->priv->ui_file,
			       view->priv->ui_name,
			       NULL);

	if (view->priv->verbs != NULL)
	{
		bonobo_ui_component_add_verb_list_with_data (view->priv->component,
							     view->priv->verbs,
							     view);
	}
	
	if (view->priv->listeners != NULL)
	{
		rb_bonobo_add_listener_list_with_data (view->priv->component,
						       view->priv->listeners,
						       view);
	}
	
	g_hash_table_foreach (view->priv->sensitive_paths,
			      (GHFunc) set_sensitive_foreach_func,
			      view);
	g_hash_table_foreach (view->priv->active_paths,
			      (GHFunc) set_active_foreach_func,
			      view);
}

void
rb_view_unmerge_ui (RBView *view)
{
	g_hash_table_foreach (view->priv->sensitive_paths,
			      (GHFunc) get_sensitive_foreach_func,
			      view);
	g_hash_table_foreach (view->priv->active_paths,
			      (GHFunc) get_active_foreach_func,
			      view);

	bonobo_ui_component_unset_container (view->priv->component, NULL);
	bonobo_object_unref (view->priv->component);
	view->priv->component = NULL;
}

RBSidebarButton *
rb_view_get_sidebar_button (RBView *view)
{
	g_return_val_if_fail (RB_IS_VIEW (view), NULL);

	return view->priv->button;
}

void
rb_view_set_sensitive (RBView *view,
		       const char *path,
		       gboolean sensitive)
{
	g_hash_table_replace (view->priv->sensitive_paths, g_strdup (path),
			      GINT_TO_POINTER (sensitive));

	if (view->priv->component != NULL)
		rb_bonobo_set_sensitive (view->priv->component, path, sensitive);
}

void
rb_view_set_active (RBView *view,
		    const char *path,
		    gboolean active)
{
	g_hash_table_replace (view->priv->active_paths, g_strdup (path),
			      GINT_TO_POINTER (active));

	if (view->priv->component != NULL)
		rb_bonobo_set_active (view->priv->component, path, active);
}

gboolean
rb_view_get_active (RBView *view,
		    const char *path)
{
	if (view->priv->component != NULL)
		return rb_bonobo_get_active (view->priv->component, path);
	else
		return GPOINTER_TO_INT (g_hash_table_lookup (view->priv->active_paths, path));
}

static void
set_sensitive_foreach_func (char *key,
			    gpointer value,
			    RBView *view)
{
	rb_bonobo_set_sensitive (view->priv->component, key, GPOINTER_TO_INT (value));
}

static void
set_active_foreach_func (char *key,
			 gpointer value,
			 RBView *view)
{
	rb_bonobo_set_active (view->priv->component, key, GPOINTER_TO_INT (value));
}

static void
get_sensitive_foreach_func (char *key,
			    gpointer value,
			    RBView *view)
{
	g_hash_table_replace (view->priv->sensitive_paths, g_strdup (key), 
	                      GINT_TO_POINTER (rb_bonobo_get_sensitive (view->priv->component, key)));
}

static void
get_active_foreach_func (char *key,
			 gpointer value,
			 RBView *view)
{
	g_hash_table_replace (view->priv->active_paths, g_strdup (key), 
	                      GINT_TO_POINTER (rb_bonobo_get_active (view->priv->component, key)));
}

void
rb_view_deleted (RBView *view)
{
	g_signal_emit (G_OBJECT (view), rb_view_signals[DELETED], 0);
}

static void
sidebar_button_deleted_cb (RBSidebarButton *button,
			   RBView *view)
{
	rb_view_deleted (view);
}

const char *
rb_view_get_description (RBView *view)
{
	RBViewClass *klass;

	klass = RB_VIEW_GET_CLASS (view);

	return klass->impl_get_description (view);
}
