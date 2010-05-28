/*
 * rb-audioscrobbler-account.c
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

#include <string.h>

#include <glib/gi18n.h>

#include <libsoup/soup.h>
#include <libsoup/soup-gnome.h>

#include "rb-audioscrobbler.h"
#include "rb-audioscrobbler-account.h"
#include "rb-builder-helpers.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

#define LASTFM_API_KEY "0337ff3c59299b6a31d75164041860b6"
#define LASTFM_API_SECRET "776c85a04a445efa8f9ed7705473c606"
#define LASTFM_API_URL "http://ws.audioscrobbler.com/2.0/"
#define LASTFM_AUTH_URL "http://www.last.fm/api/auth/"

#define LASTFM_SESSION_KEY_FILE "session_key"
#define SESSION_KEY_REQUEST_TIMEOUT 15

struct _RBAudioscrobblerAccountPrivate
{
	RBShell *shell;

	/* Authentication info */
	gchar *username;
	gchar *auth_token;
	gchar *session_key;

	/* Widgets for the prefs pane */
	GtkWidget *config_widget;
	GtkWidget *login_status_label;
	GtkWidget *auth_button;

	/* Timeout notifications */
	guint session_key_timeout_id;

	/* HTTP requests session */
	SoupSession *soup_session;

	/* The scrobbler */
	RBAudioscrobbler *audioscrobbler;
};

#define RB_AUDIOSCROBBLER_ACCOUNT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_AUDIOSCROBBLER_ACCOUNT, RBAudioscrobblerAccountPrivate))

static void	     rb_audioscrobbler_account_get_property (GObject *object,
                                                             guint prop_id,
                                                             GValue *value,
                                                             GParamSpec *pspec);
static void	     rb_audioscrobbler_account_set_property (GObject *object,
                                                             guint prop_id,
                                                             const GValue *value,
                                                             GParamSpec *pspec);
static void          rb_audioscrobbler_account_dispose (GObject *object);
static void          rb_audioscrobbler_account_finalize (GObject *object);

static void          rb_audioscrobbler_account_login_status_sync (RBAudioscrobblerAccount *account);

static void          rb_audioscrobbler_account_load_session_settings (RBAudioscrobblerAccount *account);
static void          rb_audioscrobbler_account_save_session_settings (RBAudioscrobblerAccount *account);

static void          rb_audioscrobbler_account_got_token_cb (SoupSession *session,
                                                             SoupMessage *msg,
                                                             gpointer user_data);
static void          rb_audioscrobbler_account_got_session_key_cb (SoupSession *session,
                                                                   SoupMessage *msg,
                                                                   gpointer user_data);

static gboolean      rb_audioscrobbler_account_request_session_key_timeout_cb (gpointer user_data);

enum
{
	PROP_0,
	PROP_SHELL,
};

G_DEFINE_TYPE (RBAudioscrobblerAccount, rb_audioscrobbler_account, G_TYPE_OBJECT)

static void
rb_audioscrobbler_account_constructed (GObject *object)
{
	RBAudioscrobblerAccount *account;

	RB_CHAIN_GOBJECT_METHOD (rb_audioscrobbler_account_parent_class, constructed, object);
	account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	account->priv->audioscrobbler =
		rb_audioscrobbler_new (RB_SHELL_PLAYER (rb_shell_get_player (account->priv->shell)));

	if (account->priv->username != NULL) {
		rb_debug ("setting audioscrobbler's authentication details");
		rb_audioscrobbler_set_authentication_details (account->priv->audioscrobbler,
		                                              account->priv->username,
		                                              account->priv->session_key);
	}

	rb_audioscrobbler_account_login_status_sync (account);
}

static void
rb_audioscrobbler_account_class_init (RBAudioscrobblerAccountClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = rb_audioscrobbler_account_constructed;
	object_class->dispose = rb_audioscrobbler_account_dispose;
	object_class->finalize = rb_audioscrobbler_account_finalize;

	object_class->get_property = rb_audioscrobbler_account_get_property;
	object_class->set_property = rb_audioscrobbler_account_set_property;

	g_object_class_install_property (object_class,
	                                 PROP_SHELL,
	                                 g_param_spec_object ("shell",
	                                                      "RBShell",
	                                                      "RBShell object",
	                                                      RB_TYPE_SHELL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBAudioscrobblerAccountPrivate));
}

static void
rb_audioscrobbler_account_init (RBAudioscrobblerAccount *account)
{
	account->priv = RB_AUDIOSCROBBLER_ACCOUNT_GET_PRIVATE (account);

	account->priv->username = NULL;
	account->priv->auth_token = NULL;
	account->priv->session_key = NULL;

	rb_audioscrobbler_account_load_session_settings (account);

	account->priv->session_key_timeout_id = 0;
}

static void
rb_audioscrobbler_account_dispose (GObject *object)
{
	RBAudioscrobblerAccount *account;

	account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	if (account->priv->session_key_timeout_id != 0) {
		g_source_remove (account->priv->session_key_timeout_id);
		account->priv->session_key_timeout_id = 0;
	}

	if (account->priv->soup_session != NULL) {
		soup_session_abort (account->priv->soup_session);
		g_object_unref (account->priv->soup_session);
		account->priv->soup_session = NULL;
	}

	if (account->priv->audioscrobbler != NULL) {
		g_object_unref (account->priv->audioscrobbler);
		account->priv->audioscrobbler = NULL;
	}

	G_OBJECT_CLASS (rb_audioscrobbler_account_parent_class)->dispose (object);
}

static void
rb_audioscrobbler_account_finalize (GObject *object)
{
	RBAudioscrobblerAccount *account;

	account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	g_free (account->priv->username);
	g_free (account->priv->auth_token);
	g_free (account->priv->session_key);

	G_OBJECT_CLASS (rb_audioscrobbler_account_parent_class)->finalize (object);
}

RBAudioscrobblerAccount *
rb_audioscrobbler_account_new (RBShell *shell)
{
	return g_object_new (RB_TYPE_AUDIOSCROBBLER_ACCOUNT,
	                     "shell", shell,
                             NULL);
}

static void
rb_audioscrobbler_account_get_property (GObject *object,
                                        guint prop_id,
                                        GValue *value,
                                        GParamSpec *pspec)
{
	RBAudioscrobblerAccount *account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	switch (prop_id) {
	case PROP_SHELL:
		g_value_set_object (value, account->priv->shell);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_audioscrobbler_account_set_property (GObject *object,
                                        guint prop_id,
                                        const GValue *value,
                                        GParamSpec *pspec)
{
	RBAudioscrobblerAccount *account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	switch (prop_id) {
	case PROP_SHELL:
		account->priv->shell = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_audioscrobbler_account_login_status_sync (RBAudioscrobblerAccount *account)
{
	char *status;
	char *button_text;

	if (account->priv->config_widget == NULL)
		return;

	if (account->priv->username == NULL) {
		if (account->priv->auth_token == NULL) {
			status = g_strdup (_("You are not currently logged in"));
			button_text = g_strdup (_("Login"));
		} else {
			status = g_strdup (_("Waiting for authentication..."));
			button_text = g_strdup (_("Cancel"));
		}
	} else {
		status = g_strdup_printf (_("Logged in as %s"), account->priv->username);
		button_text = g_strdup (_("Logout"));
	}

	gtk_label_set_label (GTK_LABEL (account->priv->login_status_label), status);
	gtk_button_set_label (GTK_BUTTON (account->priv->auth_button), button_text);

	g_free (status);
	g_free (button_text);
}

static gchar *
mkmd5 (char *string)
{
	GChecksum *checksum;
	gchar *md5_result;

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	g_checksum_update (checksum, (guchar *)string, -1);

	md5_result = g_strdup (g_checksum_get_string (checksum));
	g_checksum_free (checksum);

	return md5_result;
}

static void
rb_audioscrobbler_account_load_session_settings (RBAudioscrobblerAccount *account)
{
	/* Attempt to load the saved username and session key if one exists */
	const char *rb_data_dir;
	char *file_path;
	GFile *file;
	GInputStream *stream;
	GDataInputStream *data_stream;

	rb_data_dir = rb_user_data_dir ();
	if (rb_data_dir != NULL) {
		file_path = g_build_filename (rb_data_dir, LASTFM_SESSION_KEY_FILE, NULL);
		file = g_file_new_for_path (file_path);
		stream = G_INPUT_STREAM (g_file_read (file, NULL, NULL));

		if (stream != NULL) {
			data_stream = g_data_input_stream_new (stream);
			account->priv->username =
				g_data_input_stream_read_line (data_stream, NULL, NULL, NULL);
			account->priv->session_key =
				g_data_input_stream_read_line (data_stream, NULL, NULL, NULL);

			if (account->priv->username == NULL || account->priv->session_key == NULL) {
				/* Ensure both are null, incase file was dodgy */
				g_free (account->priv->username);
				account->priv->username = NULL;
				g_free (account->priv->session_key);
				account->priv->session_key = NULL;
				rb_debug ("no session was loaded");
			} else {
				rb_debug ("loaded session settings: username=\"%s\", session key=\"%s\"",
				          account->priv->username,
				          account->priv->session_key);
			}
			g_object_unref (data_stream);
			g_object_unref (stream);
		}
		g_object_unref (file);
		g_free (file_path);
	}
}

static void
rb_audioscrobbler_account_save_session_settings (RBAudioscrobblerAccount *account)
{
	/* Save the current username and session key to a file */
	const char *rb_data_dir;
	char *file_path;
	GFile *file;
	GOutputStream *stream;
	GDataOutputStream *data_stream;
	char *text_out;

	rb_data_dir = rb_user_data_dir ();
	if (rb_data_dir == NULL)
		return;

	file_path = g_build_filename (rb_data_dir, LASTFM_SESSION_KEY_FILE, NULL);
	file = g_file_new_for_path (file_path);
	stream = G_OUTPUT_STREAM (g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL));

	if (stream != NULL) {
		data_stream = g_data_output_stream_new (stream);

		if (account->priv->username != NULL) {
			text_out = g_strconcat (account->priv->username,
				                "\n",
				                account->priv->session_key,
				                NULL);
			rb_debug ("saving session settings: username=\"%s\", session key=\"%s\"",
		          account->priv->username,
		          account->priv->session_key);
		} else {
			text_out = g_strdup ("");
			rb_debug ("saving empty session");
		}

		g_data_output_stream_put_string (data_stream,
		                                 text_out,
		                                 NULL,
		                                 NULL);

		g_free (text_out);
		g_object_unref (data_stream);
		g_object_unref (stream);
	}
	g_object_unref (file);
	g_free (file_path);
}

GtkWidget *
rb_audioscrobbler_account_get_config_widget (RBAudioscrobblerAccount *account,
                                             RBPlugin *plugin)
{
	GtkBuilder *builder;
	char *builder_file;

	if (account->priv->config_widget)
		return account->priv->config_widget;

	builder_file = rb_plugin_find_file (plugin, "audioscrobbler-prefs.ui");
	g_assert (builder_file != NULL);
	builder = rb_builder_load (builder_file, account);
	g_free (builder_file);

	account->priv->config_widget = GTK_WIDGET (gtk_builder_get_object (builder, "audioscrobbler_vbox"));
	account->priv->login_status_label = GTK_WIDGET (gtk_builder_get_object (builder, "login_status_label"));
	account->priv->auth_button = GTK_WIDGET (gtk_builder_get_object (builder, "auth_button"));

	rb_audioscrobbler_account_login_status_sync (account);

	rb_builder_boldify_label (builder, "audioscrobbler_label");

	return account->priv->config_widget;
}

void
rb_audioscrobbler_account_auth_button_clicked_cb (GtkButton *button,
                                                  RBAudioscrobblerAccount *account)
{
	if (account->priv->username == NULL) {
		if (account->priv->auth_token == NULL) {
			/* request an authentication token */

			char *sig_arg;
			char *sig;
			char *url;
			SoupMessage *msg;

			/* create soup session, if we haven't got one yet */
			if (account->priv->soup_session == NULL) {
				account->priv->soup_session =
					soup_session_async_new_with_options (SOUP_SESSION_ADD_FEATURE_BY_TYPE,
				                                             SOUP_TYPE_GNOME_FEATURES_2_26,
				                                             NULL);
			}

			/* make the request */
			sig_arg = g_strdup_printf ("api_key%smethodauth.getToken%s",
			                           LASTFM_API_KEY,
			                           LASTFM_API_SECRET);
			sig = mkmd5 (sig_arg);
			url = g_strdup_printf ("%s?method=auth.getToken&api_key=%s&api_sig=%s",
					       LASTFM_API_URL, LASTFM_API_KEY, sig);

			msg = soup_message_new ("GET", url);

			rb_debug ("requesting authorisation token");
			soup_session_queue_message (account->priv->soup_session,
					            msg,
					            rb_audioscrobbler_account_got_token_cb,
					            account);

			g_free (sig_arg);
			g_free (sig);
			g_free (url);
		} else {
			/* delete the token */
			rb_debug ("cancelling authorisation request");
			g_free (account->priv->auth_token);
			account->priv->auth_token = NULL;

			/* remove timeout callback */
			g_source_remove (account->priv->session_key_timeout_id);
			account->priv->session_key_timeout_id = 0;

			rb_audioscrobbler_account_login_status_sync (account);
		}
	} else {
		/* delete the session */
		rb_debug ("logging out of session");
		g_free (account->priv->username);
		account->priv->username = NULL;
		g_free (account->priv->session_key);
		account->priv->session_key = NULL;

		rb_audioscrobbler_account_save_session_settings (account);
		rb_audioscrobbler_account_login_status_sync (account);
	}
}

/* Request callbacks */
static void
rb_audioscrobbler_account_got_token_cb (SoupSession *session,
                                        SoupMessage *msg,
                                        gpointer user_data)
{
	RBAudioscrobblerAccount *account = RB_AUDIOSCROBBLER_ACCOUNT (user_data);

	if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code) && msg->response_body->length != 0) {
		char **pre_split;
		char **post_split;
		char *url;

		/* parse the response */
		pre_split = g_strsplit (msg->response_body->data, "<token>", -1);
		post_split = g_strsplit (pre_split[1], "</token>", -1);
		account->priv->auth_token = g_strdup (post_split[0]);
		rb_debug ("granted auth token \"%s\"", account->priv->auth_token);

		/* send the user to the web page using the token */
		url = g_strdup_printf ("%s?api_key=%s&token=%s",
		                       LASTFM_AUTH_URL,
		                       LASTFM_API_KEY,
		                       account->priv->auth_token);
		rb_debug ("sending user to %s", url);
		gtk_show_uri (NULL, url, GDK_CURRENT_TIME, NULL);

		/* add timeout which will ask for session key */
		account->priv->session_key_timeout_id =
			g_timeout_add_seconds (SESSION_KEY_REQUEST_TIMEOUT,
			                       rb_audioscrobbler_account_request_session_key_timeout_cb,
			                       account);

		/* we are now waiting for authentication - update the UI */
		rb_audioscrobbler_account_login_status_sync (account);

		g_strfreev (pre_split);
		g_strfreev (post_split);
		g_free (url);
	}
}

static void
rb_audioscrobbler_account_got_session_key_cb (SoupSession *session,
                                              SoupMessage *msg,
                                              gpointer user_data)
{
	RBAudioscrobblerAccount *account;

	account = RB_AUDIOSCROBBLER_ACCOUNT (user_data);

	if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code) && msg->response_body->length != 0) {
		char **pre_split;
		char **post_split;

		/* parse the username */
		pre_split = g_strsplit (msg->response_body->data, "<name>", -1);
		post_split = g_strsplit (pre_split[1], "</name>", -1);
		account->priv->username = g_strdup (post_split[0]);
		g_strfreev (pre_split);
		g_strfreev (post_split);

		/* parse the session key */
		pre_split = g_strsplit (msg->response_body->data, "<key>", -1);
		post_split = g_strsplit (pre_split[1], "</key>", -1);
		account->priv->session_key = g_strdup (post_split[0]);
		g_strfreev (pre_split);
		g_strfreev (post_split);

		/* save our session for future use */
		rb_debug ("granted session key \"%s\" for user \"%s",
		          account->priv->session_key,
		          account->priv->username);
		rb_audioscrobbler_account_save_session_settings (account);

		/* we are now logged in - update the UI */
		rb_audioscrobbler_account_login_status_sync (account);

		/* remove timeout callback */
		g_source_remove (account->priv->session_key_timeout_id);
		account->priv->session_key_timeout_id = 0;

		/* delete authorisation token */
		g_free (account->priv->auth_token);
		account->priv->auth_token = NULL;

		/* update the audioscrobbler's details */
		if (account->priv->audioscrobbler != NULL) {
			rb_audioscrobbler_set_authentication_details (account->priv->audioscrobbler,
			                                              account->priv->username,
			                                              account->priv->session_key);
		}
	} else {
		char **pre_split;
		char **post_split;
		char *error;

		/* parse the error code */
		pre_split = g_strsplit (msg->response_body->data, "<error code=\"", -1);
		post_split = g_strsplit (pre_split[1], "\">", -1);
		error = g_strdup (post_split[0]);

		if (g_strcmp0 (error, "14") == 0) {
			rb_debug ("auth token has not been authorised yet. will try again");
		} else {
			/* some other error. most likely 4 (invalid token) or 15 (token has expired)
			   whatever it is, we wont be retrieving a session key from it */
			rb_debug ("error retrieving session key. giving up");

			/* remove timeout callback */
			g_source_remove (account->priv->session_key_timeout_id);
			account->priv->session_key_timeout_id = 0;

			/* delete authorisation token */
			g_free (account->priv->auth_token);
			account->priv->auth_token = NULL;

			rb_audioscrobbler_account_login_status_sync (account);
		}

		g_strfreev (pre_split);
		g_strfreev (post_split);
		g_free (error);
	}
}

/* Periodically sends a request for the session key */
static gboolean
rb_audioscrobbler_account_request_session_key_timeout_cb (gpointer user_data)
{
	RBAudioscrobblerAccount *account;
	char *sig_arg;
	char *sig;
	char *url;
	SoupMessage *msg;

	g_assert (RB_IS_AUDIOSCROBBLER_ACCOUNT (user_data));
	account = RB_AUDIOSCROBBLER_ACCOUNT (user_data);

	g_assert (account->priv->auth_token != NULL);

	sig_arg = g_strdup_printf ("api_key%smethodauth.getSessiontoken%s%s",
	                           LASTFM_API_KEY,
	                           account->priv->auth_token,
	                           LASTFM_API_SECRET);
	sig = mkmd5 (sig_arg);
	url = g_strdup_printf ("%s?method=auth.getSession&api_key=%s&token=%s&api_sig=%s",
	                       LASTFM_API_URL,
	                       LASTFM_API_KEY,
	                       account->priv->auth_token,
	                       sig);

	msg = soup_message_new ("GET", url);

	rb_debug ("requesting session key");
	soup_session_queue_message (account->priv->soup_session,
	                            msg,
	                            rb_audioscrobbler_account_got_session_key_cb,
	                            account);

	g_free (sig_arg);
	g_free (sig);
	g_free (url);

	return TRUE;
}
