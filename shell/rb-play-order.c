/* 
 *  arch-tag: Implementation of base class for play order classes
 *
 *  Copyright (C) 2003 Jeffrey Yasskin <jyasskin@mail.utexas.edu>
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

#include "rb-play-order.h"

#include "rb-shell-player.h"
#include <string.h>
#include "rb-preferences.h"

/* Play Orders */
#include "rb-play-order-linear.h"
#include "rb-play-order-shuffle.h"
#include "rb-play-order-random-equal-weights.h"
#include "rb-play-order-random-by-age.h"

static void rb_play_order_class_init (RBPlayOrderClass *klass);
static void rb_play_order_init (RBPlayOrder *porder);

static void rb_play_order_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rb_play_order_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);

struct RBPlayOrderPrivate
{
	RBShellPlayer *player;
};

enum
{
	PROP_0,
	PROP_PLAYER,
};


static GObjectClass *parent_class = NULL;

GType
rb_play_order_get_type (void)
{
	static GType rb_play_order_type = 0;

	if (rb_play_order_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBPlayOrderClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_play_order_class_init,
			NULL,
			NULL,
			sizeof (RBPlayOrder),
			0,
			(GInstanceInitFunc) rb_play_order_init
		};

		rb_play_order_type = g_type_register_static (G_TYPE_OBJECT,
							     "RBPlayOrder",
							     &our_info, 0);
	}

	return rb_play_order_type;
}

static void
rb_play_order_class_init (RBPlayOrderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->set_property = rb_play_order_set_property;
	object_class->get_property = rb_play_order_get_property;


	g_object_class_install_property (object_class,
					 PROP_PLAYER,
					 g_param_spec_object ("player",
						 	      "RBShellPlayer",
						 	      "Rhythmbox Player",
						 	      RB_TYPE_SHELL_PLAYER,
						 	      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_play_order_init (RBPlayOrder *porder)
{
	porder->priv = g_new0 (RBPlayOrderPrivate, 1);
}

static void
rb_play_order_set_property (GObject *object,
			    guint prop_id,
			    const GValue *value,
			    GParamSpec *pspec)
{
	RBPlayOrder *porder = RB_PLAY_ORDER (object);

	switch (prop_id) {
	case PROP_PLAYER:
		porder->priv->player = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_play_order_get_property (GObject *object,
			    guint prop_id,
			    GValue *value,
			    GParamSpec *pspec)
{
	RBPlayOrder *porder = RB_PLAY_ORDER (object);

	switch (prop_id)
	{
	case PROP_PLAYER:
		g_value_set_object (value, porder->priv->player);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * This should be the only function with full knowledge of what play orders are
 * available.
 */
RBPlayOrder *
rb_play_order_new (const char* porder_name, RBShellPlayer *player)
{
	g_return_val_if_fail (porder_name != NULL, NULL);
	g_return_val_if_fail (player != NULL, NULL);

	if (strcmp (porder_name, "random-equal-weights") == 0) {
		return RB_PLAY_ORDER (rb_random_play_order_equal_weights_new (player));
	} else if (strcmp (porder_name, "random-by-age") == 0) {
		return RB_PLAY_ORDER (rb_random_play_order_by_age_new (player));
	} else if (strcmp (porder_name, "shuffle") == 0) {
		return RB_PLAY_ORDER (rb_shuffle_play_order_new (player));
	} else {
		if (strcmp (porder_name, "linear") != 0) {
			g_warning ("Unknown value \"%s\" in GConf key \"" CONF_STATE_PLAY_ORDER
					"\". Using linear play order.", porder_name);
		}
		return RB_PLAY_ORDER (rb_linear_play_order_new (player));
	}
}

RBShellPlayer *
rb_play_order_get_player (RBPlayOrder *porder)
{
	return porder->priv->player;
}

/* mostly copied from rb_shell_player_get_playing_entry in rb-shell-player.c */
gboolean
rb_play_order_player_is_playing (RBPlayOrder *porder)
{
	RBSource *source = rb_shell_player_get_playing_source (porder->priv->player);
	if (source) {
		RBEntryView *entry_view = rb_source_get_entry_view (source);
		return rb_entry_view_get_playing_entry (entry_view) != NULL;
	}
	return FALSE;
}

RBEntryView*
rb_play_order_get_entry_view (RBPlayOrder *porder)
{
	RBSource *source;
	RBShellPlayer *player = porder->priv->player;
	if (player == NULL)
		return NULL;
	source = rb_shell_player_get_playing_source (player);
	if (source == NULL)
		return NULL;
	return rb_source_get_entry_view (source);
}

gboolean
rb_play_order_has_next (RBPlayOrder* porder)
{
	RBPlayOrderClass *po_class;

	g_return_val_if_fail (porder != NULL, FALSE);

	po_class = RB_PLAY_ORDER_GET_CLASS (porder);
	if (po_class->has_next)
		return po_class->has_next (porder);
	else
		return rb_play_order_get_next (porder) != NULL;
}

RhythmDBEntry *
rb_play_order_get_next (RBPlayOrder *porder)
{
	RBPlayOrderClass *po_class;

	g_return_val_if_fail (porder != NULL, NULL);

	po_class = RB_PLAY_ORDER_GET_CLASS (porder);
	g_return_val_if_fail (po_class->get_next != NULL, NULL);
	return po_class->get_next (porder);
}

void
rb_play_order_go_next (RBPlayOrder *porder)
{
	RBPlayOrderClass *po_class;

	g_return_if_fail (porder != NULL);

	po_class = RB_PLAY_ORDER_GET_CLASS (porder);
	if (po_class->go_next)
		po_class->go_next (porder);
}

gboolean
rb_play_order_has_previous (RBPlayOrder* porder)
{
	RBPlayOrderClass *po_class;

	g_return_val_if_fail (porder != NULL, FALSE);

	po_class = RB_PLAY_ORDER_GET_CLASS (porder);
	if (po_class->has_previous)
		return po_class->has_previous (porder);
	else
		return rb_play_order_get_previous (porder) != NULL;
}

RhythmDBEntry *
rb_play_order_get_previous (RBPlayOrder *porder)
{
	RBPlayOrderClass *po_class;

	g_return_val_if_fail (porder != NULL, NULL);

	po_class = RB_PLAY_ORDER_GET_CLASS (porder);
	g_return_val_if_fail (po_class->get_previous != NULL, NULL);
	return po_class->get_previous (porder);
}

void
rb_play_order_go_previous (RBPlayOrder *porder)
{
	RBPlayOrderClass *po_class;

	g_return_if_fail (porder != NULL);

	po_class = RB_PLAY_ORDER_GET_CLASS (porder);
	if (po_class->go_previous)
		po_class->go_previous (porder);
}
