/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * 
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

#include <string.h>

#include <glib/gi18n.h>

#include "rb-play-order.h"

#include "rb-shell-player.h"
#include "rb-debug.h"
#include "rb-preferences.h"

/* Play Orders */
#include "rb-play-order-linear.h"
#include "rb-play-order-linear-loop.h"
#include "rb-play-order-shuffle.h"
#include "rb-play-order-random-equal-weights.h"
#include "rb-play-order-random-by-age.h"
#include "rb-play-order-random-by-rating.h"
#include "rb-play-order-random-by-age-and-rating.h"

static void rb_play_order_class_init (RBPlayOrderClass *klass);
static void rb_play_order_init (RBPlayOrder *porder);
static GObject *rb_play_order_constructor (GType type, guint n_construct_properties,
					   GObjectConstructParam *construct_properties);
static void rb_play_order_finalize (GObject *object);
static void rb_play_order_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rb_play_order_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);

static gboolean default_has_next (RBPlayOrder *porder);
static gboolean default_has_previous (RBPlayOrder *porder);

static void rb_play_order_playing_entry_changed_cb (GObject *entry_view,
						    GParamSpec *pspec,
						    RBPlayOrder *porder);
static void rb_play_order_entry_added_cb (RBPlayOrder *porder, RhythmDBEntry *entry);
static void rb_play_order_entry_deleted_cb (RBPlayOrder *porder, RhythmDBEntry *entry);
static void rb_play_order_entries_replaced_cb (RBPlayOrder *porder);

struct RBPlayOrderPrivate
{
	RBShellPlayer *player;
	RBSource *source;
	RhythmDB *db;
};

#define RB_PLAY_ORDER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PLAY_ORDER, RBPlayOrderPrivate))

enum
{
	PROP_0,
	PROP_PLAYER,
};


G_DEFINE_TYPE (RBPlayOrder, rb_play_order, G_TYPE_OBJECT)

static void
rb_play_order_class_init (RBPlayOrderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructor = rb_play_order_constructor;
	object_class->finalize = rb_play_order_finalize;
	object_class->set_property = rb_play_order_set_property;
	object_class->get_property = rb_play_order_get_property;

	klass->has_next = default_has_next;
	klass->has_previous = default_has_previous;

	g_object_class_install_property (object_class,
					 PROP_PLAYER,
					 g_param_spec_object ("player",
						 	      "RBShellPlayer",
						 	      "Rhythmbox Player",
						 	      RB_TYPE_SHELL_PLAYER,
						 	      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBPlayOrderPrivate));
}

static void
rb_play_order_init (RBPlayOrder *porder)
{
	porder->priv = RB_PLAY_ORDER_GET_PRIVATE (porder);
}

static GObject *
rb_play_order_constructor (GType type, guint n_construct_properties,
			   GObjectConstructParam *construct_properties)
{
	RBPlayOrder *porder;

	porder = RB_PLAY_ORDER (G_OBJECT_CLASS (rb_play_order_parent_class)
			->constructor (type, n_construct_properties, construct_properties));

	rb_play_order_playing_source_changed (porder);

	return G_OBJECT (porder);
}

static void
rb_play_order_finalize (GObject *object)
{
	RBPlayOrder *porder;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PLAY_ORDER (object));

	porder = RB_PLAY_ORDER (object);

	if (porder->priv->source != NULL) {
		RBEntryView *entry_view = rb_source_get_entry_view (porder->priv->source);
		g_signal_handlers_disconnect_by_func (G_OBJECT (entry_view),
						      G_CALLBACK (rb_play_order_playing_entry_changed_cb),
						      porder);
		g_signal_handlers_disconnect_by_func (G_OBJECT (entry_view),
						      G_CALLBACK (rb_play_order_entry_added_cb),
						      porder);
		g_signal_handlers_disconnect_by_func (G_OBJECT (entry_view),
						      G_CALLBACK (rb_play_order_entry_deleted_cb),
						      porder);
		g_signal_handlers_disconnect_by_func (G_OBJECT (entry_view),
						      G_CALLBACK (rb_play_order_entries_replaced_cb),
						      porder);
	}

	G_OBJECT_CLASS (rb_play_order_parent_class)->finalize (object);
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
const RBPlayOrderDescription *
rb_play_order_get_orders (void)
{
	/* Exactly one entry must have is_default==TRUE. Otherwise you will
	 * cause a g_assert(). */
	static const RBPlayOrderDescription orders[] = {
		{ "linear", N_("Linear"), rb_linear_play_order_new, TRUE, TRUE },
		{ "linear-loop", N_("Linear looping"), rb_linear_play_order_loop_new, TRUE, FALSE },
		{ "shuffle", N_("Shuffle"), rb_shuffle_play_order_new, TRUE, FALSE },
		{ "random-equal-weights", N_("Random with equal weights"), rb_random_play_order_equal_weights_new, TRUE, FALSE },
		{ "random-by-age", N_("Random by time since last play"), rb_random_play_order_by_age_new, TRUE, FALSE },
		{ "random-by-rating", N_("Random by rating"), rb_random_play_order_by_rating_new, TRUE, FALSE },
		{ "random-by-age-and-rating", N_("Random by time since last play and rating"), rb_random_play_order_by_age_and_rating_new, TRUE, FALSE },
		{ NULL, NULL, NULL },
	};
	return orders;
}

RBPlayOrder *
rb_play_order_new (const char* porder_name, RBShellPlayer *player)
{
	int default_index = -1;
	const RBPlayOrderDescription *orders = rb_play_order_get_orders ();
	int i;

	g_return_val_if_fail (porder_name != NULL, NULL);
	g_return_val_if_fail (player != NULL, NULL);

	for (i=0; orders[i].name != NULL; ++i) {
		if (strcmp (orders[i].name, porder_name) == 0)
			return orders[i].constructor (player);
		if (orders[i].is_default) {
			/* There must not be two default play orders */
			g_assert (default_index == -1);
			default_index = i;
		}
	}
	/* There must be a default play order */
	g_assert (default_index != -1);

	g_warning ("Unknown value \"%s\" in GConf key \"" CONF_STATE_PLAY_ORDER
			"\". Using %s play order.", porder_name, orders[default_index].name);
	return orders[default_index].constructor (player);
}

RBShellPlayer *
rb_play_order_get_player (RBPlayOrder *porder)
{
	return porder->priv->player;
}

RBSource *
rb_play_order_get_source (RBPlayOrder *porder)
{
	return porder->priv->source;
}

RhythmDB *
rb_play_order_get_db (RBPlayOrder *porder)
{
	return porder->priv->db;
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

	g_return_val_if_fail (porder != NULL, NULL);
	
	source = porder->priv->source;
	if (source == NULL)
		return NULL;
	return rb_source_get_entry_view (source);
}

RhythmDBEntry *
rb_play_order_get_playing_entry (RBPlayOrder *porder)
{
	RhythmDBEntry *entry = NULL;

	RBEntryView *entry_view = rb_play_order_get_entry_view (porder);
	if (entry_view) {
		g_object_get (entry_view,
			      "playing-entry", &entry,
			      NULL);
	}

	return entry;
}

void
rb_play_order_playing_source_changed (RBPlayOrder *porder)
{
	RBSource *source;
	RhythmDB *db = NULL;

	g_return_if_fail (porder != NULL);

	source = rb_shell_player_get_playing_source (porder->priv->player);

	g_object_get (G_OBJECT (porder->priv->player),
		      "db", &db,
		      NULL);

	if (db != porder->priv->db) {
		if (RB_PLAY_ORDER_GET_CLASS (porder)->db_changed)
			RB_PLAY_ORDER_GET_CLASS (porder)->db_changed (porder, db);
		porder->priv->db = db;
	}

	if (source != porder->priv->source) {
		if (porder->priv->source != NULL) {
			RBEntryView *entry_view = rb_source_get_entry_view (porder->priv->source);
			g_signal_handlers_disconnect_by_func (G_OBJECT (entry_view),
							      G_CALLBACK (rb_play_order_playing_entry_changed_cb),
							      porder);
			g_signal_handlers_disconnect_by_func (G_OBJECT (entry_view),
							      G_CALLBACK (rb_play_order_entry_added_cb),
							      porder);
			g_signal_handlers_disconnect_by_func (G_OBJECT (entry_view),
							      G_CALLBACK (rb_play_order_entry_deleted_cb),
							      porder);
			g_signal_handlers_disconnect_by_func (G_OBJECT (entry_view),
							      G_CALLBACK (rb_play_order_entries_replaced_cb),
							      porder);
		}
		porder->priv->source = source;
		if (porder->priv->source != NULL) {
			/* Optimization possibilty: Only connect handlers
			 * subclass actually listens to. */
			RBEntryView *entry_view = rb_source_get_entry_view (porder->priv->source);
			g_signal_connect_object (G_OBJECT (entry_view),
						 "notify::playing-entry",
						 G_CALLBACK (rb_play_order_playing_entry_changed_cb),
						 porder, 0);
			g_signal_connect_object (G_OBJECT (entry_view),
						 "entry-added",
						 G_CALLBACK (rb_play_order_entry_added_cb),
						 porder, G_CONNECT_SWAPPED);
			g_signal_connect_object (G_OBJECT (entry_view),
						  "entry-deleted",
						  G_CALLBACK (rb_play_order_entry_deleted_cb),
						 porder, G_CONNECT_SWAPPED);
			g_signal_connect_object (G_OBJECT (entry_view),
						 "entries-replaced",
						 G_CALLBACK (rb_play_order_entries_replaced_cb),
						 porder, G_CONNECT_SWAPPED);
		}
	}

	/* Notify subclass if it cares */
	if (RB_PLAY_ORDER_GET_CLASS (porder)->playing_source_changed)
		RB_PLAY_ORDER_GET_CLASS (porder)->playing_source_changed (porder);
	/* These are an inevitable side effect of replacing the source. */
	if (RB_PLAY_ORDER_GET_CLASS (porder)->entries_replaced)
		RB_PLAY_ORDER_GET_CLASS (porder)->entries_replaced (porder);
	if (RB_PLAY_ORDER_GET_CLASS (porder)->playing_entry_changed)
		RB_PLAY_ORDER_GET_CLASS (porder)->playing_entry_changed (porder, rb_play_order_get_playing_entry (porder));
}

static void
rb_play_order_playing_entry_changed_cb (GObject *entry_view,
					GParamSpec *pspec,
					RBPlayOrder *porder)
{
	g_return_if_fail (strcmp (pspec->name, "playing-entry") == 0);

	if (RB_PLAY_ORDER_GET_CLASS (porder)->playing_entry_changed) {
		RhythmDBEntry *entry;
		g_object_get (entry_view,
				"playing-entry", &entry,
				NULL);
		RB_PLAY_ORDER_GET_CLASS (porder)->playing_entry_changed (porder, entry);
	}
}

static void
rb_play_order_entry_added_cb (RBPlayOrder *porder, RhythmDBEntry *entry)
{
	if (RB_PLAY_ORDER_GET_CLASS (porder)->entry_added)
		RB_PLAY_ORDER_GET_CLASS (porder)->entry_added (porder, entry);
}

static void
rb_play_order_entry_deleted_cb (RBPlayOrder *porder, RhythmDBEntry *entry)
{
	if (RB_PLAY_ORDER_GET_CLASS (porder)->entry_removed) {
		rb_debug ("signaling entry_deleted");
		RB_PLAY_ORDER_GET_CLASS (porder)->entry_removed (porder, entry);
	}
}

static void
rb_play_order_entries_replaced_cb (RBPlayOrder *porder)
{
	if (RB_PLAY_ORDER_GET_CLASS (porder)->entries_replaced)
		RB_PLAY_ORDER_GET_CLASS (porder)->entries_replaced (porder);
}

static gboolean
default_has_next (RBPlayOrder *porder)
{
	return rb_play_order_get_next (porder) != NULL;
}

static gboolean
default_has_previous (RBPlayOrder *porder)
{
	return rb_play_order_get_previous (porder) != NULL;
}

gboolean
rb_play_order_has_next (RBPlayOrder* porder)
{
	g_return_val_if_fail (porder != NULL, FALSE);
	g_return_val_if_fail (RB_PLAY_ORDER_GET_CLASS (porder)->has_next != NULL, FALSE);
	return RB_PLAY_ORDER_GET_CLASS (porder)->has_next (porder);
}

RhythmDBEntry *
rb_play_order_get_next (RBPlayOrder *porder)
{
	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_PLAY_ORDER_GET_CLASS (porder)->get_next != NULL, NULL);
	return RB_PLAY_ORDER_GET_CLASS (porder)->get_next (porder);
}

void
rb_play_order_go_next (RBPlayOrder *porder)
{
	g_return_if_fail (porder != NULL);
	if (RB_PLAY_ORDER_GET_CLASS (porder)->go_next)
		RB_PLAY_ORDER_GET_CLASS (porder)->go_next (porder);
}

gboolean
rb_play_order_has_previous (RBPlayOrder* porder)
{
	g_return_val_if_fail (porder != NULL, FALSE);
	g_return_val_if_fail (RB_PLAY_ORDER_GET_CLASS (porder)->has_previous != NULL, FALSE);
	return RB_PLAY_ORDER_GET_CLASS (porder)->has_previous (porder);
}

RhythmDBEntry *
rb_play_order_get_previous (RBPlayOrder *porder)
{
	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_PLAY_ORDER_GET_CLASS (porder)->get_previous != NULL, NULL);
	return RB_PLAY_ORDER_GET_CLASS (porder)->get_previous (porder);
}

void
rb_play_order_go_previous (RBPlayOrder *porder)
{
	g_return_if_fail (porder != NULL);
	if (RB_PLAY_ORDER_GET_CLASS (porder)->go_previous)
		RB_PLAY_ORDER_GET_CLASS (porder)->go_previous (porder);
}

void
rb_play_order_ref_entry_swapped (RhythmDBEntry *entry, RhythmDB *db)
{
	rhythmdb_entry_ref (db, entry);
}

void
rb_play_order_unref_entry_swapped (RhythmDBEntry *entry, RhythmDB *db)
{
	rhythmdb_entry_unref (db, entry);
}
