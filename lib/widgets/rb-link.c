/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#include <gtk/gtklabel.h>
#include <gtk/gtkeventbox.h>
#include <gtk/gtktooltips.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-url.h>
#include <string.h>

#include "rb-link.h"
#include "rb-dialog.h"
#include "rb-string-helpers.h"

static void rb_link_class_init (RBLinkClass *klass);
static void rb_link_init (RBLink *link);
static void rb_link_finalize (GObject *object);
static void rb_link_set_property (GObject *object,
			          guint prop_id,
			          const GValue *value,
			          GParamSpec *pspec);
static void rb_link_get_property (GObject *object,
			          guint prop_id,
			          GValue *value,
			          GParamSpec *pspec);
static gboolean rb_link_button_press_event_cb (GtkWidget *widget,
			                       GdkEventButton *event,
			                       RBLink *link);
static gboolean rb_link_enter_notify_event_cb (GtkWidget *widget,
			                       GdkEventCrossing *event,
			                       RBLink *link);
static gboolean rb_link_leave_notify_event_cb (GtkWidget *widget,
			                       GdkEventCrossing *event,
			                       RBLink *link);
static void rb_link_set_text (RBLink *link,
			      GdkColor *color);

struct RBLinkPrivate
{
	GtkWidget *label;

	char *text;
	char *url;

	GtkTooltips *tooltips;

	GdkColor *normal_color;
	GdkColor *prelight_color;
};

enum
{
	PROP_0,
	PROP_TEXT,
	PROP_URL
};

static GObjectClass *parent_class = NULL;

GType
rb_link_get_type (void)
{
	static GType rb_link_type = 0;

	if (rb_link_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLinkClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_link_class_init,
			NULL,
			NULL,
			sizeof (RBLink),
			0,
			(GInstanceInitFunc) rb_link_init
		};

		rb_link_type = g_type_register_static (GTK_TYPE_EVENT_BOX,
						       "RBLink",
						       &our_info, 0);
	}

	return rb_link_type;
}

static void
rb_link_class_init (RBLinkClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_link_finalize;

	object_class->set_property = rb_link_set_property;
	object_class->get_property = rb_link_get_property;

	g_object_class_install_property (object_class,
					 PROP_TEXT,
					 g_param_spec_string ("text",
							      "Link text",
							      "Link text",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_URL,
					 g_param_spec_string ("url",
							      "Link URL",
							      "Link URL",
							      NULL,
							      G_PARAM_READWRITE));
}

static void
rb_link_init (RBLink *link)
{
	GtkStyle *rcstyle;

	link->priv = g_new0 (RBLinkPrivate, 1);

	link->priv->label = gtk_label_new ("");
	gtk_label_set_use_markup (GTK_LABEL (link->priv->label), TRUE);

	gtk_container_add (GTK_CONTAINER (link), link->priv->label);

	link->priv->tooltips = gtk_tooltips_new ();

	g_signal_connect (G_OBJECT (link),
			  "button_press_event",
			  G_CALLBACK (rb_link_button_press_event_cb),
			  link);
	g_signal_connect (G_OBJECT (link),
			  "enter_notify_event",
			  G_CALLBACK (rb_link_enter_notify_event_cb),
			  link);
	g_signal_connect (G_OBJECT (link),
			  "leave_notify_event",
			  G_CALLBACK (rb_link_leave_notify_event_cb),
			  link);

	rcstyle = gtk_rc_get_style (GTK_WIDGET (link));
	if (rcstyle == NULL)
	{
		rcstyle = gtk_style_new ();
	}
	else
	{
		g_object_ref (G_OBJECT (rcstyle));
	}
	
	link->priv->normal_color = gdk_color_copy (&(rcstyle->fg[GTK_STATE_NORMAL]));
	link->priv->prelight_color = gdk_color_copy (&(rcstyle->bg[GTK_STATE_SELECTED]));

	g_object_unref (G_OBJECT (rcstyle));
}

static void
rb_link_finalize (GObject *object)
{
	RBLink *link;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LINK (object));

	link = RB_LINK (object);

	g_return_if_fail (link->priv != NULL);

	g_free (link->priv->text);
	g_free (link->priv->url);

	gdk_color_free (link->priv->normal_color);
	gdk_color_free (link->priv->prelight_color);

	g_free (link->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_link_set_property (GObject *object,
		      guint prop_id,
		      const GValue *value,
		      GParamSpec *pspec)
{
	RBLink *link = RB_LINK (object);

	switch (prop_id)
	{
	case PROP_TEXT:
		g_free (link->priv->text);
		link->priv->text = g_strdup (g_value_get_string (value));
		rb_link_set_text (link, link->priv->normal_color);
		break;
	case PROP_URL:
		{
			char *tooltip;
			
			g_free (link->priv->url);
			link->priv->url = g_strdup (g_value_get_string (value));
			
			tooltip = g_strdup_printf (_("Go to %s"), link->priv->url);
			gtk_tooltips_set_tip (link->priv->tooltips,
					      GTK_WIDGET (link),
					      tooltip,
					      NULL);
			g_free (tooltip);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_link_get_property (GObject *object,
		      guint prop_id,
		      GValue *value,
		      GParamSpec *pspec)
{
	RBLink *link = RB_LINK (object);

	switch (prop_id)
	{
	case PROP_TEXT:
		g_value_set_string (value, link->priv->text);
		break;
	case PROP_URL:
		g_value_set_string (value, link->priv->url);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBLink *
rb_link_new (void)
{
	RBLink *link;

	link = RB_LINK (g_object_new (RB_TYPE_LINK, NULL));

	g_return_val_if_fail (link->priv != NULL, NULL);

	return link;
}

void
rb_link_set (RBLink *link,
	     const char *text,
	     const char *url)
{
	g_return_if_fail (RB_IS_LINK (link));
	g_return_if_fail (text != NULL);
	g_return_if_fail (url != NULL);

	g_object_set (G_OBJECT (link),
		      "text", text,
		      "url", url,
		      NULL);
}

static gboolean
rb_link_button_press_event_cb (GtkWidget *widget,
			       GdkEventButton *event,
			       RBLink *link)
{
	GError *error = NULL;
	
	if (event->button != 1)
		return TRUE;

	gnome_url_show (link->priv->url, &error);
	if (error != NULL)
	{
		rb_error_dialog (_("There was an error going to %s:\n%s"),
				 link->priv->url,
				 error->message);
		g_error_free (error);
	}
	
	return FALSE;
}

static gboolean
rb_link_enter_notify_event_cb (GtkWidget *widget,
			       GdkEventCrossing *event,
			       RBLink *link)
{
	rb_link_set_text (link, link->priv->prelight_color);

	return TRUE;
}

static gboolean
rb_link_leave_notify_event_cb (GtkWidget *widget,
			       GdkEventCrossing *event,
			       RBLink *link)
{
	rb_link_set_text (link, link->priv->normal_color);

	return TRUE;
}

static void
rb_link_set_text (RBLink *link,
		  GdkColor *color)
{
	char *text, *escaped, *compressed;
	
	compressed = rb_string_compress (link->priv->text, 50);
	escaped = g_markup_escape_text (compressed, g_utf8_strlen (compressed, -1));
	g_free (compressed);
	text = g_strdup_printf ("<span foreground=\"#%02X%02X%02X\" underline=\"single\">%s</span>",
				color->red, color->green, color->blue, escaped);
	g_free (escaped);
	gtk_label_set_markup (GTK_LABEL (link->priv->label), text);
	g_free (text);
}
