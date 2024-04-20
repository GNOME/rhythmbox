/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2012 Jonathan Matthew <jonathan@d14n.org>
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

#include <config.h>

#include <glib/gi18n.h>

#include <widgets/rb-fading-image.h>
#include <lib/rb-debug.h>
#include <lib/rb-util.h>

#define RENDER_FRAME_TIME	(1000 / 25)	/* fps? */
#define BORDER_WIDTH		1.0

#define MAX_TOOLTIP_SIZE	256

static void rb_fading_image_class_init (RBFadingImageClass *klass);
static void rb_fading_image_init (RBFadingImage *image);

struct _RBFadingImagePrivate
{
	char *fallback_icon;
	cairo_pattern_t *current_pat;
	cairo_pattern_t *next_pat;
	cairo_pattern_t *fallback_pat;
	gdouble alpha;

	GdkPixbuf *current;
	int current_width;
	int current_height;
	GdkPixbuf *current_full;
	GdkPixbuf *next;
	GdkPixbuf *next_full;
	GdkPixbuf *fallback;
	GdkPixbufLoader *loader;
	gboolean next_set;

	guint64 start;
	guint64 end;
	gulong render_timer_id;

	gboolean use_tooltip;
};

G_DEFINE_TYPE (RBFadingImage, rb_fading_image, GTK_TYPE_WIDGET)

/**
 * SECTION:rbfadingimage
 * @short_description: image display widget that fades between two images
 *
 * This widget displays images, performing a simple fade transition between
 * them.  It also emits signals when URIs or pixbufs are dropped onto it.
 */

enum
{
	PROP_0,
	PROP_FALLBACK,
	PROP_USE_TOOLTIP
};

enum
{
	URI_DROPPED,
	PIXBUF_DROPPED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

static gboolean
prepare_image (cairo_t *cr, cairo_pattern_t **save, GdkPixbuf *pixbuf)
{
	if (*save != NULL) {
		cairo_set_source (cr, *save);
		return TRUE;
	}

	if (pixbuf != NULL) {
		gdk_cairo_set_source_pixbuf (cr, pixbuf, 0.0, 0.0);
		*save = cairo_get_source (cr);
		cairo_pattern_reference (*save);
		return TRUE;
	} else {
		return FALSE;
	}
}

static void
impl_realize (GtkWidget *widget)
{
	GtkAllocation allocation;
	GdkWindowAttr attributes;
	GdkWindow *window;
	int attributes_mask;

	gtk_widget_set_realized (widget, TRUE);

	gtk_widget_get_allocation (widget, &allocation);

	attributes.x = allocation.x;
	attributes.y = allocation.y;
	attributes.width = allocation.width;
	attributes.height = allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.event_mask = gtk_widget_get_events (widget) | GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_FOCUS_CHANGE_MASK;
	attributes.visual = gtk_widget_get_visual (widget);

	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

	window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask);
	gtk_widget_set_window (widget, window);
	gdk_window_set_user_data (window, widget);

	gtk_widget_set_can_focus (widget, TRUE);
}

static void
draw_image (cairo_t *cr, int image_width, int image_height, int width, int height, cairo_extend_t extend, double alpha, gboolean border)
{
	cairo_matrix_t matrix;
	cairo_save (cr);

	if (border) {
		cairo_matrix_init_translate (&matrix,
					     - (BORDER_WIDTH + (width/2 - image_width/2)),
					     - (BORDER_WIDTH + (height/2 - image_height/2)));
	} else {
		cairo_matrix_init_translate (&matrix,
					     - (width/2 - image_width/2),
					     - (height/2 - image_height/2));
	}
	cairo_pattern_set_matrix (cairo_get_source (cr), &matrix);
	cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_BEST);
	cairo_pattern_set_extend (cairo_get_source (cr), extend);
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

	cairo_rectangle (cr, BORDER_WIDTH, BORDER_WIDTH, width, height);
	cairo_clip (cr);
	cairo_paint_with_alpha (cr, alpha);

	cairo_restore (cr);
}

static void
render_current (RBFadingImage *image, cairo_t *cr, int width, int height, gboolean border)
{
	if (prepare_image (cr, &image->priv->current_pat, image->priv->current)) {
		draw_image (cr,
			    image->priv->current_width,
			    image->priv->current_height,
			    width,
			    height,
			    CAIRO_EXTEND_NONE,
			    1.0 - image->priv->alpha,
			    border);
	} else if (prepare_image (cr, &image->priv->fallback_pat, image->priv->fallback)) {
		draw_image (cr,
			    gdk_pixbuf_get_width (image->priv->fallback),
			    gdk_pixbuf_get_height (image->priv->fallback),
			    width,
			    height,
			    CAIRO_EXTEND_PAD,
			    1.0 - image->priv->alpha,
			    border);
	} else {
		cairo_save (cr);
		cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
		cairo_rectangle (cr,
				 border ? BORDER_WIDTH : 0,
				 border ? BORDER_WIDTH : 0,
				 width,
				 height);
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		cairo_clip (cr);
		cairo_paint (cr);
		cairo_restore (cr);
	}
}

static void
render_next (RBFadingImage *image, cairo_t *cr, int width, int height, gboolean border)
{
	if (image->priv->alpha < 0.001) {
		/* do nothing */
	} else if (prepare_image (cr, &image->priv->next_pat, image->priv->next)) {
		draw_image (cr,
			    gdk_pixbuf_get_width (image->priv->next),
			    gdk_pixbuf_get_height (image->priv->next),
			    width,
			    height,
			    CAIRO_EXTEND_NONE,
			    image->priv->alpha,
			    border);
	} else if (prepare_image (cr, &image->priv->fallback_pat, image->priv->fallback)) {
		draw_image (cr,
			    gdk_pixbuf_get_width (image->priv->fallback),
			    gdk_pixbuf_get_height (image->priv->fallback),
			    width,
			    height,
			    CAIRO_EXTEND_PAD,
			    image->priv->alpha,
			    border);
	} else {
		/* also do nothing */
	}
}

static gboolean
impl_draw (GtkWidget *widget, cairo_t *cr)
{
	RBFadingImage *image;
	int border_width;
	int border_height;
	int width;
	int height;

	width = gtk_widget_get_allocated_width (widget);
	height = gtk_widget_get_allocated_height (widget);
	border_width = width;
	border_height = height;

	image = RB_FADING_IMAGE (widget);
	if (image->priv->alpha > 0.01) {
		if (image->priv->next) {
			border_width = gdk_pixbuf_get_width (image->priv->next) + 2 * BORDER_WIDTH;
			border_height = gdk_pixbuf_get_height (image->priv->next) + 2 * BORDER_WIDTH;
		}
	} else if (image->priv->current) {
		border_width = gdk_pixbuf_get_width (image->priv->current) + 2 * BORDER_WIDTH;
		border_height = gdk_pixbuf_get_height (image->priv->current) + 2 * BORDER_WIDTH;
	}

	cairo_save (cr);
	cairo_set_line_width (cr, BORDER_WIDTH);
	cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_rectangle (cr,
			 (width - border_width) / 2,
			 (height - border_height) / 2,
			 border_width,
			 border_height);
	cairo_stroke (cr);
	cairo_restore (cr);

	width -= 2 * BORDER_WIDTH;
	height -= 2 * BORDER_WIDTH;

	render_current (image, cr, width, height, TRUE);
	render_next (image, cr, width, height, TRUE);

	return TRUE;
}

static gboolean
impl_query_tooltip (GtkWidget *widget, int x, int y, gboolean keyboard_mode, GtkTooltip *tooltip)
{
	RBFadingImage *image = RB_FADING_IMAGE (widget);
	GdkPixbuf *scaled;
	GdkPixbuf *full;

	if (image->priv->use_tooltip == FALSE)
		return FALSE;

	if (image->priv->render_timer_id != 0) {
		full = image->priv->next_full;
		scaled = image->priv->next;
	} else {
		full = image->priv->current_full;
		scaled = image->priv->current;
	}

	if (full == NULL) {
		gtk_tooltip_set_icon (tooltip, NULL);
		gtk_tooltip_set_text (tooltip, _("Drop artwork here"));
		return TRUE;
	} else if (full == scaled) {
		return FALSE;
	} else {
		gtk_tooltip_set_icon (tooltip, full);
		return TRUE;
	}
}

static void
impl_drag_data_received (GtkWidget *widget,
			 GdkDragContext *context,
			 int x,
			 int y,
			 GtkSelectionData *selection,
			 guint info,
			 guint time_)
{
	GdkPixbuf *pixbuf;
	char **uris;

	pixbuf = gtk_selection_data_get_pixbuf (selection);
	if (pixbuf != NULL) {
		g_signal_emit (widget, signals[PIXBUF_DROPPED], 0, pixbuf);
		g_object_unref (pixbuf);
		return;
	}

	uris = gtk_selection_data_get_uris (selection);
	if (uris != NULL) {
		if (uris[0] != NULL) {
			g_signal_emit (widget, signals[URI_DROPPED], 0, uris[0]);
		}

		g_strfreev (uris);
		return;
	}

	rb_debug ("weird drag data received");
}

static void
impl_drag_data_get (GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection, guint info, guint time_)
{
	RBFadingImage *image = RB_FADING_IMAGE (widget);

	if (image->priv->current_full) {
		gtk_selection_data_set_pixbuf (selection, image->priv->current_full);
	}

	/* might be nice if we could provide a uri here? */
}


static void
impl_finalize (GObject *object)
{
	RBFadingImage *image = RB_FADING_IMAGE (object);

	g_free (image->priv->fallback_icon);

	if (image->priv->current_pat != NULL) {
		cairo_pattern_destroy (image->priv->current_pat);
	}
	if (image->priv->next_pat != NULL) {
		cairo_pattern_destroy (image->priv->next_pat);
	}
	if (image->priv->fallback_pat != NULL) {
		cairo_pattern_destroy (image->priv->fallback_pat);
	}

	G_OBJECT_CLASS (rb_fading_image_parent_class)->finalize (object);
}

static void
impl_dispose (GObject *object)
{
	RBFadingImage *image = RB_FADING_IMAGE (object);

	if (image->priv->render_timer_id != 0) {
		g_source_remove (image->priv->render_timer_id);
		image->priv->render_timer_id = 0;
	}
	if (image->priv->current != NULL) {
		g_object_unref (image->priv->current);
		image->priv->current = NULL;
	}
	if (image->priv->next != NULL) {
		g_object_unref (image->priv->next);
		image->priv->next = NULL;
	}
	if (image->priv->fallback != NULL) {
		g_object_unref (image->priv->fallback);
		image->priv->fallback = NULL;
	}

	G_OBJECT_CLASS (rb_fading_image_parent_class)->dispose (object);
}

static void
impl_constructed (GObject *object)
{
	RBFadingImage *image;

	RB_CHAIN_GOBJECT_METHOD (rb_fading_image_parent_class, constructed, object);

	image = RB_FADING_IMAGE (object);

	if (image->priv->fallback_icon != NULL) {
		GError *error = NULL;
		image->priv->fallback =
			gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
						  image->priv->fallback_icon,
						  48,
						  GTK_ICON_LOOKUP_FORCE_SIZE,
						  &error);
		if (error != NULL) {
			g_warning ("couldn't load fallback icon %s: %s", image->priv->fallback_icon, error->message);
			g_clear_error (&error);
		}
	}

	gtk_widget_set_has_tooltip (GTK_WIDGET (image), TRUE);

	/* drag and drop target */
	gtk_drag_dest_set (GTK_WIDGET (image), GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
	gtk_drag_dest_add_image_targets (GTK_WIDGET (image));
	gtk_drag_dest_add_uri_targets (GTK_WIDGET (image));

	/* drag and drop source */
	gtk_drag_source_set (GTK_WIDGET (image), GDK_BUTTON1_MASK, NULL, 0, GDK_ACTION_COPY);
	gtk_drag_source_add_image_targets (GTK_WIDGET (image));
}


static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBFadingImage *image = RB_FADING_IMAGE (object);

	switch (prop_id) {
	case PROP_FALLBACK:
		g_value_set_string (value, image->priv->fallback_icon);
		break;
	case PROP_USE_TOOLTIP:
		g_value_set_boolean (value, image->priv->use_tooltip);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBFadingImage *image = RB_FADING_IMAGE (object);

	switch (prop_id) {
	case PROP_FALLBACK:
		image->priv->fallback_icon = g_value_dup_string (value);
		break;
	case PROP_USE_TOOLTIP:
		image->priv->use_tooltip = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_fading_image_init (RBFadingImage *image)
{
	image->priv = G_TYPE_INSTANCE_GET_PRIVATE (image, RB_TYPE_FADING_IMAGE, RBFadingImagePrivate);
}

static void
rb_fading_image_class_init (RBFadingImageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->constructed = impl_constructed;
	object_class->dispose = impl_dispose;
	object_class->finalize = impl_finalize;
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	widget_class->realize = impl_realize;
	widget_class->draw = impl_draw;
	widget_class->query_tooltip = impl_query_tooltip;
	widget_class->drag_data_get = impl_drag_data_get;
	widget_class->drag_data_received = impl_drag_data_received;

	/**
	 * RBFadingImage:fallback:
	 *
	 * Name of an icon to display when no image is available.
	 */
	g_object_class_install_property (object_class,
					 PROP_FALLBACK,
					 g_param_spec_string ("fallback",
							      "fallback",
							      "fallback icon name",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBFadingImage:use-tooltip:
	 *
	 * Whether to display a tooltip on the image
	 */
	g_object_class_install_property (object_class,
					 PROP_USE_TOOLTIP,
					 g_param_spec_boolean ("use-tooltip",
							       "use tooltip",
							       "use tooltip",
							       TRUE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBFadingImage::uri-dropped
	 * @image: the #RBFadingImage
	 * @uri: the URI that was dropped
	 *
	 * Emitted when a URI is dragged and dropped on the image
	 */
	signals[URI_DROPPED] =
		g_signal_new ("uri-dropped",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);

	/**
	 * RBFadingImage::pixbuf-dropped
	 * @image: the #RBFadingImage
	 * @pixbuf: the pixbuf that was dropped
	 *
	 * Emitted when an image is dragged and dropped on the image
	 */
	signals[PIXBUF_DROPPED] =
		g_signal_new ("pixbuf-dropped",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1, GDK_TYPE_PIXBUF);

	g_type_class_add_private (klass, sizeof (RBFadingImagePrivate));
}


static GdkPixbuf *
scale_thumbnail_if_necessary (RBFadingImage *image, GdkPixbuf *pixbuf)
{
	int w, h;
	int pw, ph;
	int sw, sh;
	double factor;

	w = gtk_widget_get_allocated_width (GTK_WIDGET (image)) - 2 * BORDER_WIDTH;
	h = gtk_widget_get_allocated_height (GTK_WIDGET (image)) - 2 * BORDER_WIDTH;
	if (w < 1 || h < 1) {
		return NULL;
	}

	pw = gdk_pixbuf_get_width (pixbuf);
	ph = gdk_pixbuf_get_height (pixbuf);

	if (pw <= w && ph <= h) {
		return g_object_ref (pixbuf);
	}

	if (pw > ph) {
		sw = w;
		factor = (double) w / pw;
		sh = (int)((double)ph * factor);
	} else {
		sh = h;
		factor = (double) h / ph;
		sw = (int)((double)pw * factor);
	}

	return gdk_pixbuf_scale_simple (pixbuf, sw, sh, GDK_INTERP_HYPER);
}

static GdkPixbuf *
scale_full_if_necessary (RBFadingImage *image, GdkPixbuf *pixbuf)
{
	int pw, ph;
	int sw, sh;
	double factor;

	pw = gdk_pixbuf_get_width (pixbuf);
	ph = gdk_pixbuf_get_height (pixbuf);

	if (pw <= MAX_TOOLTIP_SIZE && ph <= MAX_TOOLTIP_SIZE) {
		return g_object_ref (pixbuf);
	}
	if (pw > ph) {
		sw = MAX_TOOLTIP_SIZE;
		factor = (double) MAX_TOOLTIP_SIZE / pw;
		sh = (int)((double)ph * factor);
	} else {
		sh = MAX_TOOLTIP_SIZE;
		factor = (double) MAX_TOOLTIP_SIZE / ph;
		sw = (int)((double)pw * factor);
	}

	return gdk_pixbuf_scale_simple (pixbuf, sw, sh, GDK_INTERP_HYPER);
}


static void
clear_next (RBFadingImage *image)
{
	if (image->priv->next_pat != NULL) {
		cairo_pattern_destroy (image->priv->next_pat);
		image->priv->next_pat = NULL;
	}
	if (image->priv->next != NULL) {
		g_object_unref (image->priv->next);
		image->priv->next = NULL;
	}
	if (image->priv->next_full != NULL) {
		g_object_unref (image->priv->next_full);
		image->priv->next_full = NULL;
	}
	image->priv->next_set = FALSE;
}

static void
replace_current (RBFadingImage *image, GdkPixbuf *next, GdkPixbuf *next_full)
{
	if (image->priv->current_pat != NULL) {
		cairo_pattern_destroy (image->priv->current_pat);
		image->priv->current_pat = NULL;
	}
	if (image->priv->current != NULL) {
		g_object_unref (image->priv->current);
		image->priv->current = NULL;
	}
	if (image->priv->current_full != NULL) {
		g_object_unref (image->priv->current_full);
		image->priv->current_full = NULL;
	}
	if (next != NULL) {
		image->priv->current = g_object_ref (next);
		image->priv->current_width = gdk_pixbuf_get_width (image->priv->current);
		image->priv->current_height = gdk_pixbuf_get_height (image->priv->current);
	}
	if (next_full != NULL) {
		image->priv->current_full = g_object_ref (next_full);
	}
}

static void
composite_into_current (RBFadingImage *image)
{
	cairo_t *cr;
	cairo_surface_t *dest;
	int width;
	int height;

	width = gtk_widget_get_allocated_width (GTK_WIDGET (image)) - 2 * BORDER_WIDTH;
	height = gtk_widget_get_allocated_height (GTK_WIDGET (image)) - 2 * BORDER_WIDTH;
	if (width < 1 || height < 1) {
		if (image->priv->current_pat != NULL) {
			cairo_pattern_destroy (image->priv->current_pat);
		}
		image->priv->current_pat = NULL;
		image->priv->current_width = 0;
		image->priv->current_height = 0;
		return;
	}

	dest = cairo_image_surface_create (CAIRO_FORMAT_RGB24, width, height);

	cr = cairo_create (dest);
	render_current (image, cr, width, height, FALSE);
	render_next (image, cr, width, height, FALSE);
	cairo_destroy (cr);

	if (image->priv->current_pat != NULL) {
		cairo_pattern_destroy (image->priv->current_pat);
	}
	image->priv->current_pat = cairo_pattern_create_for_surface (dest);
	image->priv->current_width = width;
	image->priv->current_height = height;

	cairo_surface_destroy (dest);
}

/**
 * rb_fading_image_set_pixbuf:
 * @image: a #RBFadingImage
 * @pixbuf: (transfer none) (allow-none): the next pixbuf to display
 *
 * Sets the next image to be displayed.
 */
void
rb_fading_image_set_pixbuf (RBFadingImage *image, GdkPixbuf *pixbuf)
{
	GdkPixbuf *scaled = NULL;
	GdkPixbuf *full = NULL;

	if (pixbuf != NULL) {
		scaled = scale_thumbnail_if_necessary (image, pixbuf);
		full = scale_full_if_necessary (image, pixbuf);
	}

	if (image->priv->render_timer_id != 0) {
		composite_into_current (image);
		clear_next (image);

		image->priv->next_full = full;
		image->priv->next = scaled;
		image->priv->next_set = TRUE;
	} else {
		clear_next (image);
		replace_current (image, scaled, full);
		gtk_widget_queue_draw (GTK_WIDGET (image));
		gtk_widget_trigger_tooltip_query (GTK_WIDGET (image));

		if (scaled != NULL) {
			g_object_unref (scaled);
		}
		if (full != NULL) {
			g_object_unref (full);
		}
	}
}

static gboolean
render_timer (RBFadingImage *image)
{
	gint64 now;

	now = g_get_monotonic_time ();

	/* calculate alpha, whether this is the last frame, etc. */
	if (image->priv->next != NULL || image->priv->current != NULL) {
		image->priv->alpha = (((double)now - image->priv->start) / (image->priv->end - image->priv->start));
		if (image->priv->alpha > 1.0)
			image->priv->alpha = 1.0;

		gtk_widget_queue_draw (GTK_WIDGET (image));
	}

	if (now >= image->priv->end) {
		replace_current (image, image->priv->next, image->priv->next_full);
		clear_next (image);
		gtk_widget_trigger_tooltip_query (GTK_WIDGET (image));
		image->priv->alpha = 0.0;
		image->priv->render_timer_id = 0;
		return FALSE;
	}

	return TRUE;
}

static void
update_render_timer (RBFadingImage *image)
{
	/* add timer if not already present */
	if (image->priv->render_timer_id == 0) {
		image->priv->render_timer_id = g_timeout_add (RENDER_FRAME_TIME,
							      (GSourceFunc) render_timer,
							      image);
	}
}

/**
 * rb_fading_image_start:
 * @image: a #RBFadingImage
 * @duration: length of fade in milliseconds
 *
 * Starts fading to the next image.  If no next image has been supplied,
 * the fallback image will be used instead.  If the next image has been
 * supplied, but has not finished loading yet, the fade will be delayed
 * until it finishes.  If the previous fade has not yet finished,
 * something tricky happens.
 */
void
rb_fading_image_start (RBFadingImage *image, guint64 duration)
{
	image->priv->start = g_get_monotonic_time ();
	image->priv->end = image->priv->start + (duration * 1000);

	if (image->priv->next_set) {
		replace_current (image, image->priv->next, image->priv->next_full);
		clear_next (image);
		image->priv->next_set = TRUE;
	}

	update_render_timer (image);
}
