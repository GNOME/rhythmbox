/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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
#include "rb-debug.h"
#include "rb-bonobo-helpers.h"
#include "rb-thread-helpers.h"

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
static void rb_shell_clipboard_sync (RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_select_all (BonoboUIComponent *component,
					       RBShellClipboard *clipboard,
					       const char *verbname);
static void rb_shell_clipboard_cmd_select_none (BonoboUIComponent *component,
						RBShellClipboard *clipboard,
						const char *verbname);
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
static void rb_shell_clipboard_cmd_sl_delete (BonoboUIComponent *component,
					      RBShellClipboard *clipboard,
					      const char *verbname);
static void rb_shell_clipboard_cmd_sl_copy (BonoboUIComponent *component,
					    RBShellClipboard *clipboard,
					    const char *verbname);
static void rb_shell_clipboard_set (RBShellClipboard *clipboard,
			            GList *nodes);
static void node_destroyed_cb (RBNode *node,
			       RBShellClipboard *clipboard);
static void rb_shell_clipboard_nodeview_changed_cb (RBNodeView *view,
						    RBShellClipboard *clipboard);

#define CMD_PATH_CUT    "/commands/Cut"
#define CMD_PATH_COPY   "/commands/Copy"
#define CMD_PATH_PASTE  "/commands/Paste"
#define CMD_PATH_DELETE "/commands/Delete"
#define CMD_PATH_SONGLIST_POPUP_PASTE "/commands/SLPaste"

struct RBShellClipboardPrivate
{
	RBSource *source;

	BonoboUIComponent *component;

	GHashTable *signal_hash;

	GList *nodes;
};

enum
{
	PROP_0,
	PROP_SOURCE,
	PROP_COMPONENT,
};

static BonoboUIVerb rb_shell_clipboard_verbs[] =
{
	BONOBO_UI_VERB ("SelectAll",(BonoboUIVerbFn) rb_shell_clipboard_cmd_select_all),
	BONOBO_UI_VERB ("SelectNone",(BonoboUIVerbFn) rb_shell_clipboard_cmd_select_none),
	BONOBO_UI_VERB ("Cut",      (BonoboUIVerbFn) rb_shell_clipboard_cmd_cut),
	BONOBO_UI_VERB ("Copy",     (BonoboUIVerbFn) rb_shell_clipboard_cmd_copy),
	BONOBO_UI_VERB ("Paste",    (BonoboUIVerbFn) rb_shell_clipboard_cmd_paste),
	BONOBO_UI_VERB ("Delete",   (BonoboUIVerbFn) rb_shell_clipboard_cmd_delete),
	BONOBO_UI_VERB ("SLDelete", (BonoboUIVerbFn) rb_shell_clipboard_cmd_sl_delete),
	BONOBO_UI_VERB ("SLCopy",   (BonoboUIVerbFn) rb_shell_clipboard_cmd_sl_copy),
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
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "RBSource",
							      "RBSource object",
							      RB_TYPE_SOURCE,
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

	shell_clipboard->priv->signal_hash = g_hash_table_new_full (g_direct_hash, g_direct_equal,
								    NULL, g_free);
}

static void
rb_shell_clipboard_finalize (GObject *object)
{
	RBShellClipboard *shell_clipboard;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHELL_CLIPBOARD (object));

	shell_clipboard = RB_SHELL_CLIPBOARD (object);

	g_return_if_fail (shell_clipboard->priv != NULL);

	g_hash_table_destroy (shell_clipboard->priv->signal_hash);

	g_list_free (shell_clipboard->priv->nodes);
	
	g_free (shell_clipboard->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_shell_clipboard_set_property (GObject *object,
			         guint prop_id,
			         const GValue *value,
			         GParamSpec *pspec)
{
	RBShellClipboard *clipboard = RB_SHELL_CLIPBOARD (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
		if (clipboard->priv->source != NULL)
		{
			RBNodeView *songs = rb_source_get_node_view (clipboard->priv->source);

			g_signal_handlers_disconnect_by_func (G_OBJECT (songs),
							      G_CALLBACK (rb_shell_clipboard_nodeview_changed_cb),
							      clipboard);
		}
		clipboard->priv->source = g_value_get_object (value);
		rb_debug ("selected source %p", g_value_get_object (value));

		rb_shell_clipboard_sync (clipboard);

		if (clipboard->priv->source != NULL)
		{
			RBNodeView *songs = rb_source_get_node_view (clipboard->priv->source);

			g_signal_connect (G_OBJECT (songs),
					  "changed",
					  G_CALLBACK (rb_shell_clipboard_nodeview_changed_cb),
					  clipboard);
		}
		break;
	case PROP_COMPONENT:
		clipboard->priv->component = g_value_get_object (value);
		bonobo_ui_component_add_verb_list_with_data (clipboard->priv->component,
							     rb_shell_clipboard_verbs,
							     clipboard);
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
	RBShellClipboard *clipboard = RB_SHELL_CLIPBOARD (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
		g_value_set_object (value, clipboard->priv->source);
		break;
	case PROP_COMPONENT:
		g_value_set_object (value, clipboard->priv->component);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_shell_clipboard_set_source (RBShellClipboard *clipboard,
			       RBSource *source)
{
	g_return_if_fail (RB_IS_SHELL_CLIPBOARD (clipboard));
	g_return_if_fail (RB_IS_SOURCE (source));

	g_object_set (G_OBJECT (clipboard), "source", source, NULL);
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
rb_shell_clipboard_sync (RBShellClipboard *clipboard)
{
	gboolean have_selection = rb_node_view_have_selection (rb_source_get_node_view (clipboard->priv->source));
	gboolean can_cut = have_selection;	
	gboolean can_paste = have_selection;
	gboolean can_delete = have_selection;	
	gboolean can_copy = have_selection;	

	rb_debug ("syncing clipboard");
	
	if (have_selection)
		can_cut = rb_source_can_cut (clipboard->priv->source);

	can_paste = rb_source_can_cut (clipboard->priv->source);

	if (have_selection)
		can_delete = rb_source_can_delete (clipboard->priv->source);
	if (have_selection)
		can_copy = rb_source_can_copy (clipboard->priv->source);

	rb_bonobo_set_sensitive (clipboard->priv->component,
				 CMD_PATH_CUT,
				 can_cut);
	rb_bonobo_set_sensitive (clipboard->priv->component,
				 CMD_PATH_DELETE,
				 can_delete);
	rb_bonobo_set_sensitive (clipboard->priv->component,
				 CMD_PATH_COPY,
				 can_copy);

	can_paste = can_paste && g_list_length (clipboard->priv->nodes) > 0;

	rb_bonobo_set_sensitive (clipboard->priv->component,
				 CMD_PATH_PASTE,
				 can_paste);
	/* We do it here because the song list view doesnt know about
	 * the global paste status */
	rb_bonobo_set_sensitive (clipboard->priv->component,
				 CMD_PATH_SONGLIST_POPUP_PASTE, can_paste);
}

static void
rb_shell_clipboard_cmd_select_all (BonoboUIComponent *component,
				   RBShellClipboard *clipboard,
				   const char *verbname)
{
	RBNodeView *nodeview;
	rb_debug ("select all");

	nodeview = rb_source_get_node_view (clipboard->priv->source);
	rb_node_view_select_all (nodeview);
}

static void
rb_shell_clipboard_cmd_select_none (BonoboUIComponent *component,
				    RBShellClipboard *clipboard,
				    const char *verbname)
{
	RBNodeView *nodeview;
	rb_debug ("select none");

	nodeview = rb_source_get_node_view (clipboard->priv->source);
	rb_node_view_select_none (nodeview);
}

static void
rb_shell_clipboard_cmd_cut (BonoboUIComponent *component,
			    RBShellClipboard *clipboard,
			    const char *verbname)
{
	rb_debug ("cut");
	rb_shell_clipboard_set (clipboard,
				rb_source_cut (clipboard->priv->source));
}

static void
rb_shell_clipboard_cmd_copy (BonoboUIComponent *component,
			     RBShellClipboard *clipboard,
			     const char *verbname)
{
	rb_debug ("copy");
	rb_shell_clipboard_set (clipboard,
				rb_source_copy (clipboard->priv->source));
}

static void
rb_shell_clipboard_cmd_paste (BonoboUIComponent *component,
			      RBShellClipboard *clipboard,
			      const char *verbname)
{
	rb_debug ("paste");
	rb_source_paste (clipboard->priv->source, clipboard->priv->nodes);
}

static void
rb_shell_clipboard_cmd_delete (BonoboUIComponent *component,
	                       RBShellClipboard *clipboard,
			       const char *verbname)
{
	rb_debug ("delete");
	rb_source_delete (clipboard->priv->source);
}

static void
rb_shell_clipboard_cmd_sl_delete (BonoboUIComponent *component,
				  RBShellClipboard *clipboard,
				  const char *verbname)
{
	rb_debug ("sl delete");
	rb_source_delete (clipboard->priv->source);
}

static void
rb_shell_clipboard_cmd_sl_copy (BonoboUIComponent *component,
				RBShellClipboard *clipboard,
				const char *verbname)
{
	rb_debug ("sl copy");
	rb_source_copy (clipboard->priv->source);
}

static void
rb_shell_clipboard_set (RBShellClipboard *clipboard,
			GList *nodes)
{
	GList *l;

	for (l = clipboard->priv->nodes; l != NULL; l = g_list_next (l))
	{
		guint *id = g_hash_table_lookup (clipboard->priv->signal_hash, l->data);
		g_assert (id);
		rb_node_signal_disconnect (l->data, *id);
		g_hash_table_remove (clipboard->priv->signal_hash, l->data);
	}

	g_list_free (clipboard->priv->nodes);

	clipboard->priv->nodes = nodes;

	for (l = nodes; l != NULL; l = g_list_next (l))
	{
		guint *id = g_new (guint, 1);
		*id = rb_node_signal_connect_object (l->data,
						     RB_NODE_DESTROY,
						     (RBNodeCallback) node_destroyed_cb,
						     G_OBJECT (clipboard));
		g_hash_table_insert (clipboard->priv->signal_hash, l->data, id);
	}
}

static void
node_destroyed_cb (RBNode *node,
		   RBShellClipboard *clipboard)
{
	rb_thread_helpers_lock_gdk ();
	clipboard->priv->nodes = g_list_remove (clipboard->priv->nodes, node);

	rb_shell_clipboard_sync (clipboard);
	rb_thread_helpers_unlock_gdk ();
}

static void
rb_shell_clipboard_nodeview_changed_cb (RBNodeView *view,
					RBShellClipboard *clipboard)
{
	rb_debug ("nodeview changed");
	rb_shell_clipboard_sync (clipboard);
}
