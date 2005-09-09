/*
 *  Implentation of Multicast DNS for DAAP sharing
 *
 *  Copyright (C) 2005 Charles Schmidt <cschmidt2@emich.edu>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>

#include "rb-daap-mdns.h"

#include <libgnome/gnome-i18n.h>
#include "rb-dialog.h"

#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef WITH_DAAP_SUPPORT
static void mdns_error_dialog (const gchar *impl)
{
	rb_error_dialog (NULL, _("Unable to browse for remote shares"), _("Could not start browsing for music servers on your network.  Rhythmbox will continue to function, without this feature.  Check your %s installation"), impl);

	return;
}

static gboolean
is_local_address (const gchar *address)
{
	static struct hostent *he = NULL;
	struct in_addr addr;
	gint i;
	
	if (he == NULL) {
		gint ret;
		gchar hostname[255 + 7];
	
		ret = gethostname (hostname, 255);
		if (ret) {
			return FALSE;
		}

		strncat (hostname, ".local", 6);
		he = gethostbyname (hostname);

		if (he == NULL) {
			return FALSE;
		}
	}

	for (i = 0; he->h_addr_list[i]; i++) {
		memcpy (&addr, he->h_addr_list[0], sizeof (struct in_addr));

		if (strcmp (inet_ntoa (addr), address) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

typedef struct _CallbackAndData {
	gpointer callback;
	gpointer data;
} CallbackAndData;
#endif

#ifdef WITH_HOWL
/* stupid howl includes howl_config.h */
#undef PACKAGE
#undef VERSION
#include <howl.h>

static gboolean
howl_in_cb (GIOChannel *io_channel,
	    GIOCondition condition,
	    gpointer data)
{
	sw_discovery discovery = (sw_discovery) data;
	sw_salt salt;

	if (sw_discovery_salt (discovery, &salt) == SW_OKAY) {
		sw_salt_lock (salt);
		sw_discovery_read_socket (discovery);
		sw_salt_unlock (salt);
	}
	
	return TRUE;
}

static void
setup_sw_discovery (sw_discovery discovery)
{
	int fd;
	GIOChannel *channel;

	fd = sw_discovery_socket (discovery);

	channel = g_io_channel_unix_new (fd);
	g_io_add_watch (channel, G_IO_IN, howl_in_cb, discovery);
	g_io_channel_unref (channel);

	return;
}

static sw_discovery
get_sw_discovery ()
{
	sw_result result;
	static sw_discovery discovery = NULL;
	static gboolean initialized = FALSE;
	
	if (initialized == FALSE) {
		result = sw_discovery_init (&discovery);
	
		if (result != SW_OKAY) {
			g_warning ("Error starting mDNS discovery");
			mdns_error_dialog ("Howl");
			return NULL;
		}

		setup_sw_discovery (discovery);

		initialized = TRUE;
	}

	return discovery;
}
		
static sw_result
browse_cb (sw_discovery discovery,
	   sw_discovery_oid oid,
	   sw_discovery_browse_status status,
	   sw_uint32 interface_index,
	   sw_const_string name,
	   sw_const_string type,
	   sw_const_string domain,
	   sw_opaque extra)
{
	CallbackAndData *cd = (CallbackAndData *) extra;
	RBDAAPmDNSBrowserStatus bstatus;

	if (status == SW_DISCOVERY_BROWSE_ADD_SERVICE) {
		bstatus = RB_DAAP_MDNS_BROWSER_ADD_SERVICE;
	} else if (status == SW_DISCOVERY_BROWSE_REMOVE_SERVICE) {
		bstatus = RB_DAAP_MDNS_BROWSER_REMOVE_SERVICE;
	} else {
		return SW_OKAY;
	}
	
	((RBDAAPmDNSBrowserCallback)cd->callback) ((RBDAAPmDNSBrowser) oid,
						   bstatus,
						   (const gchar *) name,
						   cd->data);
	
	return SW_OKAY;
}

gboolean
rb_daap_mdns_browse (RBDAAPmDNSBrowser *browser,
		     RBDAAPmDNSBrowserCallback callback,
		     gpointer user_data)
{
	sw_discovery discovery;
	
	discovery = get_sw_discovery ();

	if (discovery) {
		static CallbackAndData cd;
		sw_result result;
		
		cd.callback = callback;
		cd.data = user_data;
		
	       	result = sw_discovery_browse (discovery,
					      0,
					      "_daap._tcp", 
					      "local", 
					      (sw_discovery_browse_reply) browse_cb, 
					      (sw_opaque) &cd, 
					      (sw_discovery_oid *)browser);

		if (result == SW_OKAY) {
			return TRUE;
		}
	}

	return FALSE;
}

void
rb_daap_mdns_browse_cancel (RBDAAPmDNSBrowser browser)
{
	sw_discovery discovery;
	
	discovery = get_sw_discovery ();

	if (discovery) {
		sw_discovery_cancel (discovery, (sw_discovery_oid) browser);
	}

	return;
}

static sw_result
resolve_cb (sw_discovery disc,
	    sw_discovery_oid oid, 
	    sw_uint32 interface_index, 
	    sw_const_string name, 
	    sw_const_string type, 
	    sw_const_string domain, 
	    sw_ipv4_address address, 
	    sw_port port, 
	    sw_octets text_record, 
	    sw_ulong text_record_length, 
	    sw_opaque extra)
{
	gchar *host = g_malloc (16);
	CallbackAndData *cd = (CallbackAndData *) extra;
	sw_text_record_iterator it;
	gboolean pp = FALSE;
	
	sw_ipv4_address_name (address, host, 16);

	if (is_local_address (host)) {
		g_free (host);
		return SW_OKAY;
	}
	
	if (sw_text_record_iterator_init (&it, text_record, text_record_length) == SW_OKAY) {
		sw_char key[SW_TEXT_RECORD_MAX_LEN];
		sw_octet val[SW_TEXT_RECORD_MAX_LEN];
		sw_ulong val_len;
		
		while (sw_text_record_iterator_next (it, (char *)key, val, &val_len) == SW_OKAY) {
			if (strcmp ((char *)key, "Password") == 0) {
				if (val_len >= 4 && strncmp ((char *)val, "true", 4) == 0) {
					pp = TRUE;
				}
			}
		}
		
		sw_text_record_iterator_fina (it);
	}
	
	((RBDAAPmDNSResolverCallback)cd->callback) ((RBDAAPmDNSResolver) oid,
						    RB_DAAP_MDNS_RESOLVER_FOUND,
						    name,
						    host,
						    (guint) port,
						    pp,
						    cd->data);

	return SW_OKAY;
}
	

gboolean
rb_daap_mdns_resolve (RBDAAPmDNSResolver *resolver,
		      const gchar *name,
		      RBDAAPmDNSResolverCallback callback,
		      gpointer user_data)
{
	sw_discovery discovery;
	
	discovery = get_sw_discovery ();

	if (discovery) {
		static CallbackAndData cd;
		sw_result result;
		
		cd.callback = callback;
		cd.data = user_data;
		
	       	result = sw_discovery_resolve (discovery,
					       0,
					       name, 
					       "_daap._tcp", 
					       "local",
					       (sw_discovery_resolve_reply) resolve_cb,
		       			       (sw_opaque) &cd,
					       (sw_discovery_oid *)resolver);
		
		if (result == SW_OKAY) {
			return TRUE;
		}
	}

	return FALSE;
}

void
rb_daap_mdns_resolve_cancel (RBDAAPmDNSResolver resolver)
{
	sw_discovery discovery;
	
	discovery = get_sw_discovery ();

	if (discovery) {
		sw_discovery_cancel (discovery, (sw_discovery_oid) resolver);
	}

	return;
}

static sw_result
publish_cb (sw_discovery discovery, 
	    sw_discovery_oid oid, 
	    sw_discovery_publish_status status, 
	    sw_opaque extra)
{
	CallbackAndData *cd = (CallbackAndData *) extra;
	RBDAAPmDNSPublisherStatus pstatus;

	if (status == SW_DISCOVERY_PUBLISH_STARTED) {
		pstatus = RB_DAAP_MDNS_PUBLISHER_STARTED;
	} else if (status == SW_DISCOVERY_PUBLISH_NAME_COLLISION) {
	/* This is all well and good, but howl won't report a name collision.
	 * http://lists.porchdogsoft.com/pipermail/howl-users/Week-of-Mon-20041206/000487.html
	 * So.  Fuck.
	 */
		pstatus = RB_DAAP_MDNS_PUBLISHER_COLLISION;
	} else {
		return SW_OKAY;
	}

	((RBDAAPmDNSPublisherCallback)cd->callback) ((RBDAAPmDNSPublisher) oid,
						     pstatus,
						     cd->data);

	return SW_OKAY;
}

gboolean
rb_daap_mdns_publish (RBDAAPmDNSPublisher *publisher,
		      const gchar *name,
		      guint port,
		      RBDAAPmDNSPublisherCallback callback,
		      gpointer user_data)
{
	sw_discovery discovery;
	
	discovery = get_sw_discovery ();

	if (discovery) {
		static CallbackAndData cd;
		sw_result result;
		
		cd.callback = callback;
		cd.data = user_data;
		
	       	result = sw_discovery_publish (discovery,
					       0,
					       name, 
					       "_daap._tcp", 
					       "local",
					       NULL,
					       port,
					       NULL,
					       0,
					       (sw_discovery_publish_reply) publish_cb,
		       			       (sw_opaque) &cd,
					       (sw_discovery_oid *)publisher);
		
		if (result == SW_OKAY) {
			return TRUE;
		}
	}

	return FALSE;
}

void
rb_daap_mdns_publish_cancel (RBDAAPmDNSPublisher publisher)
{
	sw_discovery discovery;
	
	discovery = get_sw_discovery ();

	if (discovery) {
		sw_discovery_cancel (discovery, (sw_discovery_oid) publisher);
	}

	return;
}
#endif

#ifdef WITH_AVAHI

#include <avahi-client/client.h>
#include <avahi-common/error.h>
#include <avahi-glib/glib-malloc.h>
#include <avahi-glib/glib-watch.h>

static void
client_cb (AvahiClient *client,
	   AvahiClientState state,
	   gpointer data)
{
/* FIXME
 * check to make sure we're in the _RUNNING state before we publish
 * check for COLLISION state and remove published information
 */
	return;
}
	
static AvahiClient * get_avahi_client ()
{
	static gboolean initialized = FALSE;
	static AvahiClient *client = NULL;
	
	if (initialized == FALSE) {
		AvahiGLibPoll *poll = NULL;
		
		avahi_set_allocator (avahi_glib_allocator ());

		poll = avahi_glib_poll_new (NULL, G_PRIORITY_DEFAULT);

		if (!poll) {
			return NULL;
		}

		client = avahi_client_new (avahi_glib_poll_get (poll), client_cb, NULL, NULL);
		initialized = TRUE;
	}

	return client;
}

static void
browse_cb (AvahiServiceBrowser *browser,
	   AvahiIfIndex interface,
	   AvahiProtocol protocol,
	   AvahiBrowserEvent event,
	   const char *name,
	   const char *type,
	   const char *domain,
	   void *data)
{
	CallbackAndData *cd = (CallbackAndData *) data;
	RBDAAPmDNSBrowserStatus bstatus;

	if (event == AVAHI_BROWSER_NEW) {
		bstatus = RB_DAAP_MDNS_BROWSER_ADD_SERVICE;
	} else if (event == AVAHI_BROWSER_REMOVE) {
		bstatus = RB_DAAP_MDNS_BROWSER_REMOVE_SERVICE;
	} else {
		return;
	}
	
	((RBDAAPmDNSBrowserCallback)cd->callback) ((RBDAAPmDNSBrowser) browser,
						   bstatus,
						   (const gchar *) name,
						   cd->data);
	
	return;
}

gboolean
rb_daap_mdns_browse (RBDAAPmDNSBrowser *browser,
		     RBDAAPmDNSBrowserCallback callback,
		     gpointer user_data)
{
	AvahiClient *client;
	static CallbackAndData cd;
	
	client = get_avahi_client ();
	if (!client) {
		g_warning ("Error starting mDNS discovery");
		mdns_error_dialog ("Avahi");
		return FALSE;
	}

	cd.callback = callback;
	cd.data = user_data;
	
	*browser = (gpointer) avahi_service_browser_new (client,
							 AVAHI_IF_UNSPEC,
							 AVAHI_PROTO_INET,
							 "_daap._tcp",
							 "local",
							 (AvahiServiceBrowserCallback)browse_cb,
							 &cd);
	if (!*browser) {
		g_warning ("Error starting mDNS discovery");
		mdns_error_dialog ("Avahi");
		return FALSE;
	}

	return TRUE;
}

void
rb_daap_mdns_browse_cancel (RBDAAPmDNSBrowser browser)
{
	AvahiServiceBrowser *sb = (AvahiServiceBrowser *) browser;

	avahi_service_browser_free (sb);
	browser = NULL;
	
	return;
}

static void
resolve_cb (AvahiServiceResolver *resolver,
	    AvahiIfIndex interface,
	    AvahiProtocol protocol,
	    AvahiResolverEvent event,
	    const char *name,
	    const char *type,
	    const char *domain,
	    const char *host_name,
	    const AvahiAddress *address,
	    uint16_t port,
	    AvahiStringList *text,
	    void *data)
{
	CallbackAndData *cd = (CallbackAndData *) data;

	if (event == AVAHI_RESOLVER_FOUND) {
		gchar *host = g_malloc (16);
		gboolean pp = FALSE;
		
		avahi_address_snprint (host, 16, address);

		if (is_local_address (host)) {
			g_free (host);
			return;
		}
		
		if (text) {
			AvahiStringList *l = avahi_string_list_find (text, "Password");

			if (l) {
				size_t s;
				char *n;
				char *v;

				avahi_string_list_get_pair (l, &n, &v, &s);

				if (s >= 4 && strncmp (v, "true", 4) == 0) {
					pp = TRUE;
				}

				avahi_free (n);
				avahi_free (v);
			}
		}

		((RBDAAPmDNSResolverCallback)cd->callback) ((RBDAAPmDNSResolver) resolver,
							    RB_DAAP_MDNS_RESOLVER_FOUND,
							    name,
							    host,
							    (guint16) port,
							    pp,
							    cd->data);
	} else if (event == RB_DAAP_MDNS_RESOLVER_TIMEOUT) {
		((RBDAAPmDNSResolverCallback)cd->callback) ((RBDAAPmDNSResolver) resolver,
							    RB_DAAP_MDNS_RESOLVER_TIMEOUT,
							    name,
							    NULL,
							    0,
							    FALSE,
							    cd->data);
	}

	return;
}
	
gboolean
rb_daap_mdns_resolve (RBDAAPmDNSResolver *resolver,
		      const gchar *name,
		      RBDAAPmDNSResolverCallback callback,
		      gpointer user_data)
{
	AvahiClient *client;
	static CallbackAndData cd;
	
	client = get_avahi_client ();
	if (!client) {
		g_warning ("Error starting mDNS discovery");
		mdns_error_dialog ("Avahi");
		return FALSE;
	}

	cd.callback = callback;
	cd.data = user_data;
	
	*resolver = (gpointer) avahi_service_resolver_new (client,
							   AVAHI_IF_UNSPEC,
							   AVAHI_PROTO_INET,
							   name,
							   "_daap._tcp",
							   "local",
							   AVAHI_PROTO_UNSPEC,
							   (AvahiServiceResolverCallback)resolve_cb,
							   &cd);
	if (!*resolver) {
		g_warning ("Error starting mDNS discovery");
		mdns_error_dialog ("Avahi");
		return FALSE;
	}

	return TRUE;
}

void
rb_daap_mdns_resolve_cancel (RBDAAPmDNSResolver resolver)
{
	AvahiServiceResolver *sr = (AvahiServiceResolver *) resolver;

	avahi_service_resolver_free (sr);
	resolver = NULL;
	
	return;
}

/* FIXME
 * the publisher callback should return a new char * if there was a collision
 * and we should reset/update/commit the entry group if that is the case
 */
static void
entry_group_cb (AvahiEntryGroup *group,
		AvahiEntryGroupState state,
		void *data)
{
	CallbackAndData *cd = (CallbackAndData *)data;
	RBDAAPmDNSPublisherStatus pstatus;

	if (state == AVAHI_ENTRY_GROUP_ESTABLISHED) {
		pstatus = RB_DAAP_MDNS_PUBLISHER_STARTED;
	} else if (state == AVAHI_ENTRY_GROUP_COLLISION) {
		pstatus = RB_DAAP_MDNS_PUBLISHER_COLLISION;
	} else {
		return;
	}

	((RBDAAPmDNSPublisherCallback)cd->callback) ((RBDAAPmDNSPublisher) group,
						     pstatus,
						     cd->data);

	return;
}

gboolean
rb_daap_mdns_publish (RBDAAPmDNSPublisher *publisher,
		      const gchar *name,
		      guint port,
		      RBDAAPmDNSPublisherCallback callback,
		      gpointer user_data)
{
	AvahiClient *client;
	static CallbackAndData cd;
	gint ret;
	
	client = get_avahi_client ();
	if (!client) {
		g_warning ("Error starting mDNS discovery");
		mdns_error_dialog ("Avahi");
		return FALSE;
	}

	cd.callback = callback;
	cd.data = user_data;
	
	*publisher = (gpointer) avahi_entry_group_new (client,
						       entry_group_cb,
						       &cd);
	if (!*publisher) {
		g_warning ("Error starting mDNS discovery");
		mdns_error_dialog ("Avahi");
		return FALSE;
	}
	
	ret = avahi_entry_group_add_service ((AvahiEntryGroup *)*publisher,
					     AVAHI_IF_UNSPEC,
					     AVAHI_PROTO_INET,
					     name,
					     "_daap._tcp",
					     NULL,
					     NULL,
					     port,
					     NULL);
	if (ret < 0) {
		g_warning ("Error starting mDNS discovery");
		mdns_error_dialog ("Avahi");
		return FALSE;
	}
	

	ret = avahi_entry_group_commit ((AvahiEntryGroup *)*publisher);

	if (ret < 0) {
		g_warning ("Error starting mDNS discovery");
		mdns_error_dialog ("Avahi");
		return FALSE;
	}
	
	return TRUE;
}

void
rb_daap_mdns_publish_cancel (RBDAAPmDNSPublisher publisher)
{
	AvahiEntryGroup *eg = (AvahiEntryGroup *)publisher;

	avahi_entry_group_free (eg);
	publisher = NULL;

	return;
}
#endif
