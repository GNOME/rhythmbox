/* 
 *  arch-tag: Implementation of RhythmDB - Rhythmbox backend queryable database
 *
 *  Copyright (C) 2003 Colin Walters <cwalters@gnome.org>
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

#include "rhythmdb.h"
#include "rhythmdb-model.h"
#include "rhythmdb-query-model.h"
#include "rhythmdb-property-model.h"
#include <string.h>
#include <gobject/gvaluecollector.h>
#include <libgnome/gnome-i18n.h>
#include "rb-string-helpers.h"
#include "rb-thread-helpers.h"
#include "rb-debug.h"
#include "rb-cut-and-paste-code.h"

struct RhythmDBPrivate
{
	char *name;

	GStaticRWLock lock;

	GType *column_types;

	GThread *load_thread;

	GThreadPool *query_thread_pool;
	GThreadPool *property_query_thread_pool;

	GHashTable *changed_entries;

	GMutex *exit_mutex;
	gboolean exiting;
};

struct RhythmDBQueryThreadData
{
	GPtrArray *query;
	guint propid;
	GtkTreeModel *main_model;
	gboolean lock;
	gboolean cancel;
};

static void rhythmdb_class_init (RhythmDBClass *klass);
static void rhythmdb_init (RhythmDB *source);
static void rhythmdb_finalize (GObject *object);
static void rhythmdb_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rhythmdb_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);
gpointer query_thread_main (struct RhythmDBQueryThreadData *data, RhythmDB *db);
gpointer property_query_thread_main (struct RhythmDBQueryThreadData *data, RhythmDB *db);

enum
{
	PROP_0,
	PROP_NAME,
};

enum
{
	ENTRY_ADDED,
	ENTRY_RESTORED,
	ENTRY_CHANGED,
	ENTRY_DELETED,
	LOAD_COMPLETE,
	LAST_SIGNAL
};

static guint rhythmdb_signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;


GType
rhythmdb_get_type (void)
{
	static GType rhythmdb_type = 0;

	if (rhythmdb_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RhythmDBClass),
			NULL,
			NULL,
			(GClassInitFunc) rhythmdb_class_init,
			NULL,
			NULL,
			sizeof (RhythmDB),
			0,
			(GInstanceInitFunc) rhythmdb_init
		};

		rhythmdb_type = g_type_register_static (G_TYPE_OBJECT,
						       "RhythmDB",
						       &our_info, G_TYPE_FLAG_ABSTRACT);
	}

	return rhythmdb_type;
}

static void
rhythmdb_class_init (RhythmDBClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rhythmdb_finalize;

	object_class->set_property = rhythmdb_set_property;
	object_class->get_property = rhythmdb_get_property;

	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "name",
							      "name",
							      NULL,
							      G_PARAM_READWRITE));

	rhythmdb_signals[ENTRY_ADDED] =
		g_signal_new ("entry_added",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	rhythmdb_signals[ENTRY_DELETED] =
		g_signal_new ("entry_deleted",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	rhythmdb_signals[ENTRY_RESTORED] =
		g_signal_new ("entry_restored",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_restored),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	rhythmdb_signals[ENTRY_CHANGED] =
		g_signal_new ("entry_changed",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	rhythmdb_signals[LOAD_COMPLETE] =
		g_signal_new ("load_complete",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, load_complete),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static GType
extract_gtype_from_enum_entry (GEnumClass *klass, guint i)
{
	GType ret;
	GEnumValue *value;
	char *typename;
	char *typename_end;
	
	value = g_enum_get_value (klass, i);
	typename = strstr (value->value_nick, "(");
	g_return_val_if_fail (typename != NULL, G_TYPE_INVALID);

	typename_end = strstr (typename, ")");
	typename++;
	typename = g_strndup (typename, typename_end-typename);
	ret = g_type_from_name (typename);
	g_free (typename);
	return ret;
}

static void
rhythmdb_init (RhythmDB *db)
{
	guint i, j;
	GEnumClass *prop_class, *unsaved_prop_class;
	
	db->priv = g_new0 (RhythmDBPrivate, 1);

	g_static_rw_lock_init (&db->priv->lock);

	db->priv->exit_mutex = g_mutex_new ();

	prop_class = g_type_class_ref (RHYTHMDB_TYPE_PROP);
	unsaved_prop_class = g_type_class_ref (RHYTHMDB_TYPE_UNSAVED_PROP);

	g_assert (prop_class->n_values + unsaved_prop_class->n_values == RHYTHMDB_NUM_PROPERTIES);
	g_assert (prop_class->n_values == RHYTHMDB_NUM_SAVED_PROPERTIES);
	db->priv->column_types = g_new (GType, RHYTHMDB_NUM_PROPERTIES);
	
	/* Now, extract the GType of each column from the enum descriptions,
	 * and cache that for later use. */
	for (i = 0; i < prop_class->n_values; i++) {
		db->priv->column_types[i] = extract_gtype_from_enum_entry (prop_class, i);
		g_assert (db->priv->column_types[i] != G_TYPE_INVALID);
	}
			
	
	for (j = 0; j < unsaved_prop_class->n_values; i++, j++) {
		db->priv->column_types[i] = extract_gtype_from_enum_entry (unsaved_prop_class, i);
		g_assert (db->priv->column_types[i] != G_TYPE_INVALID);
	}
	
	g_type_class_unref (prop_class);
	g_type_class_unref (unsaved_prop_class);

	db->priv->query_thread_pool = g_thread_pool_new ((GFunc) query_thread_main,
							 db, 3, TRUE, NULL);
	db->priv->property_query_thread_pool = g_thread_pool_new ((GFunc) property_query_thread_main,
								  db, 3, TRUE, NULL);
}

void
rhythmdb_shutdown (RhythmDB *db)
{
	g_return_if_fail (RHYTHMDB_IS (db));

	g_mutex_lock (db->priv->exit_mutex);
	db->priv->exiting = TRUE;
	g_mutex_unlock (db->priv->exit_mutex);

	if (db->priv->load_thread)
		g_thread_join (db->priv->load_thread);
}

static void
rhythmdb_finalize (GObject *object)
{
	RhythmDB *db;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS (object));

	db = RHYTHMDB (object);

	g_return_if_fail (db->priv != NULL);

	g_static_rw_lock_free (&db->priv->lock);

	g_mutex_free (db->priv->exit_mutex);

	g_thread_pool_free (db->priv->query_thread_pool, TRUE, FALSE);
	g_thread_pool_free (db->priv->property_query_thread_pool, TRUE, FALSE);

	g_free (db->priv->column_types);

	g_free (db->priv->name);

	g_free (db->priv);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rhythmdb_set_property (GObject *object,
		      guint prop_id,
		      const GValue *value,
		      GParamSpec *pspec)
{
	RhythmDB *source = RHYTHMDB (object);

	switch (prop_id)
	{
	case PROP_NAME:
		source->priv->name = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rhythmdb_get_property (GObject *object,
		      guint prop_id,
		      GValue *value,
		      GParamSpec *pspec)
{
	RhythmDB *source = RHYTHMDB (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, source->priv->name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rhythmdb_read_lock (RhythmDB *db)
{
	g_static_rw_lock_reader_lock (&db->priv->lock);
}

void
rhythmdb_read_unlock (RhythmDB *db)
{
	g_static_rw_lock_reader_unlock (&db->priv->lock);
}

void
rhythmdb_write_lock (RhythmDB *db)
{
	g_static_rw_lock_writer_lock (&db->priv->lock);
}

static void
emit_entry_changed (RhythmDBEntry *entry, gpointer unused,
		    RhythmDB *db)
{
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_CHANGED], 0, entry);
}

void
rhythmdb_write_unlock (RhythmDB *db)
{
	if (db->priv->changed_entries) {
		g_hash_table_foreach (db->priv->changed_entries, (GHFunc) emit_entry_changed, db);
		g_hash_table_destroy (db->priv->changed_entries);
		db->priv->changed_entries = NULL;
	}
		 
	g_static_rw_lock_writer_unlock (&db->priv->lock);
}

static inline void
db_enter (RhythmDB *db, gboolean write)
{
	if (write) 
		g_assert (db->priv->lock.have_writer);
	else
		g_assert (db->priv->lock.read_counter > 0
			  || db->priv->lock.have_writer);
}

RhythmDBEntry *
rhythmdb_entry_new (RhythmDB *db, RhythmDBEntryType type, const char *uri)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	RhythmDBEntry *ret;

	db_enter (db, TRUE);
	
	ret = klass->impl_entry_new (db, type, uri);
	rb_debug ("emitting entry added");
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_ADDED], 0, ret);
	return ret;
}

static gpointer
rhythmdb_load_thread_main (RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	klass->impl_load (db, db->priv->exit_mutex, &db->priv->exiting);

	g_signal_emit (G_OBJECT (db), rhythmdb_signals[LOAD_COMPLETE], 0);
	g_object_unref (G_OBJECT (db));
	return NULL;
}

void
rhythmdb_load (RhythmDB *db)
{
	g_assert (!db->priv->load_thread);
	g_object_ref (G_OBJECT (db));
	db->priv->load_thread =
		g_thread_create ((GThreadFunc) rhythmdb_load_thread_main, db, TRUE, NULL);
}

void
rhythmdb_load_join (RhythmDB *db)
{
	g_assert (db->priv->load_thread);

	g_thread_join (db->priv->load_thread);
	db->priv->load_thread = NULL;
}

void
rhythmdb_save (RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	db_enter (db, FALSE);
	
	klass->impl_save (db);
}

static void
set_sort_key_value (RhythmDB *db, RhythmDBClass *klass,
		    RhythmDBEntry *entry, guint propid,
		    const char *str)
{
	GValue val = {0, };
	char *key = rb_get_sort_key (str);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string_take_ownership (&val, key);
	klass->impl_entry_set (db, entry, propid, &val);
	g_value_unset (&val);
}

static void
set_folded_value (RhythmDB *db, RhythmDBClass *klass,
		  RhythmDBEntry *entry, guint propid,
		  const char *str)
{
	GValue val = {0, };
	char *key = g_utf8_casefold (str, -1);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string_take_ownership (&val, key);
	klass->impl_entry_set (db, entry, propid, &val);
	g_value_unset (&val);
}

void
rhythmdb_entry_set (RhythmDB *db, RhythmDBEntry *entry,
		    guint propid, GValue *value)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

#ifndef G_DISABLE_ASSERT	
	switch (G_VALUE_TYPE (value))
	{
	case G_TYPE_STRING:
		g_assert (g_utf8_validate (g_value_get_string (value), -1, NULL));
		break;
	case G_TYPE_BOOLEAN:
	case G_TYPE_INT:
	case G_TYPE_LONG:
	case G_TYPE_FLOAT:
	case G_TYPE_DOUBLE:
		break;
	default:
		g_assert_not_reached ();
		break;
	}
#endif

	db_enter (db, TRUE);

	if (!db->priv->changed_entries)
		db->priv->changed_entries = g_hash_table_new (NULL, NULL);

	g_hash_table_insert (db->priv->changed_entries, entry, NULL);

	klass->impl_entry_set (db, entry, propid, value);

	rhythmdb_entry_sync_mirrored (db, entry, propid, value);
}

void
rhythmdb_entry_sync_mirrored (RhythmDB *db, RhythmDBEntry *entry, guint propid,
			      GValue *value)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	switch (propid)
	{
	case RHYTHMDB_PROP_TITLE:
		set_sort_key_value (db, klass, entry,
				    RHYTHMDB_PROP_TITLE_SORT_KEY,
				    g_value_get_string (value));
		set_folded_value (db, klass, entry,
				  RHYTHMDB_PROP_TITLE_FOLDED,
				  g_value_get_string (value));
		break;
	case RHYTHMDB_PROP_ARTIST:
		set_sort_key_value (db, klass, entry,
				    RHYTHMDB_PROP_ARTIST_SORT_KEY,
				    g_value_get_string (value));
		set_folded_value (db, klass, entry,
				  RHYTHMDB_PROP_ARTIST_FOLDED,
				  g_value_get_string (value));
		break;
	case RHYTHMDB_PROP_ALBUM:
		set_sort_key_value (db, klass, entry,
				    RHYTHMDB_PROP_ALBUM_SORT_KEY,
				    g_value_get_string (value));
		set_folded_value (db, klass, entry,
				  RHYTHMDB_PROP_ALBUM_FOLDED,
				  g_value_get_string (value));
		break;
	case RHYTHMDB_PROP_LAST_PLAYED:
	{
		GValue tem = {0, };
		time_t now;
		
		g_value_init (&tem, G_TYPE_STRING);

		time (&now);

		if (g_value_get_long (value) == 0)
			g_value_set_static_string (&tem, _("Never"));
		else
			g_value_set_string_take_ownership (&tem, eel_strdup_strftime (_("%Y-%m-%d %H:%M"), localtime (&now)));
		
		klass->impl_entry_set (db, entry,
				       RHYTHMDB_PROP_LAST_PLAYED_STR,
				       &tem);
		g_value_unset (&tem);
		break;
	}
	default:
		break;
	}
}

void
rhythmdb_entry_get (RhythmDB *db, RhythmDBEntry *entry,
		    guint propid, GValue *value)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	db_enter (db, FALSE);

	klass->impl_entry_get (db, entry, propid, value);
}

void
rhythmdb_entry_delete (RhythmDB *db, RhythmDBEntry *entry)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	
	db_enter (db, TRUE);

	rhythmdb_emit_entry_deleted (db, entry);
	
	klass->impl_entry_delete (db, entry);
}

GPtrArray *
rhythmdb_query_copy (GPtrArray *array)
{
	guint i;
	GPtrArray *ret;

	if (!array)
		return NULL;
	
	ret = g_ptr_array_sized_new (array->len);	
	for (i = 0; i < array->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (array, i);
		RhythmDBQueryData *new_data = g_new0 (RhythmDBQueryData, 1);
		new_data->type = data->type;
		new_data->propid = data->propid;
		if (data->val) {
			new_data->val = g_new0 (GValue, 1);
			g_value_init (new_data->val, G_VALUE_TYPE (data->val));
			g_value_copy (data->val, new_data->val);
		}
		if (data->subquery)
			new_data->subquery = rhythmdb_query_copy (data->subquery);
		g_ptr_array_add (ret, new_data);
	}

	return ret;
}

GPtrArray *
rhythmdb_query_parse_valist (RhythmDB *db, va_list args)
{
	RhythmDBQueryType query;
	GPtrArray *ret = g_ptr_array_new ();
	char *error;
	
	while ((query = va_arg (args, RhythmDBQueryType)) != RHYTHMDB_QUERY_END) {
		RhythmDBQueryData *data = g_new0 (RhythmDBQueryData, 1);
		data->type = query;
		switch (query)
		{
		case RHYTHMDB_QUERY_DISJUNCTION:
			break;
		case RHYTHMDB_QUERY_SUBQUERY:
			data->subquery = va_arg (args, GPtrArray *);
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
		case RHYTHMDB_QUERY_PROP_LIKE:
		case RHYTHMDB_QUERY_PROP_GREATER:
		case RHYTHMDB_QUERY_PROP_LESS:
			data->propid = va_arg (args, guint);
			data->val = g_new0 (GValue, 1);
			g_value_init (data->val, rhythmdb_get_property_type (db, data->propid));
			G_VALUE_COLLECT (data->val, args, 0, &error);
			break;
		case RHYTHMDB_QUERY_END:
			g_assert_not_reached ();
			break;
		}
		g_ptr_array_add (ret, data);
	}
	return ret;
}

GPtrArray *
rhythmdb_query_parse (RhythmDB *db, ...)
{
	GPtrArray *ret;
	va_list args;

	va_start (args, db);

	ret = rhythmdb_query_parse_valist (db, args);

	va_end (args);

	return ret;
}

void
rhythmdb_query_append (RhythmDB *db, GPtrArray *query, ...)
{
	va_list args;
	guint i;
	GPtrArray *new = g_ptr_array_new ();

	va_start (args, query);

	new = rhythmdb_query_parse_valist (db, args);

	for (i = 0; i < new->len; i++)
		g_ptr_array_add (query, g_ptr_array_index (new, i));

	g_ptr_array_free (new, FALSE);

	va_end (args);
}

void
rhythmdb_query_free (GPtrArray *query)
{
	guint i;

	if (query == NULL)
		return;
	
	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);
		switch (data->type)
		{
		case RHYTHMDB_QUERY_DISJUNCTION:
			break;
		case RHYTHMDB_QUERY_SUBQUERY:
			rhythmdb_query_free (data->subquery);
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
		case RHYTHMDB_QUERY_PROP_LIKE:
		case RHYTHMDB_QUERY_PROP_GREATER:
		case RHYTHMDB_QUERY_PROP_LESS:
			g_value_unset (data->val);
			g_free (data->val);
			break;
		case RHYTHMDB_QUERY_END:
			g_assert_not_reached ();
			break;
		}
		g_free (data);
	}

	g_ptr_array_free (query, TRUE);
}

gpointer
property_query_thread_main (struct RhythmDBQueryThreadData *data,
			    RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	rb_debug ("doing property query");
	g_object_ref (G_OBJECT (data->main_model));
	if (data->lock)
		rhythmdb_read_lock (db);

	klass->impl_do_property_query (db, data->propid,
				       data->query, data->main_model);

	if (data->lock)
		rhythmdb_read_unlock (db);

	rb_debug ("completed");
	rhythmdb_property_model_complete (RHYTHMDB_PROPERTY_MODEL (data->main_model));
	g_object_unref (G_OBJECT (data->main_model));

	rhythmdb_query_free (data->query);
	g_free (data);
	return NULL;
}

void
rhythmdb_do_property_query_ptrarray (RhythmDB *db, guint property_id,
				     GtkTreeModel **model, GPtrArray *query)
{
	struct RhythmDBQueryThreadData *data;

	db_enter (db, FALSE);

	data = g_new0 (struct RhythmDBQueryThreadData, 1);
	data->propid = property_id;
	data->query = rhythmdb_query_copy (query);
	data->main_model = GTK_TREE_MODEL (rhythmdb_property_model_new (db, property_id, query));
	data->lock = FALSE;
	data->cancel = FALSE;

	property_query_thread_main (data, db);

	*model = data->main_model;
	while (rhythmdb_property_model_sync (RHYTHMDB_PROPERTY_MODEL (data->main_model), NULL))
		;
}

void
rhythmdb_do_property_query_ptrarray_async (RhythmDB *db, guint property_id,
					   GtkTreeModel *model, GPtrArray *query)
{
	struct RhythmDBQueryThreadData *data;

	data = g_new0 (struct RhythmDBQueryThreadData, 1);
	data->propid = property_id;
	data->query = rhythmdb_query_copy (query);
	data->main_model = model;
	data->lock = TRUE;
	data->cancel = FALSE;

	g_thread_pool_push (db->priv->property_query_thread_pool, data, NULL);
}

void
rhythmdb_do_property_query (RhythmDB *db, guint property_id, GtkTreeModel **model, ...)
{
	GPtrArray *query;
	va_list args;

	db_enter (db, FALSE);

	va_start (args, model);

	query = rhythmdb_query_parse_valist (db, args);

	rhythmdb_do_property_query_ptrarray (db, property_id, model, query);

	rhythmdb_query_free (query);

	va_end (args);
}


RhythmDBEntry *
rhythmdb_entry_lookup_by_location (RhythmDB *db, const char *uri)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	db_enter (db, FALSE);

	return klass->impl_lookup_by_location (db, uri);
}

gboolean
rhythmdb_evaluate_query (RhythmDB *db, GPtrArray *query, RhythmDBEntry *entry)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	db_enter (db, FALSE);

	return klass->impl_evaluate_query (db, query, entry);
}

gpointer
query_thread_main (struct RhythmDBQueryThreadData *data,
		   RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	rb_debug ("doing query");
	g_object_ref (G_OBJECT (data->main_model));

	rhythmdb_read_lock (db);
	
	klass->impl_do_full_query (db, data->query,
				   data->main_model,
				   &data->cancel);

	rhythmdb_read_unlock (db);

	rb_debug ("completed");
	rhythmdb_query_model_complete (RHYTHMDB_QUERY_MODEL (data->main_model));
	g_object_unref (G_OBJECT (data->main_model));

	rhythmdb_query_free (data->query);
	g_free (data);
	return NULL;
}

void
rhythmdb_do_full_query_async_parsed (RhythmDB *db, GtkTreeModel *main_model,
				     GPtrArray *query)
{
	struct RhythmDBQueryThreadData *data;

	data = g_new0 (struct RhythmDBQueryThreadData, 1);
	data->query = rhythmdb_query_copy (query);
	data->main_model = main_model;
	data->cancel = FALSE;

	g_object_set (G_OBJECT (RHYTHMDB_QUERY_MODEL (main_model)),
		      "query", query, NULL);

	g_thread_pool_push (db->priv->query_thread_pool, data, NULL);
}

void
rhythmdb_do_full_query_async (RhythmDB *db, GtkTreeModel *main_model, ...)
{
	GPtrArray *query;
	va_list args;

	va_start (args, main_model);

	query = rhythmdb_query_parse_valist (db, args);

	rhythmdb_do_full_query_async_parsed (db, main_model, query);

	rhythmdb_query_free (query);

	va_end (args);
}

static void
rhythmdb_do_full_query_internal (RhythmDB *db, GtkTreeModel *main_model,
				 GPtrArray *query)
{
	struct RhythmDBQueryThreadData *data;

	data = g_new0 (struct RhythmDBQueryThreadData, 1);
	data->query = rhythmdb_query_copy (query);
	data->main_model = main_model;
	data->cancel = FALSE;

	g_object_set (G_OBJECT (RHYTHMDB_QUERY_MODEL (main_model)),
		      "query", query, NULL);

	query_thread_main (data, db);
	while (rhythmdb_model_poll (RHYTHMDB_MODEL (main_model), NULL))
		;
}

void
rhythmdb_do_full_query_parsed (RhythmDB *db, GtkTreeModel *main_model,
			       GPtrArray *query)
{
	db_enter (db, FALSE);

	rhythmdb_do_full_query_internal (db, main_model, query);
}

void
rhythmdb_do_full_query (RhythmDB *db, GtkTreeModel *main_model, ...)
{
	GPtrArray *query;
	va_list args;

	db_enter (db, FALSE);

	va_start (args, main_model);

	query = rhythmdb_query_parse_valist (db, args);

	rhythmdb_do_full_query_internal (db, main_model, query);

	rhythmdb_query_free (query);
	
	va_end (args);
}

GType rhythmdb_get_property_type (RhythmDB *db, guint property_id)
{
	g_return_val_if_fail (property_id >= 0 && property_id < RHYTHMDB_NUM_PROPERTIES,
			      G_TYPE_INVALID);

	return db->priv->column_types[property_id];
}

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rhythmdb_query_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{

			ENUM_ENTRY (RHYTHMDB_QUERY_END, "Query end marker"),
			ENUM_ENTRY (RHYTHMDB_QUERY_DISJUNCTION, "Disjunctive marker"),
			ENUM_ENTRY (RHYTHMDB_QUERY_SUBQUERY, "Subquery"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_EQUALS, "Property equivalence"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_LIKE, "Fuzzy property matching"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_GREATER, "True iff property1 > property2"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_LESS, "True iff property1 < property2"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RhythmDBQueryType", values);
	}

	return etype;
}

GType
rhythmdb_prop_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{

			ENUM_ENTRY (RHYTHMDB_PROP_TYPE, "Type of entry (gint)"),
			ENUM_ENTRY (RHYTHMDB_PROP_TITLE, "Title (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE, "Genre (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST, "Artist (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM, "Album (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_TRACK_NUMBER, "Track Number (gint)"),
			ENUM_ENTRY (RHYTHMDB_PROP_DURATION, "Duration (glong)"),
			ENUM_ENTRY (RHYTHMDB_PROP_FILE_SIZE, "File Size (glong)"),
			ENUM_ENTRY (RHYTHMDB_PROP_LOCATION, "Location (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_MTIME, "Modification time (glong)"),
			ENUM_ENTRY (RHYTHMDB_PROP_RATING, "Rating (gint)"),
			ENUM_ENTRY (RHYTHMDB_PROP_PLAY_COUNT, "Play Count (gint)"),
			ENUM_ENTRY (RHYTHMDB_PROP_LAST_PLAYED, "Last Played (glong)"),
			ENUM_ENTRY (RHYTHMDB_PROP_QUALITY, "Quality (gint)"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RhythmDBPropType", values);
	}

	return etype;
}


GType
rhythmdb_unsaved_prop_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{

			ENUM_ENTRY (RHYTHMDB_PROP_TITLE_SORT_KEY, "Title sort key (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE_SORT_KEY, "Genre sort key (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST_SORT_KEY, "Artist sort key (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_SORT_KEY, "Album sort key (gchararray)"),

			ENUM_ENTRY (RHYTHMDB_PROP_TITLE_FOLDED, "Title folded (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE_FOLDED, "Genre folded (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST_FOLDED, "Artist folded (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_FOLDED, "Album folded (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_LAST_PLAYED_STR, "Last Played (gchararray)"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RhythmDBUnsavedPropType", values);
	}

	return etype;
}

#define DEFINE_GETTER(NAME, TYPE, GTYPE, DEFAULT)	\
TYPE \
rhythmdb_entry_get_ ## NAME (RhythmDB *db, RhythmDBEntry *entry, guint propid) \
{ \
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db); \
	GValue gval = {0, }; \
	TYPE retval; \
	db_enter (db, FALSE); \
	g_value_init (&gval, G_TYPE_ ## GTYPE); \
	klass->impl_entry_get (db, entry, propid, &gval); \
	retval = g_value_get_ ## NAME (&gval); \
	return retval; \
}

DEFINE_GETTER(string, const char *, STRING, NULL)
DEFINE_GETTER(boolean, gboolean, BOOLEAN, FALSE)
DEFINE_GETTER(pointer, gpointer, POINTER, NULL)
DEFINE_GETTER(long, long, LONG, 0)
DEFINE_GETTER(int, int, INT, 0)
DEFINE_GETTER(double, double, DOUBLE, 0)
DEFINE_GETTER(float, float, FLOAT, 0)
#undef DEFINE_GETTER

void
rhythmdb_emit_entry_added (RhythmDB *db, RhythmDBEntry *entry)
{
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_ADDED], 0, entry);
}

void
rhythmdb_emit_entry_restored (RhythmDB *db, RhythmDBEntry *entry)
{
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_RESTORED], 0, entry);
}

void
rhythmdb_emit_entry_deleted (RhythmDB *db, RhythmDBEntry *entry)
{
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_DELETED], 0, entry);
}
