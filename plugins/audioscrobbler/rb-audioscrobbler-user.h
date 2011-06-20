/*
 * rb-audioscrobbler-profile-user.h
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

#ifndef __RB_AUDIOSCROBBLER_USER_H
#define __RB_AUDIOSCROBBLER_USER_H

#include <glib-object.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "rb-audioscrobbler-service.h"

G_BEGIN_DECLS

#define RB_TYPE_AUDIOSCROBBLER_USER_DATA	(rb_audioscrobbler_user_data_get_type ())
#define RB_AUDIOSCROBBLER_USER_DATA(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOSCROBBLER_USER_DATA, RBAudioscrobblerUserData))
#define RB_IS_AUDIOSCROBBLER_USER_DATA(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOSCROBBLER_USER_DATA))
typedef struct {
	unsigned int refcount;

	enum {
		RB_AUDIOSCROBBLER_USER_DATA_TYPE_USER_INFO,
		RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK,
		RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST
	} type;

	GdkPixbuf *image;
	char *url;

	union {
		struct {
			char *username;
			char *playcount;
		} user_info;

		struct {
			char *title;
			char *artist;
		} track;

		struct {
			char *name;
		} artist;
	};
} RBAudioscrobblerUserData;
GType rb_audioscrobbler_user_data_get_type (void);

#define RB_TYPE_AUDIOSCROBBLER_USER         (rb_audioscrobbler_user_get_type ())
#define RB_AUDIOSCROBBLER_USER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOSCROBBLER_USER, RBAudioscrobblerUser))
#define RB_AUDIOSCROBBLER_USER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_AUDIOSCROBBLER_USER, RBAudioscrobblerUserClass))
#define RB_IS_AUDIOSCROBBLER_USER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOSCROBBLER_USER))
#define RB_IS_AUDIOSCROBBLER_USER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOSCROBBLER_USER))
#define RB_AUDIOSCROBBLER_USER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOSCROBBLER_USER, RBAudioscrobblerUserClass))

typedef struct _RBAudioscrobblerUserPrivate RBAudioscrobblerUserPrivate;

typedef struct
{
	GObject parent;

	RBAudioscrobblerUserPrivate *priv;
} RBAudioscrobblerUser;

typedef struct
{
	GObjectClass parent_class;
} RBAudioscrobblerUserClass;

GType rb_audioscrobbler_user_get_type (void);
void _rb_audioscrobbler_user_register_type (GTypeModule *module);

RBAudioscrobblerUser *rb_audioscrobbler_user_new (RBAudioscrobblerService *service);

void rb_audioscrobbler_user_set_authentication_details (RBAudioscrobblerUser *user,
                                                        const char *username,
                                                        const char *session_key);

void rb_audioscrobbler_user_update (RBAudioscrobblerUser *user);
void rb_audioscrobbler_user_force_update (RBAudioscrobblerUser *user);

void rb_audioscrobbler_user_love_track (RBAudioscrobblerUser *user,
                                        const char *title,
                                        const char *artist);
void rb_audioscrobbler_user_ban_track (RBAudioscrobblerUser *user,
                                       const char *title,
                                       const char *artist);

G_END_DECLS

#endif /* __RB_AUDIOSCROBBLER_USER_H */
