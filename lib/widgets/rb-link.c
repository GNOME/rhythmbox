/*
 *  arch-tag: Implementation of GtkLabel subclass that acts as a hyperlink
 *
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

#include <gtk/gtkeventbox.h>
#include <gtk/gtktooltips.h>
#include <gtk/gtklabel.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-url.h>
#include <string.h>

#include "rb-link.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rb-ellipsizing-label.h"

typedef enum
{
	RB_LINK_NORMAL,
	RB_LINK_PRELIGHT
} RBLinkColor;

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
static void rb_link_set_color (RBLink *link,
			       RBLinkColor color);

struct RBLinkPrivate
{
	GtkWidget *label;

	char *text;
	char *tooltip;
	char *url;
	gboolean active;

	GtkTooltips *tooltips;
};

enum
{
	PROP_0,
	PROP_TEXT,
	PROP_TOOLTIP,
	PROP_URL,
	PROP_ACTIVE
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
					 PROP_TOOLTIP,
					 g_param_spec_string ("tooltip",
							      "Link tooltip",
							      "Link tooltip",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_URL,
					 g_param_spec_string ("url",
							      "Link URL",
							      "Link URL",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_ACTIVE,
					 g_param_spec_boolean ("active",
							       "Active",
							       "Whether or not the link is active",
							       TRUE,
							       G_PARAM_READWRITE));
}

static void
rb_link_init (RBLink *link)
{
	link->priv = g_new0 (RBLinkPrivate, 1);

#ifdef RB_LINK_USE_ELLIPSIZING_LABEL
	link->priv->label = rb_ellipsizing_label_new ("");
	rb_ellipsizing_label_set_mode (RB_ELLIPSIZING_LABEL (link->priv->label), RB_ELLIPSIZE_END);
#else
	link->priv->label = gtk_label_new ("");
#endif
 	gtk_label_set_use_markup (GTK_LABEL (link->priv->label), FALSE);
 	gtk_label_set_selectable (GTK_LABEL (link->priv->label), FALSE);	
	gtk_misc_set_alignment (GTK_MISC (link->priv->label), 0.0, 0.5);

	gtk_container_add (GTK_CONTAINER (link), link->priv->label);

	link->priv->active = TRUE;

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

	rb_link_set_color (link, RB_LINK_NORMAL);
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
#ifdef RB_LINK_USE_ELLIPSIZING_LABEL
		rb_ellipsizing_label_set_text (RB_ELLIPSIZING_LABEL (link->priv->label),
					       link->priv->text);
#else
		gtk_label_set_text (GTK_LABEL (link->priv->label),
				    link->priv->text);
#endif
		
		break;
	case PROP_TOOLTIP:
		g_free (link->priv->tooltip);
		link->priv->tooltip = g_strdup (g_value_get_string (value));
		gtk_tooltips_set_tip (link->priv->tooltips,
				      GTK_WIDGET (link),
				      link->priv->tooltip,
				      NULL);
		break;
	case PROP_URL:
		{
			g_free (link->priv->url);
			link->priv->url = g_strdup (g_value_get_string (value));
		}
		break;
	case PROP_ACTIVE:
		link->priv->active = g_value_get_boolean (value);
		if (link->priv->active)
			gtk_tooltips_enable (link->priv->tooltips);
		else
			gtk_tooltips_disable (link->priv->tooltips);
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
	case PROP_TOOLTIP:
		g_value_set_string (value, link->priv->tooltip);
		break;
	case PROP_URL:
		g_value_set_string (value, link->priv->url);
		break;
	case PROP_ACTIVE:
		g_value_set_boolean (value, link->priv->active);
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
	     const char *tooltip,
	     const char *url)
{
	g_return_if_fail (RB_IS_LINK (link));
	g_return_if_fail (text != NULL);
	g_return_if_fail (tooltip != NULL);

	if (url != NULL)
		g_object_set (G_OBJECT (link),
			      "text", text,
			      "tooltip", tooltip,
			      "url", url,
			      "active", (gboolean) TRUE,
			      NULL);

	else
		g_object_set (G_OBJECT (link),
			      "text", text,
			      "tooltip", tooltip,
			      "active", (gboolean) FALSE,
			      NULL);
}

static gboolean
rb_link_button_press_event_cb (GtkWidget *widget,
			       GdkEventButton *event,
			       RBLink *link)
{
	GError *error = NULL;

	if (event->button != 1 || !link->priv->active)
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
	GdkCursor *cursor;

	rb_debug ("enter notify");

	if (!link->priv->active)
		return TRUE;

	rb_link_set_color (link, RB_LINK_PRELIGHT);

	cursor = gdk_cursor_new (GDK_HAND2);
	gdk_window_set_cursor (GTK_WIDGET (link)->window, cursor);
	gdk_cursor_unref (cursor);

	return TRUE;
}

static gboolean
rb_link_leave_notify_event_cb (GtkWidget *widget,
			       GdkEventCrossing *event,
			       RBLink *link)
{
	rb_debug ("leave notify");

	if (!link->priv->active)
		return TRUE;

	rb_link_set_color (link, RB_LINK_NORMAL);

	gdk_window_set_cursor (GTK_WIDGET (link)->window, NULL);

	return TRUE;
}

static void
rb_link_set_color (RBLink *link,
		   RBLinkColor color)
{
	PangoAttrList *pattrlist;
	PangoAttribute *attr;

	if (color == RB_LINK_NORMAL) {
		pattrlist = pango_attr_list_new ();
		attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
		attr->start_index = 0;
		attr->end_index = G_MAXINT;
		pango_attr_list_insert (pattrlist, attr);
		gtk_label_set_attributes (GTK_LABEL (link->priv->label), pattrlist);
		pango_attr_list_unref (pattrlist);
	} else {
		GtkStyle *rcstyle;
		GdkColor *gdkcolor;

		rcstyle = gtk_rc_get_style (GTK_WIDGET (link));
		if (rcstyle == NULL) {
			rcstyle = gtk_style_new ();
		} else {
			g_object_ref (G_OBJECT (rcstyle));
		}

		gdkcolor = &rcstyle->bg[GTK_STATE_SELECTED];

		pattrlist = pango_attr_list_new ();
		attr = pango_attr_foreground_new (gdkcolor->red,
						  gdkcolor->green,
						  gdkcolor->blue);
		attr->start_index = 0;
		attr->end_index = G_MAXINT;
		pango_attr_list_insert (pattrlist, attr);
		attr = pango_attr_underline_new (PANGO_UNDERLINE_SINGLE);
		attr->start_index = 0;
		attr->end_index = G_MAXINT;
		pango_attr_list_insert (pattrlist, attr);
		attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
		attr->start_index = 0;
		attr->end_index = G_MAXINT;
		pango_attr_list_insert (pattrlist, attr);
		gtk_label_set_attributes (GTK_LABEL (link->priv->label),
					  pattrlist);
		pango_attr_list_unref (pattrlist);

		g_object_unref (G_OBJECT (rcstyle));
	}
}
