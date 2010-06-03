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

#include "rb-audioscrobbler-profile-source.h"
#include "rb-audioscrobbler.h"
#include "rb-audioscrobbler-account.h"
#include "rb-debug.h"
#include "rb-util.h"

/* this API key belongs to Jamie Nicol <jamie@thenicols.net>
   generated May 2010 for use in the audioscrobbler plugin */
#define LASTFM_API_KEY "0337ff3c59299b6a31d75164041860b6"
#define LASTFM_API_SECRET "776c85a04a445efa8f9ed7705473c606"
#define LASTFM_API_URL "http://ws.audioscrobbler.com/2.0/"
#define LASTFM_AUTH_URL "http://www.last.fm/api/auth/"
#define LASTFM_SCROBBLER_URL "http://post.audioscrobbler.com/"

struct _RBAudioscrobblerProfileSourcePrivate {
	RBAudioscrobblerAccount *account;
	RBAudioscrobbler *audioscrobbler;

	GtkWidget *main_vbox;

	GtkWidget *login_bar;
	GtkWidget *login_status_label;
	GtkWidget *login_response_button;
};

#define RB_AUDIOSCROBBLER_PROFILE_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_AUDIOSCROBBLER_PROFILE_SOURCE, RBAudioscrobblerProfileSourcePrivate))

static void rb_audioscrobbler_profile_source_class_init (RBAudioscrobblerProfileSourceClass *klass);
static void rb_audioscrobbler_profile_source_init (RBAudioscrobblerProfileSource *source);
static void rb_audioscrobbler_profile_source_constructed (GObject *object);
static void rb_audioscrobbler_profile_source_dispose (GObject* object);
static void rb_audioscrobbler_profile_source_finalize (GObject *object);

static void rb_audioscrobbler_profile_source_init_login_ui (RBAudioscrobblerProfileSource *source);
static void rb_audioscrobbler_profile_source_login_bar_response (GtkInfoBar *info_bar,
                                                                 gint response_id,
                                                                 gpointer user_data);

static void rb_audioscrobbler_profile_source_login_status_change_cb (RBAudioscrobblerAccount *account,
                                                                     RBAudioscrobblerAccountLoginStatus status,
                                                                     gpointer user_data);

G_DEFINE_TYPE (RBAudioscrobblerProfileSource, rb_audioscrobbler_profile_source, RB_TYPE_SOURCE)

RBSource *
rb_audioscrobbler_profile_source_new (RBShell *shell, RBPlugin *plugin)
{
	RBSource *source;
	RhythmDB *db;
	RhythmDBEntryType entry_type;
	gchar *icon_filename;
	gint icon_size;
	GdkPixbuf *icon_pixbuf;

	g_object_get (shell, "db", &db, NULL);

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
	                                  "name", _("Last.fm"),
	                                  "source-group", RB_SOURCE_GROUP_LIBRARY,
	                                  "entry-type", entry_type,
	                                  "icon", icon_pixbuf,
	                                  NULL));

	rb_shell_register_entry_type_for_source (shell, source, entry_type);

	g_object_unref (db);
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

	g_type_class_add_private (klass, sizeof (RBAudioscrobblerProfileSource));
}

static void
rb_audioscrobbler_profile_source_init (RBAudioscrobblerProfileSource *source)
{
	source->priv = RB_AUDIOSCROBBLER_PROFILE_SOURCE_GET_PRIVATE (source);

	/* create the UI */
	source->priv->main_vbox = gtk_vbox_new (FALSE, 4);
	gtk_box_pack_start (GTK_BOX (source), source->priv->main_vbox, TRUE, TRUE, 0);
	gtk_widget_show_all (source->priv->main_vbox);

	rb_audioscrobbler_profile_source_init_login_ui (source);
}

static void
rb_audioscrobbler_profile_source_constructed (GObject *object)
{
	RBAudioscrobblerProfileSource *source;
	RBShell *shell;
	RBAudioscrobblerAccountLoginStatus login_status;

	RB_CHAIN_GOBJECT_METHOD (rb_audioscrobbler_profile_source_parent_class, constructed, object);

	source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (object);
	g_object_get (source, "shell", &shell, NULL);

	/* create the account */
	source->priv->account = rb_audioscrobbler_account_new ();
	g_signal_connect (source->priv->account,
	                  "login-status-changed",
	                  (GCallback)rb_audioscrobbler_profile_source_login_status_change_cb,
	                  source);

	/* create the scrobbler */
	source->priv->audioscrobbler =
		rb_audioscrobbler_new (RB_SHELL_PLAYER (rb_shell_get_player (shell)),
		                       LASTFM_SCROBBLER_URL,
		                       LASTFM_API_KEY,
		                       LASTFM_API_SECRET);

	/* sync account settings to UI and scrobbler settings */
	g_object_get (source->priv->account, "login-status", &login_status, NULL);
	rb_audioscrobbler_profile_source_login_status_change_cb (source->priv->account,
	                                                         login_status,
	                                                         source);

	g_object_unref (shell);
}

static void
rb_audioscrobbler_profile_source_dispose (GObject* object)
{
	RBAudioscrobblerProfileSource *source;

	source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (object);

	if (source->priv->audioscrobbler != NULL) {
		g_object_unref (source->priv->audioscrobbler);
		source->priv->audioscrobbler = NULL;
	}

	if (source->priv->account != NULL) {
		g_object_unref (source->priv->account);
		source->priv->account = NULL;
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
rb_audioscrobbler_profile_source_login_bar_response (GtkInfoBar *info_bar,
                                                     gint response_id,
                                                     gpointer user_data)
{
	RBAudioscrobblerProfileSource *source;
	RBAudioscrobblerAccountLoginStatus status;

	source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (user_data);

	g_object_get (source->priv->account, "login-status", &status, NULL);

	switch (status) {
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT:
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
static void
rb_audioscrobbler_profile_source_login_status_change_cb (RBAudioscrobblerAccount *account,
                                                         RBAudioscrobblerAccountLoginStatus status,
                                                         gpointer user_data)
{
	RBAudioscrobblerProfileSource *source;
	char *username;
	char *session_key;
	char *label_text;
	char *button_text;

	source = RB_AUDIOSCROBBLER_PROFILE_SOURCE (user_data);

	g_object_get (account,
	              "username", &username,
	              "session-key", &session_key,
	              NULL);

	rb_audioscrobbler_set_authentication_details (source->priv->audioscrobbler,
	                                              username,
	                                              session_key);

	switch (status) {
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT:
		gtk_widget_show_all (source->priv->login_bar);
		label_text = g_strdup (_("You are not currently logged in."));
		button_text = g_strdup (_("Login"));
		gtk_info_bar_set_message_type (GTK_INFO_BAR (source->priv->login_bar), GTK_MESSAGE_INFO);
		break;
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGING_IN:
		gtk_widget_show_all (source->priv->login_bar);
		label_text = g_strdup (_("Waiting for authentication..."));
		button_text = g_strdup (_("Cancel"));
		gtk_info_bar_set_message_type (GTK_INFO_BAR (source->priv->login_bar), GTK_MESSAGE_INFO);
		break;
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_IN:
		gtk_widget_show_all (source->priv->login_bar);
		label_text = g_strdup_printf (_("Logged in as %s."), username);
		button_text = g_strdup (_("Logout"));
		gtk_info_bar_set_message_type (GTK_INFO_BAR (source->priv->login_bar), GTK_MESSAGE_INFO);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	gtk_label_set_label (GTK_LABEL (source->priv->login_status_label), label_text);
	gtk_button_set_label (GTK_BUTTON (source->priv->login_response_button), button_text);

	g_free (username);
	g_free (session_key);
	g_free (label_text);
	g_free (button_text);
}
