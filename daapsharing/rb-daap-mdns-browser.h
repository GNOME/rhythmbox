/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Header for abstraction of Multicast DNS for DAAP sharing
 *
 *  Copyright (C) 2005 Charles Schmidt <cschmidt2@emich.edu>
 *  Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __RB_DAAP_MDNS_BROWSER_H
#define __RB_DAAP_MDNS_BROWSER_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_DAAP_MDNS_BROWSER         (rb_daap_mdns_browser_get_type ())
#define RB_DAAP_MDNS_BROWSER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_DAAP_MDNS_BROWSER, RBDaapMdnsBrowser))
#define RB_DAAP_MDNS_BROWSER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_DAAP_MDNS_BROWSER, RBDaapMdnsBrowserClass))
#define RB_IS_DAAP_MDNS_BROWSER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_DAAP_MDNS_BROWSER))
#define RB_IS_DAAP_MDNS_BROWSER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_DAAP_MDNS_BROWSER))
#define RB_DAAP_MDNS_BROWSER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_DAAP_MDNS_BROWSER, RBDaapMdnsBrowserClass))

typedef struct RBDaapMdnsBrowserPrivate RBDaapMdnsBrowserPrivate;

typedef struct
{
	GObject object;

	RBDaapMdnsBrowserPrivate *priv;
} RBDaapMdnsBrowser;

typedef struct
{
	GObjectClass parent_class;

	void (* service_added)    (RBDaapMdnsBrowser *browser,
				   const char        *service_name,
				   const char        *name,
				   const char        *host,
				   guint              port,
				   gboolean           password_protected);
	void (* service_removed ) (RBDaapMdnsBrowser *browser,
				   const char        *service_name);

} RBDaapMdnsBrowserClass;

typedef enum
{
	RB_DAAP_MDNS_BROWSER_ERROR_NOT_RUNNING,
	RB_DAAP_MDNS_BROWSER_ERROR_FAILED,
} RBDaapMdnsBrowserError;

#define RB_DAAP_MDNS_BROWSER_ERROR rb_daap_mdns_browser_error_quark ()

GQuark             rb_daap_mdns_browser_error_quark (void);

GType              rb_daap_mdns_browser_get_type    (void);

RBDaapMdnsBrowser *rb_daap_mdns_browser_new         (void);
gboolean           rb_daap_mdns_browser_start       (RBDaapMdnsBrowser *browser,
						     GError           **error);
gboolean           rb_daap_mdns_browser_stop        (RBDaapMdnsBrowser *browser,
						     GError           **error);

G_END_DECLS

#endif /* __RB_DAAP_MDNS_BROWSER_H */
