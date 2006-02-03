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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

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

#include "rb-daap-mdns-publisher.h"
#include "rb-debug.h"

static void	rb_daap_mdns_publisher_class_init (RBDaapMdnsPublisherClass *klass);
static void	rb_daap_mdns_publisher_init	  (RBDaapMdnsPublisher	    *publisher);
static void	rb_daap_mdns_publisher_finalize   (GObject	            *object);

#define RB_DAAP_MDNS_PUBLISHER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_DAAP_MDNS_PUBLISHER, RBDaapMdnsPublisherPrivate))

struct RBDaapMdnsPublisherPrivate
{
	AvahiClient     *client;
	AvahiGLibPoll   *poll;
	AvahiEntryGroup *entry_group;

	char            *name;
	guint            port;
	gboolean         password_required;
};

enum { 
	PUBLISHED,
	NAME_COLLISION,
	LAST_SIGNAL
};

enum {
	PROP_0
};

static GObjectClass *parent_class = NULL;
static guint	     signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (RBDaapMdnsPublisher, rb_daap_mdns_publisher, G_TYPE_OBJECT)

static gpointer publisher_object = NULL;

GQuark
rb_daap_mdns_publisher_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_daap_mdns_publisher_error");

	return quark;
}

static void
client_cb (AvahiClient         *client,
	   AvahiClientState     state,
	   RBDaapMdnsPublisher *publisher)
{
	/* FIXME
	 * check to make sure we're in the _RUNNING state before we publish
	 * check for COLLISION state and remove published information
	 */

	/* Called whenever the client or server state changes */
 
	switch (state) {
	case AVAHI_CLIENT_S_RUNNING:
         
		/* The server has startup successfully and registered its host
		 * name on the network, so it's time to create our services */
		
		break;
 
	case AVAHI_CLIENT_S_COLLISION:
         
		 /* Let's drop our registered services. When the server is back
		  * in AVAHI_SERVER_RUNNING state we will register them
		  * again with the new host name. */
		 if (publisher->priv->entry_group) {
			 avahi_entry_group_reset (publisher->priv->entry_group);
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

static void
avahi_client_init (RBDaapMdnsPublisher *publisher)
{
	gint error = 0;

	avahi_set_allocator (avahi_glib_allocator ());

	publisher->priv->poll = avahi_glib_poll_new (NULL, G_PRIORITY_DEFAULT);

	if (! publisher->priv->poll) {
		rb_debug ("Unable to create AvahiGlibPoll object for mDNS");
	}

#ifdef HAVE_AVAHI_0_5
	publisher->priv->client = avahi_client_new (avahi_glib_poll_get (publisher->priv->poll),
						    client_cb,
						    publisher,
						    &error);
#endif
#ifdef HAVE_AVAHI_0_6
	{
		AvahiClientFlags flags;
		flags = 0;

		publisher->priv->client = avahi_client_new (avahi_glib_poll_get (publisher->priv->poll),
							    flags,
							    (AvahiClientCallback)client_cb,
							    publisher,
							    &error);
	}
#endif
}

static void
entry_group_cb (AvahiEntryGroup     *group,
		AvahiEntryGroupState state,
		RBDaapMdnsPublisher *publisher)
{
	if (state == AVAHI_ENTRY_GROUP_ESTABLISHED) {

		g_signal_emit (publisher, signals [PUBLISHED], 0, publisher->priv->name);

	} else if (state == AVAHI_ENTRY_GROUP_COLLISION) {
		g_warning ("MDNS name collision");

		g_signal_emit (publisher, signals [NAME_COLLISION], 0, publisher->priv->name);
	}
}

static gboolean
create_service (RBDaapMdnsPublisher *publisher,
		GError             **error)
{
	int         ret;
	const char *txt_record;

	if (publisher->priv->entry_group == NULL) {
		publisher->priv->entry_group = avahi_entry_group_new (publisher->priv->client,
								      (AvahiEntryGroupCallback)entry_group_cb,
								      publisher);
	} else {
		avahi_entry_group_reset (publisher->priv->entry_group);
	}

	if (publisher->priv->entry_group == NULL) {
		rb_debug ("Could not create AvahiEntryGroup for publishing");
		g_set_error (error,
			     RB_DAAP_MDNS_PUBLISHER_ERROR,
			     RB_DAAP_MDNS_PUBLISHER_ERROR_FAILED,
			     "%s",
			     _("Could not create AvahiEntryGroup for publishing"));
		return FALSE;
	}

#if 0
	g_message ("Service name:%s port:%u password:%d",
		   publisher->priv->name,
		   publisher->priv->port,
		   publisher->priv->password_required);
#endif

	if (publisher->priv->password_required) {
		txt_record = "Password=true";
	} else {
		txt_record = "Password=false";
	}

	ret = avahi_entry_group_add_service (publisher->priv->entry_group,
					     AVAHI_IF_UNSPEC,
					     AVAHI_PROTO_UNSPEC,
#ifdef HAVE_AVAHI_0_6
					     0,
#endif
					     publisher->priv->name,
					     "_daap._tcp",
					     NULL,
					     NULL,
					     publisher->priv->port,
					     txt_record,
					     NULL);

	if (ret < 0) {
		g_set_error (error,
			     RB_DAAP_MDNS_PUBLISHER_ERROR,
			     RB_DAAP_MDNS_PUBLISHER_ERROR_FAILED,
			     "%s: %s",
			     _("Could not add service"),
			     avahi_strerror (ret));
		return FALSE;
	}

	ret = avahi_entry_group_commit (publisher->priv->entry_group);

	if (ret < 0) {
		g_set_error (error,
			     RB_DAAP_MDNS_PUBLISHER_ERROR,
			     RB_DAAP_MDNS_PUBLISHER_ERROR_FAILED,
			     "%s: %s",
			     _("Could not commit service"),
			     avahi_strerror (ret));
		return FALSE;
	}

	return TRUE;
}

static gboolean
refresh_service (RBDaapMdnsPublisher *publisher,
		 GError             **error)
{
	return create_service (publisher, error);
}

static gboolean
publisher_set_name_internal (RBDaapMdnsPublisher *publisher,
			     const char          *name,
			     GError             **error)
{
	g_free (publisher->priv->name);
	publisher->priv->name = g_strdup (name);

	return TRUE;
}

gboolean
rb_daap_mdns_publisher_set_name (RBDaapMdnsPublisher *publisher,
				 const char          *name,
				 GError             **error)
{
        g_return_val_if_fail (publisher != NULL, FALSE);

	publisher_set_name_internal (publisher, name, error);

	if (publisher->priv->entry_group) {
		refresh_service (publisher, error);
	}

	return TRUE;
}

static gboolean
publisher_set_port_internal (RBDaapMdnsPublisher *publisher,
			     guint                port,
			     GError             **error)
{
	publisher->priv->port = port;

	return TRUE;
}

gboolean
rb_daap_mdns_publisher_set_port (RBDaapMdnsPublisher *publisher,
				 guint                port,
				 GError             **error)
{
        g_return_val_if_fail (publisher != NULL, FALSE);

	publisher_set_port_internal (publisher, port, error);

	if (publisher->priv->entry_group) {
		refresh_service (publisher, error);
	}

	return TRUE;
}

static gboolean
publisher_set_password_required_internal (RBDaapMdnsPublisher *publisher,
					  gboolean             required,
					  GError             **error)
{
	publisher->priv->password_required = required;
	return TRUE;
}

gboolean
rb_daap_mdns_publisher_set_password_required (RBDaapMdnsPublisher *publisher,
					      gboolean             required,
					      GError             **error)
{
        g_return_val_if_fail (publisher != NULL, FALSE);

	publisher_set_password_required_internal (publisher, required, error);

	if (publisher->priv->entry_group) {
		refresh_service (publisher, error);
	}

	return TRUE;
}

gboolean
rb_daap_mdns_publisher_publish (RBDaapMdnsPublisher *publisher,
				const char          *name,
				guint                port,
				gboolean             password_required,
				GError             **error)
{
	if (publisher->priv->client == NULL) {
                g_set_error (error,
			     RB_DAAP_MDNS_PUBLISHER_ERROR,
			     RB_DAAP_MDNS_PUBLISHER_ERROR_NOT_RUNNING,
                             "%s",
                             _("The avahi MDNS service is not running"));
		return FALSE;
	}

	publisher_set_name_internal (publisher, name, NULL);
	publisher_set_port_internal (publisher, port, NULL);
	publisher_set_password_required_internal (publisher, password_required, NULL);

	return create_service (publisher, error);
}

gboolean
rb_daap_mdns_publisher_withdraw (RBDaapMdnsPublisher *publisher,
				 GError             **error)
{
	if (publisher->priv->client == NULL) {
                g_set_error (error,
			     RB_DAAP_MDNS_PUBLISHER_ERROR,
			     RB_DAAP_MDNS_PUBLISHER_ERROR_NOT_RUNNING,
                             "%s",
                             _("The avahi MDNS service is not running"));
		return FALSE;
	}

	if (publisher->priv->entry_group == NULL) {
                g_set_error (error,
			     RB_DAAP_MDNS_PUBLISHER_ERROR,
			     RB_DAAP_MDNS_PUBLISHER_ERROR_FAILED,
                             "%s",
                             _("The MDNS service is not published"));
		return FALSE;
	}

	avahi_entry_group_reset (publisher->priv->entry_group);
	avahi_entry_group_free (publisher->priv->entry_group);
	publisher->priv->entry_group = NULL;

	return TRUE;
}

static void
rb_daap_mdns_publisher_set_property (GObject	  *object,
				     guint	   prop_id,
				     const GValue *value,
				     GParamSpec	  *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_daap_mdns_publisher_get_property (GObject	 *object,
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
rb_daap_mdns_publisher_class_init (RBDaapMdnsPublisherClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize	   = rb_daap_mdns_publisher_finalize;
	object_class->get_property = rb_daap_mdns_publisher_get_property;
	object_class->set_property = rb_daap_mdns_publisher_set_property;

	signals [PUBLISHED] =
		g_signal_new ("published",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBDaapMdnsPublisherClass, published),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);
	signals [NAME_COLLISION] =
		g_signal_new ("name-collision",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBDaapMdnsPublisherClass, name_collision),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (RBDaapMdnsPublisherPrivate));
}

static void
rb_daap_mdns_publisher_init (RBDaapMdnsPublisher *publisher)
{
	publisher->priv = RB_DAAP_MDNS_PUBLISHER_GET_PRIVATE (publisher);

	avahi_client_init (publisher);
}

static void
rb_daap_mdns_publisher_finalize (GObject *object)
{
	RBDaapMdnsPublisher *publisher;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_DAAP_MDNS_PUBLISHER (object));

	publisher = RB_DAAP_MDNS_PUBLISHER (object);

	g_return_if_fail (publisher->priv != NULL);

	if (publisher->priv->entry_group) {
		avahi_entry_group_free (publisher->priv->entry_group);
	}

	if (publisher->priv->client) {
		avahi_client_free (publisher->priv->client);
	}

	if (publisher->priv->poll) {
		avahi_glib_poll_free (publisher->priv->poll);
	}

	g_free (publisher->priv->name);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBDaapMdnsPublisher *
rb_daap_mdns_publisher_new (void)
{
	if (publisher_object) {
		g_object_ref (publisher_object);
	} else {
		publisher_object = g_object_new (RB_TYPE_DAAP_MDNS_PUBLISHER, NULL);
		g_object_add_weak_pointer (publisher_object,
					   (gpointer *) &publisher_object);
	}

	return RB_DAAP_MDNS_PUBLISHER (publisher_object);
}
