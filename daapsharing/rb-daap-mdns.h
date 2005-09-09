/*
 *  Header for abstraction of Multicast DNS for DAAP sharing
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

#ifndef __RB_DAAP_MDNS_H
#define __RB_DAAP_MDNS_H

#include <glib.h>

/* discovering hosts */
typedef gpointer RBDAAPmDNSBrowser;

typedef enum {
	RB_DAAP_MDNS_BROWSER_ADD_SERVICE = 1,
	RB_DAAP_MDNS_BROWSER_REMOVE_SERVICE
} RBDAAPmDNSBrowserStatus;

typedef void (* RBDAAPmDNSBrowserCallback) (RBDAAPmDNSBrowser browser,
					    RBDAAPmDNSBrowserStatus status,
					    const gchar *name,
					    gpointer user_data);
gboolean
rb_daap_mdns_browse (RBDAAPmDNSBrowser *browser,
		     RBDAAPmDNSBrowserCallback callback,
		     gpointer data);

void
rb_daap_mdns_browse_cancel (RBDAAPmDNSBrowser browser);

/* resolving hosts */
typedef gpointer RBDAAPmDNSResolver;

typedef enum {
	RB_DAAP_MDNS_RESOLVER_FOUND = 1,
	RB_DAAP_MDNS_RESOLVER_TIMEOUT
} RBDAAPmDNSResolverStatus;

typedef void (* RBDAAPmDNSResolverCallback) (RBDAAPmDNSResolver resolver,
					     RBDAAPmDNSResolverStatus status,
					     const gchar *name,
					     gchar *host,
					     guint port,
					     gboolean password_protected,
					     gpointer user_data);

gboolean
rb_daap_mdns_resolve (RBDAAPmDNSResolver *resolver,
		      const gchar *name,
		      RBDAAPmDNSResolverCallback callback,
		      gpointer data);


void
rb_daap_mdns_resolve_cancel (RBDAAPmDNSResolver resolver);

/* publishing */
typedef gpointer RBDAAPmDNSPublisher;

typedef enum {
	RB_DAAP_MDNS_PUBLISHER_STARTED = 1,
	RB_DAAP_MDNS_PUBLISHER_COLLISION
} RBDAAPmDNSPublisherStatus;

typedef void (* RBDAAPmDNSPublisherCallback) (RBDAAPmDNSPublisher publisher,
					      RBDAAPmDNSPublisherStatus status,
					      gpointer user_data);

gboolean
rb_daap_mdns_publish (RBDAAPmDNSPublisher *publisher,
		      const gchar *name,
		      guint port,
		      RBDAAPmDNSPublisherCallback callback,
		      gpointer user_data);

void
rb_daap_mdns_publish_cancel (RBDAAPmDNSPublisher publisher);


#endif /* __RB_DAAP_MDNS_H */


