/* 
 *  Copyright (C) 2003 Colin Walters <walters@debian.org>
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
 *  $Id$
 */

#include <gtk/gtk.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-druid.h>

#include "rb-druid.h"
#include "rb-preferences.h"
#include "rb-debug.h"
#include "rb-glade-helpers.h"
#include "eel-gconf-extensions.h"

static void rb_druid_class_init (RBDruidClass *klass);
static void rb_druid_init (RBDruid *druid);
static void rb_druid_finalize (GObject *object);
static void rb_druid_set_property (GObject *object,
				   guint prop_id,
				   const GValue *value,
				   GParamSpec *pspec);
static void rb_druid_get_property (GObject *object,
				   guint prop_id,
				   GValue *value,
				   GParamSpec *pspec);
void rb_druid_browse_clicked_cb (GtkButton *button, RBDruid *druid);

struct RBDruidPrivate
{
	RBLibrary *library;
	GnomeDruid *druid;
	GtkWidget *browse_button;
};

enum
{
	PROP_0,
	PROP_LIBRARY,
};

static GObjectClass *parent_class = NULL;

GType
rb_druid_get_type (void)
{
	static GType rb_druid_type = 0;

	if (rb_druid_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBDruidClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_druid_class_init,
			NULL,
			NULL,
			sizeof (RBDruid),
			0,
			(GInstanceInitFunc) rb_druid_init
		};

		rb_druid_type = g_type_register_static (GTK_TYPE_DIALOG,
							"RBDruid",
							&our_info, 0);
	}

	return rb_druid_type;
}

static void
rb_druid_class_init (RBDruidClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_druid_finalize;

	object_class->set_property = rb_druid_set_property;
	object_class->get_property = rb_druid_get_property;

	g_object_class_install_property (object_class,
					 PROP_LIBRARY,
					 g_param_spec_object ("library",
							      "RBLibrary",
							      "RBLibrary object",
							      RB_TYPE_LIBRARY,
							      G_PARAM_READWRITE));
}

static void
rb_druid_init (RBDruid *druid)
{
	GladeXML *xml;
	druid->priv = g_new0 (RBDruidPrivate, 1);

	xml = rb_glade_xml_new ("druid.glade", "druid_toplevel", druid);
	glade_xml_signal_autoconnect (xml);

	druid->priv->druid = GNOME_DRUID (glade_xml_get_widget (xml, "druid_toplevel"));
	druid->priv->browse_button = glade_xml_get_widget (xml, "browse_button");

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (druid)->vbox), GTK_WIDGET (druid->priv->druid));
}

static void
rb_druid_finalize (GObject *object)
{
	RBDruid *druid;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_DRUID (object));

	druid = RB_DRUID (object);

	g_return_if_fail (druid->priv != NULL);

	g_free (druid->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_druid_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBDruid *druid = RB_DRUID (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		druid->priv->library = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_druid_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBDruid *druid = RB_DRUID (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		g_value_set_object (value, druid->priv->library);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBDruid *
rb_druid_new (RBLibrary *library) 
{
	RBDruid *druid = g_object_new (RB_TYPE_DRUID, "library", library, NULL);

	gtk_window_set_modal (GTK_WINDOW (druid), TRUE);

	g_return_val_if_fail (druid->priv != NULL, NULL);

	return druid;
}

void
rb_druid_browse_clicked_cb (GtkButton *button, RBDruid *druid)
{
	rb_debug ("browse");
}
