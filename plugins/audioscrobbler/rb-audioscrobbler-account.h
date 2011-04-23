/*
 * rb-audioscrobbler-account.h
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

#ifndef __RB_AUDIOSCROBBLER_ACCOUNT_H
#define __RB_AUDIOSCROBBLER_ACCOUNT_H

#include <glib-object.h>

#include "rb-audioscrobbler-service.h"

G_BEGIN_DECLS

typedef enum
{
	RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT,
	RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGING_IN,
	RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_IN,
	RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_AUTH_ERROR,
	RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_CONNECTION_ERROR
} RBAudioscrobblerAccountLoginStatus;

GType rb_audioscrobbler_account_login_status_get_type (void);
#define RB_TYPE_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS (rb_audioscrobbler_account_login_status_get_type ())

#define RB_TYPE_AUDIOSCROBBLER_ACCOUNT         (rb_audioscrobbler_account_get_type ())
#define RB_AUDIOSCROBBLER_ACCOUNT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOSCROBBLER_ACCOUNT, RBAudioscrobblerAccount))
#define RB_AUDIOSCROBBLER_ACCOUNT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), RB_TYPE_AUDIOSCROBBLER_ACCOUNT, RBAudioscrobblerAccountClass))
#define RB_IS_AUDIOSCROBBLER_ACCOUNT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOSCROBBLER_ACCOUNT))
#define RB_IS_AUDIOSCROBBLER_ACCOUNT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOSCROBBLER_ACCOUNT))
#define RB_AUDIOSCROBBLER_ACCOUNT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOSCROBBLER_ACCOUNT, RBAudioscrobblerAccountClass))

typedef struct _RBAudioscrobblerAccountPrivate RBAudioscrobblerAccountPrivate;

typedef struct
{
	GObject parent;

	RBAudioscrobblerAccountPrivate *priv;
} RBAudioscrobblerAccount;

typedef struct
{
	GObjectClass parent_class;

	/* signals */
	void (*login_status_changed) (RBAudioscrobblerAccount *account, RBAudioscrobblerAccountLoginStatus status);
} RBAudioscrobblerAccountClass;

GType                           rb_audioscrobbler_account_get_type (void);
void				_rb_audioscrobbler_account_register_type (GTypeModule *module);

RBAudioscrobblerAccount *       rb_audioscrobbler_account_new (RBAudioscrobblerService *service);

const char *                       rb_audioscrobbler_account_get_username (RBAudioscrobblerAccount *account);
const char *                       rb_audioscrobbler_account_get_session_key (RBAudioscrobblerAccount *account);
RBAudioscrobblerAccountLoginStatus rb_audioscrobbler_account_get_login_status (RBAudioscrobblerAccount *account);

void                            rb_audioscrobbler_account_authenticate (RBAudioscrobblerAccount *account);
void                            rb_audioscrobbler_account_logout (RBAudioscrobblerAccount *account);
void                            rb_audioscrobbler_account_notify_of_auth_error (RBAudioscrobblerAccount *account);

G_END_DECLS

#endif /* __RB_AUDIOSCROBBLER_ACCOUNT_H */
