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
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <gtk/gtk.h>

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
static void     gossip_cell_renderer_expander_get_size     (GtkCellRenderer                 *cell,
							    GtkWidget                       *widget,
							    const GdkRectangle              *cell_area,
							    gint                            *x_offset,
							    gint                            *y_offset,
							    gint                            *width,
							    gint                            *height);
static void     gossip_cell_renderer_expander_render       (GtkCellRenderer                 *cell,
							    cairo_t			    *cr,
							    GtkWidget                       *widget,
							    const GdkRectangle              *background_area,
							    const GdkRectangle              *cell_area,
							    GtkCellRendererState             flags);
static gboolean gossip_cell_renderer_expander_activate     (GtkCellRenderer                 *cell,
							    GdkEvent                        *event,
							    GtkWidget                       *widget,
							    const gchar                     *path,
							    const GdkRectangle              *background_area,
							    const GdkRectangle              *cell_area,
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
	gint                 expander_size;

	guint                activatable : 1;
	GtkExpanderStyle     expander_style;
};

G_DEFINE_TYPE (GossipCellRendererExpander, gossip_cell_renderer_expander, GTK_TYPE_CELL_RENDERER)

static void
gossip_cell_renderer_expander_init (GossipCellRendererExpander *expander)
{
	GossipCellRendererExpanderPriv *priv;

	priv = GET_PRIV (expander);

	priv->expander_size = 12;
	priv->activatable = TRUE;

	gtk_cell_renderer_set_padding (GTK_CELL_RENDERER (expander), 2, 2);
	g_object_set (expander,
		      "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
		      NULL);
}

static void
gossip_cell_renderer_expander_class_init (GossipCellRendererExpanderClass *klass)
{
	GObjectClass         *object_class;
	GtkCellRendererClass *cell_class;

	object_class  = G_OBJECT_CLASS (klass);
	cell_class = GTK_CELL_RENDERER_CLASS (klass);

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

GtkCellRenderer *
gossip_cell_renderer_expander_new (void)
{
	return g_object_new (GOSSIP_TYPE_CELL_RENDERER_EXPANDER, NULL);
}

/* XXX implement preferred height/width/h-f-w/w-f-h */
static void
gossip_cell_renderer_expander_get_size (GtkCellRenderer *cell,
					GtkWidget       *widget,
					const GdkRectangle *cell_area,
					gint            *x_offset,
					gint            *y_offset,
					gint            *width,
					gint            *height)
{
	GossipCellRendererExpander     *expander;
	GossipCellRendererExpanderPriv *priv;
	gint                            xpad, ypad;
	gfloat                          xalign, yalign;

	expander = (GossipCellRendererExpander*) cell;
	priv = GET_PRIV (expander);
	gtk_cell_renderer_get_padding (cell, &xpad, &ypad);

	if (cell_area) {

		gtk_cell_renderer_get_alignment (cell, &xalign, &yalign);

		if (x_offset) {
			*x_offset = xalign * (cell_area->width - (priv->expander_size + (2 * xpad)));
			*x_offset = MAX (*x_offset, 0);
		}

		if (y_offset) {
			*y_offset = yalign * (cell_area->height - (priv->expander_size + (2 * ypad)));
			*y_offset = MAX (*y_offset, 0);
		}
	} else {
		if (x_offset)
			*x_offset = 0;

		if (y_offset)
			*y_offset = 0;
	}

	if (width)
		*width = xpad * 2 + priv->expander_size;

	if (height)
		*height = ypad * 2 + priv->expander_size;
}

static void
gossip_cell_renderer_expander_render (GtkCellRenderer      *cell,
				      cairo_t              *cr,
				      GtkWidget            *widget,
				      const GdkRectangle   *background_area,
				      const GdkRectangle   *cell_area,
				      GtkCellRendererState  flags)
{
	GossipCellRendererExpander     *expander;
	GossipCellRendererExpanderPriv *priv;
	GtkStyleContext                *style_context;
	gint                            x_offset, y_offset;
	gint                            xpad, ypad;
	GtkStateFlags                   state;

	expander = (GossipCellRendererExpander*) cell;
	priv = GET_PRIV (expander);

	gossip_cell_renderer_expander_get_size (cell, widget, cell_area,
						&x_offset, &y_offset,
						NULL, NULL);
	gtk_cell_renderer_get_padding (cell, &xpad, &ypad);

	style_context = gtk_widget_get_style_context (widget);

	gtk_style_context_save (style_context);
	gtk_style_context_add_class (style_context, GTK_STYLE_CLASS_EXPANDER);

	state = gtk_cell_renderer_get_state (cell, widget, flags);

	if (priv->expander_style == GTK_EXPANDER_COLLAPSED) {
		state |= GTK_STATE_FLAG_NORMAL;
	} else {
#if GTK_CHECK_VERSION(3,13,7)
		state |= GTK_STATE_FLAG_CHECKED;
#else
		state |= GTK_STATE_FLAG_ACTIVE;
#endif
	}

	gtk_style_context_set_state (style_context, state);

	gtk_render_expander (style_context,
			     cr,
			     cell_area->x + x_offset + xpad,
			     cell_area->y + y_offset + ypad,
			     priv->expander_size,
			     priv->expander_size);

	gtk_style_context_restore (style_context);
}

static gboolean
gossip_cell_renderer_expander_activate (GtkCellRenderer      *cell,
					GdkEvent             *event,
					GtkWidget            *widget,
					const gchar          *path_string,
					const GdkRectangle   *background_area,
					const GdkRectangle   *cell_area,
					GtkCellRendererState  flags)
{
	GossipCellRendererExpanderPriv *priv;
	GtkTreePath                    *path;
	gboolean                        in_cell;
	int                             mouse_x;
	int                             mouse_y;

	priv = GET_PRIV (cell);

	if (!GTK_IS_TREE_VIEW (widget) || !priv->activatable)
		return FALSE;

	path = gtk_tree_path_new_from_string (path_string);

	gdk_window_get_device_position (gtk_widget_get_window (widget),
					gdk_event_get_device (event),
					&mouse_x,
					&mouse_y,
					NULL);
	gtk_tree_view_convert_widget_to_bin_window_coords (GTK_TREE_VIEW (widget),
							   mouse_x, mouse_y,
							   &mouse_x, &mouse_y);

	/* check if click is within the cell */
	if (mouse_x - cell_area->x >= 0
	    && mouse_x - cell_area->x <= cell_area->width) {
		in_cell = TRUE;
	} else {
		in_cell = FALSE;
	}

	if (! in_cell) {
		gtk_tree_path_free (path);
		return FALSE;
	}

	if (gtk_tree_view_row_expanded (GTK_TREE_VIEW (widget), path)) {
		gtk_tree_view_collapse_row (GTK_TREE_VIEW (widget), path);
	} else {
		gtk_tree_view_expand_row (GTK_TREE_VIEW (widget), path, FALSE);
	}

	gtk_tree_path_free (path);

	return TRUE;
}
