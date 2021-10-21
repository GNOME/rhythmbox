/*
 *  Copyright (C) 2011 Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef RB_TRANSFER_TARGET_H
#define RB_TRANSFER_TARGET_H

#include <glib-object.h>

#include <rhythmdb/rhythmdb.h>
#include <shell/rb-track-transfer-batch.h>

#define RB_TYPE_TRANSFER_TARGET         (rb_transfer_target_get_type ())
#define RB_TRANSFER_TARGET(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_TRANSFER_TARGET, RBTransferTarget))
#define RB_IS_TRANSFER_TARGET(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_TRANSFER_TARGET))
#define RB_TRANSFER_TARGET_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), RB_TYPE_TRANSFER_TARGET, RBTransferTargetInterface))

typedef struct _RBTransferTarget RBTransferTarget;
typedef struct _RBTransferTargetInterface RBTransferTargetInterface;

struct _RBTransferTargetInterface
{
	GTypeInterface g_iface;

	char*		(*build_dest_uri)	(RBTransferTarget *target,
						 RhythmDBEntry *entry,
						 const char *media_type,
						 const char *extension);
	void		(*track_prepare)	(RBTransferTarget *target,
						 RhythmDBEntry *entry,
						 const char *uri,
						 GError **error);
	void		(*track_upload)		(RBTransferTarget *target,
						 RhythmDBEntry *entry,
						 const char *uri,
						 guint64 dest_size,
						 const char *media_type,
						 GError **error);
	gboolean	(*track_added)		(RBTransferTarget *target,
						 RhythmDBEntry *entry,
						 const char *uri,
						 guint64 dest_size,
						 const char *media_type);
	gboolean	(*track_add_error)	(RBTransferTarget *target,
						 RhythmDBEntry *entry,
						 const char *uri,
						 GError *error);
	gboolean	(*should_transfer)	(RBTransferTarget *target,
						 RhythmDBEntry *entry);
};

GType		rb_transfer_target_get_type	(void);

char*		rb_transfer_target_build_dest_uri 	(RBTransferTarget *target,
							 RhythmDBEntry *entry,
							 const char *media_type,
							 const char *extension);
void		rb_transfer_target_track_prepare	(RBTransferTarget *target,
							 RhythmDBEntry *entry,
							 const char *uri,
							 GError **error);
void		rb_transfer_target_track_upload		(RBTransferTarget *target,
							 RhythmDBEntry *entry,
							 const char *uri,
							 guint64 dest_size,
							 const char *media_type,
							 GError **error);
void		rb_transfer_target_track_added		(RBTransferTarget *target,
							 RhythmDBEntry *entry,
							 const char *uri,
							 guint64 filesize,
							 const char *media_type);
void		rb_transfer_target_track_add_error	(RBTransferTarget *target,
							 RhythmDBEntry *entry,
							 const char *uri,
							 GError *error);
gboolean	rb_transfer_target_should_transfer	(RBTransferTarget *target,
							 RhythmDBEntry *entry);

GList *		rb_transfer_target_get_format_descriptions (RBTransferTarget *target);
gboolean        rb_transfer_target_check_category	(RBTransferTarget *target,
							 RhythmDBEntry *entry);
gboolean        rb_transfer_target_check_duplicate	(RBTransferTarget *target,
							 RhythmDBEntry *entry);

RBTrackTransferBatch *rb_transfer_target_transfer	(RBTransferTarget *target, GSettings *settings, GList *entries, gboolean defer);


G_END_DECLS

#endif	/* RB_TRANSFER_TARGET_H */
