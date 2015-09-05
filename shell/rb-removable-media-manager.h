/*
 *  Copyright (C) 2005 James Livingston <doclivingston@gmail.com>
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

#ifndef __RB_REMOVABLE_MEDIA_MANAGER_H
#define __RB_REMOVABLE_MEDIA_MANAGER_H

#include <gio/gio.h>

#include <sources/rb-source.h>
#include <shell/rb-shell.h>

#include <lib/libmediaplayerid/mediaplayerid.h>

G_BEGIN_DECLS

#define RB_TYPE_REMOVABLE_MEDIA_MANAGER         (rb_removable_media_manager_get_type ())
#define RB_REMOVABLE_MEDIA_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_REMOVABLE_MEDIA_MANAGER, RBRemovableMediaManager))
#define RB_REMOVABLE_MEDIA_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_REMOVABLE_MEDIA_MANAGER, RBRemovableMediaManagerClass))
#define RB_IS_REMOVABLE_MEDIA_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_REMOVABLE_MEDIA_MANAGER))
#define RB_IS_REMOVABLE_MEDIA_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_REMOVABLE_MEDIA_MANAGER))
#define RB_REMOVABLE_MEDIA_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_REMOVABLE_MEDIA_MANAGER, RBRemovableMediaManagerClass))

typedef struct _RBRemovableMediaManager RBRemovableMediaManager;
typedef struct _RBRemovableMediaManagerClass RBRemovableMediaManagerClass;

struct _RBRemovableMediaManager
{
	GObject parent;
};

struct _RBRemovableMediaManagerClass
{
	GObjectClass parent_class;

	/* signals */
	void	(*medium_added)		(RBRemovableMediaManager *mgr,
					 RBSource *source);
	RBSource * (*create_source_device) (RBRemovableMediaManager *mgr,
					 GObject *device);		/* actually a GUdevDevice */
	RBSource * (*create_source_mount) (RBRemovableMediaManager *mgr,
					 GMount *mount,
					 MPIDDevice *device_info);
	RBSource * (*create_source_volume) (RBRemovableMediaManager *mgr,
					 GVolume *volume);
};

RBRemovableMediaManager* rb_removable_media_manager_new		(RBShell *shell);
GType			rb_removable_media_manager_get_type	(void);

void			rb_removable_media_manager_scan (RBRemovableMediaManager *manager);

GObject *		rb_removable_media_manager_get_gudev_device (RBRemovableMediaManager *manager, GVolume *volume);
gboolean		rb_removable_media_manager_device_is_android (RBRemovableMediaManager *manager, GObject *device);

G_END_DECLS

#endif /* __RB_REMOVABLE_MEDIA_MANAGER_H */
