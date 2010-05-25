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

#include <gconf/gconf.h>
#include "eel-gconf-extensions.h"

#include "rb-audioscrobbler-account.h"
#include "rb-builder-helpers.h"
#include "rb-debug.h"
#include "rb-util.h"


struct _RBAudioscrobblerAccountPrivate
{
	RBShell *shell;

	/* Authentication info */
	gchar* username;

	/* Widgets for the prefs pane */
	GtkWidget *config_widget;
	GtkWidget *username_entry;
	GtkWidget *username_label;
	GtkWidget *auth_link;

	/* Preference notifications */
	guint notification_username_id;
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

static void          rb_audioscrobbler_account_import_settings (RBAudioscrobblerAccount *account);
static void          rb_audioscrobbler_account_preferences_sync (RBAudioscrobblerAccount *account);

static void          rb_audioscrobbler_account_gconf_changed_cb (GConfClient *client,
                                                                 guint cnxn_id,
                                                                 GConfEntry *entry,
                                                                 RBAudioscrobblerAccount *account);

enum
{
	PROP_0,
	PROP_SHELL,
};

G_DEFINE_TYPE (RBAudioscrobblerAccount, rb_audioscrobbler_account, G_TYPE_OBJECT)

static void
rb_audioscrobbler_account_constructed (GObject *object)
{
	RB_CHAIN_GOBJECT_METHOD (rb_audioscrobbler_account_parent_class, constructed, object);
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

	rb_audioscrobbler_account_import_settings (account);

	account->priv->notification_username_id =
		eel_gconf_notification_add (CONF_AUDIOSCROBBLER_USERNAME,
		                            (GConfClientNotifyFunc) rb_audioscrobbler_account_gconf_changed_cb,
		                            account);

	rb_audioscrobbler_account_preferences_sync (account);
}

static void
rb_audioscrobbler_account_dispose (GObject *object)
{
	RBAudioscrobblerAccount *account;

	account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	if (account->priv->notification_username_id != 0) {
		eel_gconf_notification_remove (account->priv->notification_username_id);
		account->priv->notification_username_id = 0;
	}

	G_OBJECT_CLASS (rb_audioscrobbler_account_parent_class)->dispose (object);
}

static void
rb_audioscrobbler_account_finalize (GObject *object)
{
	RBAudioscrobblerAccount *account;

	account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	g_free (account->priv->username);

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
rb_audioscrobbler_account_import_settings (RBAudioscrobblerAccount *account)
{
	/* import gconf settings. */
	g_free (account->priv->username);
	account->priv->username = eel_gconf_get_string (CONF_AUDIOSCROBBLER_USERNAME);
}

static void
rb_audioscrobbler_account_preferences_sync (RBAudioscrobblerAccount *account)
{
	char *v;

	if (account->priv->config_widget == NULL)
		return;

	rb_debug ("Syncing data with preferences window");

	v = account->priv->username;
	gtk_entry_set_text (GTK_ENTRY (account->priv->username_entry),
	                    v ? v : "");
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
	account->priv->username_entry = GTK_WIDGET (gtk_builder_get_object (builder, "username_entry"));
	account->priv->username_label = GTK_WIDGET (gtk_builder_get_object (builder, "username_label"));
	account->priv->auth_link = GTK_WIDGET (gtk_builder_get_object (builder, "auth_link"));

	rb_builder_boldify_label (builder, "audioscrobbler_label");

	rb_audioscrobbler_account_preferences_sync (account);

	return account->priv->config_widget;
}

static void
rb_audioscrobbler_account_gconf_changed_cb (GConfClient *client,
                                            guint cnxn_id,
                                            GConfEntry *entry,
                                            RBAudioscrobblerAccount *account)
{
	rb_debug ("GConf key updated: \"%s\"", entry->key);
	if (strcmp (entry->key, CONF_AUDIOSCROBBLER_USERNAME) == 0) {
		const char *username;

		username = gconf_value_get_string (entry->value);
		if (rb_safe_strcmp (username, account->priv->username) == 0) {
			rb_debug ("username not modified");
			return;
		}

		g_free (account->priv->username);
		account->priv->username = NULL;

		if (username != NULL) {
			account->priv->username = g_strdup (username);
		}

		if (account->priv->username_entry) {
			char *v = account->priv->username;
			gtk_entry_set_text (GTK_ENTRY (account->priv->username_entry),
					    v ? v : "");
		}
	} else {
		rb_debug ("Unhandled GConf key updated: \"%s\"", entry->key);
	}
}

void
rb_audioscrobbler_account_username_entry_focus_out_event_cb (GtkWidget *widget,
                                                             RBAudioscrobblerAccount *account)
{
	eel_gconf_set_string (CONF_AUDIOSCROBBLER_USERNAME,
                              gtk_entry_get_text (GTK_ENTRY (widget)));
}

void
rb_audioscrobbler_account_username_entry_activate_cb (GtkEntry *entry,
                                                      RBAudioscrobblerAccount *account)
{
	gtk_widget_grab_focus (account->priv->auth_link);
}
