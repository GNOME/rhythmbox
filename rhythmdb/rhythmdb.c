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
 *  $Id$
 */

#include "rhythmdb.h"

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

	GStaticRecMutex lock;
#ifndef G_DISABLE_ASSERT
	gint locklevel;
#endif

	GType *column_types;
};

enum
{
	PROP_0,
	PROP_NAME,
};

enum
{
	ENTRY_DELETED,
	ENTRY_ADDED,
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
						       &our_info, G_TYPE_ABSTRACT);
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

	rhythmdb_signals[ENTRY_DELETED] =
		g_signal_new ("entry_deleted",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	rhythmdb_signals[ENTRY_ADDED] =
		g_signal_new ("entry_added",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);
}

static GType
extract_gtype_from_enum_entry (GEnumClass *klass, guint i)
{
	GEnumValue *value;
	char *typename;
	char *typename_end;
	
	value = g_enum_get_value (klass, i);
	typename = strstr (value->value_nick, "(");
	g_return_val_if_fail (typename != NULL, G_TYPE_INVALID);

	typename_end = strstr (typename, ")");
	typename++;
	typename = g_strndup (typename, typename_end-typename);
	return g_type_from_name (typename);
}

static void
rhythmdb_init (RhythmDB *db)
{
	GEnumClass *prop_class, *unsaved_prop_class;
	
	db->priv = g_new0 (RhythmDBPrivate, 1);

	g_static_rec_mutex_init (&db->priv->lock);

	prop_class = g_type_class_ref (RHYTHMDB_TYPE_PROP);
	unsaved_prop_class = g_type_class_ref (RHYTHMDB_TYPE_UNSAVED_PROP);

	db->priv->column_types = g_new (GType, RHYTHMDB_NUM_PROPERTIES);
	
	/* Now, extract the GType of each column from the enum descriptions,
	 * and cache that for later use. */
	for (i = 0; i < prop_class->n_values; i++)
		db->priv->column_types[i] = extract_gtype_from_enum_entry (prop_class, i);
	
	for (; i < unsaved_prop_class->n_values; i++)
		db->priv->column_types[i] = extract_gtype_from_enum_entry (unsaved_prop_class, i);
	
	g_type_class_unref (prop_class);
	g_type_class_unref (unsaved_prop_class);
}

static void
rhythmdb_finalize (GObject *object)
{
	RhythmDB *db;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS (object));

	db = RHYTHMDB (object);

	g_return_if_fail (db->priv != NULL);

	g_static_rec_mutex_free (&db->priv->lock);
	g_free (model->priv->column_types);

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
rhythmdb_lock (RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	g_static_rec_mutex_lock (&db->priv->lock);
#ifndef G_DISABLE_ASSERT
	db->priv->locklevel++;
#endif
}

void
rhythmdb_unlock (RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

#ifndef G_DISABLE_ASSERT
	db->priv->locklevel--;
#endif
	g_static_rec_mutex_unlock (&db->priv->lock);
}

RhythmDBEntry *
rhythmdb_entry_new (RhythmDB *db, enum RhythmDBEntryType type)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	RhythmDBEntry *ret;

	g_assert (db->priv->locklevel > 0);

	ret = klass->impl_entry_new (db, type);
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_ADDED], 0, ret);
	return ret;
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

	g_assert (db->priv->locklevel > 0);

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
		
		g_value_init (&tem, G_TYPE_STRING);

		if (g_value_get_int (value) == 0)
			g_value_set_string (&tem, _("Never"));
		else
			g_value_set_string_take_ownership (&tem, eel_strdup_strftime (_("%Y-%m-%d %H:%M"), localtime (&now)));
		
		klass->impl_entry_set (db, entry,
				       RHYTHMDB_PROP_LAST_PLAYED_STR,
				       &tem);
		g_value_unset (&tem);
		break;
	}
}

void
rhythmdb_entry_get (RhythmDB *db, RhythmDBEntry *entry,
		    guint propid, GValue *value)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	g_assert (db->priv->locklevel > 0);

	klass->impl_entry_get (db, entry, propid, value);
}

void
rhythmdb_entry_delete (RhythmDB *db, RhythmDBEntry *entry)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	
	g_assert (db->priv->locklevel > 0);
	
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_DELETED], 0, entry);
	klass->impl_entry_delete (db, entry);
}

static GPtrArray *
parse_query (va_list args)
{
	enum RhythmDBQueryType query;
	GPtrArray *ret = g_ptr_array_new ();
	
	while ((query = va_arg (args, enum RhythmDBQueryType)) != RHYTHMDB_QUERY_END) {
		struct RhythmDBQueryData *data = g_new (struct RhythmDBQueryData, 1);
		data->type = query;
		data->propid = va_arg (args, guint);
		switch (query)
		{
		case RHYTHMDB_QUERY_HAVE_PROP:
		case RHYTHMDB_QUERY_DISJUNCTION:
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
		case RHYTHMDB_QUERY_PROP_LIKE:
		case RHYTHMDB_QUERY_PROP_GREATER:
		case RHYTHMDB_QUERY_PROP_LESS:
			data->val = g_new0 (GValue, 1);
			g_value_copy (va_arg (args, GValue *), data->val);
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
		switch (query[i]->type)
		{
		case RHYTHMDB_QUERY_HAVE_PROP:
		case RHYTHMDB_QUERY_DISJUNCTION:
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
		case RHYTHMDB_QUERY_PROP_LIKE:
		case RHYTHMDB_QUERY_PROP_GREATER:
		case RHYTHMDB_QUERY_PROP_LESS:
			g_value_unset (query->query[i]->val);
			break;
		case RHYTHMDB_QUERY_END:
			g_assert_not_reached ();
			break;
		}
	}

	g_ptr_array_free (query);
}

GtkTreeModel *
rhythmdb_do_entry_query (RhythmDB *db, ...)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	GtkTreeModel *ret;
	GPtrArray *query;
	va_list args;

	g_assert (db->priv->locklevel > 0);

	va_start (args);

	query = parse_query (args);

	ret = klass->impl_do_entry_query (db, query);

	free_query (query);
	va_end (args);
	return ret;
}

GtkTreeModel *
rhythmdb_do_property_query (RhythmDB *db, guint property_id, ...)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	GtkTreeModel *ret;
	GPtrArray *query;
	va_list args;

	g_assert (db->priv->locklevel > 0);

	va_start (args);

	query = parse_query (args);

	ret = klass->impl_do_property_query (db, property_id, query);

	free_query (query);
	va_end (args);
	return ret;
}

void
rhythmdb_do_full_query (RhythmDB *db, GtkTreeModel **main_model,
			GtkTreeModel **genre_model,
			GtkTreeModel **artist_model,
			GtkTreeModle **album_model, ...)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	GPtrArray *query;
	va_list args;

	g_assert (db->priv->locklevel > 0);

	va_start (args);

	query = parse_query (args);

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
#define ENUM_ENTRY (NAME, DESC) { NAME, #NAME, DESC }

GType
rhythmdb_query_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{

			ENUM_ENTRY (RHYTHMDB_QUERY_END, "Query end marker"),
			ENUM_ENTRY (RHYTHMDB_QUERY_HAVE_PROP, "Whether or not a property exists"),
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

			ENUM_ENTRY (RHYTHMDB_PROP_TYPE, "Type of entry (G_TYPE_INT)"),
			ENUM_ENTRY (RHYTHMDB_PROP_NAME, "Name (G_TYPE_STRING)"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE, "Genre (G_TYPE_STRING)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST, "Artist (G_TYPE_STRING)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM, "Album (G_TYPE_STRING)"),
			ENUM_ENTRY (RHYTHMDB_PROP_TRACK_NUMBER, "Track Number (G_TYPE_INT)"),
			ENUM_ENTRY (RHYTHMDB_PROP_DURATION, "Duration (G_TYPE_INT)"),
			ENUM_ENTRY (RHYTHMDB_PROP_FILE_SIZE, "File Size (G_TYPE_LONG)"),
			ENUM_ENTRY (RHYTHMDB_PROP_LOCATION, "Location (G_TYPE_STRING)"),
			ENUM_ENTRY (RHYTHMDB_PROP_MTIME, "Modification time (G_TYPE_LONG)"),
			ENUM_ENTRY (RHYTHMDB_PROP_RATING, "Rating (G_TYPE_INT)"),
			ENUM_ENTRY (RHYTHMDB_PROP_PLAY_COUNT, "Play Count (G_TYPE_INT)"),
			ENUM_ENTRY (RHYTHMDB_PROP_LAST_PLAYED, "Last Played (G_TYPE_LONG)"),
			ENUM_ENTRY (RHYTHMDB_PROP_QUALITY, "Quality (G_TYPE_INT)"),
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

			ENUM_ENTRY (RHYTHMDB_PROP_NAME_SORT_KEY, "Name sort key (G_TYPE_STRING)"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE_SORT_KEY, "Genre sort key (G_TYPE_STRING)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST_SORT_KEY, "Artist sort key (G_TYPE_STRING)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_SORT_KEY, "Album sort key (G_TYPE_STRING)"),
			ENUM_ENTRY (RHYTHMDB_PROP_LAST_PLAYED_STR, "Last Played (G_TYPE_STRING)"),
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
	GValue val = {0, }; \
	TYPE retval; \
	g_assert (db->priv->locklevel > 0); \
	g_value_init (&val, G_TYPE_ ## GTYPE); \
	klass->impl_entry_get (db, entry, propid, &val); \
	retval = g_value_get_ ## TYPE (&val); \
	g_value_unset (&val); \
	return retval;
}

DEFINE_GETTER(string, const char *, STRING, NULL);
DEFINE_GETTER(boolean, gboolean, BOOLEAN, FALSE);
DEFINE_GETTER(pointer, gpointer, POINTER, NULL);
DEFINE_GETTER(long, long, LONG, 0);
DEFINE_GETTER(int, int, INT, 0);
DEFINE_GETTER(double, double, DOUBLE, 0);
DEFINE_GETTER(float, float, FLOAT, 0);
#undef DEFINE_GETTER

