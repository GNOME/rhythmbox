/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * 
 *  arch-tag: Implementation of song cut/paste handler object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
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

#include <config.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-shell-clipboard.h"
#include "rhythmdb.h"
#include "rb-debug.h"

static void rb_shell_clipboard_class_init (RBShellClipboardClass *klass);
static void rb_shell_clipboard_init (RBShellClipboard *shell_clipboard);
static GObject *rb_shell_clipboard_constructor (GType type, guint n_construct_properties,
						GObjectConstructParam *construct_properties);
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
static void rb_shell_clipboard_cmd_select_all (GtkAction *action,
					       RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_select_none (GtkAction *action,
						RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_cut (GtkAction *action,
			                RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_copy (GtkAction *action,
			                 RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_paste (GtkAction *action,
			                  RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_delete (GtkAction *action,
					   RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_move_to_trash (GtkAction *action,
						  RBShellClipboard *clipboard);
static void rb_shell_clipboard_set (RBShellClipboard *clipboard,
			            GList *nodes);
static gboolean rb_shell_clipboard_idle_poll_deletions (RBShellClipboard *clipboard);
static void rb_shell_clipboard_entry_deleted_cb (RhythmDB *db,
						 RhythmDBEntry *entry,
						 RBShellClipboard *clipboard);
static void rb_shell_clipboard_entryview_changed_cb (RBEntryView *view,
						     RBShellClipboard *clipboard);

struct RBShellClipboardPrivate
{
	RhythmDB *db;
	RBSource *source;

	GtkActionGroup *actiongroup;

	GHashTable *signal_hash;

	GAsyncQueue *deleted_queue;

	guint idle_sync_id;

	GList *entries;
};

#define RB_SHELL_CLIPBOARD_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SHELL_CLIPBOARD, RBShellClipboardPrivate))

enum
{
	PROP_0,
	PROP_SOURCE,
	PROP_ACTION_GROUP,
	PROP_DB,
};

static GtkActionEntry rb_shell_clipboard_actions [] =
{
	{ "EditSelectAll", NULL, N_("Select _All"), "<control>A",
	  N_("Select all songs"),
	  G_CALLBACK (rb_shell_clipboard_cmd_select_all) },
	{ "EditSelectNone", NULL, N_("D_eselect All"), "<shift><control>A",
	  N_("Deselect all songs"),
	  G_CALLBACK (rb_shell_clipboard_cmd_select_none) },
	{ "EditCut", GTK_STOCK_CUT, N_("Cu_t"), "<control>X",
	  N_("Cut selection"),
	  G_CALLBACK (rb_shell_clipboard_cmd_cut) },
	{ "EditCopy", GTK_STOCK_COPY, N_("_Copy"), "<control>C",
	  N_("Copy selection"),
	  G_CALLBACK (rb_shell_clipboard_cmd_copy) },
	{ "EditPaste", GTK_STOCK_PASTE, N_("_Paste"), "<control>V",
	  N_("Paste selection"),
	  G_CALLBACK (rb_shell_clipboard_cmd_paste) },
	{ "EditDelete", NULL, N_("_Delete"), NULL,
	  N_("Delete selection"),
	  G_CALLBACK (rb_shell_clipboard_cmd_delete) },
	{ "EditMovetoTrash", NULL, N_("_Move To Trash"), "<control>T",
	  N_("Move selection to the trash"),
	  G_CALLBACK (rb_shell_clipboard_cmd_move_to_trash) },
};
static guint rb_shell_clipboard_n_actions = G_N_ELEMENTS (rb_shell_clipboard_actions);

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
	object_class->constructor = rb_shell_clipboard_constructor;

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
					 PROP_ACTION_GROUP,
					 g_param_spec_object ("action-group",
							      "GtkActionGroup",
							      "GtkActionGroup object",
							      GTK_TYPE_ACTION_GROUP,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBShellClipboardPrivate));
}

static void
rb_shell_clipboard_init (RBShellClipboard *shell_clipboard)
{
	shell_clipboard->priv = RB_SHELL_CLIPBOARD_GET_PRIVATE (shell_clipboard);

	shell_clipboard->priv->signal_hash = g_hash_table_new_full (g_direct_hash, g_direct_equal,
								    NULL, g_free);

	shell_clipboard->priv->deleted_queue = g_async_queue_new ();

	shell_clipboard->priv->idle_sync_id = g_idle_add ((GSourceFunc) rb_shell_clipboard_idle_poll_deletions, shell_clipboard); 
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

	g_async_queue_unref (shell_clipboard->priv->deleted_queue);

	if (shell_clipboard->priv->idle_sync_id)
		g_source_remove (shell_clipboard->priv->idle_sync_id);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GObject *
rb_shell_clipboard_constructor (GType type, guint n_construct_properties,
				GObjectConstructParam *construct_properties)
{
	RBShellClipboard *clip;
	RBShellClipboardClass *klass;
	GObjectClass *parent_class;  

	klass = RB_SHELL_CLIPBOARD_CLASS (g_type_class_peek (RB_TYPE_SHELL_CLIPBOARD));

	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	clip = RB_SHELL_CLIPBOARD (parent_class->constructor (type,
							      n_construct_properties,
							      construct_properties));
	
	g_signal_connect_object (G_OBJECT (clip->priv->db),
				 "entry_deleted",
				 G_CALLBACK (rb_shell_clipboard_entry_deleted_cb),
				 clip, 0);

	return G_OBJECT (clip);
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

			g_signal_connect_object (G_OBJECT (songs),
						 "changed",
						 G_CALLBACK (rb_shell_clipboard_entryview_changed_cb),
						 clipboard, 0);
		}
		break;
	case PROP_ACTION_GROUP:
		clipboard->priv->actiongroup = g_value_get_object (value);
		gtk_action_group_add_actions (clipboard->priv->actiongroup,
					      rb_shell_clipboard_actions,
					      rb_shell_clipboard_n_actions,
					      clipboard);
		break;
	case PROP_DB:
		clipboard->priv->db = g_value_get_object (value);
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
	case PROP_ACTION_GROUP:
		g_value_set_object (value, clipboard->priv->actiongroup);
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
rb_shell_clipboard_new (GtkActionGroup *actiongroup, RhythmDB *db)
{
	return g_object_new (RB_TYPE_SHELL_CLIPBOARD,
			     "action-group", actiongroup,
			     "db", db,
			     NULL);
}

static void
rb_shell_clipboard_sync (RBShellClipboard *clipboard)
{
	gboolean have_selection;
	gboolean can_cut;
	gboolean can_paste;
	gboolean can_delete;
	gboolean can_copy;
	gboolean can_move_to_trash;
	GtkAction *action;

	if (!clipboard->priv->source)
		return;

	have_selection = rb_entry_view_have_selection (rb_source_get_entry_view (clipboard->priv->source));
	can_cut = have_selection;	
	can_paste = have_selection;
	can_delete = have_selection;	
	can_copy = have_selection;
	can_move_to_trash = have_selection;

	rb_debug ("syncing clipboard");
	
	if (have_selection)
		can_cut = rb_source_can_cut (clipboard->priv->source);

	can_paste = rb_source_can_cut (clipboard->priv->source);

	if (have_selection)
		can_delete = rb_source_can_delete (clipboard->priv->source);
	if (have_selection)
		can_copy = rb_source_can_copy (clipboard->priv->source);
	if (have_selection)
		can_move_to_trash = rb_source_can_move_to_trash (clipboard->priv->source);

	action = gtk_action_group_get_action (clipboard->priv->actiongroup,
					      "EditCut");
	g_object_set (G_OBJECT (action), "sensitive", can_cut, NULL);
	action = gtk_action_group_get_action (clipboard->priv->actiongroup,
					      "EditDelete");
	g_object_set (G_OBJECT (action), "sensitive", can_delete, NULL);
	action = gtk_action_group_get_action (clipboard->priv->actiongroup,
					      "EditMovetoTrash");
	g_object_set (G_OBJECT (action), "sensitive", can_move_to_trash, NULL);
	action = gtk_action_group_get_action (clipboard->priv->actiongroup,
					      "EditCopy");
	g_object_set (G_OBJECT (action), "sensitive", can_copy, NULL);

	can_paste = can_paste && g_list_length (clipboard->priv->entries) > 0;

	action = gtk_action_group_get_action (clipboard->priv->actiongroup,
					      "EditPaste");
	g_object_set (G_OBJECT (action), "sensitive", can_paste, NULL);
}

static void
rb_shell_clipboard_cmd_select_all (GtkAction *action,
				   RBShellClipboard *clipboard)
{
	RBEntryView *entryview;
	rb_debug ("select all");

	entryview = rb_source_get_entry_view (clipboard->priv->source);
	rb_entry_view_select_all (entryview);
}

static void
rb_shell_clipboard_cmd_select_none (GtkAction *action,
				    RBShellClipboard *clipboard)
{
	RBEntryView *entryview;
	rb_debug ("select none");

	entryview = rb_source_get_entry_view (clipboard->priv->source);
	rb_entry_view_select_none (entryview);
}

static void
rb_shell_clipboard_cmd_cut (GtkAction *action,
			    RBShellClipboard *clipboard)
{
	rb_debug ("cut");
	rb_shell_clipboard_set (clipboard,
				rb_source_cut (clipboard->priv->source));
}

static void
rb_shell_clipboard_cmd_copy (GtkAction *action,
			     RBShellClipboard *clipboard)
{
	rb_debug ("copy");
	rb_shell_clipboard_set (clipboard,
				rb_source_copy (clipboard->priv->source));
}

static void
rb_shell_clipboard_cmd_paste (GtkAction *action,
			      RBShellClipboard *clipboard)
{
	rb_debug ("paste");
	rb_source_paste (clipboard->priv->source, clipboard->priv->entries);
}

static void
rb_shell_clipboard_cmd_delete (GtkAction *action,
	                       RBShellClipboard *clipboard)
{
	rb_debug ("delete");
	rb_source_delete (clipboard->priv->source);
}

static void
rb_shell_clipboard_cmd_move_to_trash (GtkAction *action,
				      RBShellClipboard *clipboard)
{
	rb_debug ("movetotrash");
	rb_source_move_to_trash (clipboard->priv->source);
}

static void
rb_shell_clipboard_set (RBShellClipboard *clipboard,
			GList *entries)
{
	g_list_free (clipboard->priv->entries);

	clipboard->priv->entries = entries;
}

static gboolean
rb_shell_clipboard_process_deletions (RBShellClipboard *clipboard)
{
	RhythmDBEntry *entry;

	if (clipboard->priv->entries) {
		GList *tem, *finished = NULL;
		gboolean processed = FALSE;

		while ((entry = g_async_queue_try_pop (clipboard->priv->deleted_queue)) != NULL) {
			clipboard->priv->entries = g_list_remove (clipboard->priv->entries, entry);
			finished = g_list_prepend (finished, entry);
			processed = TRUE;
		}

		if (processed)
			rb_shell_clipboard_sync (clipboard);

		for (tem = finished; tem; tem = tem->next)
			rhythmdb_entry_unref (clipboard->priv->db, tem->data);
		g_list_free (finished);

		return processed;
	} else {
		/* Fast path for when there's nothing in the clipboard */
		while ((entry = g_async_queue_try_pop (clipboard->priv->deleted_queue)) != NULL)
			rhythmdb_entry_unref (clipboard->priv->db, entry);
		return FALSE;
	}
}

static gboolean
rb_shell_clipboard_idle_poll_deletions (RBShellClipboard *clipboard)
{
	gboolean did_sync;

	GDK_THREADS_ENTER ();

	did_sync = rb_shell_clipboard_process_deletions (clipboard);

	if (did_sync)
		clipboard->priv->idle_sync_id =
			g_idle_add_full (G_PRIORITY_LOW,
					 (GSourceFunc) rb_shell_clipboard_idle_poll_deletions,
					 clipboard, NULL);
	else
		clipboard->priv->idle_sync_id =
			g_timeout_add (300,
				       (GSourceFunc) rb_shell_clipboard_idle_poll_deletions,
				       clipboard);


	GDK_THREADS_LEAVE ();

	return FALSE;
}

static void
rb_shell_clipboard_entry_deleted_cb (RhythmDB *db,
				     RhythmDBEntry *entry,
				     RBShellClipboard *clipboard)
{
	rhythmdb_entry_ref (db, entry);
	g_async_queue_push (clipboard->priv->deleted_queue, entry);
}

static void
rb_shell_clipboard_entryview_changed_cb (RBEntryView *view,
					 RBShellClipboard *clipboard)
{
	rb_debug ("entryview changed");
	rb_shell_clipboard_sync (clipboard);
}
