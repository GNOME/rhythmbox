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

#include <gtk/gtklabel.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkhscale.h>
#include <gtk/gtkalignment.h>
#include <gtk/gtkstock.h>
#include <libgnome/gnome-i18n.h>
#include <string.h>

#include "rb-stock-icons.h"
#include "rb-link.h"
#include "rb-player.h"

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
	RBViewPlayer *view_player;

	GtkWidget *image;
	GtkWidget *song;
	GtkWidget *scale;
	GtkWidget *elapsed;
	RBLink *artist;
	RBLink *album;
};

enum
{
	PROP_0,
	PROP_VIEW_PLAYER
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

		rb_player_type = g_type_register_static (GTK_TYPE_FRAME,
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
					 PROP_VIEW_PLAYER,
					 g_param_spec_object ("view-player",
							      "RBViewPlayer",
							      "RBVoewPlayer",
							      RB_TYPE_VIEW_PLAYER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_player_init (RBPlayer *player)
{
	GtkWidget *hbox, *vbox, *textline, *label, *align, *scalebox, *textvbox;
	
	player->priv = g_new0 (RBPlayerPrivate, 1);

	gtk_frame_set_shadow_type (GTK_FRAME (player), GTK_SHADOW_NONE);

	hbox = gtk_hbox_new (FALSE, 10);

	player->priv->image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_INFO,
							GTK_ICON_SIZE_DIALOG);
	gtk_box_pack_start (GTK_BOX (hbox), player->priv->image, FALSE, TRUE, 0);

	vbox = gtk_vbox_new (FALSE, 5);

	textvbox = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), textvbox, FALSE, TRUE, 0);

	align = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
	player->priv->song = gtk_label_new ("");
	gtk_label_set_use_markup (GTK_LABEL (player->priv->song), TRUE);
	gtk_label_set_markup (GTK_LABEL (player->priv->song),
			      _("<span size=\"xx-large\"><b>Born Slippy</b></span>"));
	gtk_container_add (GTK_CONTAINER (align), player->priv->song);
	gtk_box_pack_start (GTK_BOX (textvbox), align, FALSE, FALSE, 0);

	textline = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (textvbox), textline, FALSE, TRUE, 0);

	label = gtk_label_new (_("from "));
	gtk_box_pack_start (GTK_BOX (textline), label, FALSE, TRUE, 0);
	
	player->priv->album = rb_link_new ();
	rb_link_set (player->priv->album, "Second Toughest In The Infants", "album");
	gtk_box_pack_start (GTK_BOX (textline), GTK_WIDGET (player->priv->album), FALSE, TRUE, 0);

	label = gtk_label_new (_(" by "));
	gtk_box_pack_start (GTK_BOX (textline), label, FALSE, TRUE, 0);

	player->priv->artist = rb_link_new ();
	rb_link_set (player->priv->artist, "Underworld", "artist");
	gtk_box_pack_start (GTK_BOX (textline), GTK_WIDGET (player->priv->artist), FALSE, TRUE, 0);

	scalebox = gtk_hbox_new (FALSE, 2);
	player->priv->scale = gtk_hscale_new (NULL);
	gtk_scale_set_draw_value (GTK_SCALE (player->priv->scale), FALSE);
	gtk_widget_set_size_request (player->priv->scale, 150, -1);
	gtk_box_pack_start (GTK_BOX (scalebox), player->priv->scale, FALSE, TRUE, 0);
	player->priv->elapsed = gtk_label_new ("0:00");
	gtk_box_pack_start (GTK_BOX (scalebox), player->priv->elapsed, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), scalebox, FALSE, FALSE, 0);

	gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

	gtk_container_add (GTK_CONTAINER (player), hbox);
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
	case PROP_VIEW_PLAYER:
		player->priv->view_player = g_value_get_object (value);
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
	case PROP_VIEW_PLAYER:
		g_value_set_object (value, player->priv->view_player);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBPlayer *
rb_player_new (RBViewPlayer *view_player)
{
	RBPlayer *player;

	g_return_val_if_fail (RB_IS_VIEW_PLAYER (view_player), NULL);
	
	player = RB_PLAYER (g_object_new (RB_TYPE_PLAYER,
					  "view-player", view_player,
					  NULL));

	g_return_val_if_fail (player->priv != NULL, NULL);

	return player;
}
