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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <libgnome/gnome-i18n.h>

#include "rb-statusbar.h"
#include "rb-thread-helpers.h"
#include "rb-preferences.h"
#include "rb-search-entry.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"

static void rb_statusbar_class_init (RBStatusbarClass *klass);
static void rb_statusbar_init (RBStatusbar *shell_player);
static void rb_statusbar_finalize (GObject *object);
static void rb_statusbar_set_property (GObject *object,
					  guint prop_id,
					  const GValue *value,
					  GParamSpec *pspec);
static void rb_statusbar_get_property (GObject *object,
					  guint prop_id,
					  GValue *value,
					  GParamSpec *pspec);
static void rb_statusbar_sync_with_source (RBStatusbar *statusbar);
static void rb_statusbar_status_changed_cb (RBSource *source,
					    RBStatusbar *statusbar);
static void rb_statusbar_sync_toggles (RBStatusbar *statusbar);
static void rb_statusbar_state_changed_cb (GConfClient *client,
					   guint cnxn_id,
					   GConfEntry *entry,
					   RBStatusbar *statusbar);

static void rb_statusbar_toggle_changed_cb (GtkToggleButton *toggle,
					    RBStatusbar *statusbar);

struct RBStatusbarPrivate
{
	RBSource *selected_source;

	GtkWidget *shuffle;
	GtkWidget *repeat;	
	GtkWidget *status;
};

enum
{
	PROP_0,
	PROP_SOURCE,
};

static GObjectClass *parent_class = NULL;

GType
rb_statusbar_get_type (void)
{
	static GType rb_statusbar_type = 0;

	if (rb_statusbar_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBStatusbarClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_statusbar_class_init,
			NULL,
			NULL,
			sizeof (RBStatusbar),
			0,
			(GInstanceInitFunc) rb_statusbar_init
		};

		rb_statusbar_type = g_type_register_static (GTK_TYPE_HBOX,
								"RBStatusbar",
								&our_info, 0);
	}

	return rb_statusbar_type;
}

static void
rb_statusbar_class_init (RBStatusbarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_statusbar_finalize;

	object_class->set_property = rb_statusbar_set_property;
	object_class->get_property = rb_statusbar_get_property;

	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "RBSource",
							      "RBSource object",
							      RB_TYPE_SOURCE,
							      G_PARAM_READWRITE));
}

static void
rb_statusbar_init (RBStatusbar *statusbar)
{
	statusbar->priv = g_new0 (RBStatusbarPrivate, 1);

	statusbar->priv->shuffle = gtk_check_button_new_with_label (_("Shuffle"));
	statusbar->priv->repeat = gtk_check_button_new_with_label (_("Repeat"));
	g_signal_connect (G_OBJECT (statusbar->priv->shuffle), "toggled",
			  G_CALLBACK (rb_statusbar_toggle_changed_cb), statusbar);
	g_signal_connect (G_OBJECT (statusbar->priv->repeat), "toggled",
			  G_CALLBACK (rb_statusbar_toggle_changed_cb), statusbar);
	
	statusbar->priv->status = gtk_label_new ("Status goes here");
	gtk_label_set_justify (GTK_LABEL (statusbar->priv->status), GTK_JUSTIFY_RIGHT);

	gtk_box_set_spacing (GTK_BOX (statusbar), 5);

	gtk_box_pack_start (GTK_BOX (statusbar),
			    GTK_WIDGET (statusbar->priv->shuffle), FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (statusbar),
			    GTK_WIDGET (statusbar->priv->repeat), FALSE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (statusbar),
			    GTK_WIDGET (statusbar->priv->status), TRUE, TRUE, 0);

	rb_statusbar_sync_toggles (statusbar);

	eel_gconf_notification_add (CONF_STATE_SHUFFLE,
				    (GConfClientNotifyFunc) rb_statusbar_state_changed_cb,
				    statusbar);
	eel_gconf_notification_add (CONF_STATE_REPEAT,
				    (GConfClientNotifyFunc) rb_statusbar_state_changed_cb,
				    statusbar);
}

static void
rb_statusbar_finalize (GObject *object)
{
	RBStatusbar *statusbar;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_STATUSBAR (object));

	statusbar = RB_STATUSBAR (object);

	g_return_if_fail (statusbar->priv != NULL);

	g_free (statusbar->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_statusbar_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBStatusbar *statusbar = RB_STATUSBAR (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
		if (statusbar->priv->selected_source != NULL)
		{
			g_signal_handlers_disconnect_by_func (G_OBJECT (statusbar->priv->selected_source),
							      G_CALLBACK (rb_statusbar_status_changed_cb),
							      statusbar);
		}
		
		statusbar->priv->selected_source = g_value_get_object (value);
		rb_debug ("selected source %p", g_value_get_object (value));

		if (statusbar->priv->selected_source != NULL)
		{
			g_signal_connect (G_OBJECT (statusbar->priv->selected_source),
					  "status_changed",
					  G_CALLBACK (rb_statusbar_status_changed_cb),
					  statusbar);
		}
		rb_statusbar_sync_with_source (statusbar);
		
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_statusbar_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBStatusbar *statusbar = RB_STATUSBAR (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
		g_value_set_object (value, statusbar->priv->selected_source);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_statusbar_set_source (RBStatusbar *statusbar,
			    RBSource *source)
{
	g_return_if_fail (RB_IS_STATUSBAR (statusbar));
	g_return_if_fail (RB_IS_SOURCE (source));

	g_object_set (G_OBJECT (statusbar),
		      "source", source,
		      NULL);
}

RBStatusbar *
rb_statusbar_new (void)
{
	RBStatusbar *statusbar = g_object_new (RB_TYPE_STATUSBAR,
					       NULL);

	g_return_val_if_fail (statusbar->priv != NULL, NULL);

	return statusbar;
}

static void
rb_statusbar_sync_with_source (RBStatusbar *statusbar)
{
	if (statusbar->priv->selected_source != NULL) {
		const char *status = rb_source_get_status (statusbar->priv->selected_source);
		rb_debug  ("got status: %s", status);
		
		gtk_label_set_text (GTK_LABEL (statusbar->priv->status), status);
	}
}

static void
rb_statusbar_status_changed_cb (RBSource *source,
				RBStatusbar *statusbar)
{
	rb_debug  ("status changed for %p", source);
	rb_statusbar_sync_with_source (statusbar);
}

static void
rb_statusbar_sync_toggles (RBStatusbar *statusbar)
{
	rb_debug ("syncing toggles");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (statusbar->priv->shuffle),
				      eel_gconf_get_boolean (CONF_STATE_SHUFFLE));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (statusbar->priv->repeat),
				      eel_gconf_get_boolean (CONF_STATE_REPEAT));
}

static void
rb_statusbar_state_changed_cb (GConfClient *client,
			       guint cnxn_id,
			       GConfEntry *entry,
			       RBStatusbar *statusbar)
{
	rb_debug ("state changed");
	
	rb_statusbar_sync_toggles (statusbar);
}

static void
rb_statusbar_toggle_changed_cb (GtkToggleButton *toggle,
				RBStatusbar *statusbar)
{
	rb_debug ("toggle changed");
	eel_gconf_set_boolean (CONF_STATE_SHUFFLE,
			       gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (statusbar->priv->shuffle)));
	eel_gconf_set_boolean (CONF_STATE_REPEAT,
			       gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (statusbar->priv->repeat)));
}
