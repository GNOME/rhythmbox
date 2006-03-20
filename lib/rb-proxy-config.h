/*
 *  Copyright (C) 2006  Jonathan Matthew  <jonathan@kaolin.wh9.net>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  
 *  02110-1301  USA
 */

#ifndef RB_PROXY_CONFIG_H
#define RB_PROXY_CONFIG_H

#include <glib.h>
#include <glib-object.h>

#if defined(HAVE_LIBSOUP)
#include <libsoup/soup.h>
#include <libsoup/soup-uri.h>
#endif

G_BEGIN_DECLS

#define RB_TYPE_PROXY_CONFIG			(rb_proxy_config_get_type ())
#define RB_PROXY_CONFIG(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PROXY_CONFIG, RBProxyConfig))
#define RB_PROXY_CONFIG_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PROXY_CONFIG, RBProxyConfigClass))
#define RB_IS_PROXY_CONFIG(o)			(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PROXY_CONFIG))
#define RB_IS_PROXY_CONFIG_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PROXY_CONFIG))
#define RB_PROXY_CONFIG_GET_CLASS(o)		(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PROXY_CONFIG, RBProxyConfigClass))

typedef struct _RBProxyConfigPrivate RBProxyConfigPrivate;

typedef struct
{
	GObject parent;
	gboolean enabled;
	char *host;
	guint port;

	gboolean auth_enabled;
	char *username;
	char *password;

	RBProxyConfigPrivate *priv;
} RBProxyConfig;

typedef struct
{
	GObjectClass parent_class;

	void (*config_changed) (RBProxyConfig *config);
} RBProxyConfigClass;


GType		rb_proxy_config_get_type (void);

RBProxyConfig *	rb_proxy_config_new (void);

#if defined(HAVE_LIBSOUP)
SoupUri *	rb_proxy_config_get_libsoup_uri (RBProxyConfig *config);
#endif

#endif	/* RB_PROXY_CONFIG_H */

