/*
 * rb-audioscrobbler-service.h
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

#ifndef __RB_AUDIOSCROBBLER_SERVICE_H
#define __RB_AUDIOSCROBBLER_SERVICE_H

#include <glib-object.h>

/* not sure these belong here */
#define AUDIOSCROBBLER_SETTINGS_SCHEMA "org.gnome.rhythmbox.plugins.audioscrobbler.service"
#define AUDIOSCROBBLER_SETTINGS_PATH "/org/gnome/rhythmbox/plugins/audioscrobbler"
#define AUDIOSCROBBLER_SERVICE_ENABLED_KEY "enabled"
#define AUDIOSCROBBLER_SCROBBLING_ENABLED_KEY "scrobbling-enabled"


G_BEGIN_DECLS

#define RB_TYPE_AUDIOSCROBBLER_SERVICE         (rb_audioscrobbler_service_get_type ())
#define RB_AUDIOSCROBBLER_SERVICE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOSCROBBLER_SERVICE, RBAudioscrobblerService))
#define RB_AUDIOSCROBBLER_SERVICE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_AUDIOSCROBBLER_SERVICE, RBAudioscrobblerServiceClass))
#define RB_IS_AUDIOSCROBBLER_SERVICE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOSCROBBLER_SERVICE))
#define RB_IS_AUDIOSCROBBLER_SERVICE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOSCROBBLER_SERVICE))
#define RB_AUDIOSCROBBLER_SERVICE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOSCROBBLER_SERVICE, RBAudioscrobblerServiceClass))

typedef struct _RBAudioscrobblerServicePrivate RBAudioscrobblerServicePrivate;

typedef struct {
	GObject parent;

	RBAudioscrobblerServicePrivate *priv;
} RBAudioscrobblerService;

typedef struct {
	GObjectClass parent_class;
} RBAudioscrobblerServiceClass;

GType rb_audioscrobbler_service_get_type (void);
void _rb_audioscrobbler_service_register_type (GTypeModule *module);

RBAudioscrobblerService *rb_audioscrobbler_service_new_lastfm (void);
RBAudioscrobblerService *rb_audioscrobbler_service_new_librefm (void);

const char *rb_audioscrobbler_service_get_name (RBAudioscrobblerService *service);
const char *rb_audioscrobbler_service_get_auth_url (RBAudioscrobblerService *service);
const char *rb_audioscrobbler_service_get_scrobbler_url (RBAudioscrobblerService *service);
const char *rb_audioscrobbler_service_get_api_url (RBAudioscrobblerService *service);
const char *rb_audioscrobbler_service_get_old_radio_api_url (RBAudioscrobblerService *service);
const char *rb_audioscrobbler_service_get_api_key (RBAudioscrobblerService *service);
const char *rb_audioscrobbler_service_get_api_secret (RBAudioscrobblerService *service);

G_END_DECLS

#endif /* __RB_AUDIOSCROBBLER_SERVICE_H */
