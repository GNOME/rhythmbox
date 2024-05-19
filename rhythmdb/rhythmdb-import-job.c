/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2007  Jonathan Matthew  <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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

#include <glib/gi18n.h>

#include "rhythmdb-import-job.h"
#include "rhythmdb-entry-type.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-debug.h"
#include "rb-missing-plugins.h"
#include "rb-task-progress.h"

/* maximum number of new URIs in the rhythmdb action queue.
 * entries bounce around between different threads and processes a bit,
 * so having multiple in flight should help.  we also want to be able to
 * cancel import jobs quickly.  since we can't remove things from the
 * action queue, having fewer entries helps.
 */
#define PROCESSING_LIMIT		20

enum
{
	PROP_0,
	PROP_DB,
	PROP_ENTRY_TYPE,
	PROP_IGNORE_TYPE,
	PROP_ERROR_TYPE,
	PROP_TASK_LABEL,
	PROP_TASK_DETAIL,
	PROP_TASK_PROGRESS,
	PROP_TASK_OUTCOME,
	PROP_TASK_NOTIFY,
	PROP_TASK_CANCELLABLE
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
static void	rhythmdb_import_job_task_progress_init (RBTaskProgressInterface *iface);

static guint	signals[LAST_SIGNAL] = { 0 };

struct _RhythmDBImportJobPrivate
{
	int		total;
	int		imported;
	int		processed;
	GQueue		*outstanding;
	GQueue		*processing;
	RhythmDB	*db;
	RhythmDBEntryType *entry_type;
	RhythmDBEntryType *ignore_type;
	RhythmDBEntryType *error_type;
	GMutex		lock;
	GSList		*uri_list;
	GSList		*next;
	gboolean	started;
	GCancellable    *cancel;

	GSList		*retry_entries;
	gboolean	retried;

	int		status_changed_id;
	gboolean	scan_complete;
	gboolean	complete;

	char		*task_label;
	gboolean	task_notify;
};

G_DEFINE_TYPE_EXTENDED (RhythmDBImportJob,
			rhythmdb_import_job,
			G_TYPE_OBJECT,
			0,
			G_IMPLEMENT_INTERFACE (RB_TYPE_TASK_PROGRESS, rhythmdb_import_job_task_progress_init));

/**
 * SECTION:rhythmdbimportjob
 * @short_description: batch import job
 *
 * Tracks the addition to the database of files under a set of 
 * directories, providing status information.
 *
 * The entry types to use for the database entries added by the import
 * job are specified on creation.
 */

/**
 * rhythmdb_import_job_new:
 * @db: the #RhythmDB object
 * @entry_type: the #RhythmDBEntryType to use for normal entries
 * @ignore_type: the #RhythmDBEntryType to use for ignored files
 *   (or NULL to not create ignore entries)
 * @error_type: the #RhythmDBEntryType to use for import error
 *   entries (or NULL for none)
 *
 * Creates a new import job with the specified entry types.
 * Before starting the job, the caller must add one or more
 * paths to import.
 *
 * Return value: new #RhythmDBImportJob object.
 */
RhythmDBImportJob *
rhythmdb_import_job_new (RhythmDB *db,
			 RhythmDBEntryType *entry_type,
			 RhythmDBEntryType *ignore_type,
			 RhythmDBEntryType *error_type)
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

/**
 * rhythmdb_import_job_add_uri:
 * @job: a #RhythmDBImportJob
 * @uri: the URI to import
 *
 * Adds a URI to import.  All files under the specified
 * URI will be imported.
 */
void
rhythmdb_import_job_add_uri (RhythmDBImportJob *job, const char *uri)
{
	g_assert (job->priv->started == FALSE);

	g_mutex_lock (&job->priv->lock);
	job->priv->uri_list = g_slist_prepend (job->priv->uri_list, g_strdup (uri));
	g_mutex_unlock (&job->priv->lock);
}

/**
 * rhythmdb_import_job_includes_uri:
 * @job: a #RhythmDBImportJob
 * @uri: a URI to check
 *
 * Checks if the specified URI is included in the import job.
 *
 * Return value: %TRUE if the import job includes the URI
 */
gboolean
rhythmdb_import_job_includes_uri (RhythmDBImportJob *job, const char *uri)
{
	gboolean result = FALSE;
	GSList *i;

	g_mutex_lock (&job->priv->lock);
	i = job->priv->uri_list;
	while (i != NULL) {
		const char *luri = i->data;
		if ((g_strcmp0 (uri, luri) == 0) ||
		    rb_uri_is_descendant (uri, luri)) {
			result = TRUE;
			break;
		}

		i = i->next;
	}
	g_mutex_unlock (&job->priv->lock);

	return result;
}

/* must be called with lock held */
static void
maybe_start_more (RhythmDBImportJob *job)
{
	if (g_cancellable_is_cancelled (job->priv->cancel)) {
		return;
	}

	while (g_queue_get_length (job->priv->processing) < PROCESSING_LIMIT) {
		char *uri;

		uri = g_queue_pop_head (job->priv->outstanding);
		if (uri == NULL) {
			return;
		}

		g_queue_push_tail (job->priv->processing, uri);

		rhythmdb_add_uri_with_types (job->priv->db,
					     uri,
					     job->priv->entry_type,
					     job->priv->ignore_type,
					     job->priv->error_type);
	}
}

static void
missing_plugins_retry_cb (gpointer instance, gboolean installed, RhythmDBImportJob *job)
{
	GSList *i;

	g_mutex_lock (&job->priv->lock);
	g_assert (job->priv->retried == FALSE);
	if (installed == FALSE) {
		rb_debug ("plugin installation was not successful; job complete");
		job->priv->complete = TRUE;
		g_signal_emit (job, signals[COMPLETE], 0, job->priv->total);
		g_object_notify (G_OBJECT (job), "task-outcome");
	} else {
		job->priv->retried = TRUE;

		/* reset the job state to just show the retry information */
		job->priv->total = g_slist_length (job->priv->retry_entries);
		rb_debug ("plugin installation was successful, retrying %d entries", job->priv->total);
		job->priv->processed = 0;

		/* remove the import error entries and build the list of URIs to retry */
		for (i = job->priv->retry_entries; i != NULL; i = i->next) {
			RhythmDBEntry *entry = (RhythmDBEntry *)i->data;
			char *uri;

			uri = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_LOCATION);
			rhythmdb_entry_delete (job->priv->db, entry);

			g_queue_push_tail (job->priv->outstanding, g_strdup (uri));
		}
		rhythmdb_commit (job->priv->db);
	}

	maybe_start_more (job);

	g_mutex_unlock (&job->priv->lock);
}

static gboolean
emit_status_changed (RhythmDBImportJob *job)
{
	g_mutex_lock (&job->priv->lock);
	job->priv->status_changed_id = 0;

	rb_debug ("emitting status changed: %d/%d", job->priv->processed, job->priv->total);
	g_signal_emit (job, signals[STATUS_CHANGED], 0, job->priv->total, job->priv->processed);
	g_object_notify (G_OBJECT (job), "task-progress");
	g_object_notify (G_OBJECT (job), "task-detail");

	/* temporary ref while emitting this signal as we're expecting the caller
	 * to release the final reference there.
	 */
	g_object_ref (job);
	if (job->priv->scan_complete && job->priv->processed >= job->priv->total) {

		if (job->priv->retry_entries != NULL && job->priv->retried == FALSE) {
			gboolean processing = FALSE;
			char **details = NULL;
			GClosure *retry;
			GSList *l;
			int i;

			/* gather missing plugin details etc. */
			i = 0;
			for (l = job->priv->retry_entries; l != NULL; l = l->next) {
				RhythmDBEntry *entry;
				char **bits;
				int j;

				entry = (RhythmDBEntry *)l->data;
				bits = g_strsplit (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_COMMENT), "\n", 0);
				for (j = 0; bits[j] != NULL; j++) {
					if (rb_str_in_strv (bits[j], (const char **)details) == FALSE) {
						details = g_realloc (details, sizeof (char *) * (i+2));
						details[i++] = g_strdup (bits[j]);
						details[i] = NULL;
					}
				}
				g_strfreev (bits);
			}

			retry = g_cclosure_new ((GCallback) missing_plugins_retry_cb,
						g_object_ref (job),
						(GClosureNotify)g_object_unref);
			g_closure_set_marshal (retry, g_cclosure_marshal_VOID__BOOLEAN);

			processing = rb_missing_plugins_install ((const char **)details, FALSE, retry);
			g_strfreev (details);
			if (processing) {
				rb_debug ("plugin installation is in progress");
			} else {
				rb_debug ("no plugin installation attempted; job complete");
				job->priv->complete = TRUE;
				g_signal_emit (job, signals[COMPLETE], 0, job->priv->total);
				g_object_notify (G_OBJECT (job), "task-outcome");
			}
			g_closure_sink (retry);
		} else {
			rb_debug ("emitting job complete");
			job->priv->complete = TRUE;
			g_signal_emit (job, signals[COMPLETE], 0, job->priv->total);
			g_object_notify (G_OBJECT (job), "task-outcome");
		}
	} else if (g_cancellable_is_cancelled (job->priv->cancel) &&
		   g_queue_is_empty (job->priv->processing)) {
		rb_debug ("cancelled job has no processing entries, emitting complete");
		job->priv->complete = TRUE;
		g_signal_emit (job, signals[COMPLETE], 0, job->priv->total);
		g_object_notify (G_OBJECT (job), "task-outcome");
	}
	g_mutex_unlock (&job->priv->lock);
	g_object_unref (job);

	return FALSE;
}

static gboolean
uri_recurse_func (GFile *file, GFileInfo *info, RhythmDBImportJob *job)
{
	RhythmDBEntry *entry;
	char *uri;

	if (g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_STANDARD_TYPE) == G_FILE_TYPE_DIRECTORY) {
		return TRUE;
	}

	if (g_cancellable_is_cancelled (job->priv->cancel))
		return FALSE;

	if (g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK) ||
	    g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET)) {
		GFile *r;
		r = rb_file_resolve_symlink (file, NULL);
		if (r != NULL) {
			uri = g_file_get_uri (r);
			g_object_unref (r);

			if (rhythmdb_import_job_includes_uri (job, uri)) {
				rb_debug ("symlink target %s already included", uri);
				g_free (uri);
				return TRUE;
			}
		} else {
			uri = g_file_get_uri (file);
			rb_debug ("unable to resolve symlink %s", uri);
			g_free (uri);
			return TRUE;
		}
	} else {
		uri = g_file_get_uri (file);
	}

	/* if it's not already in the db, add it to the list of things to process */
	entry = rhythmdb_entry_lookup_by_location (job->priv->db, uri);
	if (entry == NULL) {
		rb_debug ("waiting for entry %s", uri);
		g_mutex_lock (&job->priv->lock);
		job->priv->total++;
		g_queue_push_tail (job->priv->outstanding, g_strdup (uri));

		if (job->priv->status_changed_id == 0) {
			job->priv->status_changed_id = g_idle_add ((GSourceFunc) emit_status_changed, job);
		}

		maybe_start_more (job);

		g_mutex_unlock (&job->priv->lock);
	} else {
		/* skip it if it's a different entry type */
		RhythmDBEntryType *et;
		et = rhythmdb_entry_get_entry_type (entry);
		if (et == job->priv->entry_type ||
		    et == job->priv->ignore_type ||
		    et == job->priv->error_type) {
			rhythmdb_add_uri_with_types (job->priv->db,
						     uri,
						     job->priv->entry_type,
						     job->priv->ignore_type,
						     job->priv->error_type);
		}
	}

	g_free (uri);
	return TRUE;
}

static gboolean
emit_scan_complete_idle (RhythmDBImportJob *job)
{
	rb_debug ("emitting scan complete");
	g_signal_emit (job, signals[SCAN_COMPLETE], 0, job->priv->total);
	emit_status_changed (job);
	g_object_unref (job);
	return FALSE;
}

static void
next_uri (RhythmDBImportJob *job)
{
	g_mutex_lock (&job->priv->lock);
	if (job->priv->next == NULL) {
		rb_debug ("no more uris to scan");
		job->priv->scan_complete = TRUE;
		g_idle_add ((GSourceFunc)emit_scan_complete_idle, job);
	} else {
		const char *uri = job->priv->next->data;
		job->priv->next = job->priv->next->next;

		rb_debug ("scanning uri %s", uri);
		rb_uri_handle_recursively_async (uri,
						 job->priv->cancel,
						 (RBUriRecurseFunc) uri_recurse_func,
						 job,
						 (GDestroyNotify) next_uri);
	}
	g_mutex_unlock (&job->priv->lock);
}

/**
 * rhythmdb_import_job_start:
 * @job: the #RhythmDBImportJob
 *
 * Starts the import job.  After this method has been called,
 * no more URIs may be added to the import job.  May only be
 * called once for a given import job.
 */
void
rhythmdb_import_job_start (RhythmDBImportJob *job)
{
	g_assert (job->priv->started == FALSE);

	rb_debug ("starting");
	g_mutex_lock (&job->priv->lock);
	job->priv->started = TRUE;
	job->priv->uri_list = g_slist_reverse (job->priv->uri_list);
	job->priv->next = job->priv->uri_list;
	g_mutex_unlock (&job->priv->lock);
	
	/* reference is released in emit_scan_complete_idle */
	next_uri (g_object_ref (job));
}

/**
 * rhythmdb_import_job_get_total:
 * @job: the #RhythmDBImportJob
 *
 * Returns the total number of files that will be processed by
 * this import job.  This increases as the import directories are
 * scanned.
 *
 * Return value: the total number of files to be processed
 */
int
rhythmdb_import_job_get_total (RhythmDBImportJob *job)
{
	return job->priv->total;
}

/**
 * rhythmdb_import_job_get_imported:
 * @job: the #RhythmDBImportJob
 *
 * Returns the number of files successfully imported by the import job so far.
 *
 * Return value: file count
 */
int
rhythmdb_import_job_get_imported (RhythmDBImportJob *job)
{
	return job->priv->imported;
}

/**
 * rhythmdb_import_job_get_processed:
 * @job: the #RhythmDBImportJob
 *
 * Returns the number of files processed by the import job so far.
 *
 * Return value: file count
 */
int
rhythmdb_import_job_get_processed (RhythmDBImportJob *job)
{
	return job->priv->processed;
}

/**
 * rhythmdb_import_job_scan_complete:
 * @job: the #RhythmDBImportJob
 *
 * Returns whether the directory scan phase of the import job is complete.
 *
 * Return value: TRUE if complete
 */
gboolean
rhythmdb_import_job_scan_complete (RhythmDBImportJob *job)
{
	return job->priv->scan_complete;
}

/**
 * rhythmdb_import_job_complete:
 * @job: the #RhythmDBImportJob
 *
 * Returns whether the import job is complete.
 *
 * Return value: TRUE if complete.
 */
gboolean
rhythmdb_import_job_complete (RhythmDBImportJob *job)
{
	return job->priv->complete;
}

/**
 * rhythmdb_import_job_cancel:
 * @job: the #RhythmDBImportJob
 *
 * Cancels the import job.  The job will cease as soon
 * as possible.  More directories may be scanned and 
 * more files may be imported before the job actually
 * ceases.
 */
void
rhythmdb_import_job_cancel (RhythmDBImportJob *job)
{
	g_mutex_lock (&job->priv->lock);
	g_cancellable_cancel (job->priv->cancel);
	g_mutex_unlock (&job->priv->lock);

	g_object_notify (G_OBJECT (job), "task-outcome");
}

static void
task_progress_cancel (RBTaskProgress *progress)
{
	rhythmdb_import_job_cancel (RHYTHMDB_IMPORT_JOB (progress));
}

static void
entry_added_cb (RhythmDB *db,
		RhythmDBEntry *entry,
		RhythmDBImportJob *job)
{
	const char *uri;
	GList *link;

	uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);

	g_mutex_lock (&job->priv->lock);
	link = g_queue_find_custom (job->priv->processing, uri, (GCompareFunc) g_strcmp0);

	if (link != NULL) {
		const char *details;
		RhythmDBEntryType *entry_type;

		entry_type = rhythmdb_entry_get_entry_type (entry);

		job->priv->processed++;

		if (entry_type == job->priv->entry_type) {
			job->priv->imported++;
			g_signal_emit (job, signals[ENTRY_ADDED], 0, entry);
		}
		rb_debug ("got entry %s; %d imported, %d processed", uri, job->priv->imported, job->priv->processed);

		/* if it's an import error with missing plugins, add it to the retry list */
		details = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_COMMENT);
		if (entry_type == job->priv->error_type &&
		    (details != NULL && details[0] != '\0')) {
			rb_debug ("entry %s is an import error with missing plugin details: %s", uri, details);
			job->priv->retry_entries = g_slist_prepend (job->priv->retry_entries, rhythmdb_entry_ref (entry));
		}

		if (job->priv->status_changed_id == 0) {
			job->priv->status_changed_id = g_idle_add ((GSourceFunc) emit_status_changed, job);
		}

		g_queue_delete_link (job->priv->processing, link);
		maybe_start_more (job);
	}
	g_mutex_unlock (&job->priv->lock);
}

static void
rhythmdb_import_job_init (RhythmDBImportJob *job)
{
	job->priv = G_TYPE_INSTANCE_GET_PRIVATE (job,
						 RHYTHMDB_TYPE_IMPORT_JOB,
						 RhythmDBImportJobPrivate);

	g_mutex_init (&job->priv->lock);
	job->priv->outstanding = g_queue_new ();
	job->priv->processing = g_queue_new ();

	job->priv->cancel = g_cancellable_new ();
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
		job->priv->db = RHYTHMDB (g_value_dup_object (value));
		g_signal_connect_object (job->priv->db,
					 "entry-added",
					 G_CALLBACK (entry_added_cb),
					 job, 0);
		break;
	case PROP_ENTRY_TYPE:
		job->priv->entry_type = g_value_get_object (value);
		break;
	case PROP_IGNORE_TYPE:
		job->priv->ignore_type = g_value_get_object (value);
		break;
	case PROP_ERROR_TYPE:
		job->priv->error_type = g_value_get_object (value);
		break;
	case PROP_TASK_LABEL:
		job->priv->task_label = g_value_dup_string (value);
		break;
	case PROP_TASK_DETAIL:
		/* ignore */
		break;
	case PROP_TASK_PROGRESS:
		/* ignore */
		break;
	case PROP_TASK_OUTCOME:
		/* ignore */
		break;
	case PROP_TASK_NOTIFY:
		job->priv->task_notify = g_value_get_boolean (value);
		break;
	case PROP_TASK_CANCELLABLE:
		/* ignore */
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
		g_value_set_object (value, job->priv->entry_type);
		break;
	case PROP_IGNORE_TYPE:
		g_value_set_object (value, job->priv->ignore_type);
		break;
	case PROP_ERROR_TYPE:
		g_value_set_object (value, job->priv->error_type);
		break;
	case PROP_TASK_LABEL:
		g_value_set_string (value, job->priv->task_label);
		break;
	case PROP_TASK_DETAIL:
		if (job->priv->scan_complete == FALSE) {
			g_value_set_string (value, _("Scanning"));
		} else if (job->priv->total > 0) {
			g_value_take_string (value,
					     g_strdup_printf (_("%d of %d"),
							      job->priv->processed,
							      job->priv->total));
		}
		break;
	case PROP_TASK_PROGRESS:
		if (job->priv->scan_complete == FALSE) {
			g_value_set_double (value, -1.0);
		} else if (job->priv->total == 0) {
			g_value_set_double (value, 0.0);
		} else {
			g_value_set_double (value, ((float)job->priv->processed / (float)job->priv->total));
		}
		break;
	case PROP_TASK_OUTCOME:
		if (job->priv->complete) {
			g_value_set_enum (value, RB_TASK_OUTCOME_COMPLETE);
		} else if (g_cancellable_is_cancelled (job->priv->cancel)) {
			g_value_set_enum (value, RB_TASK_OUTCOME_CANCELLED);
		} else {
			g_value_set_enum (value, RB_TASK_OUTCOME_NONE);
		}
		break;
	case PROP_TASK_NOTIFY:
		g_value_set_boolean (value, job->priv->task_notify);
		break;
	case PROP_TASK_CANCELLABLE:
		g_value_set_boolean (value, TRUE);
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

	if (job->priv->cancel != NULL) {
		g_object_unref (job->priv->cancel);
		job->priv->cancel = NULL;
	}
	
	G_OBJECT_CLASS (rhythmdb_import_job_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	RhythmDBImportJob *job = RHYTHMDB_IMPORT_JOB (object);

	g_queue_free_full (job->priv->outstanding, g_free);
	g_queue_free_full (job->priv->processing, g_free);

	rb_slist_deep_free (job->priv->uri_list);

	g_free (job->priv->task_label);

	G_OBJECT_CLASS (rhythmdb_import_job_parent_class)->finalize (object);
}

static void
rhythmdb_import_job_task_progress_init (RBTaskProgressInterface *interface)
{
	interface->cancel = task_progress_cancel;
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
					 g_param_spec_object ("entry-type",
							      "Entry type",
							      "Entry type to use for entries added by this job",
							      RHYTHMDB_TYPE_ENTRY_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_IGNORE_TYPE,
					 g_param_spec_object ("ignore-type",
							      "Ignored entry type",
							      "Entry type to use for ignored entries added by this job",
							      RHYTHMDB_TYPE_ENTRY_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_ERROR_TYPE,
					 g_param_spec_object ("error-type",
							      "Error entry type",
							      "Entry type to use for import error entries added by this job",
							      RHYTHMDB_TYPE_ENTRY_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_override_property (object_class, PROP_TASK_LABEL, "task-label");
	g_object_class_override_property (object_class, PROP_TASK_DETAIL, "task-detail");
	g_object_class_override_property (object_class, PROP_TASK_PROGRESS, "task-progress");
	g_object_class_override_property (object_class, PROP_TASK_OUTCOME, "task-outcome");
	g_object_class_override_property (object_class, PROP_TASK_NOTIFY, "task-notify");
	g_object_class_override_property (object_class, PROP_TASK_CANCELLABLE, "task-cancellable");

	/**
	 * RhythmDBImportJob::entry-added:
	 * @job: the #RhythmDBImportJob
	 * @entry: the newly added #RhythmDBEntry
	 *
	 * Emitted when an entry has been added to the database by the
	 * import job.
	 */
	signals [ENTRY_ADDED] =
		g_signal_new ("entry-added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBImportJobClass, entry_added),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1, RHYTHMDB_TYPE_ENTRY);
	/**
	 * RhythmDBImportJob::status-changed:
	 * @job: the #RhythmDBImportJob
	 * @total: the current total number of files to process
	 * @imported: the current count of files imported
	 *
	 * Emitted when the status of the import job has changed.
	 */
	signals [STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBImportJobClass, status_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2, G_TYPE_INT, G_TYPE_INT);
	/**
	 * RhythmDBImportJob::scan-complete:
	 * @job: the #RhythmDBImportJob
	 * @total: the number of items scanned.
	 *
	 * Emitted when the directory scan is complete.  Once
	 * the scan is complete, the total number of files to
	 * be processed will not change.
	 */
	signals[SCAN_COMPLETE] =
		g_signal_new ("scan-complete",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBImportJobClass, scan_complete),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1, G_TYPE_INT);
	/**
	 * RhythmDBImportJob::complete:
	 * @job: the #RhythmDBImportJob
	 * @total: the number of items imported.
	 *
	 * Emitted when the whole import job is complete.
	 */
	signals[COMPLETE] =
		g_signal_new ("complete",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBImportJobClass, complete),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1, G_TYPE_INT);

	g_type_class_add_private (klass, sizeof (RhythmDBImportJobPrivate));
}
