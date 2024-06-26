/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Olivier Martin <olive.martin@gmail.com>
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

#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "rb-rating.h"
#include "rb-rating-helper.h"
#include "rb-stock-icons.h"
#include "rb-cut-and-paste-code.h"

/* Offset at the beggining of the widget */
#define X_OFFSET 0

/* Vertical offset */
#define Y_OFFSET 2

static void rb_rating_class_init (RBRatingClass *class);
static void rb_rating_init (RBRating *label);
static void rb_rating_finalize (GObject *object);
static void rb_rating_get_property (GObject *object,
				    guint param_id,
				    GValue *value,
				    GParamSpec *pspec);
static void rb_rating_set_property (GObject *object,
				    guint param_id,
				    const GValue *value,
				    GParamSpec *pspec);
static void rb_rating_realize (GtkWidget *widget);
static gboolean rb_rating_draw (GtkWidget *widget, cairo_t *cr);
static gboolean rb_rating_focus (GtkWidget *widget, GtkDirectionType direction);
static gboolean rb_rating_set_rating_cb (RBRating *rating, gdouble score);
static gboolean rb_rating_adjust_rating_cb (RBRating *rating, gdouble adjust);
static gboolean rb_rating_button_press_cb (GtkWidget *widget,
					   GdkEventButton *event);
static void rb_rating_get_preferred_width (GtkWidget *widget, int *minimum_width, int *natural_width);
static void rb_rating_get_preferred_height (GtkWidget *widget, int *minimum_height, int *natural_height);

struct _RBRatingPrivate
{
	double rating;
	RBRatingPixbufs *pixbufs;
};

G_DEFINE_TYPE (RBRating, rb_rating, GTK_TYPE_WIDGET)
#define RB_RATING_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_RATING, RBRatingPrivate))

/**
 * SECTION:rbrating
 * @short_description: widget for displaying song ratings
 *
 * This widget displays a rating (0-5 stars) and allows the user to
 * alter the rating by clicking.
 */

enum
{
	PROP_0,
	PROP_RATING
};

enum
{
	RATED,
	SET_RATING,
	ADJUST_RATING,
	LAST_SIGNAL
};

static guint rb_rating_signals[LAST_SIGNAL] = { 0 };

static void
rb_rating_class_init (RBRatingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class;
	GtkBindingSet *binding_set;

	widget_class = (GtkWidgetClass*) klass;

	object_class->finalize = rb_rating_finalize;
	object_class->get_property = rb_rating_get_property;
	object_class->set_property = rb_rating_set_property;

	widget_class->realize = rb_rating_realize;
	widget_class->draw = rb_rating_draw;
	widget_class->get_preferred_width = rb_rating_get_preferred_width;
	widget_class->get_preferred_height = rb_rating_get_preferred_height;
	widget_class->button_press_event = rb_rating_button_press_cb;
	widget_class->focus = rb_rating_focus;

	klass->set_rating = rb_rating_set_rating_cb;
	klass->adjust_rating = rb_rating_adjust_rating_cb;

	/**
	 * RBRating:rating:
	 *
	 * The rating displayed in the widget, as a floating point value
	 * between 0.0 and 5.0.
	 */
	rb_rating_install_rating_property (object_class, PROP_RATING);

	/**
	 * RBRating::rated:
	 * @rating: the #RBRating
	 * @score: the new rating
	 *
	 * Emitted when the user changes the rating.
	 */
	rb_rating_signals[RATED] =
		g_signal_new ("rated",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRatingClass, rated),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_DOUBLE);
	/**
	 * RBRating::set-rating:
	 * @rating: the #RBRating
	 * @score: the new rating
	 *
	 * Action signal used to change the rating.
	 */
	rb_rating_signals[SET_RATING] =
		g_signal_new ("set-rating",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (RBRatingClass, set_rating),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_DOUBLE);
	/**
	 * RBRating::adjust-rating:
	 * @rating: the #RBRating
	 * @adjust: value to add to the rating
	 *
	 * Action signal used to make a relative adjustment to the
	 * rating.
	 */
	rb_rating_signals[ADJUST_RATING] =
		g_signal_new ("adjust-rating",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (RBRatingClass, adjust_rating),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_DOUBLE);

	binding_set = gtk_binding_set_by_class (klass);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_Home, 0, "set-rating", 1, G_TYPE_DOUBLE, 0.0);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_End, 0, "set-rating", 1, G_TYPE_DOUBLE, (double)RB_RATING_MAX_SCORE);

	gtk_binding_entry_add_signal (binding_set, GDK_KEY_equal, 0, "adjust-rating", 1, G_TYPE_DOUBLE, 1.0);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_plus, 0, "adjust-rating", 1, G_TYPE_DOUBLE, 1.0);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Add, 0, "adjust-rating", 1, G_TYPE_DOUBLE, 1.0);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_Right, 0, "adjust-rating", 1, G_TYPE_DOUBLE, 1.0);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Right, 0, "adjust-rating", 1, G_TYPE_DOUBLE, 1.0);
	
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_minus, 0, "adjust-rating", 1, G_TYPE_DOUBLE, -1.0);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Subtract, 0, "adjust-rating", 1, G_TYPE_DOUBLE, -1.0);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_Left, 0, "adjust-rating", 1, G_TYPE_DOUBLE, -1.0);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Left, 0, "adjust-rating", 1, G_TYPE_DOUBLE, -1.0);
	
	g_type_class_add_private (klass, sizeof (RBRatingPrivate));
}

static void
rb_rating_init (RBRating *rating)
{
	AtkObject *atk_obj;

	rating->priv = RB_RATING_GET_PRIVATE (rating);

	/* create the needed icons */
	rating->priv->pixbufs = rb_rating_pixbufs_load ();
	
	rb_rating_set_accessible_description (GTK_WIDGET (rating), 0.0);

	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (rating)),
				     GTK_STYLE_CLASS_ENTRY);
	atk_obj = gtk_widget_get_accessible (GTK_WIDGET (rating));
	atk_object_set_role (atk_obj, ATK_ROLE_RATING);
}

static void
rb_rating_finalize (GObject *object)
{
	RBRating *rating;

	rating = RB_RATING (object);

	if (rating->priv->pixbufs != NULL) {
		rb_rating_pixbufs_free (rating->priv->pixbufs);
	}

	G_OBJECT_CLASS (rb_rating_parent_class)->finalize (object);
}

static void
rb_rating_get_property (GObject *object,
			guint param_id,
			GValue *value,
			GParamSpec *pspec)
{
	RBRating *rating = RB_RATING (object);

	switch (param_id) {
	case PROP_RATING:
		g_value_set_double (value, rating->priv->rating);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
rb_rating_set_rating (RBRating *rating, gdouble value)
{
	/* clip to the value rating range */
	if (value > RB_RATING_MAX_SCORE) {
		value = RB_RATING_MAX_SCORE;
	} else if (value < 0.0) {
		value = 0.0;
	}

	rating->priv->rating = value;

	/* update accessible object name */
	rb_rating_set_accessible_description (GTK_WIDGET (rating), value);

	gtk_widget_queue_draw (GTK_WIDGET (rating));
}


static void
rb_rating_set_property (GObject *object,
			guint param_id,
			const GValue *value,
			GParamSpec *pspec)
{
	RBRating *rating = RB_RATING (object);

	switch (param_id) {
	case PROP_RATING:
		rb_rating_set_rating (rating, g_value_get_double (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

/**
 * rb_rating_new:
 *
 * Creates a new rating widget
 *
 * Return value: a new #RBRating widget.
 */
RBRating *
rb_rating_new (void)
{
	RBRating *rating;

	rating = g_object_new (RB_TYPE_RATING, NULL);

	g_return_val_if_fail (rating->priv != NULL, NULL);

	return rating;
}

static void
rb_rating_realize (GtkWidget *widget)
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
rb_rating_get_preferred_width (GtkWidget *widget, int *minimum_width, int *natural_width)
{
	int icon_size;
	int width;

	gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &icon_size, NULL);

	width = RB_RATING_MAX_SCORE * icon_size + X_OFFSET;
	if (minimum_width != NULL)
		*minimum_width = width;
	if (natural_width != NULL)
		*natural_width = width;
}

static void
rb_rating_get_preferred_height (GtkWidget *widget, int *minimum_height, int *natural_height)
{
	int icon_size;
	int height;
	gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &icon_size, NULL);

	height = icon_size + Y_OFFSET * 2;
	if (minimum_height != NULL)
		*minimum_height = height;
	if (natural_height != NULL)
		*natural_height = height;
}

static gboolean
rb_rating_draw (GtkWidget *widget, cairo_t *cr)
{
	gboolean ret;
	GdkWindow *window;
	RBRating *rating;
	int x = 0;
	int y = 0;
	int width;
	int height;

	g_return_val_if_fail (RB_IS_RATING (widget), FALSE);

	ret = FALSE;
	rating = RB_RATING (widget);

	window = gtk_widget_get_window (widget);
	width = gdk_window_get_width (window);
	height = gdk_window_get_height (window);

	gtk_render_background (gtk_widget_get_style_context (widget),
			       cr,
			       x, y,
			       width, height);
	gtk_render_frame (gtk_widget_get_style_context (widget),
			  cr,
			  x, y,
			  width, height);

	if (gtk_widget_has_focus (widget)) {
		int focus_width;
		gtk_widget_style_get (widget, "focus-line-width", &focus_width, NULL);

		x += focus_width;
		y += focus_width;
		width -= 2 * focus_width;
		height -= 2 * focus_width;

		gtk_render_focus (gtk_widget_get_style_context (widget),
				  cr,
				  x, y,
				  width, height);
	}

	/* draw the stars */
	if (rating->priv->pixbufs != NULL) {
		ret = rb_rating_render_stars (widget,
					      cr,
					      rating->priv->pixbufs,
					      0, 0,
					      X_OFFSET, Y_OFFSET,
					      rating->priv->rating,
					      FALSE);
	}

	return ret;
}

static gboolean
rb_rating_button_press_cb (GtkWidget *widget,
			   GdkEventButton *event)
{
	int mouse_x, mouse_y;
	double new_rating;
	RBRating *rating;
	GtkAllocation allocation;
	
	g_return_val_if_fail (widget != NULL, FALSE);
	g_return_val_if_fail (RB_IS_RATING (widget), FALSE);

	rating = RB_RATING (widget);

	gdk_window_get_device_position (gtk_widget_get_window (widget),
					gdk_event_get_device ((GdkEvent *)event),
					&mouse_x, &mouse_y, NULL);
	gtk_widget_get_allocation (widget, &allocation);

	new_rating = rb_rating_get_rating_from_widget (widget, mouse_x,
						       allocation.width,
						       rating->priv->rating);

	if (new_rating > -0.0001) {
		g_signal_emit (G_OBJECT (rating),
			       rb_rating_signals[RATED],
			       0, new_rating);
	}

	gtk_widget_grab_focus (widget);

	return FALSE;
}

static gboolean
rb_rating_set_rating_cb (RBRating *rating, gdouble score)
{
	g_signal_emit (G_OBJECT (rating), rb_rating_signals[RATED], 0, score);

	return TRUE;
}

static gboolean
rb_rating_adjust_rating_cb (RBRating *rating, gdouble adjust)
{
	gdouble new_rating;

	new_rating = rating->priv->rating + adjust;

	/* clip to the rating range */
	if (new_rating > RB_RATING_MAX_SCORE) {
		new_rating = RB_RATING_MAX_SCORE;
	} else if (new_rating < 0.0) {
		new_rating = 0.0;
	}

	g_signal_emit (G_OBJECT (rating), rb_rating_signals[RATED], 0, new_rating);

	return TRUE;
}

static gboolean
rb_rating_focus (GtkWidget *widget, GtkDirectionType direction)
{
	return (GTK_WIDGET_CLASS (rb_rating_parent_class))->focus (widget, direction);
}
