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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

/* stupid howl includes howl_config.h */
#undef PACKAGE
#undef VERSION
#include <howl.h>

#include "rb-daap-mdns-publisher.h"
#include "rb-debug.h"

static void	rb_daap_mdns_publisher_class_init (RBDaapMdnsPublisherClass *klass);
static void	rb_daap_mdns_publisher_init	  (RBDaapMdnsPublisher	    *publisher);
static void	rb_daap_mdns_publisher_finalize   (GObject	            *object);

#define RB_DAAP_MDNS_PUBLISHER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_DAAP_MDNS_PUBLISHER, RBDaapMdnsPublisherPrivate))

struct RBDaapMdnsPublisherPrivate
{
	sw_discovery     *discovery;
	sw_discovery_oid *oid;

	guint             watch_id;

	char             *name;
	guint             port;
	gboolean          password_required;
};

enum { 
	PUBLISHED,
	NAME_COLLISION,
	LAST_SIGNAL
};

enum {
	PROP_0
};

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

static gboolean
howl_in_cb (GIOChannel          *io_channel,
	    GIOCondition         condition,
	    RBDaapMdnsPublisher *publisher)
{
	sw_salt salt;

	if (sw_discovery_salt (*publisher->priv->discovery, &salt) == SW_OKAY) {
		sw_salt_lock (salt);
		sw_discovery_read_socket (*publisher->priv->discovery);
		sw_salt_unlock (salt);
	}
	
	return TRUE;
}

static void
howl_client_init (RBDaapMdnsPublisher *publisher)
{
	sw_result   result;
	int         fd;
	GIOChannel *channel;

	publisher->priv->discovery = g_new0 (sw_discovery, 1);
	result = sw_discovery_init (publisher->priv->discovery);

	if (result != SW_OKAY) {
		g_free (publisher->priv->discovery);
		publisher->priv->discovery = NULL;
		return;
	}

	fd = sw_discovery_socket (*publisher->priv->discovery);

	channel = g_io_channel_unix_new (fd);
	publisher->priv->watch_id = g_io_add_watch (channel, G_IO_IN, (GIOFunc)howl_in_cb, publisher);
	g_io_channel_unref (channel);
}

static sw_result
publish_cb (sw_discovery                discovery,
	    sw_discovery_oid            oid,
	    sw_discovery_publish_status status,
	    RBDaapMdnsPublisher        *publisher)
{
	if (status == SW_DISCOVERY_PUBLISH_STARTED) {
		g_signal_emit (publisher, signals [PUBLISHED], 0, publisher->priv->name);
	} else if (status == SW_DISCOVERY_PUBLISH_NAME_COLLISION) {
		/* This is all well and good, but howl won't report a name collision.
		 * http://lists.porchdogsoft.com/pipermail/howl-users/Week-of-Mon-20041206/000487.html
		 * So.  Fuck.
		 */
		g_warning ("MDNS name collision");

		g_signal_emit (publisher, signals [NAME_COLLISION], 0, publisher->priv->name);
	}
	
	return SW_OKAY;
}

static const char *
howl_strerror (sw_result result)
{
	const char *str;

	switch (result) {
		case SW_OKAY:
			str = "No error";
		case SW_DISCOVERY_E_NO_MEM:
			str = "Out of memory";
		case SW_DISCOVERY_E_BAD_PARAM:
			str = "Invalid paramater";
		case SW_DISCOVERY_E_UNKNOWN:
		default:
			str = "Unknown error";
	}

	return str;
}

static gboolean
create_service (RBDaapMdnsPublisher *publisher,
		GError             **error)
{
	sw_text_record text_record;
	sw_result      result;

	if (sw_text_record_init (&text_record) != SW_OKAY) {
		rb_debug ("Error initializing Howl text record");
		g_set_error (error,
			     RB_DAAP_MDNS_PUBLISHER_ERROR,
			     RB_DAAP_MDNS_PUBLISHER_ERROR_FAILED,
			     "%s",
			     _("Error initializing Howl for publishing"));
		return FALSE;
	}

	if (publisher->priv->oid != NULL) {
		rb_daap_mdns_publisher_withdraw (publisher, NULL);
	}

	publisher->priv->oid = g_new0 (sw_discovery_oid, 1);

	sw_text_record_add_key_and_string_value (text_record,
						 "Password",
						 publisher->priv->password_required ? "true" : "false");

	result = sw_discovery_publish (*publisher->priv->discovery,
				       0,
				       publisher->priv->name,
				       "_daap._tcp",
				       "local",
				       NULL,
				       publisher->priv->port,
				       sw_text_record_bytes (text_record),
				       sw_text_record_len (text_record),
				       (sw_discovery_publish_reply) publish_cb,
	       			       (sw_opaque) publisher,
				       (sw_discovery_oid *) publisher->priv->oid);

	sw_text_record_fina (text_record);

	if (result != SW_OKAY) {
		rb_debug ("Error starting mDNS publish with Howl: %s", howl_strerror (result));
		g_set_error (error,
			     RB_DAAP_MDNS_PUBLISHER_ERROR,
			     RB_DAAP_MDNS_PUBLISHER_ERROR_FAILED,
			     "%s: %s",
			     _("Error initializing Howl for publishing"),
			     howl_strerror (result));
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

gboolean
rb_daap_mdns_publisher_publish (RBDaapMdnsPublisher *publisher,
				const char          *name,
				guint                port,
				gboolean             password_required,
				GError             **error)
{
	if (publisher->priv->discovery == NULL) {
                g_set_error (error,
			     RB_DAAP_MDNS_PUBLISHER_ERROR,
			     RB_DAAP_MDNS_PUBLISHER_ERROR_NOT_RUNNING,
                             "%s",
                             _("The howl MDNS service is not running"));
		return FALSE;
	}

	rb_daap_mdns_publisher_set_name (publisher, name, NULL);
	rb_daap_mdns_publisher_set_port (publisher, port, NULL);
	rb_daap_mdns_publisher_set_password_required (publisher, password_required, NULL);

	/* special case: the _set_ functions have updated the existing entry group */
	if (publisher->priv->oid != NULL) {
		return TRUE;
	}

	return create_service (publisher, error);
}

gboolean
rb_daap_mdns_publisher_withdraw (RBDaapMdnsPublisher *publisher,
				 GError             **error)
{
	if (publisher->priv->discovery == NULL) {
                g_set_error (error,
			     RB_DAAP_MDNS_PUBLISHER_ERROR,
			     RB_DAAP_MDNS_PUBLISHER_ERROR_NOT_RUNNING,
                             "%s",
                             _("The howl MDNS service is not running"));
		return FALSE;
	}

	if (publisher->priv->oid == NULL) {
                g_set_error (error,
			     RB_DAAP_MDNS_PUBLISHER_ERROR,
			     RB_DAAP_MDNS_PUBLISHER_ERROR_FAILED,
                             "%s",
                             _("The MDNS service is not published"));
		return FALSE;
	}

	sw_discovery_cancel (*publisher->priv->discovery, *publisher->priv->oid);

	g_free (publisher->priv->oid);
	publisher->priv->oid = NULL;

	return TRUE;
}

gboolean
rb_daap_mdns_publisher_set_name (RBDaapMdnsPublisher *publisher,
				 const char          *name,
				 GError             **error)
{
        g_return_val_if_fail (publisher != NULL, FALSE);

	g_free (publisher->priv->name);
	publisher->priv->name = g_strdup (name);

	if (publisher->priv->oid) {
		refresh_service (publisher, error);
	}

	return TRUE;
}
gboolean
rb_daap_mdns_publisher_set_port (RBDaapMdnsPublisher *publisher,
				 guint                port,
				 GError             **error)
{
        g_return_val_if_fail (publisher != NULL, FALSE);

	publisher->priv->port = port;

	if (publisher->priv->oid) {
		refresh_service (publisher, error);
	}

	return TRUE;
}
gboolean
rb_daap_mdns_publisher_set_password_required (RBDaapMdnsPublisher *publisher,
					      gboolean             required,
					      GError             **error)
{
        g_return_val_if_fail (publisher != NULL, FALSE);

	publisher->priv->password_required = required;

	if (publisher->priv->oid) {
		refresh_service (publisher, error);
	}

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

	howl_client_init (publisher);
}

static void
rb_daap_mdns_publisher_finalize (GObject *object)
{
	RBDaapMdnsPublisher *publisher;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_DAAP_MDNS_PUBLISHER (object));

	publisher = RB_DAAP_MDNS_PUBLISHER (object);

	g_return_if_fail (publisher->priv != NULL);

	if (publisher->priv->oid) {
		rb_daap_mdns_publisher_withdraw (publisher, NULL);
	}

	if (publisher->priv->discovery) {
		sw_discovery_fina (*publisher->priv->discovery);
		g_free (publisher->priv->discovery);
	}

	if (publisher->priv->watch_id > 0) {
		g_source_remove (publisher->priv->watch_id);
	}

	g_free (publisher->priv->name);

	G_OBJECT_CLASS (rb_daap_mdns_publisher_parent_class)->finalize (object);
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
