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
#include "rb-debug.h"
#include "eel-gconf-extensions.h"

#include <string.h>
#include <stdlib.h>


GQuark
rb_daap_mdns_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_daap_mdns_error");

	return quark;
}

#ifdef WITH_DAAP_SUPPORT
static void mdns_error_dialog (const gchar *impl)
{
	rb_error_dialog (NULL,
			 _("Unable to browse for remote shares"),
			 _("Could not start browsing for music servers on your network.  Rhythmbox will continue to function, without this feature.  Check your %s installation"),
			 impl);
}

#endif

#ifdef WITH_HOWL
/* stupid howl includes howl_config.h */
#undef PACKAGE
#undef VERSION
#include <howl.h>

typedef struct {
	char *service_name; /* stupid hack cause howl sucks */
	gpointer callback;
	gpointer data;
	gint port;
} CallbackAndData;


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
}

static sw_discovery
get_sw_discovery (GError **error)
{
	sw_result result;
	static sw_discovery discovery = NULL;
	static gboolean initialized = FALSE;
	
	if (initialized == FALSE) {
		result = sw_discovery_init (&discovery);
	
		if (result != SW_OKAY) {
			rb_debug ("Error initializing Howl");
			g_set_error (error,
				     RB_DAAP_MDNS_ERROR,
				     RB_DAAP_MDNS_ERROR_FAILED,
				     "Hown daemon was not running");
			return NULL;
		}

		setup_sw_discovery (discovery);

		initialized = TRUE;
	}

	return discovery;
}

static const gchar *
howl_strerror (sw_result result)
{
	switch (result) {
		case SW_OKAY:
			return "No error";
		case SW_DISCOVERY_E_NO_MEM:
			return "Out of memory";
		case SW_DISCOVERY_E_BAD_PARAM:
			return "Invalid paramater";
		case SW_DISCOVERY_E_UNKNOWN:
		default:
			return "Unknown error";
	}

	return "";
}
		
		
static sw_result
browse_cb (sw_discovery discovery,
	   sw_discovery_oid oid,
	   sw_discovery_browse_status status,
	   sw_uint32 interface_index,
	   sw_const_string name,
	   sw_const_string type,
	   sw_const_string domain,
	   CallbackAndData *cd)
{
	RBDAAPmDNSBrowserStatus bstatus;

	/* This sucks.  Howl sucks.  I can't wait til avahi gets used more
	 * places and I feel fine about dropping Howl.
	 *
	 * Howl automatically renames your service if there is a collision,
	 * and doesn't tell you.  So this won't work if there is a collision.
	 *
	 * Howl also does not provide the nifty API for detecting local
	 * services like Avahi does.
	 *
	 * Bollix
	 */
	if (cd->service_name && strcmp ((const gchar *)name, cd->service_name) == 0) {
		return SW_OKAY;
	}
	
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
	CallbackAndData *cd;
	sw_result result;
	GError *error = NULL;
	
	discovery = get_sw_discovery (&error);
	if (!discovery) {
		rb_debug ("Error initializing Howl for browsing");
		if (!g_error_matches (error, RB_DAAP_MDNS_ERROR, RB_DAAP_MDNS_ERROR_NOT_RUNNING))
			mdns_error_dialog ("Howl");
		return FALSE;
	}

	cd = g_new0 (CallbackAndData, 1);
	*browser = (RBDAAPmDNSBrowser) cd;
	cd->callback = callback;
	cd->data = user_data;
		
       	result = sw_discovery_browse (discovery,
				      0,
				      "_daap._tcp", 
				      "local", 
				      (sw_discovery_browse_reply) browse_cb, 
				      (sw_opaque) cd, 
				      (sw_discovery_oid*) cd);

	if (result != SW_OKAY) {
		g_free (cd);
		rb_debug ("Error starting mDNS browsing with Howl: %s", howl_strerror (result));
		mdns_error_dialog ("Howl");
		return FALSE;
	}

	return TRUE;
}

void
rb_daap_mdns_browse_cancel (RBDAAPmDNSBrowser browser)
{
	sw_discovery discovery;
	
	discovery = get_sw_discovery (NULL);

	if (discovery) {
		sw_discovery_cancel (discovery, (sw_discovery_oid) browser);
	}

	return;
}

static sw_result
resolve_cb (sw_discovery disc,
	    sw_discovery_oid oid, 
	    sw_uint32 interface_index, 
	    sw_const_string service_name, 
	    sw_const_string type, 
	    sw_const_string domain, 
	    sw_ipv4_address address, 
	    sw_port port, 
	    sw_octets text_record, 
	    sw_ulong text_record_length, 
	    CallbackAndData *cd)
{
	gchar *host = g_malloc (16);
	sw_text_record_iterator it;
	gboolean pp = FALSE;
	gchar *name = NULL;
	
	sw_ipv4_address_name (address, host, 16);

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
			if (strcmp ((char *)key, "Machine Name") == 0) {
				name = g_strdup ((char *)val);
			}
		}
		
		sw_text_record_iterator_fina (it);
	}
	
	((RBDAAPmDNSResolverCallback)cd->callback) ((RBDAAPmDNSResolver) oid,
						    RB_DAAP_MDNS_RESOLVER_FOUND,
						    service_name,
						    name,
						    host,
						    (guint) port,
						    pp,
						    cd->data);
	g_free (host);
	g_free (name);
	
	return SW_OKAY;
}
	

gboolean
rb_daap_mdns_resolve (RBDAAPmDNSResolver *resolver,
		      const gchar *name,
		      RBDAAPmDNSResolverCallback callback,
		      gpointer user_data)
{
	sw_discovery discovery;
	CallbackAndData *cd;
	sw_result result;
	GError *error = NULL;

	discovery = get_sw_discovery (&error);
	if (!discovery) {
		rb_debug ("Error initializing Howl for resolving");
		if (!g_error_matches (error, RB_DAAP_MDNS_ERROR, RB_DAAP_MDNS_ERROR_NOT_RUNNING))
			mdns_error_dialog ("Howl");
		return FALSE;
	}

	cd = g_new0 (CallbackAndData, 1);
	*resolver = (RBDAAPmDNSResolver)cd;
	cd->callback = callback;
	cd->data = user_data;
		
       	result = sw_discovery_resolve (discovery,
				       0,
				       name, 
				       "_daap._tcp", 
				       "local",
				       (sw_discovery_resolve_reply) resolve_cb,
	       			       (sw_opaque) cd,
				       (sw_discovery_oid* )cd);
		
	if (result != SW_OKAY) {
		g_free (cd);
		rb_debug ("Error starting mDNS resolving with Howl: %s", howl_strerror (result));
		mdns_error_dialog ("Howl");
		return FALSE;
	}

	return TRUE;
}

void
rb_daap_mdns_resolve_cancel (RBDAAPmDNSResolver resolver)
{
	sw_discovery discovery = get_sw_discovery (NULL);

	if (discovery) {
		sw_discovery_cancel (discovery, (sw_discovery_oid) resolver);
	}
}

static sw_result
publish_cb (sw_discovery discovery,
	    sw_discovery_oid oid,
	    sw_discovery_publish_status status,
	    CallbackAndData *cd)
{
	if (status == SW_DISCOVERY_PUBLISH_STARTED) {
		((RBDAAPmDNSPublisherCallback)cd->callback) ((RBDAAPmDNSPublisher) oid,
							     RB_DAAP_MDNS_PUBLISHER_STARTED,
							     cd->data);
	} else if (status == SW_DISCOVERY_PUBLISH_NAME_COLLISION) {
	/* This is all well and good, but howl won't report a name collision.
	 * http://lists.porchdogsoft.com/pipermail/howl-users/Week-of-Mon-20041206/000487.html
	 * So.  Fuck.
	 */
		((RBDAAPmDNSPublisherCallback)cd->callback) ((RBDAAPmDNSPublisher) oid,
							     RB_DAAP_MDNS_PUBLISHER_COLLISION,
							     cd->data);
	}
	
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
	CallbackAndData *cd;
	sw_result result;
	GError *error = NULL;

	discovery = get_sw_discovery (&error);
	if (!discovery) {
		rb_debug ("Error initializing Howl for resolving");
		if (!g_error_matches (error, RB_DAAP_MDNS_ERROR, RB_DAAP_MDNS_ERROR_NOT_RUNNING))
			mdns_error_dialog ("Howl");
		return FALSE;
	}

	cd = g_new0 (CallbackAndData, 1);
	*publisher = (RBDAAPmDNSPublisher)cd;
	cd->callback = callback;
	cd->port = port;
	cd->data = user_data;

	cd->service_name = g_strdup (name);

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
	       			       (sw_opaque) cd,
				       (sw_discovery_oid*) cd);
		
	if (result != SW_OKAY) {
		g_free (cd);
		rb_debug ("Error starting mDNS pubilsh with Howl: %s", howl_strerror (result));
		mdns_error_dialog ("Howl");
		return FALSE;
	}

	return TRUE;
}

void
rb_daap_mdns_publish_cancel (RBDAAPmDNSPublisher publisher)
{
	CallbackAndData *cd = (CallbackAndData*) publisher;
	sw_discovery discovery;
	
	discovery = get_sw_discovery (NULL);

	if (discovery) {
		sw_discovery_cancel (discovery, (sw_discovery_oid) publisher);
	}

	g_free (cd->service_name);
}
#endif

#ifdef WITH_AVAHI

#include <avahi-client/client.h>
#include <avahi-common/error.h>
#include <avahi-glib/glib-malloc.h>
#include <avahi-glib/glib-watch.h>

typedef struct {
	AvahiServiceBrowser *sb;
	RBDAAPmDNSBrowserCallback callback;
	gpointer data;
} RBDAAPmDNSBrowserData;


static void
client_cb (AvahiClient *client,
	   AvahiClientState state,
	   gpointer data)
{
/* FIXME
 * check to make sure we're in the _RUNNING state before we publish
 * check for COLLISION state and remove published information
 */
}

static AvahiClient *
get_avahi_client (GError **err)
{
	static gboolean initialized = FALSE;
	static AvahiClient *client = NULL;

	if (initialized == FALSE) {
		AvahiGLibPoll *poll = NULL;
		gint error = 0;

		avahi_set_allocator (avahi_glib_allocator ());

		poll = avahi_glib_poll_new (NULL, G_PRIORITY_DEFAULT);

		if (!poll) {
			rb_debug ("Unable to create AvahiGlibPoll object for mDNS");
			g_set_error (err,
				     RB_DAAP_MDNS_ERROR,
				     RB_DAAP_MDNS_ERROR_FAILED,
				     "Could not create AvahiGlibPoll object");
			return NULL;
		}

		client = avahi_client_new (avahi_glib_poll_get (poll), client_cb, NULL, &error);
		if (client == NULL) {
			rb_debug ("Unable to create AvahiClient: %s", avahi_strerror (error));
			if (error == AVAHI_ERR_NO_DAEMON) {
				g_set_error (err,
					     RB_DAAP_MDNS_ERROR,
					     RB_DAAP_MDNS_ERROR_NOT_RUNNING,
					     "Avahi daemon was noty running");
			} else {
				g_set_error (err,
					     RB_DAAP_MDNS_ERROR,
					     RB_DAAP_MDNS_ERROR_FAILED,
					     "Error creating avahi client: %s", avahi_strerror (error));
			}
			return NULL;
		}

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
	   RBDAAPmDNSBrowserData *browse_data)
{
	RBDAAPmDNSBrowserStatus bstatus;

	if (avahi_client_is_service_local (get_avahi_client (NULL), interface, protocol, name, type, domain)) {
		rb_debug ("Ignoring local service %s", name);
		return;
	}
	
	if (event == AVAHI_BROWSER_NEW) {
		bstatus = RB_DAAP_MDNS_BROWSER_ADD_SERVICE;
	} else if (event == AVAHI_BROWSER_REMOVE) {
		bstatus = RB_DAAP_MDNS_BROWSER_REMOVE_SERVICE;
	} else {
		return;
	}

	(browse_data->callback) ((RBDAAPmDNSBrowser) browse_data,
				 bstatus,
				 name,
				 browse_data->data);
}

gboolean
rb_daap_mdns_browse (RBDAAPmDNSBrowser *browser,
		     RBDAAPmDNSBrowserCallback callback,
		     gpointer user_data)
{
	AvahiClient *client;
	RBDAAPmDNSBrowserData *browse_data;
	GError *error = NULL;
	
	client = get_avahi_client (&error);
	if (!client) {
		rb_debug ("Error initializing Avahi for browsing");
		if (!g_error_matches (error, RB_DAAP_MDNS_ERROR, RB_DAAP_MDNS_ERROR_NOT_RUNNING))
			mdns_error_dialog ("Avahi");
		return FALSE;
	}

	browse_data = g_new0(RBDAAPmDNSBrowserData, 1);
	*browser = (RBDAAPmDNSBrowser)browse_data;
	browse_data->callback = callback;
	browse_data->data = user_data;
	
	browse_data->sb = avahi_service_browser_new (client,
						     AVAHI_IF_UNSPEC,
						     AVAHI_PROTO_INET,
						     "_daap._tcp",
						     "local",
						     (AvahiServiceBrowserCallback)browse_cb,
						     browse_data);
	if (browse_data->sb == NULL) {
		g_free (browse_data);
		rb_debug ("Error starting mDNS discovery using AvahiServiceBrowser");
		mdns_error_dialog ("Avahi");
		return FALSE;
	}

	return TRUE;
}

void
rb_daap_mdns_browse_cancel (RBDAAPmDNSBrowser browser)
{
	RBDAAPmDNSBrowserData *browse_data = (RBDAAPmDNSBrowserData*)browser;

	avahi_service_browser_free (browse_data->sb);
	g_free (browse_data);
}



typedef struct {
	AvahiServiceResolver *sr;
	RBDAAPmDNSResolverCallback callback;
	gpointer data;
} RBDAAPmDNSResolverData;

static void
resolve_cb (AvahiServiceResolver *resolver,
	    AvahiIfIndex interface,
	    AvahiProtocol protocol,
	    AvahiResolverEvent event,
	    const char *service_name,
	    const char *type,
	    const char *domain,
	    const char *host_name,
	    const AvahiAddress *address,
	    uint16_t port,
	    AvahiStringList *text,
	    RBDAAPmDNSResolverData *res_data)
{

	if (event == AVAHI_RESOLVER_FOUND) {
		gchar *host;
		gboolean pp = FALSE;
		gchar *name = NULL;
		
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

			l = NULL;
			l = avahi_string_list_find (text, "Machine Name");
			
			if (l) {
				size_t s;
				char *n;
				char *v;

				avahi_string_list_get_pair (l, &n, &v, &s);
				name = g_strdup (v);

				avahi_free (n);
				avahi_free (v);
			}
		}

		if (name == NULL) {
			name = g_strdup (service_name);
		}
		
		host = g_malloc0 (16);
		avahi_address_snprint (host, 16, address);


		(res_data->callback) ((RBDAAPmDNSResolver) res_data,
				      RB_DAAP_MDNS_RESOLVER_FOUND,
				      service_name,
				      name,
				      host,
				      port,
				      pp,
				      res_data->data);

		g_free (host);
		g_free (name);
	} else if (event == RB_DAAP_MDNS_RESOLVER_TIMEOUT) {
		(res_data->callback) ((RBDAAPmDNSResolver) res_data,
				      RB_DAAP_MDNS_RESOLVER_TIMEOUT,
				      service_name,
				      NULL,
				      NULL,
				      0,
				      FALSE,
				      res_data->data);
	}
}

gboolean
rb_daap_mdns_resolve (RBDAAPmDNSResolver *resolver,
		      const gchar *name,
		      RBDAAPmDNSResolverCallback callback,
		      gpointer user_data)
{
	AvahiClient *client;
	GError *error = NULL;
	RBDAAPmDNSResolverData *res_data;
	
	client = get_avahi_client (&error);
	if (!client) {
		rb_debug ("Error initializing Avahi for resolving");
		if (!g_error_matches (error, RB_DAAP_MDNS_ERROR, RB_DAAP_MDNS_ERROR_NOT_RUNNING))
			mdns_error_dialog ("Avahi");
		return FALSE;
	}

	res_data = g_new0 (RBDAAPmDNSResolverData, 1);
	*resolver = (RBDAAPmDNSResolver) res_data;
	res_data->callback = callback;
	res_data->data = user_data;
	
	res_data->sr = avahi_service_resolver_new (client,
						   AVAHI_IF_UNSPEC,
						   AVAHI_PROTO_INET,
						   name,
						   "_daap._tcp",
						   "local",
						   AVAHI_PROTO_UNSPEC,
						   (AvahiServiceResolverCallback)resolve_cb,
						   res_data);
	if (res_data->sr == NULL) {
		g_free (res_data);
		rb_debug ("Error starting mDNS resolving using AvahiServiceResolver");
		mdns_error_dialog ("Avahi");
		return FALSE;
	}

	return TRUE;
}

void
rb_daap_mdns_resolve_cancel (RBDAAPmDNSResolver resolver)
{
	RBDAAPmDNSResolverData *res_data = (RBDAAPmDNSResolverData*) resolver;

	avahi_service_resolver_free (res_data->sr);
	g_free (res_data);
}



typedef struct {
	AvahiEntryGroup *eg;
	RBDAAPmDNSPublisherCallback callback;
	gpointer data;
	guint port;
} RBDAAPmDNSPublisherData;

static gint
add_service (AvahiEntryGroup *group,
	     const gchar *name,
	     guint port)
{
	gint ret;
	
	ret = avahi_entry_group_add_service (group,
					     AVAHI_IF_UNSPEC,
					     AVAHI_PROTO_INET,
					     name,
					     "_daap._tcp",
					     NULL,
					     NULL,
					     port,
					     NULL);

	if (ret < 0) {
		return ret;
	}
	
	ret = avahi_entry_group_commit (group);

	return ret;
}


static void
entry_group_cb (AvahiEntryGroup *group,
		AvahiEntryGroupState state,
		RBDAAPmDNSPublisherData *pub_data)
{
	if (state == AVAHI_ENTRY_GROUP_ESTABLISHED) {
		(pub_data->callback) ((RBDAAPmDNSPublisher)pub_data,
				      RB_DAAP_MDNS_PUBLISHER_STARTED,
				      pub_data->data);
	} else if (state == AVAHI_ENTRY_GROUP_COLLISION) {
		gchar *new_name = NULL;
		gint ret;
		
		new_name = (pub_data->callback) (
				(RBDAAPmDNSPublisher)pub_data,
				RB_DAAP_MDNS_PUBLISHER_COLLISION, 
				pub_data->data);
		
		ret = add_service (group, new_name, pub_data->port);

		g_free (new_name);

		if (ret < 0) {
			rb_debug ("Error adding service to AvahiEntryGroup: %s", avahi_strerror (ret));
			mdns_error_dialog ("Avahi");
			return;
		}
	}
}

gboolean
rb_daap_mdns_publish (RBDAAPmDNSPublisher *publisher,
		      const gchar *name,
		      guint port,
		      RBDAAPmDNSPublisherCallback callback,
		      gpointer user_data)
{
	AvahiClient *client;
	RBDAAPmDNSPublisherData *pub_data;
	gint ret;
	GError *error = NULL;
	
	client = get_avahi_client (&error);
	if (!client) {
		rb_debug ("Error initializing Avahi for publishing");
		if (!g_error_matches (error, RB_DAAP_MDNS_ERROR, RB_DAAP_MDNS_ERROR_NOT_RUNNING))
			mdns_error_dialog ("Avahi");
		return FALSE;
	}

	pub_data = g_new0 (RBDAAPmDNSPublisherData, 1);
	*publisher = (RBDAAPmDNSPublisher) pub_data;
	pub_data->callback = callback;
	pub_data->port = port;
	pub_data->data = user_data;
	
	pub_data->eg = avahi_entry_group_new (client,
					      (AvahiEntryGroupCallback)entry_group_cb,
					      pub_data);
	if (pub_data->eg == NULL) {
		g_free (pub_data);
		rb_debug ("Could not create AvahiEntryGroup for publishing");
		mdns_error_dialog ("Avahi");
		return FALSE;
	}

	ret = add_service (pub_data->eg, name, port);
	
	if (ret < 0) {
		g_free (pub_data);
		rb_debug ("Error adding service to AvahiEntryGroup: %s", avahi_strerror (ret));
		mdns_error_dialog ("Avahi");
		return FALSE;
	}
	
	return TRUE;
}

void
rb_daap_mdns_publish_cancel (RBDAAPmDNSPublisher publisher)
{
	RBDAAPmDNSPublisherData *pub_data = (RBDAAPmDNSPublisherData*)publisher;

	avahi_entry_group_free (pub_data->eg);
	g_free (pub_data);
}
#endif
