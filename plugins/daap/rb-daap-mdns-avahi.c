/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include <glib.h>
#include <avahi-glib/glib-malloc.h>
#include <avahi-glib/glib-watch.h>
#include <avahi-common/error.h>

#include "rb-daap-mdns-avahi.h"

static AvahiClient *client = NULL;
static AvahiEntryGroup *entry_group = NULL;
static gsize client_init = 0;

static void
client_cb (AvahiClient         *client,
	   AvahiClientState     state,
	   gpointer             data)
{
	/* FIXME
	 * check to make sure we're in the _RUNNING state before we publish
	 * check for COLLISION state and remove published information
	 */

	/* Called whenever the client or server state changes */

	switch (state) {
	case AVAHI_CLIENT_S_RUNNING:
		/* The server has startup successfully and registered its host
		 * name on the network, so it's time to create our services
		 */

		break;

	case AVAHI_CLIENT_S_COLLISION:

		 /* Let's drop our registered services. When the server is back
		  * in AVAHI_SERVER_RUNNING state we will register them
		  * again with the new host name.
		  */
		 if (entry_group) {
			 avahi_entry_group_reset (entry_group);
		 }
		 break;

	case AVAHI_CLIENT_FAILURE:
		 g_warning ("Client failure: %s\n", avahi_strerror (avahi_client_errno (client)));
		 break;

	case AVAHI_CLIENT_CONNECTING:
	case AVAHI_CLIENT_S_REGISTERING:
	default:
		break;
	}
}

AvahiClient *
rb_daap_mdns_avahi_get_client (void)
{
	if (g_once_init_enter (&client_init)) {
		AvahiClientFlags flags = 0;
		AvahiGLibPoll *apoll;
		int error = 0;

		avahi_set_allocator (avahi_glib_allocator ());

		apoll = avahi_glib_poll_new (NULL, G_PRIORITY_DEFAULT);
		if (apoll == NULL) {
			g_warning ("Unable to create AvahiGlibPoll object for mDNS");
		}

		client = avahi_client_new (avahi_glib_poll_get (apoll),
					   flags,
					   (AvahiClientCallback) client_cb,
					   NULL,
					   &error);
		if (error != 0) {
			g_warning ("Unable to initialize mDNS: %s", avahi_strerror (error));
		}

		g_once_init_leave (&client_init, 1);
	}

	return client;
}

void
rb_daap_mdns_avahi_set_entry_group (AvahiEntryGroup *eg)
{
	g_assert (eg == NULL || entry_group == NULL);
	g_assert (avahi_entry_group_get_client (eg) == client);
	entry_group = eg;
}
