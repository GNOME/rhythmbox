/* 
 *  arch-tag: Implementation of linear, looping navigation method
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

#include "rb-play-order-linear-loop.h"

#include "rb-debug.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"

static void rb_linear_play_order_loop_class_init (RBLinearPlayOrderLoopClass *klass);

static RhythmDBEntry* rb_linear_play_order_loop_get_next (RBPlayOrder* method);
static RhythmDBEntry* rb_linear_play_order_loop_get_previous (RBPlayOrder* method);

GType
rb_linear_play_order_loop_get_type (void)
{
	static GType rb_linear_play_order_loop_type = 0;

	if (rb_linear_play_order_loop_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLinearPlayOrderLoopClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_linear_play_order_loop_class_init,
			NULL,
			NULL,
			sizeof (RBLinearPlayOrderLoop),
			0,
			NULL
		};

		rb_linear_play_order_loop_type = g_type_register_static (RB_TYPE_PLAY_ORDER,
				"RBLinearPlayOrderLoop",
				&our_info, 0);
	}

	return rb_linear_play_order_loop_type;
}

RBPlayOrder *
rb_linear_play_order_loop_new (RBShellPlayer *player)
{
	RBLinearPlayOrderLoop *lorder;

	lorder = g_object_new (RB_TYPE_LINEAR_PLAY_ORDER_LOOP,
			       "player", player,
			       NULL);

	return RB_PLAY_ORDER (lorder);
}

static void
rb_linear_play_order_loop_class_init (RBLinearPlayOrderLoopClass *klass)
{
	RBPlayOrderClass *porder = RB_PLAY_ORDER_CLASS (klass);
	porder->get_next = rb_linear_play_order_loop_get_next;
	porder->get_previous = rb_linear_play_order_loop_get_previous;
}

static RhythmDBEntry* 
rb_linear_play_order_loop_get_next (RBPlayOrder* porder)
{
	RBEntryView *entry_view;
	RhythmDBEntry *entry;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_LINEAR_PLAY_ORDER_LOOP (porder), NULL);

	entry_view = rb_play_order_get_entry_view (porder);
	/* Does this interfere with starting from not playing? */
	if (entry_view == NULL)
		return NULL;

	rb_debug ("choosing next linked entry");
	entry = rb_entry_view_get_next_entry (entry_view);

	if (entry == NULL) {
		rb_debug ("Looping back to the first entry");
		entry = rb_entry_view_get_first_entry (entry_view);
	}

	return entry;
}

static RhythmDBEntry*
rb_linear_play_order_loop_get_previous (RBPlayOrder* porder)
{
	RBEntryView *entry_view;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_LINEAR_PLAY_ORDER_LOOP (porder), NULL);

	entry_view = rb_play_order_get_entry_view (porder);
	g_return_val_if_fail (entry_view != NULL, NULL);

	rb_debug ("choosing previous linked entry");
	return rb_entry_view_get_previous_entry (entry_view);

	/* If we're at the beginning of the list, should we go to the last
	 * entry? */
}
