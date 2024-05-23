/*
 *  Copyright (C) 2010 Jonathan Matthew <jonathan@d14n.org>
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

#include "config.h"

#include <glib/gi18n.h>

#include "rb-sync-state-ui.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"

#include "rb-segmented-bar.h"

struct _RBSyncStateUIPrivate
{
	/* we don't own a reference on this (maybe we should?) */
	RBSyncState *state;

	/* or any of these */
	GtkWidget *add_count;
	GtkWidget *remove_count;
	RBSyncBarData sync_before;
	RBSyncBarData sync_after;
};

enum {
	PROP_0,
	PROP_SYNC_STATE
};

G_DEFINE_TYPE (RBSyncStateUI, rb_sync_state_ui, GTK_TYPE_BOX)


static char *
value_formatter (gdouble percent, RBSyncBarData *bar)
{
	return g_format_size (percent * bar->capacity);
}

void
rb_sync_state_ui_create_bar (RBSyncBarData *bar, guint64 capacity, GtkWidget *label)
{
	bar->widget = rb_segmented_bar_new ();
	bar->capacity = capacity;
	g_object_set (bar->widget, "show-labels", TRUE, "show-reflection", FALSE, NULL);

	rb_segmented_bar_set_value_formatter (RB_SEGMENTED_BAR (bar->widget),
					      (RBSegmentedBarValueFormatter) value_formatter,
					      bar);

	bar->music_segment = rb_segmented_bar_add_segment (RB_SEGMENTED_BAR (bar->widget),_("Music"), 0.0, 0.2, 0.4, 0.65, 1.0);
	bar->podcast_segment = rb_segmented_bar_add_segment (RB_SEGMENTED_BAR (bar->widget), _("Podcasts"), 0.0, 0.96, 0.47, 0.0, 1.0);
	bar->other_segment = rb_segmented_bar_add_segment (RB_SEGMENTED_BAR (bar->widget), _("Other"), 0.0, 0.45, 0.82, 0.08, 1.0);
	bar->free_segment = rb_segmented_bar_add_segment_default_color (RB_SEGMENTED_BAR (bar->widget), _("Available"), 1.0);

	/* set up label relationship */
	if (label != NULL) {
		AtkObject *lobj;
		AtkObject *robj;

		lobj = gtk_widget_get_accessible (label);
		robj = gtk_widget_get_accessible (bar->widget);

		atk_object_add_relationship (lobj, ATK_RELATION_LABEL_FOR, robj);
		atk_object_add_relationship (robj, ATK_RELATION_LABELLED_BY, lobj);
	}
}


void
rb_sync_state_ui_update_volume_usage (RBSyncBarData *bar, RBSyncState *state)
{
	RBMediaPlayerSource *source;
	double fraction;
	guint64 total_other;
	guint64 free_space;

	g_object_get (state, "source", &source, NULL);
	free_space = rb_media_player_source_get_free_space (source);
	g_object_unref (source);

	total_other = bar->capacity - (free_space + state->total_music_size + state->total_podcast_size);

	fraction = (double)state->total_music_size/(double)bar->capacity;
	rb_segmented_bar_update_segment (RB_SEGMENTED_BAR (bar->widget),
					 bar->music_segment,
					 fraction);
	fraction = (double)state->total_podcast_size/(double)bar->capacity;
	rb_segmented_bar_update_segment (RB_SEGMENTED_BAR (bar->widget),
					 bar->podcast_segment,
					 fraction);
	fraction = (double)total_other/(double)bar->capacity;
	rb_segmented_bar_update_segment (RB_SEGMENTED_BAR (bar->widget),
					 bar->other_segment,
					 fraction);
	fraction = (double)free_space/(double)bar->capacity;
	rb_segmented_bar_update_segment (RB_SEGMENTED_BAR (bar->widget),
					 bar->free_segment,
					 fraction);
}

static void
update_sync_after_bar (RBSyncBarData *bar, RBSyncState *state)
{
	RBMediaPlayerSource *source;
	RBSyncSettings *settings;
	double music_fraction;
	double podcast_fraction;
	double other_fraction;
	double free_fraction;
	guint64 total_other_size;

	g_object_get (state,
		      "source", &source,
		      "sync-settings", &settings,
		      NULL);

	if (rb_sync_settings_has_enabled_groups (settings, SYNC_CATEGORY_MUSIC) ||
	    rb_sync_settings_sync_category (settings, SYNC_CATEGORY_MUSIC)) {
		music_fraction = (double)state->sync_music_size / (double)bar->capacity;
	} else {
		music_fraction = (double)state->total_music_size / (double)bar->capacity;
	}
	if (rb_sync_settings_has_enabled_groups (settings, SYNC_CATEGORY_PODCAST) ||
	    rb_sync_settings_sync_category (settings, SYNC_CATEGORY_PODCAST)) {
		podcast_fraction = (double)state->sync_podcast_size / (double)bar->capacity;
	} else {
		podcast_fraction = (double)state->total_podcast_size / (double)bar->capacity;
	}

	total_other_size = bar->capacity - (rb_media_player_source_get_free_space (source) + state->total_music_size + state->total_podcast_size);
	other_fraction = (double)total_other_size / (double)bar->capacity;

	free_fraction = 1.0 - (music_fraction + podcast_fraction + other_fraction);
	if (free_fraction < 0.0) {
		free_fraction = 0.0;
	}

	rb_segmented_bar_update_segment (RB_SEGMENTED_BAR (bar->widget),
					 bar->music_segment,
					 music_fraction);
	rb_segmented_bar_update_segment (RB_SEGMENTED_BAR (bar->widget),
					 bar->podcast_segment,
					 podcast_fraction);
	rb_segmented_bar_update_segment (RB_SEGMENTED_BAR (bar->widget),
					 bar->other_segment,
					 other_fraction);
	rb_segmented_bar_update_segment (RB_SEGMENTED_BAR (bar->widget),
					 bar->free_segment,
					 free_fraction);

	g_object_unref (source);
	g_object_unref (settings);
}

static void
sync_state_updated (RBSyncState *state, RBSyncStateUI *ui)
{
	char *text;
	rb_debug ("sync state updated");

	/* sync before state */
	rb_sync_state_ui_update_volume_usage (&ui->priv->sync_before, state);
	update_sync_after_bar (&ui->priv->sync_after, state);

	/* other stuff */
	text = g_strdup_printf ("%d", state->sync_add_count);
	gtk_label_set_text (GTK_LABEL (ui->priv->add_count), text);
	g_free (text);

	text = g_strdup_printf ("%d", state->sync_remove_count);
	gtk_label_set_text (GTK_LABEL (ui->priv->remove_count), text);
	g_free (text);
}


GtkWidget *
rb_sync_state_ui_new (RBSyncState *state)
{
	GObject *ui;
	ui = g_object_new (RB_TYPE_SYNC_STATE_UI,
			   "sync-state", state,
			   NULL);
	return GTK_WIDGET (ui);
}

static void
rb_sync_state_ui_init (RBSyncStateUI *ui)
{
	ui->priv = G_TYPE_INSTANCE_GET_PRIVATE (ui, RB_TYPE_SYNC_STATE_UI, RBSyncStateUIPrivate);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (ui), GTK_ORIENTATION_VERTICAL);
}

static void
build_ui (RBSyncStateUI *ui)
{
	RBMediaPlayerSource *source;
	GtkWidget *widget;
	GtkWidget *container;
	guint64 capacity;
	GtkBuilder *builder;

	g_object_get (ui->priv->state, "source", &source, NULL);
	capacity = rb_media_player_source_get_capacity (source);
	g_object_unref (source);

	builder = rb_builder_load ("sync-state.ui", NULL);
	if (builder == NULL) {
		g_warning ("Couldn't load sync-state.ui");
		return;
	}

	container = GTK_WIDGET (gtk_builder_get_object (builder, "sync-state-ui"));
	gtk_box_pack_start (GTK_BOX (ui), container, TRUE, TRUE, 0);

	ui->priv->add_count = GTK_WIDGET (gtk_builder_get_object (builder, "added-tracks"));
	ui->priv->remove_count = GTK_WIDGET (gtk_builder_get_object (builder, "removed-tracks"));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "sync-before-label"));
	rb_sync_state_ui_create_bar (&ui->priv->sync_before, capacity, widget);
	container = GTK_WIDGET (gtk_builder_get_object (builder, "sync-before-container"));
	gtk_container_add (GTK_CONTAINER (container), ui->priv->sync_before.widget);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "sync-after-label"));
	rb_sync_state_ui_create_bar (&ui->priv->sync_after, capacity, widget);
	container = GTK_WIDGET (gtk_builder_get_object (builder, "sync-after-container"));
	gtk_container_add (GTK_CONTAINER (container), ui->priv->sync_after.widget);

	g_object_unref (builder);
}

static void
impl_constructed (GObject *object)
{
	RBSyncStateUI *ui = RB_SYNC_STATE_UI (object);

	build_ui (ui);
	sync_state_updated (ui->priv->state, ui);

	g_signal_connect_object (ui->priv->state,
				 "updated",
				 G_CALLBACK (sync_state_updated),
				 ui, 0);

	RB_CHAIN_GOBJECT_METHOD(rb_sync_state_ui_parent_class, constructed, object);
}


static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBSyncStateUI *ui = RB_SYNC_STATE_UI (object);
	switch (prop_id) {
	case PROP_SYNC_STATE:
		ui->priv->state = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBSyncStateUI *ui = RB_SYNC_STATE_UI (object);
	switch (prop_id) {
	case PROP_SYNC_STATE:
		g_value_set_object (value, ui->priv->state);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}


static void
rb_sync_state_ui_class_init (RBSyncStateUIClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = impl_constructed;
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	g_object_class_install_property (object_class,
					 PROP_SYNC_STATE,
					 g_param_spec_object ("sync-state",
							      "sync-state",
							      "RBSyncState instance",
							      RB_TYPE_SYNC_STATE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (object_class, sizeof (RBSyncStateUIPrivate));
}
