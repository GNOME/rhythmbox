/*
 * Initial Author:
 *   Aaron Bockover <abockover@novell.com>
 *
 * Ported to C from Banshee's SegmentedBar.cs widget
 *
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2008 Christophe Fergeau <teuf@gnome.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef RB_SEGMENTED_BAR_H
#define RB_SEGMENTED_BAR_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RB_TYPE_SEGMENTED_BAR     		(rb_segmented_bar_get_type ())
#define RB_SEGMENTED_BAR(obj)     		(G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_SEGMENTED_BAR, RBSegmentedBar))
#define RB_SEGMENTED_BAR_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_SEGMENTED_BAR, RBSegmenterBarClass))
#define RB_IS_SEGMENTED_BAR(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_SEGMENTED_BAR))
#define RB_IS_SEGMENTER_BAR_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE ((klass), RB_TYPE_SEGMENTED_BAR))

typedef struct _RBSegmentedBar RBSegmentedBar;
typedef struct _RBSegmentedBarClass RBSegmentedBarClass;
typedef struct _RBSegmentedBarPrivate RBSegmentedBarPrivate;

struct _RBSegmentedBar
{
	GtkWidget parent;

	RBSegmentedBarPrivate *priv;
};

struct _RBSegmentedBarClass
{
	GtkWidgetClass parent;

};

typedef gchar *(*RBSegmentedBarValueFormatter) (gdouble percent, gpointer data);

GType    	rb_segmented_bar_get_type (void);

GtkWidget  	*rb_segmented_bar_new     (void);
guint rb_segmented_bar_add_segment (RBSegmentedBar *bar,
				    const gchar *title, gdouble percent,
				    gdouble red, gdouble green,
				    gdouble blue, gdouble alpha);
guint rb_segmented_bar_add_segment_default_color (RBSegmentedBar *bar,
						  const gchar *title,
						  gdouble percent);
void rb_segmented_bar_update_segment (RBSegmentedBar *bar,
				      guint segment_index,
				      gdouble percent);
void rb_segmented_bar_set_value_formatter (RBSegmentedBar *bar,
					   RBSegmentedBarValueFormatter formatter,
					   gpointer data);
G_END_DECLS

#endif /* RB_SEGMENTED_BAR:_H */
