/*
 * rb-audioscrobbler-profile-source.h
 *
 * Copyright (C) 2010 Jamie Nicol <jamie@thenicols.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#ifndef __RB_AUDIOSCROBBLER_PROFILE_SOURCE_H
#define __RB_AUDIOSCROBBLER_PROFILE_SOURCE_H

#include "rb-source.h"
#include "rb-shell.h"
#include "rb-plugin.h"
#include "rb-audioscrobbler-service.h"

G_BEGIN_DECLS

#define RB_TYPE_AUDIOSCROBBLER_PROFILE_SOURCE         (rb_audioscrobbler_profile_source_get_type ())
#define RB_AUDIOSCROBBLER_PROFILE_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOSCROBBLER_PROFILE_SOURCE, RBAudioscrobblerProfileSource))
#define RB_AUDIOSCROBBLER_PROFILE_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_AUDIOSCROBBLER_PROFILE_SOURCE, RBAudioscrobblerProfileSourceClass))
#define RB_IS_AUDIOSCROBBLER_PROFILE_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOSCROBBLER_PROFILE_SOURCE))
#define RB_IS_AUDIOSCROBBLER_PROFILE_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOSCROBBLER_PROFILE_SOURCE))
#define RB_AUDIOSCROBBLER_PROFILE_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOSCROBBLER_PROFILE_SOURCE, RBAudioscrobblerProfileSourceClass))

typedef struct _RBAudioscrobblerProfileSourcePrivate RBAudioscrobblerProfileSourcePrivate;

typedef struct
{
	RBSource parent;

	RBAudioscrobblerProfileSourcePrivate *priv;
} RBAudioscrobblerProfileSource;

typedef struct
{
	RBSourceClass parent_class;
} RBAudioscrobblerProfileSourceClass;

GType rb_audioscrobbler_profile_source_get_type (void);
RBSource *rb_audioscrobbler_profile_source_new (RBShell *shell,
                                                RBPlugin *plugin,
                                                RBAudioscrobblerService *service);
void rb_audioscrobbler_profile_source_remove_radio_station (RBAudioscrobblerProfileSource *source,
                                                            RBSource *station);

G_END_DECLS

#endif /* __RB_AUDIOSCROBBLER_PROFILE_SOURCE_H */
