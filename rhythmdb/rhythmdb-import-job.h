/*
 *  Copyright (C) 2007  Jonathan Matthew <jonathan@d14n.org>
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

#ifndef RHYTHMDB_IMPORT_JOB_H
#define RHYTHMDB_IMPORT_JOB_H

#include <glib.h>
#include <glib-object.h>

#include <rhythmdb/rhythmdb.h>

G_BEGIN_DECLS

#define RHYTHMDB_TYPE_IMPORT_JOB		(rhythmdb_import_job_get_type ())
#define RHYTHMDB_IMPORT_JOB(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_IMPORT_JOB, RhythmDBImportJob))
#define RHYTHMDB_IMPORT_JOB_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_TYPE_IMPORT_JOB, RhythmDBImportJobClass))
#define RHYTHMDB_IS_IMPORT_JOB(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_IMPORT_JOB))
#define RHYTHMDB_IS_IMPORT_JOB_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), RHYTHMDB_TYPE_IMPORT_JOB))
#define RHYTHMDB_IMPORT_JOB_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RHYTHMDB_TYPE_IMPORT_JOB, RhythmDBImportJobClass))

typedef struct _RhythmDBImportJob 	RhythmDBImportJob;
typedef struct _RhythmDBImportJobClass 	RhythmDBImportJobClass;

typedef struct _RhythmDBImportJobPrivate RhythmDBImportJobPrivate;

struct _RhythmDBImportJob
{
	GObject parent;
	RhythmDBImportJobPrivate *priv;
};

struct _RhythmDBImportJobClass
{
	GObjectClass parent_class;

	/* signals */
	void (*entry_added) (RhythmDBImportJob *job, RhythmDBEntry *entry);
	void (*status_changed) (RhythmDBImportJob *job, int total, int imported);
	void (*scan_complete) (RhythmDBImportJob *job, int total);
	void (*complete) (RhythmDBImportJob *job, int total);
};

GType		rhythmdb_import_job_get_type		(void);

RhythmDBImportJob *rhythmdb_import_job_new		(RhythmDB *db,
							 RhythmDBEntryType *entry_type,
							 RhythmDBEntryType *ignore_type,
							 RhythmDBEntryType *error_type);
void		rhythmdb_import_job_add_uri		(RhythmDBImportJob *job, const char *uri);
gboolean	rhythmdb_import_job_includes_uri	(RhythmDBImportJob *job, const char *uri);
void		rhythmdb_import_job_start		(RhythmDBImportJob *job);
void		rhythmdb_import_job_cancel		(RhythmDBImportJob *job);

gboolean	rhythmdb_import_job_complete		(RhythmDBImportJob *job);
gboolean	rhythmdb_import_job_scan_complete	(RhythmDBImportJob *job);
int		rhythmdb_import_job_get_total		(RhythmDBImportJob *job);
int		rhythmdb_import_job_get_imported	(RhythmDBImportJob *job);
int		rhythmdb_import_job_get_processed	(RhythmDBImportJob *job);

G_END_DECLS

#endif /* RHYTHMDB_IMPORT_JOB_H */
