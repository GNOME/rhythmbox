/* 
 *  arch-tag: Implementation of linear navigation method
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

#include "rb-play-order-linear.h"

#include "rb-debug.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"

static void rb_linear_play_order_class_init (RBLinearPlayOrderClass *klass);

static RhythmDBEntry* rb_linear_play_order_get_next (RBPlayOrder* method);
static RhythmDBEntry* rb_linear_play_order_get_previous (RBPlayOrder* method);

GType
rb_linear_play_order_get_type (void)
{
	static GType rb_linear_play_order_type = 0;

	if (rb_linear_play_order_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLinearPlayOrderClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_linear_play_order_class_init,
			NULL,
			NULL,
			sizeof (RBLinearPlayOrder),
			0,
			NULL
		};

		rb_linear_play_order_type = g_type_register_static (RB_TYPE_PLAY_ORDER,
				"RBLinearPlayOrder",
				&our_info, 0);
	}

	return rb_linear_play_order_type;
}

RBLinearPlayOrder *
rb_linear_play_order_new (RBShellPlayer *player)
{
	RBLinearPlayOrder *lorder;

	lorder = g_object_new (RB_TYPE_LINEAR_PLAY_ORDER,
			       "player", player,
			       NULL);

	return lorder;
}

static void
rb_linear_play_order_class_init (RBLinearPlayOrderClass *klass)
{
	RBPlayOrderClass *porder = RB_PLAY_ORDER_CLASS (klass);
	porder->get_next = rb_linear_play_order_get_next;
	porder->get_previous = rb_linear_play_order_get_previous;
}

static RhythmDBEntry* 
rb_linear_play_order_get_next (RBPlayOrder* porder)
{
	RBEntryView *entry_view;
	RhythmDBEntry *entry;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_LINEAR_PLAY_ORDER (porder), NULL);

	entry_view = rb_play_order_get_entry_view (porder);
	/* Does this interfere with starting from not playing? */
	if (entry_view == NULL)
		return NULL;

	rb_debug ("choosing next linked entry");
	entry = rb_entry_view_get_next_entry (entry_view);

	if (entry == NULL
			&& (rb_entry_view_get_playing_entry (entry_view) == NULL
				|| eel_gconf_get_boolean (CONF_STATE_REPEAT))) {
		rb_debug ("No next entry, but repeat is enabled, or no current entry");
		entry = rb_entry_view_get_first_entry (entry_view);
	}

	return entry;
}

static RhythmDBEntry*
rb_linear_play_order_get_previous (RBPlayOrder* porder)
{
	RBEntryView *entry_view;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_LINEAR_PLAY_ORDER (porder), NULL);

	entry_view = rb_play_order_get_entry_view (porder);
	g_return_val_if_fail (entry_view != NULL, NULL);

	rb_debug ("choosing previous linked entry");
	return rb_entry_view_get_previous_entry (entry_view);

	/* If we're at the beginning of the list and Repeat is enabled, should
	 * we go to the last entry? */
}
