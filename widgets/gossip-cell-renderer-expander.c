/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006  Kristian Rietveld <kris@babi-pangang.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* To do:
 *  - should probably cancel animation if model changes
 *  - need to handle case where node-in-animation is removed
 *  - it only handles a single animation at a time; but I guess users
 *    aren't fast enough to trigger two or more animations at once anyway :P
 *    (could guard for this by just cancelling the "old" animation, and
 *     start the new one).
 */

#include "config.h"

#include <gtk/gtktreeview.h>

#include "gossip-cell-renderer-expander.h"

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GOSSIP_TYPE_CELL_RENDERER_EXPANDER, GossipCellRendererExpanderPriv))

static void     gossip_cell_renderer_expander_init         (GossipCellRendererExpander      *expander);
static void     gossip_cell_renderer_expander_class_init   (GossipCellRendererExpanderClass *klass);
static void     gossip_cell_renderer_expander_get_property (GObject                         *object,
							    guint                            param_id,
							    GValue                          *value,
							    GParamSpec                      *pspec);
static void     gossip_cell_renderer_expander_set_property (GObject                         *object,
							    guint                            param_id,
							    const GValue                    *value,
							    GParamSpec                      *pspec);
static void     gossip_cell_renderer_expander_finalize     (GObject                         *object);
static void     gossip_cell_renderer_expander_get_size     (GtkCellRenderer                 *cell,
							    GtkWidget                       *widget,
							    GdkRectangle                    *cell_area,
							    gint                            *x_offset,
							    gint                            *y_offset,
							    gint                            *width,
							    gint                            *height);
static void     gossip_cell_renderer_expander_render       (GtkCellRenderer                 *cell,
							    GdkWindow                       *window,
							    GtkWidget                       *widget,
							    GdkRectangle                    *background_area,
							    GdkRectangle                    *cell_area,
							    GdkRectangle                    *expose_area,
							    GtkCellRendererState             flags);
static gboolean gossip_cell_renderer_expander_activate     (GtkCellRenderer                 *cell,
							    GdkEvent                        *event,
							    GtkWidget                       *widget,
							    const gchar                     *path,
							    GdkRectangle                    *background_area,
							    GdkRectangle                    *cell_area,
							    GtkCellRendererState             flags);

/* Properties */
enum {
	PROP_0,
	PROP_EXPANDER_STYLE,
	PROP_EXPANDER_SIZE,
	PROP_ACTIVATABLE
};

typedef struct _GossipCellRendererExpanderPriv GossipCellRendererExpanderPriv;

struct _GossipCellRendererExpanderPriv {
	GtkExpanderStyle     expander_style;
	gint                 expander_size;

	GtkTreeView         *animation_view;
	GtkTreeRowReference *animation_node;
	GtkExpanderStyle     animation_style;
	guint                animation_timeout;
	GdkRectangle         animation_area;

	guint                activatable : 1;
	guint                animation_expanding : 1;
};

G_DEFINE_TYPE (GossipCellRendererExpander, gossip_cell_renderer_expander, GTK_TYPE_CELL_RENDERER)

static void
gossip_cell_renderer_expander_init (GossipCellRendererExpander *expander)
{
	GossipCellRendererExpanderPriv *priv;

	priv = GET_PRIV (expander);

	priv->expander_style = GTK_EXPANDER_COLLAPSED;
	priv->expander_size = 12;
	priv->activatable = TRUE;
	priv->animation_node = NULL;

	GTK_CELL_RENDERER (expander)->xpad = 2;
	GTK_CELL_RENDERER (expander)->ypad = 2;
	GTK_CELL_RENDERER (expander)->mode = GTK_CELL_RENDERER_MODE_ACTIVATABLE;
}

static void
gossip_cell_renderer_expander_class_init (GossipCellRendererExpanderClass *klass)
{
	GObjectClass         *object_class;
	GtkCellRendererClass *cell_class;

	object_class  = G_OBJECT_CLASS (klass);
	cell_class = GTK_CELL_RENDERER_CLASS (klass);

	object_class->finalize = gossip_cell_renderer_expander_finalize;

	object_class->get_property = gossip_cell_renderer_expander_get_property;
	object_class->set_property = gossip_cell_renderer_expander_set_property;

	cell_class->get_size = gossip_cell_renderer_expander_get_size;
	cell_class->render = gossip_cell_renderer_expander_render;
	cell_class->activate = gossip_cell_renderer_expander_activate;

	g_object_class_install_property (object_class,
					 PROP_EXPANDER_STYLE,
					 g_param_spec_enum ("expander-style",
							    "Expander Style",
							    "Style to use when painting the expander",
							    GTK_TYPE_EXPANDER_STYLE,
							    GTK_EXPANDER_COLLAPSED,
							    G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_EXPANDER_SIZE,
					 g_param_spec_int ("expander-size",
							   "Expander Size",
							   "The size of the expander",
							   0,
							   G_MAXINT,
							   12,
							   G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_ACTIVATABLE,
					 g_param_spec_boolean ("activatable",
							       "Activatable",
							       "The expander can be activated",
							       TRUE,
							       G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof (GossipCellRendererExpanderPriv));
}

static void
gossip_cell_renderer_expander_get_property (GObject    *object,
					    guint       param_id,
					    GValue     *value,
					    GParamSpec *pspec)
{
	GossipCellRendererExpander     *expander;
	GossipCellRendererExpanderPriv *priv;

	expander = GOSSIP_CELL_RENDERER_EXPANDER (object);
	priv = GET_PRIV (expander);

	switch (param_id) {
	case PROP_EXPANDER_STYLE:
		g_value_set_enum (value, priv->expander_style);
		break;

	case PROP_EXPANDER_SIZE:
		g_value_set_int (value, priv->expander_size);
		break;

	case PROP_ACTIVATABLE:
		g_value_set_boolean (value, priv->activatable);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
gossip_cell_renderer_expander_set_property (GObject      *object,
					    guint         param_id,
					    const GValue *value,
					    GParamSpec   *pspec)
{
	GossipCellRendererExpander     *expander;
	GossipCellRendererExpanderPriv *priv;

	expander = GOSSIP_CELL_RENDERER_EXPANDER (object);
	priv = GET_PRIV (expander);

	switch (param_id) {
	case PROP_EXPANDER_STYLE:
		priv->expander_style = g_value_get_enum (value);
		break;

	case PROP_EXPANDER_SIZE:
		priv->expander_size = g_value_get_int (value);
		break;

	case PROP_ACTIVATABLE:
		priv->activatable = g_value_get_boolean (value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
gossip_cell_renderer_expander_finalize (GObject *object)
{
	GossipCellRendererExpanderPriv *priv;

	priv = GET_PRIV (object);

	if (priv->animation_timeout) {
		g_source_remove (priv->animation_timeout);
		priv->animation_timeout = 0;
	}

	if (priv->animation_node) {
		gtk_tree_row_reference_free (priv->animation_node);
	}

	(* G_OBJECT_CLASS (gossip_cell_renderer_expander_parent_class)->finalize) (object);
}

GtkCellRenderer *
gossip_cell_renderer_expander_new (void)
{
	return g_object_new (GOSSIP_TYPE_CELL_RENDERER_EXPANDER, NULL);
}

static void
gossip_cell_renderer_expander_get_size (GtkCellRenderer *cell,
					GtkWidget       *widget,
					GdkRectangle    *cell_area,
					gint            *x_offset,
					gint            *y_offset,
					gint            *width,
					gint            *height)
{
	GossipCellRendererExpander     *expander;
	GossipCellRendererExpanderPriv *priv;

	expander = (GossipCellRendererExpander*) cell;
	priv = GET_PRIV (expander);

	if (cell_area) {
		if (x_offset) {
			*x_offset = cell->xalign * (cell_area->width - (priv->expander_size + (2 * cell->xpad)));
			*x_offset = MAX (*x_offset, 0);
		}

		if (y_offset) {
			*y_offset = cell->yalign * (cell_area->height - (priv->expander_size + (2 * cell->ypad)));
			*y_offset = MAX (*y_offset, 0);
		}
	} else {
		if (x_offset)
			*x_offset = 0;

		if (y_offset)
			*y_offset = 0;
	}

	if (width)
		*width = cell->xpad * 2 + priv->expander_size;

	if (height)
		*height = cell->ypad * 2 + priv->expander_size;
}

static void
gossip_cell_renderer_expander_render (GtkCellRenderer      *cell,
				      GdkWindow            *window,
				      GtkWidget            *widget,
				      GdkRectangle         *background_area,
				      GdkRectangle         *cell_area,
				      GdkRectangle         *expose_area,
				      GtkCellRendererState  flags)
{
	GossipCellRendererExpander     *expander;
	GossipCellRendererExpanderPriv *priv;
	GtkExpanderStyle                expander_style;
	gint                            x_offset, y_offset;

	expander = (GossipCellRendererExpander*) cell;
	priv = GET_PRIV (expander);

	if (priv->animation_node) {
		GtkTreePath *path;
		GdkRectangle rect;

		/* Not sure if I like this ... */
		path = gtk_tree_row_reference_get_path (priv->animation_node);
		gtk_tree_view_get_background_area (priv->animation_view, path,
						   NULL, &rect);
		gtk_tree_path_free (path);

		if (background_area->y == rect.y)
			expander_style = priv->animation_style;
		else
			expander_style = priv->expander_style;
	} else
		expander_style = priv->expander_style;

	gossip_cell_renderer_expander_get_size (cell, widget, cell_area,
						&x_offset, &y_offset,
						NULL, NULL);

	gtk_paint_expander (widget->style,
			    window,
			    GTK_STATE_NORMAL,
			    expose_area,
			    widget,
			    "treeview",
			    cell_area->x + x_offset + cell->xpad + priv->expander_size / 2,
			    cell_area->y + y_offset + cell->ypad + priv->expander_size / 2,
			    expander_style);
}

static void
invalidate_node (GtkTreeView *tree_view,
		 GtkTreePath *path)
{
       GdkWindow    *bin_window;
       GdkRectangle  rect;

       bin_window = gtk_tree_view_get_bin_window (tree_view);

       gtk_tree_view_get_background_area (tree_view, path, NULL, &rect);

       rect.x = 0;
       rect.width = GTK_WIDGET (tree_view)->allocation.width;

       gdk_window_invalidate_rect (bin_window, &rect, TRUE);
}

static gboolean
do_animation (GossipCellRendererExpander *expander)
{
	GossipCellRendererExpanderPriv *priv;
	GtkTreePath                    *path;
	gboolean                        done = FALSE;

	priv = GET_PRIV (expander);

	if (priv->animation_expanding) {
		if (priv->animation_style == GTK_EXPANDER_SEMI_COLLAPSED)
			priv->animation_style = GTK_EXPANDER_SEMI_EXPANDED;
		else if (priv->animation_style == GTK_EXPANDER_SEMI_EXPANDED) {
			priv->animation_style = GTK_EXPANDER_EXPANDED;
			done = TRUE;
		}
	} else {
		if (priv->animation_style == GTK_EXPANDER_SEMI_EXPANDED)
			priv->animation_style = GTK_EXPANDER_SEMI_COLLAPSED;
		else if (priv->animation_style == GTK_EXPANDER_SEMI_COLLAPSED) {
			priv->animation_style = GTK_EXPANDER_COLLAPSED;
			done = TRUE;
		}
	}

	path = gtk_tree_row_reference_get_path (priv->animation_node);
	invalidate_node (priv->animation_view, path);
	gtk_tree_path_free (path);

	if (done) {
		gtk_tree_row_reference_free (priv->animation_node);
		priv->animation_node = NULL;
		priv->animation_timeout = 0;
	}

	return !done;
}

static gboolean
animation_timeout (gpointer data)
{
	gboolean retval;

	GDK_THREADS_ENTER ();

	retval = do_animation (data);

	GDK_THREADS_LEAVE ();

	return retval;
}

static void
gossip_cell_renderer_expander_start_animation (GossipCellRendererExpander *expander,
					       GtkTreeView                *tree_view,
					       GtkTreePath                *path,
					       gboolean                    expanding,
					       GdkRectangle               *background_area)
{
	GossipCellRendererExpanderPriv *priv;

	priv = GET_PRIV (expander);

	if (expanding) {
		priv->animation_style = GTK_EXPANDER_SEMI_COLLAPSED;
	} else {
		priv->animation_style = GTK_EXPANDER_SEMI_EXPANDED;
	}

	invalidate_node (tree_view, path);

	priv->animation_expanding = expanding;
	priv->animation_view = tree_view;
	priv->animation_node = gtk_tree_row_reference_new (gtk_tree_view_get_model (tree_view), path);
	priv->animation_timeout = g_timeout_add (50, animation_timeout, expander);
}

static gboolean
gossip_cell_renderer_expander_activate (GtkCellRenderer      *cell,
					GdkEvent             *event,
					GtkWidget            *widget,
					const gchar          *path_string,
					GdkRectangle         *background_area,
					GdkRectangle         *cell_area,
					GtkCellRendererState  flags)
{
	GossipCellRendererExpander     *expander;
	GossipCellRendererExpanderPriv *priv;
	GtkTreePath                    *path;
	GtkSettings                    *settings;
	gboolean                        animate;
	gboolean                        expanding;
	gboolean                        in_cell;
	int                             mouse_x;
	int                             mouse_y;

	expander = GOSSIP_CELL_RENDERER_EXPANDER (cell);
	priv = GET_PRIV (cell);

	if (!GTK_IS_TREE_VIEW (widget) || !priv->activatable)
		return FALSE;

	path = gtk_tree_path_new_from_string (path_string);

	gtk_widget_get_pointer (widget, &mouse_x, &mouse_y);
	gtk_tree_view_widget_to_tree_coords (GTK_TREE_VIEW (widget),
					     mouse_x,
					     mouse_y,
					     &mouse_x,
					     &mouse_y);

	/* check if click is within the cell */
	if (mouse_x - cell_area->x >= 0
	    && mouse_x - cell_area->x <= cell_area->width) {
		in_cell = TRUE;
	} else {
		in_cell = FALSE;
	}

	if (! in_cell) {
		return FALSE;
	}

#if 0
	if (gtk_tree_path_get_depth (path) > 1) {
		gtk_tree_path_free (path);
		return TRUE;
	}
#endif

	settings = gtk_widget_get_settings (GTK_WIDGET (widget));
	if (g_object_class_find_property (G_OBJECT_GET_CLASS (settings), "gtk-enable-animations")) {
		g_object_get (settings,
			      "gtk-enable-animations", &animate,
			      NULL);
	} else {
		animate = FALSE;
	}

	if (gtk_tree_view_row_expanded (GTK_TREE_VIEW (widget), path)) {
		gtk_tree_view_collapse_row (GTK_TREE_VIEW (widget), path);
		expanding = FALSE;
	} else {
		gtk_tree_view_expand_row (GTK_TREE_VIEW (widget), path, FALSE);
		expanding = TRUE;
	}

	if (animate) {
		gossip_cell_renderer_expander_start_animation (expander,
							       GTK_TREE_VIEW (widget),
							       path,
							       expanding,
							       background_area);
	}

	gtk_tree_path_free (path);

	return TRUE;
}
