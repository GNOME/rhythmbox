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

#include "rb-view-cmd.h"
#include "rb-view-clipboard.h"

void
rb_view_cmd_song_copy (BonoboUIComponent *component,
		       RBView *view,
		       const char *verbname)
{
	GList *selection;

	selection = rb_view_get_selection (view);
	rb_view_clipboard_set (RB_VIEW_CLIPBOARD (view), selection);

	g_list_free (selection);	
}

void
rb_view_cmd_song_cut  (BonoboUIComponent *component,
		       RBView *view,
		       const char *verbname)
{
	GList *nodes;

	nodes = rb_view_clipboard_cut (RB_VIEW_CLIPBOARD (view));
 	rb_view_clipboard_set (RB_VIEW_CLIPBOARD (view), nodes);
}

void
rb_view_cmd_song_paste  (BonoboUIComponent *component,
		         RBView *view,
		         const char *verbname)
{
	rb_view_clipboard_request_paste (RB_VIEW_CLIPBOARD (view));
}

void
rb_view_cmd_song_delete (BonoboUIComponent *component,
		         RBView *view,
		         const char *verbname)
{
	rb_view_clipboard_delete (RB_VIEW_CLIPBOARD (view));
}

void
rb_view_cmd_song_properties (BonoboUIComponent *component,
		             RBView *view,
		             const char *verbname)
{
	rb_view_clipboard_song_info (RB_VIEW_CLIPBOARD (view));
}
