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
#include <gtk/gtkalignment.h>

#include "rb-shell-status.h"

static void rb_shell_status_class_init (RBShellStatusClass *klass);
static void rb_shell_status_init (RBShellStatus *shell_status);
static void rb_shell_status_finalize (GObject *object);
static void rb_shell_status_set_property (GObject *object,
					  guint prop_id,
					  const GValue *value,
					  GParamSpec *pspec);
static void rb_shell_status_get_property (GObject *object,
					  guint prop_id,
					  GValue *value,
					  GParamSpec *pspec);
static void rb_shell_status_add_hint_cb (BonoboUIEngine *engine,
			                 const char *hint,
			                 RBShellStatus *status);
static void rb_shell_status_remove_hint_cb (BonoboUIEngine *engine,
			                    RBShellStatus *status);
static void rb_shell_status_sync (RBShellStatus *status);
static void rb_view_status_changed_cb (RBViewStatus *status,
			               RBShellStatus *shell_status);

struct RBShellStatusPrivate
{
	RBViewStatus *status;

	BonoboUIEngine *engine;

	GtkWidget *temp_label;
	GtkWidget *temp_frame;
	GtkWidget *base_label;

	gboolean temp_shown;
};

enum
{
	PROP_0,
	PROP_STATUS,
	PROP_ENGINE
};

static GObjectClass *parent_class = NULL;

GType
rb_shell_status_get_type (void)
{
	static GType rb_shell_status_type = 0;

	if (rb_shell_status_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBShellStatusClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_shell_status_class_init,
			NULL,
			NULL,
			sizeof (RBShellStatus),
			0,
			(GInstanceInitFunc) rb_shell_status_init
		};

		rb_shell_status_type = g_type_register_static (GTK_TYPE_HBOX,
							       "RBShellStatus",
							       &our_info, 0);
	}

	return rb_shell_status_type;
}

static void
rb_shell_status_class_init (RBShellStatusClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_shell_status_finalize;

	object_class->set_property = rb_shell_status_set_property;
	object_class->get_property = rb_shell_status_get_property;

	g_object_class_install_property (object_class,
					 PROP_STATUS,
					 g_param_spec_object ("status",
							      "RBViewStatus",
							      "RBViewStatus object",
							      RB_TYPE_VIEW_STATUS,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_ENGINE,
					 g_param_spec_object ("engine",
							      "BonoboUIEngine",
							      "BonoboUIEngine object",
							      BONOBO_TYPE_UI_ENGINE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_shell_status_init (RBShellStatus *shell_status)
{
	shell_status->priv = g_new0 (RBShellStatusPrivate, 1);

	shell_status->priv->temp_frame = gtk_alignment_new (0.0, 0.5, 0.0, 1.0);
	shell_status->priv->temp_label = gtk_label_new ("");
	gtk_container_add (GTK_CONTAINER (shell_status->priv->temp_frame),
			   shell_status->priv->temp_label);

	shell_status->priv->base_label = gtk_label_new ("");
	gtk_container_add (GTK_CONTAINER (shell_status),
			   shell_status->priv->base_label);
	
	g_object_ref (G_OBJECT (shell_status->priv->temp_frame));
	g_object_ref (G_OBJECT (shell_status->priv->base_label));
}

static void
rb_shell_status_finalize (GObject *object)
{
	RBShellStatus *shell_status;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHELL_STATUS (object));

	shell_status = RB_SHELL_STATUS (object);

	g_return_if_fail (shell_status->priv != NULL);

	g_object_unref (G_OBJECT (shell_status->priv->temp_frame));
	g_object_unref (G_OBJECT (shell_status->priv->base_label));
	
	g_free (shell_status->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_shell_status_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBShellStatus *shell_status = RB_SHELL_STATUS (object);

	switch (prop_id)
	{
	case PROP_STATUS:
		if (shell_status->priv->status != NULL)
		{
			g_signal_handlers_disconnect_by_func (G_OBJECT (shell_status->priv->status),
							      G_CALLBACK (rb_view_status_changed_cb),
							      shell_status);
		}
		
		shell_status->priv->status = g_value_get_object (value);

		if (shell_status->priv->status != NULL)
		{
			g_signal_connect (G_OBJECT (shell_status->priv->status),
					  "status_changed",
					  G_CALLBACK (rb_view_status_changed_cb),
					  shell_status);
		}

		rb_shell_status_sync (shell_status);
		break;
	case PROP_ENGINE:
		shell_status->priv->engine = g_value_get_object (value);
		
		g_signal_connect (G_OBJECT (shell_status->priv->engine),
				  "add_hint",
				  G_CALLBACK (rb_shell_status_add_hint_cb),
				  shell_status);
		g_signal_connect (G_OBJECT (shell_status->priv->engine),
				  "remove_hint",
				  G_CALLBACK (rb_shell_status_remove_hint_cb),
				  shell_status);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_shell_status_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBShellStatus *shell_status = RB_SHELL_STATUS (object);

	switch (prop_id)
	{
	case PROP_STATUS:
		g_value_set_object (value, shell_status->priv->status);
		break;
	case PROP_ENGINE:
		g_value_set_object (value, shell_status->priv->engine);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_shell_status_set_status (RBShellStatus *shell_status,
			    RBViewStatus *status)
{
	g_return_if_fail (RB_IS_SHELL_STATUS (shell_status));
	g_return_if_fail (RB_IS_VIEW_STATUS (status));

	g_object_set (G_OBJECT (shell_status),
		      "status", status,
		      NULL);
}

RBViewStatus *
rb_shell_status_get_status (RBShellStatus *shell_status)
{
	RBViewStatus *status;
	
	g_return_val_if_fail (RB_IS_SHELL_STATUS (shell_status), NULL);

	g_object_get (G_OBJECT (shell_status),
		      "status", &status,
		      NULL);

	return status;
}

RBShellStatus *
rb_shell_status_new (BonoboUIEngine *engine)
{
	RBShellStatus *shell_status;

	g_return_val_if_fail (BONOBO_IS_UI_ENGINE (engine), NULL);

	shell_status = g_object_new (RB_TYPE_SHELL_STATUS,
				     "engine", engine,
				     NULL);

	g_return_val_if_fail (shell_status->priv != NULL, NULL);

	return shell_status;
}

static void
rb_shell_status_add_hint_cb (BonoboUIEngine *engine,
			     const char *hint,
			     RBShellStatus *status)
{
	if (status->priv->temp_shown == FALSE)
	{
		gtk_container_remove (GTK_CONTAINER (status),
				      status->priv->base_label);
		gtk_container_add (GTK_CONTAINER (status),
				   status->priv->temp_frame);
		gtk_widget_show_all (status->priv->temp_frame);

		status->priv->temp_shown = TRUE;
	}

	gtk_label_set (GTK_LABEL (status->priv->temp_label), hint);
}

static void
rb_shell_status_remove_hint_cb (BonoboUIEngine *engine,
			        RBShellStatus *status)
{
	if (status->priv->temp_shown == TRUE)
	{
		gtk_container_remove (GTK_CONTAINER (status),
				      status->priv->temp_frame);
		gtk_container_add (GTK_CONTAINER (status),
				   status->priv->base_label);
		
		gtk_widget_show (status->priv->base_label);

		status->priv->temp_shown = FALSE;
	}
}

static void
rb_shell_status_sync (RBShellStatus *status)
{
	const char *text = "";

	if (status->priv->status != NULL)
		text = rb_view_status_get (status->priv->status);

	gtk_label_set_text (GTK_LABEL (status->priv->base_label), text);
}

static void
rb_view_status_changed_cb (RBViewStatus *status,
			   RBShellStatus *shell_status)
{
	rb_shell_status_sync (shell_status);
}
