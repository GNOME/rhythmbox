/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <cwalters@gnome.org>
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

#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-ui-container.h>
#include <bonobo/bonobo-ui-util.h>
#include <libgnome/gnome-i18n.h>

#include "rb-source.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-bonobo-helpers.h"

static void rb_source_class_init (RBSourceClass *klass);
static void rb_source_init (RBSource *source);
static void rb_source_finalize (GObject *object);
static void rb_source_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rb_source_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);

const char * default_get_browser_key (RBSource *status);
GList *default_get_extra_views (RBSource *source);
void default_song_properties (RBSource *source);
GtkWidget * default_get_config_widget (RBSource *source);
RBSourceEOFType default_handle_eos (RBSource *source);
void default_buffering_done  (RBSource *source);

struct RBSourcePrivate
{
	char *ui_file;
	char *ui_name;
	char *config_name;

	BonoboUIContainer *container;
	BonoboUIComponent *component;
};

enum
{
	PROP_0,
	PROP_UI_FILE,
	PROP_UI_NAME,
	PROP_CONFIG_NAME,
	PROP_CONTAINER,
};

enum
{
	STATUS_CHANGED,
	FILTER_CHANGED,
	LAST_SIGNAL
};

static guint rb_source_signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;

GType
rb_source_get_type (void)
{
	static GType rb_source_type = 0;

	if (rb_source_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBSourceClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_source_class_init,
			NULL,
			NULL,
			sizeof (RBSource),
			0,
			(GInstanceInitFunc) rb_source_init
		};

		rb_source_type = g_type_register_static (GTK_TYPE_HBOX,
						       "RBSource",
						       &our_info, 0);
	}

	return rb_source_type;
}

static void
rb_source_class_init (RBSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_source_finalize;

	object_class->set_property = rb_source_set_property;
	object_class->get_property = rb_source_get_property;

	klass->impl_get_browser_key = default_get_browser_key;
	klass->impl_get_extra_views = default_get_extra_views;
	klass->impl_song_properties = default_song_properties;
	klass->impl_handle_eos = default_handle_eos;
	klass->impl_buffering_done = default_buffering_done;
	klass->impl_get_config_widget = default_get_config_widget;

	g_object_class_install_property (object_class,
					 PROP_UI_FILE,
					 g_param_spec_string ("ui-file",
							      "UI file",
							      "Bonobo UI file",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_UI_NAME,
					 g_param_spec_string ("ui-name",
							      "UI name",
							      "Bonobo UI name",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_CONFIG_NAME,
					 g_param_spec_string ("config-name",
							      "Config name",
							      "Name for configuration dialog",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_CONTAINER,
					 g_param_spec_object ("container",
							      "BonoboUIContainer",
							      "BonoboUIContainer object",
							      BONOBO_TYPE_UI_CONTAINER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	rb_source_signals[STATUS_CHANGED] =
		g_signal_new ("status_changed",
			      RB_TYPE_SOURCE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceClass, status_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	rb_source_signals[FILTER_CHANGED] =
		g_signal_new ("filter_changed",
			      RB_TYPE_SOURCE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceClass, filter_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static void
rb_source_init (RBSource *source)
{
	source->priv = g_new0 (RBSourcePrivate, 1);

}

static void
rb_source_finalize (GObject *object)
{
	RBSource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SOURCE (object));

	source = RB_SOURCE (object);

	g_return_if_fail (source->priv != NULL);

	rb_debug ("Finalizing view %p", source);

	g_free (source->priv->ui_file);
	g_free (source->priv->ui_name);
	g_free (source->priv->config_name);

	g_free (source->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_source_set_property (GObject *object,
		      guint prop_id,
		      const GValue *value,
		      GParamSpec *pspec)
{
	RBSource *source = RB_SOURCE (object);

	switch (prop_id)
	{
	case PROP_UI_FILE:
		source->priv->ui_file = g_strdup (g_value_get_string (value));
		break;
	case PROP_UI_NAME:
		source->priv->ui_name = g_strdup (g_value_get_string (value));
		break;
	case PROP_CONFIG_NAME:
		source->priv->config_name = g_strdup (g_value_get_string (value));
		break;
	case PROP_CONTAINER:
		source->priv->container = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_source_get_property (GObject *object,
		      guint prop_id,
		      GValue *value,
		      GParamSpec *pspec)
{
	RBSource *source = RB_SOURCE (object);

	switch (prop_id)
	{
	case PROP_UI_FILE:
		g_value_set_string (value, source->priv->ui_file);
		break;
	case PROP_UI_NAME:
		g_value_set_string (value, source->priv->ui_name);
		break;
	case PROP_CONFIG_NAME:
		if (source->priv->config_name)
			g_value_set_string (value, source->priv->config_name);
		else
			g_value_set_string (value, source->priv->ui_name);
		break;
	case PROP_CONTAINER:
		g_value_set_object (value, source->priv->container);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}


const char *
rb_source_get_status (RBSource *status)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (status);

	return klass->impl_get_status (status);
}

const char *
default_get_browser_key (RBSource *status)
{
	return NULL;
}

const char *
rb_source_get_browser_key (RBSource *status)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (status);

	return klass->impl_get_browser_key (status);
}

void
rb_source_notify_status_changed (RBSource *status)
{
	g_signal_emit (G_OBJECT (status), rb_source_signals[STATUS_CHANGED], 0);
}

void
rb_source_notify_filter_changed (RBSource *status)
{
	g_signal_emit (G_OBJECT (status), rb_source_signals[FILTER_CHANGED], 0);
}

RBNodeView *
rb_source_get_node_view (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_node_view (source);
}

GList *
default_get_extra_views (RBSource *source)
{
	return NULL;
}

GList *
rb_source_get_extra_views (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_extra_views (source);
}

const char *
rb_source_get_description (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_description (source);
}

GdkPixbuf *
rb_source_get_pixbuf (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_pixbuf (source);
}

void
rb_source_search (RBSource *source, const char *text)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_search (source, text);
}

GtkWidget *
default_get_config_widget (RBSource *source)
{
	return NULL;
}

GtkWidget *
rb_source_get_config_widget (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_config_widget (source);
}

void
default_song_properties (RBSource *source)
{
	rb_error_dialog (_("No properties available."));
}

void
rb_source_song_properties (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_song_properties (source);
}

gboolean
rb_source_can_pause (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_can_pause (source);
}

RBSourceEOFType
default_handle_eos (RBSource *source)
{
	return RB_SOURCE_EOF_NEXT;
}

RBSourceEOFType
rb_source_handle_eos (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_handle_eos (source);
}

gboolean
rb_source_have_artist_album (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_have_artist_album (source);
}

const char *
rb_source_get_artist (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_artist (source);
}

const char *
rb_source_get_album (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_album (source);
}

gboolean
rb_source_have_url (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_have_url (source);
}

void
default_buffering_done  (RBSource *source)
{
	rb_debug ("No implementation of buffering_done for active source");
}
	

void
rb_source_buffering_done (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_buffering_done (source);
}
