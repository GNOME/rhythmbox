/*
 *  arch-tag: Header for song cut/paste handler object
 *
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
 */

#include <bonobo/bonobo-ui-component.h>
#include "rb-source.h"

#ifndef __RB_SHELL_CLIPBOARD_H
#define __RB_SHELL_CLIPBOARD_H

G_BEGIN_DECLS

#define RB_TYPE_SHELL_CLIPBOARD         (rb_shell_clipboard_get_type ())
#define RB_SHELL_CLIPBOARD(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SHELL_CLIPBOARD, RBShellClipboard))
#define RB_SHELL_CLIPBOARD_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SHELL_CLIPBOARD, RBShellClipboardClass))
#define RB_IS_SHELL_CLIPBOARD(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SHELL_CLIPBOARD))
#define RB_IS_SHELL_CLIPBOARD_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SHELL_CLIPBOARD))
#define RB_SHELL_CLIPBOARD_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SHELL_CLIPBOARD, RBShellClipboardClass))

typedef struct RBShellClipboardPrivate RBShellClipboardPrivate;

typedef struct
{
	GObject parent;

	RBShellClipboardPrivate *priv;
} RBShellClipboard;

typedef struct
{
	GObjectClass parent_class;
} RBShellClipboardClass;

GType             rb_shell_clipboard_get_type		(void);

RBShellClipboard *rb_shell_clipboard_new		(BonoboUIComponent *component, RhythmDB *db);

void              rb_shell_clipboard_set_source		(RBShellClipboard *shell_clipboard,
							 RBSource *source);

G_END_DECLS

#endif /* __RB_SHELL_CLIPBOARD_H */
