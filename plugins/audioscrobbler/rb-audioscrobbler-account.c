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

#include "rb-audioscrobbler-account.h"
#include "rb-builder-helpers.h"
#include "rb-debug.h"
#include "rb-util.h"


struct _RBAudioscrobblerAccountPrivate
{
	RBShell *shell;

	/* Widgets for the prefs pane */
	GtkWidget *config_widget;
	GtkWidget *username_entry;
	GtkWidget *username_label;
	GtkWidget *auth_link;
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
}

static void
rb_audioscrobbler_account_dispose (GObject *object)
{
	G_OBJECT_CLASS (rb_audioscrobbler_account_parent_class)->dispose (object);
}

static void
rb_audioscrobbler_account_finalize (GObject *object)
{
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

	return account->priv->config_widget;
}
