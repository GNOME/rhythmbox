/* 
 *  arch-tag: Implementation of Song History List
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

#include "rb-history.h"

#include "rhythmdb.h"
#include <string.h>

struct RBHistoryPrivate
{
	GList *head;
	GList *tail;
	GList *current;
	guint size;

	RhythmDB *db;

	GHashTable *entry_to_link;

	gboolean truncate_on_play;
	guint maximum_size;
};

#define MAX_HISTORY_SIZE 50

static void rb_history_class_init (RBHistoryClass *klass);
static void rb_history_init (RBHistory *shell_player);
static GObject* rb_history_constructor (GType type, guint n_construct_properties,
					GObjectConstructParam *construct_properties);
static void rb_history_finalize (GObject *object);

static void rb_history_set_property (GObject *object,
				     guint prop_id,
				     const GValue *value,
				     GParamSpec *pspec);
static void rb_history_get_property (GObject *object,
				     guint prop_id,
				     GValue *value,
				     GParamSpec *pspec);

static void rb_history_limit_size (RBHistory *hist, gboolean cut_from_beginning);

static void rb_history_song_deleted_cb (RhythmDB *view, RhythmDBEntry *entry, RBHistory *hist);

/**
 * Removes a link from the history and updates all pointers. Doesn't unref the entry, but does change the size of the list.
 */
static void rb_history_delete_link (RBHistory *hist, GList *to_delete);

enum
{
	PROP_0,
	PROP_TRUNCATE_ON_PLAY,
	PROP_DB,
	PROP_MAX_SIZE,
};

static GObjectClass *parent_class = NULL;

GType
rb_history_get_type (void)
{
	static GType rb_history_type = 0;

	if (rb_history_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBHistoryClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_history_class_init,
			NULL,
			NULL,
			sizeof (RBHistory),
			0,
			(GInstanceInitFunc) rb_history_init
		};

		rb_history_type = g_type_register_static (G_TYPE_OBJECT,
							  "RBHistory",
							  &our_info, 0);
	}

	return rb_history_type;
}

static void
rb_history_class_init (RBHistoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->constructor = rb_history_constructor;
	object_class->finalize = rb_history_finalize;

	object_class->set_property = rb_history_set_property;
	object_class->get_property = rb_history_get_property;

	g_object_class_install_property (object_class,
					 PROP_TRUNCATE_ON_PLAY,
					 g_param_spec_boolean ("truncate-on-play",
							       "Truncate on Play",
							       "Whether rb_history_set_playing() truncates the rest of the list",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_MAX_SIZE,
					 g_param_spec_uint ("maximum-size",
							    "Maximum Size",
							    "Maximum length of the history. Infinite if 0",
							    0, G_MAXUINT,
							    0,
							    G_PARAM_READWRITE));

}

RBHistory *
rb_history_new (gboolean truncate_on_play)
{
	RBHistory *history;

	history = g_object_new (RB_TYPE_HISTORY, 
				"truncate-on-play", truncate_on_play,
				NULL);

	g_return_val_if_fail (history->priv != NULL, NULL);

	return history;
}

static void
rb_history_init (RBHistory *hist)
{
	hist->priv = g_new0 (RBHistoryPrivate, 1);

	hist->priv->entry_to_link = g_hash_table_new (g_direct_hash,
						      g_direct_equal);
}

static GObject*
rb_history_constructor (GType type, guint n_construct_properties,
			GObjectConstructParam *construct_properties)
{
	RBHistory *history;

	history = RB_HISTORY (parent_class->constructor (type, n_construct_properties,
				construct_properties));

	return G_OBJECT (history);
}

static void 
rb_history_finalize (GObject *object)
{
	RBHistory *hist;
	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_HISTORY (object));

	hist = RB_HISTORY (object);

	if (hist->priv->db)
		g_signal_handlers_disconnect_by_func (G_OBJECT (hist->priv->db),
						      G_CALLBACK (rb_history_song_deleted_cb),
						      hist);
	
	/* unref all of the stored entries */
	rb_history_clear (hist);

	g_hash_table_destroy (hist->priv->entry_to_link);
	g_list_free (hist->priv->head);
	g_free (hist->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_history_set_property (GObject *object,
			 guint prop_id,
			 const GValue *value,
			 GParamSpec *pspec)
{
	RBHistory *hist = RB_HISTORY (object);

	switch (prop_id)
	{
	case PROP_TRUNCATE_ON_PLAY: {
		hist->priv->truncate_on_play = g_value_get_boolean (value);
		} break;
	case PROP_DB: {
		RhythmDB *db = g_value_get_object (value);

		if (db != hist->priv->db) {
			if (hist->priv->db) {
				g_signal_handlers_disconnect_by_func (G_OBJECT (hist->priv->db),
								      G_CALLBACK (rb_history_song_deleted_cb),
								      hist);
				/* If the db changes, clear the history so it
				 * doesn't try to unref an entry from the wrong
				 * db */
				rb_history_clear (hist);
			}
			hist->priv->db = db;
			if (hist->priv->db) {
				g_signal_connect (G_OBJECT (hist->priv->db),
						  "entry_deleted",
						  G_CALLBACK (rb_history_song_deleted_cb),
						  hist);
			}
		}

		} break;
	case PROP_MAX_SIZE: {
		hist->priv->maximum_size = g_value_get_uint (value);
		rb_history_limit_size (hist, TRUE);
		} break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_history_get_property (GObject *object,
			 guint prop_id,
			 GValue *value,
			 GParamSpec *pspec)
{
	RBHistory *hist = RB_HISTORY (object);

	switch (prop_id)
	{
	case PROP_TRUNCATE_ON_PLAY: {
		g_value_set_boolean (value, hist->priv->truncate_on_play);
		} break;
	case PROP_DB: {
		g_value_set_object (value, hist->priv->db);
		} break;
	case PROP_MAX_SIZE: {
		g_value_set_uint (value, hist->priv->maximum_size);
		} break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}

}

guint
rb_history_length (RBHistory *hist)
{
    return hist->priv->size;
}

RhythmDBEntry *
rb_history_first (RBHistory *hist)
{
	return hist->priv->head ? hist->priv->head->data : NULL;
}

RhythmDBEntry *
rb_history_previous (RBHistory *hist)
{
	if (hist->priv->current == hist->priv->head)
		return NULL;
	
	return g_list_previous (hist->priv->current)->data;
}

RhythmDBEntry * 
rb_history_current (RBHistory *hist)
{
	return hist->priv->current ? hist->priv->current->data : NULL;
}

RhythmDBEntry * 
rb_history_next (RBHistory *hist)
{
	if (hist->priv->current == hist->priv->tail)
		return NULL;
	
	return g_list_next (hist->priv->current)->data;
}

RhythmDBEntry *
rb_history_last (RBHistory *hist)
{
	return hist->priv->tail ? hist->priv->tail->data : NULL;
}

void
rb_history_go_first (RBHistory *hist)
{
	hist->priv->current = hist->priv->head;
}

void
rb_history_go_previous (RBHistory *hist)
{
	if (hist->priv->current == hist->priv->head)
		return;

	hist->priv->current = hist->priv->current->prev;
}

void
rb_history_go_next (RBHistory *hist)
{
	if (hist->priv->current == hist->priv->tail)
		return;

	hist->priv->current = hist->priv->current->next;
}

void
rb_history_go_last (RBHistory *hist)
{
	hist->priv->current = hist->priv->tail;
}


void
rb_history_set_playing (RBHistory *hist, RhythmDBEntry *entry)
{
	g_return_if_fail (hist->priv->db != NULL);
	rhythmdb_entry_ref (hist->priv->db, entry);
	hist->priv->size++;
	if (hist->priv->head == NULL) {
		hist->priv->head = hist->priv->tail = hist->priv->current = g_list_append (NULL, entry);
		g_hash_table_insert (hist->priv->entry_to_link, entry, hist->priv->current);
	} else {
		rb_history_remove_entry (hist, entry);

		g_list_insert_before (hist->priv->current, hist->priv->current->next, entry);
		if (hist->priv->tail == hist->priv->current)
			hist->priv->tail = hist->priv->current->next;
		hist->priv->current = hist->priv->current->next;
		g_hash_table_insert (hist->priv->entry_to_link, entry, hist->priv->current);
		if (hist->priv->truncate_on_play) {
			while (hist->priv->current->next) {
				rb_history_remove_entry (hist, hist->priv->current->next->data);
			}
		}
	}
	rb_history_limit_size (hist, TRUE);
}

void
rb_history_append (RBHistory *hist, RhythmDBEntry *entry)
{
	g_return_if_fail (hist->priv->db != NULL);
	rhythmdb_entry_ref (hist->priv->db, entry);
	rb_history_remove_entry (hist, entry);
	hist->priv->size++;
	if (hist->priv->head == NULL)
		hist->priv->head = hist->priv->tail = hist->priv->current = g_list_append (NULL, entry);
	else {
		g_list_append (hist->priv->tail, entry);
		hist->priv->tail = hist->priv->tail->next;
	}
	g_hash_table_insert (hist->priv->entry_to_link, entry, hist->priv->tail);
	rb_history_limit_size (hist, TRUE);
}

guint
rb_history_get_current_index (RBHistory *hist)
{
	guint result = 0;
	GList *cur = hist->priv->head;
	while (cur != hist->priv->current) {
		result++;
		cur = cur->next;
	}
	return result;
}

void
rb_history_insert_at_index (RBHistory *hist, RhythmDBEntry *entry, guint index)
{
	g_return_if_fail (hist->priv->db != NULL);
	g_return_if_fail (index <= hist->priv->size);

	rhythmdb_entry_ref (hist->priv->db, entry);
	rb_history_remove_entry (hist, entry);
	hist->priv->size++;

	if (hist->priv->head == NULL) {
		hist->priv->head = hist->priv->tail = hist->priv->current = g_list_append (NULL, entry);
		g_hash_table_insert (hist->priv->entry_to_link, entry, hist->priv->tail);
	} else if (index == hist->priv->size) {
		g_list_append (hist->priv->tail, entry);
		hist->priv->tail = hist->priv->tail->next;
		g_hash_table_insert (hist->priv->entry_to_link, entry, hist->priv->tail);
	} else {
		guint i;
		GList *insert_loc = hist->priv->head;
		for (i=0; i<index; ++i)
			insert_loc = insert_loc->next;
		hist->priv->head = g_list_insert_before (hist->priv->head, insert_loc, entry);
		g_hash_table_insert (hist->priv->entry_to_link, entry, insert_loc->prev);
	}
	rb_history_limit_size (hist, TRUE);
}

/**
 * Cuts nodes off of the history from the desired end until it is smaller than max_size. 
 * Never cuts off the current node.
 */
static void
rb_history_limit_size (RBHistory *hist, gboolean cut_from_beginning)
{
	if (hist->priv->maximum_size != 0) 
		while (hist->priv->size > hist->priv->maximum_size) {
			if (cut_from_beginning 
					|| hist->priv->current == hist->priv->tail) {
				rb_history_remove_entry (hist, hist->priv->head->data);
			} else {
				rb_history_remove_entry (hist, hist->priv->tail->data);
			}
		}
}

void
rb_history_remove_entry (RBHistory *hist, RhythmDBEntry *entry)
{
	GList *to_delete = g_hash_table_lookup (hist->priv->entry_to_link, entry);
	if (to_delete) {
		g_return_if_fail (hist->priv->db != NULL);
		g_hash_table_remove (hist->priv->entry_to_link, entry);
		rhythmdb_entry_unref (hist->priv->db, entry);

		rb_history_delete_link (hist, to_delete);
	}
}

static void
rb_history_delete_link (RBHistory *hist, GList *to_delete)
{
	/* There are 3 pointers into the GList, and we need to
	 * make sure they still point to the right places after
	 * deleting *to_delete.
	 * It's pretty messy. If you see a better structure for
	 * the if structure, please fix it.
	 */
	if (to_delete == hist->priv->head) {
		if (to_delete == hist->priv->tail) {
			hist->priv->head = 
				hist->priv->current =
				hist->priv->tail = g_list_delete_link (hist->priv->head, to_delete);
		} else if (to_delete == hist->priv->current) {
			hist->priv->head = 
				hist->priv->current = g_list_delete_link (hist->priv->head, to_delete);
		} else {
			hist->priv->head = g_list_delete_link (hist->priv->head, to_delete);
		}
	} else if (to_delete == hist->priv->current) {
		if (to_delete == hist->priv->tail) {
			hist->priv->current = hist->priv->tail = g_list_previous (hist->priv->tail);
			g_list_delete_link (hist->priv->head, to_delete);
		} else {
			hist->priv->current = g_list_next (hist->priv->current);
			g_list_delete_link (hist->priv->head, to_delete);
		}
	} else if (to_delete == hist->priv->tail) {
		hist->priv->tail = g_list_previous (hist->priv->tail);
		g_list_delete_link (hist->priv->head, to_delete);
	} else {
		g_list_delete_link (hist->priv->head, to_delete);
	}

	hist->priv->size--;
}

static void
rb_history_song_deleted_cb (RhythmDB *db, RhythmDBEntry *entry,
			    RBHistory *hist)
{
	rb_history_remove_entry (hist, entry);
}

void
rb_history_clear (RBHistory *hist)
{
	while (hist->priv->head != NULL)
		rb_history_remove_entry (hist, hist->priv->head->data);
	if (hist->priv->size != 0)
		g_error ("RBHistory instance is corrupt");
}

GPtrArray *
rb_history_dump (RBHistory *hist)
{
	GList *cur;
	GPtrArray *result = g_ptr_array_sized_new (hist->priv->size);
	for (cur = hist->priv->head; cur != NULL; cur = cur->next) {
		g_ptr_array_add (result, cur->data);
	}
	return result;
}
