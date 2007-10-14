/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2007  Jonathan Matthew  <jonathan@d14n.org>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include "rhythmdb-import-job.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-marshal.h"
#include "rb-debug.h"

enum
{
	PROP_0,
	PROP_DB,
	PROP_ENTRY_TYPE,
	PROP_IGNORE_TYPE,
	PROP_ERROR_TYPE
};

enum
{
	ENTRY_ADDED,
	STATUS_CHANGED,
	SCAN_COMPLETE,
	COMPLETE,
	LAST_SIGNAL
};

static void	rhythmdb_import_job_class_init (RhythmDBImportJobClass *klass);
static void	rhythmdb_import_job_init (RhythmDBImportJob *job);

static guint	signals[LAST_SIGNAL] = { 0 };

struct _RhythmDBImportJobPrivate
{
	int		total;
	int		imported;
	GHashTable	*outstanding;
	RhythmDB	*db;
	RhythmDBEntryType entry_type;
	RhythmDBEntryType ignore_type;
	RhythmDBEntryType error_type;
	GStaticMutex    lock;
	GSList		*uri_list;
	gboolean	started;
	gboolean	cancel;

	int		status_changed_id;
	gboolean	scan_complete;
	gboolean	complete;
};

G_DEFINE_TYPE (RhythmDBImportJob, rhythmdb_import_job, G_TYPE_OBJECT)

RhythmDBImportJob *
rhythmdb_import_job_new (RhythmDB *db,
			 RhythmDBEntryType entry_type,
			 RhythmDBEntryType ignore_type,
			 RhythmDBEntryType error_type)
{
	GObject *obj;

	obj = g_object_new (RHYTHMDB_TYPE_IMPORT_JOB,
			    "db", db,
			    "entry-type", entry_type,
			    "ignore-type", ignore_type,
			    "error-type", error_type,
			    NULL);
	return RHYTHMDB_IMPORT_JOB (obj);
}

void
rhythmdb_import_job_add_uri (RhythmDBImportJob *job, const char *uri)
{
	g_assert (job->priv->started == FALSE);

	g_static_mutex_lock (&job->priv->lock);
	job->priv->uri_list = g_slist_prepend (job->priv->uri_list, g_strdup (uri));
	g_static_mutex_unlock (&job->priv->lock);
}

static gboolean
emit_status_changed (RhythmDBImportJob *job)
{
	g_static_mutex_lock (&job->priv->lock);
	job->priv->status_changed_id = 0;

	rb_debug ("emitting status changed: %d/%d", job->priv->total, job->priv->imported);
	g_signal_emit (job, signals[STATUS_CHANGED], 0, job->priv->total, job->priv->imported);

	/* temporary ref while emitting this signal as we're expecting the caller
	 * to release the final reference there.
	 */
	g_object_ref (job);
	if (job->priv->scan_complete && job->priv->imported >= job->priv->total) {
		rb_debug ("emitting job complete");
		g_signal_emit (job, signals[COMPLETE], 0, job->priv->total);
	}
	g_static_mutex_unlock (&job->priv->lock);
	g_object_unref (job);

	return FALSE;
}

static void
uri_recurse_func (const char *uri, gboolean dir, RhythmDBImportJob *job)
{
	RhythmDBEntry *entry;

	if (dir) {
		return;
	}

	/* only add the entry to the map of entries we're waiting for
	 * if it's not already in the db.
	 */
	entry = rhythmdb_entry_lookup_by_location (job->priv->db, uri);
	if (entry == NULL) {
		rb_debug ("waiting for entry %s", uri);
		g_static_mutex_lock (&job->priv->lock);
		job->priv->total++;
		g_hash_table_insert (job->priv->outstanding, g_strdup (uri), GINT_TO_POINTER (1));

		if (job->priv->status_changed_id == 0) {
			job->priv->status_changed_id = g_idle_add ((GSourceFunc) emit_status_changed, job);
		}

		g_static_mutex_unlock (&job->priv->lock);
	}

	rhythmdb_add_uri_with_types (job->priv->db,
				     uri,
				     job->priv->entry_type,
				     job->priv->ignore_type,
				     job->priv->error_type);
}

static gboolean
emit_scan_complete_idle (RhythmDBImportJob *job)
{
	rb_debug ("emitting scan complete");
	g_signal_emit (job, signals[SCAN_COMPLETE], 0, job->priv->total);
	g_object_unref (job);
	return FALSE;
}

static void
next_uri (RhythmDBImportJob *job)
{
	g_static_mutex_lock (&job->priv->lock);
	if (job->priv->uri_list == NULL) {
		rb_debug ("no more uris to scan");
		job->priv->scan_complete = TRUE;
		g_idle_add ((GSourceFunc)emit_scan_complete_idle, job);
	} else {
		char *uri = job->priv->uri_list->data;
		job->priv->uri_list = g_slist_delete_link (job->priv->uri_list,
							   job->priv->uri_list);

		rb_debug ("scanning uri %s", uri);
		rb_uri_handle_recursively_async (uri,
						 (RBUriRecurseFunc) uri_recurse_func,
						 &job->priv->cancel,
						 job,
						 (GDestroyNotify) next_uri);

		g_free (uri);
	}
	g_static_mutex_unlock (&job->priv->lock);
}

void
rhythmdb_import_job_start (RhythmDBImportJob *job)
{
	g_assert (job->priv->started == FALSE);

	rb_debug ("starting");
	g_static_mutex_lock (&job->priv->lock);
	job->priv->started = TRUE;
	job->priv->uri_list = g_slist_reverse (job->priv->uri_list);
	g_static_mutex_unlock (&job->priv->lock);
	
	/* reference is released in emit_scan_complete_idle */
	next_uri (g_object_ref (job));
}

int
rhythmdb_import_job_get_total (RhythmDBImportJob *job)
{
	return job->priv->total;
}

int
rhythmdb_import_job_get_imported (RhythmDBImportJob *job)
{
	return job->priv->imported;
}

gboolean
rhythmdb_import_job_scan_complete (RhythmDBImportJob *job)
{
	return job->priv->scan_complete;
}

gboolean
rhythmdb_import_job_complete (RhythmDBImportJob *job)
{
	return job->priv->complete;
}

void
rhythmdb_import_job_cancel (RhythmDBImportJob *job)
{
	g_static_mutex_lock (&job->priv->lock);
	job->priv->cancel = TRUE;
	g_static_mutex_unlock (&job->priv->lock);
}

static void
entry_added_cb (RhythmDB *db,
		RhythmDBEntry *entry,
		RhythmDBImportJob *job)
{
	const char *uri;
	gboolean ours;

	uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);

	g_static_mutex_lock (&job->priv->lock);
	ours = g_hash_table_remove (job->priv->outstanding, uri);

	if (ours) {
		job->priv->imported++;
		rb_debug ("got entry %s; %d now imported", uri, job->priv->imported);
		g_signal_emit (job, signals[ENTRY_ADDED], 0, entry);

		if (job->priv->status_changed_id == 0) {
			job->priv->status_changed_id = g_idle_add ((GSourceFunc) emit_status_changed, job);
		}
	}
	g_static_mutex_unlock (&job->priv->lock);
}

static void
rhythmdb_import_job_init (RhythmDBImportJob *job)
{
	job->priv = G_TYPE_INSTANCE_GET_PRIVATE (job,
						 RHYTHMDB_TYPE_IMPORT_JOB,
						 RhythmDBImportJobPrivate);

	g_static_mutex_init (&job->priv->lock);
	job->priv->outstanding = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
impl_set_property (GObject *object,
		   guint prop_id,
		   const GValue *value,
		   GParamSpec *pspec)
{
	RhythmDBImportJob *job = RHYTHMDB_IMPORT_JOB (object);

	switch (prop_id) {
	case PROP_DB:
		job->priv->db = g_value_dup_object (value);
		g_signal_connect_object (job->priv->db,
					 "entry-added",
					 G_CALLBACK (entry_added_cb),
					 job, 0);
		break;
	case PROP_ENTRY_TYPE:
		job->priv->entry_type = g_value_get_boxed (value);
		break;
	case PROP_IGNORE_TYPE:
		job->priv->ignore_type = g_value_get_boxed (value);
		break;
	case PROP_ERROR_TYPE:
		job->priv->error_type = g_value_get_boxed (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object,
		   guint prop_id,
		   GValue *value,
		   GParamSpec *pspec)
{
	RhythmDBImportJob *job = RHYTHMDB_IMPORT_JOB (object);

	switch (prop_id) {
	case PROP_DB:
		g_value_set_object (value, job->priv->db);
		break;
	case PROP_ENTRY_TYPE:
		g_value_set_boxed (value, job->priv->entry_type);
		break;
	case PROP_IGNORE_TYPE:
		g_value_set_boxed (value, job->priv->ignore_type);
		break;
	case PROP_ERROR_TYPE:
		g_value_set_boxed (value, job->priv->error_type);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}
static void
impl_dispose (GObject *object)
{
	RhythmDBImportJob *job = RHYTHMDB_IMPORT_JOB (object);

	if (job->priv->db != NULL) {
		g_object_unref (job->priv->db);
		job->priv->db = NULL;
	}
	
	G_OBJECT_CLASS (rhythmdb_import_job_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	RhythmDBImportJob *job = RHYTHMDB_IMPORT_JOB (object);
	
	g_hash_table_destroy (job->priv->outstanding);

	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, job->priv->entry_type);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, job->priv->ignore_type);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, job->priv->error_type);
	
	rb_slist_deep_free (job->priv->uri_list);

	g_static_mutex_free (&job->priv->lock);
	
	G_OBJECT_CLASS (rhythmdb_import_job_parent_class)->finalize (object);
}

static void
rhythmdb_import_job_class_init (RhythmDBImportJobClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;
	object_class->dispose = impl_dispose;
	object_class->finalize = impl_finalize;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
						 	      "db",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_ENTRY_TYPE,
					 g_param_spec_boxed ("entry-type",
						 	     "Entry type",
							     "Entry type to use for entries added by this job",
							     RHYTHMDB_TYPE_ENTRY_TYPE,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_IGNORE_TYPE,
					 g_param_spec_boxed ("ignore-type",
						 	     "Ignored entry type",
							     "Entry type to use for ignored entries added by this job",
							     RHYTHMDB_TYPE_ENTRY_TYPE,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_ERROR_TYPE,
					 g_param_spec_boxed ("error-type",
						 	     "Error entry type",
							     "Entry type to use for import error entries added by this job",
							     RHYTHMDB_TYPE_ENTRY_TYPE,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	signals [ENTRY_ADDED] =
		g_signal_new ("entry-added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBImportJobClass, entry_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE,
			      1, RHYTHMDB_TYPE_ENTRY);
	signals [STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBImportJobClass, status_changed),
			      NULL, NULL,
			      rb_marshal_VOID__INT_INT,
			      G_TYPE_NONE,
			      2, G_TYPE_INT, G_TYPE_INT);
	signals[SCAN_COMPLETE] =
		g_signal_new ("scan-complete",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBImportJobClass, scan_complete),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE,
			      1, G_TYPE_INT);
	signals[COMPLETE] =
		g_signal_new ("complete",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBImportJobClass, complete),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE,
			      1, G_TYPE_INT);

	g_type_class_add_private (klass, sizeof (RhythmDBImportJobPrivate));
}

