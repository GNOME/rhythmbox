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

#include "rb-audioscrobbler-account.h"
#include "rb-builder-helpers.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

/* this API key belongs to Jamie Nicol <jamie@thenicols.net>
   generated May 2010 for use in the audioscrobbler plugin */
#define LASTFM_API_KEY "0337ff3c59299b6a31d75164041860b6"
#define LASTFM_API_SECRET "776c85a04a445efa8f9ed7705473c606"
#define LASTFM_API_URL "http://ws.audioscrobbler.com/2.0/"
#define LASTFM_AUTH_URL "http://www.last.fm/api/auth/"
#define LASTFM_SCROBBLER_URL "http://post.audioscrobbler.com/"

#define LASTFM_SESSION_SETTINGS_FILE "lastfm_session"
#define SESSION_KEY_REQUEST_TIMEOUT 15

struct _RBAudioscrobblerAccountPrivate
{
	/* Authentication info */
	gchar *username;
	gchar *auth_token;
	gchar *session_key;
	RBAudioscrobblerAccountLoginStatus login_status;

	/* Widgets for the prefs pane */
	GtkWidget *config_widget;
	GtkWidget *login_status_label;
	GtkWidget *auth_button;

	/* Timeout notifications */
	guint session_key_timeout_id;

	/* HTTP requests session */
	SoupSession *soup_session;
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

static void          rb_audioscrobbler_account_load_session_settings (RBAudioscrobblerAccount *account);
static void          rb_audioscrobbler_account_save_session_settings (RBAudioscrobblerAccount *account);

static void          rb_audioscrobbler_account_request_token (RBAudioscrobblerAccount *account);
static void          rb_audioscrobbler_account_got_token_cb (SoupSession *session,
                                                             SoupMessage *msg,
                                                             gpointer user_data);
static gboolean      rb_audioscrobbler_account_request_session_key_timeout_cb (gpointer user_data);
static void          rb_audioscrobbler_account_got_session_key_cb (SoupSession *session,
                                                                   SoupMessage *msg,
                                                                   gpointer user_data);
enum
{
	PROP_0,
	PROP_USERNAME,
	PROP_SESSION_KEY,
	PROP_LOGIN_STATUS
};

enum
{
	LOGIN_STATUS_CHANGED,
	LAST_SIGNAL
};

static guint rb_audioscrobbler_account_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBAudioscrobblerAccount, rb_audioscrobbler_account, G_TYPE_OBJECT)

static void
rb_audioscrobbler_account_constructed (GObject *object)
{
	RBAudioscrobblerAccount *account;

	RB_CHAIN_GOBJECT_METHOD (rb_audioscrobbler_account_parent_class, constructed, object);
	account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	rb_audioscrobbler_account_load_session_settings (account);
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
	                                 PROP_USERNAME,
	                                 g_param_spec_string ("username",
	                                                      "Username",
	                                                      "Username",
	                                                      NULL,
                                                              G_PARAM_READABLE));

	g_object_class_install_property (object_class,
	                                 PROP_SESSION_KEY,
	                                 g_param_spec_string ("session-key",
	                                                      "Session Key",
	                                                      "Session key used to authenticate the user",
	                                                      NULL,
                                                              G_PARAM_READABLE));

	g_object_class_install_property (object_class,
	                                 PROP_LOGIN_STATUS,
	                                 g_param_spec_enum ("login-status",
	                                                     "Login Status",
	                                                     "Login status",
	                                                     RB_TYPE_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS,
	                                                     RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT,
                                                             G_PARAM_READABLE));

	/**
	 * RBAudioscrobblerAccount::login-status-changed:
	 * @account: the #RBAudioscrobblerAccount
	 * @status: new status
	 *
	 * Emitted after the login status of the account has changed.
	 */
	rb_audioscrobbler_account_signals[LOGIN_STATUS_CHANGED] =
		g_signal_new ("login-status-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBAudioscrobblerAccountClass, login_status_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__ENUM,
			      G_TYPE_NONE,
			      1,
			      RB_TYPE_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS);

	g_assert (rb_audioscrobbler_account_signals[LOGIN_STATUS_CHANGED] != 0);

	g_type_class_add_private (klass, sizeof (RBAudioscrobblerAccountPrivate));
}

static void
rb_audioscrobbler_account_init (RBAudioscrobblerAccount *account)
{
	account->priv = RB_AUDIOSCROBBLER_ACCOUNT_GET_PRIVATE (account);

	account->priv->username = NULL;
	account->priv->auth_token = NULL;
	account->priv->session_key = NULL;
	account->priv->login_status = RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT;

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
rb_audioscrobbler_account_new (void)
{
	return g_object_new (RB_TYPE_AUDIOSCROBBLER_ACCOUNT,
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
	case PROP_USERNAME:
		g_value_set_string (value, account->priv->username);
		break;
	case PROP_SESSION_KEY:
		g_value_set_string (value, account->priv->session_key);
		break;
	case PROP_LOGIN_STATUS:
		g_value_set_enum (value, account->priv->login_status);
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
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
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
	/* Attempt to load the saved session */
	const char *rb_data_dir;
	char *file_path;
	GKeyFile *key_file;

	rb_data_dir = rb_user_data_dir ();
	if (rb_data_dir == NULL) {
		rb_debug ("error loading session: could not find data dir");
		return;
	}

	file_path = g_build_filename (rb_data_dir, LASTFM_SESSION_SETTINGS_FILE, NULL);
	key_file = g_key_file_new ();
	g_key_file_load_from_file (key_file, file_path, G_KEY_FILE_NONE, NULL);

	account->priv->username = g_key_file_get_string (key_file, "last.fm", "username", NULL);
	account->priv->session_key = g_key_file_get_string (key_file, "last.fm", "session_key", NULL);

	g_free (file_path);
	g_key_file_free (key_file);

	if (account->priv->username != NULL && account->priv->session_key != NULL) {
		rb_debug ("loaded session: username=\"%s\", session key=\"%s\"",
			          account->priv->username,
			          account->priv->session_key);

		account->priv->login_status = RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_IN;
		g_signal_emit (account, rb_audioscrobbler_account_signals[LOGIN_STATUS_CHANGED],
		               0, account->priv->login_status);
	} else {
		rb_debug ("there is no session to load");

		/* free both incase only one of them did not load */
		g_free (account->priv->username);
		account->priv->username = NULL;
		g_free (account->priv->session_key);
		account->priv->session_key = NULL;

		account->priv->login_status = RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT;
		g_signal_emit (account, rb_audioscrobbler_account_signals[LOGIN_STATUS_CHANGED],
		               0, account->priv->login_status);
	}
}

static void
rb_audioscrobbler_account_save_session_settings (RBAudioscrobblerAccount *account)
{
	/* Save the current session */
	const char *rb_data_dir;
	char *file_path;
	GKeyFile *key_file;
	char *data;
	gsize data_length;
	GFile *out_file;
	GError *error;

	rb_data_dir = rb_user_data_dir ();
	if (rb_data_dir == NULL) {
		rb_debug ("error saving session: could not find data dir");
		return;
	}

	file_path = g_build_filename (rb_data_dir, LASTFM_SESSION_SETTINGS_FILE, NULL);
	key_file = g_key_file_new ();
	/* load existing file contents. errors wont matter, just means file doesn't exist yet */
	g_key_file_load_from_file (key_file, file_path, G_KEY_FILE_KEEP_COMMENTS, NULL);

	/* set the new data */
	if (account->priv->username != NULL && account->priv->session_key != NULL) {
		g_key_file_set_string (key_file, "last.fm", "username", account->priv->username);
		g_key_file_set_string (key_file, "last.fm", "session_key", account->priv->session_key);
	} else {
		g_key_file_remove_group (key_file, "last.fm", NULL);
	}

	data = g_key_file_to_data (key_file, &data_length, NULL);
	g_key_file_free (key_file);

	/* write data to the file */
	out_file = g_file_new_for_path (file_path);
	g_free (file_path);

	error = NULL;
	g_file_replace_contents (out_file, data, data_length, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL, &error);
	if (error != NULL) {
		rb_debug ("error saving session: %s", error->message);
		g_error_free (error);
	} else {
		rb_debug ("successfully saved session");
	}

	g_free (data);
	g_object_unref (out_file);
}

/* public authentication functions */
void
rb_audioscrobbler_account_authenticate (RBAudioscrobblerAccount *account)
{
	/* begin the web services authentication process */
	if (account->priv->login_status != RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT) {
		rb_debug ("logging out before starting auth process");
		rb_audioscrobbler_account_logout (account);
	}

	/* request an authentication token */
	rb_audioscrobbler_account_request_token (account);
}

void
rb_audioscrobbler_account_logout (RBAudioscrobblerAccount *account)
{
	g_free (account->priv->username);
	account->priv->username = NULL;

	g_free (account->priv->auth_token);
	account->priv->auth_token = NULL;

	g_free (account->priv->session_key);
	account->priv->session_key = NULL;

	if (account->priv->session_key_timeout_id != 0) {
		g_source_remove (account->priv->session_key_timeout_id);
		account->priv->session_key_timeout_id = 0;
	}

	rb_audioscrobbler_account_save_session_settings (account);

	account->priv->login_status = RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT;
	g_signal_emit (account, rb_audioscrobbler_account_signals[LOGIN_STATUS_CHANGED],
	               0, account->priv->login_status);
}

/* private authentication functions */
static void
rb_audioscrobbler_account_request_token (RBAudioscrobblerAccount *account)
{
	/* requests an authentication token
	 * first stage of the authentication process
	 */
	char *sig_arg;
	char *sig;
	char *url;
	SoupMessage *msg;

	/* create the soup session, if we haven't got one yet */
	if (account->priv->soup_session == NULL) {
		account->priv->soup_session =
			soup_session_async_new_with_options (SOUP_SESSION_ADD_FEATURE_BY_TYPE,
		                                             SOUP_TYPE_GNOME_FEATURES_2_26,
		                                             NULL);
	}

	/* create the request */
	sig_arg = g_strdup_printf ("api_key%smethodauth.getToken%s",
	                           LASTFM_API_KEY,
	                           LASTFM_API_SECRET);
	sig = mkmd5 (sig_arg);
	url = g_strdup_printf ("%s?method=auth.getToken&api_key=%s&api_sig=%s",
			       LASTFM_API_URL, LASTFM_API_KEY, sig);

	msg = soup_message_new ("GET", url);

	/* send the request */
	rb_debug ("requesting authorisation token");
	soup_session_queue_message (account->priv->soup_session,
			            msg,
			            rb_audioscrobbler_account_got_token_cb,
			            account);

	/* update status */
	account->priv->login_status = RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGING_IN;
	g_signal_emit (account, rb_audioscrobbler_account_signals[LOGIN_STATUS_CHANGED],
	               0, account->priv->login_status);

	g_free (sig_arg);
	g_free (sig);
	g_free (url);
}

static void
rb_audioscrobbler_account_got_token_cb (SoupSession *session,
                                        SoupMessage *msg,
                                        gpointer user_data)
{
	/* parses the authentication token from the response
	 */
	RBAudioscrobblerAccount *account;

	g_assert (RB_IS_AUDIOSCROBBLER_ACCOUNT (user_data));
	account = RB_AUDIOSCROBBLER_ACCOUNT (user_data);

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

		g_strfreev (pre_split);
		g_strfreev (post_split);
		g_free (url);
	} else {
		/* failed. go back to being logged out */
		rb_audioscrobbler_account_logout (account);
	}
}

static gboolean
rb_audioscrobbler_account_request_session_key_timeout_cb (gpointer user_data)
{
	/* Periodically sends a request for the session key */
	RBAudioscrobblerAccount *account;
	char *sig_arg;
	char *sig;
	char *url;
	SoupMessage *msg;

	g_assert (RB_IS_AUDIOSCROBBLER_ACCOUNT (user_data));
	account = RB_AUDIOSCROBBLER_ACCOUNT (user_data);

	/* create the request */
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

	/* send the request */
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

static void
rb_audioscrobbler_account_got_session_key_cb (SoupSession *session,
                                              SoupMessage *msg,
                                              gpointer user_data)
{
	/* parses the session details from the response.
	 * if successful then authentication is complete.
	 * if the error is that the token has not been authenticated
	 * then keep trying.
	 * on other errors stop trying and go to logged out state.
	 */
	RBAudioscrobblerAccount *account;

	g_assert (RB_IS_AUDIOSCROBBLER_ACCOUNT (user_data));
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

		/* remove timeout callback */
		g_source_remove (account->priv->session_key_timeout_id);
		account->priv->session_key_timeout_id = 0;

		/* delete authorisation token */
		g_free (account->priv->auth_token);
		account->priv->auth_token = NULL;

		/* update status */
		account->priv->login_status = RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_IN;
		g_signal_emit (account, rb_audioscrobbler_account_signals[LOGIN_STATUS_CHANGED],
		               0, account->priv->login_status);
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

			/* go back to being logged out */
			rb_audioscrobbler_account_logout (account);
		}

		g_strfreev (pre_split);
		g_strfreev (post_split);
		g_free (error);
	}
}

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_audioscrobbler_account_login_status_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT, "Logged out"),
			ENUM_ENTRY (RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGING_IN, "Logging in"),
			ENUM_ENTRY (RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_IN, "Logged in"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBAudioscrobblerAccountLoginStatus", values);
	}

	return etype;
}
