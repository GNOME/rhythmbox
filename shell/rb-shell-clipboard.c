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
#include "rb-node.h"
#include "rb-bonobo-helpers.h"

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
static void rb_view_clipboard_changed_cb (RBViewClipboard *clipboard,
			                  RBShellClipboard *shell_clipboard);
static void rb_shell_clipboard_sync (RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_cut (BonoboUIComponent *component,
			                RBShellClipboard *clipboard,
			                const char *verbname);
static void rb_shell_clipboard_cmd_copy (BonoboUIComponent *component,
			                 RBShellClipboard *clipboard,
			                 const char *verbname);
static void rb_shell_clipboard_cmd_paste (BonoboUIComponent *component,
			                  RBShellClipboard *clipboard,
			                  const char *verbname);
static void rb_shell_clipboard_cmd_delete (BonoboUIComponent *component,
					   RBShellClipboard *clipboard,
					   const char *verbname);
static void rb_shell_clipboard_cmd_song_info (BonoboUIComponent *component,
				              RBShellClipboard *clipboard,
				              const char *verbname);
static void rb_shell_clipboard_set (RBShellClipboard *clipboard,
			            GList *nodes);
static void rb_node_destroyed_cb (RBNode *node,
		                  RBShellClipboard *clipboard);

#define CMD_PATH_CUT    "/commands/Cut"
#define CMD_PATH_COPY   "/commands/Copy"
#define CMD_PATH_PASTE  "/commands/Paste"
#define CMD_PATH_DELETE "/commands/Delete"
#define CMD_PATH_SONGLIST_POPUP_PASTE "/commands/SLPaste"

struct RBShellClipboardPrivate
{
	RBViewClipboard *clipboard;

	BonoboUIComponent *component;

	GList *nodes;
};

enum
{
	PROP_0,
	PROP_CLIPBOARD,
	PROP_COMPONENT
};

static BonoboUIVerb rb_shell_clipboard_verbs[] =
{
	BONOBO_UI_VERB ("Cut",      (BonoboUIVerbFn) rb_shell_clipboard_cmd_cut),
	BONOBO_UI_VERB ("Copy",     (BonoboUIVerbFn) rb_shell_clipboard_cmd_copy),
	BONOBO_UI_VERB ("Paste",    (BonoboUIVerbFn) rb_shell_clipboard_cmd_paste),
	BONOBO_UI_VERB ("Delete",   (BonoboUIVerbFn) rb_shell_clipboard_cmd_delete),
	BONOBO_UI_VERB ("SongInfo", (BonoboUIVerbFn) rb_shell_clipboard_cmd_song_info),
	BONOBO_UI_VERB_END
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
	g_object_class_install_property (object_class,
					 PROP_COMPONENT,
					 g_param_spec_object ("component",
							      "BonoboUIComponent",
							      "BonoboUIComponent object",
							      BONOBO_TYPE_UI_COMPONENT,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
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

	g_list_free (shell_clipboard->priv->nodes);
	
	g_free (shell_clipboard->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_view_paste_request_cb (RBViewClipboard *clipboard,
			  RBShellClipboard *shell_clipboard)
{
	rb_view_clipboard_paste (shell_clipboard->priv->clipboard, 
			         shell_clipboard->priv->nodes);
}

static void
rb_view_clipboard_set_cb (RBViewClipboard *clipboard,
			  GList *selection,
			  RBShellClipboard *shell_clipboard)
{
	rb_shell_clipboard_set (shell_clipboard, g_list_copy (selection));
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
		if (shell_clipboard->priv->clipboard != NULL)
		{
			g_signal_handlers_disconnect_by_func (G_OBJECT (shell_clipboard->priv->clipboard),
							      G_CALLBACK (rb_view_clipboard_changed_cb),
							      shell_clipboard);
		}
		
		shell_clipboard->priv->clipboard = g_value_get_object (value);

		if (shell_clipboard->priv->clipboard != NULL)
		{
			g_signal_connect (G_OBJECT (shell_clipboard->priv->clipboard),
					  "clipboard_changed",
					  G_CALLBACK (rb_view_clipboard_changed_cb),
					  shell_clipboard);
			g_signal_connect (G_OBJECT (shell_clipboard->priv->clipboard),
					  "set_clipboard",
					  G_CALLBACK (rb_view_clipboard_set_cb),
					  shell_clipboard);
			g_signal_connect (G_OBJECT (shell_clipboard->priv->clipboard),
					  "paste_request",
					  G_CALLBACK (rb_view_paste_request_cb),
					  shell_clipboard);
		}

		rb_shell_clipboard_sync (shell_clipboard);
		break;
	case PROP_COMPONENT:
		shell_clipboard->priv->component = g_value_get_object (value);
		bonobo_ui_component_add_verb_list_with_data (shell_clipboard->priv->component,
							     rb_shell_clipboard_verbs,
							     shell_clipboard);
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
	case PROP_COMPONENT:
		g_value_set_object (value, shell_clipboard->priv->component);
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
	g_return_val_if_fail (RB_IS_SHELL_CLIPBOARD (shell_clipboard), NULL);

	return shell_clipboard->priv->clipboard;
}

RBShellClipboard *
rb_shell_clipboard_new (BonoboUIComponent *component)
{
	RBShellClipboard *shell_clipboard;

	shell_clipboard = g_object_new (RB_TYPE_SHELL_CLIPBOARD,
					"component", component,
					NULL);

	g_return_val_if_fail (shell_clipboard->priv != NULL, NULL);

	return shell_clipboard;
}

static void
rb_view_clipboard_changed_cb (RBViewClipboard *clipboard,
			      RBShellClipboard *shell_clipboard)
{
	rb_shell_clipboard_sync (shell_clipboard);
}

static void
rb_shell_clipboard_sync (RBShellClipboard *clipboard)
{
	gboolean can_paste;
	
	rb_bonobo_set_sensitive (clipboard->priv->component,
				 CMD_PATH_CUT,
				 rb_view_clipboard_can_cut (clipboard->priv->clipboard));
	rb_bonobo_set_sensitive (clipboard->priv->component,
				 CMD_PATH_COPY,
				 rb_view_clipboard_can_copy (clipboard->priv->clipboard));
	rb_bonobo_set_sensitive (clipboard->priv->component,
				 CMD_PATH_DELETE,
				 rb_view_clipboard_can_delete (clipboard->priv->clipboard));

	can_paste = rb_view_clipboard_can_paste (clipboard->priv->clipboard) &&
		g_list_length (clipboard->priv->nodes) > 0;
	
	rb_bonobo_set_sensitive (clipboard->priv->component,
				 CMD_PATH_PASTE, can_paste);
	/* We do it here because the song list view doesnt know about
	 * the global paste status */
	rb_bonobo_set_sensitive (clipboard->priv->component,
				 CMD_PATH_SONGLIST_POPUP_PASTE, can_paste);
}

static void
rb_shell_clipboard_cmd_cut (BonoboUIComponent *component,
			    RBShellClipboard *clipboard,
			    const char *verbname)
{
	rb_shell_clipboard_set (clipboard,
				rb_view_clipboard_cut (clipboard->priv->clipboard));
}

static void
rb_shell_clipboard_cmd_copy (BonoboUIComponent *component,
			     RBShellClipboard *clipboard,
			     const char *verbname)
{
	rb_shell_clipboard_set (clipboard,
				rb_view_clipboard_copy (clipboard->priv->clipboard));
}

static void
rb_shell_clipboard_cmd_paste (BonoboUIComponent *component,
			      RBShellClipboard *clipboard,
			      const char *verbname)
{
	rb_view_clipboard_paste (clipboard->priv->clipboard, clipboard->priv->nodes);
}

static void
rb_shell_clipboard_cmd_delete (BonoboUIComponent *component,
	                       RBShellClipboard *clipboard,
			       const char *verbname)
{
	rb_view_clipboard_delete (clipboard->priv->clipboard);
}

static void
rb_shell_clipboard_cmd_song_info (BonoboUIComponent *component,
				  RBShellClipboard *clipboard,
				  const char *verbname)
{
	rb_view_clipboard_song_info (clipboard->priv->clipboard);
}

static void
rb_shell_clipboard_set (RBShellClipboard *clipboard,
			GList *nodes)
{
	GList *l;

	for (l = clipboard->priv->nodes; l != NULL; l = g_list_next (l))
	{
		g_signal_handlers_disconnect_by_func (G_OBJECT (l->data),
						      G_CALLBACK (rb_node_destroyed_cb),
						      clipboard);
	}

	g_list_free (clipboard->priv->nodes);

	clipboard->priv->nodes = nodes;

	for (l = nodes; l != NULL; l = g_list_next (l))
	{
		g_signal_connect_object (G_OBJECT (l->data),
				         "destroyed",
				         G_CALLBACK (rb_node_destroyed_cb),
				         G_OBJECT (clipboard),
					 0);
	}
}

static void
rb_node_destroyed_cb (RBNode *node,
		      RBShellClipboard *clipboard)
{
	clipboard->priv->nodes = g_list_remove (clipboard->priv->nodes, node);

	rb_shell_clipboard_sync (clipboard);
}
