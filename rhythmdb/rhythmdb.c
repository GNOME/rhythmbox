/* 
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
};

enum
{
	PROP_0,
	PROP_NAME,
};

enum
{
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
						       &our_info, 0);
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

/* 	rhythmdb_signals[FILTER_CHANGED] = */
/* 		g_signal_new ("filter_changed", */
/* 			      RB_TYPE_SOURCE, */
/* 			      G_SIGNAL_RUN_LAST, */
/* 			      G_STRUCT_OFFSET (RhythmDBClass, filter_changed), */
/* 			      NULL, NULL, */
/* 			      g_cclosure_marshal_VOID__VOID, */
/* 			      G_TYPE_NONE, */
/* 			      0); */
}

static void
rhythmdb_init (RhythmDB *source)
{
	source->priv = g_new0 (RhythmDBPrivate, 1);

	g_static_rec_mutex_init (&source->priv->lock);
}

static void
rhythmdb_finalize (GObject *object)
{
	RhythmDB *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SOURCE (object));

	source = RHYTHMDB (object);

	g_return_if_fail (source->priv != NULL);

	g_static_rec_mutex_free (&source->priv->lock);

	g_free (source->priv->name);

	g_free (source->priv);

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
rhythmdb_entry_new (RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	g_assert (db->priv->locklevel > 0);

	return klass->impl_entry_new (db);
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

	klass->impl_entry_delete (db, entry);
}
	
#define DEFINE_GETTER(NAME, TYPE, GTYPE, DEFAULT) \
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

GPtrArray *
rhythmdb_do_entry_query (RhythmDB *db, ...)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	GPtrArray *ret;
	va_list args;

	g_assert (db->priv->locklevel > 0);

	va_start (args);

	ret = klass->impl_do_entry_query (db, args);
	va_end (args);
	return ret;
}

GPtrArray *
rhythmdb_do_property_query (RhythmDB *db, const char *property, ...)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	GPtrArray *ret;
	va_list args;

	g_assert (db->priv->locklevel > 0);

	va_start (args);

	ret = klass->impl_do_property_query (db, property, args);
	va_end (args);
	return ret;
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
