/* 
 *  arch-tag: Implementation of Internet Podcast source object
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho <renato.filho@indt.org.br>
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
 */

#include <config.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtkiconfactory.h>
#include <gtk/gtkprogressbar.h>
#include <gtk/gtkcombobox.h>
#include <gtk/gtkstock.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtkfilechooserbutton.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkvpaned.h>
#include <gtk/gtkhpaned.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkcellrendererprogress.h>
#include <gtk/gtksignal.h>
#include <glib/gfileutils.h>
#include <glade/glade.h>
#include <libgnome/gnome-i18n.h>
#include <libxml/tree.h>
#include <string.h>
#define __USE_XOPEN
#include <time.h>

#include "rb-podcast-source.h"

#include "rhythmdb-query-model.h"
#include "rb-statusbar.h"
#include "rb-glade-helpers.h"
#include "rb-stock-icons.h"
#include "rb-entry-view.h"
#include "rb-property-view.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "totem-pl-parser.h"
#include "rb-preferences.h"
#include "rb-dialog.h"
#include "rb-new-podcast-dialog.h"
#include "rb-podcast-properties-dialog.h"
#include "rb-feed-podcast-properties-dialog.h"
#include "rb-playlist-manager.h"
#include "rb-debug.h"
#include "rb-statusbar.h"
#include "eel-gconf-extensions.h"
#include "rb-podcast-manager.h"
#include "rb-cell-renderer-pixbuf.h"

typedef enum
{
	RB_PODCAST_QUERY_TYPE_ALL,
	RB_PODCAST_QUERY_TYPE_ALBUM,
	RB_PODCAST_QUERY_TYPE_SEARCH,
} RBPodcastQueryType;

static void rb_podcast_source_class_init 		(RBPodcastSourceClass *klass);

static void rb_podcast_source_init 			(RBPodcastSource *source);

static GObject *rb_podcast_source_constructor 		(GType type, 
							 guint n_construct_properties,
					      		 GObjectConstructParam *construct_properties);

static void rb_podcast_source_dispose 			(GObject *object);

static void rb_podcast_source_finalize 			(GObject *object);

static void rb_podcast_source_set_property 		(GObject *object,
			                  		 guint prop_id,
			                  		 const GValue *value,
			                  		 GParamSpec *pspec);

static void rb_podcast_source_get_property 		(GObject *object,
			                  		 guint prop_id,
			                  		 GValue *value,
			                  		 GParamSpec *pspec);

static void rb_podcast_source_songs_show_popup_cb 	(RBEntryView *view,
						  	 RBPodcastSource *source);


static void rb_podcast_source_feeds_show_popup_cb 	(RBSimpleView *view,
						  	 RBPodcastSource *source);

static void paned_size_allocate_cb 			(GtkWidget *widget,
				    			 GtkAllocation *allocation,
		                    			 RBPodcastSource *source);
				    
static void rb_podcast_source_state_pref_changed 	(GConfClient *client,
						 	 guint cnxn_id,
						 	 GConfEntry *entry,
					 		 RBPodcastSource *source);

static void rb_podcast_source_show_browser 		(RBPodcastSource *source,
					   		 gboolean show);

static void rb_podcast_source_state_prefs_sync 		(RBPodcastSource *source);


static void feed_select_change_cb 			(RBPropertyView *propview, 
							 GList *feeds,
			 			       	 RBPodcastSource *podcast_source);
							 

static void rb_podcast_source_posts_view_sort_order_changed_cb (RBEntryView *view, 
								RBPodcastSource *source);

static void rb_podcast_source_download_status_changed_cb(RBPodcastManager *download, 
							 RhythmDBEntry *entry, 
							 gulong status, 
							 RBPodcastSource *source);

static void rb_podcast_source_btn_file_change_cb 	(GtkFileChooserButton *widget, 
							 gpointer data);


void rb_podcast_source_show_columns_changed_cb 		(GtkToggleButton *button, 
							 RBPodcastSource *source);

static void posts_view_drag_data_received_cb 		(GtkWidget *widget,
					      		 GdkDragContext *dc,
  				              		 gint x, gint y,
				              		 GtkSelectionData *selection_data, 
				              		 guint info, guint time,
				              		 RBPodcastSource *source);

static void rb_podcast_source_cmd_download_post		(GtkAction *action,
							 RBPodcastSource *source);

static void rb_podcast_source_cmd_delete_feed		(GtkAction *action,
							 RBPodcastSource *source);

static void rb_podcast_source_cmd_update_feed		(GtkAction *action,
							 RBPodcastSource *source);

static void rb_podcast_source_cmd_update_all		(GtkAction *action,
							 RBPodcastSource *source);

static void rb_podcast_source_cmd_properties_feed	(GtkAction *action,
							 RBPodcastSource *source);

static void register_action_group			(RBPodcastSource *source);


static gint rb_podcast_source_post_date_cell_sort_func 	(RhythmDBEntry *a, RhythmDBEntry *b,
		                                   	 RBPodcastSource *source);

static gint rb_podcast_source_post_status_cell_sort_func(RhythmDBEntry *a, RhythmDBEntry *b,
		                                   	 RBPodcastSource *source);

static gint rb_podcast_source_post_feed_cell_sort_func 	(RhythmDBEntry *a, RhythmDBEntry *b,
		                                   	 RBPodcastSource *source);

static void rb_podcast_source_post_status_cell_data_func(GtkTreeViewColumn *column, GtkCellRenderer *renderer,
						     	 GtkTreeModel *tree_model, GtkTreeIter *iter,
						     	 RBPodcastSource *source);

static void rb_podcast_source_post_date_cell_data_func 	(GtkTreeViewColumn *column, GtkCellRenderer *renderer,
		 			     	   	 GtkTreeModel *tree_model, GtkTreeIter *iter,
				     	           	 RBPodcastSource *source);

static void rb_podcast_source_post_feed_cell_data_func  (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
						     	 GtkTreeModel *tree_model, GtkTreeIter *iter,
						     	 RBPodcastSource *source);

static void rb_podcast_source_feed_status_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
						     	 GtkTreeModel *tree_model, GtkTreeIter *iter,
						     	 RBPodcastSource *source);

static void rb_podcast_source_feed_title_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
						     	 GtkTreeModel *tree_model, GtkTreeIter *iter,
						     	 RBPodcastSource *source);

static void rb_podcast_source_start_download_cb 	(RBPodcastManager *pd, 
							 RhythmDBEntry *entry,
							 RBPodcastSource *source);
	
static void rb_podcast_source_finish_download_cb 	(RBPodcastManager *pd, 
							 RhythmDBEntry *entry,
							 RBPodcastSource *source);

static void rb_podcast_source_feed_updates_avaliable_cb (RBPodcastManager *pd, 
							 RhythmDBEntry *entry,
							 RBPodcastSource *source);

static void rb_podcast_source_download_process_error_cb (RBPodcastManager *pd,
							 const char *error,
					  		 RBPodcastSource *source);

static void rb_podcast_source_cb_interval_changed_cb 	(GtkComboBox *box, gpointer cb_data);

static gboolean rb_podcast_source_load_finish_cb  	(gpointer cb_data);
static RBShell *rb_podcast_source_get_shell		(RBPodcastSource *source);
static void rb_podcast_source_entry_activated_cb (RBEntryView *view,
						  RhythmDBEntry *entry,
						  RBPodcastSource *source);



/* source methods */
static char *impl_get_status 				(RBSource *source);
static const char *impl_get_browser_key 		(RBSource *source);
static GdkPixbuf *impl_get_pixbuf 			(RBSource *source);
static RBEntryView *impl_get_entry_view 		(RBSource *source);
static void impl_search 				(RBSource *source, const char *text);
static void impl_delete 				(RBSource *source);
static void impl_song_properties 			(RBSource *source);
static RBSourceEOFType impl_handle_eos 			(RBSource *asource);
static gboolean impl_show_popup 			(RBSource *source);
static void rb_podcast_source_do_query			(RBPodcastSource *source, RBPodcastQueryType type);
static GtkWidget *impl_get_config_widget 		(RBSource *source);
static gboolean impl_receive_drag 			(RBSource *source, 
							 GtkSelectionData *data);



#define CMD_PATH_SHOW_BROWSER "/commands/ShowBrowser"
#define CMD_PATH_CURRENT_STATION "/commands/CurrentStation"
#define CMD_PATH_SONG_INFO    "/commands/SongInfo"


#define CONF_UI_PODCAST_DIR 			CONF_PREFIX "/ui/podcast"
#define CONF_UI_PODCAST_COLUMNS_SETUP 		CONF_PREFIX "/ui/podcast/columns_setup"
#define CONF_STATE_PODCAST_PREFIX		CONF_PREFIX "/state/podcast"
#define CONF_STATE_PANED_POSITION 		CONF_STATE_PODCAST_PREFIX "/paned_position"
#define CONF_STATE_PODCAST_SORTING_POSTS	CONF_STATE_PODCAST_PREFIX "/sorting_posts"
#define CONF_STATE_PODCAST_SORTING_FEEDS	CONF_STATE_PODCAST_PREFIX "/sorting_feeds"
#define CONF_STATE_SHOW_BROWSER   		CONF_STATE_PODCAST_PREFIX "/show_browser"
#define CONF_STATE_PODCAST_DOWNLOAD_DIR		CONF_STATE_PODCAST_PREFIX "/download_prefix"
#define CONF_STATE_PODCAST_DOWNLOAD_INTERVAL	CONF_STATE_PODCAST_PREFIX "/download_interval"
#define CONF_STATE_PODCAST_DOWNLOAD_NEXT_TIME	CONF_STATE_PODCAST_PREFIX "/download_next_time"

struct RBPodcastSourcePrivate
{
	gboolean disposed;
	
	RhythmDB *db;


	GtkWidget *vbox;
	GtkWidget *config_widget;
	GtkWidget *paned;
	
	GdkPixbuf *pixbuf;
	GdkPixbuf *feed_subscribe_pixbuf;
	GdkPixbuf *feed_unsubscribe_pixbuf;
	GdkPixbuf *feed_working;

	RBSimpleView *feeds;
	RBEntryView *posts;
	GtkActionGroup *action_group;

	char *search_text;
	GList *selected_feeds;
	RhythmDBQueryModel *cached_all_query;

	gboolean initialized;
	
	RhythmDBEntryType entry_type;
	RBPodcastManager *podcast_mg;	
};

static GtkActionEntry rb_podcast_source_actions [] =
{
	{ "PodcastSrcDownloadPost", NULL, N_("Download Podcast _Episode"), NULL,
	  N_("Download Post"),
	  G_CALLBACK (rb_podcast_source_cmd_download_post) },
	{ "PodcastFeedProperties", GTK_STOCK_PROPERTIES, N_("_Properties"), NULL,
	  N_("Properties Feed"),
	  G_CALLBACK (rb_podcast_source_cmd_properties_feed) },
	{ "PodcastFeedUpdate", GTK_STOCK_REFRESH, N_("_Update Podcast Feed"), NULL,
	  N_("Update Feed"),
	  G_CALLBACK (rb_podcast_source_cmd_update_feed) },
	{ "PodcastFeedDelete", GTK_STOCK_DELETE, N_("_Delete Podcast Feed"), NULL,
	  N_("Delete Feed"),
	  G_CALLBACK (rb_podcast_source_cmd_delete_feed) },
	{ "PodcastUpdateAllFeeds", GTK_STOCK_REFRESH, N_("_Update All Feeds"), NULL,
	  N_("Update all feeds"),
	  G_CALLBACK (rb_podcast_source_cmd_update_all) },
};


static const GtkTargetEntry posts_view_drag_types[] = {
	{  "text/uri-list", 0, 0 },
	{  "_NETSCAPE_URL", 0, 1 },
	{  "application/rss+xml", 0, 2 },
};


enum
{
	PROP_0,
	PROP_ENTRY_TYPE,
	PROP_PODCAST_MANAGER
};

G_DEFINE_TYPE (RBPodcastSource, rb_podcast_source, RB_TYPE_SOURCE)


static void
rb_podcast_source_class_init (RBPodcastSourceClass *klass)
{

	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = rb_podcast_source_dispose;
	object_class->finalize = rb_podcast_source_finalize;
	object_class->constructor = rb_podcast_source_constructor;

	object_class->set_property = rb_podcast_source_set_property;
	object_class->get_property = rb_podcast_source_get_property;

	source_class->impl_get_status  = impl_get_status;
	source_class->impl_get_browser_key  = impl_get_browser_key;
	source_class->impl_get_pixbuf  = impl_get_pixbuf;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_search = impl_search;
	source_class->impl_get_config_widget = impl_get_config_widget;
	source_class->impl_search = impl_search;
	source_class->impl_get_entry_view = impl_get_entry_view;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_delete = impl_delete;
	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_handle_eos = impl_handle_eos;
	source_class->impl_have_url = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_receive_drag = impl_receive_drag;
	
	g_object_class_install_property (object_class,
					 PROP_ENTRY_TYPE,
					 g_param_spec_uint ("entry-type",
							    "Entry type",
							    "Type of the entries which should be displayed by this source",
							    0,
							    G_MAXINT,
							    RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_PODCAST_MANAGER,
					 g_param_spec_object ("podcast-manager",
					                      "RBPodcastManager",
					                      "RBPodcastManager object",
					                      RB_TYPE_PODCAST_MANAGER,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	
	
	
}

static void
rb_podcast_source_init (RBPodcastSource *source)
{
	GtkWidget *dummy = gtk_tree_view_new ();
	source->priv = g_new0 (RBPodcastSourcePrivate, 1);

	source->priv->selected_feeds = NULL;
	source->priv->vbox = gtk_vbox_new (FALSE, 5);

	gtk_container_add (GTK_CONTAINER (source), source->priv->vbox);
	
	source->priv->pixbuf = gtk_widget_render_icon (dummy,
						       RB_STOCK_PODCAST,
						       GTK_ICON_SIZE_LARGE_TOOLBAR,
						       NULL);


	source->priv->feed_subscribe_pixbuf = gtk_widget_render_icon (dummy,
								      GTK_STOCK_YES,
								      GTK_ICON_SIZE_MENU,
								      NULL);
	
	source->priv->feed_unsubscribe_pixbuf = gtk_widget_render_icon (dummy,
								        GTK_STOCK_NO,
								      	GTK_ICON_SIZE_MENU,
								      	NULL);

	gtk_widget_destroy (dummy);
}

static void
rb_podcast_source_dispose (GObject *object)
{
	RBPodcastSource *source;

	rb_debug ("dispose podcast_source");
	source = RB_PODCAST_SOURCE (object);

	if (source->priv->disposed)
		return;
	
	source->priv->disposed = TRUE;
}

static void
rb_podcast_source_finalize (GObject *object)
{
	RBPodcastSource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PODCAST_SOURCE (object));

	source = RB_PODCAST_SOURCE (object);

	g_return_if_fail (source->priv != NULL);

	rb_debug ("finalizing podcast source");

	g_object_unref (source->priv->podcast_mg);

	if (source->priv->selected_feeds) {
		g_list_foreach (source->priv->selected_feeds, (GFunc) g_free, NULL);
	        g_list_free (source->priv->selected_feeds);
	}	

       if (source->priv->cached_all_query)
		g_object_unref (G_OBJECT (source->priv->cached_all_query));

	g_free (source->priv);


	G_OBJECT_CLASS (rb_podcast_source_parent_class)->finalize (object);
}

static GObject *
rb_podcast_source_constructor (GType type, guint n_construct_properties,
			      GObjectConstructParam *construct_properties)
{
	RBPodcastSource *source;
	RBPodcastSourceClass *klass;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	RhythmDBPropertyModel *feed_model;


	RBShell *shell;

	RhythmDBQueryModel *query_model;
	GtkTreeModel *model;
	GPtrArray *query;
	gint width;

	klass = RB_PODCAST_SOURCE_CLASS (g_type_class_peek (RB_TYPE_PODCAST_SOURCE));

	source = RB_PODCAST_SOURCE (G_OBJECT_CLASS (rb_podcast_source_parent_class)->
			constructor (type, n_construct_properties, construct_properties));

	register_action_group (source);
	source->priv->paned = gtk_vpaned_new ();

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "db", &source->priv->db, NULL);
	
	gtk_idle_add ((GtkFunction) rb_podcast_source_load_finish_cb, source);

	source->priv->podcast_mg = rb_podcast_manager_new (source->priv->db);

	g_object_unref (shell);
	

	/* set up posts view */
	source->priv->posts = rb_entry_view_new (source->priv->db, CONF_STATE_PODCAST_SORTING_POSTS,
						    FALSE, FALSE);
	g_signal_connect_object (G_OBJECT (source->priv->posts),
				 "entry-activated",
				 G_CALLBACK (rb_podcast_source_entry_activated_cb),
				 source, 0);


	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new();
	
	gtk_tree_view_column_pack_start (column, renderer, TRUE);

	gtk_tree_view_column_set_clickable (column, TRUE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	
	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 (GtkTreeCellDataFunc) rb_podcast_source_post_date_cell_data_func,
						 source, NULL);
	
	rb_entry_view_append_column_custom (source->priv->posts, column, 
					    _("_Date"), "Date", 
					    (GCompareDataFunc) rb_podcast_source_post_date_cell_sort_func, NULL);
						    

	rb_entry_view_append_column (source->priv->posts, RB_ENTRY_VIEW_COL_TITLE);
	
	//COLUMN FEED
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new();
	
	gtk_tree_view_column_pack_start (column, renderer, TRUE);

	gtk_tree_view_column_set_clickable (column, TRUE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	
	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 (GtkTreeCellDataFunc) rb_podcast_source_post_feed_cell_data_func,
						 source, NULL);
	
	rb_entry_view_append_column_custom (source->priv->posts, column, 
					    _("_Feed"), "Feed", 
					    (GCompareDataFunc) rb_podcast_source_post_feed_cell_sort_func, NULL);

	
	rb_entry_view_append_column (source->priv->posts, RB_ENTRY_VIEW_COL_DURATION);
	rb_entry_view_append_column (source->priv->posts, RB_ENTRY_VIEW_COL_RATING);
	rb_entry_view_append_column (source->priv->posts, RB_ENTRY_VIEW_COL_PLAY_COUNT);
	rb_entry_view_append_column (source->priv->posts, RB_ENTRY_VIEW_COL_LAST_PLAYED);


	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_progress_new();
	
	gtk_tree_view_column_pack_start (column, renderer, TRUE);

	gtk_tree_view_column_set_clickable (column, TRUE);
	gtk_tree_view_column_set_fixed_width (column, 80);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
	
	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 (GtkTreeCellDataFunc) rb_podcast_source_post_status_cell_data_func,
						 source, NULL);
	
	rb_entry_view_append_column_custom (source->priv->posts, column, 
					    _("Status"), "Status", 
					    (GCompareDataFunc) rb_podcast_source_post_status_cell_sort_func, NULL);

	g_signal_connect_object (G_OBJECT (source->priv->posts),
				 "sort-order-changed",
				 G_CALLBACK (rb_podcast_source_posts_view_sort_order_changed_cb),
				 source, 0);

	/* set up drag and drop for the song tree view.
	 * we don't use RBEntryView's DnD support because it does too much.
	 * we just want to be able to drop stations in to add them.
	 */
	g_signal_connect_object (G_OBJECT (source->priv->posts), 
				 "drag_data_received",
				 G_CALLBACK (posts_view_drag_data_received_cb),
				 source, 0);

	gtk_drag_dest_set (GTK_WIDGET (source->priv->posts),
			   GTK_DEST_DEFAULT_ALL,
			   posts_view_drag_types, 2,
			   GDK_ACTION_COPY | GDK_ACTION_MOVE);


	g_signal_connect (G_OBJECT (source->priv->podcast_mg),
			  "status_changed",
			  G_CALLBACK (rb_podcast_source_download_status_changed_cb),
			  source);
	
	g_signal_connect_object (G_OBJECT (source->priv->podcast_mg), 
			  	 "process_error",
			 	 G_CALLBACK (rb_podcast_source_download_process_error_cb),
			  	 source, 0);
	
	g_signal_connect_object (G_OBJECT (source->priv->posts),
				 "size_allocate",
				 G_CALLBACK (paned_size_allocate_cb),
				 source, 0);
	
	g_signal_connect_object (G_OBJECT (source->priv->posts), "show_popup",
				 G_CALLBACK (rb_podcast_source_songs_show_popup_cb), source, 0);
	

	//configure feed view
	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      RHYTHMDB_ENTRY_TYPE_PODCAST_FEED,
				      RHYTHMDB_QUERY_END);
	
	query_model = rhythmdb_query_model_new_empty (source->priv->db);

	model = GTK_TREE_MODEL (query_model);
	
	rhythmdb_do_full_query_parsed (source->priv->db, model, query);


	source->priv->feeds = rb_simple_view_new (source->priv->db,  RHYTHMDB_PROP_LOCATION, 
						  _("Feed"));	
	
	rb_property_view_set_selection_mode (RB_PROPERTY_VIEW (source->priv->feeds), GTK_SELECTION_MULTIPLE);
	
	feed_model = rb_property_view_get_model (RB_PROPERTY_VIEW (source->priv->feeds));
	g_object_set (G_OBJECT (feed_model), "query-model", query_model, NULL);
	
	rhythmdb_query_free (query);
	
	//column status
	column = gtk_tree_view_column_new ();
	renderer = rb_cell_renderer_pixbuf_new ();
	
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &width, NULL);
	gtk_tree_view_column_set_fixed_width (column, width + 5);


	gtk_tree_view_column_set_cell_data_func (column, 
						 renderer,
						 (GtkTreeCellDataFunc) rb_podcast_source_feed_status_cell_data_func,
						 source, NULL);

	rb_simple_view_append_column_custom (source->priv->feeds,
					       column, " ", source);
	

	//column title
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();

	gtk_tree_view_column_pack_start (column, renderer, TRUE);

	gtk_tree_view_column_set_cell_data_func (column, 
						 renderer,
						 (GtkTreeCellDataFunc) rb_podcast_source_feed_title_cell_data_func,
						 source, NULL);
	
	rb_simple_view_append_column_custom (source->priv->feeds,
		 		             column, _("Feed"), source);

	
	g_signal_connect_object (G_OBJECT (source->priv->feeds), 
				 "drag_data_received",
				 G_CALLBACK (posts_view_drag_data_received_cb),
				 source, 0);

	gtk_drag_dest_set (GTK_WIDGET (source->priv->feeds),
			   GTK_DEST_DEFAULT_ALL,
			   posts_view_drag_types, 2,
			   GDK_ACTION_COPY | GDK_ACTION_MOVE);


	g_signal_connect_object (G_OBJECT (source->priv->feeds), "show_popup",
				 G_CALLBACK (rb_podcast_source_feeds_show_popup_cb), 
				 source, 0);

	g_signal_connect_object (G_OBJECT (source->priv->feeds),
				 "properties-selected",
				 G_CALLBACK (feed_select_change_cb),
				 source, 0);


	g_object_ref (G_OBJECT (source->priv->feeds));


	/* set up propiets page */
	
	gtk_paned_pack2 (GTK_PANED (source->priv->paned),
			 GTK_WIDGET (source->priv->posts), TRUE, FALSE);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->vbox), source->priv->paned);


	rb_podcast_source_state_prefs_sync (source);
	
	
	eel_gconf_notification_add (CONF_STATE_PODCAST_PREFIX,
				    (GConfClientNotifyFunc) rb_podcast_source_state_pref_changed,
				    source);
	
	gtk_widget_show_all (GTK_WIDGET (source));

	rb_podcast_source_do_query (source, RB_PODCAST_QUERY_TYPE_ALL);
	

	return G_OBJECT (source);
}

static void
rb_podcast_source_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (object);

	switch (prop_id)
	{
	case PROP_ENTRY_TYPE:
		source->priv->entry_type = g_value_get_uint (value);
		break;
	case PROP_PODCAST_MANAGER:
		source->priv->podcast_mg = g_value_get_object (value);
		break;		
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_podcast_source_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (object);

	switch (prop_id)
	{
	case PROP_ENTRY_TYPE:
		g_value_set_uint (value, source->priv->entry_type);
		break;
	case PROP_PODCAST_MANAGER:
		g_value_set_object (value, source->priv->podcast_mg);
	        break;	
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBSource *
rb_podcast_source_new (RBShell *shell)
{
	RBSource *source;
	source = RB_SOURCE (g_object_new (RB_TYPE_PODCAST_SOURCE,
					  "name", _("Podcasts"),
					  "shell", shell,
					  NULL));

	rb_shell_register_entry_type_for_source (shell, source, 
						 RHYTHMDB_ENTRY_TYPE_PODCAST_POST);
	
	return source;
}

static GdkPixbuf *
impl_get_pixbuf (RBSource *asource)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);

	return source->priv->pixbuf;
}

static void
impl_search (RBSource *asource, const char *search_text)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);

	if (source->priv->initialized) {
		if (search_text == NULL && source->priv->search_text == NULL)
			return;
		if (search_text != NULL &&
		    source->priv->search_text != NULL
		    && !strcmp (search_text, source->priv->search_text))
			return;
	}

	source->priv->initialized = TRUE;
	if (search_text != NULL && search_text[0] == '\0')
		search_text = NULL;

	g_free (source->priv->search_text);
	if (search_text)
		source->priv->search_text = g_utf8_casefold (search_text, -1);
	else
		source->priv->search_text = NULL;
	rb_podcast_source_do_query (source, RB_PODCAST_QUERY_TYPE_SEARCH);

	rb_source_notify_filter_changed (RB_SOURCE (source));
}

static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);

	return source->priv->posts;
}

static RBSourceEOFType
impl_handle_eos (RBSource *asource)
{
	return RB_SOURCE_EOF_NEXT;
}

static char *
impl_get_status (RBSource *asource)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);
	gchar *status;

	status = rhythmdb_compute_status_normal (rb_entry_view_get_num_entries (source->priv->posts),
						 rb_entry_view_get_duration (source->priv->posts),
						 rb_entry_view_get_total_size (source->priv->posts));
	return status;
}

static const char *
impl_get_browser_key (RBSource *asource)
{
	return CONF_STATE_SHOW_BROWSER;
}

static void
impl_delete (RBSource *asource)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);
	GList *l;
	gint ret;
	GtkWidget *dialog;
	GtkWidget *button;
	GtkWindow *window;
	RBShell *shell;

	rb_debug ("Delete episode action");
	
	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "window", &window, NULL);
	g_object_unref (G_OBJECT (shell));
	
	dialog = gtk_message_dialog_new (window,
			                 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_NONE,
					 _("Delete the podcast episode and downloaded file?"));

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
	                                          _("If you choose to delete the episode and file, "
						    "they will be permanently lost.  Please note that "
						    "you can delete the episode but keep the downloaded "
						    "file by choosing to delete the episode only."));

	gtk_window_set_title (GTK_WINDOW (dialog), "");

	gtk_dialog_add_buttons (GTK_DIALOG (dialog), 
	                        _("Delete _Episode Only"), 
	                        GTK_RESPONSE_NO,
	                        GTK_STOCK_CANCEL, 
	                        GTK_RESPONSE_CANCEL, 
	                        NULL);
	button = gtk_dialog_add_button (GTK_DIALOG (dialog),
	                                _("_Delete Episode And File"),
			                GTK_RESPONSE_YES);
	
	gtk_window_set_focus (GTK_WINDOW (dialog), button);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);

	ret = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	if (ret == GTK_RESPONSE_CANCEL || ret == GTK_RESPONSE_DELETE_EVENT)
		return;

	rb_podcast_manager_set_remove_files (source->priv->podcast_mg, 
					     (ret == GTK_RESPONSE_YES));
	
	for (l = rb_entry_view_get_selected_entries (source->priv->posts); l != NULL;
	     l = g_list_next (l)) {
		rhythmdb_entry_delete (source->priv->db, l->data);
		rhythmdb_commit (source->priv->db);
	}
}

static void
impl_song_properties (RBSource *asource)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);
	GtkWidget *dialog = rb_podcast_properties_dialog_new (source->priv->posts);
	rb_debug ("in song properties");
	if (dialog)
		gtk_widget_show_all (dialog);
	else
		rb_debug ("no selection!");
}

static void
paned_size_allocate_cb (GtkWidget *widget,
			GtkAllocation *allocation,
		        RBPodcastSource *source)
{
	rb_debug ("paned size allocate");
	eel_gconf_set_integer (CONF_STATE_PANED_POSITION,
			       gtk_paned_get_position (GTK_PANED (source->priv->paned)));
}

static void
rb_podcast_source_state_prefs_sync (RBPodcastSource *source)
{
	rb_debug ("syncing state");
	gtk_paned_set_position (GTK_PANED (source->priv->paned),
				eel_gconf_get_integer (CONF_STATE_PANED_POSITION));
	rb_podcast_source_show_browser (source,
				       eel_gconf_get_boolean (CONF_STATE_SHOW_BROWSER));
}

static void
rb_podcast_source_state_pref_changed (GConfClient *client,
				     guint cnxn_id,
				     GConfEntry *entry,
				     RBPodcastSource *source)
{
	rb_debug ("state prefs changed");
	rb_podcast_source_state_prefs_sync (source);
}

static void
rb_podcast_source_posts_view_sort_order_changed_cb (RBEntryView *view,
						   RBPodcastSource *source)
{
	rb_debug ("sort order changed");
	rb_entry_view_set_resorting (view);
	rb_podcast_source_do_query (source, RB_PODCAST_QUERY_TYPE_SEARCH);
}

static void
rb_podcast_source_download_status_changed_cb (RBPodcastManager *download,
						RhythmDBEntry *entry,
						gulong status,
						RBPodcastSource *source)
{
	gtk_widget_queue_draw(GTK_WIDGET(source->priv->posts));
	return;

}


static void
rb_podcast_source_songs_show_popup_cb (RBEntryView *view,
				       RBPodcastSource *source)
{
	if (G_OBJECT (source) == NULL) {
		return;
	}
	else {
		GtkAction* act_post;
		gulong status;
		gulong all_status = 999;
		RhythmDBEntry *entry = NULL;
		GList *lst = rb_entry_view_get_selected_entries(view);
		
		while (lst) {
			entry = (RhythmDBEntry *) lst->data;
			status = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);
			if ((status != all_status) && (all_status != 999)) {
				all_status = 999;
				break;
			} 
			else {
				all_status = status;
			}
					
			lst = lst->next;
		}

		act_post = gtk_action_group_get_action (source->priv->action_group, "PodcastSrcDownloadPost");

		if ((all_status < 100) || (all_status == 102))  {
			g_object_set (G_OBJECT (act_post), "label", _("_Cancel Download") , NULL);
			gtk_action_set_visible (act_post, TRUE);
		}
		else if ((all_status > 100) && (all_status != 999)) {
			g_object_set (G_OBJECT (act_post), "label", _("_Download Post") , NULL);
			gtk_action_set_visible (act_post, TRUE);
		}
		else 
			gtk_action_set_visible (act_post, FALSE);

		_rb_source_show_popup (RB_SOURCE (source), "/PodcastViewPopup");
	}
}


static void
rb_podcast_source_feeds_show_popup_cb (RBSimpleView *view,
				       RBPodcastSource *source)
{
	if (G_OBJECT (source) == NULL) {
		return;
	}
	else {
		GtkAction* act_update;
		GtkAction* act_properties;
		GtkAction* act_delete;

		gulong status;
		gulong all_status = 999;
		RhythmDBEntry *entry = NULL;
		GList *lst = source->priv->selected_feeds;

		act_update = gtk_action_group_get_action (source->priv->action_group, "PodcastFeedUpdate");
		act_properties = gtk_action_group_get_action (source->priv->action_group, "PodcastFeedProperties");
		act_delete = gtk_action_group_get_action (source->priv->action_group, "PodcastFeedDelete");
		
		if (lst) {
			while (lst) {
				entry = rhythmdb_entry_lookup_by_location (source->priv->db, 
									   (gchar *) lst->data);
				status = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);
				if ((status != all_status) && (all_status != 999)) {
					all_status = 999;
					break;
				} 
				else {
					all_status = status;
				}
						
				lst = lst->next;
			}

			gtk_action_set_visible (act_properties, TRUE);
			gtk_action_set_visible (act_delete, TRUE);
		} else {
			gtk_action_set_visible (act_update, FALSE);
			gtk_action_set_visible (act_properties, FALSE);
			gtk_action_set_visible (act_delete, FALSE);
		}

		_rb_source_show_popup (RB_SOURCE (source), "/PodcastFeedViewPopup");
	}
}



static void
feed_select_change_cb (RBPropertyView *propview, GList *feeds,
		   RBPodcastSource *source)
{
	if (source->priv->selected_feeds) {
		g_list_foreach (source->priv->selected_feeds, (GFunc) g_free, NULL);
	        g_list_free (source->priv->selected_feeds);
	}	

	source->priv->selected_feeds = feeds;
	
	rb_podcast_source_do_query (source, RB_PODCAST_QUERY_TYPE_ALBUM);

	rb_source_notify_filter_changed (RB_SOURCE (source));
}



static void
rb_podcast_source_show_browser (RBPodcastSource *source,
			       gboolean show)
{
	
	GtkWidget *feedswidget = GTK_WIDGET (source->priv->feeds);

	if (show == TRUE) {
		gtk_paned_pack1 (GTK_PANED (source->priv->paned), feedswidget, FALSE, FALSE);
		gtk_widget_show_all (feedswidget);
	} else if (show == FALSE) {
		GList *children = gtk_container_get_children (GTK_CONTAINER (source->priv->paned));
		gtk_widget_hide (feedswidget);
		if (g_list_find (children, feedswidget))
		    gtk_container_remove (GTK_CONTAINER (source->priv->paned), feedswidget);
		
		g_list_free (children);
	}
}

static void
rb_podcast_source_do_query (RBPodcastSource *source, RBPodcastQueryType qtype)
{
	RhythmDBEntryType entry_type;
	RhythmDBQueryModel *query_model;
	GtkTreeModel *model;
	GPtrArray *query;
	GList *lst_feeds;
	gboolean is_all_query;

	rb_debug ("select entry filter");
	
	is_all_query  = ((qtype == RB_PODCAST_QUERY_TYPE_ALL) &&
			 (source->priv->selected_feeds == NULL) &&
			 (source->priv->search_text == NULL));

	if (is_all_query && (source->priv->cached_all_query)) {
                rb_debug ("using cached query");
                rb_entry_view_set_model (source->priv->posts, RHYTHMDB_QUERY_MODEL (source->priv->cached_all_query));
                return;
	}
	
	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      entry_type,
				      RHYTHMDB_QUERY_END);

	query_model = rhythmdb_query_model_new_empty (source->priv->db);

	if (is_all_query) {
                source->priv->cached_all_query = query_model;
	} else {
		if (source->priv->search_text) {
			GPtrArray *subquery = rhythmdb_query_parse (source->priv->db,
								    RHYTHMDB_QUERY_PROP_LIKE,
								    RHYTHMDB_PROP_ALBUM_FOLDED,
								    source->priv->search_text,
								    RHYTHMDB_QUERY_DISJUNCTION,
								    RHYTHMDB_QUERY_PROP_LIKE,
								    RHYTHMDB_PROP_TITLE_FOLDED,
								    source->priv->search_text,
								    RHYTHMDB_QUERY_END);
			rhythmdb_query_append (source->priv->db,
					       query,
					       RHYTHMDB_QUERY_SUBQUERY,
					       subquery,
					       RHYTHMDB_QUERY_END);
		}

		lst_feeds = source->priv->selected_feeds;
	
		if (lst_feeds) {
			GPtrArray *subquery = g_ptr_array_new ();
			
			rb_debug ("feed selected");
			
			rhythmdb_query_append (source->priv->db,
					       subquery,
					       RHYTHMDB_QUERY_PROP_EQUALS,
					       RHYTHMDB_PROP_SUBTITLE,
					       (gchar *) lst_feeds->data,
					       RHYTHMDB_QUERY_END);

			rb_debug ("entry: %s", (gchar *) lst_feeds->data);
			lst_feeds = lst_feeds->next;				       

			while (lst_feeds) {
				rb_debug ("equals loop");
				rhythmdb_query_append (source->priv->db,
						       subquery,
						       RHYTHMDB_QUERY_DISJUNCTION,
						       RHYTHMDB_QUERY_PROP_EQUALS,
						       RHYTHMDB_PROP_SUBTITLE,
						       (gchar *) lst_feeds->data,
						       RHYTHMDB_QUERY_END);
				lst_feeds = lst_feeds->next;
			}
			
			rhythmdb_query_append (source->priv->db, query, RHYTHMDB_QUERY_SUBQUERY, subquery,
					       RHYTHMDB_QUERY_END);
		}
	}
		
	rb_debug ("query create end");


	model = GTK_TREE_MODEL (query_model);

	rb_entry_view_set_model (source->priv->posts, RHYTHMDB_QUERY_MODEL (query_model));

	rhythmdb_do_full_query_async_parsed (source->priv->db, model, query);

	rhythmdb_query_free (query);
}

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/PodcastSourcePopup");
	return TRUE;
}


static GtkWidget *
impl_get_config_widget (RBSource *asource)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);
	GtkWidget *cb_update_interval;
	GtkWidget *btn_file;
	const char *download_dir;
	GladeXML *xml;

	if (source->priv->config_widget)
		return source->priv->config_widget;


	xml = rb_glade_xml_new ("podcast-prefs.glade", "podcast_vbox", source);
	source->priv->config_widget = glade_xml_get_widget (xml, "podcast_vbox");
	
	btn_file = glade_xml_get_widget (xml, "location_chooser");
	download_dir = 	eel_gconf_get_string(CONF_STATE_PODCAST_DOWNLOAD_DIR);
	if (download_dir != NULL) {
		gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER(btn_file), 
						download_dir);
	}
	g_signal_connect ( G_OBJECT(btn_file),
				"selection-changed",
				G_CALLBACK (rb_podcast_source_btn_file_change_cb),
				CONF_STATE_PODCAST_DOWNLOAD_DIR);

	cb_update_interval = glade_xml_get_widget (xml, "cb_update_interval");
	gtk_combo_box_set_active (GTK_COMBO_BOX (cb_update_interval),
				  eel_gconf_get_integer (CONF_STATE_PODCAST_DOWNLOAD_INTERVAL));
	g_signal_connect ( G_OBJECT(cb_update_interval),
			   "changed",
			   G_CALLBACK(rb_podcast_source_cb_interval_changed_cb),
			   source);
				
	return source->priv->config_widget;
}

static void 
rb_podcast_source_btn_file_change_cb (GtkFileChooserButton *widget, gpointer data)
{

	eel_gconf_set_string ((const char *) data,
			      gnome_vfs_get_local_path_from_uri (
		              gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(widget)))
			     );
}


static void
posts_view_drag_data_received_cb (GtkWidget *widget,
				  GdkDragContext *dc,
				  gint x, gint y,
				  GtkSelectionData *selection_data, 
				  guint info, guint time,
				  RBPodcastSource *source)
{
	GList *list, *uri_list, *i;

	rb_debug ("parsing uri list");
	list = gnome_vfs_uri_list_parse ((char *)selection_data->data);

	if (list == NULL)
		return;

	uri_list = NULL;

	for (i = list; i != NULL; i = g_list_next (i))
		uri_list = g_list_append (uri_list, gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0));

	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL)
		return;

	rb_debug ("adding uris");

	i = uri_list;
	while (i != NULL) {
		char *uri = NULL;

		uri = i->data;

		/* as totem source says, "Super _NETSCAPE_URL trick" */
		if (info == 1) {
			i = i->next;
			if (i != NULL) {
				g_free (i->data);
			}
		}
		if ((uri != NULL) && 
		    (!rhythmdb_entry_lookup_by_location (source->priv->db, uri))) {
			rb_podcast_source_add_feed (source, uri);
		}

		g_free (uri);

		if (i != NULL)
			i = i->next;
	}

	g_list_free (uri_list);
	return;
}

void 
rb_podcast_source_add_feed (RBPodcastSource *source, const char *uri)
{
	rb_podcast_manager_subscribe_feed (source->priv->podcast_mg, uri);
}


static void
register_action_group (RBPodcastSource *source)
{
	GtkUIManager *uimanager;
	GList *actiongroups;
	GList *group;

	g_object_get (G_OBJECT (source), "ui-manager", &uimanager, NULL);
	actiongroups = gtk_ui_manager_get_action_groups (uimanager);

	/* Don't create the action group if it's already registered */
	for (group = actiongroups; group != NULL; group = group->next) {
		const gchar *name;
		name = gtk_action_group_get_name (GTK_ACTION_GROUP (group->data));
		if (strcmp (name, "PodcastActions") == 0) {
			g_object_unref (G_OBJECT (uimanager));
			source->priv->action_group = GTK_ACTION_GROUP (group->data);
			return;
		}
	}

	source->priv->action_group = gtk_action_group_new ("PodcastActions");
	gtk_action_group_set_translation_domain (source->priv->action_group,
						 GETTEXT_PACKAGE);
	gtk_action_group_add_actions (source->priv->action_group, 
				      rb_podcast_source_actions,
				      G_N_ELEMENTS (rb_podcast_source_actions),
				      source);
	gtk_ui_manager_insert_action_group (uimanager, 
					    source->priv->action_group, 0);
	g_object_unref (G_OBJECT (uimanager));

}

static void
rb_podcast_source_cmd_download_post (GtkAction *action,
				     RBPodcastSource *source)
{
	GList *lst;
	gulong status;
	GValue val = {0, };
	RBEntryView *posts;
	
	rb_debug ("Add to download action");
	posts = source->priv->posts;
	
	lst = rb_entry_view_get_selected_entries (posts);
	g_value_init (&val, G_TYPE_ULONG);

	while (lst != NULL) {
		RhythmDBEntry *entry  = (RhythmDBEntry *) lst->data;
		status  = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);
		if ( (status < 100) || (status == 102)) {
			//cancel download
			g_value_set_ulong (&val, 103);
			rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_STATUS, &val);
			rb_podcast_manager_cancel_download (source->priv->podcast_mg, entry);
		}
		else {
			//add to downlaod
			g_value_set_ulong (&val, 102);
			rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_STATUS, &val);
			rb_podcast_manager_download_entry (source->priv->podcast_mg, entry);
		}
			
		lst = lst->next;
	}
	g_value_unset (&val);
	rhythmdb_commit (source->priv->db);
}

static void
rb_podcast_source_cmd_delete_feed (GtkAction *action,
			     	   RBPodcastSource *source)
{
	GList *lst;
	gint ret;
	GtkWidget *dialog;
	GtkWidget *button;
	GtkWindow *window;
	RBShell *shell;

	rb_debug ("Delete feed action");

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "window", &window, NULL);
	g_object_unref (G_OBJECT (shell));

	dialog = gtk_message_dialog_new (window,
			                 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_NONE,
					 _("Delete the podcast feed and downloaded files?"));

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
	                                          _("If you choose to delete the feed and files, "
						    "they will be permanently lost.  Please note that "
						    "you can delete the feed but keep the downloaded "
						    "files by choosing to delete the feed only."));
	
	gtk_window_set_title (GTK_WINDOW (dialog), "");

	gtk_dialog_add_buttons (GTK_DIALOG (dialog), 
	                        _("Delete _Feed Only"), 
	                        GTK_RESPONSE_NO,
	                        GTK_STOCK_CANCEL, 
	                        GTK_RESPONSE_CANCEL, 
	                        NULL);

	button = gtk_dialog_add_button (GTK_DIALOG (dialog),
	                                _("_Delete Feed And Files"),
			                GTK_RESPONSE_YES);
	
	gtk_window_set_focus (GTK_WINDOW (dialog), button);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);
	
	ret = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	if (ret == GTK_RESPONSE_CANCEL || ret == GTK_RESPONSE_DELETE_EVENT)
		return;
	
	lst = source->priv->selected_feeds;

	while (lst != NULL) {
		g_return_if_fail (lst->data != NULL);
		rb_podcast_manager_remove_feed (source->priv->podcast_mg,
						(gchar *) lst->data,
						(ret == GTK_RESPONSE_YES) );

		lst = lst->next;
	}
}

static void
rb_podcast_source_cmd_properties_feed (GtkAction *action,
			     	       RBPodcastSource *source)
{
	RhythmDBEntry *entry;
	
	entry = rhythmdb_entry_lookup_by_location (source->priv->db, 
						   (gchar *) source->priv->selected_feeds->data );
	
	GtkWidget *dialog = rb_feed_podcast_properties_dialog_new (entry);
	rb_debug ("in feed properties");
	
	if (dialog)
		gtk_widget_show_all (dialog);
	else
		rb_debug ("no selection!");

}
	
static void
rb_podcast_source_cmd_update_feed (GtkAction *action,
			     	   RBPodcastSource *source)
{
	GList *lst;
	
	rb_debug ("Update action");
	lst = source->priv->selected_feeds;

	while (lst != NULL) {
		rb_podcast_manager_subscribe_feed (source->priv->podcast_mg,
						   (gchar *) lst->data);

		lst = lst->next;
	}
}

static gboolean
rb_podcast_source_update_feed_func (GtkTreeModel *model,
				    GtkTreePath *path,
				    GtkTreeIter *iter,
				    RBPodcastSource *source)
{
	RhythmDBEntry *entry;
	const char *uri;

	gtk_tree_model_get (model, iter, 0, &entry, -1);
	uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	
	rb_podcast_manager_subscribe_feed (source->priv->podcast_mg, uri);

	return FALSE;
}

static void
rb_podcast_source_cmd_update_all (GtkAction *action, RBPodcastSource *source)
{
	RhythmDBPropertyModel *feed_model;
	RhythmDBQueryModel *query_model;

	feed_model = rb_property_view_get_model (RB_PROPERTY_VIEW (source->priv->feeds));
	g_object_get (G_OBJECT (feed_model), "query-model", &query_model, NULL);

	gtk_tree_model_foreach (GTK_TREE_MODEL (query_model),
				(GtkTreeModelForeachFunc) rb_podcast_source_update_feed_func,
				source);
}

static void
rb_podcast_source_post_status_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				     	      GtkTreeModel *tree_model, GtkTreeIter *iter,
				     	      RBPodcastSource *source)

{
	RhythmDBEntry *entry;
	guint value;

	gtk_tree_model_get (tree_model, iter, 0, &entry, -1);

	switch (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS))
	{
	case 100:
		g_object_set (G_OBJECT (renderer), "text", _("Completed"), NULL);
		value = 100;  
		break;
	case 101:
		g_object_set (G_OBJECT (renderer), "text", _("Failed"), NULL);
		value = 0;
		break;
	case 102:
		g_object_set (G_OBJECT (renderer), "text", _("Waiting"), NULL);
		value = 0;
		break;
	case 103:
		g_object_set (G_OBJECT (renderer), "text", _("Paused"), NULL);
		value = 0;
		break;
	default:
		{
			char *s = g_strdup_printf ("%u %%", (guint)entry->podcast->status);
			
			g_object_set (G_OBJECT (renderer), "text", s, NULL);
			value = entry->podcast->status;
			g_free (s);
		}
	}
		
	g_object_set (G_OBJECT (renderer), "value", value, NULL);
	


}

static void
rb_podcast_source_post_feed_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				       	    GtkTreeModel *tree_model, GtkTreeIter *iter,
				       	    RBPodcastSource *source)

{
	RhythmDBEntry *entry;
	const gchar *album;

	gtk_tree_model_get (tree_model, iter, 0, &entry, -1);
	album = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
		
	g_object_set (G_OBJECT (renderer), "text", album, NULL);
}


static void
rb_podcast_source_feed_status_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				     	      GtkTreeModel *tree_model, GtkTreeIter *iter,
				     	      RBPodcastSource *source)
{
	GdkPixbuf *pixbuf;
	gchar *location;
	RhythmDBEntry *entry = NULL;

	gtk_tree_model_get (tree_model, iter, 0, &location, -1);
	
	entry = rhythmdb_entry_lookup_by_location (source->priv->db, location);
	g_free (location);	

	if (entry == NULL) { 
		g_object_set (G_OBJECT (renderer), "pixbuf", NULL, NULL);
		return;
	}
	
	
	switch (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS))
	{
	case 0:
		pixbuf = source->priv->feed_unsubscribe_pixbuf;
		break;
	case 1:
		pixbuf = source->priv->feed_subscribe_pixbuf;
		break;
	default:		
		pixbuf = source->priv->feed_unsubscribe_pixbuf;
	}
	
	g_object_set (G_OBJECT (renderer), "pixbuf", pixbuf, NULL);
	
}



static void
rb_podcast_source_feed_title_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				     	      GtkTreeModel *tree_model, GtkTreeIter *iter,
				     	      RBPodcastSource *source)
{
	RhythmDBEntry *entry = NULL;
	gchar *str;
	gboolean bold;

	gtk_tree_model_get (tree_model, iter, 0, &str, 1, &bold, -1);
	
	entry = rhythmdb_entry_lookup_by_location (source->priv->db, str);

	if (entry != NULL) {
		str = g_strdup (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE));
	}

	g_object_set (G_OBJECT (renderer), "text", str,
		      "weight", G_UNLIKELY (bold) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
		      NULL);

	g_free (str);	

}


static void
rb_podcast_source_post_date_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				     	    GtkTreeModel *tree_model, GtkTreeIter *iter,
				     	    RBPodcastSource *source)
{
	RhythmDBEntry *entry;
	gulong value;
	char str[255];

	gtk_tree_model_get (tree_model, iter, 0, &entry, -1);

      	value = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_POST_TIME);
        if (value == 0)
	       g_strlcpy (str, _("Unknown"), sizeof (str));
	else {
		struct tm *time_tm;
		gint ret;
		time_t time = (time_t) value;
		time_tm = localtime(&time);
		ret = strftime (str, sizeof (str), "%x %H:%M", time_tm);
	}
	
        g_object_set (G_OBJECT (renderer), "text", str, NULL);
}




static void
rb_podcast_source_cb_interval_changed_cb (GtkComboBox *box, gpointer cb_data)
{
	guint index = gtk_combo_box_get_active (box);
	eel_gconf_set_integer (CONF_STATE_PODCAST_DOWNLOAD_INTERVAL,
			       index);

	rb_podcast_manager_start_sync (RB_PODCAST_SOURCE (cb_data)->priv->podcast_mg);
}

static gboolean 
rb_podcast_source_load_finish_cb  (gpointer cb_data)
{
	RBPodcastSource *source  = RB_PODCAST_SOURCE (cb_data);
	
	rb_podcast_manager_start_sync (source->priv->podcast_mg);
	
	g_signal_connect_after (G_OBJECT (source->priv->podcast_mg),
	 		        "start_download",
			  	G_CALLBACK (rb_podcast_source_start_download_cb),
			  	source);

	g_signal_connect_after (G_OBJECT (source->priv->podcast_mg),
			  	"finish_download",
			  	G_CALLBACK (rb_podcast_source_finish_download_cb),
			  	source);

	g_signal_connect_after (G_OBJECT (source->priv->podcast_mg),
			  	"feed_updates_avaliable",
 			  	G_CALLBACK (rb_podcast_source_feed_updates_avaliable_cb),
			  	source);

	return FALSE;
}

static gboolean
impl_receive_drag (RBSource *asource, GtkSelectionData *data)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);
	GList *list, *uri_list, *i;

	rb_debug ("parsing uri list");
	list = gnome_vfs_uri_list_parse ((char *) data->data);

	if (list == NULL)
		return FALSE;

	uri_list = NULL;

	for (i = list; i != NULL; i = g_list_next (i))
		uri_list = g_list_append (uri_list, gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0));

	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL)
		return FALSE;
	
	rb_debug ("adding uris");

	for (i = uri_list; i != NULL; i = i->next) {
		char *uri = NULL;

		uri = i->data;

		if ((uri != NULL) && 
		    (!rhythmdb_entry_lookup_by_location (source->priv->db, uri))) {
			rb_podcast_source_add_feed (source, uri);
		}

		g_free (uri);
	}

	g_list_free (uri_list);
	return TRUE;
}


static void
rb_podcast_source_start_download_cb (RBPodcastManager *pd, RhythmDBEntry *entry, RBPodcastSource *source)
{
	RBShell *shell = rb_podcast_source_get_shell (source);
	const gchar *podcast_name = rhythmdb_entry_get_string(entry, RHYTHMDB_PROP_TITLE);
	rb_debug ("Start download");
	rb_shell_hidden_notify (shell, 4000, _("Downloading podcast"), NULL, podcast_name);
	g_object_unref (G_OBJECT (shell));
}

static void
rb_podcast_source_finish_download_cb (RBPodcastManager *pd, RhythmDBEntry *entry, RBPodcastSource *source)
{
	RBShell *shell = rb_podcast_source_get_shell (source);
	const gchar *podcast_name = rhythmdb_entry_get_string(entry, RHYTHMDB_PROP_TITLE);
	rb_debug ("Finish download");
	rb_shell_hidden_notify (shell, 4000, _("Finished downloading podcast"), NULL, podcast_name);
	g_object_unref (G_OBJECT (shell));
}

static void
rb_podcast_source_feed_updates_avaliable_cb (RBPodcastManager *pd, RhythmDBEntry *entry, RBPodcastSource *source)
{
	RBShell *shell = rb_podcast_source_get_shell (source);
	const gchar *podcast_name = rhythmdb_entry_get_string(entry, RHYTHMDB_PROP_TITLE);
	rb_debug ("Updates avaliable");
	rb_shell_hidden_notify (shell, 4000, _("New updates avaliable from"), NULL, podcast_name);
	g_object_unref (G_OBJECT (shell));

}

static RBShell*
rb_podcast_source_get_shell (RBPodcastSource *source)
{
	RBShell *shell;
	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	return shell;
}

void 
rb_podcast_source_shutdown	(RBPodcastSource *source)
{
	rb_debug ("podcast source shutdown");
	rb_podcast_manager_cancel_all (source->priv->podcast_mg);
}

static gint
rb_podcast_source_post_date_cell_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
	                                    RBPodcastSource *source)
{
	gulong a_val, b_val;
	gint ret;

	a_val = rhythmdb_entry_get_ulong (a, RHYTHMDB_PROP_POST_TIME);
	b_val = rhythmdb_entry_get_ulong (b, RHYTHMDB_PROP_POST_TIME);

	ret = (a_val == b_val ? 0 : (a_val > b_val ? 1 : -1));
        return ret;
}

static gint
rb_podcast_source_post_status_cell_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
	                                      RBPodcastSource *source)
{
	gulong a_val, b_val;
	gint ret;

	a_val = rhythmdb_entry_get_ulong (a, RHYTHMDB_PROP_STATUS);
	b_val = rhythmdb_entry_get_ulong (b, RHYTHMDB_PROP_STATUS);

	ret = (a_val == b_val ? 0 : (a_val > b_val ? 1 : -1));
        return ret;
}

static gint
rb_podcast_source_post_feed_cell_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
	                                    RBPodcastSource *source)
{
	const char *a_val;
        const char *b_val;

	a_val = rhythmdb_entry_get_string (a, RHYTHMDB_PROP_ALBUM);
	b_val = rhythmdb_entry_get_string (b, RHYTHMDB_PROP_ALBUM);
	
	return strcmp (a_val, b_val);

}

static void
rb_podcast_source_download_process_error_cb (RBPodcastManager *pd,
					     const char *error,
					     RBPodcastSource *source)
{
	rb_error_dialog (NULL, _("Error in podcast"), "%s", error);
}

static void rb_podcast_source_entry_activated_cb (RBEntryView *view,
						  RhythmDBEntry *entry,
						  RBPodcastSource *source)
{
	GValue val = {0,};
	
	/* check to see if it has already been downloaded */
	if (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT) != NULL)
		return;
	
	g_value_init (&val, G_TYPE_ULONG);
	g_value_set_ulong (&val, 102);
	rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_STATUS, &val);
	rhythmdb_commit (source->priv->db);
	g_value_unset (&val);

	rb_podcast_manager_download_entry (source->priv->podcast_mg, entry);
}

