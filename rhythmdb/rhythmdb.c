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
static void default_lock (RhythmDB *db);
static void default_unlock (RhythmDB *db);

struct RhythmDBPrivate
{
	char *name;

	GStaticRecMutex lock;
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

	klass->impl_lock = default_lock;
	klass->impl_unlock = default_unlock;

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

static void
default_lock (RhythmDB *db)
{
	g_static_rec_mutex_lock (&db->priv->lock);
}

void
rhythmdb_lock (RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_lock (db);
}

static void
default_unlock (RhythmDB *db)
{
	g_static_rec_mutex_unlock (&db->priv->lock);
}

void
rhythmdb_unlock (RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_unlock (db);
}

RhythmDBEntry *
rhythmdb_entry_new (RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_entry_new (db);
}

void
rhythmdb_entry_set (RhythmDB *db, RhythmDBEntry *entry,
		    guint propid, GValue *value)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	klass->impl_entry_set (db, entry, propid, value);
}

void
rhythmdb_entry_get (RhythmDB *db, RhythmDBEntry *entry,
		    guint propid, GValue *value)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	klass->impl_entry_get (db, entry, propid, value);
}

void
rhythmdb_entry_delete (RhythmDB *db, RhythmDBEntry *entry)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	klass->impl_entry_delete (db, entry);
}
	
#define DEFINE_GETTER (name, TYPE)			\
TYPE \
rhythmdb_entry_get_ ## name (RhythmDB *db, RhythmDBEntry *entry, \
			     guint property_id) \
{ \
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db); \
	return klass->impl_entry_get_ ## name (db, entry); \
}

DEFINE_GETTER (string, const char *)
DEFINE_GETTER (boolean, gboolean)
DEFINE_GETTER (long, long)
DEFINE_GETTER (int, int)
DEFINE_GETTER (double, double)
DEFINE_GETTER (float, float)
DEFINE_GETTER (pointer, pointer)
#undef DEFINE_GETTER

GtkTreeModel *
rhythmdb_do_entry_query (RhythmDB *db, ...)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	GtkTreeModel *ret;
	va_list args;

	va_start (args);

	ret = klass->impl_do_entry_query (db, args);
	va_end (args);
	return ret;
}

GtkTreeModel *
rhythmdb_do_property_query (RhythmDB *db, const char *property, ...)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	GtkTreeModel *ret;
	va_list args;

	va_start (args);

	ret = klass->impl_do_property_query (db, property, args);
	va_end (args);
	return ret;
}
