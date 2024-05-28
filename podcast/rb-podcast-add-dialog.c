/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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
#include <gtk/gtk.h>

#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-podcast-add-dialog.h"
#include "rb-podcast-search.h"
#include "rb-podcast-entry-types.h"
#include "rb-builder-helpers.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-cut-and-paste-code.h"
#include "rb-search-entry.h"
#include "nautilus-floating-bar.h"

static void rb_podcast_add_dialog_class_init (RBPodcastAddDialogClass *klass);
static void rb_podcast_add_dialog_init (RBPodcastAddDialog *dialog);

enum {
	PROP_0,
	PROP_PODCAST_MANAGER,
	PROP_SHELL,
};

enum {
	CLOSE,
	CLOSED,
	LAST_SIGNAL
};

enum {
	FEED_COLUMN_TITLE = 0,
	FEED_COLUMN_AUTHOR,
	FEED_COLUMN_IMAGE,
	FEED_COLUMN_IMAGE_FILE,
	FEED_COLUMN_EPISODE_COUNT,
	FEED_COLUMN_PARSED_FEED,
	FEED_COLUMN_DATE,
};

struct RBPodcastAddDialogPrivate
{
	RBPodcastManager *podcast_mgr;
	RhythmDB *db;
	RBShell *shell;

	GtkWidget *feed_view;
	GtkListStore *feed_model;
	GtkWidget *feed_status;

	GtkWidget *episode_view;

	GtkWidget *text_entry;
	GtkWidget *subscribe_button;
	GtkWidget *info_bar;
	GtkWidget *info_bar_message;

	RBSearchEntry *search_entry;

	gboolean paned_size_set;
	gboolean have_selection;
	gboolean clearing;
	GtkTreeIter selected_feed;

	int running_searches;
	gboolean search_successful;
	int reset_count;
};

/* various prefixes that identify things we treat as feed URLs rather than search terms */
static const char *podcast_uri_prefixes[] = {
	"http://",
	"https://",
	"feed://",
	"zcast://",
	"zune://",
	"itpc://",
	"itms://",
	"itmss://",
	"file://",
};

/* number of search results to request from each available search */
#define PODCAST_SEARCH_LIMIT		25

#define PODCAST_IMAGE_SIZE		50

static guint signals[LAST_SIGNAL] = {0,};

G_DEFINE_TYPE (RBPodcastAddDialog, rb_podcast_add_dialog, GTK_TYPE_BOX);


static gboolean
remove_all_feeds_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, RBPodcastAddDialog *dialog)
{
	RBPodcastChannel *channel;
	gtk_tree_model_get (model, iter, FEED_COLUMN_PARSED_FEED, &channel, -1);
	rb_podcast_parse_channel_unref (channel);
	return FALSE;
}

static void
remove_all_feeds (RBPodcastAddDialog *dialog)
{
	/* remove all feeds from the model and free associated data */
	gtk_tree_model_foreach (GTK_TREE_MODEL (dialog->priv->feed_model),
				(GtkTreeModelForeachFunc) remove_all_feeds_cb,
				dialog);

	dialog->priv->clearing = TRUE;
	gtk_list_store_clear (dialog->priv->feed_model);
	dialog->priv->clearing = FALSE;

	dialog->priv->have_selection = FALSE;
	gtk_widget_set_sensitive (dialog->priv->subscribe_button, FALSE);
	gtk_widget_hide (dialog->priv->feed_status);
}

static void
add_posts_for_feed (RBPodcastAddDialog *dialog, RBPodcastChannel *channel)
{
	GList *l;
	gulong position;

	position = 1;
	for (l = channel->posts; l != NULL; l = l->next) {
		RBPodcastItem *item = (RBPodcastItem *) l->data;

		rb_podcast_manager_add_post (dialog->priv->db,
					     TRUE,
					     NULL,
					     channel->title ? channel->title : channel->url,
					     item->title,
					     channel->url,
					     channel->author,
					     item->author,
					     item->url,
					     item->description,
					     item->guid,
					     item->img,
					     (item->pub_date > 0 ? item->pub_date : channel->pub_date),
					     item->duration,
					     position++,
					     item->filesize);
	}

	rhythmdb_commit (dialog->priv->db);
}

static void
image_file_read_cb (GObject *file, GAsyncResult *result, RBPodcastAddDialog *dialog)
{
	GFileInputStream *stream;
	GdkPixbuf *pixbuf;
	GError *error = NULL;

	stream = g_file_read_finish (G_FILE (file), result, &error);
	if (error != NULL) {
		rb_debug ("podcast image read failed: %s", error->message);
		g_clear_error (&error);
		g_object_unref (dialog);
		return;
	}

	pixbuf = gdk_pixbuf_new_from_stream_at_scale (G_INPUT_STREAM (stream), PODCAST_IMAGE_SIZE, PODCAST_IMAGE_SIZE, TRUE, NULL, &error);
	if (error != NULL) {
		rb_debug ("podcast image load failed: %s", error->message);
		g_clear_error (&error);
	} else {
		GtkTreeIter iter;

		if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (dialog->priv->feed_model), &iter)) {
			do {
				GFile *feedfile;
				gtk_tree_model_get (GTK_TREE_MODEL (dialog->priv->feed_model), &iter,
						    FEED_COLUMN_IMAGE_FILE, &feedfile,
						    -1);
				if (feedfile == G_FILE (file)) {
					gtk_list_store_set (dialog->priv->feed_model,
							    &iter,
							    FEED_COLUMN_IMAGE, g_object_ref (pixbuf),
							    -1);
					break;
				}
			} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (dialog->priv->feed_model), &iter));
		}
		g_object_unref (pixbuf);
	}

	g_object_unref (dialog);
	g_object_unref (stream);
}

static void
insert_search_result (RBPodcastAddDialog *dialog, RBPodcastChannel *channel, gboolean select)
{
	GtkTreeIter iter;
	GFile *image_file;
	int episodes;

	rb_podcast_parse_channel_ref (channel);

	if (channel->posts) {
		episodes = g_list_length (channel->posts);
	} else {
		episodes = channel->num_posts;
	}

	/* if there's an image to load, fetch it */
	if (channel->img) {
		rb_debug ("fetching image %s", channel->img);
		image_file = g_file_new_for_uri (channel->img);
	} else {
		image_file = NULL;
	}

	gtk_list_store_insert_with_values (dialog->priv->feed_model,
					   &iter,
					   G_MAXINT,
					   FEED_COLUMN_TITLE, channel->title,
					   FEED_COLUMN_AUTHOR, channel->author,
					   FEED_COLUMN_EPISODE_COUNT, episodes,
					   FEED_COLUMN_IMAGE, NULL,
					   FEED_COLUMN_IMAGE_FILE, image_file,
					   FEED_COLUMN_PARSED_FEED, channel,
					   -1);

	if (image_file != NULL) {
		g_file_read_async (image_file,
				   G_PRIORITY_DEFAULT,
				   NULL,
				   (GAsyncReadyCallback) image_file_read_cb,
				   g_object_ref (dialog));
	}

	if (select) {
		GtkTreeSelection *selection;
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->priv->feed_view));
		gtk_tree_selection_select_iter (selection, &iter);
	}
}

static void
update_feed_status (RBPodcastAddDialog *dialog)
{
	int feeds;
	char *text;

	feeds = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (dialog->priv->feed_model), NULL);
	text = g_strdup_printf (ngettext ("%d feed", "%d feeds", feeds), feeds);
	nautilus_floating_bar_set_primary_label (NAUTILUS_FLOATING_BAR (dialog->priv->feed_status), text);
	g_free (text);

	nautilus_floating_bar_set_show_spinner (NAUTILUS_FLOATING_BAR (dialog->priv->feed_status),
						dialog->priv->running_searches > 0);
	gtk_widget_show (dialog->priv->feed_status);
}


typedef struct {
	RBPodcastAddDialog *dialog;
	RBPodcastChannel *channel;
	gboolean existing;
	gboolean single;
	int reset_count;
} ParseData;

static void
parse_cb (RBPodcastChannel *channel, GError *error, gpointer user_data)
{
	ParseData *data = user_data;
	gboolean is_selected_channel = FALSE;

	g_assert (channel == data->channel);

	if (data->reset_count != data->dialog->priv->reset_count) {
		rb_debug ("dialog reset while parsing");
		rb_podcast_parse_channel_unref (channel);
		g_object_unref (data->dialog);
		g_free (data);
		return;
	}

	/* get selected feed if any */
	if (data->dialog->priv->have_selection) {
		RBPodcastChannel *selected_channel;
		gtk_tree_model_get (GTK_TREE_MODEL (data->dialog->priv->feed_model),
				    &data->dialog->priv->selected_feed,
				    FEED_COLUMN_PARSED_FEED, &selected_channel,
				    -1);
		if (channel == selected_channel)
			is_selected_channel = TRUE;
	}

	if (error != NULL) {
		if (channel->title == NULL || channel->title[0] == '\0') {
			/* fake up a channel with just the url as the
			 * title, allowing the user to subscribe to
			 * the podcast anyway.
			 */
			channel->title = g_strdup (channel->url);
		}

		if (is_selected_channel) {
			const char *message;
			if (g_error_matches (error, RB_PODCAST_PARSE_ERROR, RB_PODCAST_PARSE_ERROR_NO_ITEMS)) {
				message = error->message;
			} else {
				message = _("Unable to load the feed. Check your network connection.");
			}

			gtk_label_set_label (GTK_LABEL (data->dialog->priv->info_bar_message), message);
			gtk_widget_show (data->dialog->priv->info_bar);
		}
	} else {
		if (is_selected_channel) {
			gtk_widget_hide (data->dialog->priv->info_bar);
		}
	}

	if (channel->is_opml) {
		GList *l;
		/* convert each item into its own channel */
		for (l = channel->posts; l != NULL; l = l->next) {
			RBPodcastChannel *channel;
			RBPodcastItem *item;

			item = l->data;
			channel = rb_podcast_parse_channel_new ();
			channel->url = g_strdup (item->url);
			channel->title = g_strdup (item->title);
			/* none of the other fields get populated anyway */
			insert_search_result (data->dialog, channel, FALSE);
			rb_podcast_parse_channel_unref (channel);
		}
		update_feed_status (data->dialog);
	} else if (data->existing) {
		GtkTreeIter iter;
		gboolean found = FALSE;

		/* find the row for the feed */
		if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (data->dialog->priv->feed_model), &iter)) {
			do {
				RBPodcastChannel *rchannel;
				gtk_tree_model_get (GTK_TREE_MODEL (data->dialog->priv->feed_model), &iter,
						    FEED_COLUMN_PARSED_FEED, &rchannel,
						    -1);
				if (g_strcmp0 (rchannel->url, channel->url) == 0) {
					found = TRUE;
					break;
				}
			} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (data->dialog->priv->feed_model), &iter));
		}

		/* if the row is selected, create entries for the channel contents */
		if (found && data->dialog->priv->have_selection) {
			GtkTreePath *a;
			GtkTreePath *b;

			a = gtk_tree_model_get_path (GTK_TREE_MODEL (data->dialog->priv->feed_model), &iter);
			b = gtk_tree_model_get_path (GTK_TREE_MODEL (data->dialog->priv->feed_model), &data->dialog->priv->selected_feed);
			if (gtk_tree_path_compare (a, b) == 0) {
				add_posts_for_feed (data->dialog, channel);
			}

			gtk_tree_path_free (a);
			gtk_tree_path_free (b);
		}
	} else {
		insert_search_result (data->dialog, channel, data->single);
		update_feed_status (data->dialog);
	}

	rb_podcast_parse_channel_unref (channel);
	g_object_unref (data->dialog);
	g_free (data);
}

static void
parse_search_text (RBPodcastAddDialog *dialog, const char *text)
{
	ParseData *data;
	RBPodcastChannel *channel;

	channel = rb_podcast_parse_channel_new ();
	channel->url = g_strdup (text);

	data = g_new0 (ParseData, 1);
	data->dialog = g_object_ref (dialog);
	data->channel = channel;
	data->existing = FALSE;
	data->single = TRUE;
	data->reset_count = dialog->priv->reset_count;

	rb_podcast_parse_load_feed (data->channel, NULL, parse_cb, data);
}

static void
parse_search_result (RBPodcastAddDialog *dialog, RBPodcastChannel *channel)
{
	ParseData *data;

	data = g_new0 (ParseData, 1);
	data->dialog = g_object_ref (dialog);
	data->channel = rb_podcast_parse_channel_ref (channel);
	data->existing = TRUE;
	data->single = FALSE;
	data->reset_count = dialog->priv->reset_count;

	rb_podcast_parse_load_feed (channel, NULL, parse_cb, data);
}

static void
podcast_search_result_cb (RBPodcastSearch *search, RBPodcastChannel *feed, RBPodcastAddDialog *dialog)
{
	rb_debug ("got result %s from podcast search %s", feed->url, G_OBJECT_TYPE_NAME (search));
	insert_search_result (dialog, feed, FALSE);
}

static void
podcast_search_finished_cb (RBPodcastSearch *search, gboolean successful, RBPodcastAddDialog *dialog)
{
	rb_debug ("podcast search %s finished", G_OBJECT_TYPE_NAME (search));
	g_object_unref (search);

	dialog->priv->search_successful |= successful;
	dialog->priv->running_searches--;
	update_feed_status (dialog);

	if (dialog->priv->running_searches == 0) {
		if (dialog->priv->search_successful == FALSE) {
			gtk_label_set_label (GTK_LABEL (dialog->priv->info_bar_message),
					     _("Unable to search for podcasts. Check your network connection."));
			gtk_widget_show (dialog->priv->info_bar);
			gtk_widget_hide (dialog->priv->feed_status);
		}
	}
}

static void
search_cb (RBSearchEntry *entry, const char *text, RBPodcastAddDialog *dialog)
{
	GList *searches;
	GList *s;
	int i;

	/* remove previous feeds */
	remove_all_feeds (dialog);
	rhythmdb_entry_delete_by_type (dialog->priv->db, RHYTHMDB_ENTRY_TYPE_PODCAST_SEARCH);
	rhythmdb_commit (dialog->priv->db);

	gtk_widget_hide (dialog->priv->info_bar);

	if (text == NULL || text[0] == '\0') {
		return;
	}

	/* if the entered text looks like a feed URL, parse it directly */
	for (i = 0; i < G_N_ELEMENTS (podcast_uri_prefixes); i++) {
		if (g_str_has_prefix (text, podcast_uri_prefixes[i])) {
			parse_search_text (dialog, text);
			return;
		}
	}

	/* not really sure about this one */
	if (g_path_is_absolute (text)) {
		char *uri;

		uri = g_filename_to_uri (text, NULL, NULL);
		if (uri != NULL) {
			parse_search_text (dialog, uri);
			g_free (uri);
		}
		return;
	}

	/* otherwise, try podcast searches */
	dialog->priv->search_successful = FALSE;
	searches = rb_podcast_manager_get_searches (dialog->priv->podcast_mgr);
	for (s = searches; s != NULL; s = s->next) {
		RBPodcastSearch *search = s->data;

		g_signal_connect_object (search, "result", G_CALLBACK (podcast_search_result_cb), dialog, 0);
		g_signal_connect_object (search, "finished", G_CALLBACK (podcast_search_finished_cb), dialog, 0);
		rb_podcast_search_start (search, text, PODCAST_SEARCH_LIMIT);
		dialog->priv->running_searches++;
	}
}

static void
subscribe_selected_feed (RBPodcastAddDialog *dialog)
{
	RBPodcastChannel *channel;

	g_assert (dialog->priv->have_selection);

	rhythmdb_entry_delete_by_type (dialog->priv->db, RHYTHMDB_ENTRY_TYPE_PODCAST_SEARCH);
	rhythmdb_commit (dialog->priv->db);

	/* subscribe selected feed */
	gtk_tree_model_get (GTK_TREE_MODEL (dialog->priv->feed_model),
			    &dialog->priv->selected_feed,
			    FEED_COLUMN_PARSED_FEED, &channel,
			    -1);
	if (channel->status == RB_PODCAST_PARSE_STATUS_SUCCESS) {
		rb_podcast_manager_add_parsed_feed (dialog->priv->podcast_mgr, channel);
	} else {
		rb_podcast_manager_subscribe_feed (dialog->priv->podcast_mgr, channel->url, TRUE);
	}
}

static void
subscribe_clicked_cb (GtkButton *button, RBPodcastAddDialog *dialog)
{
	if (dialog->priv->have_selection == FALSE) {
		rb_debug ("no selection");
		return;
	}

	subscribe_selected_feed (dialog);

	dialog->priv->clearing = TRUE;
	gtk_list_store_remove (GTK_LIST_STORE (dialog->priv->feed_model), &dialog->priv->selected_feed);
	dialog->priv->clearing = FALSE;

	gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->priv->feed_view)));
	dialog->priv->have_selection = FALSE;
	gtk_widget_set_sensitive (dialog->priv->subscribe_button, FALSE);
}

static void
close_clicked_cb (GtkButton *button, RBPodcastAddDialog *dialog)
{
	g_signal_emit (dialog, signals[CLOSED], 0);
}

static void
feed_activated_cb (GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, RBPodcastAddDialog *dialog)
{
	gtk_tree_model_get_iter (GTK_TREE_MODEL (dialog->priv->feed_model), &dialog->priv->selected_feed, path);
	dialog->priv->have_selection = TRUE;

	subscribe_selected_feed (dialog);

	dialog->priv->have_selection = FALSE;

	g_signal_emit (dialog, signals[CLOSED], 0);
}

static void
feed_selection_changed_cb (GtkTreeSelection *selection, RBPodcastAddDialog *dialog)
{
	GtkTreeModel *model;

	if (dialog->priv->clearing)
		return;

	gtk_widget_hide (dialog->priv->info_bar);

	dialog->priv->have_selection =
		gtk_tree_selection_get_selected (selection, &model, &dialog->priv->selected_feed);
	gtk_widget_set_sensitive (dialog->priv->subscribe_button, dialog->priv->have_selection);

	rhythmdb_entry_delete_by_type (dialog->priv->db, RHYTHMDB_ENTRY_TYPE_PODCAST_SEARCH);
	rhythmdb_commit (dialog->priv->db);

	if (dialog->priv->have_selection) {
		RBPodcastChannel *channel = NULL;

		gtk_tree_model_get (model,
				    &dialog->priv->selected_feed,
				    FEED_COLUMN_PARSED_FEED, &channel,
				    -1);

		switch (channel->status) {
		case RB_PODCAST_PARSE_STATUS_UNPARSED:
		case RB_PODCAST_PARSE_STATUS_ERROR:
			rb_debug ("parsing feed %s to get posts", channel->url);
			parse_search_result (dialog, channel);
			break;
		case RB_PODCAST_PARSE_STATUS_SUCCESS:
			add_posts_for_feed (dialog, channel);
			break;
		}
	}
}

static void
episode_count_column_cell_data_func (GtkTreeViewColumn *column,
				     GtkCellRenderer *renderer,
				     GtkTreeModel *model,
				     GtkTreeIter *iter,
				     gpointer data)
{
	GtkTreeIter parent;
	if (gtk_tree_model_iter_parent (model, &parent, iter)) {
		g_object_set (renderer, "visible", FALSE, NULL);
	} else {
		int count;
		char *text;
		gtk_tree_model_get (model, iter, FEED_COLUMN_EPISODE_COUNT, &count, -1);
		text = g_strdup_printf ("%d", count);
		g_object_set (renderer, "visible", TRUE, "text", text, NULL);
		g_free (text);
	}
}

static void
podcast_post_date_cell_data_func (GtkTreeViewColumn *column,
				  GtkCellRenderer *renderer,
				  GtkTreeModel *tree_model,
				  GtkTreeIter *iter,
				  gpointer data)
{
	RhythmDBEntry *entry;
	gulong value;
	char *str;

	gtk_tree_model_get (tree_model, iter, 0, &entry, -1);

	value = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_POST_TIME);
        if (value == 0) {
		str = g_strdup (_("Unknown"));
	} else {
		str = rb_utf_friendly_time (value);
	}

	g_object_set (G_OBJECT (renderer), "text", str, NULL);
	g_free (str);

	rhythmdb_entry_unref (entry);
}

static gint
podcast_post_date_sort_func (RhythmDBEntry *a,
			     RhythmDBEntry *b,
			     RhythmDBQueryModel *model)
{
	gulong a_val, b_val;
	gint ret;

	a_val = rhythmdb_entry_get_ulong (a, RHYTHMDB_PROP_POST_TIME);
	b_val = rhythmdb_entry_get_ulong (b, RHYTHMDB_PROP_POST_TIME);

	if (a_val != b_val)
		ret = (a_val > b_val) ? 1 : -1;
	else
		ret = rhythmdb_query_model_title_sort_func (a, b, model);

        return ret;
}

static void
episodes_sort_changed_cb (GObject *object, GParamSpec *pspec, RBPodcastAddDialog *dialog)
{
	rb_entry_view_resort_model (RB_ENTRY_VIEW (object));
}

static void
impl_close (RBPodcastAddDialog *dialog)
{
	g_signal_emit (dialog, signals[CLOSED], 0);
}

static gboolean
set_paned_position (GtkWidget *paned)
{
	gtk_paned_set_position (GTK_PANED (paned), gtk_widget_get_allocated_height (paned) / 2);
	g_object_unref (paned);
	return FALSE;
}

static void
paned_size_allocate_cb (GtkWidget *widget, GdkRectangle *allocation, RBPodcastAddDialog *dialog)
{
	if (dialog->priv->paned_size_set == FALSE) {
		dialog->priv->paned_size_set = TRUE;
		g_idle_add ((GSourceFunc) set_paned_position, g_object_ref (widget));
	}
}

static void
episode_entry_activated_cb (RBEntryView *entry_view, RhythmDBEntry *entry, RBPodcastAddDialog *dialog)
{
	rb_debug ("search result podcast entry %s activated", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
	rb_shell_load_uri (dialog->priv->shell,
			   rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION),
			   TRUE,
			   NULL);
}

static void
impl_constructed (GObject *object)
{
	RBPodcastAddDialog *dialog;
	GtkBuilder *builder;
	GtkWidget *widget;
	GtkWidget *paned;
	GtkWidget *overlay;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	RBEntryView *episodes;
	RBShellPlayer *shell_player;
	RhythmDBQuery *query;
	RhythmDBQueryModel *query_model;
	const char *episode_strings[3];
	int xpad, ypad;

	RB_CHAIN_GOBJECT_METHOD (rb_podcast_add_dialog_parent_class, constructed, object);
	dialog = RB_PODCAST_ADD_DIALOG (object);

	g_object_get (dialog->priv->podcast_mgr, "db", &dialog->priv->db, NULL);

	builder = rb_builder_load ("podcast-add-dialog.ui", NULL);

	dialog->priv->info_bar_message = gtk_label_new ("");
	dialog->priv->info_bar = gtk_info_bar_new ();
	g_object_set (dialog->priv->info_bar, "spacing", 0, NULL);
	gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (dialog->priv->info_bar))),
			   dialog->priv->info_bar_message);
	gtk_widget_set_no_show_all (dialog->priv->info_bar, TRUE);
	gtk_box_pack_start (GTK_BOX (dialog), dialog->priv->info_bar, FALSE, FALSE, 0);
	gtk_widget_show (dialog->priv->info_bar_message);

	dialog->priv->subscribe_button = GTK_WIDGET (gtk_builder_get_object (builder, "subscribe-button"));
	g_signal_connect_object (dialog->priv->subscribe_button, "clicked", G_CALLBACK (subscribe_clicked_cb), dialog, 0);
	gtk_widget_set_sensitive (dialog->priv->subscribe_button, FALSE);

	dialog->priv->feed_view = GTK_WIDGET (gtk_builder_get_object (builder, "feed-view"));
	g_signal_connect (dialog->priv->feed_view, "row-activated", G_CALLBACK (feed_activated_cb), dialog);
	g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->priv->feed_view)),
			  "changed",
			  G_CALLBACK (feed_selection_changed_cb),
			  dialog);

	dialog->priv->search_entry = rb_search_entry_new (FALSE);
	gtk_widget_set_size_request (GTK_WIDGET (dialog->priv->search_entry), 400, -1);
	g_object_set (dialog->priv->search_entry,"explicit-mode", TRUE, NULL);
	g_signal_connect (dialog->priv->search_entry, "search", G_CALLBACK (search_cb), dialog);
	g_signal_connect (dialog->priv->search_entry, "activate", G_CALLBACK (search_cb), dialog);
	gtk_container_add (GTK_CONTAINER (gtk_builder_get_object (builder, "search-entry-box")),
			   GTK_WIDGET (dialog->priv->search_entry));

	g_signal_connect (gtk_builder_get_object (builder, "close-button"),
			  "clicked",
			  G_CALLBACK (close_clicked_cb),
			  dialog);

	dialog->priv->feed_model = gtk_list_store_new (7,
						       G_TYPE_STRING,	/* name */
						       G_TYPE_STRING,	/* author */
						       GDK_TYPE_PIXBUF, /* image */
						       G_TYPE_FILE,	/* image file */
						       G_TYPE_INT,	/* episode count */
						       G_TYPE_POINTER,	/* RBPodcastChannel */
						       G_TYPE_ULONG);	/* date */
	gtk_tree_view_set_model (GTK_TREE_VIEW (dialog->priv->feed_view), GTK_TREE_MODEL (dialog->priv->feed_model));

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_cell_renderer_get_padding (renderer, &xpad, &ypad);
	gtk_cell_renderer_set_fixed_size (renderer, PODCAST_IMAGE_SIZE + (xpad * 2), PODCAST_IMAGE_SIZE + (ypad * 2));

	column = gtk_tree_view_column_new_with_attributes (_("Title"), renderer, "pixbuf", FEED_COLUMN_IMAGE, NULL);
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_attributes (column, renderer, "text", FEED_COLUMN_TITLE, NULL);

	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (dialog->priv->feed_view), column);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Author"), renderer, "text", FEED_COLUMN_AUTHOR, NULL);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (dialog->priv->feed_view), column);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Episodes"), renderer, NULL);
	gtk_tree_view_column_set_cell_data_func (column, renderer, episode_count_column_cell_data_func, NULL, NULL);
	episode_strings[0] = "0000";
	episode_strings[1] = _("Episodes");
	episode_strings[2] = NULL;
	rb_set_tree_view_column_fixed_width (dialog->priv->feed_view, column, renderer, episode_strings, 6);
	gtk_tree_view_append_column (GTK_TREE_VIEW (dialog->priv->feed_view), column);

	overlay = GTK_WIDGET (gtk_builder_get_object (builder, "overlay"));
	gtk_widget_add_events (overlay, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
	dialog->priv->feed_status = nautilus_floating_bar_new (NULL, NULL, FALSE);
	gtk_widget_set_no_show_all (dialog->priv->feed_status, TRUE);
	gtk_widget_set_halign (dialog->priv->feed_status, GTK_ALIGN_END);
	gtk_widget_set_valign (dialog->priv->feed_status, GTK_ALIGN_END);
	gtk_overlay_add_overlay (GTK_OVERLAY (overlay), dialog->priv->feed_status);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "podcast-add-dialog"));
	gtk_box_pack_start (GTK_BOX (dialog), widget, TRUE, TRUE, 0);

	/* set up episode view */
	g_object_get (dialog->priv->shell, "shell-player", &shell_player, NULL);
	episodes = rb_entry_view_new (dialog->priv->db, G_OBJECT (shell_player), TRUE, FALSE);
	g_object_unref (shell_player);

	g_signal_connect (episodes, "entry-activated", G_CALLBACK (episode_entry_activated_cb), dialog);

	/* date column */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new();

	gtk_tree_view_column_pack_start (column, renderer, TRUE);

	gtk_tree_view_column_set_clickable (column, TRUE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
	{
		const char *sample_strings[3];
		sample_strings[0] = _("Date");
		sample_strings[1] = rb_entry_view_get_time_date_column_sample ();
		sample_strings[2] = NULL;
		rb_entry_view_set_fixed_column_width (episodes, column, renderer, sample_strings);
	}

	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 (GtkTreeCellDataFunc) podcast_post_date_cell_data_func,
						 dialog, NULL);

	rb_entry_view_append_column_custom (episodes, column,
					    _("Date"), "Date",
					    (GCompareDataFunc) podcast_post_date_sort_func,
					    0, NULL);
	rb_entry_view_append_column (episodes, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_append_column (episodes, RB_ENTRY_VIEW_COL_DURATION, TRUE);
	rb_entry_view_set_sorting_order (RB_ENTRY_VIEW (episodes), "Date", GTK_SORT_DESCENDING);
	g_signal_connect (episodes,
			  "notify::sort-order",
			  G_CALLBACK (episodes_sort_changed_cb),
			  dialog);

	query = rhythmdb_query_parse (dialog->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      RHYTHMDB_ENTRY_TYPE_PODCAST_SEARCH,
				      RHYTHMDB_QUERY_END);
	query_model = rhythmdb_query_model_new_empty (dialog->priv->db);
	rb_entry_view_set_model (episodes, query_model);

	rhythmdb_do_full_query_async_parsed (dialog->priv->db, RHYTHMDB_QUERY_RESULTS (query_model), query);
	rhythmdb_query_free (query);

	g_object_unref (query_model);

	paned = GTK_WIDGET (gtk_builder_get_object (builder, "paned"));
	g_signal_connect (paned, "size-allocate", G_CALLBACK (paned_size_allocate_cb), dialog);
	gtk_paned_pack2 (GTK_PANED (paned),
			 GTK_WIDGET (episodes),
			 TRUE,
			 FALSE);

	gtk_widget_show_all (GTK_WIDGET (dialog));
	g_object_unref (builder);
}

static void
impl_dispose (GObject *object)
{
	RBPodcastAddDialog *dialog = RB_PODCAST_ADD_DIALOG (object);

	g_clear_object (&dialog->priv->podcast_mgr);
	g_clear_object (&dialog->priv->db);

	G_OBJECT_CLASS (rb_podcast_add_dialog_parent_class)->dispose (object);
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBPodcastAddDialog *dialog = RB_PODCAST_ADD_DIALOG (object);

	switch (prop_id) {
	case PROP_PODCAST_MANAGER:
		dialog->priv->podcast_mgr = g_value_dup_object (value);
		break;
	case PROP_SHELL:
		dialog->priv->shell = g_value_dup_object (value);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBPodcastAddDialog *dialog = RB_PODCAST_ADD_DIALOG (object);

	switch (prop_id) {
	case PROP_PODCAST_MANAGER:
		g_value_set_object (value, dialog->priv->podcast_mgr);
		break;
	case PROP_SHELL:
		g_value_set_object (value, dialog->priv->shell);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static void
rb_podcast_add_dialog_init (RBPodcastAddDialog *dialog)
{
	dialog->priv = G_TYPE_INSTANCE_GET_PRIVATE (dialog,
						    RB_TYPE_PODCAST_ADD_DIALOG,
						    RBPodcastAddDialogPrivate);
}

static void
rb_podcast_add_dialog_class_init (RBPodcastAddDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = impl_constructed;
	object_class->dispose = impl_dispose;
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	klass->close = impl_close;

	g_object_class_install_property (object_class,
					 PROP_PODCAST_MANAGER,
					 g_param_spec_object ("podcast-manager",
							      "podcast-manager",
							      "RBPodcastManager instance",
							      RB_TYPE_PODCAST_MANAGER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_SHELL,
					 g_param_spec_object ("shell",
							      "shell",
							      "RBShell instance",
							      RB_TYPE_SHELL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	signals[CLOSE] = g_signal_new ("close",
				       RB_TYPE_PODCAST_ADD_DIALOG,
				       G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
				       G_STRUCT_OFFSET (RBPodcastAddDialogClass, close),
				       NULL, NULL,
				       NULL,
				       G_TYPE_NONE,
				       0);
	signals[CLOSED] = g_signal_new ("closed",
					RB_TYPE_PODCAST_ADD_DIALOG,
					G_SIGNAL_RUN_LAST,
					G_STRUCT_OFFSET (RBPodcastAddDialogClass, closed),
					NULL, NULL,
					NULL,
					G_TYPE_NONE,
					0);

	g_type_class_add_private (object_class, sizeof (RBPodcastAddDialogPrivate));

	gtk_binding_entry_add_signal (gtk_binding_set_by_class (klass),
				      GDK_KEY_Escape,
				      0,
				      "close",
				      0);
}

void
rb_podcast_add_dialog_reset (RBPodcastAddDialog *dialog, const char *text, gboolean load)
{
	dialog->priv->reset_count++;
	remove_all_feeds (dialog);
	rhythmdb_entry_delete_by_type (dialog->priv->db, RHYTHMDB_ENTRY_TYPE_PODCAST_SEARCH);
	rhythmdb_commit (dialog->priv->db);

	gtk_widget_hide (dialog->priv->info_bar);
	rb_search_entry_set_text (dialog->priv->search_entry, text);

	if (load) {
		search_cb (dialog->priv->search_entry, text, dialog);
	} else {
		rb_search_entry_grab_focus (dialog->priv->search_entry);
	}
}

GtkWidget *
rb_podcast_add_dialog_new (RBShell *shell, RBPodcastManager *podcast_mgr)
{
	return GTK_WIDGET (g_object_new (RB_TYPE_PODCAST_ADD_DIALOG,
					 "shell", shell,
					 "podcast-manager", podcast_mgr,
					 "orientation", GTK_ORIENTATION_VERTICAL,
					 NULL));
}

