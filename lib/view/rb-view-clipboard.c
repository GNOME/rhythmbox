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

#include "rb-view-clipboard.h"

static void rb_view_clipboard_base_init (gpointer g_iface);

enum
{
	CLIPBOARD_CHANGED,
	LAST_SIGNAL
};

static guint rb_view_clipboard_signals[LAST_SIGNAL] = { 0 };

GType
rb_view_clipboard_get_type (void)
{
	static GType rb_view_clipboard_type = 0;

	if (rb_view_clipboard_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBViewClipboardIface),
			rb_view_clipboard_base_init,
			NULL,
			NULL,
			NULL,
			NULL,
			0,
			0,
			NULL
		};

		rb_view_clipboard_type = g_type_register_static (G_TYPE_INTERFACE,
							         "RBViewClipboard",
							         &our_info, 0);
		g_type_interface_add_prerequisite (rb_view_clipboard_type, G_TYPE_OBJECT);
	}

	return rb_view_clipboard_type;
}

static void
rb_view_clipboard_base_init (gpointer g_iface)
{
	static gboolean initialized = FALSE;

	if (initialized == TRUE)
		return;

	rb_view_clipboard_signals[CLIPBOARD_CHANGED] =
		g_signal_new ("clipboard_changed",
			      RB_TYPE_VIEW_CLIPBOARD,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBViewClipboardIface, clipboard_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	initialized = TRUE;
}

gboolean
rb_view_clipboard_can_cut (RBViewClipboard *clipboard)
{
	RBViewClipboardIface *iface = RB_VIEW_CLIPBOARD_GET_IFACE (clipboard);

	return iface->impl_can_cut (clipboard);
}

gboolean
rb_view_clipboard_can_copy (RBViewClipboard *clipboard)
{
	RBViewClipboardIface *iface = RB_VIEW_CLIPBOARD_GET_IFACE (clipboard);

	return iface->impl_can_copy (clipboard);
}

gboolean
rb_view_clipboard_can_paste (RBViewClipboard *clipboard)
{
	RBViewClipboardIface *iface = RB_VIEW_CLIPBOARD_GET_IFACE (clipboard);

	return iface->impl_can_paste (clipboard);
}

gboolean
rb_view_clipboard_can_delete (RBViewClipboard *clipboard)
{
	RBViewClipboardIface *iface = RB_VIEW_CLIPBOARD_GET_IFACE (clipboard);

	return iface->impl_can_delete (clipboard);
}

GList *
rb_view_clipboard_cut (RBViewClipboard *clipboard)
{
	RBViewClipboardIface *iface = RB_VIEW_CLIPBOARD_GET_IFACE (clipboard);
	GList *ret;

	ret = iface->impl_cut (clipboard);

	rb_view_clipboard_notify_changed (clipboard);

	return ret;
}

GList *
rb_view_clipboard_copy (RBViewClipboard *clipboard)
{
	RBViewClipboardIface *iface = RB_VIEW_CLIPBOARD_GET_IFACE (clipboard);
	GList *ret;

	ret = iface->impl_copy (clipboard);

	rb_view_clipboard_notify_changed (clipboard);

	return ret;
}

void
rb_view_clipboard_paste (RBViewClipboard *clipboard,
		         GList *nodes)
{
	RBViewClipboardIface *iface = RB_VIEW_CLIPBOARD_GET_IFACE (clipboard);

	iface->impl_paste (clipboard, nodes);
}

void
rb_view_clipboard_delete (RBViewClipboard *clipboard)
{
	RBViewClipboardIface *iface = RB_VIEW_CLIPBOARD_GET_IFACE (clipboard);

	iface->impl_delete (clipboard);
}

void
rb_view_clipboard_notify_changed (RBViewClipboard *clipboard)
{
	g_signal_emit (G_OBJECT (clipboard), rb_view_clipboard_signals[CLIPBOARD_CHANGED], 0);
}
