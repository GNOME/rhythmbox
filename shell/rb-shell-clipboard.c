/* 
 *  arch-tag: Implementation of song cut/paste handler object
 *
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
 */

#include "rb-shell-clipboard.h"
#include "rhythmdb.h"
#include "rb-debug.h"
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
static void entry_deleted_cb (RhythmDB *db,
			      RhythmDBEntry *entry,
			      RBShellClipboard *clipboard);
static void rb_shell_clipboard_entryview_changed_cb (RBEntryView *view,
						     RBShellClipboard *clipboard);

#define CMD_PATH_CUT    "/commands/Cut"
#define CMD_PATH_COPY   "/commands/Copy"
#define CMD_PATH_PASTE  "/commands/Paste"
#define CMD_PATH_DELETE "/commands/Delete"
#define CMD_PATH_SONGLIST_POPUP_PASTE "/commands/SLPaste"

struct RBShellClipboardPrivate
{
	RhythmDB *db;
	RBSource *source;

	BonoboUIComponent *component;

	GHashTable *signal_hash;

	GList *entries;
};

enum
{
	PROP_0,
	PROP_SOURCE,
	PROP_COMPONENT,
	PROP_DB,
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
	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
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

	g_list_free (shell_clipboard->priv->entries);
	
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
			RBEntryView *songs = rb_source_get_entry_view (clipboard->priv->source);

			g_signal_handlers_disconnect_by_func (G_OBJECT (songs),
							      G_CALLBACK (rb_shell_clipboard_entryview_changed_cb),
							      clipboard);
		}
		clipboard->priv->source = g_value_get_object (value);
		rb_debug ("selected source %p", g_value_get_object (value));

		rb_shell_clipboard_sync (clipboard);

		if (clipboard->priv->source != NULL)
		{
			RBEntryView *songs = rb_source_get_entry_view (clipboard->priv->source);

			g_signal_connect (G_OBJECT (songs),
					  "changed",
					  G_CALLBACK (rb_shell_clipboard_entryview_changed_cb),
					  clipboard);
		}
		break;
	case PROP_COMPONENT:
		clipboard->priv->component = g_value_get_object (value);
		bonobo_ui_component_add_verb_list_with_data (clipboard->priv->component,
							     rb_shell_clipboard_verbs,
							     clipboard);
		break;
	case PROP_DB:
		clipboard->priv->db = g_value_get_object (value);
		g_signal_connect_object (G_OBJECT (clipboard->priv->db),
					 "entry_deleted",
					 G_CALLBACK (entry_deleted_cb),
					 clipboard, 0);
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
	case PROP_DB:
		g_value_set_object (value, clipboard->priv->db);
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
rb_shell_clipboard_new (BonoboUIComponent *component, RhythmDB *db)
{
	RBShellClipboard *shell_clipboard;

	shell_clipboard = g_object_new (RB_TYPE_SHELL_CLIPBOARD,
					"component", component,
					"db", db,
					NULL);

	g_return_val_if_fail (shell_clipboard->priv != NULL, NULL);

	return shell_clipboard;
}

static void
rb_shell_clipboard_sync (RBShellClipboard *clipboard)
{
	gboolean have_selection;
	gboolean can_cut;	
	gboolean can_paste;
	gboolean can_delete;	
	gboolean can_copy;	

	if (!clipboard->priv->source)
		return;

	have_selection = rb_entry_view_have_selection (rb_source_get_entry_view (clipboard->priv->source));
	can_cut = have_selection;	
	can_paste = have_selection;
	can_delete = have_selection;	
	can_copy = have_selection;	

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

	can_paste = can_paste && g_list_length (clipboard->priv->entries) > 0;

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
	RBEntryView *entryview;
	rb_debug ("select all");

	entryview = rb_source_get_entry_view (clipboard->priv->source);
	rb_entry_view_select_all (entryview);
}

static void
rb_shell_clipboard_cmd_select_none (BonoboUIComponent *component,
				    RBShellClipboard *clipboard,
				    const char *verbname)
{
	RBEntryView *entryview;
	rb_debug ("select none");

	entryview = rb_source_get_entry_view (clipboard->priv->source);
	rb_entry_view_select_none (entryview);
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
	rb_source_paste (clipboard->priv->source, clipboard->priv->entries);
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
			GList *entries)
{
	g_list_free (clipboard->priv->entries);

	clipboard->priv->entries = entries;
}

static void
entry_deleted_cb (RhythmDB *db,
		  RhythmDBEntry *entry,
		  RBShellClipboard *clipboard)
{
	clipboard->priv->entries = g_list_remove (clipboard->priv->entries, entry);

	rb_shell_clipboard_sync (clipboard);
}

static void
rb_shell_clipboard_entryview_changed_cb (RBEntryView *view,
					 RBShellClipboard *clipboard)
{
	rb_debug ("entryview changed");
	rb_shell_clipboard_sync (clipboard);
}
