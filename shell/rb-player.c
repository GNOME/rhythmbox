/*
 *  Copyright © 2002 Jorn Baayen.  All rights reserved.
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

/* FIXME fix up button crack and summary bits? */
/* FIXME reorderable */
/* FIXME shuffle/repeat */
/* FIXME make all nodes bold */

/* FIXME scroll to playing node */
/* FIXME volume control */
/* FIXME treeview row tooltips if it doesnt fit */
/* FIXME playlist management button? */
/* FIXME delete song from playlist button, ordering buttons, play this one button */
/* FIXME Whole track foo of bar thing should ellipsize */

#include <gtk/gtkvbox.h>
#include <gtk/gtkalignment.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkhscale.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtktooltips.h>
#include <gtk/gtkeventbox.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <string.h>
#include <libxml/tree.h>
#include <unistd.h>
#include <math.h>
#include <monkey-media.h>

#include "rb-debug.h"
#include "rb-player.h"
#include "rb-stock-icons.h"
#include "rb-file-helpers.h"
#include "rb-node-view.h"
#include "rb-link.h"
#include "rb-ellipsizing-label.h"
#include "rb-library-dnd-types.h"
#include "rb-dialog.h"

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
static void drag_data_received_cb (GtkWidget *widget,
				   GdkDragContext *context,
			           int x, int y,
			           GtkSelectionData *data,
			           guint info, guint time,
			           RBPlayer *player);
static void update_buttons (RBPlayer *player);
static void set_playing (RBPlayer *player, RBNode *song);
static void play_cb (GtkWidget *widget, RBPlayer *player);
static void pause_cb (GtkWidget *widget, RBPlayer *player);
static void previous_cb (GtkWidget *widget, RBPlayer *player);
static void next_cb (GtkWidget *widget, RBPlayer *player);
static void shuffle_cb (GtkWidget *widget, RBPlayer *player);
static void repeat_cb (GtkWidget *widget, RBPlayer *player);
static void eos_cb (MonkeyMediaPlayer *mm_player, RBPlayer *player);
static void node_activated_cb (RBNodeView *view, RBNode *song, RBPlayer *player);
static gboolean slider_pressed_cb (GtkWidget *widget, GdkEventButton *event, RBPlayer *player);
static gboolean slider_moved_cb (GtkWidget *widget, GdkEventMotion *event, RBPlayer *player);
static gboolean slider_released_cb (GtkWidget *widget, GdkEventButton *event, RBPlayer *player);
static void slider_changed_cb (GtkWidget *widget, RBPlayer *player);
static void nullify_info (RBPlayer *player);
static gboolean sync_time_timeout (RBPlayer *player);
static void rb_clear (RBPlayer *player);
static void sync_time (RBPlayer *player);
static void check_song_tooltip (RBPlayer *player);
static void song_label_size_allocate_cb (GtkWidget *widget,
			                 GtkAllocation *allocation,
			                 RBPlayer *player);
static void check_view_state (RBPlayer *player);

struct RBPlayerPrivate
{
	RB *rb;

	GtkTooltips *tooltips;
	GtkWidget *info_notebook;
	GtkWidget *song_label_ebox;
	GtkWidget *song_label;
	GtkWidget *artist_link;
	GtkAdjustment *song_adjustment;
	GtkWidget *song_scale;
	GtkWidget *time_ebox;
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

	RBNode *playing;

	RBPlayerState state;

	MonkeyMediaPlayer *player;

	guint timeout;

	struct
	{
		guint slider_moved_timeout;
		long latest_set_time;
		guint value_changed_update_handler;
		gboolean slider_dragging;
		gboolean slider_locked;
		long fake_elapsed;
	} slider_drag_info;
};

enum
{
	PROP_0,
	PROP_RB
};

#define PLAYLIST_XML_VERSION "1.0"

static GObjectClass *parent_class = NULL;

static const GtkTargetEntry target_table[] =
	{ { RB_LIBRARY_DND_NODE_ID_TYPE, 0, RB_LIBRARY_DND_NODE_ID } };

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
rb_player_init (RBPlayer *player)
{
	GtkWidget *hbox, *vbox, *infobox, *vbox_label, *big_label, *label;
	GtkWidget *artist_box, *scale_box, *song_box;
	PangoAttribute *attr;
	PangoAttrList *pattrlist;
	GError *error = NULL;

	player->priv = g_new0 (RBPlayerPrivate, 1);

	player->priv->state = RB_PLAYER_STOPPED;

	gtk_box_set_spacing (GTK_BOX (player), 6);

	player->priv->info_notebook = gtk_notebook_new ();
	gtk_notebook_set_show_border (GTK_NOTEBOOK (player->priv->info_notebook), FALSE);
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (player->priv->info_notebook), FALSE);
	gtk_box_pack_start (GTK_BOX (player),
			    player->priv->info_notebook,
			    FALSE, FALSE, 0);

	/* player area + play controls box */
	player->priv->tooltips = gtk_tooltips_new ();

	vbox = gtk_vbox_new (FALSE, 5);
	gtk_notebook_append_page (GTK_NOTEBOOK (player->priv->info_notebook), vbox, NULL);

	/* player area box */
	song_box = gtk_vbox_new (FALSE, 3);
	gtk_box_pack_start (GTK_BOX (vbox), song_box,
			    FALSE, FALSE, 0);

	infobox = gtk_vbox_new (FALSE, 1);
	gtk_box_pack_start (GTK_BOX (song_box), infobox,
			    FALSE, FALSE, 0);

	player->priv->song_label_ebox = gtk_event_box_new ();
	player->priv->song_label = rb_ellipsizing_label_new ("");
	gtk_container_add (GTK_CONTAINER (player->priv->song_label_ebox), player->priv->song_label);
	g_signal_connect (G_OBJECT (player->priv->song_label),
			  "size_allocate",
			  G_CALLBACK (song_label_size_allocate_cb),
			  player);
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
	gtk_box_pack_start (GTK_BOX (infobox), player->priv->song_label_ebox,
			    FALSE, FALSE, 0);

	artist_box = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (infobox), artist_box,
			    TRUE, TRUE, 0);

	label = gtk_label_new (_("by "));
	gtk_box_pack_start (GTK_BOX (artist_box), label,
			    FALSE, FALSE, 0);
	player->priv->artist_link = GTK_WIDGET (rb_link_new ());
	gtk_box_pack_start (GTK_BOX (artist_box), player->priv->artist_link,
			    TRUE, TRUE, 0);

	scale_box = gtk_hbox_new (FALSE, 2);
	gtk_box_pack_start (GTK_BOX (song_box), scale_box,
			    FALSE, FALSE, 0);

	player->priv->song_adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 1.0, 0.01, 0.1, 0.0));
	player->priv->song_scale = gtk_hscale_new (player->priv->song_adjustment);
	gtk_tooltips_set_tip (player->priv->tooltips,
			      player->priv->song_scale,
			      _("Drag to go to a particular moment in the song"),
			      NULL);
	gtk_scale_set_draw_value (GTK_SCALE (player->priv->song_scale), FALSE);
	g_signal_connect (G_OBJECT (player->priv->song_scale),
			  "button_press_event",
			  G_CALLBACK (slider_pressed_cb),
			  player);
	g_signal_connect (G_OBJECT (player->priv->song_scale),
			  "button_release_event",
			  G_CALLBACK (slider_released_cb),
			  player);
	g_signal_connect (G_OBJECT (player->priv->song_scale),
			  "motion_notify_event",
			  G_CALLBACK (slider_moved_cb),
			  player);
	g_signal_connect (G_OBJECT (player->priv->song_scale),
			  "value_changed",
			  G_CALLBACK (slider_changed_cb),
			  player);
	gtk_box_pack_start (GTK_BOX (scale_box), player->priv->song_scale,
			    TRUE, TRUE, 0);

	player->priv->time_ebox = gtk_event_box_new ();
	gtk_box_pack_start (GTK_BOX (scale_box), player->priv->time_ebox,
			    FALSE, FALSE, 0);
	player->priv->time_label = gtk_label_new ("0:00");
	gtk_container_add (GTK_CONTAINER (player->priv->time_ebox),
			   player->priv->time_label);

	/* play controls box */
	hbox = gtk_hbox_new (FALSE, 20);
	gtk_box_pack_start (GTK_BOX (vbox), hbox,
			    FALSE, FALSE, 0);

	player->priv->first_button_box = gtk_hbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (hbox), player->priv->first_button_box,
			    FALSE, FALSE, 0);

	player->priv->second_button_box = gtk_hbox_new (FALSE, 5);
	gtk_box_pack_end (GTK_BOX (hbox), player->priv->second_button_box,
			    FALSE, FALSE, 0);

	player->priv->previous = create_button_with_icon (RB_STOCK_PREVIOUS);
	gtk_tooltips_set_tip (player->priv->tooltips,
			      player->priv->previous,
			      _("Play the previous song"), NULL);
	pack_button (player, player->priv->previous, FALSE);
	g_signal_connect (G_OBJECT (player->priv->previous), "clicked",
			  G_CALLBACK (previous_cb), player);
	player->priv->play     = create_button_with_icon (RB_STOCK_PLAY);
	gtk_tooltips_set_tip (player->priv->tooltips,
			      player->priv->play,
			      _("Start playback"), NULL);
	pack_button (player, player->priv->play, FALSE);
	g_signal_connect (G_OBJECT (player->priv->play), "clicked",
			  G_CALLBACK (play_cb), player);
	player->priv->pause    = create_button_with_icon (RB_STOCK_PAUSE);
	gtk_tooltips_set_tip (player->priv->tooltips,
			      player->priv->pause,
			      _("Pause playback"), NULL);
	pack_button (player, player->priv->pause, FALSE);
	g_signal_connect (G_OBJECT (player->priv->pause), "clicked",
			  G_CALLBACK (pause_cb), player);
	player->priv->next     = create_button_with_icon (RB_STOCK_NEXT);
	gtk_tooltips_set_tip (player->priv->tooltips,
			      player->priv->next,
			      _("Play the next song"), NULL);
	pack_button (player, player->priv->next, FALSE);
	g_signal_connect (G_OBJECT (player->priv->next), "clicked",
			  G_CALLBACK (next_cb), player);

	player->priv->repeat   = create_button_with_icon (RB_STOCK_REPEAT);
	gtk_tooltips_set_tip (player->priv->tooltips,
			      player->priv->repeat,
			      _("Repeat the play list"), NULL);
	pack_button (player, player->priv->repeat, TRUE);
	g_signal_connect (G_OBJECT (player->priv->repeat), "clicked",
			  G_CALLBACK (repeat_cb), player);
	player->priv->shuffle  = create_button_with_icon (RB_STOCK_SHUFFLE);
	gtk_tooltips_set_tip (player->priv->tooltips,
			      player->priv->shuffle,
			      _("Shuffle the play list"), NULL);
	pack_button (player, player->priv->shuffle, TRUE);
	g_signal_connect (G_OBJECT (player->priv->shuffle), "clicked",
			  G_CALLBACK (shuffle_cb), player);

	/* 'Empty playlist' label */
	vbox_label = gtk_vbox_new (TRUE, 5);
	gtk_container_set_border_width (GTK_CONTAINER (vbox_label), 20);

	big_label = gtk_label_new (_("Not playing"));
	gtk_misc_set_alignment (GTK_MISC (big_label), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (vbox_label), big_label,
			    TRUE, TRUE, 0);

	pattrlist = pango_attr_list_new ();
	attr = pango_attr_scale_new (PANGO_SCALE_XX_LARGE);
	attr->start_index = 0;
	attr->end_index = strlen (_("Not playing"));
	pango_attr_list_insert (pattrlist, attr);
	gtk_label_set_attributes (GTK_LABEL (big_label),
				  pattrlist);
	pango_attr_list_unref (pattrlist);

	label = gtk_label_new (_("To play, add music to the play list"));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (vbox_label), label,
			    TRUE, TRUE, 0);

	gtk_notebook_append_page (GTK_NOTEBOOK (player->priv->info_notebook),
				  vbox_label, NULL);

	/* playlist */
	/* FIXME should be reorderable */
	player->priv->playlist = rb_node_new ();
	player->priv->playlist_view = rb_node_view_new (player->priv->playlist,
							rb_file ("rb-node-view-playlist.xml"),
							NULL);
	g_signal_connect (G_OBJECT (player->priv->playlist_view), "drag_data_received",
			  G_CALLBACK (drag_data_received_cb), player);
	g_signal_connect (G_OBJECT (player->priv->playlist_view), "node_activated",
			  G_CALLBACK (node_activated_cb), player);
	gtk_drag_dest_set (GTK_WIDGET (player->priv->playlist_view), GTK_DEST_DEFAULT_ALL,
			   target_table, 1, GDK_ACTION_COPY);
	gtk_box_pack_start (GTK_BOX (player),
			    GTK_WIDGET (player->priv->playlist_view),
			    TRUE, TRUE, 0);

	player->priv->player = monkey_media_player_new (&error);

	if (error != NULL) {
		rb_error_dialog (_("Failed to create player, exiting. Error was:\n%s"), error->message);
		g_error_free (error);
		exit (-1);
	}

	g_signal_connect (G_OBJECT (player->priv->player),
			  "eos",
			  G_CALLBACK (eos_cb), player);

	player->priv->timeout = g_timeout_add (1000, (GSourceFunc) sync_time_timeout, player);

	gtk_widget_show_all (GTK_WIDGET (player));

	update_buttons (player);
	nullify_info (player);
}

static void
rb_player_finalize (GObject *object)
{
	RBPlayer *player;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PLAYER (object));

	player = RB_PLAYER (object);

	g_return_if_fail (player->priv != NULL);

	rb_clear (player);

	g_object_unref (G_OBJECT (player->priv->player));

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

static void
update_buttons (RBPlayer *player)
{
	if (rb_node_view_get_first_node (player->priv->playlist_view) == NULL) {
		gtk_widget_hide (player->priv->pause);
		gtk_widget_show (player->priv->play);

		gtk_widget_set_sensitive (player->priv->play, FALSE);
		gtk_widget_set_sensitive (player->priv->previous, FALSE);
		gtk_widget_set_sensitive (player->priv->next, FALSE);

		return;
	}

	gtk_widget_set_sensitive (player->priv->play, TRUE);

	gtk_widget_set_sensitive (player->priv->next,
				  (rb_node_view_get_next_node (player->priv->playlist_view) != NULL));
	gtk_widget_set_sensitive (player->priv->previous,
				  (rb_node_view_get_previous_node (player->priv->playlist_view) != NULL));

	switch (rb_player_get_state (player)) {
	case RB_PLAYER_PLAYING:
		gtk_widget_hide (player->priv->play);
		gtk_widget_show (player->priv->pause);
		break;
	case RB_PLAYER_STOPPED:
	case RB_PLAYER_PAUSED:
		gtk_widget_hide (player->priv->pause);
		gtk_widget_show (player->priv->play);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static void
rb_next (RBPlayer *player)
{
	set_playing (player, rb_node_view_get_next_node (player->priv->playlist_view));

	rb_player_set_state (player, RB_PLAYER_PLAYING);
}

static void
rb_previous (RBPlayer *player)
{
	set_playing (player, rb_node_view_get_previous_node (player->priv->playlist_view));

	rb_player_set_state (player, RB_PLAYER_PLAYING);
}

static void
rb_play (RBPlayer *player)
{
	rb_player_set_state (player, RB_PLAYER_PLAYING);
}

static void
rb_pause (RBPlayer *player)
{
	rb_player_set_state (player, RB_PLAYER_PAUSED);
}

static void
rb_clear (RBPlayer *player)
{
	GPtrArray *kids;
	int i;

	kids = rb_node_get_children (player->priv->playlist);
	rb_node_thaw (player->priv->playlist);
	for (i = kids->len - 1; i >= 0; i--) {
		RBNode *kid;

		kid = g_ptr_array_index (kids, i);

		rb_node_remove_child (player->priv->playlist, kid);

		if (rb_node_clone_of (kid) != NULL)
			rb_node_unref (kid);
	}
}

static void
rb_shuffle (RBPlayer *player)
{
	/* FIXME */
	update_buttons (player);
}

static void
append_repeat_node (RBPlayer *player)
{
	/* FIXME */
	update_buttons (player);
}

static RBNode *
insert_song (RBPlayer *player, RBNode *song, int index)
{
	RBNode *node;

	/* FIXME order */

	if (rb_node_has_child (player->priv->playlist, song)) {
		/* insert a clone instead */
		node = rb_node_new_clone (song);
	} else
		node = song;

	rb_node_add_child (player->priv->playlist, node);

	if (player->priv->playing == NULL) {
		set_playing (player, node);

		rb_player_set_state (player, RB_PLAYER_PAUSED);
	}

	update_buttons (player);

	return node;
}

static RBNode *
prepend_song (RBPlayer *player, RBNode *song)
{
	return insert_song (player, song, 0);
}

static RBNode *
append_song (RBPlayer *player, RBNode *song)
{
	return insert_song (player, song, -1);
}

#if 0
static void
delete_song (RBPlayer *player, RBNode *song)
{
	if (player->priv->playing == song) {
		RBNode *next;

		next = rb_node_view_get_next_node (player->priv->playlist_view);
		if (next == NULL)
			next = rb_node_view_get_previous_node (player->priv->playlist_view);

		rb_player_set_state (player, RB_PLAYER_PAUSED);

		set_playing (player, next);
	}

	rb_node_remove_child (player->priv->playlist, song);

	if (rb_node_clone_of (song) != NULL)
		rb_node_unref (song);
}
#endif

static char *
escape_for_allmusic (char *in)
{
	char *tmp = in;

	while ((tmp = strstr (tmp, " ")) != NULL)
		*tmp = '|';

	return in;
}

#define ALBUM_INFO_URL(xALBUM)   escape_for_allmusic (g_strdup_printf ("http://www.allmusic.com/cg/amg.dll?p=amg&opt1=2&sql=%s", xALBUM))
#define ARTIST_INFO_URL(xARTIST) escape_for_allmusic (g_strdup_printf ("http://www.allmusic.com/cg/amg.dll?p=amg&opt1=1&sql=%s", xARTIST))

static void
sync_info (RBPlayer *player)
{
	const char *title, *artist;
	char *url, *title_str;

	title = rb_node_get_property_string (player->priv->playing, RB_NODE_PROP_NAME);
	rb_ellipsizing_label_set_text (RB_ELLIPSIZING_LABEL (player->priv->song_label), title);
	check_song_tooltip (player);

	artist = rb_node_get_property_string (player->priv->playing, RB_NODE_PROP_ARTIST);
	url = ARTIST_INFO_URL (artist);
	rb_link_set (RB_LINK (player->priv->artist_link), artist,
		     _("Get information about this artist from the web"), url);
	g_free (url);

	gtk_notebook_set_current_page (GTK_NOTEBOOK (player->priv->info_notebook), 0);

	if (artist != NULL && title != NULL)
		title_str = g_strdup_printf (_("%s - %s"), artist, title);
	else if (artist != NULL && title == NULL)
		title_str = g_strdup_printf (_("%s - Unknown"), artist);
	else if (artist == NULL && title != NULL)
		title_str = g_strdup_printf ("%s", title);
	else
		title_str = NULL;

	rb_set_title (player->priv->rb, title_str);

	g_free (title_str);
}

static void
set_playing (RBPlayer *player, RBNode *song)
{
	if (player->priv->playing == song)
		return;

	player->priv->playing = song;

	rb_node_view_set_playing_node (player->priv->playlist_view, song);

	if (song == NULL)
		nullify_info (player);
	else {
		GError *error = NULL;

		monkey_media_player_open (player->priv->player,
					  rb_node_get_property_string (song, RB_NODE_PROP_LOCATION),
					  &error);
		if (error != NULL) {
			rb_error_dialog (_("Failed to create stream, error was:\n%s"), error->message);
			g_error_free (error);
			return;
		}

		sync_info (player);
	}
}

static void
nullify_info (RBPlayer *player)
{
	gtk_notebook_set_current_page (GTK_NOTEBOOK (player->priv->info_notebook), 1);

	if (player->priv->rb != NULL)
		rb_set_title (player->priv->rb, NULL);
}

void
rb_player_queue_song (RBPlayer *player,
		      RBNode *song,
		      gboolean start_playing)
{
	if (start_playing) {
		RBNode *real;

		real = prepend_song (player, song);
		set_playing (player, real);

		rb_player_set_state (player, RB_PLAYER_PLAYING);
	} else {
		append_song (player, song);
	}
}

RBNode *
rb_player_get_song (RBPlayer *player)
{
	return player->priv->playing;
}

void
rb_player_set_state (RBPlayer *player,
		     RBPlayerState state)
{
	player->priv->state = state;

	switch (state) {
	case RB_PLAYER_STOPPED:
		monkey_media_player_close (player->priv->player);
		break;
	case RB_PLAYER_PLAYING:
		monkey_media_player_play (player->priv->player);
		break;
	case RB_PLAYER_PAUSED:
		monkey_media_player_pause (player->priv->player);
		break;
	}

	check_view_state (player);

	update_buttons (player);

	sync_time (player);
}

RBPlayerState
rb_player_get_state (RBPlayer *player)
{
	return player->priv->state;
}

void
rb_player_load_playlist (RBPlayer *player,
			 const char *uri,
			 GError **error)
{
	xmlDocPtr doc;
	xmlNodePtr child, root;
	char *tmp;

	g_return_if_fail (RB_IS_PLAYER (player));

	rb_clear (player);

	if (g_file_test (uri, G_FILE_TEST_EXISTS) == FALSE)
		return;

	doc = xmlParseFile (uri);
	if (doc == NULL) {
		rb_warning_dialog (_("Failed to parse %s as playlist"), uri);
		return;
	}

	root = xmlDocGetRootElement (doc);

	tmp = xmlGetProp (root, "version");
	if (tmp == NULL || strcmp (tmp, PLAYLIST_XML_VERSION) != 0) {
		g_free (tmp);
		xmlFreeDoc (doc);
		unlink (uri);
		return;
	}
	g_free (tmp);

	for (child = root->children; child != NULL; child = child->next) {
		long id;
		RBNode *node;

		tmp = xmlGetProp (child, "id");
		if (tmp == NULL)
			continue;
		id = atol (tmp);
		g_free (tmp);

		node = rb_node_get_from_id (id);

		if (node == NULL)
			continue;

		rb_node_add_child (player->priv->playlist, node);
	}

	xmlFreeDoc (doc);

	update_buttons (player);
}

void
rb_player_save_playlist (RBPlayer *player,
			 const char *uri,
			 const char *name,
			 GError **error)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	GPtrArray *kids;
	int i;

	g_return_if_fail (RB_IS_PLAYER (player));

	xmlIndentTreeOutput = TRUE;
	doc = xmlNewDoc ("1.0");

	root = xmlNewDocNode (doc, NULL, "rhythmbox_playlist", NULL);
	xmlSetProp (root, "version", PLAYLIST_XML_VERSION);
	xmlSetProp (root, "name", name);
	xmlDocSetRootElement (doc, root);

	kids = rb_node_get_children (player->priv->playlist);
	for (i = 0; i < kids->len; i++) {
		RBNode *node = g_ptr_array_index (kids, i);
		xmlNodePtr xmlnode;
		char *tmp;

		xmlnode = xmlNewChild (root, NULL, "node_pointer", NULL);

		tmp = g_strdup_printf ("%ld", rb_node_get_id (node));
		xmlSetProp (xmlnode, "id", tmp);
		g_free (tmp);
	}
	rb_node_thaw (player->priv->playlist);

	xmlSaveFormatFile (uri, doc, 1);
	xmlFreeDoc (doc);
}

static void
handle_songs_func (RBNode *node,
		   RBPlayer *player)
{
	append_song (player, node);
}

static void
drag_data_received_cb (GtkWidget *widget,
		       GdkDragContext *context,
		       int x, int y,
		       GtkSelectionData *data,
		       guint info, guint time,
		       RBPlayer *player)
{
	/* FIXME insert at a particular position */
	GtkTargetList *tlist;
	GdkAtom target;
	RBNode *node;

	tlist = gtk_target_list_new (target_table, G_N_ELEMENTS (target_table));
	target = gtk_drag_dest_find_target (widget, context, tlist);
	gtk_target_list_unref (tlist);

	if (target == GDK_NONE)
		return;

	g_assert (info == RB_LIBRARY_DND_NODE_ID);

	node = rb_node_get_from_id (atol (data->data));

	if (node != NULL) {
		rb_library_handle_songs (rb_get_library (player->priv->rb), node,
					 (GFunc) handle_songs_func, player);
	}

	gtk_drag_finish (context, TRUE, FALSE, time);
}

static void
play_cb (GtkWidget *widget,
	 RBPlayer *player)
{
	rb_play (player);
}

static void
pause_cb (GtkWidget *widget,
	 RBPlayer *player)
{
	rb_pause (player);
}

static void
previous_cb (GtkWidget *widget,
	 RBPlayer *player)
{
	rb_previous (player);
}

static void
next_cb (GtkWidget *widget,
	 RBPlayer *player)
{
	rb_next (player);
}

static void
shuffle_cb (GtkWidget *widget,
	    RBPlayer *player)
{
	rb_shuffle (player);
}

static void
repeat_cb (GtkWidget *widget,
	   RBPlayer *player)
{
	append_repeat_node (player);
}

static void
eos_cb (MonkeyMediaPlayer *mm_player,
	RBPlayer *player)
{
	RBNode *next;

	GDK_THREADS_ENTER ();

	next = rb_node_view_get_next_node (player->priv->playlist_view);
	if (next == NULL) {
		player->priv->state = RB_PLAYER_PAUSED;
		next = rb_node_view_get_first_node (player->priv->playlist_view);
	}

	set_playing (player, next);

	update_buttons (player);

	sync_time (player);

	GDK_THREADS_LEAVE ();
}

static void
node_activated_cb (RBNodeView *view,
		   RBNode *song,
		   RBPlayer *player)
{
	set_playing (player, song);

	rb_player_set_state (player, RB_PLAYER_PLAYING);
}

static void
do_real_seek (RBPlayer *player)
{
	long new, duration;
	double progress;

	duration = rb_node_get_property_long (player->priv->playing, RB_NODE_PROP_REAL_DURATION);
	progress = gtk_adjustment_get_value (player->priv->song_adjustment);

	new = (long) (progress * duration);
	monkey_media_player_set_time (player->priv->player, new);

	if (new != player->priv->slider_drag_info.latest_set_time)
		player->priv->slider_drag_info.latest_set_time = new;
}

static void
sync_time (RBPlayer *player)
{
	long elapsed, duration;
	double progress = 0.0;
	char *elapsed_str, *tooltip_str;

	if (player->priv->slider_drag_info.slider_dragging)
		elapsed = player->priv->slider_drag_info.fake_elapsed;
	else {
		elapsed = monkey_media_player_get_time (player->priv->player);
	}

	duration = rb_node_get_property_long (player->priv->playing, RB_NODE_PROP_REAL_DURATION);

	if (elapsed > 0)
		progress = (double) ((long) elapsed) / duration;

	if (!player->priv->slider_drag_info.slider_dragging) {
		player->priv->slider_drag_info.slider_locked = TRUE;
		gtk_adjustment_set_value (player->priv->song_adjustment, progress);
		player->priv->slider_drag_info.slider_locked = FALSE;
	}

	elapsed_str = g_strdup_printf (_("%ld:%.2ld"), elapsed / 60, elapsed % 60);
	gtk_label_set_text (GTK_LABEL (player->priv->time_label), elapsed_str);
	g_free (elapsed_str);

	tooltip_str = g_strdup_printf (_("Total %ld:%.2ld (%ld:%.2ld remaining)"),
				       duration / 60, duration % 60,
				       (duration - elapsed) / 60, (duration - elapsed) % 60);
	gtk_tooltips_set_tip (player->priv->tooltips,
			      player->priv->time_ebox,
			      tooltip_str, NULL);
	g_free (tooltip_str);
}

static gboolean
slider_pressed_cb (GtkWidget *widget, GdkEventButton *event, RBPlayer *player)
{
	player->priv->slider_drag_info.slider_dragging = TRUE;
        player->priv->slider_drag_info.latest_set_time = -1;

        return FALSE;
}

static gboolean
slider_moved_idle (RBPlayer *player)
{
	do_real_seek (player);

        return FALSE;
}

static gboolean
slider_moved_cb (GtkWidget *widget,
                 GdkEventMotion *event,
                 RBPlayer *player)
{
        double progress;
        long duration;

        if (player->priv->slider_drag_info.slider_dragging == FALSE)
                return FALSE;

        progress = gtk_adjustment_get_value (player->priv->song_adjustment);
	duration = rb_node_get_property_long (player->priv->playing, RB_NODE_PROP_REAL_DURATION);

        player->priv->slider_drag_info.fake_elapsed = (long) (progress * duration);

	sync_time (player);

        if (player->priv->slider_drag_info.slider_moved_timeout != 0) {
                g_source_remove (player->priv->slider_drag_info.slider_moved_timeout);
                player->priv->slider_drag_info.slider_moved_timeout = 0;
        }
        player->priv->slider_drag_info.slider_moved_timeout =
                g_timeout_add (40, (GSourceFunc) slider_moved_idle, player);

        return FALSE;
}

static gboolean
slider_released_cb (GtkWidget *widget, GdkEventButton *event, RBPlayer *player)
{
        if (player->priv->slider_drag_info.slider_dragging == FALSE)
                return FALSE;

	do_real_seek (player);

        player->priv->slider_drag_info.slider_dragging = FALSE;

	sync_time (player);

        if (player->priv->slider_drag_info.slider_moved_timeout != 0) {
                g_source_remove (player->priv->slider_drag_info.slider_moved_timeout);
                player->priv->slider_drag_info.slider_moved_timeout = 0;
        }

        return FALSE;
}

static gboolean
slider_changed_idle (RBPlayer *player)
{
	/* force a sync */
        slider_released_cb (player->priv->song_scale, NULL, player);

        player->priv->slider_drag_info.value_changed_update_handler = 0;

        return FALSE;
}

static void
slider_changed_cb (GtkWidget *widget, RBPlayer *player)
{
	if (player->priv->slider_drag_info.slider_dragging == FALSE &&
            player->priv->slider_drag_info.slider_locked == FALSE &&
            player->priv->slider_drag_info.value_changed_update_handler == 0) {
                player->priv->slider_drag_info.slider_dragging = TRUE;
                player->priv->slider_drag_info.value_changed_update_handler =
                        g_idle_add ((GSourceFunc) slider_changed_idle, player);
        }
}

static gboolean
sync_time_timeout (RBPlayer *player)
{
	if (player->priv->slider_drag_info.slider_dragging)
		return TRUE;

	if (player->priv->playing == NULL)
		return TRUE;

	sync_time (player);

	return TRUE;
}

static void
check_song_tooltip (RBPlayer *player)
{
	const char *title;

	if (rb_ellipsizing_label_get_ellipsized (RB_ELLIPSIZING_LABEL (player->priv->song_label)))
		title = rb_node_get_property_string (player->priv->playing, RB_NODE_PROP_NAME);
	else
		title = NULL;

	gtk_tooltips_set_tip (player->priv->tooltips,
			      player->priv->song_label_ebox,
			      title, NULL);
}

static void
song_label_size_allocate_cb (GtkWidget *widget,
			     GtkAllocation *allocation,
			     RBPlayer *player)
{
	check_song_tooltip (player);
}

static void
check_view_state (RBPlayer *player)
{
	rb_node_view_set_playing (player->priv->playlist_view,
				  (rb_player_get_state (player) == RB_PLAYER_PLAYING));
}
