/* eggwrapbox.c
 * Copyright (C) 2007-2010 Openismus GmbH
 *
 * Authors:
 *      Tristan Van Berkom <tristanvb@openismus.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */


/**
 * SECTION:eggwrapbox
 * @Short_Description: A container that wraps its children
 * @Title: EggWrapBox
 *
 * #EggWrapBox positions child widgets in sequence according to its
 * orientation. For instance, with the horizontal orientation, the widgets
 * will be arranged from left to right, starting a new row under the
 * previous row when necessary. Reducing the width in this case will
 * require more rows, so a larger height will be requested.
 *
 * Likewise, with the vertical orientation, the widgets will be arranged
 * from top to bottom, starting a new column to the right when necessary.
 * Reducing the height will require more columns, so a larger width will be
 * requested.
 *
 * Unlike a GtkTable, the child widgets do not need to align in a grid.
 */

#include <config.h>

#include "eggwrapbox.h"
#include "eggwrapbox-enums.h"

#define P_(msgid) (msgid)
#define GTK_PARAM_READWRITE (G_PARAM_READABLE | G_PARAM_WRITABLE)


typedef struct _EggWrapBoxChild  EggWrapBoxChild;

enum {
  PROP_0,
  PROP_ORIENTATION,
  PROP_ALLOCATION_MODE,
  PROP_HORIZONTAL_SPREADING,
  PROP_VERTICAL_SPREADING,
  PROP_HORIZONTAL_SPACING,
  PROP_VERTICAL_SPACING,
  PROP_MINIMUM_LINE_CHILDREN,
  PROP_NATURAL_LINE_CHILDREN
};

enum
{
  CHILD_PROP_0,
  CHILD_PROP_PACKING
};

struct _EggWrapBoxPrivate
{
  GtkOrientation        orientation;
  EggWrapAllocationMode mode;
  EggWrapBoxSpreading   horizontal_spreading;
  EggWrapBoxSpreading   vertical_spreading;

  guint16               vertical_spacing;
  guint16               horizontal_spacing;

  guint16               minimum_line_children;
  guint16               natural_line_children;

  GList                *children;
};

struct _EggWrapBoxChild
{
  GtkWidget        *widget;

  EggWrapBoxPacking packing;
};

/* GObjectClass */
static void egg_wrap_box_get_property         (GObject             *object,
                                               guint                prop_id,
                                               GValue              *value,
                                               GParamSpec          *pspec);
static void egg_wrap_box_set_property         (GObject             *object,
                                               guint                prop_id,
                                               const GValue        *value,
                                               GParamSpec          *pspec);

/* GtkWidgetClass */
static void egg_wrap_box_size_allocate        (GtkWidget           *widget,
                                               GtkAllocation       *allocation);

/* GtkContainerClass */
static void egg_wrap_box_add                  (GtkContainer        *container,
                                               GtkWidget           *widget);
static void egg_wrap_box_remove               (GtkContainer        *container,
                                               GtkWidget           *widget);
static void egg_wrap_box_forall               (GtkContainer        *container,
                                               gboolean             include_internals,
                                               GtkCallback          callback,
                                               gpointer             callback_data);
static void egg_wrap_box_set_child_property   (GtkContainer        *container,
                                               GtkWidget           *child,
                                               guint                property_id,
                                               const GValue        *value,
                                               GParamSpec          *pspec);
static void egg_wrap_box_get_child_property   (GtkContainer        *container,
                                               GtkWidget           *child,
                                               guint                property_id,
                                               GValue              *value,
                                               GParamSpec          *pspec);
static GType egg_wrap_box_child_type          (GtkContainer        *container);


/* GtkWidget      */
static GtkSizeRequestMode egg_wrap_box_get_request_mode (GtkWidget           *widget);
static void egg_wrap_box_get_preferred_width            (GtkWidget           *widget,
                                                         gint                *minimum_size,
                                                         gint                *natural_size);
static void egg_wrap_box_get_preferred_height           (GtkWidget           *widget,
                                                         gint                *minimum_size,
                                                         gint                *natural_size);
static void egg_wrap_box_get_preferred_height_for_width (GtkWidget           *box,
                                                         gint                 width,
                                                         gint                *minimum_height,
                                                         gint                *natural_height);
static void egg_wrap_box_get_preferred_width_for_height (GtkWidget           *box,
                                                         gint                 width,
                                                         gint                *minimum_height,
                                                         gint                *natural_height);


G_DEFINE_TYPE_WITH_CODE (EggWrapBox, egg_wrap_box, GTK_TYPE_CONTAINER,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_ORIENTABLE, NULL))


#define ORIENTATION_SPREADING(box)					\
  (((EggWrapBox *)(box))->priv->orientation == GTK_ORIENTATION_HORIZONTAL ? \
   ((EggWrapBox *)(box))->priv->horizontal_spreading :			\
   ((EggWrapBox *)(box))->priv->vertical_spreading)

#define OPPOSING_ORIENTATION_SPREADING(box)				\
  (((EggWrapBox *)(box))->priv->orientation == GTK_ORIENTATION_HORIZONTAL ? \
   ((EggWrapBox *)(box))->priv->vertical_spreading :			\
   ((EggWrapBox *)(box))->priv->horizontal_spreading)



static void
egg_wrap_box_class_init (EggWrapBoxClass *class)
{
  GObjectClass      *gobject_class    = G_OBJECT_CLASS (class);
  GtkWidgetClass    *widget_class     = GTK_WIDGET_CLASS (class);
  GtkContainerClass *container_class  = GTK_CONTAINER_CLASS (class);

  gobject_class->get_property         = egg_wrap_box_get_property;
  gobject_class->set_property         = egg_wrap_box_set_property;

  widget_class->size_allocate         = egg_wrap_box_size_allocate;
  widget_class->get_request_mode      = egg_wrap_box_get_request_mode;
  widget_class->get_preferred_width   = egg_wrap_box_get_preferred_width;
  widget_class->get_preferred_height  = egg_wrap_box_get_preferred_height;
  widget_class->get_preferred_height_for_width = egg_wrap_box_get_preferred_height_for_width;
  widget_class->get_preferred_width_for_height = egg_wrap_box_get_preferred_width_for_height;

  container_class->add                = egg_wrap_box_add;
  container_class->remove             = egg_wrap_box_remove;
  container_class->forall             = egg_wrap_box_forall;
  container_class->child_type         = egg_wrap_box_child_type;
  container_class->set_child_property = egg_wrap_box_set_child_property;
  container_class->get_child_property = egg_wrap_box_get_child_property;
  gtk_container_class_handle_border_width (container_class);

  /* GObjectClass properties */
  g_object_class_override_property (gobject_class, PROP_ORIENTATION, "orientation");

  /**
   * EggWrapBox:allocation-mode:
   *
   * The #EggWrapAllocationMode to use.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_ALLOCATION_MODE,
                                   g_param_spec_uint ("allocation-mode",
                                                      P_("Allocation Mode"),
                                                      P_("The allocation mode to use"),
                                                      0, EGG_WRAP_ALLOCATE_HOMOGENEOUS,
                                                      EGG_WRAP_ALLOCATE_FREE,
                                                      GTK_PARAM_READWRITE));

  /**
   * EggWrapBox:horizontal-spreading:
   *
   * The #EggWrapBoxSpreading to used to define what is done with extra
   * space in a given orientation.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_HORIZONTAL_SPREADING,
                                   g_param_spec_uint ("horizontal-spreading",
                                                      P_("Horizontal Spreading"),
                                                      P_("The spreading mode to use horizontally"),
                                                      0, EGG_WRAP_BOX_SPREAD_EXPAND,
						      EGG_WRAP_BOX_SPREAD_START,
                                                      GTK_PARAM_READWRITE));

  /**
   * EggWrapBox:vertical-spreading:
   *
   * The #EggWrapBoxSpreading to used to define what is done with extra
   * space in a given orientation.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_VERTICAL_SPREADING,
                                   g_param_spec_uint ("vertical-spreading",
                                                      P_("Vertical Spreading"),
                                                      P_("The spreading mode to use vertically"),
						      0, EGG_WRAP_BOX_SPREAD_EXPAND,
                                                      EGG_WRAP_BOX_SPREAD_START,
                                                      GTK_PARAM_READWRITE));


  /**
   * EggWrapBox:minimum-line-children:
   *
   * The minimum number of children to allocate consecutively in the given orientation.
   *
   * <note><para>Setting the minimum children per line ensures
   * that a reasonably small height will be requested
   * for the overall minimum width of the box.</para></note>
   *
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MINIMUM_LINE_CHILDREN,
                                   g_param_spec_uint ("minimum-line-children",
                                                      P_("Minimum Line Children"),
                                                      P_("The minimum number of children to allocate "
                                                        "consecutively in the given orientation."),
                                                      0,
                                                      65535,
                                                      0,
                                                      GTK_PARAM_READWRITE));

  /**
   * EggWrapBox:natural-line-children:
   *
   * The maximum amount of children to request space for consecutively in the given orientation.
   *
   */
  g_object_class_install_property (gobject_class,
                                   PROP_NATURAL_LINE_CHILDREN,
                                   g_param_spec_uint ("natural-line-children",
                                                      P_("Natural Line Children"),
                                                      P_("The maximum amount of children to request space for "
                                                        "consecutively in the given orientation."),
                                                      0,
                                                      65535,
                                                      0,
                                                      GTK_PARAM_READWRITE));

  /**
   * EggWrapBox:vertical-spacing:
   *
   * The amount of vertical space between two children.
   *
   */
  g_object_class_install_property (gobject_class,
                                   PROP_VERTICAL_SPACING,
                                   g_param_spec_uint ("vertical-spacing",
                                                     P_("Vertical spacing"),
                                                     P_("The amount of vertical space between two children"),
                                                     0,
                                                     65535,
                                                     0,
                                                     GTK_PARAM_READWRITE));

  /**
   * EggWrapBox:horizontal-spacing:
   *
   * The amount of horizontal space between two children.
   *
   */
  g_object_class_install_property (gobject_class,
                                   PROP_HORIZONTAL_SPACING,
                                   g_param_spec_uint ("horizontal-spacing",
                                                     P_("Horizontal spacing"),
                                                     P_("The amount of horizontal space between two children"),
                                                     0,
                                                     65535,
                                                     0,
                                                     GTK_PARAM_READWRITE));

  /* GtkContainerClass child properties */

  /**
   * EggWrapBox:packing:
   *
   * The #EggWrapBoxPacking options to specify how to pack a child into the box.
   */
  gtk_container_class_install_child_property (container_class,
                                              CHILD_PROP_PACKING,
                                              g_param_spec_flags
                                              ("packing",
                                               P_("Packing"),
                                               P_("The packing options to use for this child"),
                                               EGG_TYPE_WRAP_BOX_PACKING, 0,
                                               GTK_PARAM_READWRITE));

  g_type_class_add_private (class, sizeof (EggWrapBoxPrivate));
}

static void
egg_wrap_box_init (EggWrapBox *box)
{
  EggWrapBoxPrivate *priv;

  box->priv = priv =
    G_TYPE_INSTANCE_GET_PRIVATE (box, EGG_TYPE_WRAP_BOX, EggWrapBoxPrivate);

  priv->orientation          = GTK_ORIENTATION_HORIZONTAL;
  priv->mode                 = EGG_WRAP_ALLOCATE_FREE;
  priv->horizontal_spreading = EGG_WRAP_BOX_SPREAD_START;
  priv->vertical_spreading   = EGG_WRAP_BOX_SPREAD_START;
  priv->horizontal_spacing   = 0;
  priv->vertical_spacing     = 0;
  priv->children             = NULL;

  gtk_widget_set_has_window (GTK_WIDGET (box), FALSE);
}

/*****************************************************
 *                  GObectClass                      *
 *****************************************************/
static void
egg_wrap_box_get_property (GObject      *object,
                           guint         prop_id,
                           GValue       *value,
                           GParamSpec   *pspec)
{
  EggWrapBox        *box  = EGG_WRAP_BOX (object);
  EggWrapBoxPrivate *priv = box->priv;

  switch (prop_id)
    {
    case PROP_ORIENTATION:
      g_value_set_enum (value, priv->orientation);
      break;
    case PROP_ALLOCATION_MODE:
      g_value_set_uint (value, priv->mode);
      break;
    case PROP_HORIZONTAL_SPREADING:
      g_value_set_uint (value, priv->horizontal_spreading);
      break;
    case PROP_VERTICAL_SPREADING:
      g_value_set_uint (value, priv->vertical_spreading);
      break;
    case PROP_HORIZONTAL_SPACING:
      g_value_set_uint (value, priv->horizontal_spacing);
      break;
    case PROP_VERTICAL_SPACING:
      g_value_set_uint (value, priv->vertical_spacing);
      break;
    case PROP_MINIMUM_LINE_CHILDREN:
      g_value_set_uint (value, priv->minimum_line_children);
      break;
    case PROP_NATURAL_LINE_CHILDREN:
      g_value_set_uint (value, priv->natural_line_children);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
egg_wrap_box_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  EggWrapBox        *box = EGG_WRAP_BOX (object);
  EggWrapBoxPrivate *priv   = box->priv;

  switch (prop_id)
    {
    case PROP_ORIENTATION:
      priv->orientation = g_value_get_enum (value);

      /* Re-box the children in the new orientation */
      gtk_widget_queue_resize (GTK_WIDGET (box));
      break;
    case PROP_ALLOCATION_MODE:
      egg_wrap_box_set_allocation_mode (box, g_value_get_uint (value));
      break;
    case PROP_HORIZONTAL_SPREADING:
      egg_wrap_box_set_horizontal_spreading (box, g_value_get_uint (value));
      break;
    case PROP_VERTICAL_SPREADING:
      egg_wrap_box_set_vertical_spreading (box, g_value_get_uint (value));
      break;
    case PROP_HORIZONTAL_SPACING:
      egg_wrap_box_set_horizontal_spacing (box, g_value_get_uint (value));
      break;
    case PROP_VERTICAL_SPACING:
      egg_wrap_box_set_vertical_spacing (box, g_value_get_uint (value));
      break;
    case PROP_MINIMUM_LINE_CHILDREN:
      egg_wrap_box_set_minimum_line_children (box, g_value_get_uint (value));
      break;
    case PROP_NATURAL_LINE_CHILDREN:
      egg_wrap_box_set_natural_line_children (box, g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/*****************************************************
 *                 GtkWidgetClass                    *
 *****************************************************/

static gint
get_visible_children (EggWrapBox  *box)
{
  EggWrapBoxPrivate *priv = box->priv;
  GList             *list;
  gint               i = 0;

  for (list = priv->children; list; list = list->next)
    {
      EggWrapBoxChild *child = list->data;

      if (!gtk_widget_get_visible (child->widget))
        continue;

      i++;
    }

  return i;
}

static gint
get_visible_expand_children (EggWrapBox     *box,
                             GtkOrientation  orientation,
                             GList          *cursor,
                             gint            n_visible)
{
  GList *list;
  gint   i, expand_children = 0;

  for (i = 0, list = cursor; (n_visible > 0 ? i < n_visible : TRUE) && list; list = list->next)
    {
      EggWrapBoxChild *child = list->data;

      if (!gtk_widget_get_visible (child->widget))
        continue;

      if ((orientation == GTK_ORIENTATION_HORIZONTAL && (child->packing & EGG_WRAP_BOX_H_EXPAND) != 0) ||
          (orientation == GTK_ORIENTATION_VERTICAL   && (child->packing & EGG_WRAP_BOX_V_EXPAND) != 0))
        expand_children++;

      i++;
    }

  return expand_children;
}

/* Used in columned modes where all items share at least their
 * equal widths or heights
 */
static void
get_average_item_size (EggWrapBox      *box,
                       GtkOrientation   orientation,
                       gint            *min_size,
                       gint            *nat_size)
{
  EggWrapBoxPrivate *priv = box->priv;
  GList             *list;
  gint               max_min_size = 0;
  gint               max_nat_size = 0;

  for (list = priv->children; list; list = list->next)
    {
      EggWrapBoxChild *child = list->data;
      gint             child_min, child_nat;

      if (!gtk_widget_get_visible (child->widget))
        continue;

      if (orientation == GTK_ORIENTATION_HORIZONTAL)
        gtk_widget_get_preferred_width (child->widget, &child_min, &child_nat);
      else
        gtk_widget_get_preferred_height (child->widget, &child_min, &child_nat);

      max_min_size = MAX (max_min_size, child_min);
      max_nat_size = MAX (max_nat_size, child_nat);
    }

  if (min_size)
    *min_size = max_min_size;

  if (nat_size)
    *nat_size = max_nat_size;
}


/* Gets the largest minimum/natural size for a given size
 * (used to get the largest item heights for a fixed item width and the opposite) */
static void
get_largest_size_for_opposing_orientation (EggWrapBox         *box,
                                           GtkOrientation      orientation,
                                           gint                item_size,
                                           gint               *min_item_size,
                                           gint               *nat_item_size)
{
  EggWrapBoxPrivate *priv = box->priv;
  GList             *list;
  gint               max_min_size = 0;
  gint               max_nat_size = 0;

  for (list = priv->children; list; list = list->next)
    {
      EggWrapBoxChild *child = list->data;
      gint             child_min, child_nat;

      if (!gtk_widget_get_visible (child->widget))
        continue;

      if (orientation == GTK_ORIENTATION_HORIZONTAL)
        gtk_widget_get_preferred_height_for_width (child->widget,
                                                         item_size,
                                                         &child_min, &child_nat);
      else
        gtk_widget_get_preferred_width_for_height (child->widget,
                                                   item_size,
                                                   &child_min, &child_nat);

      max_min_size = MAX (max_min_size, child_min);
      max_nat_size = MAX (max_nat_size, child_nat);
    }

  if (min_item_size)
    *min_item_size = max_min_size;

  if (nat_item_size)
    *nat_item_size = max_nat_size;
}


/* Gets the largest minimum/natural size on a single line for a given size
 * (used to get the largest line heights for a fixed item width and the opposite
 * while itterating over a list of children, note the new index is returned) */
static GList *
get_largest_size_for_line_in_opposing_orientation (EggWrapBox       *box,
                                                   GtkOrientation    orientation,
                                                   GList            *cursor,
                                                   gint              line_length,
                                                   GtkRequestedSize *item_sizes,
                                                   gint              extra_pixels,
                                                   gint             *min_item_size,
                                                   gint             *nat_item_size)
{
  GList  *list;
  gint    max_min_size = 0;
  gint    max_nat_size = 0;
  gint    i;

  for (list = cursor, i = 0; list && i < line_length; list = list->next)
    {
      EggWrapBoxChild *child = list->data;
      gint             child_min, child_nat, this_item_size;

      if (!gtk_widget_get_visible (child->widget))
        continue;

      /* Distribute the extra pixels to the first children in the line
       * (could be fancier and spread them out more evenly) */
      this_item_size = item_sizes[i].minimum_size;
      if (extra_pixels > 0 && ORIENTATION_SPREADING (box) == EGG_WRAP_BOX_SPREAD_EXPAND)
        {
          this_item_size++;
          extra_pixels--;
        }

      if (orientation == GTK_ORIENTATION_HORIZONTAL)
        gtk_widget_get_preferred_height_for_width (child->widget,
                                                   this_item_size,
                                                   &child_min, &child_nat);
      else
        gtk_widget_get_preferred_width_for_height (child->widget,
                                                   this_item_size,
                                                   &child_min, &child_nat);

      max_min_size = MAX (max_min_size, child_min);
      max_nat_size = MAX (max_nat_size, child_nat);

      i++;
    }

  if (min_item_size)
    *min_item_size = max_min_size;

  if (nat_item_size)
    *nat_item_size = max_nat_size;

  /* Return next item in the list */
  return list;
}


/* Gets the largest minimum/natural size on a single line for a given allocated line size
 * (used to get the largest line heights for a width in pixels and the opposite
 * while itterating over a list of children, note the new index is returned) */
static GList *
get_largest_size_for_free_line_in_opposing_orientation (EggWrapBox      *box,
                                                        GtkOrientation   orientation,
                                                        GList           *cursor,
                                                        gint             min_items,
                                                        gint             avail_size,
                                                        gint            *min_item_size,
                                                        gint            *nat_item_size,
                                                        gint            *extra_pixels,
                                                        GArray         **ret_array)
{
  EggWrapBoxPrivate *priv = box->priv;
  GtkRequestedSize  *sizes;
  GList             *list;
  GArray            *array;
  gint               max_min_size = 0;
  gint               max_nat_size = 0;
  gint               i, size = avail_size;
  gint               line_length, spacing;
  gint               expand_children = 0;
  gint               expand_per_child;
  gint               expand_remainder;

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    spacing = priv->horizontal_spacing;
  else
    spacing = priv->vertical_spacing;

  /* First determine the length of this line in items (how many items fit) */
  for (i = 0, list = cursor; size > 0 && list; list = list->next)
    {
      EggWrapBoxChild *child = list->data;
      gint             child_size;

      if (!gtk_widget_get_visible (child->widget))
        continue;

      if (orientation == GTK_ORIENTATION_HORIZONTAL)
        gtk_widget_get_preferred_width (child->widget, NULL, &child_size);
      else
        gtk_widget_get_preferred_height (child->widget, NULL, &child_size);

      if (i > 0)
        child_size += spacing;

      if (size - child_size >= 0)
        size -= child_size;
      else
        break;

      i++;
    }

  line_length = MAX (min_items, i);
  size        = avail_size;

  /* Collect the sizes of the items on this line */
  array = g_array_new (0, TRUE, sizeof (GtkRequestedSize));

  for (i = 0, list = cursor; i < line_length && list; list = list->next)
    {
      EggWrapBoxChild  *child = list->data;
      GtkRequestedSize  requested;

      if (!gtk_widget_get_visible (child->widget))
        continue;

      requested.data = child;
      if (orientation == GTK_ORIENTATION_HORIZONTAL)
        gtk_widget_get_preferred_width (child->widget,
                                        &requested.minimum_size,
                                        &requested.natural_size);
      else
        gtk_widget_get_preferred_height (child->widget,
                                         &requested.minimum_size,
                                         &requested.natural_size);

      if (i > 0)
        size -= spacing;

      size -= requested.minimum_size;

      g_array_append_val (array, requested);

      i++;
    }

  sizes = (GtkRequestedSize *)array->data;
  size  = gtk_distribute_natural_allocation (size, array->len, sizes);

  if (extra_pixels)
    *extra_pixels = size;

  /* Cut out any expand space if we're not distributing any */
  if (ORIENTATION_SPREADING (box) != EGG_WRAP_BOX_SPREAD_EXPAND)
    size = 0;

  /* Count how many children are going to expand... */
  expand_children = get_visible_expand_children (box, orientation,
                                                 cursor, line_length);

  /* If no child prefers to expand, they all get some expand space */
  if (expand_children == 0)
    {
      expand_per_child = size / line_length;
      expand_remainder = size % line_length;
    }
  else
    {
      expand_per_child = size / expand_children;
      expand_remainder = size % expand_children;
    }

  /* Now add the remaining expand space and get the collective size of this line
   * in the opposing orientation */
  for (i = 0, list = cursor; i < line_length && list; list = list->next)
    {
      EggWrapBoxChild *child = list->data;
      gint child_min, child_nat;

      if (!gtk_widget_get_visible (child->widget))
        continue;

      g_assert (child == sizes[i].data);

      if ((orientation == GTK_ORIENTATION_HORIZONTAL && (child->packing & EGG_WRAP_BOX_H_EXPAND) != 0) ||
          (orientation == GTK_ORIENTATION_VERTICAL   && (child->packing & EGG_WRAP_BOX_V_EXPAND) != 0) ||
          expand_children == 0)
        {
          sizes[i].minimum_size += expand_per_child;
          if (expand_remainder)
            {
              sizes[i].minimum_size++;
              expand_remainder--;
            }
        }

      if (orientation == GTK_ORIENTATION_HORIZONTAL)
        gtk_widget_get_preferred_height_for_width (child->widget,
                                                   sizes[i].minimum_size,
                                                   &child_min, &child_nat);
      else
        gtk_widget_get_preferred_width_for_height (child->widget,
                                                   sizes[i].minimum_size,
                                                   &child_min, &child_nat);

      max_min_size = MAX (max_min_size, child_min);
      max_nat_size = MAX (max_nat_size, child_nat);

      i++;
    }

  if (ret_array)
    *ret_array = array;
  else
    g_array_free (array, TRUE);

  if (min_item_size)
    *min_item_size = max_min_size;

  if (nat_item_size)
    *nat_item_size = max_nat_size;

  /* Return the next item */
  return list;
}

static void
allocate_child (EggWrapBox      *box,
                EggWrapBoxChild *child,
                gint             item_offset,
                gint             line_offset,
                gint             item_size,
                gint             line_size)
{
  EggWrapBoxPrivate  *priv   = box->priv;
  GtkAllocation       widget_allocation;
  GtkAllocation       child_allocation;

  gtk_widget_get_allocation (GTK_WIDGET (box), &widget_allocation);

  if (priv->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      child_allocation.x      = widget_allocation.x + item_offset;
      child_allocation.y      = widget_allocation.y + line_offset;
      child_allocation.width  = item_size;
      child_allocation.height = line_size;
    }
  else /* GTK_ORIENTATION_VERTICAL */
    {
      child_allocation.x      = widget_allocation.x + line_offset;
      child_allocation.y      = widget_allocation.y + item_offset;
      child_allocation.width  = line_size;
      child_allocation.height = item_size;
    }

  gtk_widget_size_allocate (child->widget, &child_allocation);
}

/* fit_aligned_item_requests() helper */
static gint
gather_aligned_item_requests (EggWrapBox       *box,
                              GtkOrientation    orientation,
                              gint              line_length,
                              gint              item_spacing,
                              gint              n_children,
                              GtkRequestedSize *item_sizes)
{
  EggWrapBoxPrivate *priv   = box->priv;
  GList             *list;
  gint               i;
  gint               extra_items, natural_line_size = 0;

  extra_items = n_children % line_length;

  for (list = priv->children, i = 0; list; list = list->next, i++)
    {
      EggWrapBoxChild *child = list->data;
      gint             child_min, child_nat;
      gint             position;

      if (!gtk_widget_get_visible (child->widget))
        continue;

      if (orientation == GTK_ORIENTATION_HORIZONTAL)
        gtk_widget_get_preferred_width (child->widget,
                                        &child_min, &child_nat);
      else
        gtk_widget_get_preferred_height (child->widget,
                                         &child_min, &child_nat);

      /* Get the index and push it over for the last line when spreading to the end */
      position = i % line_length;

      if (ORIENTATION_SPREADING (box) == EGG_WRAP_BOX_SPREAD_END && i >= n_children - extra_items)
        position += line_length - extra_items;

      /* Round up the size of every column/row */
      item_sizes[position].minimum_size = MAX (item_sizes[position].minimum_size, child_min);
      item_sizes[position].natural_size = MAX (item_sizes[position].natural_size, child_nat);
    }

  for (i = 0; i < line_length; i++)
    natural_line_size += item_sizes[i].natural_size;

  natural_line_size += (line_length - 1) * item_spacing;

  return natural_line_size;
}

static GtkRequestedSize *
fit_aligned_item_requests (EggWrapBox       *box, 
                           GtkOrientation    orientation, 
                           gint              avail_size,
                           gint              item_spacing,
                           gint             *line_length, /* in-out */
                           gint              n_children)
{
  GtkRequestedSize  *sizes, *try_sizes;
  gint               try_line_size, try_length;

  sizes = g_new0 (GtkRequestedSize, *line_length);

  /* get the sizes for the initial guess */
  try_line_size = 
    gather_aligned_item_requests (box, orientation, *line_length, item_spacing, n_children, sizes);

  /* Try columnizing the whole thing and adding an item to the end of the line;
   * try to fit as many columns into the available size as possible */
  for (try_length = *line_length + 1; try_line_size < avail_size; try_length++)
    {
      try_sizes     = g_new0 (GtkRequestedSize, try_length);
      try_line_size = gather_aligned_item_requests (box, orientation, try_length, item_spacing, 
                                                    n_children, try_sizes);

      if (try_line_size <= avail_size)
        {
          *line_length = try_length;

          g_free (sizes);
          sizes = try_sizes;
        }
      else
        {
          /* oops, this one failed; stick to the last size that fit and then return */
          g_free (try_sizes);
          break;
        }
    }

  return sizes;
}


typedef struct {
  GArray *requested;
  gint    extra_pixels;
} AllocatedLine;

static void
egg_wrap_box_size_allocate (GtkWidget     *widget,
                            GtkAllocation *allocation)
{
  EggWrapBox         *box  = EGG_WRAP_BOX (widget);
  EggWrapBoxPrivate  *priv = box->priv;
  gint                avail_size, avail_other_size, min_items, item_spacing, line_spacing;
  EggWrapBoxSpreading item_spreading;
  EggWrapBoxSpreading line_spreading;

  gtk_widget_set_allocation (widget, allocation);

  min_items = MAX (1, priv->minimum_line_children);

  if (priv->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      avail_size       = allocation->width;
      avail_other_size = allocation->height;
      item_spacing     = priv->horizontal_spacing;
      line_spacing     = priv->vertical_spacing;
    }
  else /* GTK_ORIENTATION_VERTICAL */
    {
      avail_size       = allocation->height;
      avail_other_size = allocation->width;
      item_spacing     = priv->vertical_spacing;
      line_spacing     = priv->horizontal_spacing;
    }

  item_spreading = ORIENTATION_SPREADING (box);
  line_spreading    = OPPOSING_ORIENTATION_SPREADING (box);


  /*********************************************************
   * Deal with ALIGNED/HOMOGENEOUS modes first, start with * 
   * initial guesses at item/line sizes                    *
   *********************************************************/
  if (priv->mode == EGG_WRAP_ALLOCATE_ALIGNED ||
      priv->mode == EGG_WRAP_ALLOCATE_HOMOGENEOUS)
    {
      GtkRequestedSize *line_sizes = NULL;
      GtkRequestedSize *item_sizes = NULL;
      GList *list;
      gint   min_item_size, nat_item_size;
      gint   line_length;
      gint   item_size = 0;
      gint   line_size = 0, min_fixed_line_size = 0, nat_fixed_line_size = 0;
      gint   line_offset, item_offset, n_children, n_lines, line_count;
      gint   extra_pixels, extra_per_item = 0, extra_extra = 0;
      gint   extra_line_pixels, extra_per_line = 0, extra_line_extra = 0;
      gint   i, this_line_size;

      get_average_item_size (box, priv->orientation, &min_item_size, &nat_item_size);

      /* By default wrap at the natural item width */
      line_length = avail_size / (nat_item_size + item_spacing);

      /* After the above aproximation, check if we cant fit one more on the line */
      if (line_length * item_spacing + (line_length + 1) * nat_item_size <= avail_size)
        line_length++;

      /* Its possible we were allocated just less than the natural width of the
       * minimum item wrap length */
      line_length = MAX (min_items, line_length);

      /* Get how many lines we'll be needing to wrap */
      n_children = get_visible_children (box);

      /* Here we just use the largest height-for-width and use that for the height
       * of all lines */
      if (priv->mode == EGG_WRAP_ALLOCATE_HOMOGENEOUS)
        {
          n_lines    = n_children / line_length;
          if ((n_children % line_length) > 0)
            n_lines++;
          
          n_lines = MAX (n_lines, 1);

          /* Now we need the real item allocation size */
          item_size = (avail_size - (line_length - 1) * item_spacing) / line_length;
          
          /* Cut out the expand space if we're not distributing any */
          if (item_spreading != EGG_WRAP_BOX_SPREAD_EXPAND)
            item_size = MIN (item_size, nat_item_size);
          
          get_largest_size_for_opposing_orientation (box, priv->orientation, item_size,
                                                     &min_fixed_line_size,
                                                     &nat_fixed_line_size);

          /* resolve a fixed 'line_size' */
          line_size = (avail_other_size - (n_lines - 1) * line_spacing) / n_lines;

	  if (line_spreading != EGG_WRAP_BOX_SPREAD_EXPAND)
	    line_size = MIN (line_size, nat_fixed_line_size);

          /* Get the real extra pixels incase of EGG_WRAP_BOX_SPREAD_START lines */
          extra_pixels      = avail_size       - (line_length - 1) * item_spacing - item_size * line_length;
	  extra_line_pixels = avail_other_size - (n_lines - 1)     * line_spacing - line_size * n_lines;
        }
      else /* EGG_WRAP_ALLOCATE_ALIGNED */
        {
          GList            *list;
          gboolean          first_line = TRUE;

          /* Find the amount of columns that can fit aligned into the available space
           * and collect their requests.
           */
          item_sizes = fit_aligned_item_requests (box, priv->orientation, avail_size,
                                                  item_spacing, &line_length, n_children);

          /* Calculate the number of lines after determining the final line_length */
          n_lines    = n_children / line_length;
          if ((n_children % line_length) > 0)
            n_lines++;
          
          n_lines = MAX (n_lines, 1);
          line_sizes = g_new0 (GtkRequestedSize, n_lines);

          /* Get the available remaining size */
          avail_size -= (line_length - 1) * item_spacing;
          for (i = 0; i < line_length; i++)
            avail_size -= item_sizes[i].minimum_size;

          /* Perform a natural allocation on the columnized items and get the remaining pixels */
          extra_pixels = gtk_distribute_natural_allocation (avail_size, line_length, item_sizes);

          /* Now that we have the size of each column of items find the size of each individual 
           * line based on the aligned item sizes.
           */
          for (i = 0, list = priv->children; list != NULL; i++)
            {

              list =
                get_largest_size_for_line_in_opposing_orientation (box, priv->orientation,
                                                                   list, line_length,
                                                                   item_sizes, extra_pixels,
                                                                   &line_sizes[i].minimum_size,
                                                                   &line_sizes[i].natural_size);


              /* Its possible a line is made of completely invisible children */
              if (line_sizes[i].natural_size > 0)
                {
                  if (first_line)
                    first_line = FALSE;
                  else
                    avail_other_size -= line_spacing;

                  avail_other_size -= line_sizes[i].minimum_size;

                  line_sizes[i].data = GINT_TO_POINTER (i);
                }
            }

          /* Distribute space among lines naturally */
          extra_line_pixels = gtk_distribute_natural_allocation (avail_other_size, n_lines, line_sizes);
        }

      /*********************************************************
       * Initial sizes of items/lines guessed at this point,   * 
       * go on to distribute expand space if needed.           *
       *********************************************************/

      /* FIXME: This portion needs to consider which columns
       * and rows asked for expand space and distribute those
       * accordingly for the case of ALIGNED allocation.
       *
       * If at least one child in a column/row asked for expand;
       * we should make that row/column expand entirely.
       */

      /* Calculate expand space per item */
      if (item_spreading == EGG_WRAP_BOX_SPREAD_EVEN)
        {
          extra_per_item = extra_pixels / MAX (line_length -1, 1);
          extra_extra    = extra_pixels % MAX (line_length -1, 1);
        }
      else if (item_spreading == EGG_WRAP_BOX_SPREAD_EXPAND)
        {
          extra_per_item = extra_pixels / line_length;
          extra_extra    = extra_pixels % line_length;
        }

      /* Calculate expand space per line */
      if (line_spreading == EGG_WRAP_BOX_SPREAD_EVEN)
        {
          extra_per_line   = extra_line_pixels / MAX (n_lines -1, 1);
          extra_line_extra = extra_line_pixels % MAX (n_lines -1, 1);
        }
      else if (line_spreading == EGG_WRAP_BOX_SPREAD_EXPAND)
        {
          extra_per_line   = extra_line_pixels / n_lines;
          extra_line_extra = extra_line_pixels % n_lines;
        }

      /*********************************************************
       * Prepare item/line initial offsets and jump into the   *
       * real allocation loop.                                 *
       *********************************************************/
      line_offset = item_offset = 0;

      /* prepend extra space to item_offset/line_offset for SPREAD_END */
      if (item_spreading == EGG_WRAP_BOX_SPREAD_END)
        item_offset += extra_pixels;

      if (line_spreading == EGG_WRAP_BOX_SPREAD_END)
	line_offset += extra_line_pixels;

      /* Get the allocation size for the first line */
      if (priv->mode == EGG_WRAP_ALLOCATE_HOMOGENEOUS)
	this_line_size = line_size;
      else 
	{
	  this_line_size  = line_sizes[0].minimum_size;

          if (line_spreading == EGG_WRAP_BOX_SPREAD_EXPAND)
	    {
	      this_line_size += extra_per_line;

	      if (extra_line_extra > 0)
		this_line_size++;
	    }
	}

      for (i = 0, line_count = 0, list = priv->children; list; list = list->next)
        {
          EggWrapBoxChild *child = list->data;
          gint             position;
          gint             this_item_size;

          if (!gtk_widget_get_visible (child->widget))
            continue;

          /* Get item position */
          position = i % line_length;

          /* adjust the line_offset/count at the beginning of each new line */
          if (i > 0 && position == 0)
            {
	      /* Push the line_offset */
	      line_offset += this_line_size + line_spacing;

	      if (line_spreading == EGG_WRAP_BOX_SPREAD_EVEN)
		{
		  line_offset += extra_per_line;
		      
		  if (line_count < extra_line_extra)
		    line_offset++;
		}

              line_count++;

	      /* Get the new line size */
	      if (priv->mode == EGG_WRAP_ALLOCATE_HOMOGENEOUS)
		this_line_size = line_size;
	      else
		{
		  this_line_size = line_sizes[line_count].minimum_size;

		  if (line_spreading == EGG_WRAP_BOX_SPREAD_EXPAND)
		    {
		      this_line_size += extra_per_line;
		      
		      if (line_count < extra_line_extra)
			this_line_size++;
		    }
		}

              item_offset = 0;

              if (item_spreading == EGG_WRAP_BOX_SPREAD_END)
                {
                  item_offset += extra_pixels;

                  /* If we're on the last line, prepend the space for
                   * any leading items */
                  if (line_count == n_lines -1)
                    {
                      gint extra_items = n_children % line_length;

                      if (priv->mode == EGG_WRAP_ALLOCATE_HOMOGENEOUS)
                        {
                          item_offset += item_size * (line_length - extra_items);
                          item_offset += item_spacing * (line_length - extra_items);
                        }
                      else
                        {
                          gint j;

                          for (j = 0; j < (line_length - extra_items); j++)
                            {
                              item_offset += item_sizes[j].minimum_size;
                              item_offset += item_spacing;
                            }
                        }
                    }
                }
            }

          /* Push the index along for the last line when spreading to the end */
          if (item_spreading == EGG_WRAP_BOX_SPREAD_END &&
              line_count == n_lines -1)
            {
              gint extra_items = n_children % line_length;

              position += line_length - extra_items;
            }

          if (priv->mode == EGG_WRAP_ALLOCATE_HOMOGENEOUS)
            this_item_size = item_size;
          else /* aligned mode */
            this_item_size = item_sizes[position].minimum_size;

          if (item_spreading == EGG_WRAP_BOX_SPREAD_EXPAND)
            {
              this_item_size += extra_per_item;

              if (position < extra_extra)
                this_item_size++;
            }

          /* Do the actual allocation */
          allocate_child (box, child, item_offset, line_offset, this_item_size, this_line_size);

          item_offset += this_item_size;
          item_offset += item_spacing;

          /* deal with extra spacing here */
          if (item_spreading == EGG_WRAP_BOX_SPREAD_EVEN)
            {
              item_offset += extra_per_item;

              if (position < extra_extra)
                item_offset++;
            }

          i++;
        }

      g_free (item_sizes);
      g_free (line_sizes);
    }
  else /* EGG_WRAP_ALLOCATE_FREE */
    {
      /* Here we just fit as many children as we can allocate their natural size to
       * on each line and add the heights for each of them on each line */
      GtkRequestedSize  requested; 
      GtkRequestedSize *sizes = NULL;
      GList            *list = priv->children;
      gboolean          first_line = TRUE;
      gint              i, line_count = 0;
      gint              line_offset, item_offset;
      gint              extra_per_line = 0, extra_line_extra = 0;
      gint              extra_pixels;
      GArray           *array;
  
      array = g_array_new (0, TRUE, sizeof (GtkRequestedSize));

      while (list != NULL)
        {
          GArray         *line_array;
          AllocatedLine  *line;

          list =
            get_largest_size_for_free_line_in_opposing_orientation (box, priv->orientation,
                                                                    list, min_items, avail_size,
                                                                    &requested.minimum_size,
                                                                    &requested.natural_size,
                                                                    &extra_pixels,
                                                                    &line_array);

          /* Its possible a line is made of completely invisible children */
          if (requested.natural_size > 0)
            {
              if (first_line)
                first_line = FALSE;
              else
                avail_other_size -= line_spacing;

              avail_other_size -= requested.minimum_size;

              line = g_slice_new0 (AllocatedLine);
              line->requested    = line_array;
              line->extra_pixels = extra_pixels;

              requested.data  = line;

              g_array_append_val (array, requested);
            }
        }

      /* Distribute space among lines naturally */
      sizes            = (GtkRequestedSize *)array->data;
      avail_other_size = gtk_distribute_natural_allocation (avail_other_size, array->len, sizes);

      /* Calculate expand space per line */
      if (line_spreading == EGG_WRAP_BOX_SPREAD_EVEN)
        {
          extra_per_line   = avail_other_size / MAX (array->len -1, 1);
          extra_line_extra = avail_other_size % MAX (array->len -1, 1);
        }
      else if (line_spreading == EGG_WRAP_BOX_SPREAD_EXPAND)
        {
          extra_per_line   = avail_other_size / array->len;
          extra_line_extra = avail_other_size % array->len;
        }

      if (line_spreading == EGG_WRAP_BOX_SPREAD_END)
	line_offset = avail_other_size;
      else
	line_offset = 0;

      for (line_count = 0; line_count < array->len; line_count++)
        {
          AllocatedLine    *line       = (AllocatedLine *)sizes[line_count].data;
          GArray           *line_array = line->requested;
          GtkRequestedSize *line_sizes = (GtkRequestedSize *)line_array->data;
          gint              line_size  = sizes[line_count].minimum_size;
          gint              extra_per_item = 0;
          gint              extra_extra = 0;

          /* Set line start offset */
          item_offset = 0;

	  if (line_spreading == EGG_WRAP_BOX_SPREAD_EXPAND)
	    {
	      line_size += extra_per_line;

	      if (line_count < extra_line_extra)
		line_size++;
	    }

          if (item_spreading == EGG_WRAP_BOX_SPREAD_END)
            item_offset += line->extra_pixels;
          else if (item_spreading == EGG_WRAP_BOX_SPREAD_EVEN)
            {
              extra_per_item = line->extra_pixels / MAX (line_array->len -1, 1);
              extra_extra    = line->extra_pixels % MAX (line_array->len -1, 1);
            }

          for (i = 0; i < line_array->len; i++)
            {
              EggWrapBoxChild *child     = line_sizes[i].data;
              gint             item_size = line_sizes[i].minimum_size;

              /* Do the actual allocation */
              allocate_child (box, child, item_offset, line_offset, item_size, line_size);

              /* Add extra space evenly between children */
              if (item_spreading == EGG_WRAP_BOX_SPREAD_EVEN)
                {
                  item_offset += extra_per_item;
                  if (i < extra_extra)
                    item_offset++;
                }

              /* Move item cursor along for the next allocation */
              item_offset += item_spacing;
              item_offset += item_size;
            }

          /* New line, increment offset and reset item cursor */
          line_offset += line_spacing;
          line_offset += line_size;

	  if (line_spreading == EGG_WRAP_BOX_SPREAD_EVEN)
	    {
	      line_offset += extra_per_line;

	      if (line_count < extra_line_extra)
		line_offset++;
	    }

          /* Free the array for this line now its not needed anymore */
          g_array_free (line_array, TRUE);
          g_slice_free (AllocatedLine, line);
        }

      g_array_free (array, TRUE);
    }
}

/*****************************************************
 *                GtkContainerClass                  *
 *****************************************************/
static void
egg_wrap_box_add (GtkContainer *container,
                  GtkWidget    *widget)
{
  egg_wrap_box_insert_child (EGG_WRAP_BOX (container), widget, -1, 0);
}

static gint
find_child_in_list (EggWrapBoxChild *child_in_list,
                    GtkWidget       *search)
{
  return (child_in_list->widget == search) ? 0 : -1;
}

static void
egg_wrap_box_remove (GtkContainer *container,
                     GtkWidget    *widget)
{
  EggWrapBox        *box = EGG_WRAP_BOX (container);
  EggWrapBoxPrivate *priv   = box->priv;
  GList             *list;

  list = g_list_find_custom (priv->children, widget,
                             (GCompareFunc)find_child_in_list);

  if (list)
    {
      EggWrapBoxChild *child = list->data;
      gboolean was_visible = gtk_widget_get_visible (widget);

      gtk_widget_unparent (widget);

      g_slice_free (EggWrapBoxChild, child);
      priv->children = g_list_delete_link (priv->children, list);

      if (was_visible && gtk_widget_get_visible (GTK_WIDGET (container)))
        gtk_widget_queue_resize (GTK_WIDGET (container));
    }
}

static void
egg_wrap_box_forall (GtkContainer *container,
                     gboolean      include_internals,
                     GtkCallback   callback,
                     gpointer      callback_data)
{
  EggWrapBox        *box = EGG_WRAP_BOX (container);
  EggWrapBoxPrivate *priv   = box->priv;
  EggWrapBoxChild   *child;
  GList             *list;

  list = priv->children;

  while (list)
    {
      child = list->data;
      list  = list->next;

      (* callback) (child->widget, callback_data);
    }
}

static GType
egg_wrap_box_child_type (GtkContainer   *container)
{
  return GTK_TYPE_WIDGET;
}

static void
egg_wrap_box_set_child_property (GtkContainer    *container,
                                 GtkWidget       *widget,
                                 guint            property_id,
                                 const GValue    *value,
                                 GParamSpec      *pspec)
{
  EggWrapBox        *box  = EGG_WRAP_BOX (container);
  EggWrapBoxPrivate *priv = box->priv;
  EggWrapBoxChild   *child;
  GList             *list;

  list = g_list_find_custom (priv->children, widget,
                             (GCompareFunc)find_child_in_list);
  g_return_if_fail (list != NULL);

  child = list->data;

  switch (property_id)
    {
    case CHILD_PROP_PACKING:
      child->packing = g_value_get_flags (value);
      break;
    default:
      GTK_CONTAINER_WARN_INVALID_CHILD_PROPERTY_ID (container, property_id, pspec);
      break;
    }

  if (gtk_widget_get_visible (widget) &&
      gtk_widget_get_visible (GTK_WIDGET (box)))
    gtk_widget_queue_resize (widget);
}

static void
egg_wrap_box_get_child_property (GtkContainer    *container,
                                 GtkWidget       *widget,
                                 guint            property_id,
                                 GValue          *value,
                                 GParamSpec      *pspec)
{
  EggWrapBox        *box = EGG_WRAP_BOX (container);
  EggWrapBoxPrivate *priv   = box->priv;
  EggWrapBoxChild   *child;
  GList             *list;

  list = g_list_find_custom (priv->children, widget,
                             (GCompareFunc)find_child_in_list);
  g_return_if_fail (list != NULL);

  child = list->data;

  switch (property_id)
    {
    case CHILD_PROP_PACKING:
      g_value_set_flags (value, child->packing);
      break;
    default:
      GTK_CONTAINER_WARN_INVALID_CHILD_PROPERTY_ID (container, property_id, pspec);
      break;
    }
}

/*****************************************************
 *                 size requests                     *
 *****************************************************/


static GtkSizeRequestMode
egg_wrap_box_get_request_mode (GtkWidget      *widget)
{
  EggWrapBox        *box = EGG_WRAP_BOX (widget);
  EggWrapBoxPrivate *priv   = box->priv;

  return (priv->orientation == GTK_ORIENTATION_HORIZONTAL) ?
    GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH : GTK_SIZE_REQUEST_WIDTH_FOR_HEIGHT;
}

/* Gets the largest minimum and natural length of
 * 'line_length' consecutive items */
static void
get_largest_line_length (EggWrapBox      *box,
                         GtkOrientation   orientation,
                         gint             line_length,
                         gint            *min_size,
                         gint            *nat_size)
{
  EggWrapBoxPrivate *priv = box->priv;
  GList             *list, *l;
  gint               max_min_size = 0;
  gint               max_nat_size = 0;
  gint               spacing;

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    spacing = priv->horizontal_spacing;
  else
    spacing = priv->vertical_spacing;

  /* Get the largest size of 'line_length' consecutive items in the list.
   */
  for (list = priv->children; list; list = list->next)
    {
      gint   line_min = 0;
      gint   line_nat = 0;
      gint   i;

      for (l = list, i = 0; l && i < line_length; l = l->next)
        {
          EggWrapBoxChild *child = l->data;
          gint             child_min, child_nat;

          if (!gtk_widget_get_visible (child->widget))
            continue;

          if (orientation == GTK_ORIENTATION_HORIZONTAL)
            gtk_widget_get_preferred_width (child->widget,
                                            &child_min, &child_nat);
          else /* GTK_ORIENTATION_VERTICAL */
            gtk_widget_get_preferred_height (child->widget,
                                             &child_min, &child_nat);

          line_min += child_min;
          line_nat += child_nat;

          i++;
        }

      max_min_size = MAX (max_min_size, line_min);
      max_nat_size = MAX (max_nat_size, line_nat);
    }

  max_min_size += (line_length - 1) * spacing;
  max_nat_size += (line_length - 1) * spacing;

  if (min_size)
    *min_size = max_min_size;

  if (nat_size)
    *nat_size = max_nat_size;
}

/* Gets the largest minimum and natural length of
 * 'line_length' consecutive items when aligned into rows/columns */
static void
get_largest_aligned_line_length (EggWrapBox      *box,
				 GtkOrientation   orientation,
				 gint             line_length,
				 gint            *min_size,
				 gint            *nat_size)
{
  EggWrapBoxPrivate *priv = box->priv;
  GList             *list;
  gint               max_min_size = 0;
  gint               max_nat_size = 0;
  gint               spacing, i;
  GtkRequestedSize  *aligned_item_sizes;

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    spacing = priv->horizontal_spacing;
  else
    spacing = priv->vertical_spacing;

  aligned_item_sizes = g_new0 (GtkRequestedSize, line_length);

  /* Get the largest sizes of each index in the line.
   */
  for (list = priv->children, i = 0; list; list = list->next)
    {
      EggWrapBoxChild *child = list->data;
      gint             child_min, child_nat;
      
      if (!gtk_widget_get_visible (child->widget))
	continue;

      if (orientation == GTK_ORIENTATION_HORIZONTAL)
	gtk_widget_get_preferred_width (child->widget,
                                        &child_min, &child_nat);
      else /* GTK_ORIENTATION_VERTICAL */
	gtk_widget_get_preferred_height (child->widget,
                                         &child_min, &child_nat);

      aligned_item_sizes[i % line_length].minimum_size = 
	MAX (aligned_item_sizes[i % line_length].minimum_size, child_min);

      aligned_item_sizes[i % line_length].natural_size = 
	MAX (aligned_item_sizes[i % line_length].natural_size, child_nat);

      i++;
    }

  /* Add up the largest indexes */
  for (i = 0; i < line_length; i++)
    {
      max_min_size += aligned_item_sizes[i].minimum_size;
      max_nat_size += aligned_item_sizes[i].natural_size;
    }

  g_free (aligned_item_sizes);

  max_min_size += (line_length - 1) * spacing;
  max_nat_size += (line_length - 1) * spacing;

  if (min_size)
    *min_size = max_min_size;

  if (nat_size)
    *nat_size = max_nat_size;
}


static void
egg_wrap_box_get_preferred_width (GtkWidget           *widget,
                                  gint                *minimum_size,
                                  gint                *natural_size)
{
  EggWrapBox        *box  = EGG_WRAP_BOX (widget);
  EggWrapBoxPrivate *priv = box->priv;
  gint               min_item_width, nat_item_width;
  gint               min_items, nat_items;
  gint               min_width, nat_width;

  min_items = MAX (1, priv->minimum_line_children);
  nat_items = MAX (min_items, priv->natural_line_children);

  if (priv->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      min_width    = nat_width = 0;

      if (priv->mode == EGG_WRAP_ALLOCATE_FREE ||
	  priv->mode == EGG_WRAP_ALLOCATE_ALIGNED)
        {
	  /* In FREE and ALIGNED modes; horizontally oriented boxes
	   * need enough width for the widest row */
          if (min_items == 1)
            {
              get_average_item_size (box, GTK_ORIENTATION_HORIZONTAL,
                                     &min_item_width, &nat_item_width);

              min_width += min_item_width;
              nat_width += nat_item_width;
            }
          else if (priv->mode == EGG_WRAP_ALLOCATE_FREE)
            {
              gint min_line_length, nat_line_length;

              get_largest_line_length (box, GTK_ORIENTATION_HORIZONTAL, min_items,
                                       &min_line_length, &nat_line_length);

              if (nat_items > min_items)
                get_largest_line_length (box, GTK_ORIENTATION_HORIZONTAL, nat_items,
                                         NULL, &nat_line_length);

              min_width += min_line_length;
              nat_width += nat_line_length;
            }
	  else /* EGG_WRAP_MODE_ALIGNED */
	    {
              gint min_line_length, nat_line_length;

              get_largest_aligned_line_length (box, GTK_ORIENTATION_HORIZONTAL, min_items,
					       &min_line_length, &nat_line_length);

              if (nat_items > min_items)
                get_largest_aligned_line_length (box, GTK_ORIENTATION_HORIZONTAL, nat_items,
						 NULL, &nat_line_length);

              min_width += min_line_length;
              nat_width += nat_line_length;
	    }
        }
      else /* In HOMOGENEOUS mode; horizontally oriented boxs
            * give the same width to all children */
        {
          get_average_item_size (box, GTK_ORIENTATION_HORIZONTAL,
                                 &min_item_width, &nat_item_width);

          min_width += min_item_width * min_items;
          min_width += (min_items -1) * priv->horizontal_spacing;

          nat_width += nat_item_width * nat_items;
          nat_width += (nat_items -1) * priv->horizontal_spacing;
        }
    }
  else /* GTK_ORIENTATION_VERTICAL */
    {
      /* Return the width for the minimum height */
      gint min_height;

      GTK_WIDGET_GET_CLASS (widget)->get_preferred_height (widget, &min_height, NULL);
      GTK_WIDGET_GET_CLASS (widget)->get_preferred_width_for_height (widget, min_height,
                                                                           &min_width, &nat_width);

    }

  if (minimum_size)
    *minimum_size = min_width;

  if (natural_size)
    *natural_size = nat_width;
}

static void
egg_wrap_box_get_preferred_height (GtkWidget           *widget,
                                   gint                *minimum_size,
                                   gint                *natural_size)
{
  EggWrapBox        *box  = EGG_WRAP_BOX (widget);
  EggWrapBoxPrivate *priv = box->priv;
  gint               min_item_height, nat_item_height;
  gint               min_items, nat_items;
  gint               min_height, nat_height;

  min_items = MAX (1, priv->minimum_line_children);
  nat_items = MAX (min_items, priv->natural_line_children);

  if (priv->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      /* Return the height for the minimum width */
      gint min_width;

      GTK_WIDGET_GET_CLASS (widget)->get_preferred_width (widget, &min_width, NULL);
      GTK_WIDGET_GET_CLASS (widget)->get_preferred_height_for_width (widget, min_width,
                                                                           &min_height, &nat_height);
    }
  else /* GTK_ORIENTATION_VERTICAL */
    {
      min_height   = nat_height = 0;

      if (priv->mode == EGG_WRAP_ALLOCATE_FREE ||
	  priv->mode == EGG_WRAP_ALLOCATE_ALIGNED)
        {
	  /* In FREE and ALIGNED modes; vertically oriented boxes
	   * need enough height for the tallest column */
          if (min_items == 1)
            {
              get_average_item_size (box, GTK_ORIENTATION_VERTICAL,
                                     &min_item_height, &nat_item_height);

              min_height += min_item_height;
              nat_height += nat_item_height;
            }
          else if (priv->mode == EGG_WRAP_ALLOCATE_FREE)
            {
              gint min_line_length, nat_line_length;

              get_largest_line_length (box, GTK_ORIENTATION_VERTICAL, min_items,
                                       &min_line_length, &nat_line_length);

              if (nat_items > min_items)
                get_largest_line_length (box, GTK_ORIENTATION_VERTICAL, nat_items,
                                         NULL, &nat_line_length);

              min_height += min_line_length;
              nat_height += nat_line_length;
            }
	  else /* EGG_WRAP_ALLOCATE_ALIGNED */
	    {
              gint min_line_length, nat_line_length;

              get_largest_aligned_line_length (box, GTK_ORIENTATION_VERTICAL, min_items,
					       &min_line_length, &nat_line_length);

              if (nat_items > min_items)
                get_largest_aligned_line_length (box, GTK_ORIENTATION_VERTICAL, nat_items,
						 NULL, &nat_line_length);

              min_height += min_line_length;
              nat_height += nat_line_length;
	    }

        }
      else /* In HOMOGENEOUS mode; vertically oriented boxs
            * give the same height to all children */
        {
          get_average_item_size (box, GTK_ORIENTATION_VERTICAL,
                                 &min_item_height, &nat_item_height);

          min_height += min_item_height * min_items;
          min_height += (min_items -1) * priv->vertical_spacing;

          nat_height += nat_item_height * nat_items;
          nat_height += (nat_items -1) * priv->vertical_spacing;
        }
    }

  if (minimum_size)
    *minimum_size = min_height;

  if (natural_size)
    *natural_size = nat_height;
}

static void
egg_wrap_box_get_preferred_height_for_width (GtkWidget           *widget,
                                             gint                 width,
                                             gint                *minimum_height,
                                             gint                *natural_height)
{
  EggWrapBox        *box = EGG_WRAP_BOX (widget);
  EggWrapBoxPrivate *priv   = box->priv;
  gint               min_item_width, nat_item_width;
  gint               min_items;
  gint               min_height, nat_height;
  gint               avail_size, n_children;

  min_items = MAX (1, priv->minimum_line_children);

  min_height = 0;
  nat_height = 0;

  if (priv->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      gint min_width;

      n_children = get_visible_children (box);

      /* Make sure its no smaller than the minimum */
      GTK_WIDGET_GET_CLASS (widget)->get_preferred_width (widget, &min_width, NULL);

      avail_size  = MAX (width, min_width);

      if (priv->mode == EGG_WRAP_ALLOCATE_ALIGNED ||
          priv->mode == EGG_WRAP_ALLOCATE_HOMOGENEOUS)
        {
          gint line_length;
          gint item_size, extra_pixels;

          get_average_item_size (box, GTK_ORIENTATION_HORIZONTAL, &min_item_width, &nat_item_width);

          /* By default wrap at the natural item width */
          line_length = avail_size / (nat_item_width + priv->horizontal_spacing);

          /* After the above aproximation, check if we cant fit one more on the line */
          if (line_length * priv->horizontal_spacing + (line_length + 1) * nat_item_width <= avail_size)
            line_length++;

          /* Its possible we were allocated just less than the natural width of the
           * minimum item wrap length */
          line_length = MAX (min_items, line_length);

          /* Now we need the real item allocation size */
          item_size = (avail_size - (line_length - 1) * priv->horizontal_spacing) / line_length;

          /* Cut out the expand space if we're not distributing any */
          if (priv->horizontal_spreading != EGG_WRAP_BOX_SPREAD_EXPAND)
            {
              item_size    = MIN (item_size, nat_item_width);
              extra_pixels = 0;
            }
          else
            /* Collect the extra pixels for expand children */
            extra_pixels = (avail_size - (line_length - 1) * priv->horizontal_spacing) % line_length;

          if (priv->mode == EGG_WRAP_ALLOCATE_HOMOGENEOUS)
            {
              gint min_item_height, nat_item_height;
              gint lines;

              /* Here we just use the largest height-for-width and
               * add up the size accordingly */
              get_largest_size_for_opposing_orientation (box, GTK_ORIENTATION_HORIZONTAL, item_size,
                                                         &min_item_height, &nat_item_height);

              /* Round up how many lines we need to allocate for */
              lines      = n_children / line_length;
              if ((n_children % line_length) > 0)
                lines++;

              min_height = min_item_height * lines;
              nat_height = nat_item_height * lines;

              min_height += (lines - 1) * priv->vertical_spacing;
              nat_height += (lines - 1) * priv->vertical_spacing;
            }
          else /* EGG_WRAP_ALLOCATE_ALIGNED */
            {
              GList *list = priv->children;
              gint min_line_height, nat_line_height, i;
              gboolean first_line = TRUE;
              GtkRequestedSize *item_sizes;

              /* First get the size each set of items take to span the line
               * when aligning the items above and below after wrapping.
               */
              item_sizes = fit_aligned_item_requests (box, priv->orientation, avail_size,
                                                      priv->horizontal_spacing, &line_length, n_children);


              /* Get the available remaining size */
              avail_size -= (line_length - 1) * priv->horizontal_spacing;
              for (i = 0; i < line_length; i++)
                avail_size -= item_sizes[i].minimum_size;

              extra_pixels = gtk_distribute_natural_allocation (avail_size, line_length, item_sizes);

              while (list != NULL)
                {
                  list =
                    get_largest_size_for_line_in_opposing_orientation (box, GTK_ORIENTATION_HORIZONTAL,
                                                                       list, line_length,
                                                                       item_sizes, extra_pixels,
                                                                       &min_line_height, &nat_line_height);

                  /* Its possible the line only had invisible widgets */
                  if (nat_line_height > 0)
                    {
                      if (first_line)
                        first_line = FALSE;
                      else
                        {
                          min_height += priv->vertical_spacing;
                          nat_height += priv->vertical_spacing;
                        }

                      min_height += min_line_height;
                      nat_height += nat_line_height;
                    }
                }

              g_free (item_sizes);
            }
        }
      else /* EGG_WRAP_ALLOCATE_FREE */
        {
          /* Here we just fit as many children as we can allocate their natural size to
           * on each line and add the heights for each of them on each line */
          GList *list = priv->children;
          gint min_line_height = 0, nat_line_height = 0;
          gboolean first_line = TRUE;

          while (list != NULL)
            {
              list =
                get_largest_size_for_free_line_in_opposing_orientation (box, GTK_ORIENTATION_HORIZONTAL,
                                                                        list, min_items, avail_size,
                                                                        &min_line_height, &nat_line_height,
                                                                        NULL, NULL);

              /* Its possible the last line only had invisible widgets */
              if (nat_line_height > 0)
                {
                  if (first_line)
                    first_line = FALSE;
                  else
                    {
                      min_height += priv->vertical_spacing;
                      nat_height += priv->vertical_spacing;
                    }

                  min_height += min_line_height;
                  nat_height += nat_line_height;
                }
            }
        }
    }
  else /* GTK_ORIENTATION_VERTICAL */
    {
      /* Return the minimum height */
      GTK_WIDGET_GET_CLASS (widget)->get_preferred_height (widget, &min_height, &nat_height);
    }

  if (minimum_height)
    *minimum_height = min_height;

  if (natural_height)
    *natural_height = nat_height;
}

static void
egg_wrap_box_get_preferred_width_for_height (GtkWidget           *widget,
                                             gint                 height,
                                             gint                *minimum_width,
                                             gint                *natural_width)
{
  EggWrapBox        *box = EGG_WRAP_BOX (widget);
  EggWrapBoxPrivate *priv   = box->priv;
  gint               min_item_height, nat_item_height;
  gint               min_items;
  gint               min_width, nat_width;
  gint               avail_size, n_children;

  min_items = MAX (1, priv->minimum_line_children);

  min_width = 0;
  nat_width = 0;

  if (priv->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      /* Return the minimum width */
      GTK_WIDGET_GET_CLASS (widget)->get_preferred_width (widget, &min_width, &nat_width);
    }
  else /* GTK_ORIENTATION_VERTICAL */
    {
      gint min_height;

      n_children = get_visible_children (box);

      /* Make sure its no smaller than the minimum */
      GTK_WIDGET_GET_CLASS (widget)->get_preferred_height (widget, &min_height, NULL);

      avail_size  = MAX (height, min_height);

      if (priv->mode == EGG_WRAP_ALLOCATE_ALIGNED ||
          priv->mode == EGG_WRAP_ALLOCATE_HOMOGENEOUS)
        {
          gint line_length;
          gint item_size, extra_pixels;

          get_average_item_size (box, GTK_ORIENTATION_VERTICAL, &min_item_height, &nat_item_height);

          /* By default wrap at the natural item width */
          line_length = avail_size / (nat_item_height + priv->vertical_spacing);

          /* After the above aproximation, check if we cant fit one more on the line */
          if (line_length * priv->vertical_spacing + (line_length + 1) * nat_item_height <= avail_size)
            line_length++;

          /* Its possible we were allocated just less than the natural width of the
           * minimum item wrap length */
          line_length = MAX (min_items, line_length);

          /* Now we need the real item allocation size */
          item_size = (avail_size - (line_length - 1) * priv->vertical_spacing) / line_length;

          /* Cut out the expand space if we're not distributing any */
          if (priv->vertical_spreading != EGG_WRAP_BOX_SPREAD_EXPAND)
            {
              item_size    = MIN (item_size, nat_item_height);
              extra_pixels = 0;
            }
          else
            /* Collect the extra pixels for expand children */
            extra_pixels = (avail_size - (line_length - 1) * priv->vertical_spacing) % line_length;

          if (priv->mode == EGG_WRAP_ALLOCATE_HOMOGENEOUS)
            {
              gint min_item_width, nat_item_width;
              gint lines;

              /* Here we just use the largest height-for-width and
               * add up the size accordingly */
              get_largest_size_for_opposing_orientation (box, GTK_ORIENTATION_VERTICAL, item_size,
                                                         &min_item_width, &nat_item_width);

              /* Round up how many lines we need to allocate for */
              n_children = get_visible_children (box);
              lines      = n_children / line_length;
              if ((n_children % line_length) > 0)
                lines++;

              min_width = min_item_width * lines;
              nat_width = nat_item_width * lines;

              min_width += (lines - 1) * priv->horizontal_spacing;
              nat_width += (lines - 1) * priv->horizontal_spacing;
            }
          else /* EGG_WRAP_ALLOCATE_ALIGNED */
            {
              GList *list = priv->children;
              gint min_line_width, nat_line_width, i;
              gboolean first_line = TRUE;
              GtkRequestedSize *item_sizes;

              /* First get the size each set of items take to span the line
               * when aligning the items above and below after wrapping.
               */
              item_sizes = fit_aligned_item_requests (box, priv->orientation, avail_size,
                                                      priv->vertical_spacing, &line_length, n_children);

              /* Get the available remaining size */
              avail_size -= (line_length - 1) * priv->horizontal_spacing;
              for (i = 0; i < line_length; i++)
                avail_size -= item_sizes[i].minimum_size;

              extra_pixels = gtk_distribute_natural_allocation (avail_size, line_length, item_sizes);

              while (list != NULL)
                {
                  list =
                    get_largest_size_for_line_in_opposing_orientation (box, GTK_ORIENTATION_VERTICAL,
                                                                       list, line_length,
                                                                       item_sizes, extra_pixels,
                                                                       &min_line_width, &nat_line_width);

                  /* Its possible the last line only had invisible widgets */
                  if (nat_line_width > 0)
                    {
                      if (first_line)
                        first_line = FALSE;
                      else
                        {
                          min_width += priv->horizontal_spacing;
                          nat_width += priv->horizontal_spacing;
                        }

                      min_width += min_line_width;
                      nat_width += nat_line_width;
                    }
                }
              g_free (item_sizes);
            }
        }
      else /* EGG_WRAP_ALLOCATE_FREE */
        {
          /* Here we just fit as many children as we can allocate their natural size to
           * on each line and add the heights for each of them on each line */
          GList *list = priv->children;
          gint min_line_width = 0, nat_line_width = 0;
          gboolean first_line = TRUE;

          while (list != NULL)
            {
              list =
                get_largest_size_for_free_line_in_opposing_orientation (box, GTK_ORIENTATION_VERTICAL,
                                                                        list, min_items, avail_size,
                                                                        &min_line_width, &nat_line_width,
                                                                        NULL, NULL);

              /* Its possible the last line only had invisible widgets */
              if (nat_line_width > 0)
                {
                  if (first_line)
                    first_line = FALSE;
                  else
                    {
                      min_width += priv->horizontal_spacing;
                      nat_width += priv->horizontal_spacing;
                    }

                  min_width += min_line_width;
                  nat_width += nat_line_width;
                }
            }
        }
    }

  if (minimum_width)
    *minimum_width = min_width;

  if (natural_width)
    *natural_width = nat_width;
}

/*****************************************************
 *                       API                         *
 *****************************************************/

/**
 * egg_wrap_box_new:
 * @mode: The #EggWrapAllocationMode to use
 * @horizontal_spreading: The horizontal #EggWrapBoxSpreading policy to use
 * @vertical_spreading: The vertical #EggWrapBoxSpreading policy to use
 * @horizontal_spacing: The horizontal spacing to add between children
 * @vertical_spacing: The vertical spacing to add between children
 *
 * Creates an #EggWrapBox.
 *
 * Returns: A new #EggWrapBox container
 */
GtkWidget *
egg_wrap_box_new (EggWrapAllocationMode mode,
                  EggWrapBoxSpreading   horizontal_spreading,
                  EggWrapBoxSpreading   vertical_spreading,
                  guint                 horizontal_spacing,
                  guint                 vertical_spacing)
{
  return (GtkWidget *)g_object_new (EGG_TYPE_WRAP_BOX,
                                    "allocation-mode", mode,
                                    "horizontal-spreading", horizontal_spreading,
                                    "vertical-spreading", vertical_spreading,
                                    "vertical-spacing", vertical_spacing,
                                    "horizontal-spacing", horizontal_spacing,
                                    NULL);
}

/**
 * egg_wrap_box_set_allocation_mode:
 * @box: An #EggWrapBox
 * @mode: The #EggWrapAllocationMode to use.
 *
 * Sets the allocation mode for @box's children.
 */
void
egg_wrap_box_set_allocation_mode (EggWrapBox           *box,
                                  EggWrapAllocationMode mode)
{
  EggWrapBoxPrivate *priv;

  g_return_if_fail (EGG_IS_WRAP_BOX (box));

  priv = box->priv;

  if (priv->mode != mode)
    {
      priv->mode = mode;

      gtk_widget_queue_resize (GTK_WIDGET (box));

      g_object_notify (G_OBJECT (box), "allocation-mode");
    }
}

/**
 * egg_wrap_box_get_allocation_mode:
 * @box: An #EggWrapBox
 *
 * Gets the allocation mode.
 *
 * Returns: The #EggWrapAllocationMode for @box.
 */
EggWrapAllocationMode
egg_wrap_box_get_allocation_mode (EggWrapBox *box)
{
  g_return_val_if_fail (EGG_IS_WRAP_BOX (box), FALSE);

  return box->priv->mode;
}


/**
 * egg_wrap_box_set_horizontal_spreading:
 * @box: An #EggWrapBox
 * @spreading: The #EggWrapBoxSpreading to use.
 *
 * Sets the horizontal spreading mode for @box's children.
 */
void
egg_wrap_box_set_horizontal_spreading (EggWrapBox          *box,
				       EggWrapBoxSpreading  spreading)
{
  EggWrapBoxPrivate *priv;

  g_return_if_fail (EGG_IS_WRAP_BOX (box));

  priv = box->priv;

  if (priv->horizontal_spreading != spreading)
    {
      priv->horizontal_spreading = spreading;

      gtk_widget_queue_resize (GTK_WIDGET (box));

      g_object_notify (G_OBJECT (box), "horizontal-spreading");
    }
}

/**
 * egg_wrap_box_get_horizontal_spreading:
 * @box: An #EggWrapBox
 *
 * Gets the horizontal spreading mode.
 *
 * Returns: The horizontal #EggWrapBoxSpreading for @box.
 */
EggWrapBoxSpreading
egg_wrap_box_get_horizontal_spreading (EggWrapBox *box)
{
  g_return_val_if_fail (EGG_IS_WRAP_BOX (box), FALSE);

  return box->priv->horizontal_spreading;
}


/**
 * egg_wrap_box_set_vertical_spreading:
 * @box: An #EggWrapBox
 * @spreading: The #EggWrapBoxSpreading to use.
 *
 * Sets the vertical spreading mode for @box's children.
 */
void
egg_wrap_box_set_vertical_spreading (EggWrapBox          *box,
				     EggWrapBoxSpreading  spreading)
{
  EggWrapBoxPrivate *priv;

  g_return_if_fail (EGG_IS_WRAP_BOX (box));

  priv = box->priv;

  if (priv->vertical_spreading != spreading)
    {
      priv->vertical_spreading = spreading;

      gtk_widget_queue_resize (GTK_WIDGET (box));

      g_object_notify (G_OBJECT (box), "vertical-spreading");
    }
}

/**
 * egg_wrap_box_get_vertical_spreading:
 * @box: An #EggWrapBox
 *
 * Gets the vertical spreading mode.
 *
 * Returns: The vertical #EggWrapBoxSpreading for @box.
 */
EggWrapBoxSpreading
egg_wrap_box_get_vertical_spreading (EggWrapBox *box)
{
  g_return_val_if_fail (EGG_IS_WRAP_BOX (box), FALSE);

  return box->priv->vertical_spreading;
}


/**
 * egg_wrap_box_set_vertical_spacing:
 * @box: An #EggWrapBox
 * @spacing: The spacing to use.
 *
 * Sets the vertical space to add between children.
 */
void
egg_wrap_box_set_vertical_spacing  (EggWrapBox    *box,
                                    guint          spacing)
{
  EggWrapBoxPrivate *priv;

  g_return_if_fail (EGG_IS_WRAP_BOX (box));

  priv = box->priv;

  if (priv->vertical_spacing != spacing)
    {
      priv->vertical_spacing = spacing;

      gtk_widget_queue_resize (GTK_WIDGET (box));

      g_object_notify (G_OBJECT (box), "vertical-spacing");
    }
}

/**
 * egg_wrap_box_get_vertical_spacing:
 * @box: An #EggWrapBox
 *
 * Gets the vertical spacing.
 *
 * Returns: The vertical spacing.
 */
guint
egg_wrap_box_get_vertical_spacing  (EggWrapBox *box)
{
  g_return_val_if_fail (EGG_IS_WRAP_BOX (box), FALSE);

  return box->priv->vertical_spacing;
}

/**
 * egg_wrap_box_set_horizontal_spacing:
 * @box: An #EggWrapBox
 * @spacing: The spacing to use.
 *
 * Sets the horizontal space to add between children.
 */
void
egg_wrap_box_set_horizontal_spacing (EggWrapBox    *box,
                                     guint          spacing)
{
  EggWrapBoxPrivate *priv;

  g_return_if_fail (EGG_IS_WRAP_BOX (box));

  priv = box->priv;

  if (priv->horizontal_spacing != spacing)
    {
      priv->horizontal_spacing = spacing;

      gtk_widget_queue_resize (GTK_WIDGET (box));

      g_object_notify (G_OBJECT (box), "horizontal-spacing");
    }
}

/**
 * egg_wrap_box_get_horizontal_spacing:
 * @box: An #EggWrapBox
 *
 * Gets the horizontal spacing.
 *
 * Returns: The horizontal spacing.
 */
guint
egg_wrap_box_get_horizontal_spacing (EggWrapBox *box)
{
  g_return_val_if_fail (EGG_IS_WRAP_BOX (box), FALSE);

  return box->priv->horizontal_spacing;
}

/**
 * egg_wrap_box_set_minimum_line_children:
 * @box: An #EggWrapBox
 * @n_children: The minimum amount of children per line.
 *
 * Sets the minimum amount of children to line up
 * in @box's orientation before wrapping.
 */
void
egg_wrap_box_set_minimum_line_children (EggWrapBox *box,
                                        guint       n_children)
{
  EggWrapBoxPrivate *priv;

  g_return_if_fail (EGG_IS_WRAP_BOX (box));

  priv = box->priv;

  if (priv->minimum_line_children != n_children)
    {
      priv->minimum_line_children = n_children;

      gtk_widget_queue_resize (GTK_WIDGET (box));

      g_object_notify (G_OBJECT (box), "minimum-line-children");
    }
}

/**
 * egg_wrap_box_get_minimum_line_children:
 * @box: An #EggWrapBox
 *
 * Gets the minimum amount of children per line.
 *
 * Returns: The minimum amount of children per line.
 */
guint
egg_wrap_box_get_minimum_line_children (EggWrapBox *box)
{
  g_return_val_if_fail (EGG_IS_WRAP_BOX (box), FALSE);

  return box->priv->minimum_line_children;
}

/**
 * egg_wrap_box_set_natural_line_children:
 * @box: An #EggWrapBox
 * @n_children: The natural amount of children per line.
 *
 * Sets the natural length of items to request and
 * allocate space for in @box's orientation.
 *
 * Setting the natural amount of children per line
 * limits the overall natural size request to be no more
 * than @n_children items long in the given orientation.
 */
void
egg_wrap_box_set_natural_line_children (EggWrapBox *box,
                                        guint       n_children)
{
  EggWrapBoxPrivate *priv;

  g_return_if_fail (EGG_IS_WRAP_BOX (box));

  priv = box->priv;

  if (priv->natural_line_children != n_children)
    {
      priv->natural_line_children = n_children;

      gtk_widget_queue_resize (GTK_WIDGET (box));

      g_object_notify (G_OBJECT (box), "natural-line-children");
    }
}

/**
 * egg_wrap_box_get_natural_line_children:
 * @box: An #EggWrapBox
 *
 * Gets the natural amount of children per line.
 *
 * Returns: The natural amount of children per line.
 */
guint
egg_wrap_box_get_natural_line_children (EggWrapBox *box)
{
  g_return_val_if_fail (EGG_IS_WRAP_BOX (box), FALSE);

  return box->priv->natural_line_children;
}


/**
 * egg_wrap_box_insert_child:
 * @box: And #EggWrapBox
 * @widget: the child #GtkWidget to add
 * @index: the position in the child list to insert, specify -1 to append to the list.
 * @packing: The #EggWrapBoxPacking options to use.
 *
 * Adds a child to an #EggWrapBox with its packing options set
 *
 */
void
egg_wrap_box_insert_child (EggWrapBox        *box,
                           GtkWidget         *widget,
                           gint               index,
                           EggWrapBoxPacking  packing)
{
  EggWrapBoxPrivate *priv;
  EggWrapBoxChild   *child;
  GList             *list;

  g_return_if_fail (EGG_IS_WRAP_BOX (box));
  g_return_if_fail (GTK_IS_WIDGET (widget));

  priv = box->priv;

  list = g_list_find_custom (priv->children, widget,
                             (GCompareFunc)find_child_in_list);
  g_return_if_fail (list == NULL);

  child           = g_slice_new0 (EggWrapBoxChild);
  child->widget   = widget;
  child->packing  = packing;

  priv->children = g_list_insert (priv->children, child, index);

  gtk_widget_set_parent (widget, GTK_WIDGET (box));
}

/**
 * egg_wrap_box_reorder_child:
 * @box: An #EggWrapBox
 * @widget: The child to reorder
 * @index: The new child position
 *
 * Reorders the child @widget in @box's list of children.
 */
void
egg_wrap_box_reorder_child (EggWrapBox *box,
                            GtkWidget  *widget,
                            guint       index)
{
  EggWrapBoxPrivate *priv;
  EggWrapBoxChild   *child;
  GList             *list;

  g_return_if_fail (EGG_IS_WRAP_BOX (box));
  g_return_if_fail (GTK_IS_WIDGET (widget));

  priv = box->priv;

  list = g_list_find_custom (priv->children, widget,
                             (GCompareFunc)find_child_in_list);
  g_return_if_fail (list != NULL);

  if (g_list_position (priv->children, list) != index)
    {
      child = list->data;
      priv->children = g_list_delete_link (priv->children, list);
      priv->children = g_list_insert (priv->children, child, index);

      gtk_widget_queue_resize (GTK_WIDGET (box));
    }
}
