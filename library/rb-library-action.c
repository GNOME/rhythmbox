/*
 *  arch-tag: Implementation of library change request
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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
 */

#include "rb-library-action.h"

static void rb_library_action_class_init (RBLibraryActionClass *klass);
static void rb_library_action_init (RBLibraryAction *library_action);
static void rb_library_action_finalize (GObject *object);
static void rb_library_action_dispose (GObject *object);
static void rb_library_action_set_property (GObject *object,
				            guint prop_id,
				            const GValue *value,
				            GParamSpec *pspec);
static void rb_library_action_get_property (GObject *object,
				            guint prop_id,
				            GValue *value,
				            GParamSpec *pspec);

struct RBLibraryActionPrivate
{
	RBLibraryActionType type;
	char *uri;
};

static GObjectClass *parent_class = NULL;

enum
{
	PROP_0,
	PROP_TYPE,
	PROP_URI
};

GType
rb_library_action_get_type (void)
{
	static GType rb_library_action_type = 0;

	if (rb_library_action_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibraryActionClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_action_class_init,
			NULL,
			NULL,
			sizeof (RBLibraryAction),
			0,
			(GInstanceInitFunc) rb_library_action_init
		};

		rb_library_action_type = g_type_register_static (G_TYPE_OBJECT,
						                 "RBLibraryAction",
						                 &our_info, 0);
	}

	return rb_library_action_type;
}

static void
rb_library_action_class_init (RBLibraryActionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_action_finalize;
	object_class->dispose  = rb_library_action_dispose;

	object_class->set_property = rb_library_action_set_property;
	object_class->get_property = rb_library_action_get_property;

	g_object_class_install_property (object_class,
					 PROP_TYPE,
					 g_param_spec_enum ("type",
							    "Action type",
							    "Action type",
							    RB_TYPE_LIBRARY_ACTION_TYPE,
							    RB_LIBRARY_ACTION_ADD_FILE,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_URI,
					 g_param_spec_string ("uri",
							      "Action uri",
							      "Action uri",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_library_action_init (RBLibraryAction *library_action)
{
	library_action->priv = g_new0 (RBLibraryActionPrivate, 1);
}

static void
rb_library_action_finalize (GObject *object)
{
	RBLibraryAction *library_action;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY_ACTION (object));

	library_action = RB_LIBRARY_ACTION (object);

	g_return_if_fail (library_action->priv != NULL);

	g_free (library_action->priv->uri);

	g_free (library_action->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_library_action_dispose (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
rb_library_action_set_property (GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	RBLibraryAction *action = RB_LIBRARY_ACTION (object);

	switch (prop_id)
	{
	case PROP_TYPE:
		action->priv->type = g_value_get_enum (value);
		break;
	case PROP_URI:
		action->priv->uri = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_library_action_get_property (GObject *object,
				guint prop_id,
				GValue *value,
				GParamSpec *pspec)
{
	RBLibraryAction *action = RB_LIBRARY_ACTION (object);

	switch (prop_id)
	{
	case PROP_TYPE:
		g_value_set_enum (value, action->priv->type);
		break;
	case PROP_URI:
		g_value_set_string (value, action->priv->uri);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBLibraryAction *
rb_library_action_new (RBLibraryActionType type,
		       const char *uri)
{
	RBLibraryAction *library_action;

	library_action = RB_LIBRARY_ACTION (g_object_new (RB_TYPE_LIBRARY_ACTION,
							  "type", type,
							  "uri", uri,
							  NULL));

	g_return_val_if_fail (library_action->priv != NULL, NULL);

	return library_action;
}

void
rb_library_action_get (RBLibraryAction *action,
		       RBLibraryActionType *type,
		       char **uri)
{
	g_return_if_fail (RB_IS_LIBRARY_ACTION (action));

	*type = action->priv->type;
	*uri = (char *) action->priv->uri;
}

GType
rb_library_action_type_get_type (void)
{
	static GType etype = 0;
	
	if (etype == 0)
	{
		static const GEnumValue values[] =
		{
			{ RB_LIBRARY_ACTION_ADD_FILE,      "RB_LIBRARY_ACTION_ADD_FILE",      "add file" },
			{ RB_LIBRARY_ACTION_REMOVE_FILE,   "RB_LIBRARY_ACTION_REMOVE_FILE",   "remove file" },
			{ RB_LIBRARY_ACTION_UPDATE_FILE,   "RB_LIBRARY_ACTION_UPDATE_FILE",   "update file" },
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBLibraryActionType", values);
	}

	return etype;
}
