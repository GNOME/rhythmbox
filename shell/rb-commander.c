/*
 *  Copyright © 2002 Jorn Baayen.  All rights reserved.
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

#include <config.h>
#include <libgnome/gnome-i18n.h>

#include "rb-commander.h"

static void rb_commander_class_init (RBCommanderClass *klass);
static void rb_commander_init (RBCommander *commander);
static void rb_commander_finalize (GObject *object);
static void rb_commander_set_property (GObject *object,
				       guint prop_id,
				       const GValue *value,
				       GParamSpec *pspec);
static void rb_commander_get_property (GObject *object,
				       guint prop_id,
				       GValue *value,
			               GParamSpec *pspec);

struct RBCommanderPrivate
{
	RB *rb;

	char *foo;
};

enum
{
	PROP_0,
	PROP_RB
};

static GObjectClass *parent_class = NULL;

GType
rb_commander_get_type (void)
{
	static GType rb_commander_type = 0;

	if (rb_commander_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBCommanderClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_commander_class_init,
			NULL,
			NULL,
			sizeof (RBCommander),
			0,
			(GInstanceInitFunc) rb_commander_init
		};

		rb_commander_type = g_type_register_static (G_TYPE_OBJECT,
							    "RBCommander",
							    &our_info, 0);
	}

	return rb_commander_type;
}

static void
rb_commander_class_init (RBCommanderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_commander_finalize;

	object_class->set_property = rb_commander_set_property;
	object_class->get_property = rb_commander_get_property;

	g_object_class_install_property (object_class,
					 PROP_RB,
					 g_param_spec_object ("rb",
							      "RB",
							      "RB object",
							      RB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_commander_init (RBCommander *commander)
{
	commander->priv = g_new0 (RBCommanderPrivate, 1);
}

static void
rb_commander_finalize (GObject *object)
{
	RBCommander *commander;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_COMMANDER (object));

	commander = RB_COMMANDER (object);

	g_return_if_fail (commander->priv != NULL);

	g_free (commander->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_commander_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBCommander *commander = RB_COMMANDER (object);

	switch (prop_id)
	{
	case PROP_RB:
		commander->priv->rb = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_commander_get_property (GObject *object,
			   guint prop_id,
			   GValue *value,
			   GParamSpec *pspec)
{
	RBCommander *commander = RB_COMMANDER (object);

	switch (prop_id)
	{
	case PROP_RB:
		g_value_set_object (value, commander->priv->rb);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBCommander *
rb_commander_new (RB *rb)
{
	RBCommander *commander;

	commander = g_object_new (RB_TYPE_COMMANDER,
			          "rb", rb,
			          NULL);

	g_return_val_if_fail (commander->priv != NULL, NULL);

	return commander;
}
