/* -*- Mode: C; tab-width: 2; indent-tabs-mode: t; c-basic-offset: 2 -*- */
/* 
 * Copyright (C) 2004 Carlos Garnacho
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: Carlos Garnacho Parro <carlosg@gnome.org>
 */

#include <config.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkiconfactory.h>
#include <gtk/gtkalignment.h>
#include "gst-hig-dialog.h"
#include <string.h>

struct _GstHigDialogPrivate {
  GtkWidget *image;
  GtkWidget *primary_label;
	GtkWidget *secondary_label;

	GtkWidget *extra_align;
	GtkWidget *extra_widget;
};

static void gst_hig_dialog_class_init   (GstHigDialogClass *klass);
static void gst_hig_dialog_init         (GstHigDialog      *dialog);
static void gst_hig_dialog_style_set    (GtkWidget         *widget,
																				 GtkStyle          *prev_style);

static void gst_hig_dialog_set_property (GObject         *object,
																				 guint            prop_id,
																				 const GValue    *value,
																				 GParamSpec      *pspec);
static void gst_hig_dialog_get_property (GObject         *object,
																				 guint            prop_id,
																				 GValue          *value,
																				 GParamSpec      *pspec);

enum {
  PROP_0,
  PROP_MESSAGE_TYPE,
  PROP_EXTRA_WIDGET
};

static gpointer parent_class;

GType
gst_hig_message_type_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_HIG_MESSAGE_INFO, "GST_HIG_MESSAGE_INFO", "info" },
      { GST_HIG_MESSAGE_WARNING, "GST_HIG_MESSAGE_WARNING", "warning" },
      { GST_HIG_MESSAGE_QUESTION, "GST_HIG_MESSAGE_QUESTION", "question" },
      { GST_HIG_MESSAGE_ERROR, "GST_HIG_MESSAGE_ERROR", "error" },
      { GST_HIG_MESSAGE_AUTHENTICATION, "GST_HIG_MESSAGE_AUTHENTICATION", "authentication" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstHigMessageType", values);
  }
  return etype;
}

GType
gst_hig_dialog_get_type (void)
{
	static GType dialog_type = 0;

  if (!dialog_type)
	  {
      static const GTypeInfo dialog_info =
        {
					sizeof (GstHigDialogClass),
					NULL,		/* base_init */
					NULL,		/* base_finalize */
					(GClassInitFunc) gst_hig_dialog_class_init,
					NULL,		/* class_finalize */
					NULL,		/* class_data */
					sizeof (GstHigDialog),
					0,		/* n_preallocs */
					(GInstanceInitFunc) gst_hig_dialog_init,
				};

      dialog_type = g_type_register_static (GTK_TYPE_DIALOG, "GstHigDialog",
																						&dialog_info, 0);
    }

  return dialog_type;
}

static void
gst_hig_dialog_class_init (GstHigDialogClass *class)
{
  GtkWidgetClass *widget_class;
  GObjectClass *gobject_class;

  widget_class = GTK_WIDGET_CLASS (class);
  gobject_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);
  
  widget_class->style_set = gst_hig_dialog_style_set;

  gobject_class->set_property = gst_hig_dialog_set_property;
  gobject_class->get_property = gst_hig_dialog_get_property;
  
  gtk_widget_class_install_style_property (widget_class,
																					 g_param_spec_int ("message_border",
                                                             "Image/label border",
                                                             "Width of border around the label and image in the message dialog",
                                                             0,
                                                             G_MAXINT,
                                                             12,
                                                             G_PARAM_READABLE));
  gtk_widget_class_install_style_property (widget_class,
																					 g_param_spec_boolean ("use_separator",
																																 "Use separator",
																																 "Whether to put a separator between the message dialog's text and the buttons",
																																 FALSE,
																																 G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
                                   PROP_MESSAGE_TYPE,
                                   g_param_spec_enum ("message_type",
																											"Message Type",
																											"The type of message",
																											GST_TYPE_HIG_MESSAGE_TYPE,
                                                      GST_HIG_MESSAGE_INFO,
                                                      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT));
	g_object_class_install_property (gobject_class,
																	 PROP_EXTRA_WIDGET,
																	 g_param_spec_object ("extra_widget",
																												"Extra widget",
																												"Widget for extra options",
																												GTK_TYPE_WIDGET,
																												G_PARAM_READWRITE));
}

static void
gst_hig_dialog_init (GstHigDialog *dialog)
{
  GtkWidget *hbox, *vbox;

	dialog->_priv = g_new0 (GstHigDialogPrivate, 1);

  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  
  dialog->_priv->primary_label   = gtk_label_new (NULL);
	dialog->_priv->secondary_label = gtk_label_new (NULL);
	dialog->_priv->extra_align     = gtk_alignment_new (0.0, 0.0, 1.0, 1.0);
  dialog->_priv->image = gtk_image_new_from_stock (NULL, GTK_ICON_SIZE_DIALOG);
  gtk_misc_set_alignment (GTK_MISC (dialog->_priv->image), 0.5, 0.0);
  
  gtk_label_set_line_wrap  (GTK_LABEL (dialog->_priv->primary_label), TRUE);
  gtk_label_set_selectable (GTK_LABEL (dialog->_priv->primary_label), TRUE);
	gtk_label_set_use_markup (GTK_LABEL (dialog->_priv->primary_label), TRUE);
	gtk_misc_set_alignment   (GTK_MISC  (dialog->_priv->primary_label), 0.0, 0.0);
  
  gtk_label_set_line_wrap  (GTK_LABEL (dialog->_priv->secondary_label), TRUE);
  gtk_label_set_selectable (GTK_LABEL (dialog->_priv->secondary_label), TRUE);
	gtk_label_set_use_markup (GTK_LABEL (dialog->_priv->secondary_label), TRUE);
	gtk_misc_set_alignment   (GTK_MISC  (dialog->_priv->secondary_label), 0.0, 0.0);

  hbox = gtk_hbox_new (FALSE, 12);
	vbox = gtk_vbox_new (FALSE, 12);

  gtk_box_pack_start (GTK_BOX (vbox), dialog->_priv->primary_label,
                      FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (vbox), dialog->_priv->secondary_label,
											TRUE, TRUE, 0);

	gtk_box_pack_end   (GTK_BOX (vbox), dialog->_priv->extra_align,
											FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (hbox), dialog->_priv->image,
                      FALSE, FALSE, 0);

	gtk_box_pack_start (GTK_BOX (hbox), vbox,
											TRUE, TRUE, 0);
											
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
                      hbox,
                      FALSE, FALSE, 0);

  gtk_widget_show_all (hbox);
	gtk_widget_hide     (dialog->_priv->extra_align);

	gtk_window_set_title (GTK_WINDOW (dialog), "");
}

static GstHigMessageType
gst_hig_dialog_get_message_type (GstHigDialog *dialog)
{
  const gchar* stock_id = NULL;

  g_return_val_if_fail (GST_IS_HIG_DIALOG (dialog), GST_HIG_MESSAGE_INFO);
  g_return_val_if_fail (GTK_IS_IMAGE(dialog->_priv->image), GST_HIG_MESSAGE_INFO);

  stock_id = GTK_IMAGE(dialog->_priv->image)->data.stock.stock_id;

  /* Look at the stock id of the image to guess the
   * GstHigMessageType value that was used to choose it
   * in setup_type()
   */
  if (strcmp (stock_id, GTK_STOCK_DIALOG_INFO) == 0)
    return GST_HIG_MESSAGE_INFO;
  else if (strcmp (stock_id, GTK_STOCK_DIALOG_QUESTION) == 0)
    return GST_HIG_MESSAGE_QUESTION;
  else if (strcmp (stock_id, GTK_STOCK_DIALOG_WARNING) == 0)
    return GST_HIG_MESSAGE_WARNING;
  else if (strcmp (stock_id, GTK_STOCK_DIALOG_ERROR) == 0)
    return GST_HIG_MESSAGE_ERROR;
	else if (strcmp (stock_id, GTK_STOCK_DIALOG_AUTHENTICATION) == 0)
		return GST_HIG_MESSAGE_AUTHENTICATION;
  else
	  {
      g_assert_not_reached (); 
      return GST_HIG_MESSAGE_INFO;
    }
}

static void
setup_type (GstHigDialog      *dialog,
						GstHigMessageType  type)
{
  const gchar *stock_id = NULL;
  
  switch (type)
    {
    case GST_HIG_MESSAGE_INFO:
      stock_id = GTK_STOCK_DIALOG_INFO;
      break;

    case GST_HIG_MESSAGE_QUESTION:
      stock_id = GTK_STOCK_DIALOG_QUESTION;
      break;

    case GST_HIG_MESSAGE_WARNING:
      stock_id = GTK_STOCK_DIALOG_WARNING;
      break;
      
    case GST_HIG_MESSAGE_ERROR:
      stock_id = GTK_STOCK_DIALOG_ERROR;
      break;
		case GST_HIG_MESSAGE_AUTHENTICATION:
			stock_id = GTK_STOCK_DIALOG_AUTHENTICATION;
			break;
    default:
      g_warning ("Unknown GstHigMessageType %d", type);
      break;
    }

  if (stock_id == NULL)
    stock_id = GTK_STOCK_DIALOG_INFO;

	gtk_image_set_from_stock (GTK_IMAGE (dialog->_priv->image), stock_id,
														GTK_ICON_SIZE_DIALOG);
}

static void
setup_extra_widget (GstHigDialog *dialog,
										GtkWidget    *extra_widget)
{
	GtkWidget *align, *child;

	align = dialog->_priv->extra_align;
	child = GTK_BIN (align)->child;

	dialog->_priv->extra_widget = extra_widget;

	if (child)
		gtk_container_remove (GTK_CONTAINER (align), child);

	if (extra_widget)
	  {
			gtk_container_add (GTK_CONTAINER (align), extra_widget);
			gtk_widget_show (dialog->_priv->extra_align);
			gtk_widget_show (dialog->_priv->extra_widget);
		}
	else
	  {
			gtk_widget_hide (dialog->_priv->extra_align);
		}
}

static void 
gst_hig_dialog_set_property (GObject      *object,
														 guint         prop_id,
														 const GValue *value,
														 GParamSpec   *pspec)
{
  GstHigDialog *dialog;
  
  dialog = GST_HIG_DIALOG (object);

  switch (prop_id)
    {
    case PROP_MESSAGE_TYPE:
      setup_type (dialog, g_value_get_enum (value));
      break;
		case PROP_EXTRA_WIDGET:
			setup_extra_widget (dialog, g_value_get_object (value));
			break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void 
gst_hig_dialog_get_property (GObject     *object,
														 guint        prop_id,
														 GValue      *value,
														 GParamSpec  *pspec)
{
  GstHigDialog *dialog;
  
  dialog = GST_HIG_DIALOG (object);
  
  switch (prop_id)
    {
    case PROP_MESSAGE_TYPE:
      g_value_set_enum (value, gst_hig_dialog_get_message_type (dialog));
      break;
		case PROP_EXTRA_WIDGET:
			g_value_set_object (value, dialog->_priv->extra_widget);
			break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static GtkWidget*
gst_hig_dialog_new_va (GtkWindow         *parent,
											 GtkDialogFlags     flags,
											 GstHigMessageType  type,
											 const gchar       *primary_text,
											 const gchar       *secondary_text,
											 const gchar       *first_button_text,
											 va_list            args)
{
  GtkWidget *widget;
  GtkDialog *dialog;
  gchar *msg = NULL;
	gint  response_id;
	const gchar *button_text = first_button_text;

  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), NULL);

  widget = g_object_new (GST_TYPE_HIG_DIALOG,
												 "message_type", type,
												 "extra_widget", NULL,
												 NULL);
  dialog = GTK_DIALOG (widget);

	while (button_text)
	  {
			response_id = va_arg (args, gint);
			gtk_dialog_add_button (dialog, button_text, response_id);
			button_text = va_arg (args, const gchar*);
		}

	if (primary_text)
	  {
			gtk_widget_show (GST_HIG_DIALOG (widget)->_priv->primary_label);

			msg = g_strdup_printf ("<span size=\"larger\" weight=\"bold\">%s</span>", primary_text);

			gtk_label_set_markup (GTK_LABEL (GST_HIG_DIALOG (widget)->_priv->primary_label),
														msg);
			g_free (msg);
		}
	else
		gtk_widget_hide (GST_HIG_DIALOG (widget)->_priv->primary_label);

  if (secondary_text)
    {
			gtk_widget_show (GST_HIG_DIALOG (widget)->_priv->secondary_label);
      gtk_label_set_markup (GTK_LABEL (GST_HIG_DIALOG (widget)->_priv->secondary_label),
														secondary_text);
    }
	else
		gtk_widget_hide (GST_HIG_DIALOG (widget)->_priv->secondary_label);

  if (parent != NULL)
    gtk_window_set_transient_for (GTK_WINDOW (widget),
                                  GTK_WINDOW (parent));
  
  if (flags & GTK_DIALOG_MODAL)
    gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

  if (flags & GTK_DIALOG_DESTROY_WITH_PARENT)
    gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);

  return widget;
}

GtkWidget*
gst_hig_dialog_new (GtkWindow         *parent,
										GtkDialogFlags     flags,
										GstHigMessageType  type,
										const gchar       *primary_text,
										const gchar       *secondary_text,
										const gchar       *first_button_text,
										...)
{
	GtkWidget *result;
  va_list args;

	va_start (args, first_button_text);
	result = gst_hig_dialog_new_va (parent, flags, type,
																	primary_text, secondary_text,
																	first_button_text, args);
	va_end (args);

	return result;
}

static void
gst_hig_dialog_set_text (GtkLabel    *label,
													const gchar *message_format,
													va_list      args)
{
	gchar *msg;

	msg = g_markup_vprintf_escaped (message_format, args);
	gtk_label_set_markup (label, msg);

	g_free (msg);
}

void
gst_hig_dialog_set_primary_text (GstHigDialog *dialog,
																 const gchar  *message_format,
																 ...)
{
	va_list  args;
	gchar   *msg;
	
	g_return_if_fail (GST_IS_HIG_DIALOG (dialog));

	if (message_format)
	  {
			gtk_widget_show (dialog->_priv->primary_label);
			
			msg = g_strdup_printf ("<span size=\"larger\" weight=\"bold\">%s</span>", message_format);

			va_start (args, message_format);
			gst_hig_dialog_set_text (GTK_LABEL (dialog->_priv->primary_label), msg, args);
			va_end (args);

			g_free (msg);
		}
	else
		gtk_widget_hide (dialog->_priv->primary_label);
}

void
gst_hig_dialog_set_secondary_text (GstHigDialog *dialog,
																	 const gchar *message_format,
																	 ...)
{
	va_list  args;

	g_return_if_fail (GST_IS_HIG_DIALOG (dialog));

	if (message_format)
	  {
			gtk_widget_show (dialog->_priv->secondary_label);
			
			va_start (args, message_format);
			gst_hig_dialog_set_text (GTK_LABEL (dialog->_priv->secondary_label), message_format, args);
			va_end (args);
		}
	else
		gtk_widget_hide (dialog->_priv->secondary_label);
}

void
gst_hig_dialog_set_extra_widget (GstHigDialog *dialog,
																 GtkWidget *extra_widget)
{
	g_return_if_fail (GST_IS_HIG_DIALOG (dialog));

	g_object_set (G_OBJECT (dialog), "extra_widget", extra_widget, NULL);
}

static void
gst_hig_dialog_style_set (GtkWidget *widget,
													GtkStyle  *prev_style)
{
  GtkWidget *parent;
  gint border_width = 0;
  gboolean use_separator;

  parent = GTK_WIDGET (GST_HIG_DIALOG (widget)->_priv->image->parent);

  if (parent)
    {
      gtk_widget_style_get (widget, "message_border",
                            &border_width, NULL);
      
      gtk_container_set_border_width (GTK_CONTAINER (parent),
                                      border_width);
    }

  gtk_widget_style_get (widget,
												"use_separator", &use_separator,
												NULL);
  gtk_dialog_set_has_separator (GTK_DIALOG (widget), use_separator);

  if (GTK_WIDGET_CLASS (parent_class)->style_set)
    (GTK_WIDGET_CLASS (parent_class)->style_set) (widget, prev_style);
}
