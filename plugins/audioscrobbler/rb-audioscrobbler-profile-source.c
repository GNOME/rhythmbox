/*
 * rb-audioscrobbler-profile-source.c
 *
 * Copyright (C) 2010 Jamie Nicol <jamie@thenicols.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <math.h>

#include "eel-gconf-extensions.h"

#include "rb-audioscrobbler-profile-source.h"
#include "rb-audioscrobbler.h"
#include "rb-audioscrobbler-account.h"
#include "rb-audioscrobbler-user.h"
#include "rb-debug.h"
#include "rb-builder-helpers.h"
#include "rb-file-helpers.h"
#include "rb-preferences.h"
#include "rb-util.h"

#define CONF_AUDIOSCROBBLER_ENABLE_SCROBBLING CONF_PLUGINS_PREFIX "/audioscrobbler/%s/scrobbling_enabled"
#define LIST_ITEM_IMAGE_SIZE 34

struct _RBAudioscrobblerProfileSourcePrivate {
	RBAudioscrobblerService *service;
	RBAudioscrobblerAccount *account;
	RBAudioscrobbler *audioscrobbler;

	RBAudioscrobblerUser *user;

	guint scrobbling_enabled_notification_id;

	GtkWidget *main_vbox;

	GtkWidget *login_bar;
	GtkWidget *login_status_label;
	GtkWidget *login_response_button;

	GtkWidget *profile_vbox;

	GtkWidget *user_info_area;
	GtkWidget *profile_image;
	GtkWidget *username_label;
	GtkWidget *playcount_label;
	GtkWidget *scrobbling_enabled_check;
	GtkWidget *view_profile_link;

	GtkWidget *scrobbler_status_msg_label;
	GtkWidget *scrobbler_queue_count_label;
	GtkWidget *scrobbler_submit_count_label;
	GtkWidget *scrobbler_submit_time_label;

	GtkWidget *recent_tracks_area;
	GtkWidget *recent_tracks_table;
	GtkWidget *top_tracks_area;
	GtkWidget *top_tracks_table;
	GtkWidget *loved_tracks_area;
	GtkWidget *loved_tracks_table;
	GtkWidget *top_artists_area;
	GtkWidget *top_artists_table;
	GtkWidget *recommended_artists_area;
	GtkWidget *recommended_artists_table;
};

#define RB_AUDIOSCROBBLER_PROFILE_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_AUDIOSCROBBLER_PROFILE_SOURCE, RBAudioscrobblerProfileSourcePrivate))

static void rb_audioscrobbler_profile_source_class_init (RBAudioscrobblerProfileSourceClass *klass);
static void rb_audioscrobbler_profile_source_init (RBAudioscrobblerProfileSource *source);
static void rb_audioscrobbler_profile_source_constructed (GObject *object);
static void rb_audioscrobbler_profile_source_dispose (GObject* object);
static void rb_audioscrobbler_profile_source_finalize (GObject *object);
static void rb_audioscrobbler_profile_source_get_property (GObject *object,
                                                           guint prop_id,
                                                           GValue *value,
                                                           GParamSpec *pspec);
static void rb_audioscrobbler_profile_source_set_property (GObject *object,
                                                           guint prop_id,
                                                           const GValue *value,
                                                           GParamSpec *pspec);

static void rb_audioscrobbler_profile_source_init_login_ui (RBAudioscrobblerProfileSource *source);
static void rb_audioscrobbler_profile_source_init_profile_ui (RBAudioscrobblerProfileSource *source);

static void rb_audioscrobbler_profile_source_login_bar_response (GtkInfoBar *info_bar,
                                                                 gint response_id,
                                                                 gpointer user_data);
void rb_audioscrobbler_profile_source_logout_button_clicked_cb (GtkButton *button,
                                                                gpointer user_data);
void rb_audioscrobbler_profile_source_scrobbling_enabled_check_toggled_cb (GtkToggleButton *togglebutton,
                                                                           gpointer user_data);
static void rb_audioscrobbler_profile_source_scrobbling_enabled_changed_cb (GConfClient *client,
                                                                            guint cnxn_id,
                                                                            GConfEntry *entry,
                                                                            RBAudioscrobblerProfileSource *source);

static void rb_audioscrobbler_profile_source_login_status_change_cb (RBAudioscrobblerAccount *account,
                                                                     RBAudioscrobblerAccountLoginStatus status,
                                                                     gpointer user_data);

static void rb_audioscrobbler_profile_source_scrobbler_authentication_error_cb (RBAudioscrobbler *audioscrobbler,
                                                                                gpointer user_data);
static void rb_audioscrobbler_profile_source_scrobbler_statistics_changed_cb (RBAudioscrobbler *audioscrobbler,
                                                                              const char *status_msg,
                                                                              guint queue_count,
                                                                              guint submit_count,
                                                                              const char *submit_time,
                                                                              gpointer user_data);

static void rb_audioscrobbler_profile_source_user_info_updated_cb (RBAudioscrobblerUser *user,
                                                                   RBAudioscrobblerUserData *info,
                                                                   gpointer user_data);
static void rb_audioscrobbler_profile_source_set_user_list (RBAudioscrobblerProfileSource *source,
                                                            GtkWidget *list_table,
                                                            GPtrArray *list_data);
static void rb_audioscrobbler_profile_source_recent_tracks_updated_cb (RBAudioscrobblerUser *user,
                                                                       GPtrArray *recent_tracks,
                                                                       gpointer user_data);
static void rb_audioscrobbler_profile_source_top_tracks_updated_cb (RBAudioscrobblerUser *user,
                                                                    GPtrArray *top_tracks,
                                                                    gpointer user_data);
static void rb_audioscrobbler_profile_source_loved_tracks_updated_cb (RBAudioscrobblerUser *user,
                                                                      GPtrArray *loved_tracks,
                                                                      gpointer user_data);
static void rb_audioscrobbler_profile_source_top_artists_updated_cb (RBAudioscrobblerUser *user,
                                                                     GPtrArray *top_artists,
                                                                     gpointer user_data);
static void rb_audioscrobbler_profile_source_recommended_artists_updated_cb (RBAudioscrobblerUser *user,
                                                                             GPtrArray *recommended_artists,
                                                                             gpointer user_data);
static void rb_audioscrobbler_profile_source_list_table_pack_start (GtkTable *list_table, GtkWidget *item);
void rb_audioscrobbler_profile_source_list_layout_size_allocate_cb (GtkWidget *layout,
                                                                    GtkAllocation *allocation,
                                                                    gpointer user_data);


enum {
	PROP_0,
	PROP_SERVICE
};

G_DEFINE_TYPE (RBAudioscrobblerProfileSource, rb_audioscrobbler_profile_source, RB_TYPE_SOURCE)

RBSource *
rb_audioscrobbler_profile_source_new (RBShell *shell, RBPlugin *plugin, RBAudioscrobblerService *service)
{
	RBSource *source;
	RhythmDB *db;
	char *name;
	RhythmDBEntryType entry_type;
	gchar *icon_filename;
	gint icon_size;
	GdkPixbuf *icon_pixbuf;

	g_object_get (shell, "db", &db, NULL);
	g_object_get (service, "name", &name, NULL);

	entry_type = rhythmdb_entry_type_get_by_name (db, "audioscrobbler-radio-track");
	if (entry_type == RHYTHMDB_ENTRY_TYPE_INVALID) {
		entry_type = rhythmdb_entry_register_type (db, "audioscrobbler-radio-track");
		entry_type->save_to_disk = FALSE;
		entry_type->category = RHYTHMDB_ENTRY_NORMAL;
	}

	icon_filename = rb_plugin_find_file (plugin, "as-icon.png");
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &icon_size, NULL);
	icon_pixbuf = gdk_pixbuf_new_from_file_at_size (icon_filename, icon_size, icon_size, NULL);

	source = RB_SOURCE (g_object_new (RB_TYPE_AUDIOSCROBBLER_PROFILE_SOURCE,
	                                  "shell", shell,
	                                  "plugin", plugin,
	                                  "name", name,
	                                  "source-group", RB_SOURCE_GROUP_LIBRARY,
	                                  "entry-type", entry_type,
	                                  "icon", icon_pixbuf,
	                                  "service", service,
	                                  NULL));

	rb_shell_register_entry_type_for_source (shell, source, entry_type);

	g_object_unref (db);
	g_free (name);
	g_free (icon_filename);
	g_object_unref (icon_pixbuf);

	return source;
}

static void
rb_audioscrobbler_profile_source_class_init (RBAudioscrobblerProfileSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = rb_audioscrobbler_profile_source_constructed;
	object_class->dispose = rb_audioscrobbler_profile_source_dispose;
	object_class->finalize = rb_audioscrobbler_profile_source_finalize;
	object_class->get_property = rb_audioscrobbler_profile_source_get_property;
	object_class->set_property = rb_audioscrobbler_profile_source_set_property;

	g_object_class_install_property (object_class,
	                                 PROP_SERVICE,
	                                 g_param_spec_object ("service",
	                                                      "Service",
	                                                      "Audioscrobbler service that this is a source for",
	                                                      RB_TYPE_AUDIOSCROBBLER_SERVICE,
                                                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBAudioscrobblerProfileSourcePrivate));
}

static void
rb_audioscrobbler_profile_source_init (RBAudioscrobblerProfileSource *source)
{
	source->priv = RB_AUDIOSCROBBLER_PROFILE_SOURCE_GET_PRIVATE (source);
}

static void
rb_audioscrobbler_profile_source_constructed (GObject *object)
{
	RBAudioscrobblerProfileSource *source;
	RBShell *shell;
	char *scrobbling_enabled_conf_key;

	RB_CHAIN_GOBJECT_METHOD (rb_audioscrobbler_profile_source_parent_class, constructed, object);

	source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (object);
	g_object_get (source, "shell", &shell, NULL);

	/* create the UI */
	source->priv->main_vbox = gtk_vbox_new (FALSE, 4);
	gtk_box_pack_start (GTK_BOX (source), source->priv->main_vbox, TRUE, TRUE, 0);
	gtk_widget_show (source->priv->main_vbox);

	rb_audioscrobbler_profile_source_init_login_ui (source);
	rb_audioscrobbler_profile_source_init_profile_ui (source);

	/* create the user */
	source->priv->user = rb_audioscrobbler_user_new (source->priv->service);
	g_signal_connect (source->priv->user,
	                  "user-info-updated",
	                  (GCallback)rb_audioscrobbler_profile_source_user_info_updated_cb,
	                  source);
	g_signal_connect (source->priv->user,
	                  "recent-tracks-updated",
	                  (GCallback)rb_audioscrobbler_profile_source_recent_tracks_updated_cb,
	                  source);
	g_signal_connect (source->priv->user,
	                  "top-tracks-updated",
	                  (GCallback)rb_audioscrobbler_profile_source_top_tracks_updated_cb,
	                  source);
	g_signal_connect (source->priv->user,
	                  "loved-tracks-updated",
	                  (GCallback)rb_audioscrobbler_profile_source_loved_tracks_updated_cb,
	                  source);
	g_signal_connect (source->priv->user,
	                  "top-artists-updated",
	                  (GCallback)rb_audioscrobbler_profile_source_top_artists_updated_cb,
	                  source);
	g_signal_connect (source->priv->user,
	                  "recommended-artists-updated",
	                  (GCallback)rb_audioscrobbler_profile_source_recommended_artists_updated_cb,
	                  source);

	/* create the account */
	source->priv->account = rb_audioscrobbler_account_new (source->priv->service);
	g_signal_connect (source->priv->account,
	                  "login-status-changed",
	                  (GCallback)rb_audioscrobbler_profile_source_login_status_change_cb,
	                  source);
	rb_audioscrobbler_profile_source_login_status_change_cb (source->priv->account,
	                                                         rb_audioscrobbler_account_get_login_status (source->priv->account),
	                                                         source);

	/* create the scrobbler, if it is enabled */
	scrobbling_enabled_conf_key = g_strdup_printf (CONF_AUDIOSCROBBLER_ENABLE_SCROBBLING,
	                                               rb_audioscrobbler_service_get_name (source->priv->service));
	source->priv->scrobbling_enabled_notification_id =
		eel_gconf_notification_add (scrobbling_enabled_conf_key,
				            (GConfClientNotifyFunc) rb_audioscrobbler_profile_source_scrobbling_enabled_changed_cb,
				            source);
	if (eel_gconf_get_boolean (scrobbling_enabled_conf_key)) {
		source->priv->audioscrobbler =
			rb_audioscrobbler_new (source->priv->service,
				               RB_SHELL_PLAYER (rb_shell_get_player (shell)));
		rb_audioscrobbler_set_authentication_details (source->priv->audioscrobbler,
			                                      rb_audioscrobbler_account_get_username (source->priv->account),
			                                      rb_audioscrobbler_account_get_session_key (source->priv->account));
		g_signal_connect (source->priv->audioscrobbler,
			          "authentication-error",
			          (GCallback)rb_audioscrobbler_profile_source_scrobbler_authentication_error_cb,
			          source);
		g_signal_connect (source->priv->audioscrobbler,
			          "statistics-changed",
			          (GCallback)rb_audioscrobbler_profile_source_scrobbler_statistics_changed_cb,
			          source);
		rb_audioscrobbler_statistics_changed (source->priv->audioscrobbler);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (source->priv->scrobbling_enabled_check),
		                              TRUE);
	}

	g_object_unref (shell);
	g_free (scrobbling_enabled_conf_key);
}

static void
rb_audioscrobbler_profile_source_dispose (GObject* object)
{
	RBAudioscrobblerProfileSource *source;

	source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (object);

	if (source->priv->service != NULL) {
		g_object_unref (source->priv->service);
		source->priv->service = NULL;
	}

	if (source->priv->audioscrobbler != NULL) {
		g_object_unref (source->priv->audioscrobbler);
		source->priv->audioscrobbler = NULL;
	}

	if (source->priv->account != NULL) {
		g_object_unref (source->priv->account);
		source->priv->account = NULL;
	}

	if (source->priv->user != NULL) {
		g_object_unref (source->priv->user);
		source->priv->user = NULL;
	}

	if (source->priv->scrobbling_enabled_notification_id != 0) {
		eel_gconf_notification_remove (source->priv->scrobbling_enabled_notification_id);
		source->priv->scrobbling_enabled_notification_id = 0;
	}

	G_OBJECT_CLASS (rb_audioscrobbler_profile_source_parent_class)->dispose (object);
}

static void
rb_audioscrobbler_profile_source_finalize (GObject *object)
{
	RBAudioscrobblerProfileSource *source;
	source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (object);

	G_OBJECT_CLASS (rb_audioscrobbler_profile_source_parent_class)->finalize (object);
}

static void
rb_audioscrobbler_profile_source_get_property (GObject *object,
                                               guint prop_id,
                                               GValue *value,
                                               GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_audioscrobbler_profile_source_set_property (GObject *object,
                                               guint prop_id,
                                               const GValue *value,
                                               GParamSpec *pspec)
{
	RBAudioscrobblerProfileSource *source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (object);
	switch (prop_id) {
	case PROP_SERVICE:
		source->priv->service = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_audioscrobbler_profile_source_init_login_ui (RBAudioscrobblerProfileSource *source)
{
	GtkWidget *content_area;

	source->priv->login_bar = gtk_info_bar_new ();
	source->priv->login_status_label = gtk_label_new ("");
	source->priv->login_response_button = gtk_button_new ();
	content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (source->priv->login_bar));
	gtk_container_add (GTK_CONTAINER (content_area), source->priv->login_status_label);
	source->priv->login_response_button =
		gtk_info_bar_add_button (GTK_INFO_BAR (source->priv->login_bar),
		                         "", GTK_RESPONSE_OK);
	g_signal_connect (source->priv->login_bar,
	                  "response",
	                  G_CALLBACK (rb_audioscrobbler_profile_source_login_bar_response),
	                  source);
	gtk_box_pack_start (GTK_BOX (source->priv->main_vbox), source->priv->login_bar, FALSE, FALSE, 0);
}

static void
rb_audioscrobbler_profile_source_init_profile_ui (RBAudioscrobblerProfileSource *source)
{
	RBPlugin *plugin;
	char *builder_file;
	GtkBuilder *builder;
	GtkWidget *viewport;
	GtkWidget *scrolled_win;

	g_object_get (source, "plugin", &plugin, NULL);

	builder_file = rb_plugin_find_file (plugin, "audioscrobbler-profile.ui");
	g_assert (builder_file != NULL);
	builder = rb_builder_load (builder_file, source);

	source->priv->profile_vbox = GTK_WIDGET (gtk_builder_get_object (builder, "profile_vbox"));

	source->priv->user_info_area = GTK_WIDGET (gtk_builder_get_object (builder, "user_info_area"));
	source->priv->profile_image = GTK_WIDGET (gtk_builder_get_object (builder, "profile_image"));
	source->priv->username_label = GTK_WIDGET (gtk_builder_get_object (builder, "username_label"));
	source->priv->playcount_label = GTK_WIDGET (gtk_builder_get_object (builder, "playcount_label"));
	source->priv->scrobbling_enabled_check = GTK_WIDGET (gtk_builder_get_object (builder, "scrobbling_enabled_check"));
	source->priv->view_profile_link = GTK_WIDGET (gtk_builder_get_object (builder, "view_profile_link"));

	/* scrobbler statistics */
	source->priv->scrobbler_status_msg_label = GTK_WIDGET (gtk_builder_get_object (builder, "scrobbler_status_msg_label"));
	source->priv->scrobbler_queue_count_label = GTK_WIDGET (gtk_builder_get_object (builder, "scrobbler_queue_count_label"));
	source->priv->scrobbler_submit_count_label = GTK_WIDGET (gtk_builder_get_object (builder, "scrobbler_submit_count_label"));
	source->priv->scrobbler_submit_time_label = GTK_WIDGET (gtk_builder_get_object (builder, "scrobbler_submit_time_label"));

	/* lists of data */
	source->priv->recent_tracks_area = GTK_WIDGET (gtk_builder_get_object (builder, "recent_tracks_area"));
	source->priv->recent_tracks_table = GTK_WIDGET (gtk_builder_get_object (builder, "recent_tracks_table"));

	source->priv->top_tracks_area = GTK_WIDGET (gtk_builder_get_object (builder, "top_tracks_area"));
	source->priv->top_tracks_table = GTK_WIDGET (gtk_builder_get_object (builder, "top_tracks_table"));

	source->priv->loved_tracks_area = GTK_WIDGET (gtk_builder_get_object (builder, "loved_tracks_area"));
	source->priv->loved_tracks_table = GTK_WIDGET (gtk_builder_get_object (builder, "loved_tracks_table"));

	source->priv->top_artists_area = GTK_WIDGET (gtk_builder_get_object (builder, "top_artists_area"));
	source->priv->top_artists_table = GTK_WIDGET (gtk_builder_get_object (builder, "top_artists_table"));

	source->priv->recommended_artists_area = GTK_WIDGET (gtk_builder_get_object (builder, "recommended_artists_area"));
	source->priv->recommended_artists_table = GTK_WIDGET (gtk_builder_get_object (builder, "recommended_artists_table"));


	viewport = gtk_viewport_new (NULL, NULL);
	gtk_viewport_set_shadow_type (GTK_VIEWPORT (viewport),
	                              GTK_SHADOW_NONE);
	scrolled_win = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_win),
	                                     GTK_SHADOW_NONE);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_win),
	                                GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (scrolled_win),
	                   viewport);
	gtk_container_add (GTK_CONTAINER (viewport),
	                   source->priv->profile_vbox);
	gtk_widget_show_all (scrolled_win);
	gtk_box_pack_start (GTK_BOX (source->priv->main_vbox), scrolled_win, TRUE, TRUE, 0);


	g_object_unref (plugin);
	g_free (builder_file);
	g_object_unref (builder);
}

static void
rb_audioscrobbler_profile_source_login_bar_response (GtkInfoBar *info_bar,
                                                     gint response_id,
                                                     gpointer user_data)
{
	RBAudioscrobblerProfileSource *source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (user_data);

	switch (rb_audioscrobbler_account_get_login_status (source->priv->account)) {
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT:
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_AUTH_ERROR:
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_CONNECTION_ERROR:
		rb_audioscrobbler_account_authenticate (source->priv->account);
		break;
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGING_IN:
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_IN:
		rb_audioscrobbler_account_logout (source->priv->account);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

void
rb_audioscrobbler_profile_source_logout_button_clicked_cb (GtkButton *button,
                                                           gpointer user_data)
{
	RBAudioscrobblerProfileSource *source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (user_data);

	rb_audioscrobbler_account_logout (source->priv->account);
}

void
rb_audioscrobbler_profile_source_scrobbling_enabled_check_toggled_cb (GtkToggleButton *togglebutton,
                                                                      gpointer user_data)
{
	RBAudioscrobblerProfileSource *source;
	char *conf_key;

	source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (user_data);
	conf_key = g_strdup_printf (CONF_AUDIOSCROBBLER_ENABLE_SCROBBLING,
	                            rb_audioscrobbler_service_get_name (source->priv->service));
	eel_gconf_set_boolean (conf_key,
			       gtk_toggle_button_get_active (togglebutton));
	g_free (conf_key);
}

static void
rb_audioscrobbler_profile_source_scrobbling_enabled_changed_cb (GConfClient *client,
                                                                guint cnxn_id,
                                                                GConfEntry *entry,
                                                                RBAudioscrobblerProfileSource *source)
{
	gboolean enabled = gconf_value_get_bool (entry->value);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (source->priv->scrobbling_enabled_check),
	                              enabled);

	if (source->priv->audioscrobbler != NULL && enabled == FALSE) {
		g_object_unref (source->priv->audioscrobbler);
		source->priv->audioscrobbler = NULL;
		gtk_label_set_label (GTK_LABEL (source->priv->scrobbler_status_msg_label),
		                     _("Disabled"));
	} else if (source->priv->audioscrobbler == NULL && enabled == TRUE) {
		RBShell *shell;
		g_object_get (source, "shell", &shell, NULL);
		source->priv->audioscrobbler =
			rb_audioscrobbler_new (source->priv->service,
				               RB_SHELL_PLAYER (rb_shell_get_player (shell)));
		rb_audioscrobbler_set_authentication_details (source->priv->audioscrobbler,
			                                      rb_audioscrobbler_account_get_username (source->priv->account),
			                                      rb_audioscrobbler_account_get_session_key (source->priv->account));
		g_signal_connect (source->priv->audioscrobbler,
			          "authentication-error",
			          (GCallback)rb_audioscrobbler_profile_source_scrobbler_authentication_error_cb,
			          source);
		g_signal_connect (source->priv->audioscrobbler,
			          "statistics-changed",
			          (GCallback)rb_audioscrobbler_profile_source_scrobbler_statistics_changed_cb,
			          source);
		rb_audioscrobbler_statistics_changed (source->priv->audioscrobbler);
		g_object_unref (shell);
	}
}

static void
rb_audioscrobbler_profile_source_login_status_change_cb (RBAudioscrobblerAccount *account,
                                                         RBAudioscrobblerAccountLoginStatus status,
                                                         gpointer user_data)
{
	RBAudioscrobblerProfileSource *source;
	const char *username;
	const char *session_key;
	char *label_text = NULL;
	char *button_text = NULL;
	gboolean show_login_bar;
	gboolean show_profile;

	source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (user_data);

	username = rb_audioscrobbler_account_get_username (source->priv->account);
	session_key = rb_audioscrobbler_account_get_session_key (source->priv->account);

	/* update the audioscrobbler with new authentication */
	if (source->priv->audioscrobbler != NULL) {
		rb_audioscrobbler_set_authentication_details (source->priv->audioscrobbler,
			                                      username,
			                                      session_key);
	}

	/* set the new user details */
	rb_audioscrobbler_user_set_authentication_details (source->priv->user, username, session_key);
	if (username != NULL) {
		rb_audioscrobbler_user_update (source->priv->user);
	}

	/* update the login ui */
	switch (status) {
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT:
		show_login_bar = TRUE;
		show_profile = FALSE;
		label_text = g_strdup (_("You are not currently logged in."));
		button_text = g_strdup (_("Login"));
		gtk_info_bar_set_message_type (GTK_INFO_BAR (source->priv->login_bar), GTK_MESSAGE_INFO);
		break;
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGING_IN:
		show_login_bar = TRUE;
		show_profile = FALSE;
		label_text = g_strdup (_("Waiting for authentication..."));
		button_text = g_strdup (_("Cancel"));
		gtk_info_bar_set_message_type (GTK_INFO_BAR (source->priv->login_bar), GTK_MESSAGE_INFO);
		break;
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_IN:
		show_login_bar = FALSE;
		show_profile = TRUE;
		break;
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_AUTH_ERROR:
		show_login_bar = TRUE;
		show_profile = FALSE;
		label_text = g_strdup (_("Authentication error. Please try logging in again."));
		button_text = g_strdup (_("Login"));
		gtk_info_bar_set_message_type (GTK_INFO_BAR (source->priv->login_bar), GTK_MESSAGE_WARNING);
		break;
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_CONNECTION_ERROR:
		show_login_bar = TRUE;
		show_profile = FALSE;
		label_text = g_strdup (_("Connection error. Please try logging in again."));
		button_text = g_strdup (_("Login"));
		gtk_info_bar_set_message_type (GTK_INFO_BAR (source->priv->login_bar), GTK_MESSAGE_WARNING);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	gtk_label_set_label (GTK_LABEL (source->priv->login_status_label), label_text);
	gtk_button_set_label (GTK_BUTTON (source->priv->login_response_button), button_text);
	if (show_login_bar == TRUE) {
		gtk_widget_show_all (source->priv->login_bar);
	} else {
		gtk_widget_hide_all (source->priv->login_bar);
	}
	if (show_profile == TRUE) {
		gtk_widget_show (source->priv->profile_vbox);
	} else {
		gtk_widget_hide_all (source->priv->profile_vbox);
	}

	g_free (label_text);
	g_free (button_text);
}

static void
rb_audioscrobbler_profile_source_scrobbler_authentication_error_cb (RBAudioscrobbler *audioscrobbler,
                                                                    gpointer user_data)
{
	RBAudioscrobblerProfileSource *source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (user_data);

	rb_audioscrobbler_account_notify_of_auth_error (source->priv->account);
}

static void
rb_audioscrobbler_profile_source_scrobbler_statistics_changed_cb (RBAudioscrobbler *audioscrobbler,
                                                                  const char *status_msg,
                                                                  guint queue_count,
                                                                  guint submit_count,
                                                                  const char *submit_time,
                                                                  gpointer user_data)
{
	RBAudioscrobblerProfileSource *source;
	char *queue_count_text;
	char *submit_count_text;

	source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (user_data);

	gtk_label_set_text (GTK_LABEL (source->priv->scrobbler_status_msg_label), status_msg);

	queue_count_text = g_strdup_printf ("%u", queue_count);
	gtk_label_set_text (GTK_LABEL (source->priv->scrobbler_queue_count_label), queue_count_text);

	submit_count_text = g_strdup_printf ("%u", submit_count);
	gtk_label_set_text (GTK_LABEL (source->priv->scrobbler_submit_count_label), submit_count_text);

	gtk_label_set_text (GTK_LABEL (source->priv->scrobbler_submit_time_label), submit_time);

	g_free (queue_count_text);
	g_free (submit_count_text);
}

static void
rb_audioscrobbler_profile_source_set_user_list (RBAudioscrobblerProfileSource *source,
                                                GtkWidget *list_table,
                                                GPtrArray *list_data)
{
	int i;
	GList *button_node;

	/* delete all existing buttons */
	for (button_node = gtk_container_get_children (GTK_CONTAINER (list_table));
	     button_node != NULL;
	     button_node = g_list_next (button_node)) {
		gtk_widget_destroy (button_node->data);
	}

	/* add a new button for each item in the list */
	for (i = 0; i < list_data->len; i++) {
		GtkWidget *button;
		RBAudioscrobblerUserData *data;
		char *button_text;
		GtkWidget *label;
		GtkWidget *label_alignment;
		GtkWidget *button_contents;

		button = gtk_button_new ();
		gtk_button_set_alignment (GTK_BUTTON (button),
			                  0, 0.5);
		gtk_button_set_focus_on_click (GTK_BUTTON (button),
			                       FALSE);
		gtk_button_set_relief (GTK_BUTTON (button),
			               GTK_RELIEF_NONE);

		data = g_ptr_array_index (list_data, i);

		button_text = NULL;
		if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK) {
			char *escaped_title_text;
			char *escaped_artist_text;

			escaped_title_text = g_markup_escape_text (data->track.title, -1);
			escaped_artist_text = g_markup_escape_text (data->track.artist, -1);
			button_text = g_strdup_printf ("%s\n<small>%s</small>",
				                       escaped_title_text,
				                       escaped_artist_text);

			g_free (escaped_title_text);
			g_free (escaped_artist_text);

		} else if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST) {
			button_text = g_markup_escape_text (data->artist.name, -1);
		}

		label = gtk_label_new ("");
		gtk_label_set_markup (GTK_LABEL (label), button_text);
		g_free (button_text);

		label_alignment = gtk_alignment_new (0, 0.5, 0, 0);
		gtk_container_add (GTK_CONTAINER (label_alignment), label);

		button_contents = gtk_hbox_new (FALSE, 4);
		if (data->image != NULL) {
			GtkWidget *image;
			GtkWidget *viewport;
			GtkWidget *alignment;

			image = gtk_image_new_from_pixbuf (data->image);

			viewport = gtk_viewport_new (NULL, NULL);
			gtk_container_add (GTK_CONTAINER (viewport), image);

			alignment = gtk_alignment_new (0, 0.5, 0, 0);
			gtk_container_add (GTK_CONTAINER (alignment), viewport);

			gtk_box_pack_start (GTK_BOX (button_contents),
			                    alignment,
			                    FALSE, FALSE, 0);


			gtk_alignment_set_padding (GTK_ALIGNMENT (label_alignment),
			                           0, 0,
			                           LIST_ITEM_IMAGE_SIZE - gdk_pixbuf_get_width (data->image), 0);
		} else {
			gtk_alignment_set_padding (GTK_ALIGNMENT (label_alignment), 0, 0, LIST_ITEM_IMAGE_SIZE + 4, 0);
		}

		gtk_box_pack_start (GTK_BOX (button_contents),
		                    label_alignment,
		                    FALSE, FALSE, 0);
		gtk_container_add (GTK_CONTAINER (button), button_contents);

		rb_audioscrobbler_profile_source_list_table_pack_start (GTK_TABLE (list_table), button);
	}
}

static void
rb_audioscrobbler_profile_source_user_info_updated_cb (RBAudioscrobblerUser *user,
                                                       RBAudioscrobblerUserData *data,
                                                       gpointer user_data)
{
	RBAudioscrobblerProfileSource *source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (user_data);
	if (data != NULL) {
		char *playcount_text;

		gtk_label_set_label (GTK_LABEL (source->priv->username_label),
			             data->user_info.username);

		playcount_text = g_strdup_printf (_("%s plays"), data->user_info.playcount);
		gtk_label_set_label (GTK_LABEL (source->priv->playcount_label),
		                     playcount_text);

		gtk_link_button_set_uri (GTK_LINK_BUTTON (source->priv->view_profile_link),
		                         data->user_info.url);

		gtk_image_set_from_pixbuf (GTK_IMAGE (source->priv->profile_image), data->image);

		gtk_widget_show_all (source->priv->user_info_area);

		g_free (playcount_text);
	} else {
		gtk_widget_hide_all (source->priv->user_info_area);
	}
}

static void
rb_audioscrobbler_profile_source_recent_tracks_updated_cb (RBAudioscrobblerUser *user,
                                                           GPtrArray *recent_tracks,
                                                           gpointer user_data)
{
	RBAudioscrobblerProfileSource *source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (user_data);

	if (recent_tracks != NULL) {
		rb_audioscrobbler_profile_source_set_user_list (source, source->priv->recent_tracks_table, recent_tracks);
		gtk_widget_show_all (source->priv->recent_tracks_area);
	} else {
		gtk_widget_hide_all (source->priv->recent_tracks_area);
	}
}

static void
rb_audioscrobbler_profile_source_top_tracks_updated_cb (RBAudioscrobblerUser *user,
                                                        GPtrArray *top_tracks,
                                                        gpointer user_data)
{
	RBAudioscrobblerProfileSource *source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (user_data);

	if (top_tracks != NULL) {
		rb_audioscrobbler_profile_source_set_user_list (source, source->priv->top_tracks_table, top_tracks);
		gtk_widget_show_all (source->priv->top_tracks_area);
	} else {
		gtk_widget_hide_all (source->priv->top_tracks_area);
	}
}

static void
rb_audioscrobbler_profile_source_loved_tracks_updated_cb (RBAudioscrobblerUser *user,
                                                          GPtrArray *loved_tracks,
                                                          gpointer user_data)
{
	RBAudioscrobblerProfileSource *source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (user_data);

	if (loved_tracks != NULL) {
		rb_audioscrobbler_profile_source_set_user_list (source, source->priv->loved_tracks_table, loved_tracks);
		gtk_widget_show_all (source->priv->loved_tracks_area);
	} else {
		gtk_widget_hide_all (source->priv->loved_tracks_area);
	}
}

static void
rb_audioscrobbler_profile_source_top_artists_updated_cb (RBAudioscrobblerUser *user,
                                                         GPtrArray *top_artists,
                                                         gpointer user_data)
{
	RBAudioscrobblerProfileSource *source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (user_data);

	if (top_artists != NULL) {
		rb_audioscrobbler_profile_source_set_user_list (source, source->priv->top_artists_table, top_artists);
		gtk_widget_show_all (source->priv->top_artists_area);
	} else {
		gtk_widget_hide_all (source->priv->top_artists_area);
	}
}

static void
rb_audioscrobbler_profile_source_recommended_artists_updated_cb (RBAudioscrobblerUser *user,
                                                                 GPtrArray *recommended_artists,
                                                                 gpointer user_data)
{
	RBAudioscrobblerProfileSource *source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (user_data);

	if (recommended_artists != NULL) {
		rb_audioscrobbler_profile_source_set_user_list (source, source->priv->recommended_artists_table, recommended_artists);
		gtk_widget_show_all (source->priv->recommended_artists_area);
	} else {
		gtk_widget_hide_all (source->priv->recommended_artists_area);
	}
}

static void
rb_audioscrobbler_profile_source_list_table_pack_start (GtkTable *list_table, GtkWidget *item)
{
	int num_columns;
	int num_rows;
	int i;

	g_object_get (list_table, "n-columns", &num_columns, "n-rows", &num_rows, NULL);
	i = g_list_length (gtk_container_get_children (GTK_CONTAINER (list_table)));

	gtk_table_attach_defaults (list_table,
	                           item,
	                           i % num_columns, i % num_columns + 1,
	                           i / num_columns, i / num_columns + 1);
}

void
rb_audioscrobbler_profile_source_list_layout_size_allocate_cb (GtkWidget *layout,
                                                               GtkAllocation *allocation,
                                                               gpointer user_data)
{
	GtkWidget *table = gtk_container_get_children (GTK_CONTAINER (layout))->data;
	GList *buttons = gtk_container_get_children (GTK_CONTAINER (table));
	int num_buttons;
	GtkAllocation button_allocation;
	int current_num_columns;
	int new_num_columns;
	int spacing;
	GtkRequisition table_requisition;

	num_buttons = g_list_length (buttons);
	if (num_buttons == 0)
		return;

	gtk_widget_get_allocation (buttons->data, &button_allocation);

	g_object_get (table, "n-columns", &current_num_columns, NULL);
	spacing = gtk_table_get_default_col_spacing (GTK_TABLE (table));

	new_num_columns = allocation->width / (button_allocation.width + spacing);
	if (new_num_columns == 0) {
		new_num_columns = 1;
	}

	if (new_num_columns != current_num_columns) {
		int new_num_rows;
		GList *button;

		new_num_rows = (double)ceil ((double)num_buttons / (double)new_num_columns);

		/* remove each button from the table, reffing it first so that it is not destroyed */
		for (button = g_list_first (buttons); button != NULL; button = g_list_next (button)) {
			g_object_ref (button->data);
			gtk_container_remove (GTK_CONTAINER (table), button->data);
		}

		/* resize the table */
		rb_debug ("resizing table from %i to %ix%i", current_num_columns, new_num_columns, new_num_rows);
		gtk_table_resize (GTK_TABLE (table), new_num_columns, new_num_rows);

		/* don't know why, but g_table_resize doesn't always update these properties properly */
		g_object_set (table, "n-columns", new_num_columns, "n-rows", new_num_rows, NULL);

		/* re-attach each button to the table */
		for (button = g_list_last (buttons); button != NULL; button = g_list_previous (button)) {

			rb_audioscrobbler_profile_source_list_table_pack_start (GTK_TABLE (table),
			                                                        button->data);
			g_object_unref (button->data);
		}
	}

	gtk_widget_get_requisition (table, &table_requisition);
	gtk_widget_set_size_request (layout, 0, table_requisition.height);
	gtk_layout_set_size (GTK_LAYOUT (layout), table_requisition.width, table_requisition.height);
}
