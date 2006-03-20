/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2005  Ruben Vermeersch <ruben@Lambda1.be>
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

#include "config.h"

#include "rb-proxy-config.h"
#include "eel-gconf-extensions.h"
#include "rb-debug.h"

enum
{
	CONFIG_CHANGED,
	LAST_SIGNAL
};

static void	rb_proxy_config_class_init (RBProxyConfigClass *klass);
static void	rb_proxy_config_init (RBProxyConfig *config);
static void	rb_proxy_config_dispose (GObject *object);
static void	rb_proxy_config_finalize (GObject *object);
static void	rb_proxy_config_gconf_changed_cb (GConfClient *client,
						  guint cnxn_id,
				 		  GConfEntry *entry,
				 		  RBProxyConfig *config);
static void	get_proxy_config (RBProxyConfig *config);

static guint	rb_proxy_config_signals[LAST_SIGNAL] = { 0 };

struct _RBProxyConfigPrivate
{
	guint enabled_notify_id;
	guint host_notify_id;
	guint port_notify_id;

	guint auth_enabled_notify_id;
	guint username_notify_id;
	guint password_notify_id;
};

G_DEFINE_TYPE (RBProxyConfig, rb_proxy_config, G_TYPE_OBJECT)

static void
rb_proxy_config_class_init (RBProxyConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_proxy_config_dispose;
	object_class->finalize = rb_proxy_config_finalize;

	rb_proxy_config_signals [CONFIG_CHANGED] =
		g_signal_new ("config-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBProxyConfigClass, config_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	g_type_class_add_private (klass, sizeof (RBProxyConfigPrivate));
}

static void
rb_proxy_config_init (RBProxyConfig *config)
{
	config->priv = G_TYPE_INSTANCE_GET_PRIVATE (config, RB_TYPE_PROXY_CONFIG, RBProxyConfigPrivate);

	rb_debug ("watching HTTP proxy configuration");

	eel_gconf_monitor_add ("/system/http_proxy");
	config->priv->enabled_notify_id =
		eel_gconf_notification_add ("/system/http_proxy/use_http_proxy",
					(GConfClientNotifyFunc) rb_proxy_config_gconf_changed_cb,
					config);
	config->priv->host_notify_id = 
		eel_gconf_notification_add ("/system/http_proxy/host",
					(GConfClientNotifyFunc) rb_proxy_config_gconf_changed_cb,
					config);
	config->priv->port_notify_id = 
		eel_gconf_notification_add ("/system/http_proxy/port",
					(GConfClientNotifyFunc) rb_proxy_config_gconf_changed_cb,
					config);
	config->priv->auth_enabled_notify_id =
		eel_gconf_notification_add ("/system/http_proxy/use_authentication",
					(GConfClientNotifyFunc) rb_proxy_config_gconf_changed_cb,
					config);
	config->priv->username_notify_id = 
		eel_gconf_notification_add ("/system/http_proxy/authentication_user",
					(GConfClientNotifyFunc) rb_proxy_config_gconf_changed_cb,
					config);
	config->priv->password_notify_id = 
		eel_gconf_notification_add ("/system/http_proxy/authentication_password",
					(GConfClientNotifyFunc) rb_proxy_config_gconf_changed_cb,
					config);

	get_proxy_config (config);
}

static void
rb_proxy_config_dispose (GObject *object)
{
	RBProxyConfig *config;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PROXY_CONFIG (object));
	config = RB_PROXY_CONFIG (object);
	g_return_if_fail (config->priv != NULL);
	
	rb_debug ("Removing HTTP proxy config watch");
	eel_gconf_notification_remove (config->priv->enabled_notify_id);
	eel_gconf_notification_remove (config->priv->host_notify_id);
	eel_gconf_notification_remove (config->priv->port_notify_id);
	eel_gconf_notification_remove (config->priv->auth_enabled_notify_id);
	eel_gconf_notification_remove (config->priv->username_notify_id);
	eel_gconf_notification_remove (config->priv->password_notify_id);
	eel_gconf_monitor_remove ("/system/http_proxy");

	G_OBJECT_CLASS (rb_proxy_config_parent_class)->dispose (object);
}

static void
rb_proxy_config_finalize (GObject *object)
{
	RBProxyConfig *config;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PROXY_CONFIG (object));
	config = RB_PROXY_CONFIG (object);
	g_return_if_fail (config->priv != NULL);
	
	g_free (config->host);
	g_free (config->username);
	g_free (config->password);
	
	G_OBJECT_CLASS (rb_proxy_config_parent_class)->finalize (object);
}

RBProxyConfig *
rb_proxy_config_new ()
{
	return g_object_new (RB_TYPE_PROXY_CONFIG, NULL);
}

static void	
rb_proxy_config_gconf_changed_cb (GConfClient *client,
				  guint cnxn_id,
		 		  GConfEntry *entry,
		 		  RBProxyConfig *config)
{
	rb_debug ("HTTP proxy configuration changed");
	get_proxy_config (config);
	g_signal_emit (config, rb_proxy_config_signals[CONFIG_CHANGED], 0);
}

static void	
get_proxy_config (RBProxyConfig *config)
{
	config->enabled = eel_gconf_get_boolean ("/system/http_proxy/use_http_proxy");

	g_free (config->host);
	config->host = eel_gconf_get_string ("/system/http_proxy/host");
	config->port = eel_gconf_get_integer ("/system/http_proxy/port");

	config->auth_enabled = eel_gconf_get_boolean ("/system/http_proxy/use_authentication");
	g_free (config->username);
	g_free (config->password);
	config->username = eel_gconf_get_string ("/system/http_proxy/authentication_user");
	config->password = eel_gconf_get_string ("/system/http_proxy/authentication_password");

	if (config->enabled) {
		if (config->auth_enabled)
			rb_debug ("HTTP proxy URL is http://%s:<password>@%s:%u/", 
				  config->username, config->host, config->port);
		else
			rb_debug ("HTTP proxy URL is http://%s:%u/",
				  config->host, config->port);
	} else {
		rb_debug ("HTTP proxy is disabled");
	}
}

#if defined(HAVE_LIBSOUP)
SoupUri *
rb_proxy_config_get_libsoup_uri (RBProxyConfig *config)
{
	SoupUri *uri = NULL;

	if (!config->enabled)
		return NULL;

	uri = g_new0 (SoupUri, 1);
	uri->protocol = SOUP_PROTOCOL_HTTP;

	uri->host = g_strdup (config->host);
	uri->port = config->port;
	if (config->auth_enabled) {
		uri->user = g_strdup (config->username);
		uri->passwd = g_strdup (config->password);
	}

	return uri;
}
#endif

