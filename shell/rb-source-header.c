/* 
 *  Copyright (C) 2003 Colin Walters <walters@debian.org>
 *  Copyright (C) 2003 Jorn Baayen <jorn@nl.linux.org>
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
#include <bonobo/bonobo-ui-util.h>

#include "disclosure-widget.h"
#include "rb-source-header.h"
#include "rb-stock-icons.h"
#include "rb-thread-helpers.h"
#include "rb-bonobo-helpers.h"
#include "rb-preferences.h"
#include "rb-search-entry.h"
#include "rb-glist-wrapper.h"
#include "rb-debug.h"
#include "rb-player.h"
#include "rb-remote.h"
#include "eel-gconf-extensions.h"

static void rb_source_header_class_init (RBSourceHeaderClass *klass);
static void rb_source_header_init (RBSourceHeader *shell_player);
static void rb_source_header_finalize (GObject *object);
static void rb_source_header_set_property (GObject *object,
					  guint prop_id,
					  const GValue *value,
					  GParamSpec *pspec);
static void rb_source_header_get_property (GObject *object,
					  guint prop_id,
					  GValue *value,
					  GParamSpec *pspec);
static void rb_source_header_filter_changed_cb (RBSource *source,
						RBSourceHeader *header);
static void rb_source_header_search_cb (RBSearchEntry *search,
					const char *text,
					RBSourceHeader *header);
static void rb_source_header_disclosure_toggled_cb (GtkToggleButton *disclosure,
						    gpointer data);
static void rb_source_header_gconf_disclosure_changed_cb (GConfClient *client,
							  guint cnxn_id,
							  GConfEntry *entry,
							  RBSourceHeader *header);
static void rb_source_header_view_browser_changed_cb (BonoboUIComponent *component,
						      const char *path,
						      Bonobo_UIComponent_EventType type,
						      const char *state,
						      RBSourceHeader *header);
static void rb_source_header_sync_control_state (RBSourceHeader *header);

struct RBSourceHeaderPrivate
{
	RBSource *selected_source;

	BonoboUIComponent *component;

	GtkWidget *search;
	GtkWidget *disclosure;

	guint browser_notify_id;
	const char *browser_key;
};

enum
{
	PROP_0,
	PROP_COMPONENT,
	PROP_SOURCE,
};

#define CMD_PATH_VIEW_BROWSER	"/commands/ViewBrowser"

static RBBonoboUIListener rb_source_header_listeners[] =
{
	RB_BONOBO_UI_LISTENER ("ViewBrowser", (BonoboUIListenerFn) rb_source_header_view_browser_changed_cb),
	RB_BONOBO_UI_LISTENER_END
};

static GObjectClass *parent_class = NULL;

GType
rb_source_header_get_type (void)
{
	static GType rb_source_header_type = 0;

	if (rb_source_header_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBSourceHeaderClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_source_header_class_init,
			NULL,
			NULL,
			sizeof (RBSourceHeader),
			0,
			(GInstanceInitFunc) rb_source_header_init
		};

		rb_source_header_type = g_type_register_static (GTK_TYPE_HBOX,
							       "RBSourceHeader",
							       &our_info, 0);
	}

	return rb_source_header_type;
}

static void
rb_source_header_class_init (RBSourceHeaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_source_header_finalize;

	object_class->set_property = rb_source_header_set_property;
	object_class->get_property = rb_source_header_get_property;

	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "RBSource",
							      "RBSource object",
							      RB_TYPE_SOURCE,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_COMPONENT,
					 g_param_spec_object ("component",
							      "BonoboUIComponent",
							      "BonoboUIComponent object",
							      BONOBO_TYPE_UI_COMPONENT,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_source_header_init (RBSourceHeader *header)
{
	GtkWidget *align;
	header->priv = g_new0 (RBSourceHeaderPrivate, 1);

	gtk_box_set_spacing (GTK_BOX (header), 5);

	header->priv->search = GTK_WIDGET (rb_search_entry_new ());

	g_signal_connect (G_OBJECT (header->priv->search), "search",
			  G_CALLBACK (rb_source_header_search_cb), header);

	header->priv->disclosure = cddb_disclosure_new (_("Show Browser"),
							_("Hide Browser"));
	gtk_widget_set_sensitive (header->priv->disclosure, FALSE);
	g_signal_connect (G_OBJECT (header->priv->disclosure), "toggled",
			  G_CALLBACK (rb_source_header_disclosure_toggled_cb), header);

	gtk_box_pack_start (GTK_BOX (header),
			    GTK_WIDGET (header->priv->disclosure), FALSE, TRUE, 0);

	align = gtk_alignment_new (1.0, 0.5, 0.5, 1.0);
	gtk_container_add (GTK_CONTAINER (align), GTK_WIDGET (header->priv->search));
	gtk_box_pack_end (GTK_BOX (header),
			  align, TRUE, TRUE, 0);
}

static void
rb_source_header_finalize (GObject *object)
{
	RBSourceHeader *header;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SOURCE_HEADER (object));

	header = RB_SOURCE_HEADER (object);

	g_return_if_fail (header->priv != NULL);

	g_free (header->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_source_header_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBSourceHeader *header = RB_SOURCE_HEADER (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
		if (header->priv->selected_source != NULL)
		{
			if (header->priv->browser_key)
				eel_gconf_notification_remove (header->priv->browser_notify_id);

			g_signal_handlers_disconnect_by_func (G_OBJECT (header->priv->selected_source),
							      G_CALLBACK (rb_source_header_filter_changed_cb),
							      header);
		}
		
		header->priv->selected_source = g_value_get_object (value);
		rb_debug ("selected source %p", g_value_get_object (value));

		if (header->priv->selected_source != NULL)
		{
			header->priv->browser_key = rb_source_get_browser_key (header->priv->selected_source);
			if (header->priv->browser_key)
				header->priv->browser_notify_id
					= eel_gconf_notification_add (header->priv->browser_key,
								      (GConfClientNotifyFunc) rb_source_header_gconf_disclosure_changed_cb,
								      header);
			g_signal_connect (G_OBJECT (header->priv->selected_source),
					  "filter_changed",
					  G_CALLBACK (rb_source_header_filter_changed_cb),
					  header);
		}
		rb_source_header_sync_control_state (header);
		
		break;
	case PROP_COMPONENT:
		header->priv->component = g_value_get_object (value);
		rb_bonobo_add_listener_list_with_data (header->priv->component,
						       rb_source_header_listeners,
						       header);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_source_header_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBSourceHeader *header = RB_SOURCE_HEADER (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
		g_value_set_object (value, header->priv->selected_source);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_source_header_set_source (RBSourceHeader *header,
			    RBSource *source)
{
	g_return_if_fail (RB_IS_SOURCE_HEADER (header));
	g_return_if_fail (RB_IS_SOURCE (source));

	g_object_set (G_OBJECT (header),
		      "source", source,
		      NULL);
}

RBSourceHeader *
rb_source_header_new (BonoboUIComponent *component)
{
	RBSourceHeader *header = g_object_new (RB_TYPE_SOURCE_HEADER,
					       "component", component,
					       NULL);

	g_return_val_if_fail (header->priv != NULL, NULL);

	return header;
}

static void
rb_source_header_filter_changed_cb (RBSource *source,
				   RBSourceHeader *header)
{
	rb_debug  ("filter changed for %p", source);
	
	rb_search_entry_clear (RB_SEARCH_ENTRY (header->priv->search));
}

static void
rb_source_header_search_cb (RBSearchEntry *search,
			    const char *text,
			    RBSourceHeader *header)
{
	rb_debug  ("searching for \"%s\"", text);
	
	rb_source_search (header->priv->selected_source, text);
}

static void
rb_source_header_disclosure_toggled_cb (GtkToggleButton *disclosure,
					gpointer data)
{
	RBSourceHeader *header = RB_SOURCE_HEADER (data);
	gboolean disclosed = gtk_toggle_button_get_active (disclosure);

	rb_debug ("disclosed: %s", disclosed ? "TRUE" : "FALSE");
	
	if (header->priv->selected_source != NULL)
		eel_gconf_set_boolean (header->priv->browser_key, disclosed);
}

static void
rb_source_header_gconf_disclosure_changed_cb (GConfClient *client,
					      guint cnxn_id,
					      GConfEntry *entry,
					      RBSourceHeader *header)
{
	rb_debug ("gconf disclosure changed");

	g_return_if_fail (header->priv->browser_key != NULL);

	rb_source_header_sync_control_state (header);
}

static void
rb_source_header_view_browser_changed_cb (BonoboUIComponent *component,
					  const char *path,
					  Bonobo_UIComponent_EventType type,
					  const char *state,
					  RBSourceHeader *header)
{
	rb_debug ("got view browser toggle");
	eel_gconf_set_boolean (header->priv->browser_key,
			       rb_bonobo_get_active (component, CMD_PATH_VIEW_BROWSER));
}

static void
rb_source_header_sync_control_state (RBSourceHeader *header)
{
	gboolean have_browser = header->priv->selected_source != NULL
		&& header->priv->browser_key != NULL;
	gtk_widget_set_sensitive (header->priv->disclosure,
				  have_browser);
	rb_bonobo_set_sensitive (header->priv->component, CMD_PATH_VIEW_BROWSER,
				have_browser);
	if (have_browser) {
		gboolean shown = eel_gconf_get_boolean (header->priv->browser_key);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (header->priv->disclosure),
					      shown);
		rb_bonobo_set_active (header->priv->component,
				      CMD_PATH_VIEW_BROWSER,
				      shown);
	}
}
