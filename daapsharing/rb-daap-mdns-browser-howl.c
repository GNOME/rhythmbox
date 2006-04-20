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

#include <libgnomevfs/gnome-vfs-address.h>
#include <libgnomevfs/gnome-vfs-resolve.h>

/* stupid howl includes howl_config.h */
#undef PACKAGE
#undef VERSION
#include <howl.h>

#include "rb-daap-mdns-browser.h"
#include "rb-marshal.h"
#include "rb-debug.h"

static void	rb_daap_mdns_browser_class_init (RBDaapMdnsBrowserClass *klass);
static void	rb_daap_mdns_browser_init	(RBDaapMdnsBrowser	*browser);
static void	rb_daap_mdns_browser_finalize   (GObject	        *object);

#define RB_DAAP_MDNS_BROWSER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_DAAP_MDNS_BROWSER, RBDaapMdnsBrowserPrivate))

struct RBDaapMdnsBrowserPrivate
{
	sw_discovery     *discovery;
	sw_discovery_oid *oid;

	GnomeVFSAddress  *local_address;
	guint             watch_id;
	GSList           *resolvers;
};

enum { 
	SERVICE_ADDED,
	SERVICE_REMOVED,
	LAST_SIGNAL
};

enum {
	PROP_0
};

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

static gboolean
howl_in_cb (GIOChannel        *io_channel,
	    GIOCondition       condition,
	    RBDaapMdnsBrowser *browser)
{
	sw_salt salt;

	if (sw_discovery_salt (*browser->priv->discovery, &salt) == SW_OKAY) {
		sw_salt_lock (salt);
		sw_discovery_read_socket (*browser->priv->discovery);
		sw_salt_unlock (salt);
	}
	
	return TRUE;
}

static void
howl_client_init (RBDaapMdnsBrowser *browser)
{
	sw_result   result;
	int         fd;
	GIOChannel *channel;

	browser->priv->discovery = g_new0 (sw_discovery, 1);
	result = sw_discovery_init (browser->priv->discovery);

	if (result != SW_OKAY) {
		g_free (browser->priv->discovery);
		browser->priv->discovery = NULL;
		return;
	}

	fd = sw_discovery_socket (*browser->priv->discovery);

	channel = g_io_channel_unix_new (fd);
	browser->priv->watch_id = g_io_add_watch (channel, G_IO_IN, (GIOFunc)howl_in_cb, browser);
	g_io_channel_unref (channel);
}

static gboolean
host_is_local (RBDaapMdnsBrowser *browser,
	       const char        *host)
{
	GnomeVFSAddress *remote;
	gboolean         equal;
	guint32          l_ip;
	guint32          r_ip;

	if (browser->priv->local_address == NULL) {
		g_warning ("Unable to resolve address");
		return FALSE;
	}

	remote = gnome_vfs_address_new_from_string (host);
	if (remote == NULL) {
		g_warning ("Unable to resolve address for %s", host);
		return FALSE;
	}

	l_ip = gnome_vfs_address_get_ipv4 (browser->priv->local_address);
	r_ip = gnome_vfs_address_get_ipv4 (remote);
	equal = l_ip == r_ip;

	/* FIXME: Use this when we can depend on gnome-vfs 2.14 */
	/*equal = gnome_vfs_address_equal (browser->priv->local_address, remote);*/

	gnome_vfs_address_free (remote);

	return equal;
}

static void
set_local_address (RBDaapMdnsBrowser *browser)
{
	char                   host_name [256];
	GnomeVFSResolveHandle *rh;
	GnomeVFSAddress       *address;
	GnomeVFSResult         res;

	if (gethostname (host_name, sizeof (host_name)) != 0) {
		g_warning ("gethostname failed: %s", g_strerror (errno));
		return;
	}

	res = gnome_vfs_resolve (host_name, &rh);

	if (res != GNOME_VFS_OK) {
		return;
	}

	address = NULL;
	while (gnome_vfs_resolve_next_address (rh, &address)) {
		if (browser->priv->local_address == NULL) {
			browser->priv->local_address = gnome_vfs_address_dup (address);
		}
		gnome_vfs_address_free (address);
	}

	gnome_vfs_resolve_free (rh);
}

static sw_result
resolve_cb (sw_discovery       discovery,
	    sw_discovery_oid   oid, 
	    sw_uint32          interface_index, 
	    sw_const_string    service_name, 
	    sw_const_string    type, 
	    sw_const_string    domain, 
	    sw_ipv4_address    address, 
	    sw_port            port, 
	    sw_octets          text_record, 
	    sw_ulong           text_record_length, 
	    RBDaapMdnsBrowser *browser)
{
	char                   *host;
	char                   *name;
	sw_text_record_iterator it;
	gboolean                pp = FALSE;
	
	host = g_malloc (16);
	name = NULL;

	sw_ipv4_address_name (address, host, 16);

	/* skip local services */
	if (host_is_local (browser, host)) {
		goto done;
	}

	if (sw_text_record_iterator_init (&it, text_record, text_record_length) == SW_OKAY) {
		sw_char  key [SW_TEXT_RECORD_MAX_LEN];
		sw_octet val [SW_TEXT_RECORD_MAX_LEN];
		sw_ulong val_len;
		
		while (sw_text_record_iterator_next (it, (char *)key, val, &val_len) == SW_OKAY) {
			if (strcmp ((char *)key, "Password") == 0) {
				if (val_len >= 4 && strncmp ((char *)val, "true", 4) == 0) {
					pp = TRUE;
				}
			}
			if (strcmp ((char *)key, "Machine Name") == 0) {
				if (name != NULL)
					g_free (name);
				name = g_strdup ((char *)val);
			}
		}
		
		sw_text_record_iterator_fina (it);
	}

	if (name == NULL) {
		name = g_strdup (service_name);
	}

	g_signal_emit (browser,
		       signals [SERVICE_ADDED],
		       0,
		       service_name,
		       name,
		       host,
		       port,
		       pp);
 done:
	g_free (host);
	g_free (name);
	
	return SW_OKAY;
}

static gboolean
rb_daap_mdns_browser_resolve (RBDaapMdnsBrowser *browser,
			      const char        *name)
{
	sw_result        result;
	sw_discovery_oid oid;

       	result = sw_discovery_resolve (*browser->priv->discovery,
				       0,
				       name, 
				       "_daap._tcp", 
				       "local",
				       (sw_discovery_resolve_reply) resolve_cb,
	       			       (sw_opaque) browser,
				       (sw_discovery_oid *) &oid);

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

static sw_result
browse_cb (sw_discovery               discovery,
	   sw_discovery_oid           oid,
	   sw_discovery_browse_status status,
	   sw_uint32                  interface_index,
	   sw_const_string            name,
	   sw_const_string            type,
	   sw_const_string            domain,
	   RBDaapMdnsBrowser         *browser)
{
	if (status == SW_DISCOVERY_BROWSE_ADD_SERVICE) {
		browser_add_service (browser, name);
	} else if (status == SW_DISCOVERY_BROWSE_REMOVE_SERVICE) {
		browser_remove_service (browser, name);
	}

	return SW_OKAY;
}

gboolean
rb_daap_mdns_browser_start (RBDaapMdnsBrowser *browser,
			    GError           **error)
{
	sw_result result;

	if (browser->priv->discovery == NULL) {
		g_set_error (error,
			     RB_DAAP_MDNS_BROWSER_ERROR,
			     RB_DAAP_MDNS_BROWSER_ERROR_NOT_RUNNING,
			     "%s",
			     _("MDNS service is not running"));
		return FALSE;
	}

	if (browser->priv->oid != NULL) {
		g_set_error (error,
			     RB_DAAP_MDNS_BROWSER_ERROR,
			     RB_DAAP_MDNS_BROWSER_ERROR_FAILED,
			     "%s",
			     _("Browser already active"));
		return FALSE;
	}

	browser->priv->oid = g_new0 (sw_discovery_oid, 1);

       	result = sw_discovery_browse (*browser->priv->discovery,
				      0,
				      "_daap._tcp", 
				      "local", 
				      (sw_discovery_browse_reply) browse_cb, 
				      (sw_opaque) browser,
				      (sw_discovery_oid *) browser->priv->oid);


	if (result != SW_OKAY) {
		rb_debug ("Error starting mDNS discovery using Howl");
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
	if (browser->priv->discovery == NULL) {
		g_set_error (error,
			     RB_DAAP_MDNS_BROWSER_ERROR,
			     RB_DAAP_MDNS_BROWSER_ERROR_NOT_RUNNING,
			     "%s",
			     _("MDNS service is not running"));
		return FALSE;
	}
	if (browser->priv->oid == NULL) {
		g_set_error (error,
			     RB_DAAP_MDNS_BROWSER_ERROR,
			     RB_DAAP_MDNS_BROWSER_ERROR_FAILED,
			     "%s",
			     _("Browser is not active"));
		return FALSE;

	}

	sw_discovery_cancel (*browser->priv->discovery, *browser->priv->oid);

	g_free (browser->priv->oid);
	browser->priv->oid = NULL;

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

	set_local_address (browser);

	howl_client_init (browser);
}

static void
resolver_free (sw_discovery_oid  *oid,
	       RBDaapMdnsBrowser *browser)
{
	sw_discovery_cancel (*browser->priv->discovery,
			     *oid);
	g_free (oid);
}


static void
rb_daap_mdns_browser_finalize (GObject *object)
{
	RBDaapMdnsBrowser *browser;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_DAAP_MDNS_BROWSER (object));

	browser = RB_DAAP_MDNS_BROWSER (object);

	g_return_if_fail (browser->priv != NULL);

	if (browser->priv->oid) {
		rb_daap_mdns_browser_stop (browser, NULL);
	}

	if (browser->priv->resolvers) {
		g_slist_foreach (browser->priv->resolvers,
				 (GFunc)resolver_free,
				 browser);
		g_slist_free (browser->priv->resolvers);
	}

	if (browser->priv->discovery) {
		sw_discovery_fina (*browser->priv->discovery);
		g_free (browser->priv->discovery);
	}

	if (browser->priv->watch_id > 0) {
		g_source_remove (browser->priv->watch_id);
	}

	if (browser->priv->local_address) {
		gnome_vfs_address_free (browser->priv->local_address);
	}

	G_OBJECT_CLASS (rb_daap_mdns_browser_parent_class)->finalize (object);
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
