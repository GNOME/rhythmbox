/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2005 Charles Schmidt <cschmidt2@emich.edu>
 *  Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#ifdef HAVE_AVAHI_0_6
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#endif
#include <avahi-client/client.h>
#include <avahi-common/error.h>
#include <avahi-glib/glib-malloc.h>
#include <avahi-glib/glib-watch.h>

#include "rb-daap-mdns-browser.h"
#include "rb-marshal.h"
#include "rb-debug.h"

#ifdef HAVE_AVAHI_0_5
#define AVAHI_ADDRESS_STR_MAX	(40)	/* IPv6 Max = 4*8 + 7 + 1 for NUL */
#endif

static void	rb_daap_mdns_browser_class_init (RBDaapMdnsBrowserClass *klass);
static void	rb_daap_mdns_browser_init	(RBDaapMdnsBrowser	*browser);
static void	rb_daap_mdns_browser_finalize   (GObject	        *object);

#define RB_DAAP_MDNS_BROWSER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_DAAP_MDNS_BROWSER, RBDaapMdnsBrowserPrivate))

struct RBDaapMdnsBrowserPrivate
{
	AvahiClient         *client;
	AvahiGLibPoll       *poll;
	AvahiServiceBrowser *service_browser;

	GSList              *resolvers;
};

enum {
	SERVICE_ADDED,
	SERVICE_REMOVED,
	LAST_SIGNAL
};

enum {
	PROP_0
};

static GObjectClass *parent_class = NULL;
static guint	     signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (RBDaapMdnsBrowser, rb_daap_mdns_browser, G_TYPE_OBJECT)

static gpointer browser_object = NULL;

GQuark
rb_daap_mdns_browser_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_daap_mdns_browser_error");

	return quark;
}

static void
client_cb (AvahiClient         *client,
	   AvahiClientState     state,
	   RBDaapMdnsBrowser   *browser)
{
	/* Called whenever the client or server state changes */

	switch (state) {
#ifdef HAVE_AVAHI_0_6
	case AVAHI_CLIENT_FAILURE:

		 g_warning ("Client failure: %s\n", avahi_strerror (avahi_client_errno (client)));
		 break;
#endif
	default:
		break;
	}
}

static void
avahi_client_init (RBDaapMdnsBrowser *browser)
{
	int error = 0;

	avahi_set_allocator (avahi_glib_allocator ());

	browser->priv->poll = avahi_glib_poll_new (NULL, G_PRIORITY_DEFAULT);

	if (! browser->priv->poll) {
		rb_debug ("Unable to create AvahiGlibPoll object for mDNS");
	}

#ifdef HAVE_AVAHI_0_5
	browser->priv->client = avahi_client_new (avahi_glib_poll_get (browser->priv->poll),
						  (AvahiClientCallback) client_cb,
						  browser,
						  &error);
#endif
#ifdef HAVE_AVAHI_0_6
	{
		AvahiClientFlags flags;
		flags = 0;

		browser->priv->client = avahi_client_new (avahi_glib_poll_get (browser->priv->poll),
							  flags,
							  (AvahiClientCallback)client_cb,
							  browser,
							  &error);
	}
#endif
}

static void
resolve_cb (AvahiServiceResolver  *service_resolver,
	    AvahiIfIndex           interface,
	    AvahiProtocol          protocol,
	    AvahiResolverEvent     event,
	    const char            *service_name,
	    const char            *type,
	    const char            *domain,
	    const char            *host_name,
	    const AvahiAddress    *address,
	    uint16_t               port,
	    AvahiStringList       *text,
#ifdef HAVE_AVAHI_0_6
	    AvahiLookupResultFlags flags,
#endif
	    RBDaapMdnsBrowser     *browser)
{
	if (event == AVAHI_RESOLVER_FOUND) {
		char    *name = NULL;
		char     host [AVAHI_ADDRESS_STR_MAX];
		gboolean pp = FALSE;

		if (text) {
			AvahiStringList *l;

			for (l = text; l != NULL; l = l->next) {
				size_t size;
				char  *key;
				char  *value;
				int    ret;

				ret = avahi_string_list_get_pair (l, &key, &value, &size);
				if (ret != 0 || key == NULL) {
					continue;
				}

				if (strcmp (key, "Password") == 0) {
					if (size >= 4 && strncmp (value, "true", 4) == 0) {
						pp = TRUE;
					}
				} else if (strcmp (key, "Machine Name") == 0) {
					name = g_strdup (value);
				}

				g_free (key);
				g_free (value);
			}
		}

		if (name == NULL) {
			name = g_strdup (service_name);
		}

		avahi_address_snprint (host, AVAHI_ADDRESS_STR_MAX, address);

		g_signal_emit (browser,
			       signals [SERVICE_ADDED],
			       0,
			       service_name,
			       name,
			       host,
			       port,
			       pp);

		g_free (name);
	}

	browser->priv->resolvers = g_slist_remove (browser->priv->resolvers, service_resolver);
	avahi_service_resolver_free (service_resolver);
}

static gboolean
rb_daap_mdns_browser_resolve (RBDaapMdnsBrowser *browser,
			      const char        *name)
{
	AvahiServiceResolver *service_resolver;

	service_resolver = avahi_service_resolver_new (browser->priv->client,
						       AVAHI_IF_UNSPEC,
						       AVAHI_PROTO_INET,
						       name,
						       "_daap._tcp",
						       NULL,
						       AVAHI_PROTO_UNSPEC,
#ifdef HAVE_AVAHI_0_6
						       0,
#endif
						       (AvahiServiceResolverCallback)resolve_cb,
						       browser);
	if (service_resolver == NULL) {
		rb_debug ("Error starting mDNS resolving using AvahiServiceResolver");
		return FALSE;
	}

	browser->priv->resolvers = g_slist_prepend (browser->priv->resolvers, service_resolver);

	return TRUE;
}

static void
browser_add_service (RBDaapMdnsBrowser *browser,
		     const char        *service_name)
{
	rb_daap_mdns_browser_resolve (browser, service_name);
}

static void
browser_remove_service (RBDaapMdnsBrowser *browser,
			const char        *service_name)
{
	g_signal_emit (browser, signals [SERVICE_REMOVED], 0, service_name);
}

static void
browse_cb (AvahiServiceBrowser   *service_browser,
	   AvahiIfIndex           interface,
	   AvahiProtocol          protocol,
	   AvahiBrowserEvent      event,
	   const char            *name,
	   const char            *type,
	   const char            *domain,
#ifdef HAVE_AVAHI_0_6
	   AvahiLookupResultFlags flags,
#endif
	   RBDaapMdnsBrowser     *browser)
{
	gboolean local;

#ifdef HAVE_AVAHI_0_5
	local = avahi_client_is_service_local (browser->priv->client, interface, protocol, name, type, domain);
#endif
#ifdef HAVE_AVAHI_0_6
	local = ((flags & AVAHI_LOOKUP_RESULT_LOCAL) != 0);
#endif
	if (local) {
		rb_debug ("Ignoring local service %s", name);
		return;
	}

	if (event == AVAHI_BROWSER_NEW) {
		browser_add_service (browser, name);
	} else if (event == AVAHI_BROWSER_REMOVE) {
		browser_remove_service (browser, name);
	}
}

gboolean
rb_daap_mdns_browser_start (RBDaapMdnsBrowser *browser,
			    GError           **error)
{
	if (browser->priv->client == NULL) {
		g_set_error (error,
			     RB_DAAP_MDNS_BROWSER_ERROR,
			     RB_DAAP_MDNS_BROWSER_ERROR_NOT_RUNNING,
			     "%s",
			     _("MDNS service is not running"));
		return FALSE;
	}
	if (browser->priv->service_browser != NULL) {
		g_set_error (error,
			     RB_DAAP_MDNS_BROWSER_ERROR,
			     RB_DAAP_MDNS_BROWSER_ERROR_FAILED,
			     "%s",
			     _("Browser already active"));
		return FALSE;
	}

	browser->priv->service_browser = avahi_service_browser_new (browser->priv->client,
								    AVAHI_IF_UNSPEC,
								    AVAHI_PROTO_UNSPEC,
								    "_daap._tcp",
								    NULL,
#ifdef HAVE_AVAHI_0_6
								    0,
#endif
								    (AvahiServiceBrowserCallback)browse_cb,
								    browser);
	if (browser->priv->service_browser == NULL) {
		rb_debug ("Error starting mDNS discovery using AvahiServiceBrowser");
		g_set_error (error,
			     RB_DAAP_MDNS_BROWSER_ERROR,
			     RB_DAAP_MDNS_BROWSER_ERROR_FAILED,
			     "%s",
			     _("Unable to activate browser"));

		return FALSE;
	}

	return TRUE;
}

gboolean
rb_daap_mdns_browser_stop (RBDaapMdnsBrowser *browser,
			   GError           **error)
{
	if (browser->priv->client == NULL) {
		g_set_error (error,
			     RB_DAAP_MDNS_BROWSER_ERROR,
			     RB_DAAP_MDNS_BROWSER_ERROR_NOT_RUNNING,
			     "%s",
			     _("MDNS service is not running"));
		return FALSE;
	}
	if (browser->priv->service_browser == NULL) {
		g_set_error (error,
			     RB_DAAP_MDNS_BROWSER_ERROR,
			     RB_DAAP_MDNS_BROWSER_ERROR_FAILED,
			     "%s",
			     _("Browser is not active"));
		return FALSE;

	}
	avahi_service_browser_free (browser->priv->service_browser);
	browser->priv->service_browser = NULL;

	return TRUE;
}

static void
rb_daap_mdns_browser_set_property (GObject      *object,
				   guint	 prop_id,
				   const GValue *value,
				   GParamSpec   *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_daap_mdns_browser_get_property (GObject	 *object,
				   guint	  prop_id,
				   GValue	 *value,
				   GParamSpec	 *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_daap_mdns_browser_class_init (RBDaapMdnsBrowserClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize	   = rb_daap_mdns_browser_finalize;
	object_class->get_property = rb_daap_mdns_browser_get_property;
	object_class->set_property = rb_daap_mdns_browser_set_property;

	signals [SERVICE_ADDED] =
		g_signal_new ("service-added",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBDaapMdnsBrowserClass, service_added),
			      NULL,
			      NULL,
			      rb_marshal_VOID__STRING_STRING_STRING_UINT_BOOLEAN,
			      G_TYPE_NONE,
			      5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_BOOLEAN);
	signals [SERVICE_REMOVED] =
		g_signal_new ("service-removed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBDaapMdnsBrowserClass, service_removed),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (RBDaapMdnsBrowserPrivate));
}

static void
rb_daap_mdns_browser_init (RBDaapMdnsBrowser *browser)
{
	browser->priv = RB_DAAP_MDNS_BROWSER_GET_PRIVATE (browser);

	avahi_client_init (browser);
}

static void
rb_daap_mdns_browser_finalize (GObject *object)
{
	RBDaapMdnsBrowser *browser;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_DAAP_MDNS_BROWSER (object));

	browser = RB_DAAP_MDNS_BROWSER (object);

	g_return_if_fail (browser->priv != NULL);

	if (browser->priv->resolvers) {
		g_slist_foreach (browser->priv->resolvers,
				 (GFunc)avahi_service_resolver_free,
				 NULL);
		g_slist_free (browser->priv->resolvers);
	}

	if (browser->priv->service_browser) {
		avahi_service_browser_free (browser->priv->service_browser);
	}

	if (browser->priv->client) {
		avahi_client_free (browser->priv->client);
	}

	if (browser->priv->poll) {
		avahi_glib_poll_free (browser->priv->poll);
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBDaapMdnsBrowser *
rb_daap_mdns_browser_new (void)
{
	if (browser_object) {
		g_object_ref (browser_object);
	} else {
		browser_object = g_object_new (RB_TYPE_DAAP_MDNS_BROWSER, NULL);
		g_object_add_weak_pointer (browser_object,
					   (gpointer *) &browser_object);
	}

	return RB_DAAP_MDNS_BROWSER (browser_object);
}
