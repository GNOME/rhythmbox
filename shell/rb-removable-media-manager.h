/*
 *  arch-tag: Header for Rhythmbox removable media management object
 *
 *  Copyright (C) 2005 James Livingston <jrl@ids.org.au>
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

#ifndef __RB_REMOVABLE_MEDIA_MANAGER_H
#define __RB_REMOVABLE_MEDIA_MANAGER_H

#include <libgnomevfs/gnome-vfs.h>

#include "rb-source.h"
#include "rhythmdb.h"
#include "rb-shell.h"

G_BEGIN_DECLS

#define RB_TYPE_REMOVABLE_MEDIA_MANAGER         (rb_removable_media_manager_get_type ())
#define RB_REMOVABLE_MEDIA_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_REMOVABLE_MEDIA_MANAGER, RBRemovableMediaManager))
#define RB_REMOVABLE_MEDIA_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_REMOVABLE_MEDIA_MANAGER, RBRemovableMediaManagerClass))
#define RB_IS_REMOVABLE_MEDIA_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_REMOVABLE_MEDIA_MANAGER))
#define RB_IS_REMOVABLE_MEDIA_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_REMOVABLE_MEDIA_MANAGER))
#define RB_REMOVABLE_MEDIA_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_REMOVABLE_MEDIA_MANAGER, RBRemovableMediaManagerClass))

typedef void (*RBTranferCompleteCallback) (RhythmDBEntry *entry,
					   const char *dest,
					   gpointer userdata);

typedef struct
{
	GObject parent;
} RBRemovableMediaManager;

typedef struct
{
	GObjectClass parent_class;

	/* signals */
	void	(*medium_added)		(RBRemovableMediaManager *mgr,
					 RBSource *source);
	void	(*transfer_progress)	(RBRemovableMediaManager *mgr,
					 gint done,
					 gint total,
					 double fraction);
	RBSource * (*create_source)	(RBRemovableMediaManager *mgr,
					 GnomeVFSVolume *volume);
} RBRemovableMediaManagerClass;

RBRemovableMediaManager* rb_removable_media_manager_new		(RBShell *shell);
GType			rb_removable_media_manager_get_type	(void);

void			rb_removable_media_manager_scan (RBRemovableMediaManager *manager);

#ifdef ENABLE_TRACK_TRANSFER
void	rb_removable_media_manager_queue_transfer (RBRemovableMediaManager *mgr,
						   RhythmDBEntry *entry,
						   const char *dest,
						   GList *mime_types,
						   RBTranferCompleteCallback callback,
						   gpointer userdata);
#endif

G_END_DECLS

#endif /* __RB_REMOVABLE_MEDIA_MANAGER_H */
