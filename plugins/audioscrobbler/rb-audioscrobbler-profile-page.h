/*
 * rb-audioscrobbler-profile-page.h
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

#ifndef __RB_AUDIOSCROBBLER_PROFILE_PAGE_H
#define __RB_AUDIOSCROBBLER_PROFILE_PAGE_H

#include <sources/rb-display-page.h>
#include <sources/rb-source.h>
#include <shell/rb-shell.h>

#include "rb-audioscrobbler-service.h"

G_BEGIN_DECLS

#define RB_TYPE_AUDIOSCROBBLER_PROFILE_PAGE         (rb_audioscrobbler_profile_page_get_type ())
#define RB_AUDIOSCROBBLER_PROFILE_PAGE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOSCROBBLER_PROFILE_PAGE, RBAudioscrobblerProfilePage))
#define RB_AUDIOSCROBBLER_PROFILE_PAGE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_AUDIOSCROBBLER_PROFILE_PAGE, RBAudioscrobblerProfilePageClass))
#define RB_IS_AUDIOSCROBBLER_PROFILE_PAGE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOSCROBBLER_PROFILE_PAGE))
#define RB_IS_AUDIOSCROBBLER_PROFILE_PAGE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOSCROBBLER_PROFILE_PAGE))
#define RB_AUDIOSCROBBLER_PROFILE_PAGE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOSCROBBLER_PROFILE_PAGE, RBAudioscrobblerProfilePageClass))

typedef struct _RBAudioscrobblerProfilePagePrivate RBAudioscrobblerProfilePagePrivate;

typedef struct
{
	RBDisplayPage parent;

	RBAudioscrobblerProfilePagePrivate *priv;
} RBAudioscrobblerProfilePage;

typedef struct
{
	RBDisplayPageClass parent_class;
} RBAudioscrobblerProfilePageClass;

GType 		rb_audioscrobbler_profile_page_get_type (void);
void 		_rb_audioscrobbler_profile_page_register_type (GTypeModule *module);
RBDisplayPage  *rb_audioscrobbler_profile_page_new (RBShell *shell,
						    GObject *plugin,
						    RBAudioscrobblerService *service);
void 		rb_audioscrobbler_profile_page_remove_radio_station (RBAudioscrobblerProfilePage *page,
								     RBSource *station);

G_END_DECLS

#endif /* __RB_AUDIOSCROBBLER_PROFILE_PAGE_H */
