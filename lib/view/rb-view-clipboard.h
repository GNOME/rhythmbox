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

#include <gtk/gtktreeselection.h>

#ifndef __RB_VIEW_CLIPBOARD_H
#define __RB_VIEW_CLIPBOARD_H

G_BEGIN_DECLS

#define RB_TYPE_VIEW_CLIPBOARD         (rb_view_clipboard_get_type ())
#define RB_VIEW_CLIPBOARD(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_VIEW_CLIPBOARD, RBViewClipboard))
#define RB_IS_VIEW_CLIPBOARD(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_VIEW_CLIPBOARD))
#define RB_VIEW_CLIPBOARD_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), RB_TYPE_VIEW_CLIPBOARD, RBViewClipboardIface))

typedef struct RBViewClipboard RBViewClipboard; /* Dummy typedef */

typedef struct
{
	GTypeInterface g_iface;

	/* signals */
	void (*clipboard_changed) (RBViewClipboard *clipboard);

	/* methods */
	gboolean (*impl_can_cut)   (RBViewClipboard *clipboard);
	gboolean (*impl_can_copy)  (RBViewClipboard *clipboard);
	gboolean (*impl_can_paste) (RBViewClipboard *clipboard);
	
	GList   *(*impl_cut)       (RBViewClipboard *clipboard);
	GList   *(*impl_copy)      (RBViewClipboard *clipboard);
	void     (*impl_paste)     (RBViewClipboard *clipboard,
				    GList *nodes);
} RBViewClipboardIface;

GType    rb_view_clipboard_get_type       (void);

gboolean rb_view_clipboard_can_cut        (RBViewClipboard *clipboard);
gboolean rb_view_clipboard_can_copy       (RBViewClipboard *clipboard);
gboolean rb_view_clipboard_can_paste      (RBViewClipboard *clipboard);

GList   *rb_view_clipboard_cut            (RBViewClipboard *clipboard);
GList   *rb_view_clipboard_copy           (RBViewClipboard *clipboard);
void     rb_view_clipboard_paste          (RBViewClipboard *clipboard,
				           GList *nodes);

void     rb_view_clipboard_notify_changed (RBViewClipboard *clipboard);

G_END_DECLS

#endif /* __RB_VIEW_CLIPBOARD_H */
