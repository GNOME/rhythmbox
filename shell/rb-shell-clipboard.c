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

#include "rb-shell-clipboard.h"

static void rb_shell_clipboard_class_init (RBShellClipboardClass *klass);
static void rb_shell_clipboard_init (RBShellClipboard *shell_clipboard);
static void rb_shell_clipboard_finalize (GObject *object);
static void rb_shell_clipboard_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rb_shell_clipboard_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);

struct RBShellClipboardPrivate
{
	RBViewClipboard *clipboard;
};

enum
{
	PROP_0,
	PROP_CLIPBOARD
};

static GObjectClass *parent_class = NULL;

GType
rb_shell_clipboard_get_type (void)
{
	static GType rb_shell_clipboard_type = 0;

	if (rb_shell_clipboard_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBShellClipboardClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_shell_clipboard_class_init,
			NULL,
			NULL,
			sizeof (RBShellClipboard),
			0,
			(GInstanceInitFunc) rb_shell_clipboard_init
		};

		rb_shell_clipboard_type = g_type_register_static (G_TYPE_OBJECT,
							       "RBShellClipboard",
							       &our_info, 0);
	}

	return rb_shell_clipboard_type;
}

static void
rb_shell_clipboard_class_init (RBShellClipboardClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_shell_clipboard_finalize;

	object_class->set_property = rb_shell_clipboard_set_property;
	object_class->get_property = rb_shell_clipboard_get_property;

	g_object_class_install_property (object_class,
					 PROP_CLIPBOARD,
					 g_param_spec_object ("clipboard",
							      "RBViewClipboard",
							      "RBViewClipboard object",
							      RB_TYPE_VIEW_CLIPBOARD,
							      G_PARAM_READWRITE));
}

static void
rb_shell_clipboard_init (RBShellClipboard *shell_clipboard)
{
	shell_clipboard->priv = g_new0 (RBShellClipboardPrivate, 1);
}

static void
rb_shell_clipboard_finalize (GObject *object)
{
	RBShellClipboard *shell_clipboard;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHELL_CLIPBOARD (object));

	shell_clipboard = RB_SHELL_CLIPBOARD (object);

	g_return_if_fail (shell_clipboard->priv != NULL);
	
	g_free (shell_clipboard->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_shell_clipboard_set_property (GObject *object,
			         guint prop_id,
			         const GValue *value,
			         GParamSpec *pspec)
{
	RBShellClipboard *shell_clipboard = RB_SHELL_CLIPBOARD (object);

	switch (prop_id)
	{
	case PROP_CLIPBOARD:
		shell_clipboard->priv->clipboard = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_shell_clipboard_get_property (GObject *object,
			         guint prop_id,
			         GValue *value,
			         GParamSpec *pspec)
{
	RBShellClipboard *shell_clipboard = RB_SHELL_CLIPBOARD (object);

	switch (prop_id)
	{
	case PROP_CLIPBOARD:
		g_value_set_object (value, shell_clipboard->priv->clipboard);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_shell_clipboard_set_clipboard (RBShellClipboard *shell_clipboard,
			          RBViewClipboard *clipboard)
{
	g_return_if_fail (RB_IS_SHELL_CLIPBOARD (shell_clipboard));
	g_return_if_fail (RB_IS_VIEW_CLIPBOARD (clipboard));

	g_object_set (G_OBJECT (shell_clipboard),
		      "clipboard", clipboard,
		      NULL);
}

RBViewClipboard *
rb_shell_clipboard_get_clipboard (RBShellClipboard *shell_clipboard)
{
	RBViewClipboard *clipboard;
	
	g_return_val_if_fail (RB_IS_SHELL_CLIPBOARD (shell_clipboard), NULL);

	g_object_get (G_OBJECT (shell_clipboard),
		      "clipboard", &clipboard,
		      NULL);

	return clipboard;
}

RBShellClipboard *
rb_shell_clipboard_new (BonoboUIComponent *component)
{
	RBShellClipboard *shell_clipboard;

	shell_clipboard = g_object_new (RB_TYPE_SHELL_CLIPBOARD, NULL);

	g_return_val_if_fail (shell_clipboard->priv != NULL, NULL);

	return shell_clipboard;
}
