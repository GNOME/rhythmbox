/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  $Id$
 */

#include <gtk/gtkvbox.h>
#include <gtk/gtkalignment.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkhscale.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>

#include "rb-player.h"
#include "rb-stock-icons.h"
#include "rb-file-helpers.h"
#include "rb-node-view.h"
#include "rb-link.h"
#include "rb-ellipsizing-label.h"

static void rb_player_class_init (RBPlayerClass *klass);
static void rb_player_init (RBPlayer *player);
static void rb_player_finalize (GObject *object);
static void rb_player_set_property (GObject *object,
				    guint prop_id,
				    const GValue *value,
				    GParamSpec *pspec);
static void rb_player_get_property (GObject *object,
				    guint prop_id,
				    GValue *value,
			            GParamSpec *pspec);

struct RBPlayerPrivate
{
	RB *rb;

	GtkWidget *song_label;
	GtkWidget *album_link;
	GtkWidget *artist_link;
	GtkAdjustment *song_adjustment;
	GtkWidget *song_scale;
	GtkWidget *time_label;

	GtkWidget *first_button_box;

	GtkWidget *previous;
	GtkWidget *play;
	GtkWidget *pause;
	GtkWidget *next;

	GtkWidget *second_button_box;

	GtkWidget *shuffle;
	GtkWidget *repeat;

	RBNode *playlist;
	RBNodeView *playlist_view;
};

enum
{
	PROP_0,
	PROP_RB
};

static GObjectClass *parent_class = NULL;

GType
rb_player_get_type (void)
{
	static GType rb_player_type = 0;

	if (rb_player_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBPlayerClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_player_class_init,
			NULL,
			NULL,
			sizeof (RBPlayer),
			0,
			(GInstanceInitFunc) rb_player_init
		};

		rb_player_type = g_type_register_static (GTK_TYPE_HBOX,
							 "RBPlayer",
							 &our_info, 0);
	}

	return rb_player_type;
}

static void
rb_player_class_init (RBPlayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_player_finalize;

	object_class->set_property = rb_player_set_property;
	object_class->get_property = rb_player_get_property;

	g_object_class_install_property (object_class,
					 PROP_RB,
					 g_param_spec_object ("rb",
							      "RB",
							      "RB object",
							      RB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static GtkWidget *
create_button_with_icon (const char *stock_id)
{
	GtkWidget *ret, *image;

	ret = gtk_button_new ();
	image = gtk_image_new_from_stock (stock_id,
					  GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_container_add (GTK_CONTAINER (ret), image);

	return ret;
}

static void
pack_button (RBPlayer *player, GtkWidget *widget,
	     gboolean pack_in_second_box)
{
	GtkBox *box;

	if (pack_in_second_box)
		box = GTK_BOX (player->priv->second_button_box);
	else
		box = GTK_BOX (player->priv->first_button_box);

	gtk_box_pack_start (box, widget, FALSE, FALSE, 0);
}

static void
sync_play_button (RBPlayer *player)
{
	gtk_widget_hide (player->priv->pause);
}

static void
rb_player_init (RBPlayer *player)
{
	GtkWidget *hbox, *vbox, *infobox, *label;
	GtkWidget *artist_album_box, *scale_box, *song_box;
	PangoAttribute *attr;
	PangoAttrList *pattrlist;

	player->priv = g_new0 (RBPlayerPrivate, 1);

	gtk_box_set_spacing (GTK_BOX (player), 6);

	/* player area + play controls box */
	vbox = gtk_vbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (player), vbox,
			    FALSE, FALSE, 0);

	/* player area box */
	song_box = gtk_vbox_new (FALSE, 3);
	gtk_box_pack_start (GTK_BOX (vbox), song_box,
			    FALSE, FALSE, 0);

	infobox = gtk_vbox_new (FALSE, 1);
	gtk_box_pack_start (GTK_BOX (song_box), infobox,
			    FALSE, FALSE, 0);

	player->priv->song_label = rb_ellipsizing_label_new (_("Not playing"));
	gtk_misc_set_alignment (GTK_MISC (player->priv->song_label), 0.0, 0.5);
	pattrlist = pango_attr_list_new ();
	attr = pango_attr_scale_new (PANGO_SCALE_XX_LARGE);
	attr->start_index = 0;
	attr->end_index = G_MAXINT;
	pango_attr_list_insert (pattrlist, attr);
	gtk_label_set_attributes (GTK_LABEL (player->priv->song_label),
				  pattrlist);
	pango_attr_list_unref (pattrlist);
	gtk_label_set_selectable (GTK_LABEL (player->priv->song_label),
				  TRUE);
	rb_ellipsizing_label_set_mode (RB_ELLIPSIZING_LABEL (player->priv->song_label),
				       RB_ELLIPSIZE_END);
	gtk_box_pack_start (GTK_BOX (infobox), player->priv->song_label,
			    FALSE, FALSE, 0);

	artist_album_box = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (infobox), artist_album_box,
			    FALSE, FALSE, 0);

	label = gtk_label_new (_("From "));
	gtk_box_pack_start (GTK_BOX (artist_album_box), label,
			    FALSE, FALSE, 0);
	player->priv->album_link = GTK_WIDGET (rb_link_new ());
	gtk_box_pack_start (GTK_BOX (artist_album_box), player->priv->album_link,
			    FALSE, FALSE, 0);
	label = gtk_label_new (_(" by "));
	gtk_box_pack_start (GTK_BOX (artist_album_box), label,
			    FALSE, FALSE, 0);
	player->priv->artist_link = GTK_WIDGET (rb_link_new ());
	gtk_box_pack_start (GTK_BOX (artist_album_box), player->priv->artist_link,
			    FALSE, FALSE, 0);

	scale_box = gtk_hbox_new (FALSE, 2);
	gtk_box_pack_start (GTK_BOX (song_box), scale_box,
			    FALSE, FALSE, 0);

	player->priv->song_adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 1.0, 0.01, 0.1, 0.0));
	player->priv->song_scale = gtk_hscale_new (player->priv->song_adjustment);
	gtk_scale_set_draw_value (GTK_SCALE (player->priv->song_scale), FALSE);
	gtk_box_pack_start (GTK_BOX (scale_box), player->priv->song_scale,
			    TRUE, TRUE, 0);

	player->priv->time_label = gtk_label_new ("0:00");
	gtk_box_pack_start (GTK_BOX (scale_box), player->priv->time_label,
			    FALSE, FALSE, 0);

	/* play controls box */
	hbox = gtk_hbox_new (FALSE, 20);
	gtk_box_pack_start (GTK_BOX (vbox), hbox,
			    FALSE, FALSE, 0);

	player->priv->first_button_box = gtk_hbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (hbox), player->priv->first_button_box,
			    FALSE, FALSE, 0);

	player->priv->second_button_box = gtk_hbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (hbox), player->priv->second_button_box,
			    FALSE, FALSE, 0);

	player->priv->previous = create_button_with_icon (RB_STOCK_PREVIOUS);
	pack_button (player, player->priv->previous, FALSE);
	player->priv->play     = create_button_with_icon (RB_STOCK_PLAY);
	pack_button (player, player->priv->play, FALSE);
	player->priv->pause    = create_button_with_icon (RB_STOCK_PAUSE);
	pack_button (player, player->priv->pause, FALSE);
	player->priv->next     = create_button_with_icon (RB_STOCK_NEXT);
	pack_button (player, player->priv->next, FALSE);

	player->priv->repeat   = create_button_with_icon (RB_STOCK_REPEAT);
	pack_button (player, player->priv->repeat, TRUE);
	player->priv->shuffle  = create_button_with_icon (RB_STOCK_SHUFFLE);
	pack_button (player, player->priv->shuffle, TRUE);

	/* playlist */
	player->priv->playlist = rb_node_new ();
	player->priv->playlist_view = rb_node_view_new (player->priv->playlist,
							rb_file ("rb-node-view-playlist.xml"),
							NULL);
	gtk_box_pack_start (GTK_BOX (player),
			    GTK_WIDGET (player->priv->playlist_view),
			    TRUE, TRUE, 0);

	gtk_widget_show_all (GTK_WIDGET (player));

	sync_play_button (player);
}

static void
rb_player_finalize (GObject *object)
{
	RBPlayer *player;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PLAYER (object));

	player = RB_PLAYER (object);

	g_return_if_fail (player->priv != NULL);

	g_free (player->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_player_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	RBPlayer *player = RB_PLAYER (object);

	switch (prop_id)
	{
	case PROP_RB:
		player->priv->rb = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_player_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	RBPlayer *player = RB_PLAYER (object);

	switch (prop_id)
	{
	case PROP_RB:
		g_value_set_object (value, player->priv->rb);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBPlayer *
rb_player_new (RB *rb)
{
	RBPlayer *player;

	player = g_object_new (RB_TYPE_PLAYER,
			       "rb", rb,
			       NULL);

	g_return_val_if_fail (player->priv != NULL, NULL);

	return player;
}
