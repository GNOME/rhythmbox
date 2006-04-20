/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  arch-tag: Implementation of the "disclosure" widget
 *
 *  Authors: Iain Holmes <iain@ximian.com>
 *
 *  Copyright 2002 Iain Holmes
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <gtk/gtkexpander.h>

#include "disclosure-widget.h"

static void expander_callback  (GObject *object,
				GParamSpec *param_spec,
				gpointer    user_data);

static GtkExpanderClass *parent_class = NULL;

struct CDDBDisclosurePrivate {
	GtkWidget *container;
	char *shown;
	char *hidden;
};

#define CDDB_DISCLOSURE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CDDB_DISCLOSURE_TYPE, CDDBDisclosurePrivate))

static void
finalize (GObject *object)
{
	CDDBDisclosure *disclosure;

	disclosure = CDDB_DISCLOSURE (object);

	g_free (disclosure->priv->hidden);
	g_free (disclosure->priv->shown);

	if (disclosure->priv->container != NULL) {
		g_object_unref (G_OBJECT (disclosure->priv->container));
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (CDDBDisclosureClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);;

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = finalize;

	g_type_class_add_private (klass, sizeof (CDDBDisclosurePrivate));
}

static void
init (CDDBDisclosure *disclosure)
{
	disclosure->priv = CDDB_DISCLOSURE_GET_PRIVATE (disclosure);
}

GType
cddb_disclosure_get_type (void)
{
	static GType type = 0;

	if (type == 0) {
		GTypeInfo info = {
			sizeof (CDDBDisclosureClass),
			NULL, NULL, (GClassInitFunc) class_init, NULL, NULL,
			sizeof (CDDBDisclosure), 0, (GInstanceInitFunc) init
		};

		type = g_type_register_static (GTK_TYPE_EXPANDER, "CDDBDisclosure", &info, 0);
	}

	return type;
}

GtkWidget *
cddb_disclosure_new (const char *shown,
		     const char *hidden)
{
	CDDBDisclosure *disclosure;

	disclosure = g_object_new (cddb_disclosure_get_type (), "use_underline", TRUE, NULL);

	cddb_disclosure_set_labels (disclosure, shown, hidden);
	g_signal_connect (G_OBJECT (disclosure), "notify::expanded", G_CALLBACK (expander_callback), NULL);

	return GTK_WIDGET (disclosure);
}

void
cddb_disclosure_set_container 	(CDDBDisclosure *cddb, GtkWidget *widget)
{
	if (widget != NULL) {
		g_object_ref (widget);
	}
	if (cddb->priv->container != NULL) {
		g_object_unref (cddb->priv->container);
	}
	cddb->priv->container = widget;
}

void
cddb_disclosure_set_labels	(CDDBDisclosure *cddb,
		                 const char *label_when_shown,
		                 const char *label_when_hidden)
{
	gboolean active;

	g_free (cddb->priv->shown);
	g_free (cddb->priv->hidden);
	cddb->priv->shown = g_strdup (label_when_shown);
	cddb->priv->hidden = g_strdup (label_when_hidden);

	/* update the correct label text depending on button state */
	active = gtk_expander_get_expanded (GTK_EXPANDER(cddb));
	g_object_set (G_OBJECT(cddb),
		      "label", active ? cddb->priv->hidden : cddb->priv->shown,
		      NULL);
}

static void
expander_callback (GObject    *object,
                   GParamSpec *param_spec,
                   gpointer    user_data)
{
	CDDBDisclosure *disclosure = CDDB_DISCLOSURE (object);
	gboolean active;

	active = gtk_expander_get_expanded (GTK_EXPANDER (disclosure));
	g_object_set (disclosure,
		      "label", active ? disclosure->priv->hidden : disclosure->priv->shown,
		      NULL);
}
