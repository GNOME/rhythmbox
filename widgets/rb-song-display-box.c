/*
 *  RBSongDisplayBox is a hack of GtkHBox
 *  arch-tag: RBSongDisplayBox - display album/artist information
 */ 

/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Modified by the GTK+ Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/. 
 */

#include <config.h>

#include <libgnome/gnome-i18n.h>

#include "rb-song-display-box.h"
#include "rb-debug.h"


struct RBSongDisplayBoxPrivate
{
	GtkWidget *from;
	GtkWidget *by;
};

static void rb_song_display_box_class_init    (RBSongDisplayBoxClass *klass);
static void rb_song_display_box_init          (RBSongDisplayBox *box);
static void rb_song_display_box_finalize      (GObject *object);
static void rb_song_display_box_size_request  (GtkWidget  *widget,
					       GtkRequisition *requisition);
static void rb_song_display_box_size_allocate (GtkWidget *widget,
					       GtkAllocation  *allocation);

/*  size_allocate helper functions */
static int  displaybox_get_childwidth         (GtkWidget *widget);
static void do_allocation                     (GtkWidget *widget,
					       int size,
					       GtkAllocation *allocation);

static GObjectClass *parent_class = NULL;

GType
rb_song_display_box_get_type (void)
{
	static GType box_type = 0;

	if (!box_type) {
		static const GTypeInfo box_info =
		{
			sizeof (RBSongDisplayBoxClass),
			NULL,           /* base_init */
			NULL,           /* base_finalize */
			(GClassInitFunc) rb_song_display_box_class_init,
			NULL,           /* class_finalize */
			NULL,           /* class_data */
			sizeof (RBSongDisplayBox),
			0,              /* n_preallocs */
			(GInstanceInitFunc) rb_song_display_box_init,
		};

		box_type = g_type_register_static (GTK_TYPE_BOX,
						   "RBSongDisplayBox",
						   &box_info, 0);
	}

	return box_type;
}

static void
rb_song_display_box_class_init (RBSongDisplayBoxClass *class)
{
	GtkWidgetClass *widget_class;

	widget_class = (GtkWidgetClass*) class;
	parent_class = g_type_class_peek_parent (class);

	G_OBJECT_CLASS (class)->finalize = rb_song_display_box_finalize;

	widget_class->size_request = rb_song_display_box_size_request;
	widget_class->size_allocate = rb_song_display_box_size_allocate;
}

static void
rb_song_display_box_init (RBSongDisplayBox *displaybox)
{
	GtkBox *box;

	displaybox->priv = g_new0 (RBSongDisplayBoxPrivate, 1);
	box = GTK_BOX (displaybox);
	box->spacing = 0;
	box->homogeneous = FALSE;

	displaybox->priv->from = gtk_label_new (_("from"));
	displaybox->album = (GnomeHRef *) gnome_href_new ("", "");
	displaybox->priv->by = gtk_label_new (_("by"));
	displaybox->artist = (GnomeHRef *) gnome_href_new ("", "");

	gtk_box_pack_start (box, displaybox->priv->from, FALSE, FALSE, 0);
	gtk_box_pack_start (box, GTK_WIDGET (displaybox->album), FALSE, FALSE, 0);
	gtk_box_pack_start (box, displaybox->priv->by, FALSE, FALSE, 0);
	gtk_box_pack_start (box, GTK_WIDGET (displaybox->artist), FALSE, FALSE, 0);
}

GtkWidget*
rb_song_display_box_new (void)
{
	RBSongDisplayBox *displaybox;

	displaybox = g_object_new (RB_TYPE_SONG_DISPLAY_BOX, NULL);

	return GTK_WIDGET (displaybox);
}

static void
rb_song_display_box_finalize (GObject *object)
{
	RBSongDisplayBox *box;

	box = RB_SONG_DISPLAY_BOX (object);

	g_free (box->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_song_display_box_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
	RBSongDisplayBox *displaybox;
	GtkBox *box;
	GtkBoxChild *child;
	GList *children;
	GtkRequisition child_requisition;

	box = GTK_BOX (widget);
	displaybox = RB_SONG_DISPLAY_BOX (widget);
	
	requisition->height = 0;

	for (children = box->children; children; children = children->next)
	{
  		child = children->data;

		if (GTK_WIDGET_VISIBLE (child->widget))
		{
			gtk_widget_size_request (child->widget,
						 &child_requisition);
			requisition->height = MAX (requisition->height,
						   child_requisition.height);
		}
	}

	gtk_widget_size_request (displaybox->priv->by, &child_requisition);
	requisition->width = child_requisition.width;
}

static void
rb_song_display_box_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	RBSongDisplayBox *displaybox;
	GtkAllocation remain;
	int shown, width;

	displaybox = RB_SONG_DISPLAY_BOX (widget);
  	widget->allocation = *allocation;

	/* calculate how much room we have for the 'album' link */
	width = allocation->width;
	width -= displaybox_get_childwidth (displaybox->priv->from);
	width -= displaybox_get_childwidth (displaybox->priv->by);
	width -= displaybox_get_childwidth (GTK_WIDGET (displaybox->artist));

	/* if we can't fit the entire thing and we have less than 30 pixels
	   then just don't bother.
	*/
	if (width < 30 &&
	    width < displaybox_get_childwidth (GTK_WIDGET (displaybox->album)))
	{
		width = 0;
		shown = 0;
	}
	else
		shown = -1;
		
	remain = widget->allocation;
	do_allocation (displaybox->priv->from,          shown, &remain);
	do_allocation (GTK_WIDGET (displaybox->album),  width, &remain);
	do_allocation (displaybox->priv->by,            -1,    &remain);
	do_allocation (GTK_WIDGET (displaybox->artist), -1,    &remain);
}

static int
displaybox_get_childwidth (GtkWidget *widget)
{
	GtkRequisition requisition;

	gtk_widget_get_child_requisition (widget, &requisition);
  	return requisition.width;
}

static void
do_allocation (GtkWidget *widget, int size, GtkAllocation *allocation)
{
	GtkAllocation child_allocation;
	int width;

	width = displaybox_get_childwidth (widget);
	if (size != -1)
		width = MIN (width, size);

	child_allocation.x = allocation->x;
	child_allocation.width = MIN (width, allocation->width);
	allocation->width -= child_allocation.width;
	allocation->x += child_allocation.width;

	child_allocation.height = allocation->height;
	child_allocation.y = allocation->y;

	gtk_widget_size_allocate( widget, &child_allocation );
}
