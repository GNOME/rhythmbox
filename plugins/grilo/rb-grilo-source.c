/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2011 Jonathan Matthew
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

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <grilo.h>

#include "rhythmdb.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-grilo-source.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-gst-media-types.h"
#include "rb-search-entry.h"

/* number of items to check before giving up on finding any
 * of a particular type
 */
#define CONTAINER_GIVE_UP_POINT		100

/* maximum number of tracks to fetch before stopping and
 * requiring the user to ask for more.
 */
#define CONTAINER_MAX_TRACKS		1000

/* number of items to fetch at once */
#define CONTAINER_FETCH_SIZE		50

enum {
	CONTAINER_UNKNOWN_MEDIA = 0,
	CONTAINER_MARKER,
	CONTAINER_NO_MEDIA,
	CONTAINER_HAS_MEDIA
};

enum
{
	PROP_0,
	PROP_GRILO_SOURCE
};

static void rb_grilo_source_dispose (GObject *object);
static void rb_grilo_source_finalize (GObject *object);
static void rb_grilo_source_constructed (GObject *object);
static void impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void start_media_browse (RBGriloSource *source, GrlSupportedOps op_type, GrlMedia *container, GtkTreeIter *container_iter, guint limit);

static void browser_selection_changed_cb (GtkTreeSelection *selection, RBGriloSource *source);
static void browser_row_expanded_cb (GtkTreeView *tree_view, GtkTreeIter *iter, GtkTreePath *path, RBGriloSource *source);
static void scroll_adjust_changed_cb (GtkAdjustment *adjustment, RBGriloSource *source);
static void scroll_adjust_value_changed_cb (GtkAdjustment *adjustment, RBGriloSource *source);
static gboolean maybe_expand_container (RBGriloSource *source);
static void fetch_more_cb (GtkInfoBar *bar, gint response, RBGriloSource *source);
static void search_cb (RBSearchEntry *search, const char *text, RBGriloSource *source);
static void notify_sort_order_cb (GObject *object, GParamSpec *pspec, RBGriloSource *source);

static void impl_delete_thyself (RBDisplayPage *page);
static void impl_selected (RBDisplayPage *page);
static void impl_deselected (RBDisplayPage *page);

static RBEntryView *impl_get_entry_view (RBSource *source);

struct _RBGriloSourcePrivate
{
	GrlSource *grilo_source;
	GList *grilo_keys;

	RhythmDBEntryType *entry_type;

	/* some widgets and things */
	GtkWidget *paned;
	RhythmDBQueryModel *query_model;
	RBEntryView *entry_view;
	GtkTreeStore *browser_model;
	GtkWidget *browser_view;
	gboolean done_initial_browse;
	GtkWidget *info_bar;
	GtkWidget *info_bar_label;
	RBSearchEntry *search_entry;

	/* current browsing operation (should allow multiple concurrent ops?) */
	guint browse_op;
	GrlMedia *browse_container;
	GtkTreeIter browse_container_iter;
	guint browse_position;
	gboolean browse_got_results;
	gboolean browse_got_media;
	guint maybe_expand_idle;

	/* current media browse operation */
	GrlSupportedOps media_browse_op_type;
	guint media_browse_op;
	char *search_text;
	GrlMedia *media_browse_container;
	GtkTreeIter media_browse_container_iter;
	guint media_browse_position;
	gboolean media_browse_got_results;
	gboolean media_browse_got_containers;
	guint media_browse_limit;

	RhythmDB *db;
};

G_DEFINE_DYNAMIC_TYPE (RBGriloSource, rb_grilo_source, RB_TYPE_SOURCE)

/* entry type */

G_DEFINE_DYNAMIC_TYPE (RBGriloEntryType, rb_grilo_entry_type, RHYTHMDB_TYPE_ENTRY_TYPE);

static void
rb_grilo_entry_type_destroy_entry (RhythmDBEntryType *etype, RhythmDBEntry *entry)
{
	RBGriloEntryData *data;

	data = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RBGriloEntryData);
	g_object_unref (data->grilo_data);
	if (data->grilo_container != NULL)
		g_object_unref (data->grilo_container);
}

static void
rb_grilo_entry_type_class_init (RBGriloEntryTypeClass *klass)
{
	RhythmDBEntryTypeClass *etype_class = RHYTHMDB_ENTRY_TYPE_CLASS (klass);
	etype_class->can_sync_metadata = (RhythmDBEntryTypeBooleanFunc) rb_true_function;
	etype_class->sync_metadata = (RhythmDBEntryTypeSyncFunc) rb_null_function;
	etype_class->destroy_entry = rb_grilo_entry_type_destroy_entry;
}

static void
rb_grilo_entry_type_class_finalize (RBGriloEntryTypeClass *klass)
{
}

static void
rb_grilo_entry_type_init (RBGriloEntryType *etype)
{
}

static void
rb_grilo_source_class_init (RBGriloSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->constructed = rb_grilo_source_constructed;
	object_class->dispose = rb_grilo_source_dispose;
	object_class->finalize = rb_grilo_source_finalize;
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	page_class->delete_thyself = impl_delete_thyself;
	page_class->selected = impl_selected;
	page_class->deselected = impl_deselected;

	source_class->get_entry_view = impl_get_entry_view;

	g_object_class_install_property (object_class,
					 PROP_GRILO_SOURCE,
					 g_param_spec_object ("grilo-source",
							      "grilo source",
							      "grilo source object",
							      GRL_TYPE_SOURCE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBGriloSourcePrivate));
}

static void
rb_grilo_source_class_finalize (RBGriloSourceClass *klass)
{
}

static void
rb_grilo_source_init (RBGriloSource *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, RB_TYPE_GRILO_SOURCE, RBGriloSourcePrivate);
}

static void
rb_grilo_source_finalize (GObject *object)
{
	RBGriloSource *source = RB_GRILO_SOURCE (object);

	g_free (source->priv->search_text);

	g_list_free (source->priv->grilo_keys);

	G_OBJECT_CLASS (rb_grilo_source_parent_class)->finalize (object);
}

static void
rb_grilo_source_dispose (GObject *object)
{
	RBGriloSource *source = RB_GRILO_SOURCE (object);

	if (source->priv->browse_op != 0) {
		grl_operation_cancel (source->priv->browse_op);
		source->priv->browse_op = 0;
	}

	if (source->priv->media_browse_op != 0) {
		grl_operation_cancel (source->priv->media_browse_op);
		source->priv->media_browse_op = 0;
	}

	if (source->priv->query_model != NULL) {
		g_object_unref (source->priv->query_model);
		source->priv->query_model = NULL;
	}

	if (source->priv->entry_type != NULL) {
		g_object_unref (source->priv->entry_type);
		source->priv->entry_type = NULL;
	}

	if (source->priv->maybe_expand_idle != 0) {
		g_source_remove (source->priv->maybe_expand_idle);
		source->priv->maybe_expand_idle = 0;
	}

	G_OBJECT_CLASS (rb_grilo_source_parent_class)->dispose (object);
}

static void
rb_grilo_source_constructed (GObject *object)
{
	RBGriloSource *source;
	RBShell *shell;
	RBShellPlayer *shell_player;
	const GList *source_keys;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	GtkTreeSelection *selection;
	GtkWidget *scrolled;
	GtkWidget *browserbox;
	GtkWidget *vbox;
	GtkWidget *mainbox;
	GtkAdjustment *adjustment;
	gboolean want_quality_column = FALSE;

	RB_CHAIN_GOBJECT_METHOD (rb_grilo_source_parent_class, constructed, object);
	source = RB_GRILO_SOURCE (object);

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell,
		      "db", &source->priv->db,
		      "shell-player", &shell_player,
		      NULL);
	g_object_unref (shell);

	g_object_get (source, "entry-type", &source->priv->entry_type, NULL);

	source->priv->entry_view = rb_entry_view_new (source->priv->db, G_OBJECT (shell_player), TRUE, FALSE);
	g_object_unref (shell_player);
	g_signal_connect (source->priv->entry_view,
			  "notify::sort-order",
			  G_CALLBACK (notify_sort_order_cb),
			  source);

	source_keys = grl_source_supported_keys (source->priv->grilo_source);

	if (g_list_find ((GList *)source_keys, GUINT_TO_POINTER(GRL_METADATA_KEY_TRACK_NUMBER))) {
		rb_entry_view_append_column (source->priv->entry_view, RB_ENTRY_VIEW_COL_TRACK_NUMBER, FALSE);
		source->priv->grilo_keys = g_list_prepend (source->priv->grilo_keys,
							   GUINT_TO_POINTER(GRL_METADATA_KEY_TRACK_NUMBER));
	}
	if (g_list_find ((GList *)source_keys, GUINT_TO_POINTER(GRL_METADATA_KEY_ALBUM_DISC_NUMBER))) {
		source->priv->grilo_keys = g_list_prepend (source->priv->grilo_keys,
							   GUINT_TO_POINTER(GRL_METADATA_KEY_ALBUM_DISC_NUMBER));
	}

	if (g_list_find ((GList *)source_keys, GUINT_TO_POINTER(GRL_METADATA_KEY_TITLE))) {
		rb_entry_view_append_column (source->priv->entry_view, RB_ENTRY_VIEW_COL_TITLE, TRUE);
		source->priv->grilo_keys = g_list_prepend (source->priv->grilo_keys,
							   GUINT_TO_POINTER(GRL_METADATA_KEY_TITLE));
	}

	if (g_list_find ((GList *)source_keys, GUINT_TO_POINTER(GRL_METADATA_KEY_GENRE))) {
		rb_entry_view_append_column (source->priv->entry_view, RB_ENTRY_VIEW_COL_GENRE, FALSE);
		source->priv->grilo_keys = g_list_prepend (source->priv->grilo_keys,
							   GUINT_TO_POINTER(GRL_METADATA_KEY_GENRE));
	}
	if (g_list_find ((GList *)source_keys, GUINT_TO_POINTER(GRL_METADATA_KEY_ARTIST))) {
		rb_entry_view_append_column (source->priv->entry_view, RB_ENTRY_VIEW_COL_ARTIST, FALSE);
		source->priv->grilo_keys = g_list_prepend (source->priv->grilo_keys,
							   GUINT_TO_POINTER(GRL_METADATA_KEY_ARTIST));
	}
	if (g_list_find ((GList *)source_keys, GUINT_TO_POINTER(GRL_METADATA_KEY_ALBUM))) {
		rb_entry_view_append_column (source->priv->entry_view, RB_ENTRY_VIEW_COL_ALBUM, FALSE);
		source->priv->grilo_keys = g_list_prepend (source->priv->grilo_keys,
							   GUINT_TO_POINTER(GRL_METADATA_KEY_ALBUM));
	}
	/*
	if (g_list_find ((GList *)source_keys, GUINT_TO_POINTER(GRL_METADATA_KEY_DATE))) {
		rb_entry_view_append_column (source->priv->entry_view, RB_ENTRY_VIEW_COL_YEAR, FALSE);
		source->priv->grilo_keys = g_list_prepend (source->priv->grilo_keys,
							   GUINT_TO_POINTER(GRL_METADATA_KEY_DATE));
	}
	*/
	if (g_list_find ((GList *)source_keys, GUINT_TO_POINTER(GRL_METADATA_KEY_DURATION))) {
		rb_entry_view_append_column (source->priv->entry_view, RB_ENTRY_VIEW_COL_DURATION, FALSE);
		source->priv->grilo_keys = g_list_prepend (source->priv->grilo_keys,
							   GUINT_TO_POINTER(GRL_METADATA_KEY_DURATION));
	}
	if (g_list_find ((GList *)source_keys, GUINT_TO_POINTER(GRL_METADATA_KEY_BITRATE))) {
		want_quality_column = TRUE;
		source->priv->grilo_keys = g_list_prepend (source->priv->grilo_keys,
							   GUINT_TO_POINTER(GRL_METADATA_KEY_BITRATE));
	}
	if (g_list_find ((GList *)source_keys, GUINT_TO_POINTER(GRL_METADATA_KEY_MIME))) {
		want_quality_column = TRUE;
		source->priv->grilo_keys = g_list_prepend (source->priv->grilo_keys,
							   GUINT_TO_POINTER(GRL_METADATA_KEY_MIME));
	}
	if (want_quality_column) {
		rb_entry_view_append_column (source->priv->entry_view, RB_ENTRY_VIEW_COL_QUALITY, FALSE);
	}

	source->priv->grilo_keys = g_list_prepend (source->priv->grilo_keys, GUINT_TO_POINTER(GRL_METADATA_KEY_CHILDCOUNT));
	source->priv->grilo_keys = g_list_prepend (source->priv->grilo_keys, GUINT_TO_POINTER(GRL_METADATA_KEY_URL));
	source->priv->grilo_keys = g_list_prepend (source->priv->grilo_keys, GUINT_TO_POINTER(GRL_METADATA_KEY_THUMBNAIL));

	/* probably add an image column too? */
	source->priv->browser_model = gtk_tree_store_new (4, GRL_TYPE_MEDIA, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
	source->priv->browser_view = gtk_tree_view_new ();
	gtk_tree_view_set_model (GTK_TREE_VIEW (source->priv->browser_view), GTK_TREE_MODEL (source->priv->browser_model));

	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_set_title (column, _("Browse"));
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "text", 1);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);

	gtk_tree_view_append_column (GTK_TREE_VIEW (source->priv->browser_view), column);
	gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (source->priv->browser_view), TRUE);
	gtk_tree_view_set_expander_column (GTK_TREE_VIEW (source->priv->browser_view), column);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (source->priv->browser_view), TRUE);
	gtk_tree_view_set_fixed_height_mode (GTK_TREE_VIEW (source->priv->browser_view), TRUE);

	g_signal_connect (source->priv->browser_view, "row-expanded", G_CALLBACK (browser_row_expanded_cb), source);
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (source->priv->browser_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);	/* should be multiple eventually */
	g_signal_connect (selection, "changed", G_CALLBACK (browser_selection_changed_cb), source);

	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled), GTK_SHADOW_NONE);
	adjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scrolled));
	g_signal_connect (adjustment, "changed", G_CALLBACK (scroll_adjust_changed_cb), source);
	g_signal_connect (adjustment, "value-changed", G_CALLBACK (scroll_adjust_value_changed_cb), source);

	browserbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

	/* search bar (if the source supports searching) */
	if (grl_source_supported_operations (source->priv->grilo_source) & GRL_OP_SEARCH) {
		source->priv->search_entry = rb_search_entry_new (FALSE);
		g_object_set (source->priv->search_entry, "explicit-mode", TRUE, NULL);
		g_signal_connect (source->priv->search_entry, "search", G_CALLBACK (search_cb), source);
		g_signal_connect (source->priv->search_entry, "activate", G_CALLBACK (search_cb), source);
		gtk_box_pack_start (GTK_BOX (browserbox), GTK_WIDGET (source->priv->search_entry), FALSE, FALSE, 6);
	}
	gtk_container_add (GTK_CONTAINER (scrolled), source->priv->browser_view);
	gtk_box_pack_start (GTK_BOX (browserbox), scrolled, TRUE, TRUE, 0);

	mainbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_box_pack_start (GTK_BOX (source), mainbox, TRUE, TRUE, 0);

	/* info bar */
	source->priv->info_bar_label = gtk_label_new ("");
	source->priv->info_bar = gtk_info_bar_new ();
	gtk_info_bar_set_message_type (GTK_INFO_BAR (source->priv->info_bar), GTK_MESSAGE_INFO);
	gtk_info_bar_add_button (GTK_INFO_BAR (source->priv->info_bar), _("Fetch more tracks"), GTK_RESPONSE_OK);
	gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (source->priv->info_bar))),
			   source->priv->info_bar_label);
	gtk_widget_show (GTK_WIDGET (source->priv->info_bar_label));
	gtk_widget_set_no_show_all (GTK_WIDGET (source->priv->info_bar), TRUE);
	g_signal_connect (source->priv->info_bar, "response", G_CALLBACK (fetch_more_cb), source);

	/* don't allow the browser to be hidden? */
	source->priv->paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	rb_source_bind_settings (RB_SOURCE (source), GTK_WIDGET (source->priv->entry_view), source->priv->paned, NULL, TRUE);
	gtk_paned_pack1 (GTK_PANED (source->priv->paned), browserbox, FALSE, FALSE);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (source->priv->entry_view), TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), source->priv->info_bar, FALSE, FALSE, 0);
	gtk_paned_pack2 (GTK_PANED (source->priv->paned), vbox, TRUE, FALSE);

	gtk_box_pack_start (GTK_BOX (mainbox), source->priv->paned, TRUE, TRUE, 0);

	gtk_widget_show_all (GTK_WIDGET (source));
}

RBSource *
rb_grilo_source_new (GObject *plugin, GrlSource *grilo_source)
{
	GObject *source;
	RBShell *shell;
	GSettings *settings;
	RhythmDBEntryType *entry_type;
	RhythmDB *db;
	char *name;

	name = g_strdup_printf ("grilo:%s", grl_source_get_id (grilo_source));

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	entry_type = g_object_new (rb_grilo_entry_type_get_type (),
				   "db", db,
				   "name", name,
				   "save-to-disk", FALSE,
				   "category", RHYTHMDB_ENTRY_NORMAL,
				   "type-data-size", sizeof(RBGriloEntryData),
				   NULL);
	rhythmdb_register_entry_type (db, entry_type);
	g_object_unref (db);
	g_free (name);

	settings = g_settings_new ("org.gnome.rhythmbox.plugins.grilo");
	source = g_object_new (RB_TYPE_GRILO_SOURCE,
			       "name", grl_source_get_name (grilo_source),
			       "entry-type", entry_type,
			       "shell", shell,
			       "plugin", plugin,
			       "show-browser", FALSE,
			       "settings", g_settings_get_child (settings, "source"),
			       "grilo-source", grilo_source,
			       NULL);
	g_object_unref (settings);
	rb_display_page_set_icon_name (RB_DISPLAY_PAGE (source), "network-server-symbolic");

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	g_object_unref (shell);
	return RB_SOURCE (source);
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBGriloSource *source = RB_GRILO_SOURCE (object);
	switch (prop_id) {
	case PROP_GRILO_SOURCE:
		source->priv->grilo_source = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBGriloSource *source = RB_GRILO_SOURCE (object);

	switch (prop_id) {
	case PROP_GRILO_SOURCE:
		g_value_set_object (value, source->priv->grilo_source);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_delete_thyself (RBDisplayPage *page)
{
	RBGriloSource *source = RB_GRILO_SOURCE (page);
	RhythmDBEntryType *entry_type;

	if (source->priv->browse_op != 0) {
		grl_operation_cancel (source->priv->browse_op);
		source->priv->browse_op = 0;
	}

	if (source->priv->media_browse_op != 0) {
		grl_operation_cancel (source->priv->media_browse_op);
		source->priv->media_browse_op = 0;
	}

	g_object_get (source, "entry-type", &entry_type, NULL);
	rhythmdb_entry_delete_by_type (source->priv->db, entry_type);
	g_object_unref (entry_type);

	rhythmdb_commit (source->priv->db);

	RB_DISPLAY_PAGE_CLASS (rb_grilo_source_parent_class)->delete_thyself (page);
}

void
_rb_grilo_source_register_type (GTypeModule *module)
{
	rb_grilo_source_register_type (module);
	rb_grilo_entry_type_register_type (module);
}

static GrlOperationOptions *
make_operation_options (RBGriloSource *source, GrlSupportedOps op, int position)
{
	GrlOperationOptions *options;
	GrlCaps *caps;

	caps = grl_source_get_caps (source->priv->grilo_source, op);

	options = grl_operation_options_new (caps);
	grl_operation_options_set_skip (options, position);
	grl_operation_options_set_count (options,
					 CONTAINER_FETCH_SIZE);
	grl_operation_options_set_type_filter (options, GRL_TYPE_FILTER_AUDIO);
	grl_operation_options_set_resolution_flags (options, GRL_RESOLVE_NORMAL);

	return options;
}

/* grilo media -> rhythmdb entry */

static void
set_string_prop_from_key (RhythmDB *db, RhythmDBEntry *entry, RhythmDBPropType prop, GrlData *data, GrlKeyID key)
{
	GValue v = {0,};
	if (grl_data_has_key (data, key) == FALSE)
		return;

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, grl_data_get_string (data, key));
	rhythmdb_entry_set (db, entry, prop, &v);
	g_value_unset (&v);
}

static RhythmDBEntry *
create_entry_for_media (RhythmDB *db, RhythmDBEntryType *entry_type, GrlData *data, GrlData *container)
{
	RhythmDBEntry *entry;
	RBGriloEntryData *entry_data;
	gulong bitrate = 0;
	const gchar *url;

	url = grl_media_get_url (GRL_MEDIA (data));
	if (url == NULL) {
		return NULL;
	}

	entry = rhythmdb_entry_lookup_by_location (db, url);
	if (entry != NULL) {
		return entry;
	}

	rb_debug ("creating entry for %s / %s", grl_media_get_url (GRL_MEDIA (data)), grl_media_get_id (GRL_MEDIA (data)));

	entry = rhythmdb_entry_new (db, entry_type, grl_media_get_url (GRL_MEDIA (data)));	/* just use the url? */
	if (entry == NULL) {
		/* crap. */
		return NULL;
	}

	set_string_prop_from_key (db, entry, RHYTHMDB_PROP_TITLE, data, GRL_METADATA_KEY_TITLE);
	set_string_prop_from_key (db, entry, RHYTHMDB_PROP_ALBUM, data, GRL_METADATA_KEY_ALBUM);
	set_string_prop_from_key (db, entry, RHYTHMDB_PROP_ARTIST, data, GRL_METADATA_KEY_ARTIST);
	set_string_prop_from_key (db, entry, RHYTHMDB_PROP_GENRE, data, GRL_METADATA_KEY_GENRE);
	set_string_prop_from_key (db, entry, RHYTHMDB_PROP_TITLE, data, GRL_METADATA_KEY_TITLE);

	if (grl_data_has_key (data, GRL_METADATA_KEY_PUBLICATION_DATE)) {
		/* something - grilo has this as a string? */
	}

	if (grl_data_has_key (data, GRL_METADATA_KEY_BITRATE)) {
		bitrate = grl_data_get_int (data, GRL_METADATA_KEY_BITRATE);
	}

	if (grl_data_has_key (data, GRL_METADATA_KEY_DURATION)) {
		/* this is probably in seconds */
		GValue v = {0,};
		g_value_init (&v, G_TYPE_ULONG);
		g_value_set_ulong (&v, grl_data_get_int (data, GRL_METADATA_KEY_DURATION));
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DURATION, &v);
		g_value_unset (&v);
	}

	if (grl_data_has_key (data, GRL_METADATA_KEY_MIME)) {
		const char *media_type;
		media_type = rb_gst_mime_type_to_media_type (grl_data_get_string (data, GRL_METADATA_KEY_MIME));
		if (media_type) {
			if (rb_gst_media_type_is_lossless (media_type)) {
				/* For lossless, rhythmdb_entry_is_lossless expects
				 * bitrate == 0/unset, but DLNA server may report
				 * actual bitrate. Unset bitrate to keep
				 * rhythmdb_entry_is_lossless happy. */
				bitrate = 0;
			}
			GValue v = {0,};
			g_value_init (&v, G_TYPE_STRING);
			g_value_set_string (&v, media_type);
			rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_MEDIA_TYPE, &v);
			g_value_unset (&v);
		}
	}

	if (bitrate != 0) {
		GValue v = {0,};
		g_value_init (&v, G_TYPE_ULONG);
		/* Grilo uses kbps and so do we. */
		g_value_set_ulong (&v, bitrate);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_BITRATE, &v);
		g_value_unset (&v);
	}

	if (grl_data_has_key (data, GRL_METADATA_KEY_TRACK_NUMBER)) {
		GValue v = {0,};
		g_value_init (&v, G_TYPE_ULONG);
		g_value_set_ulong (&v, grl_data_get_int (data, GRL_METADATA_KEY_TRACK_NUMBER));
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TRACK_NUMBER, &v);
		g_value_unset (&v);
	}

	if (grl_data_has_key (data, GRL_METADATA_KEY_ALBUM_DISC_NUMBER)) {
		GValue v = {0,};
		g_value_init (&v, G_TYPE_ULONG);
		g_value_set_ulong (&v, grl_data_get_int (data, GRL_METADATA_KEY_ALBUM_DISC_NUMBER));
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DISC_NUMBER, &v);
		g_value_unset (&v);
	}

	/* rating and play count? */

	entry_data = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RBGriloEntryData);
	entry_data->grilo_data = g_object_ref (data);
	if (container != NULL) {
		entry_data->grilo_container = g_object_ref (container);
	}

	/* might want to consider batching this */
	rhythmdb_commit (db);

	return entry;
}

/* container browsing */

static void browse_next (RBGriloSource *source);

static void
delete_marker_row (RBGriloSource *source, GtkTreeIter *iter)
{
	GtkTreeIter marker_iter;
	if (gtk_tree_model_iter_children (GTK_TREE_MODEL (source->priv->browser_model), &marker_iter, iter)) {
		do {
			GrlMedia *container;
			gtk_tree_model_get (GTK_TREE_MODEL (source->priv->browser_model), &marker_iter,
					    0, &container,
					    -1);
			if (container == NULL) {
				gtk_tree_store_remove (GTK_TREE_STORE (source->priv->browser_model), &marker_iter);
				break;
			}
		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (source->priv->browser_model), &marker_iter));
	}
}

static void
set_container_type (RBGriloSource *source, GtkTreeIter *iter, gboolean has_media)
{
	int container_type;

	gtk_tree_model_get (GTK_TREE_MODEL (source->priv->browser_model),
			    iter,
			    2, &container_type,
			    -1);
	if (container_type != CONTAINER_HAS_MEDIA) {
		container_type = has_media ? CONTAINER_HAS_MEDIA : CONTAINER_NO_MEDIA;
	}

	gtk_tree_store_set (source->priv->browser_model,
			    iter,
			    2, container_type,
			    -1);
}

static void
grilo_browse_cb (GrlSource *grilo_source, guint operation_id, GrlMedia *media, guint remaining, RBGriloSource *source, const GError *error)
{
	if (operation_id != source->priv->browse_op) {
		return;
	}

	if (error != NULL) {
		/* do something? */
		rb_debug ("got error for %s: %s", grl_source_get_name (grilo_source), error->message);
		source->priv->browse_op = 0;
		return;
	}

	if (media != NULL) {
		source->priv->browse_got_results = TRUE;
		source->priv->browse_position++;
	}

	if (media && grl_media_is_container (media)) {

		GtkTreeIter new_row;
		if (source->priv->browse_container == NULL) {
			/* insert at the end */
			gtk_tree_store_insert_with_values (source->priv->browser_model,
							   &new_row,
							   NULL,
							   -1,
							   0, g_object_ref (media),
							   1, grl_media_get_title (media),
							   2, CONTAINER_UNKNOWN_MEDIA,
							   3, 0,
							   -1);
		} else {
			int n;
			/* insert before the expand marker row */
			n = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (source->priv->browser_model),
							    &source->priv->browse_container_iter);
			gtk_tree_store_insert_with_values (source->priv->browser_model,
							   &new_row,
							   &source->priv->browse_container_iter,
							   n - 1,
							   0, g_object_ref (media),
							   1, grl_media_get_title (media),
							   2, CONTAINER_UNKNOWN_MEDIA,
							   3, 0,
							   -1);
		}

		/* and insert an expand marker below it too */
		gtk_tree_store_insert_with_values (source->priv->browser_model,
						   NULL,
						   &new_row,
						   -1,
						   0, NULL,
						   1, "...",	/* needs to be translatable? */
						   2, CONTAINER_MARKER,
						   3, 0,
						   -1);
	} else if (media && grl_media_is_audio (media)) {
		source->priv->browse_got_media = TRUE;
	}

	if (remaining == 0) {
		source->priv->browse_op = 0;
		if (source->priv->browse_got_results == FALSE &&
		    source->priv->browse_container != NULL) {
			/* no more results for this container, so delete the marker row */
			delete_marker_row (source, &source->priv->browse_container_iter);

			set_container_type (source,
					    &source->priv->browse_container_iter,
					    FALSE);
			gtk_tree_store_set (source->priv->browser_model,
					    &source->priv->browse_container_iter,
					    3, -1,
					    -1);
		} else if (source->priv->browse_container != NULL) {
			if (source->priv->browse_got_media) {
				set_container_type (source,
						    &source->priv->browse_container_iter,
						    TRUE);
			}

			if (source->priv->browse_position >= CONTAINER_GIVE_UP_POINT &&
			    gtk_tree_model_iter_n_children (GTK_TREE_MODEL (source->priv->browser_model),
							    &source->priv->browse_container_iter) == 1) {
				/* no containers yet, so remove the marker row */
				delete_marker_row (source, &source->priv->browse_container_iter);
			} else {
				/* store browse position for next time we want more */
				gtk_tree_store_set (source->priv->browser_model,
						    &source->priv->browse_container_iter,
						    3, source->priv->browse_position,
						    -1);
				maybe_expand_container (source);
			}

		} else if (source->priv->browse_container == NULL) {
		       	if (source->priv->browse_got_results) {
				/* get all top-level containers */
				browse_next (source);
			} else if (source->priv->browse_got_media) {
				GtkTreeIter root_iter;
				GtkTreeSelection *selection;
				/* root container has media, so show it in the browser */
				gtk_tree_store_insert_with_values (source->priv->browser_model,
								   &root_iter,
								   NULL,
								   0,
								   0, NULL,
								   1, grl_source_get_name (source->priv->grilo_source),
								   2, CONTAINER_HAS_MEDIA,
								   3, 0,
								   -1);

				/* select it, so we start fetching the media */
				selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (source->priv->browser_view));
				gtk_tree_selection_select_iter (selection, &root_iter);

			}
		}
	}
}

static void
browse_next (RBGriloSource *source)
{
	GrlOperationOptions *options;
	rb_debug ("next browse op for %s (%d)",
		  grl_source_get_name (source->priv->grilo_source),
		  source->priv->browse_position);
	source->priv->browse_got_results = FALSE;
	options = make_operation_options (source, GRL_OP_BROWSE, source->priv->browse_position);
	source->priv->browse_op = grl_source_browse (source->priv->grilo_source,
						     source->priv->browse_container,
						     source->priv->grilo_keys,
						     options,
						     (GrlSourceResultCb) grilo_browse_cb,
						     source);
}

static void
start_browse (RBGriloSource *source, GrlMedia *container, GtkTreeIter *container_iter, int position)
{
	rb_debug ("starting browse op for %s", grl_source_get_name (source->priv->grilo_source));

	/* cancel existing operation? */
	if (source->priv->browse_op != 0) {
		grl_operation_cancel (source->priv->browse_op);
	}

	if (source->priv->browse_container != NULL) {
		g_object_unref (source->priv->browse_container);
	}
	source->priv->browse_container = container;
	if (container_iter != NULL) {
		/* hrm, probably have to use row references here.. */
		source->priv->browse_container_iter = *container_iter;
	}
	source->priv->browse_position = position;
	source->priv->browse_got_media = FALSE;

	browse_next (source);
}

/* media browsing */

static void media_browse_next (RBGriloSource *source);

static void
grilo_media_browse_cb (GrlSource *grilo_source, guint operation_id, GrlMedia *media, guint remaining, RBGriloSource *source, const GError *error)
{
	if (operation_id != source->priv->media_browse_op) {
		return;
	}

	if (error != NULL) {
		/* do something? */
		rb_debug ("got error for %s: %s",
			  grl_source_get_name (grilo_source),
			  error->message);
		return;
	}

	if (media != NULL) {
		source->priv->media_browse_got_results = TRUE;
		source->priv->media_browse_position++;

		if (grl_media_is_audio (media)) {
			RhythmDBEntry *entry;
			entry = create_entry_for_media (source->priv->db,
							source->priv->entry_type,
							GRL_DATA (media),
							GRL_DATA (source->priv->browse_container));
			if (entry != NULL) {
				rhythmdb_query_model_add_entry (source->priv->query_model, entry, -1);
			}
		} else if (grl_media_is_container (media)) {
			source->priv->media_browse_got_containers = TRUE;
		}
	}

	if (remaining == 0) {
		source->priv->media_browse_op = 0;

		if (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (source->priv->query_model), NULL) == 0 &&
		    source->priv->media_browse_position >= CONTAINER_GIVE_UP_POINT) {
			/* if we don't find any media within the first 100 or so results,
			 * assume this container doesn't have any.  otherwise we'd load
			 * the entire thing when it got selected, which would suck.
			 */
			rb_debug ("didn't find any media in %s, giving up", grl_media_get_title (source->priv->media_browse_container));
			gtk_tree_store_set (source->priv->browser_model,
					    &source->priv->media_browse_container_iter,
					    2, CONTAINER_NO_MEDIA,
					    -1);
		} else if (source->priv->media_browse_got_results) {
			if (source->priv->media_browse_position < source->priv->media_browse_limit) {
				media_browse_next (source);
			} else {
				char *text;

				text = g_strdup_printf (ngettext ("Only showing %d result",
								  "Only showing %d results",
								  source->priv->media_browse_position),
							source->priv->media_browse_position);
				gtk_label_set_text (GTK_LABEL (source->priv->info_bar_label), text);
				g_free (text);

				gtk_widget_show (source->priv->info_bar);
			}
		} else if (source->priv->media_browse_got_containers == FALSE &&
			   source->priv->media_browse_container != NULL) {
			/* make sure there's no marker row for this container */
			delete_marker_row (source, &source->priv->media_browse_container_iter);
		}
	}
}

static void
media_browse_next (RBGriloSource *source)
{
	GrlOperationOptions *options;

	rb_debug ("next media_browse op for %s (%d)",
		  grl_source_get_name (source->priv->grilo_source),
		  source->priv->media_browse_position);

	source->priv->media_browse_got_results = FALSE;
	if (source->priv->media_browse_op_type == GRL_OP_BROWSE) {
		options = make_operation_options (source,
						  GRL_OP_BROWSE,
						  source->priv->media_browse_position);
		source->priv->media_browse_op =
			grl_source_browse (source->priv->grilo_source,
					   source->priv->media_browse_container,
					   source->priv->grilo_keys,
					   options,
					   (GrlSourceResultCb) grilo_media_browse_cb,
					   source);
	} else if (source->priv->media_browse_op_type == GRL_OP_SEARCH) {
		options = make_operation_options (source,
						  GRL_OP_SEARCH,
						  source->priv->media_browse_position);
		source->priv->media_browse_op =
			grl_source_search (source->priv->grilo_source,
					   source->priv->search_text,
					   source->priv->grilo_keys,
					   options,
					   (GrlSourceResultCb) grilo_media_browse_cb,
					   source);
	} else {
		g_assert_not_reached ();
	}
}

static void
start_media_browse (RBGriloSource *source, GrlSupportedOps op_type, GrlMedia *container, GtkTreeIter *container_iter, guint limit)
{
	rb_debug ("starting media browse for %s",
		  grl_source_get_name (source->priv->grilo_source));

	/* cancel existing operation? */
	if (source->priv->media_browse_op != 0) {
		grl_operation_cancel (source->priv->media_browse_op);
	}

	if (source->priv->media_browse_container != NULL) {
		g_object_unref (source->priv->media_browse_container);
	}
	source->priv->media_browse_container = container;
	if (container_iter != NULL) {
		/* hrm, probably have to use row references here.. */
		source->priv->media_browse_container_iter = *container_iter;
	}
	source->priv->media_browse_position = 0;
	source->priv->media_browse_limit = limit;
	source->priv->media_browse_got_containers = FALSE;
	source->priv->media_browse_op_type = op_type;

	if (source->priv->query_model != NULL) {
		g_object_unref (source->priv->query_model);
	}
	source->priv->query_model = rhythmdb_query_model_new_empty (source->priv->db);
	rb_entry_view_set_model (RB_ENTRY_VIEW (source->priv->entry_view), source->priv->query_model);
	g_object_set (source, "query-model", source->priv->query_model, NULL);

	media_browse_next (source);
}

static void
fetch_more_cb (GtkInfoBar *bar, gint response, RBGriloSource *source)
{
	if (response != GTK_RESPONSE_OK) {
		return;
	}

	gtk_widget_hide (GTK_WIDGET (bar));
	source->priv->media_browse_limit += CONTAINER_MAX_TRACKS;
	media_browse_next (source);
}

static gboolean
expand_from_marker (RBGriloSource *source, GtkTreeIter *iter)
{
	/* this is a marker row, fetch more containers underneath the parent */
	GrlMedia *container;
	GtkTreeIter browse;
	int position;
	gtk_tree_model_iter_parent (GTK_TREE_MODEL (source->priv->browser_model), &browse, iter);
	gtk_tree_model_get (GTK_TREE_MODEL (source->priv->browser_model),
			    &browse,
			    0, &container,
			    3, &position,
			    -1);
	if (position >= 0) {
		start_browse (source, container, &browse, position);
		return TRUE;
	}

	return FALSE;
}

static void
browser_selection_changed_cb (GtkTreeSelection *selection, RBGriloSource *source)
{
	GtkTreeIter iter;
	GrlMedia *container;
	int container_type;

	gtk_widget_hide (GTK_WIDGET (source->priv->info_bar));
	if (gtk_tree_selection_get_selected (selection, NULL, &iter) == FALSE) {
		rb_debug ("nothing selected");
		return;
	}

	if (source->priv->search_entry != NULL) {
		rb_search_entry_clear (source->priv->search_entry);
	}

	gtk_tree_model_get (GTK_TREE_MODEL (source->priv->browser_model), &iter,
			    0, &container,
			    2, &container_type,
			    -1);

	switch (container_type) {
	case CONTAINER_MARKER:
		expand_from_marker (source, &iter);
		break;
	case CONTAINER_NO_MEDIA:
		/* clear the track list? */
		break;
	case CONTAINER_UNKNOWN_MEDIA:
	case CONTAINER_HAS_MEDIA:
		/* fetch media directly under this container */
		start_media_browse (source, GRL_OP_BROWSE, container, &iter, CONTAINER_MAX_TRACKS);
		break;
	}
}

static gboolean
maybe_expand_container (RBGriloSource *source)
{
	GtkTreePath *path;
	GtkTreePath *end;
	GtkTreeIter iter;
	GtkTreeIter end_iter;
	GtkTreeIter next;
	int container_type;
	gboolean last;

	source->priv->maybe_expand_idle = 0;

	if (source->priv->browse_op != 0) {
		rb_debug ("not expanding, already browsing");
		return FALSE;
	}

	/* if we find a visible marker row, find more results */
	if (gtk_tree_view_get_visible_range (GTK_TREE_VIEW (source->priv->browser_view), &path, &end) == FALSE) {
		rb_debug ("not expanding, nothing to expand");
		return FALSE;
	}

	gtk_tree_model_get_iter (GTK_TREE_MODEL (source->priv->browser_model), &iter, path);
	gtk_tree_model_get_iter (GTK_TREE_MODEL (source->priv->browser_model), &end_iter, end);

	do {
		gtk_tree_path_free (path);
		path = gtk_tree_model_get_path (GTK_TREE_MODEL (source->priv->browser_model), &iter);
		last = (gtk_tree_path_compare (path, end) >= 0);
		gtk_tree_model_get (GTK_TREE_MODEL (source->priv->browser_model), &iter,
				    2, &container_type,
				    -1);
		if (container_type == CONTAINER_MARKER) {
			if (expand_from_marker (source, &iter)) {
				rb_debug ("expanding");
				break;
			}
		}

		next = iter;
		if (gtk_tree_view_row_expanded (GTK_TREE_VIEW (source->priv->browser_view), path) &&
		    gtk_tree_model_iter_has_child (GTK_TREE_MODEL (source->priv->browser_model), &iter)) {
			gtk_tree_model_iter_children (GTK_TREE_MODEL (source->priv->browser_model), &iter, &next);
		} else if (gtk_tree_model_iter_next (GTK_TREE_MODEL (source->priv->browser_model), &next)) {
			iter = next;
		} else {
			if (gtk_tree_model_iter_parent (GTK_TREE_MODEL (source->priv->browser_model), &next, &iter) == FALSE) {
				break;
			}
			iter = next;
			if (gtk_tree_model_iter_next (GTK_TREE_MODEL (source->priv->browser_model), &iter) == FALSE) {
				break;
			}
		}
	} while (last == FALSE);

	gtk_tree_path_free (path);
	gtk_tree_path_free (end);
	return FALSE;
}

static void
maybe_expand_container_idle (RBGriloSource *source)
{
	if (source->priv->maybe_expand_idle == 0) {
		source->priv->maybe_expand_idle = g_idle_add ((GSourceFunc)maybe_expand_container, source);
	}
}

static void
browser_row_expanded_cb (GtkTreeView *tree_view, GtkTreeIter *iter, GtkTreePath *path, RBGriloSource *source)
{
	maybe_expand_container_idle (source);
}

static void
scroll_adjust_changed_cb (GtkAdjustment *adjustment, RBGriloSource *source)
{
	maybe_expand_container_idle (source);
}

static void
scroll_adjust_value_changed_cb (GtkAdjustment *adjustment, RBGriloSource *source)
{
	maybe_expand_container_idle (source);
}

static void
impl_selected (RBDisplayPage *page)
{
	RBGriloSource *source = RB_GRILO_SOURCE (page);

	RB_DISPLAY_PAGE_CLASS (rb_grilo_source_parent_class)->selected (page);

	if (source->priv->done_initial_browse == FALSE) {
		source->priv->done_initial_browse = TRUE;
		start_browse (source, NULL, NULL, 0);
	}

	if (source->priv->search_entry != NULL) {
		rb_search_entry_set_mnemonic (source->priv->search_entry, TRUE);
	}
}

static void
impl_deselected (RBDisplayPage *page)
{
	RBGriloSource *source = RB_GRILO_SOURCE (page);

	RB_DISPLAY_PAGE_CLASS (rb_grilo_source_parent_class)->deselected (page);

	if (source->priv->search_entry != NULL) {
		rb_search_entry_set_mnemonic (source->priv->search_entry, FALSE);
	}
}

static RBEntryView *
impl_get_entry_view (RBSource *bsource)
{
	RBGriloSource *source = RB_GRILO_SOURCE (bsource);
	return source->priv->entry_view;
}

static void
search_cb (RBSearchEntry *search, const char *text, RBGriloSource *source)
{
	g_free (source->priv->search_text);
	source->priv->search_text = g_strdup (text);

	gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (source->priv->browser_view)));

	start_media_browse (source, GRL_OP_SEARCH, NULL, NULL, CONTAINER_MAX_TRACKS);
}

static void
notify_sort_order_cb (GObject *object, GParamSpec *pspec, RBGriloSource *source)
{
	rb_entry_view_resort_model (RB_ENTRY_VIEW (object));
}
