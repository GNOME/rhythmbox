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

#ifndef __RB_DAAP_MDNS_PUBLISHER_H
#define __RB_DAAP_MDNS_PUBLISHER_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_DAAP_MDNS_PUBLISHER         (rb_daap_mdns_publisher_get_type ())
#define RB_DAAP_MDNS_PUBLISHER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_DAAP_MDNS_PUBLISHER, RBDaapMdnsPublisher))
#define RB_DAAP_MDNS_PUBLISHER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_DAAP_MDNS_PUBLISHER, RBDaapMdnsPublisherClass))
#define RB_IS_DAAP_MDNS_PUBLISHER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_DAAP_MDNS_PUBLISHER))
#define RB_IS_DAAP_MDNS_PUBLISHER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_DAAP_MDNS_PUBLISHER))
#define RB_DAAP_MDNS_PUBLISHER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_DAAP_MDNS_PUBLISHER, RBDaapMdnsPublisherClass))

typedef struct RBDaapMdnsPublisherPrivate RBDaapMdnsPublisherPrivate;

typedef struct
{
	GObject object;

	RBDaapMdnsPublisherPrivate *priv;
} RBDaapMdnsPublisher;

typedef struct
{
	GObjectClass parent_class;

	void (* published)         (RBDaapMdnsPublisher *publisher,
				    const char          *name);
	void (* name_collision)    (RBDaapMdnsPublisher *publisher,
				    const char          *name);
} RBDaapMdnsPublisherClass;

typedef enum
{
	RB_DAAP_MDNS_PUBLISHER_ERROR_NOT_RUNNING,
	RB_DAAP_MDNS_PUBLISHER_ERROR_FAILED,
} RBDaapMdnsPublisherError;

#define RB_DAAP_MDNS_PUBLISHER_ERROR rb_daap_mdns_publisher_error_quark ()

GQuark      rb_daap_mdns_publisher_error_quark                     (void);

GType       rb_daap_mdns_publisher_get_type                        (void);

RBDaapMdnsPublisher *rb_daap_mdns_publisher_new                    (void);
gboolean             rb_daap_mdns_publisher_publish                (RBDaapMdnsPublisher *publisher,
								    const char          *name,
								    guint                port,
								    gboolean             password_required,
								    GError             **error);
gboolean             rb_daap_mdns_publisher_set_name               (RBDaapMdnsPublisher *publisher,
								    const char          *name,
								    GError             **error);
gboolean             rb_daap_mdns_publisher_set_port               (RBDaapMdnsPublisher *publisher,
								    guint                port,
								    GError             **error);
gboolean             rb_daap_mdns_publisher_set_password_required  (RBDaapMdnsPublisher *publisher,
								    gboolean             required,
								    GError             **error);
gboolean             rb_daap_mdns_publisher_withdraw               (RBDaapMdnsPublisher *publisher,
								    GError             **error);

G_END_DECLS

#endif /* __RB_DAAP_MDNS_PUBLISHER_H */
