/* Volume Button / popup widget
 * (c) copyright 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __BACON_VOLUME_BUTTON_H__
#define __BACON_VOLUME_BUTTON_H__

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkicontheme.h>

G_BEGIN_DECLS

#define BACON_TYPE_VOLUME_BUTTON \
  (bacon_volume_button_get_type ())
#define BACON_VOLUME_BUTTON(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), BACON_TYPE_VOLUME_BUTTON, \
			       BaconVolumeButton))

typedef struct _BaconVolumeButton {
  GtkButton parent;

  /* popup */
  GtkWidget *dock, *scale, *image, *plus, *min;
  GtkIconSize size;
  gint click_id;
  float direction;
  gboolean timeout;
  guint32 pop_time;
  GdkPixbuf *icon[4];
} BaconVolumeButton;

typedef struct _BaconVolumeButtonClass {
  GtkButtonClass parent_class;

  /* signals */
  void	(* value_changed)	(BaconVolumeButton * button);

  gpointer __bla[4];
} BaconVolumeButtonClass;

GType		bacon_volume_button_get_type	(void);

GtkWidget *	bacon_volume_button_new		(GtkIconSize size,
						 float min, float max,
						 float step);
float		bacon_volume_button_get_value	(BaconVolumeButton * button);
void		bacon_volume_button_set_value	(BaconVolumeButton * button,
						 float value);

G_END_DECLS

#endif /* __BACON_VOLUME_BUTTON_H__ */
