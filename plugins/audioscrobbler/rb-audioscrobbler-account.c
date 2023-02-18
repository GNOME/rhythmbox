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

#include "config.h"

#include <string.h>

#include <glib/gi18n.h>

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "rb-audioscrobbler-account.h"
#include "rb-builder-helpers.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

#define SESSION_SETTINGS_FILE "sessions"
#define SESSION_KEY_REQUEST_TIMEOUT 5

struct _RBAudioscrobblerAccountPrivate
{
	RBAudioscrobblerService *service;

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

static void          rb_audioscrobbler_account_class_init (RBAudioscrobblerAccountClass *klass);
static void          rb_audioscrobbler_account_init (RBAudioscrobblerAccount *account);
static void          rb_audioscrobbler_account_constructed (GObject *object);
static void          rb_audioscrobbler_account_dispose (GObject *object);
static void          rb_audioscrobbler_account_finalize (GObject *object);
static void	     rb_audioscrobbler_account_get_property (GObject *object,
                                                             guint prop_id,
                                                             GValue *value,
                                                             GParamSpec *pspec);
static void	     rb_audioscrobbler_account_set_property (GObject *object,
                                                             guint prop_id,
                                                             const GValue *value,
                                                             GParamSpec *pspec);

/* load/save session to file to avoid having to reauthenticate */
static void          load_session_settings (RBAudioscrobblerAccount *account);
static void          save_session_settings (RBAudioscrobblerAccount *account);

/* private functions used in authentication process */
static void          cancel_session (RBAudioscrobblerAccount *account);
static void          request_token (RBAudioscrobblerAccount *account);
static void          got_token_cb (SoupSession *session,
                                   GAsyncResult *result,
                                   RBAudioscrobblerAccount *account);
static void          parse_token (RBAudioscrobblerAccount *account,
                                  const char *body,
                                  gsize body_size);
static gboolean      request_session_key_timeout_cb (gpointer user_data);
static void          got_session_key_cb (SoupSession *session,
                                         GAsyncResult *result,
                                         RBAudioscrobblerAccount *account);
static void          parse_session_key (RBAudioscrobblerAccount *account,
                                        const char *body,
                                        gsize body_size);

enum
{
	PROP_0,
	PROP_SERVICE,
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

G_DEFINE_DYNAMIC_TYPE (RBAudioscrobblerAccount, rb_audioscrobbler_account, G_TYPE_OBJECT)

RBAudioscrobblerAccount *
rb_audioscrobbler_account_new (RBAudioscrobblerService *service)
{
	return g_object_new (RB_TYPE_AUDIOSCROBBLER_ACCOUNT,
                             "service", service,
	                     NULL);
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
	                                 PROP_SERVICE,
	                                 g_param_spec_object ("service",
	                                                      "Service",
	                                                      "Audioscrobbler service the account is with",
	                                                      RB_TYPE_AUDIOSCROBBLER_SERVICE,
                                                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

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
			      NULL,
			      G_TYPE_NONE,
			      1,
			      RB_TYPE_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS);

	g_type_class_add_private (klass, sizeof (RBAudioscrobblerAccountPrivate));
}

static void
rb_audioscrobbler_account_class_finalize (RBAudioscrobblerAccountClass *klass)
{
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
rb_audioscrobbler_account_constructed (GObject *object)
{
	RBAudioscrobblerAccount *account;

	RB_CHAIN_GOBJECT_METHOD (rb_audioscrobbler_account_parent_class, constructed, object);
	account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	load_session_settings (account);
}

static void
rb_audioscrobbler_account_dispose (GObject *object)
{
	RBAudioscrobblerAccount *account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	if (account->priv->service != NULL) {
		g_object_unref (account->priv->service);
		account->priv->service = NULL;
	}

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
	RBAudioscrobblerAccount *account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	g_free (account->priv->username);
	g_free (account->priv->auth_token);
	g_free (account->priv->session_key);

	G_OBJECT_CLASS (rb_audioscrobbler_account_parent_class)->finalize (object);
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
		g_value_set_string (value, rb_audioscrobbler_account_get_username (account));
		break;
	case PROP_SESSION_KEY:
		g_value_set_string (value, rb_audioscrobbler_account_get_session_key (account));
		break;
	case PROP_LOGIN_STATUS:
		g_value_set_enum (value, rb_audioscrobbler_account_get_login_status (account));
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
	case PROP_SERVICE:
		account->priv->service = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

const char *
rb_audioscrobbler_account_get_username (RBAudioscrobblerAccount *account)
{
	return account->priv->username;
}

const char *
rb_audioscrobbler_account_get_session_key (RBAudioscrobblerAccount *account)
{
	return account->priv->session_key;
}

RBAudioscrobblerAccountLoginStatus
rb_audioscrobbler_account_get_login_status (RBAudioscrobblerAccount *account)
{
	return account->priv->login_status;
}

static void
load_session_settings (RBAudioscrobblerAccount *account)
{
	/* Attempt to load the saved session */
	const char *rb_data_dir;
	char *file_path;
	GKeyFile *key_file;
	char *service_name;

	rb_data_dir = rb_user_data_dir ();
	if (rb_data_dir == NULL) {
		rb_debug ("error loading session: could not find data dir");
		return;
	}

	file_path = g_build_filename (rb_data_dir, "audioscrobbler", SESSION_SETTINGS_FILE, NULL);
	key_file = g_key_file_new ();
	g_key_file_load_from_file (key_file, file_path, G_KEY_FILE_NONE, NULL);

	/* get the service name */
	g_object_get (account->priv->service, "name", &service_name, NULL);

	account->priv->username = g_key_file_get_string (key_file, service_name, "username", NULL);
	account->priv->session_key = g_key_file_get_string (key_file, service_name, "session_key", NULL);

	g_free (file_path);
	g_key_file_free (key_file);
	g_free (service_name);

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
save_session_settings (RBAudioscrobblerAccount *account)
{
	/* Save the current session */
	const char *rb_data_dir;
	char *file_path;
	GKeyFile *key_file;
	char *service_name;
	char *data;
	gsize data_length;
	GFile *out_file;
	GError *error;

	rb_data_dir = rb_user_data_dir ();
	if (rb_data_dir == NULL) {
		rb_debug ("error saving session: could not find data dir");
		return;
	}

	file_path = g_build_filename (rb_data_dir, "audioscrobbler", SESSION_SETTINGS_FILE, NULL);
	key_file = g_key_file_new ();
	/* load existing file contents. errors wont matter, just means file doesn't exist yet */
	g_key_file_load_from_file (key_file, file_path, G_KEY_FILE_KEEP_COMMENTS, NULL);

	/* get the service name */
	g_object_get (account->priv->service, "name", &service_name, NULL);

	/* set the new data */
	if (account->priv->username != NULL && account->priv->session_key != NULL) {
		g_key_file_set_string (key_file, service_name, "username", account->priv->username);
		g_key_file_set_string (key_file, service_name, "session_key", account->priv->session_key);
	} else {
		g_key_file_remove_group (key_file, service_name, NULL);
	}
	g_free (service_name);

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

void
rb_audioscrobbler_account_authenticate (RBAudioscrobblerAccount *account)
{
	/* begin the web services authentication process */
	if (account->priv->login_status != RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT) {
		rb_debug ("logging out before starting auth process");
		rb_audioscrobbler_account_logout (account);
	}

	/* request an authentication token */
	request_token (account);
}

void
rb_audioscrobbler_account_logout (RBAudioscrobblerAccount *account)
{
	cancel_session (account);
	save_session_settings (account);

	account->priv->login_status = RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT;
	g_signal_emit (account, rb_audioscrobbler_account_signals[LOGIN_STATUS_CHANGED],
	               0, account->priv->login_status);
}

void
rb_audioscrobbler_account_notify_of_auth_error (RBAudioscrobblerAccount *account)
{
	/* After a session has been granted, no authentication methods will be called
	 * therefore we must rely on other classes which call other methods (submissions,
	 * radio, etc) to notify us when there is an authentication error
	 */

	cancel_session (account);
	save_session_settings (account);

	account->priv->login_status = RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_AUTH_ERROR;
	g_signal_emit (account, rb_audioscrobbler_account_signals[LOGIN_STATUS_CHANGED],
	               0, account->priv->login_status);
}

static void
cancel_session (RBAudioscrobblerAccount *account)
{
	/* cancels the current session, freeing the username,
	 * session key, auth token. removing timeout callbacks etc.
	 * Basically log out without setting state to logged out:
	 * eg error states will also want to cancel the session
	 */
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
}

static void
request_token (RBAudioscrobblerAccount *account)
{
	/* requests an authentication token
	 * first stage of the authentication process
	 */
	const char *api_key;
	const char *api_sec;
	const char *api_url;
	char *sig_arg;
	char *sig;
	char *query;
	SoupMessage *msg;

	/* create the soup session, if we haven't got one yet */
	if (account->priv->soup_session == NULL) {
		account->priv->soup_session = soup_session_new ();
	}

	api_key = rb_audioscrobbler_service_get_api_key (account->priv->service);
	api_sec = rb_audioscrobbler_service_get_api_secret (account->priv->service);
	api_url = rb_audioscrobbler_service_get_api_url (account->priv->service);

	/* create the request */
	sig_arg = g_strdup_printf ("api_key%smethodauth.getToken%s", api_key, api_sec);
	sig = g_compute_checksum_for_string (G_CHECKSUM_MD5, sig_arg, -1);

	query = soup_form_encode ("method", "auth.getToken",
				  "api_key", api_key,
				  "api_sig", sig,
				  "format", "json",
				  NULL);

	g_free (sig_arg);
	g_free (sig);

	msg = soup_message_new_from_encoded_form (SOUP_METHOD_GET, api_url, query);
	g_return_if_fail (msg != NULL);

	/* send the request */
	rb_debug ("requesting authorisation token");

	soup_session_send_and_read_async (account->priv->soup_session,
					  msg,
					  G_PRIORITY_DEFAULT,
					  NULL,
					  (GAsyncReadyCallback) got_token_cb,
					  account);

	/* update status */
	account->priv->login_status = RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGING_IN;
	g_signal_emit (account, rb_audioscrobbler_account_signals[LOGIN_STATUS_CHANGED],
	               0, account->priv->login_status);
}

static void
got_token_cb (SoupSession *session, GAsyncResult *result, RBAudioscrobblerAccount *account)
{
	GBytes *bytes;
	const char *body;
	gsize size;

	bytes = soup_session_send_and_read_finish (session, result, NULL);
	if (bytes != NULL) {
		body = g_bytes_get_data (bytes, &size);
	} else {
		body = NULL;
		size = 0;
	}

	parse_token (account, body, size);

	if (bytes != NULL) {
		g_bytes_unref (bytes);
	}
}

static void
parse_token (RBAudioscrobblerAccount *account, const char *body, gsize body_size)
{
	JsonParser *parser;

	parser = json_parser_new ();

	if (body != NULL && json_parser_load_from_data (parser, body, (gssize)body_size, NULL)) {
		JsonObject *root_object;

		root_object = json_node_get_object (json_parser_get_root (parser));
		if (json_object_has_member (root_object, "token")) {
			char *url;

			account->priv->auth_token = g_strdup (json_object_get_string_member (root_object, "token"));
			rb_debug ("granted auth token \"%s\"", account->priv->auth_token);

			/* send the user to the web page using the token */
			url = g_strdup_printf ("%s?api_key=%s&token=%s",
				               rb_audioscrobbler_service_get_auth_url (account->priv->service),
				               rb_audioscrobbler_service_get_api_key (account->priv->service),
				               account->priv->auth_token);
			rb_debug ("sending user to %s", url);
			gtk_show_uri (NULL, url, GDK_CURRENT_TIME, NULL);

			/* add timeout which will ask for session key */
			account->priv->session_key_timeout_id =
				g_timeout_add_seconds (SESSION_KEY_REQUEST_TIMEOUT,
					               request_session_key_timeout_cb,
					               account);

			g_free (url);
		} else {
			rb_debug ("error retrieving auth token: %s",
			          json_object_get_string_member (root_object, "message"));

			/* go back to being logged out */
			rb_audioscrobbler_account_logout (account);
		}
	} else {
		/* treat as connection error */
		rb_debug ("empty or invalid response retrieving auth token. treating as connection error");

		cancel_session (account);

		account->priv->login_status = RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_CONNECTION_ERROR;
		g_signal_emit (account, rb_audioscrobbler_account_signals[LOGIN_STATUS_CHANGED],
		               0, account->priv->login_status);
	}

	g_object_unref (parser);
}

static gboolean
request_session_key_timeout_cb (gpointer user_data)
{
	/* Periodically sends a request for the session key */
	RBAudioscrobblerAccount *account;
	const char *api_key;
	const char *api_sec;
	const char *api_url;
	char *sig_arg;
	char *sig;
	char *query;
	SoupMessage *msg;

	g_assert (RB_IS_AUDIOSCROBBLER_ACCOUNT (user_data));
	account = RB_AUDIOSCROBBLER_ACCOUNT (user_data);

	api_key = rb_audioscrobbler_service_get_api_key (account->priv->service);
	api_sec = rb_audioscrobbler_service_get_api_secret (account->priv->service);
	api_url = rb_audioscrobbler_service_get_api_url (account->priv->service);

	/* create the request */
	sig_arg = g_strdup_printf ("api_key%smethodauth.getSessiontoken%s%s",
	                           api_key,
	                           account->priv->auth_token,
	                           api_sec);
	sig = g_compute_checksum_for_string (G_CHECKSUM_MD5, sig_arg, -1);

	query = soup_form_encode ("method", "auth.getSession",
				  "api_key", api_key,
				  "token", account->priv->auth_token,
				  "api_sig", sig,
				  "format", "json",
				  NULL);

	g_free (sig_arg);
	g_free (sig);

	msg = soup_message_new_from_encoded_form (SOUP_METHOD_GET, api_url, query);
	g_return_val_if_fail (msg != NULL, FALSE);

	/* send the request */
	rb_debug ("requesting session key");

	soup_session_send_and_read_async (account->priv->soup_session,
					  msg,
					  G_PRIORITY_DEFAULT,
					  NULL,
					  (GAsyncReadyCallback) got_session_key_cb,
					  account);

	return TRUE;
}

static void
got_session_key_cb (SoupSession *session, GAsyncResult *result, RBAudioscrobblerAccount *account)
{
	GBytes *bytes;
	const char *body;
	gsize size;

	bytes = soup_session_send_and_read_finish (session, result, NULL);
	if (bytes != NULL) {
		body = g_bytes_get_data (bytes, &size);
	} else {
		body = NULL;
		size = 0;
	}

	parse_session_key (account, body, size);

	if (bytes != NULL) {
		g_bytes_unref (bytes);
	}
}

static void
parse_session_key (RBAudioscrobblerAccount *account, const char *body, gsize body_size)
{
	/* parses the session details from the response.
	 * if successful then authentication is complete.
	 * if the error is that the token has not been authenticated
	 * then keep trying.
	 * on other errors stop trying and go to logged out state.
	 */
	JsonParser *parser;

	parser = json_parser_new ();

	if (body != NULL && json_parser_load_from_data (parser, body, (gssize)body_size, NULL)) {
		JsonObject *root_object;

		root_object = json_node_get_object (json_parser_get_root (parser));
		if (json_object_has_member (root_object, "session")) {
			JsonObject *session_object;

			/* cancel the old session (and remove timeout) */
			cancel_session (account);

			session_object = json_object_get_object_member (root_object, "session");
			account->priv->username = g_strdup (json_object_get_string_member (session_object, "name"));
			account->priv->session_key = g_strdup (json_object_get_string_member (session_object, "key"));

			rb_debug ("granted session key \"%s\" for user \"%s\"",
				  account->priv->session_key,
				  account->priv->username);

			/* save our session for future use */
			save_session_settings (account);

			/* update status */
			account->priv->login_status = RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_IN;
			g_signal_emit (account, rb_audioscrobbler_account_signals[LOGIN_STATUS_CHANGED],
				       0, account->priv->login_status);
		} else {
			int code;
			const char *message;

			code = json_object_get_int_member (root_object, "error");
			message = json_object_get_string_member (root_object, "message");

			switch (code) {
			case 14:
				rb_debug ("auth token has not been authorised yet. will try again");
				break;
			default:
				/* some other error. most likely 4 (invalid token) or 15 (token has expired)
				 * whatever it is, we wont be retrieving a session key from it
				 */
				rb_debug ("error retrieving session key: %s", message);

				/* go back to being logged out */
				rb_audioscrobbler_account_logout (account);
				break;
			}
		}

	} else {
		/* treat as connection error */
		rb_debug ("empty or invalid response retrieving session key. treating as connection error");

		cancel_session (account);

		account->priv->login_status = RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_CONNECTION_ERROR;
		g_signal_emit (account, rb_audioscrobbler_account_signals[LOGIN_STATUS_CHANGED],
		               0, account->priv->login_status);
	}

	g_object_unref (parser);
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
			ENUM_ENTRY (RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_AUTH_ERROR, "Authentication Error"),
			ENUM_ENTRY (RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_CONNECTION_ERROR, "Connection Error"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBAudioscrobblerAccountLoginStatus", values);
	}

	return etype;
}

void
_rb_audioscrobbler_account_register_type (GTypeModule *module)
{
	rb_audioscrobbler_account_register_type (module);
}
