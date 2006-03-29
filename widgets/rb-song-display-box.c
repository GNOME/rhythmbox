/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnomeui/libgnomeui.h> /* for GnomeUrl */
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rb-song-display-box.h"
#include "rb-debug.h"


struct RBSongDisplayBoxPrivate
{
	GtkWidget *from;
	GtkWidget *album;
	GtkWidget *by;
	GtkWidget *artist;

	GtkTooltips *artist_tips, *album_tips;
};

#define RB_SONG_DISPLAY_BOX_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SONG_DISPLAY_BOX, RBSongDisplayBoxPrivate))

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

G_DEFINE_TYPE (RBSongDisplayBox, rb_song_display_box, GTK_TYPE_BOX);


static void
rb_song_display_box_class_init (RBSongDisplayBoxClass *class)
{
	GtkWidgetClass *widget_class;

	widget_class = (GtkWidgetClass*) class;

	G_OBJECT_CLASS (class)->finalize = rb_song_display_box_finalize;

	widget_class->size_request = rb_song_display_box_size_request;
	widget_class->size_allocate = rb_song_display_box_size_allocate;

	g_type_class_add_private (class, sizeof (RBSongDisplayBoxPrivate));
}

static void
rb_song_display_box_init (RBSongDisplayBox *displaybox)
{
	GtkBox *box;

	displaybox->priv = RB_SONG_DISPLAY_BOX_GET_PRIVATE (displaybox);


	box = GTK_BOX (displaybox);
	box->spacing = 0;
	box->homogeneous = FALSE;

	displaybox->priv->from = gtk_label_new (_("from"));
	displaybox->priv->album = gnome_href_new ("", "");
	displaybox->priv->by = gtk_label_new (_("by"));
	displaybox->priv->artist = gnome_href_new ("", "");


	gtk_box_pack_start (box, displaybox->priv->from, 
			    FALSE, FALSE, 0);
	gtk_box_pack_start (box, displaybox->priv->album,
			    FALSE, FALSE, 0);
	gtk_box_pack_start (box, displaybox->priv->by, 
			    FALSE, FALSE, 0);
	gtk_box_pack_start (box, displaybox->priv->artist, 
			    FALSE, FALSE, 0);

	gtk_widget_set_no_show_all (displaybox->priv->from, TRUE);
	gtk_widget_set_no_show_all (displaybox->priv->album, TRUE);
	gtk_widget_set_no_show_all (displaybox->priv->by, TRUE);
	gtk_widget_set_no_show_all (displaybox->priv->artist, TRUE);

	displaybox->priv->artist_tips = gtk_tooltips_new ();
	displaybox->priv->album_tips = gtk_tooltips_new ();
}

GtkWidget*
rb_song_display_box_new (void)
{
	RBSongDisplayBox *displaybox;

	displaybox = g_object_new (RB_TYPE_SONG_DISPLAY_BOX, NULL, NULL);

	return GTK_WIDGET (displaybox);
}

static void
rb_song_display_box_finalize (GObject *object)
{
	RBSongDisplayBox *box;

	box = RB_SONG_DISPLAY_BOX (object);

	G_OBJECT_CLASS (rb_song_display_box_parent_class)->finalize (object);
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
	width -= displaybox_get_childwidth (displaybox->priv->artist);

	/* if we can't fit the entire thing and we have less than 30 pixels
	   then just don't bother.
	*/
	if (width < 30 &&
	    width < displaybox_get_childwidth (displaybox->priv->album))
	{
		width = 0;
		shown = 0;
	}
	else
		shown = -1;
		
	remain = widget->allocation;
	if (gtk_widget_get_default_direction () == GTK_TEXT_DIR_RTL) {
		remain.x += remain.width;
	}
	do_allocation (displaybox->priv->from, shown, &remain);
	do_allocation (displaybox->priv->album, width, &remain);
	do_allocation (displaybox->priv->by, -1, &remain);
	do_allocation (displaybox->priv->artist,-1, &remain);
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
	
	child_allocation.width = MIN (width, allocation->width);
	allocation->width -= child_allocation.width;
	if (gtk_widget_get_default_direction () == GTK_TEXT_DIR_RTL) {
		allocation->x -= child_allocation.width;
		child_allocation.x = allocation->x;
	} else {
		child_allocation.x = allocation->x;
		allocation->x += child_allocation.width;
	}

	child_allocation.height = allocation->height;
	child_allocation.y = allocation->y;

	gtk_widget_size_allocate( widget, &child_allocation );
}

static char *
sanitize_string (const char *data)
{
	char *ret;
	
	/*
	 * netlabels often put URLs (or domain names) in the 'album' field
	 * of their releases; since there are no artist names in AMG that
	 * start with 'http://' or 'www.' (there is a 'www', from the 70s,
	 * strangely enough), we can safely assume anything that looks 
	 * like a URL or domain name is one.
	 *
	 * There's sometimes some trailing junk, usually after a space,
	 * so it's probably sensible to strip that off.
	 */
	if (g_str_has_prefix (data, "http://")) {
		char *end = strchr (data, ' ');
		if (end != NULL)
			ret = g_strndup (data, end - data);
		else
			ret = g_strdup (data);
	} else if (g_str_has_prefix (data, "www.")) {
		char *end = strchr (data, ' ');
		if (end != NULL)
			ret = g_strdup_printf ("http://%*s",
					       (int) (end-data), data);
		else
			ret = g_strdup_printf ("http://%s", data);
	} else {
		ret = g_strdup (data);
	}
	return g_strstrip (ret);
}

static char *
info_url (const char *artist, const char *album)
{
	char *escaped_artist;
	char *sanitized_artist;
	char *ret;

	sanitized_artist = sanitize_string (artist);
	escaped_artist = gnome_vfs_escape_string (sanitized_artist);
	g_free (sanitized_artist);
	if (album) {
		char *sanitized_album;
		char *escaped_album;
		sanitized_album = sanitize_string (album);
		escaped_album = gnome_vfs_escape_string (sanitized_album);
		g_free (sanitized_album);
		ret = g_strdup_printf ("http://www.last.fm/music/%s/%s",
				       escaped_artist, escaped_album);
		g_free (escaped_album);
	} else {
		ret = g_strdup_printf ("http://www.last.fm/music/%s", escaped_artist);
	}
	g_free (escaped_artist);

	return ret;
}

void
rb_song_display_box_sync (RBSongDisplayBox *displaybox, RhythmDBEntry *entry)
{
	const char *album, *artist; 
	char *tmp;

	if (entry == NULL) {
		gtk_widget_hide (displaybox->priv->from);
		gtk_widget_hide (displaybox->priv->album);
		gtk_widget_hide (displaybox->priv->by);
		gtk_widget_hide (displaybox->priv->artist);
		return;
	}
	
	gtk_widget_show (displaybox->priv->from);
	gtk_widget_show (displaybox->priv->album);
	gtk_widget_show (displaybox->priv->by);
	gtk_widget_show (displaybox->priv->artist);
	
	album = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
	artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
	
	tmp = info_url (artist, album);
	gnome_href_set_url (GNOME_HREF (displaybox->priv->album), tmp);
	g_free (tmp);
	tmp = g_markup_escape_text (album, -1);
	gnome_href_set_text (GNOME_HREF (displaybox->priv->album), tmp);
	g_free (tmp);

	gtk_tooltips_set_tip (displaybox->priv->album_tips,
			      displaybox->priv->album,
			      _("Get information on this album from the web"), 
			      NULL);

	tmp = info_url (artist, NULL);
	gnome_href_set_url (GNOME_HREF (displaybox->priv->artist), tmp);
	g_free (tmp);
	tmp = g_markup_escape_text (artist, -1);
	gnome_href_set_text (GNOME_HREF (displaybox->priv->artist), tmp);
	g_free (tmp);

	gtk_tooltips_set_tip (displaybox->priv->artist_tips,
			      displaybox->priv->artist,
			      _("Get information on this artist from the web"),
			      NULL);
}
