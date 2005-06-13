/* 
 *  arch-tag: Implementation of RhythmDB libgda/SQLite database
 *
 *  Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
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
#include "rhythmdb-gda.h"
#include "rhythmdb-query-model.h"

#if 0
#define g_print(...)
#endif

static void rhythmdb_gda_class_init (RhythmDBGdaClass * klass);
static void rhythmdb_gda_init (RhythmDBGda * shell_player);
static void rhythmdb_gda_finalize (GObject * object);

static void rhythmdb_gda_load (RhythmDB * rdb, gboolean * die);
static void rhythmdb_gda_save (RhythmDB * rdb);
static RhythmDBEntry *rhythmdb_gda_entry_new (RhythmDB * db,
    RhythmDBEntryType type, const char *uri);
static void rhythmdb_gda_entry_set (RhythmDB * db, RhythmDBEntry * entry,
    guint propid, const GValue * value);
static void rhythmdb_gda_entry_get (RhythmDB * db, RhythmDBEntry * entry,
    guint propid, GValue * value);
static void rhythmdb_gda_entry_delete (RhythmDB * db, RhythmDBEntry * entry);
static void rhythmdb_gda_entry_delete_by_type (RhythmDB * adb,
    RhythmDBEntryType type);
static RhythmDBEntry *rhythmdb_gda_entry_lookup_by_location (RhythmDB * db,
    const char *uri);
static void rhythmdb_gda_do_full_query (RhythmDB * db, GPtrArray * query,
    GtkTreeModel * main_model, gboolean * cancel);
static gboolean rhythmdb_gda_evaluate_query (RhythmDB * adb, GPtrArray * query,
    RhythmDBEntry * aentry);

static GObjectClass *parent_class = NULL;

GType
rhythmdb_gda_get_type (void)
{
  static GType rhythmdb_gda_type = 0;

  if (rhythmdb_gda_type == 0) {
    static const GTypeInfo our_info = {
      sizeof (RhythmDBGdaClass),
      NULL,
      NULL,
      (GClassInitFunc) rhythmdb_gda_class_init,
      NULL,
      NULL,
      sizeof (RhythmDBGda),
      0,
      (GInstanceInitFunc) rhythmdb_gda_init
    };

    rhythmdb_gda_type = g_type_register_static (RHYTHMDB_TYPE,
        "RhythmDBGda", &our_info, 0);
  }

  return rhythmdb_gda_type;
}

static void
rhythmdb_gda_class_init (RhythmDBGdaClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  RhythmDBClass *rhythmdb_class = RHYTHMDB_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->finalize = rhythmdb_gda_finalize;

  rhythmdb_class->impl_load = rhythmdb_gda_load;
  rhythmdb_class->impl_save = rhythmdb_gda_save;
  rhythmdb_class->impl_entry_new = rhythmdb_gda_entry_new;
  rhythmdb_class->impl_entry_set = rhythmdb_gda_entry_set;
  rhythmdb_class->impl_entry_get = rhythmdb_gda_entry_get;
  rhythmdb_class->impl_entry_delete = rhythmdb_gda_entry_delete;
  rhythmdb_class->impl_entry_delete_by_type = rhythmdb_gda_entry_delete_by_type;
  rhythmdb_class->impl_lookup_by_location =
      rhythmdb_gda_entry_lookup_by_location;
  rhythmdb_class->impl_evaluate_query = rhythmdb_gda_evaluate_query;
  rhythmdb_class->impl_do_full_query = rhythmdb_gda_do_full_query;
}

static void
rhythmdb_gda_init (RhythmDBGda * db)
{
  gint i;
  /* FIXME: This is a hack to replace '-' with '_' because of SQL column syntax */
  for (i = 0; i < RHYTHMDB_NUM_PROPERTIES; i++) {
    gchar *mod = (gchar *) rhythmdb_nice_elt_name_from_propid (RHYTHMDB (db), i);
    while (*mod) {
      if (*mod == '-') *mod = '_';
      mod ++;
    }
  }
  
  /* we'll set up the Db in the _new function when we actually know the filename */
}

static gchar *
escape_string (const gchar *orig)
{
  gchar **strv;
  gchar *ret, *tmp;
  /* this is the shortest possible version. it's definitely slow */
  strv = g_strsplit (orig, "\"", 0);
  tmp = g_strjoinv ("\"\"", strv);
  g_strfreev (strv);
  ret = g_strdup_printf ("\"%s\"", tmp);
  g_free (tmp);
  return ret;
}

/* debugging */
static void
dump_model (GdaDataModel *model)
{
  guint i, j;

  for (i = 0; i < gda_data_model_get_n_rows (model); i++) {
    for (j = 0; j < gda_data_model_get_n_columns (model); j++) {
      const GdaValue *value = gda_data_model_get_value_at (model, j, i);
      
      if (value) {
	gchar *str = gda_value_stringify (value);
	g_print ("(%4u, %4u) - %s (%d)\n", i, j, str, gda_value_get_type (value));
	g_free (str);
      } else {
	g_print ("(%4u, %4u) - (NULL)\n", i, j);
      }
    }
  }
}

GStaticMutex my_mutex = G_STATIC_MUTEX_INIT;

static GdaDataModel *
execute_query (RhythmDBGda *db, const gchar *query)
{
  GdaDataModel *model;
  GdaCommand *command;
  
  g_print ("Executing Query:    %s\n", query);
  command = gda_command_new (query, GDA_COMMAND_TYPE_SQL, GDA_COMMAND_OPTION_STOP_ON_ERRORS);
  g_static_mutex_lock (&my_mutex);
  model = gda_connection_execute_single_command (db->conn, command, NULL);
  g_static_mutex_unlock (&my_mutex);
  gda_command_free (command);
  if (model) {
    dump_model (model);
  } else {
    g_warning ("query '%s' failed", query);
  }
  
  return model;
}

static gboolean
execute_nonquery (RhythmDBGda *db, const gchar *query)
{
  gboolean ret;
  GdaCommand *command;
  
  g_print ("Executing NonQuery: %s\n", query);
  command = gda_command_new (query, GDA_COMMAND_TYPE_SQL, GDA_COMMAND_OPTION_STOP_ON_ERRORS);
  g_static_mutex_lock (&my_mutex);
  ret = gda_connection_execute_non_query (db->conn, command, NULL) != -1;
  g_static_mutex_unlock (&my_mutex);
  gda_command_free (command);
  if (!ret)
    g_warning ("query '%s' failed", query);

  return ret;
}

#define TABLE "tracks"
static gboolean
ensure_table_exists (RhythmDBGda *db)
{
  GdaDataModel *model;
  guint i;
  GString *s;
  gboolean ret;
  
  model = gda_connection_get_schema (db->conn, GDA_CONNECTION_SCHEMA_TABLES, NULL);
  g_assert (model);
  dump_model (model);

  for (i = 0; i < gda_data_model_get_n_rows (model); i++) {
    const GdaValue *value = gda_data_model_get_value_at (model, i, 0);
    if (g_str_equal (gda_value_get_string (value), TABLE)) {
      g_print ("Table %s already exists. Great!\n", TABLE);
      return TRUE;
    }
  }
  /* create the table */
  s = g_string_new ("create table " TABLE " (refcount INTEGER, ");
  for (i = 0; i < RHYTHMDB_NUM_PROPERTIES; i++) {
    GType type = rhythmdb_get_property_type (RHYTHMDB (db), i);
    if (i > 0)
      g_string_append (s, ", ");
    g_string_append (s, rhythmdb_nice_elt_name_from_propid (RHYTHMDB (db), i));
    switch (type) {
      case G_TYPE_STRING:
	g_string_append_printf (s, " VARCHAR (200)");
	break;
      case G_TYPE_BOOLEAN:
	g_string_append_printf (s, " BOOLEAN");
	break;
      case G_TYPE_INT:
      case G_TYPE_LONG: /* FIXME */
      case G_TYPE_UINT64: /* FIXME */
	g_string_append_printf (s, " INTEGER");
	break;
      case G_TYPE_FLOAT:
      case G_TYPE_DOUBLE:
	g_string_append_printf (s, " FLOAT");
	break;
      default:
	g_warning ("unknown type %u", (guint) type);
	g_assert_not_reached ();
	break;
    }
  }
  /* optimizations */
  if (i == RHYTHMDB_PROP_LOCATION) {
    /* location is unique */
    g_string_append (s, " UNIQUE"); 
  }
  g_string_append (s, ")");
  ret =  execute_nonquery (db, s->str);
  g_string_free (s, TRUE);
  if (ret) {
    /* refcounting with autodelete (woohoo!) */
    ret = execute_nonquery (db, "create trigger delete_track after update of refcount on "
	TABLE " when new.refcount = 0 begin delete from " TABLE 
	" where _rowid_ = new._rowid_; end");
  }
  return ret;
}

static gchar *
collect_value_for_sql (const GValue *val) 
{
  gchar *value;
  
  switch (G_VALUE_TYPE (val)) {
    case G_TYPE_STRING:
      value = escape_string (g_value_get_string (val));
      break;
    case G_TYPE_BOOLEAN:
      value = g_strdup (g_value_get_boolean (val) ? "\"TRUE\"" : "\"FALSE\"");
      break;
    case G_TYPE_INT:
      value = g_strdup_printf ("%d", g_value_get_int (val));
      break;
    case G_TYPE_LONG:
      value = g_strdup_printf ("%ld", g_value_get_long (val));
      break;
    case G_TYPE_UINT64:
      value = g_strdup_printf ("%"G_GUINT64_FORMAT, g_value_get_uint64 (val));
      break;
    case G_TYPE_FLOAT:
      value = g_strdup_printf ("%f", g_value_get_float (val));
      break;
    case G_TYPE_DOUBLE:
      value = g_strdup_printf ("%g", g_value_get_double (val));
      break;
    default:
      g_assert_not_reached ();
      return NULL;
  }
  return value;
}

static void
collect_value_from_sql (GValue *dest, const GdaValue *src)
{
  const gchar *str;
  
  if (gda_value_isa (src, GDA_VALUE_TYPE_NULL))
    return;
  g_assert (gda_value_isa (src, GDA_VALUE_TYPE_STRING));
  str = gda_value_get_string (src);
  
  /* keep in sync with create table */
  switch (G_VALUE_TYPE (dest)) {
    case G_TYPE_STRING:
      g_value_set_string (dest, str);
      break;
    case G_TYPE_BOOLEAN:
      g_value_set_boolean (dest, g_str_equal (str, "TRUE"));
      break;
    case G_TYPE_INT:
      g_value_set_int (dest, strtol (str, NULL, 10));
      break;
    case G_TYPE_LONG:
      g_value_set_long (dest, strtol (str, NULL, 10));
      break;
    case G_TYPE_UINT64:
      g_value_set_uint64 (dest, strtoul (str, NULL, 10));
      break;
    case G_TYPE_FLOAT:
      g_value_set_float (dest, atof (str));
      break;
    case G_TYPE_DOUBLE:
      g_value_set_double (dest, atof (str));
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}
  
static gboolean
_initialize (RhythmDBGda *db)
{
  /* check songs table */
  if (!ensure_table_exists (db))
    return FALSE;
  
  return execute_nonquery (db, "update " TABLE " set refcount=1");
}

RhythmDB *
rhythmdb_gda_new (const char *name)
{
  RhythmDBGda *db = g_object_new (RHYTHMDB_TYPE_GDA, "name", name, NULL);
  gchar *conn_str = g_strdup_printf ("URI=%s", name);

  g_print ("opening Db with conn string: %s\n", conn_str);
  db->client = gda_client_new ();
  g_return_val_if_fail (db->client, NULL);
  db->conn = gda_client_open_connection_from_string (db->client, "SQLite",
      conn_str, 0);
  g_free (conn_str);
  if (!db->conn) {
    g_warning ("GDA: error opening the library");
    g_object_unref (db->client);
    g_object_unref (db);
    return NULL;
  }
  if (!_initialize (db)) {
    g_warning ("GDA: error initializing the library");
    g_object_unref (db->conn);
    g_object_unref (db->client);
    g_object_unref (db);
    return NULL;
  }
  
  return RHYTHMDB (db);
}

static void 
rhythmdb_gda_finalize (GObject * object)
{
  RhythmDBGda *db = RHYTHMDB_GDA (object);

  g_object_unref (db->conn);
  g_object_unref (db->client);

  parent_class->finalize (object);
}

static void
rhythmdb_gda_load (RhythmDB * rdb, gboolean * die)
{
  guint i, j;
  static guint types[] = { RHYTHMDB_PROP_TITLE, RHYTHMDB_PROP_ARTIST, 
      RHYTHMDB_PROP_ALBUM, RHYTHMDB_PROP_LAST_PLAYED };
  RhythmDBGda *db = RHYTHMDB_GDA (rdb);
  GdaDataModel *model = execute_query (db, "select _rowid_, title, artist, album, last_played from " TABLE);
  g_return_if_fail (model);
    
  for (i = 0; i < gda_data_model_get_n_rows (model); i++) {
    gpointer entry = GINT_TO_POINTER ((gint) strtol (gda_value_get_string (
	    gda_data_model_get_value_at (model, 0, i)), NULL, 10));
    for (j = 0; j < G_N_ELEMENTS (types); j++) {
      GValue val = {0, };
      g_value_init (&val, rhythmdb_get_property_type (rdb, types [j]));
      collect_value_from_sql (&val,  gda_data_model_get_value_at (model, j + 1, i));
      rhythmdb_entry_sync_mirrored (rdb, entry, types [j], &val);
      g_value_unset (&val);
    }
    rhythmdb_emit_entry_restored (rdb, entry);
  }
}

static void
rhythmdb_gda_save (RhythmDB * rdb)
{
  /* nothing to do here */
}

static RhythmDBEntry *
rhythmdb_gda_entry_new (RhythmDB * rdb, RhythmDBEntryType type, const char *uri)
{
  RhythmDBGda *db = RHYTHMDB_GDA (rdb);
  gchar *query = g_strdup_printf ("insert into " TABLE 
      " (type, refcount, location) values (%d, 1, \"%s\")", (gint) type, uri);
  
  if (!execute_nonquery (db, query)) {
    g_free (query);
    return NULL;
  }
  g_free (query);
  return rhythmdb_gda_entry_lookup_by_location (rdb, uri);
}

static void
rhythmdb_gda_entry_set (RhythmDB * rdb, RhythmDBEntry * entry,
    guint propid, const GValue * value)
{
  RhythmDBGda *db = RHYTHMDB_GDA (rdb);
  gchar *collect = collect_value_for_sql (value);
  gchar *query = g_strdup_printf ("update " TABLE " set %s = %s where _rowid_ = %d",
      rhythmdb_nice_elt_name_from_propid (rdb, propid), collect,
      GPOINTER_TO_INT (entry));

  execute_nonquery (db, query);
  g_free (query);  
}

static void
rhythmdb_gda_entry_get (RhythmDB * rdb, RhythmDBEntry * entry,
    guint propid, GValue * value)
{
  RhythmDBGda *db = RHYTHMDB_GDA (rdb);
  gchar *query = g_strdup_printf ("select %s from " TABLE 
      " where _ROWID_ = %d", rhythmdb_nice_elt_name_from_propid (rdb, propid),
      GPOINTER_TO_INT (entry));
  GdaDataModel *model = execute_query (db, query);

  g_free (query);
  if (!model) return;
  
  if (gda_data_model_get_n_rows (model) > 0) {
    g_assert (gda_data_model_get_n_rows (model) == 1);
    collect_value_from_sql (value, gda_data_model_get_value_at (model, 0, 0));
  }
  g_object_unref (G_OBJECT (model));
}

void
rhythmdb_gda_ref (RhythmDBGda *db, gint id, gint count)
{
  gchar *query = g_strdup_printf ("select refcount from " TABLE 
      " where _ROWID_ = %d", id);
  GdaDataModel *model = execute_query (db, query);

  g_free (query);
  g_assert (model);
  
  g_assert (gda_data_model_get_n_rows (model) == 1);
  count += strtol (gda_value_get_string (
	gda_data_model_get_value_at (model, 0, 0)), NULL, 10);
  g_object_unref (model);
  
  query = g_strdup_printf ("update " TABLE " set refcount = %d where _ROWID_ = %d",
      count, id);
  execute_nonquery (db, query);
  g_free (query);
}

static void
rhythmdb_gda_entry_delete (RhythmDB * rdb, RhythmDBEntry * entry)
{
  rhythmdb_gda_ref (RHYTHMDB_GDA (rdb), GPOINTER_TO_INT (entry), -1);
}

static void
rhythmdb_gda_entry_delete_by_type (RhythmDB * rdb, RhythmDBEntryType type)
{
  g_assert_not_reached ();
}

static RhythmDBEntry *
rhythmdb_gda_entry_lookup_by_location (RhythmDB * rdb, const char *uri)
{
  gpointer ret;
  RhythmDBGda *db = RHYTHMDB_GDA (rdb);
  gchar *escaped_uri = escape_string (uri);
  gchar *query = g_strdup_printf ("select _ROWID_ from " TABLE 
      " where location = %s", escaped_uri);
  GdaDataModel *model = execute_query (db, query);

  g_free (escaped_uri);
  g_free (query);
  if (!model) return NULL;
  
  if (gda_data_model_get_n_rows (model) > 0) {
    g_assert (gda_data_model_get_n_rows (model) == 1);
    ret = GINT_TO_POINTER (strtol (gda_value_get_string (
	    gda_data_model_get_value_at (model, 0, 0)), NULL, 10));
  } else {
    ret = NULL;
  }
  g_object_unref (G_OBJECT (model));
  g_print ("FOUND ENTRY %p\n", ret);
  return ret;
}

static gchar *
translate_query (RhythmDBGda *db, RhythmDBQueryData *data)
{
  gchar *operation = NULL, *value, *ret;
  
  switch (data->type) {
    case RHYTHMDB_QUERY_DISJUNCTION:
    case RHYTHMDB_QUERY_SUBQUERY:
      g_assert_not_reached (); /* FIXME */
      return NULL;
    case RHYTHMDB_QUERY_PROP_EQUALS:
      operation = "%s = %s";
      break;
    case RHYTHMDB_QUERY_PROP_LIKE:
    case RHYTHMDB_QUERY_PROP_NOT_LIKE:
      g_assert_not_reached (); /* FIXME */
      break;
    case RHYTHMDB_QUERY_PROP_GREATER:
      operation = "%s > %s";
      break;
    case RHYTHMDB_QUERY_PROP_LESS:
      operation = "%s < %s";
      break;
    case RHYTHMDB_QUERY_END:
    default:
      g_assert_not_reached ();
      return NULL;
  }
  /* collect value */
  value = collect_value_for_sql (data->val);
  ret = g_strdup_printf (operation, rhythmdb_nice_elt_name_from_propid (RHYTHMDB (db), data->propid),
      value);
  g_free (value);
  
  return ret;
}

/* set rowid to 0 for all rows */
static GdaDataModel *
do_query (RhythmDBGda *db, GPtrArray * query, gint rowid)
{
  guint i;
  RhythmDBQueryData *data;
  GString *s = g_string_new (NULL);
  GdaDataModel *model;
  gchar *tmp;
  
  g_assert (query->len == 1);
  g_string_append (s, "select _ROWID_ from " TABLE " where ");
  if (rowid)
    g_string_append_printf (s, "rowid == %d AND (", rowid);
  for (i = 0; i < query->len; i++) {
    data = (RhythmDBQueryData *) g_ptr_array_index (query, i);
    tmp = translate_query (db, data);
    if (i > 0)
      g_string_append (s, " and ");
    g_string_append (s, tmp);
    g_free (tmp);
  }
  if (rowid)
    g_string_append (s, ")");

  model = execute_query (db, s->str);
  g_string_free (s, TRUE);
  return model;
}

static void
rhythmdb_gda_do_full_query (RhythmDB * rdb, GPtrArray * query,
    GtkTreeModel * main_model, gboolean * cancel)
{
  RhythmDBGda *db = RHYTHMDB_GDA (rdb);
  GdaDataModel *model = do_query (db, query, 0);
  g_return_if_fail (model);

  /* now the cludge */
  {
    int j;
    GPtrArray *queue;
    
    queue = g_ptr_array_sized_new (gda_data_model_get_n_rows (model));
    for (j = 0; j < gda_data_model_get_n_rows (model); j++) {
      g_ptr_array_add (queue, GINT_TO_POINTER (strtol (gda_value_get_string (
	    gda_data_model_get_value_at (model, 0, j)), NULL, 10)));
    }
    rhythmdb_query_model_add_entries (RHYTHMDB_QUERY_MODEL (main_model), queue);
  }
}

static gboolean
rhythmdb_gda_evaluate_query (RhythmDB * rdb, GPtrArray * query,
    RhythmDBEntry * aentry)
{
  gboolean ret;
  RhythmDBGda *db = RHYTHMDB_GDA (rdb);
  GdaDataModel *model = do_query (db, query, GPOINTER_TO_INT (aentry));

  if (!model) return FALSE;
  ret = gda_data_model_get_n_rows (model) > 0;
  g_object_unref (model);
  return ret;
}
