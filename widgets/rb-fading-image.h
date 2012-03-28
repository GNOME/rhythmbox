/*
 *  Copyright (C) 2012 Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef RB_FADING_IMAGE_H
#define RB_FADING_IMAGE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RB_TYPE_FADING_IMAGE         (rb_fading_image_get_type ())
#define RB_FADING_IMAGE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_FADING_IMAGE, RBFadingImage))
#define RB_FADING_IMAGE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_FADING_IMAGE, RBFadingImageClass))
#define RB_IS_FADING_IMAGE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_FADING_IMAGE))
#define RB_IS_FADING_IMAGE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_FADING_IMAGE))
#define RB_FADING_IMAGE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_FADING_IMAGE, RBFadingImageClass))

typedef struct _RBFadingImage RBFadingImage;
typedef struct _RBFadingImageClass RBFadingImageClass;
typedef struct _RBFadingImagePrivate RBFadingImagePrivate;

struct _RBFadingImage
{
	GtkWidget parent;

	RBFadingImagePrivate *priv;
};

struct _RBFadingImageClass
{
	GtkWidgetClass parent;
};

/* create instances using g_object_new, since you'll need to set widget properties too */

GType		rb_fading_image_get_type	(void);

void		rb_fading_image_set_pixbuf	(RBFadingImage *image, GdkPixbuf *pixbuf);

void		rb_fading_image_start		(RBFadingImage *image, guint64 duration);

G_END_DECLS

#endif /* RB_FADING_IMAGE_H */
