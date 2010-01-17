/*
 *  arch-tag: Implementation of the Media Player Source object
 *
 *  Copyright (C) 2009 Paul Bellamy  <paul.a.bellamy@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <string.h>

#include "rb-shell.h"
#include "rb-media-player-source.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-util.h"

typedef struct {
	/* properties dialog bits */
	GtkDialog *properties_dialog;

} RBMediaPlayerSourcePrivate;

G_DEFINE_TYPE (RBMediaPlayerSource, rb_media_player_source, RB_TYPE_REMOVABLE_MEDIA_SOURCE);

#define MEDIA_PLAYER_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_MEDIA_PLAYER_SOURCE, RBMediaPlayerSourcePrivate))

static void rb_media_player_source_class_init (RBMediaPlayerSourceClass *klass);
static void rb_media_player_source_init (RBMediaPlayerSource *source);

static void rb_media_player_source_set_property (GObject *object,
					 guint prop_id,
					 const GValue *value,
					 GParamSpec *pspec);
static void rb_media_player_source_get_property (GObject *object,
					 guint prop_id,
					 GValue *value,
					 GParamSpec *pspec);

enum
{
	PROP_0,
	PROP_DEVICE_SERIAL
};

static void
rb_media_player_source_class_init (RBMediaPlayerSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = rb_media_player_source_set_property;
	object_class->get_property = rb_media_player_source_get_property;

	klass->impl_get_capacity = NULL;
	klass->impl_get_free_space = NULL;
	klass->impl_show_properties = NULL;

	g_object_class_install_property (object_class,
					 PROP_DEVICE_SERIAL,
					 g_param_spec_string ("serial",
							      "serial",
							      "device serial number",
							      NULL,
							      G_PARAM_READABLE));

	g_type_class_add_private (klass, sizeof (RBMediaPlayerSourcePrivate));
}

static void
rb_media_player_source_init (RBMediaPlayerSource *source)
{
}

static void
rb_media_player_source_set_property (GObject *object,
			     guint prop_id,
			     const GValue *value,
			     GParamSpec *pspec)
{
	/*RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (object);*/
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_media_player_source_get_property (GObject *object,
			     guint prop_id,
			     GValue *value,
			     GParamSpec *pspec)
{
	/*RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (object);*/
	switch (prop_id) {
	case PROP_DEVICE_SERIAL:
		/* not actually supported in the base class */
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}



static guint64
get_capacity (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_get_capacity (source);
}

static guint64
get_free_space (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_get_free_space (source);
}

static void
properties_dialog_response_cb (GtkDialog *dialog,
			       int response_id,
			       RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	rb_debug ("media player properties dialog closed");
	gtk_widget_destroy (GTK_WIDGET (dialog));
	g_object_unref (priv->properties_dialog);
	priv->properties_dialog = NULL;
}

void
rb_media_player_source_show_properties (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);
	GtkBuilder *builder;
	GtkWidget *widget;
	const char *ui_file;
	char *used_str;
	char *capacity_str;
	char *name;
	char *text;
	guint64 capacity;
	guint64 free_space;

	if (priv->properties_dialog != NULL) {
		gtk_window_present (GTK_WINDOW (priv->properties_dialog));
		return;
	}

	/* load dialog UI */
	ui_file = rb_file ("media-player-properties.ui");
	if (ui_file == NULL) {
		g_warning ("Couldn't find media-player-properties.ui");
		return;
	}

	builder = rb_builder_load (ui_file, NULL);
	if (builder == NULL) {
		g_warning ("Couldn't load media-player-properties.ui");
		return;
	}

	priv->properties_dialog = GTK_DIALOG (gtk_builder_get_object (builder, "media-player-properties"));
	g_signal_connect_object (priv->properties_dialog,
				 "response",
				 G_CALLBACK (properties_dialog_response_cb),
				 source, 0);

	g_object_get (source, "name", &name, NULL);
	text = g_strdup_printf (_("%s Properties"), name);
	gtk_window_set_title (GTK_WINDOW (priv->properties_dialog), text);
	g_free (text);
	g_free (name);

	/*
	 * fill in some common details:
	 * - volume usage (need to hook up signals etc. to update this live)
	 */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar-device-usage"));
	capacity = get_capacity (source);
	free_space = get_free_space (source);
	used_str = g_format_size_for_display (capacity - free_space);
	capacity_str = g_format_size_for_display (capacity);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget),
				       (double)(capacity - free_space)/(double)capacity);
	/* Translators: this is used to display the amount of storage space
	 * used and the total storage space on an device.
	 */
	text = g_strdup_printf (_("%s of %s"), used_str, capacity_str);
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (widget), text);
	g_free (text);
	g_free (capacity_str);
	g_free (used_str);

	/* let the subclass fill in device type specific details (model names, device names,
	 * .. battery levels?) and add more tabs to the notebook to display 'advanced' stuff.
	 */

	if (klass->impl_show_properties) {
		klass->impl_show_properties (source,
					     GTK_WIDGET (gtk_builder_get_object (builder, "device-info-box")),
					     GTK_WIDGET (gtk_builder_get_object (builder, "media-player-notebook")));
	}

	gtk_widget_show (GTK_WIDGET (priv->properties_dialog));
}
