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
#include <string.h>
#include <gobject/gvaluecollector.h>
#include <gdk/gdk.h>
#include <libgnome/gnome-i18n.h>
#include "rb-string-helpers.h"
#include "rb-thread-helpers.h"
#include "rb-cut-and-paste-code.h"

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

struct RhythmDBPrivate
{
	char *name;

	GStaticRWLock lock;

	GType *column_types;

	GThread *load_thread;

	GMutex *exit_mutex;
	gboolean exiting;
};

enum
{
	PROP_0,
	PROP_NAME,
};

enum
{
	ENTRY_ADDED,
	GENRE_ADDED,
	ARTIST_ADDED,
	ALBUM_ADDED,
	GENRE_DELETED,
	ARTIST_DELETED,
	ALBUM_DELETED,
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

	rhythmdb_signals[GENRE_DELETED] =
		g_signal_new ("genre_deleted",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, genre_deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);

	rhythmdb_signals[ARTIST_DELETED] =
		g_signal_new ("artist_deleted",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, artist_deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);

	rhythmdb_signals[ALBUM_DELETED] =
		g_signal_new ("album_deleted",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, album_deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);

	rhythmdb_signals[GENRE_ADDED] =
		g_signal_new ("genre_added",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, genre_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);

	rhythmdb_signals[ARTIST_ADDED] =
		g_signal_new ("artist_added",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, artist_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);

	rhythmdb_signals[ALBUM_ADDED] =
		g_signal_new ("album_added",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, album_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);
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
	if (rb_thread_helpers_in_main_thread ())
		GDK_THREADS_LEAVE ();
	g_static_rw_lock_reader_lock (&db->priv->lock);
}

void
rhythmdb_read_unlock (RhythmDB *db)
{
	if (rb_thread_helpers_in_main_thread ())
		GDK_THREADS_ENTER ();
	g_static_rw_lock_reader_unlock (&db->priv->lock);
}

void
rhythmdb_write_lock (RhythmDB *db)
{
	if (rb_thread_helpers_in_main_thread ())
		GDK_THREADS_LEAVE ();
	g_static_rw_lock_writer_lock (&db->priv->lock);
}

void
rhythmdb_write_unlock (RhythmDB *db)
{
	if (rb_thread_helpers_in_main_thread ())
		GDK_THREADS_ENTER ();
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
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_ADDED], 0, ret);
	return ret;
}

static gpointer
rhythmdb_load_thread_main (RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	klass->impl_load (db, db->priv->exit_mutex, &db->priv->exiting);

	g_thread_exit (NULL);
	return NULL;
}

void
rhythmdb_load (RhythmDB *db)
{
	g_assert (!db->priv->load_thread);
	db->priv->load_thread =
		g_thread_create ((GThreadFunc) rhythmdb_load_thread_main, db, TRUE, NULL);
}

void
rhythmdb_load_join (RhythmDB *db)
{
	if (rb_thread_helpers_in_main_thread ())
		GDK_THREADS_LEAVE ();

	g_assert (db->priv->load_thread);

	g_thread_join (db->priv->load_thread);
	db->priv->load_thread = NULL;

	if (rb_thread_helpers_in_main_thread ())
		GDK_THREADS_ENTER ();
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

	klass->impl_entry_set (db, entry, propid, value);

	/* Handle any mirrored (unsaved) properties */
	switch (propid)
	{
	case RHYTHMDB_PROP_NAME:
		set_sort_key_value (db, klass, entry,
				    RHYTHMDB_PROP_NAME_SORT_KEY,
				    g_value_get_string (value));
		break;
	case RHYTHMDB_PROP_ARTIST:
		set_sort_key_value (db, klass, entry,
				    RHYTHMDB_PROP_ARTIST_SORT_KEY,
				    g_value_get_string (value));
		break;
	case RHYTHMDB_PROP_ALBUM:
		set_sort_key_value (db, klass, entry,
				    RHYTHMDB_PROP_ALBUM_SORT_KEY,
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
	
	klass->impl_entry_delete (db, entry);
}

static GPtrArray *
parse_query (RhythmDB *db, va_list args)
{
	RhythmDBQueryType query;
	GPtrArray *ret = g_ptr_array_new ();
	char *error;
	
	while ((query = va_arg (args, RhythmDBQueryType)) != RHYTHMDB_QUERY_END) {
		RhythmDBQueryData *data = g_new (RhythmDBQueryData, 1);
		data->type = query;
		switch (query)
		{
		case RHYTHMDB_QUERY_DISJUNCTION:
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

static void
free_query (GPtrArray *query)
{
	guint i;
	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);
		switch (data->type)
		{
		case RHYTHMDB_QUERY_DISJUNCTION:
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

/* GtkTreeModel * */
/* rhythmdb_do_entry_query (RhythmDB *db, ...) */
/* { */
/* 	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db); */
/* 	GtkTreeModel *ret; */
/* 	GPtrArray *query; */
/* 	va_list args; */

/* 	db_enter (db, FALSE); */

/* 	va_start (args, db); */

/* 	query = parse_query (args); */

/* 	ret = klass->impl_do_entry_query (db, query); */

/* 	free_query (query); */
/* 	va_end (args); */
/* 	return ret; */
/* } */

/* GtkTreeModel * */
/* rhythmdb_do_property_query (RhythmDB *db, guint property_id, ...) */
/* { */
/* 	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db); */
/* 	GtkTreeModel *ret; */
/* 	GPtrArray *query; */
/* 	va_list args; */

/* 	db_enter (db, FALSE); */

/* 	va_start (args); */

/* 	query = parse_query (args); */

/* 	ret = klass->impl_do_property_query (db, property_id, query); */

/* 	free_query (query); */
/* 	va_end (args); */
/* 	return ret; */
/* } */

RhythmDBEntry *
rhythmdb_entry_lookup_by_location (RhythmDB *db, const char *uri)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	db_enter (db, FALSE);

	return klass->impl_lookup_by_location (db, uri);
}


void
rhythmdb_do_full_query (RhythmDB *db, GtkTreeModel **main_model,
			GtkTreeModel **genre_model,
			GtkTreeModel **artist_model,
			GtkTreeModel **album_model, ...)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	GPtrArray *query;
	va_list args;

	db_enter (db, FALSE);

	va_start (args, album_model);

	query = parse_query (db, args);

	klass->impl_do_full_query (db, query, main_model, genre_model, artist_model, album_model);

	free_query (query);
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
			ENUM_ENTRY (RHYTHMDB_PROP_NAME, "Name (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE, "Genre (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST, "Artist (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM, "Album (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_TRACK_NUMBER, "Track Number (gint)"),
			ENUM_ENTRY (RHYTHMDB_PROP_DURATION, "Duration (gint)"),
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

			ENUM_ENTRY (RHYTHMDB_PROP_NAME_SORT_KEY, "Name sort key (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE_SORT_KEY, "Genre sort key (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST_SORT_KEY, "Artist sort key (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_SORT_KEY, "Album sort key (gchararray)"),
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
rhythmdb_emit_genre_added (RhythmDB *db, const char *genre)
{
	rb_thread_helpers_lock_gdk ();
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[GENRE_ADDED], 0, genre);
	rb_thread_helpers_unlock_gdk ();
}

void
rhythmdb_emit_artist_added (RhythmDB *db, const char *artist)
{
	rb_thread_helpers_lock_gdk ();
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ARTIST_ADDED], 0, artist);
	rb_thread_helpers_unlock_gdk ();
}

void
rhythmdb_emit_album_added (RhythmDB *db, const char *album)
{
	rb_thread_helpers_lock_gdk ();
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ALBUM_ADDED], 0, album);
	rb_thread_helpers_unlock_gdk ();
}

void
rhythmdb_emit_genre_deleted (RhythmDB *db, const char *genre)
{
	rb_thread_helpers_lock_gdk ();
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[GENRE_DELETED], 0, genre);
	rb_thread_helpers_unlock_gdk ();
}

void
rhythmdb_emit_artist_deleted (RhythmDB *db, const char *artist)
{
	rb_thread_helpers_lock_gdk ();
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ARTIST_DELETED], 0, artist);
	rb_thread_helpers_unlock_gdk ();
}

void
rhythmdb_emit_album_deleted (RhythmDB *db, const char *album)
{
	rb_thread_helpers_lock_gdk ();
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ALBUM_DELETED], 0, album);
	rb_thread_helpers_unlock_gdk ();
}
