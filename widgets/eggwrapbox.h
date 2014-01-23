/*
 * Copyright (C) 2010 Openismus GmbH
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

#ifndef __EGG_WRAP_BOX_H__
#define __EGG_WRAP_BOX_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS


#define EGG_TYPE_WRAP_BOX                  (egg_wrap_box_get_type ())
#define EGG_WRAP_BOX(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), EGG_TYPE_WRAP_BOX, EggWrapBox))
#define EGG_WRAP_BOX_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), EGG_TYPE_WRAP_BOX, EggWrapBoxClass))
#define EGG_IS_WRAP_BOX(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EGG_TYPE_WRAP_BOX))
#define EGG_IS_WRAP_BOX_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), EGG_TYPE_WRAP_BOX))
#define EGG_WRAP_BOX_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), EGG_TYPE_WRAP_BOX, EggWrapBoxClass))

typedef struct _EggWrapBox            EggWrapBox;
typedef struct _EggWrapBoxPrivate     EggWrapBoxPrivate;
typedef struct _EggWrapBoxClass       EggWrapBoxClass;


/**
 * EggWrapAllocationMode:
 * @EGG_WRAP_ALLOCATE_FREE:        Items wrap freely in the box's orientation
 * @EGG_WRAP_ALLOCATE_ALIGNED:     Items are aligned into rows and columns
 * @EGG_WRAP_ALLOCATE_HOMOGENEOUS: Items are all allocated the same size
 *
 * Describes how an #EggWrapBox positions its children.
 */
typedef enum {
  EGG_WRAP_ALLOCATE_FREE = 0,
  EGG_WRAP_ALLOCATE_ALIGNED,
  EGG_WRAP_ALLOCATE_HOMOGENEOUS
} EggWrapAllocationMode;


/**
 * EggWrapBoxSpreading:
 * @EGG_WRAP_BOX_SPREAD_START:  Children are allocated no more than their natural size
 *                              in the given orientation and any extra space is left trailing at 
 *                              the end of each row/column.
 * @EGG_WRAP_BOX_SPREAD_END:    Children are allocated no more than their natural size
 *                              in the given orientation and any extra space skipped at the beginning
 *                              of each row/column.
 * @EGG_WRAP_BOX_SPREAD_EVEN:   Children are allocated no more than their natural size
 *                              in the given orientation and any extra space is evenly distributed
 *                              as empty space between children.
 * @EGG_WRAP_BOX_SPREAD_EXPAND: Extra space is given to children which asked to expand in the given
 *                              orientation (or columns/rows which contain children who asked to expand).
 *                              If no children asked to expand; extra space is distributed evenly.
 *
 * Describes how a #EggWrapBox deals with extra space in a given orientation when allocating children.
 */
typedef enum {
  EGG_WRAP_BOX_SPREAD_START = 0,
  EGG_WRAP_BOX_SPREAD_END,
  EGG_WRAP_BOX_SPREAD_EVEN,
  EGG_WRAP_BOX_SPREAD_EXPAND
} EggWrapBoxSpreading;

/**
 * EggWrapBoxPacking:
 * @EGG_WRAP_BOX_H_EXPAND: Whether the child expands horizontally.
 * @EGG_WRAP_BOX_V_EXPAND: Whether the child expands vertically.
 *
 * Specifies how widgets will expand vertically and
 * horizontally when placed inside a #EggWrapBox.
 */
typedef enum
{
  EGG_WRAP_BOX_H_EXPAND = 1 << 0,
  EGG_WRAP_BOX_V_EXPAND = 1 << 1
} EggWrapBoxPacking;


struct _EggWrapBox
{
  GtkContainer container;

  /*< private >*/
  EggWrapBoxPrivate *priv;
};

struct _EggWrapBoxClass
{
  GtkContainerClass parent_class;
};

GType                 egg_wrap_box_get_type                  (void) G_GNUC_CONST;

GtkWidget            *egg_wrap_box_new                       (EggWrapAllocationMode mode,
                                                              EggWrapBoxSpreading   horizontal_spreading,
							      EggWrapBoxSpreading   vertical_spreading,
                                                              guint                 horizontal_spacing,
                                                              guint                 vertical_spacing);
void                  egg_wrap_box_set_allocation_mode       (EggWrapBox           *box,
                                                              EggWrapAllocationMode mode);
EggWrapAllocationMode egg_wrap_box_get_allocation_mode       (EggWrapBox           *box);

void                  egg_wrap_box_set_horizontal_spreading  (EggWrapBox           *box,
                                                              EggWrapBoxSpreading   spreading);
EggWrapBoxSpreading   egg_wrap_box_get_horizontal_spreading  (EggWrapBox           *box);

void                  egg_wrap_box_set_vertical_spreading    (EggWrapBox           *box,
                                                              EggWrapBoxSpreading   spreading);
EggWrapBoxSpreading   egg_wrap_box_get_vertical_spreading    (EggWrapBox           *box);

void                  egg_wrap_box_set_vertical_spacing      (EggWrapBox           *box,
                                                              guint                 spacing);
guint                 egg_wrap_box_get_vertical_spacing      (EggWrapBox           *box);

void                  egg_wrap_box_set_horizontal_spacing    (EggWrapBox           *box,
                                                              guint                 spacing);
guint                 egg_wrap_box_get_horizontal_spacing    (EggWrapBox           *box);

void                  egg_wrap_box_set_minimum_line_children (EggWrapBox           *box,
                                                              guint                 n_children);
guint                 egg_wrap_box_get_minimum_line_children (EggWrapBox           *box);

void                  egg_wrap_box_set_natural_line_children (EggWrapBox           *box,
                                                              guint                 n_children);
guint                 egg_wrap_box_get_natural_line_children (EggWrapBox           *box);

void                  egg_wrap_box_insert_child              (EggWrapBox           *box,
                                                              GtkWidget            *widget,
                                                              gint                  index,
                                                              EggWrapBoxPacking     packing);

void                  egg_wrap_box_reorder_child             (EggWrapBox           *box,
                                                              GtkWidget            *widget,
                                                              guint                 index);

G_END_DECLS


#endif /* __EGG_WRAP_BOX_H__ */
