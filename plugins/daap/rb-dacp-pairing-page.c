/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Implementation of DACP (iTunes Remote) pairing page object
 *
 *  Copyright (C) 2010 Alexandre Rosenfeld <alexandre.rosenfeld@gmail.com>
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
#include <ctype.h>
#include <math.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rhythmdb.h"
#include "rb-shell.h"
#include "rb-display-page-group.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-dialog.h"
#include "rb-playlist-manager.h"
#include "rb-shell-player.h"
#include "rb-display-page-model.h"
#include "rb-rhythmdb-dmap-db-adapter.h"
#include "rb-dmap-container-db-adapter.h"

#include "rb-dacp-pairing-page.h"
#include "rb-daap-plugin.h"
#include "rb-daap-sharing.h"
#include "rb-dacp-player.h"

static void impl_constructed (GObject *object);
static void impl_dispose (GObject *object);
static void impl_set_property  (GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec);
static void impl_get_property  (GObject *object,
				guint prop_id,
				GValue *value,
				GParamSpec *pspec);

static void rb_dacp_pairing_page_connecting (RBDACPPairingPage *page, gboolean connecting);
static gboolean entry_insert_text_cb (GtkWidget *entry, gchar *text, gint len, gint *position, RBDACPPairingPage *page);
static gboolean entry_backspace_cb (GtkWidget *entry, RBDACPPairingPage *page);
static void remote_paired_cb (DmapControlShare *share, gchar *service_name, gboolean connected, RBDACPPairingPage *page);

static void dacp_remote_added (DmapControlShare *share, gchar *service_name, gchar *display_name, RBDaapPlugin *plugin);
static void dacp_remote_removed (DmapControlShare *share, gchar *service_name, RBDaapPlugin *plugin);

/* DmapControlShare signals */
static gboolean dacp_lookup_guid (DmapControlShare *share, gchar *guid, GSettings *settings);
static void     dacp_add_guid    (DmapControlShare *share, gchar *guid, GSettings *settings);

static void dacp_player_updated (RBDACPPlayer *player, DmapControlShare *share);

struct RBDACPPairingPagePrivate
{
	char *service_name;

	gboolean done_pairing;

	DmapControlShare *dacp_share;

	GtkBuilder *builder;
	GtkWidget *entries[4];
	GtkWidget *finished_widget;
	GtkWidget *pairing_widget;
	GtkWidget *pairing_status_widget;
};

enum {
	PROP_0,
	PROP_SERVICE_NAME
};

G_DEFINE_DYNAMIC_TYPE (RBDACPPairingPage, rb_dacp_pairing_page, RB_TYPE_DISPLAY_PAGE)

static gboolean
entry_insert_text_cb (GtkWidget *entry, gchar *text, gint len, gint *position, RBDACPPairingPage *page)
{
	gchar new_char = text[*position];
	gint entry_pos = 0;
	gchar passcode[4];
	int i;

	/* Find out which entry the user just entered text */
	for (entry_pos = 0; entry_pos < 4; entry_pos++) {
		if (entry == page->priv->entries[entry_pos]) {
			break;
		}
	}

	if (!isdigit (new_char)) {
		/* is this a number? If not, don't let it in */
		g_signal_stop_emission_by_name (entry, "insert-text");
		return TRUE;
	}
	if (entry_pos < 3) {
		/* Focus the next entry */
		gtk_widget_grab_focus (page->priv->entries[entry_pos + 1]);
	} else if (entry_pos == 3) {
		/* The user entered all 4 characters of the passcode, so let's pair */
		for (i = 0; i < 3; i++) {
			const gchar *text = gtk_entry_get_text (GTK_ENTRY (page->priv->entries[i]));
			passcode[i] = text[0];
		}
		/* The last character is still not in the entry */
		passcode[3] = new_char;
		rb_dacp_pairing_page_connecting (page, TRUE);
		/* Let DmapControlShare do the heavy-lifting */
		dmap_control_share_pair (page->priv->dacp_share,
		                         page->priv->service_name,
		                         passcode);
	}
	/* let the default handler display the number */
	return FALSE;
}

static gboolean
entry_backspace_cb (GtkWidget *entry, RBDACPPairingPage *page)
{
	gint entry_pos = 0;

	/* Find out which entry the user just entered text */
	for (entry_pos = 0; entry_pos < 4; entry_pos++) {
		if (entry == page->priv->entries[entry_pos]) {
			break;
		}
	}

	if (entry_pos > 0) {
		gtk_entry_set_text (GTK_ENTRY (page->priv->entries[entry_pos]), "");
		/* Focus the previous entry */
		gtk_widget_grab_focus (page->priv->entries[entry_pos - 1]);
	}

	return FALSE;
}

static gboolean
close_pairing_clicked_cb (GtkButton *button, RBDACPPairingPage *page)
{
	rb_display_page_delete_thyself (RB_DISPLAY_PAGE (page));
	return FALSE;
}

static void
impl_dispose (GObject *object)
{
	RBDACPPairingPage *page = RB_DACP_PAIRING_PAGE (object);

	if (page->priv->builder != NULL) {
		g_object_unref (page->priv->builder);
		page->priv->builder = NULL;
	}

	if (page->priv->dacp_share != NULL) {
		g_object_unref (page->priv->dacp_share);
		page->priv->dacp_share = NULL;
	}

	G_OBJECT_CLASS (rb_dacp_pairing_page_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	RBDACPPairingPage *page = RB_DACP_PAIRING_PAGE (object);

	g_free (page->priv->service_name);

	G_OBJECT_CLASS (rb_dacp_pairing_page_parent_class)->finalize (object);
}

static void
rb_dacp_pairing_page_class_init (RBDACPPairingPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed  = impl_constructed;
	object_class->dispose      = impl_dispose;
	object_class->finalize     = impl_finalize;
	object_class->get_property = impl_get_property;
	object_class->set_property = impl_set_property;

	g_object_class_install_property (object_class,
					 PROP_SERVICE_NAME,
					 g_param_spec_string ("service-name",
							      "Service name",
							      "mDNS/DNS-SD service name of the share",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBDACPPairingPagePrivate));
}

static void
rb_dacp_pairing_page_class_finalize (RBDACPPairingPageClass *klass)
{
}

static void
rb_dacp_pairing_page_init (RBDACPPairingPage *page)
{
	page->priv = G_TYPE_INSTANCE_GET_PRIVATE (page,
						  RB_TYPE_DACP_PAIRING_PAGE,
						  RBDACPPairingPagePrivate);
}

static void
impl_constructed (GObject *object)
{
	RBDACPPairingPage *page = RB_DACP_PAIRING_PAGE (object);
	GtkWidget *passcode_widget;
	GtkWidget *close_pairing_button;
	PangoFontDescription *font;
	GObject *plugin;
	int i;

	g_object_get (page, "plugin", &plugin, NULL);

	page->priv->builder = rb_builder_load_plugin_file (G_OBJECT (plugin), "daap-prefs.ui", NULL);

	passcode_widget = GTK_WIDGET (gtk_builder_get_object (page->priv->builder, "passcode_widget"));
	gtk_container_add (GTK_CONTAINER (page), passcode_widget);

	close_pairing_button = GTK_WIDGET (gtk_builder_get_object (page->priv->builder, "close_pairing_button"));
	g_signal_connect_object (close_pairing_button, "clicked", G_CALLBACK (close_pairing_clicked_cb), page, 0);

	page->priv->finished_widget = GTK_WIDGET (gtk_builder_get_object (page->priv->builder, "finished_widget"));
	page->priv->pairing_widget = GTK_WIDGET (gtk_builder_get_object (page->priv->builder, "pairing_widget"));
	page->priv->pairing_status_widget = GTK_WIDGET (gtk_builder_get_object (page->priv->builder, "pairing_status_widget"));

	font = pango_font_description_from_string ("normal 28");

	for (i = 0; i < 4; i++) {
		char *entry_name;

		entry_name = g_strdup_printf ("passcode_entry%d", i + 1);
		page->priv->entries[i] = GTK_WIDGET (gtk_builder_get_object (page->priv->builder, entry_name));
		gtk_widget_override_font (page->priv->entries[i], font);
		g_signal_connect_object (page->priv->entries[i],
		                         "insert-text",
		                         G_CALLBACK (entry_insert_text_cb),
		                         page,
		                         0);
		g_signal_connect_object (page->priv->entries[i],
		                         "backspace",
		                         G_CALLBACK (entry_backspace_cb),
		                         page,
		                         0);
		g_free (entry_name);
	}

	pango_font_description_free (font);

	gtk_widget_show (passcode_widget);

	g_object_unref (plugin);
}

static void
impl_set_property (GObject *object,
		   guint prop_id,
		   const GValue *value,
		   GParamSpec *pspec)
{
	RBDACPPairingPage *page = RB_DACP_PAIRING_PAGE (object);

	switch (prop_id) {
	case PROP_SERVICE_NAME:
		page->priv->service_name = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object,
		   guint prop_id,
		   GValue *value,
		   GParamSpec *pspec)
{
	RBDACPPairingPage *page = RB_DACP_PAIRING_PAGE (object);

	switch (prop_id) {
	case PROP_SERVICE_NAME:
		g_value_set_string (value, page->priv->service_name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBDACPPairingPage *
rb_dacp_pairing_page_new (GObject *plugin,
			  RBShell *shell,
			  DmapControlShare *dacp_share,
			  const char *display_name,
			  const char *service_name)
{
	RBDACPPairingPage *page;

	page = RB_DACP_PAIRING_PAGE (g_object_new (RB_TYPE_DACP_PAIRING_PAGE,
						   "name", display_name,
						   "service-name", service_name,
						   "icon", g_themed_icon_new ("phone-symbolic"),
						   "shell", shell,
						   "plugin", plugin,
						   NULL));

	g_object_ref (dacp_share);
	page->priv->dacp_share = dacp_share;
	/* Retrieve notifications when the remote is finished pairing */
	g_signal_connect_object (dacp_share, "remote-paired", G_CALLBACK (remote_paired_cb), page, 0);

	return page;
}


static void
rb_dacp_pairing_page_reset_passcode (RBDACPPairingPage *page)
{
	int i;

	for (i = 0; i < 4; i++) {
		gtk_entry_set_text (GTK_ENTRY (page->priv->entries[i]), "");
	}
	gtk_widget_grab_focus (page->priv->entries [0]);
}

void
rb_dacp_pairing_page_remote_found (RBDACPPairingPage *page)
{
	if (page->priv->done_pairing) {
		rb_dacp_pairing_page_reset_passcode (page);
		gtk_widget_show (page->priv->pairing_widget);
		gtk_widget_hide (page->priv->pairing_status_widget);
		gtk_widget_hide (page->priv->finished_widget);
		page->priv->done_pairing = FALSE;
	}
}

void
rb_dacp_pairing_page_remote_lost (RBDACPPairingPage *page)
{
	if (!page->priv->done_pairing) {
		rb_display_page_delete_thyself (RB_DISPLAY_PAGE (page));
	}
}

static void
rb_dacp_pairing_page_connecting (RBDACPPairingPage *page, gboolean connecting) {
	int i;

	if (connecting) {
		gtk_widget_show (page->priv->pairing_status_widget);
		gtk_label_set_markup (GTK_LABEL (page->priv->pairing_status_widget), _("Connecting..."));
	} else {
		gtk_label_set_markup (GTK_LABEL (page->priv->pairing_status_widget), _("Could not pair with this Remote."));
	}

	for (i = 0; i < 4; i++) {
		gtk_widget_set_sensitive (page->priv->entries [i], !connecting);
	}
}

static void
remote_paired_cb (DmapControlShare *share, gchar *service_name, gboolean connected, RBDACPPairingPage *page)
{
	/* Check if this remote is the remote paired */
	if (g_strcmp0 (service_name, page->priv->service_name) != 0)
		return;

	rb_dacp_pairing_page_connecting (page, FALSE);
	if (connected) {
		gtk_widget_hide (page->priv->pairing_widget);
		gtk_widget_show (page->priv->finished_widget);
		page->priv->done_pairing = TRUE;
	} else {
		gtk_widget_show (page->priv->pairing_status_widget);
		rb_dacp_pairing_page_reset_passcode (page);
	}
}

DmapControlShare *
rb_daap_create_dacp_share (GObject *plugin)
{
	DmapControlShare *share;
	DmapControlPlayer *player;
	RhythmDB *rdb;
	DmapDb *db;
	DmapContainerDb *container_db;
	RBPlaylistManager *playlist_manager;
	RBShell *shell;
	GSettings *share_settings;
	GSettings *daap_settings;
	GSettings *settings;
	gchar *name;

	g_object_get (plugin, "object", &shell, NULL);

	g_object_get (shell,
	              "db", &rdb,
	              "playlist-manager", &playlist_manager,
	              NULL);
	db = DMAP_DB (rb_rhythmdb_dmap_db_adapter_new (rdb, RHYTHMDB_ENTRY_TYPE_SONG));
	container_db = DMAP_CONTAINER_DB (rb_dmap_container_db_adapter_new (playlist_manager));

	player = DMAP_CONTROL_PLAYER (rb_dacp_player_new (shell));

	share_settings = g_settings_new ("org.gnome.rhythmbox.sharing");
	name = g_settings_get_string (share_settings, "share-name");
	if (name == NULL || *name == '\0') {
		g_free (name);
		name = rb_daap_sharing_default_share_name ();
	}
	g_object_unref (share_settings);

	share = dmap_control_share_new (name, player, db, container_db);

	daap_settings = g_settings_new ("org.gnome.rhythmbox.plugins.daap");
	settings = g_settings_get_child (daap_settings, "dacp");
	g_object_unref (daap_settings);

	g_signal_connect_object (share,
				 "add-guid",
				 G_CALLBACK (dacp_add_guid),
				 settings,
				 0);
	g_signal_connect_object (share,
				 "lookup-guid",
				 G_CALLBACK (dacp_lookup_guid),
				 settings,
				 0);

	g_signal_connect_object (share,
				 "remote-found",
				 G_CALLBACK (dacp_remote_added),
				 RB_DAAP_PLUGIN (plugin),
				 0);
	g_signal_connect_object (share,
				 "remote-lost",
				 G_CALLBACK (dacp_remote_removed),
				 RB_DAAP_PLUGIN (plugin),
				 0);

	g_signal_connect_object (player,
	                         "player-updated",
	                         G_CALLBACK (dacp_player_updated),
	                         share,
	                         0);

	g_object_unref (db);
	g_object_unref (container_db);
	g_object_unref (rdb);
	g_object_unref (playlist_manager);
	g_object_unref (player);
	g_object_unref (shell);

	return share;
}

static void
dacp_player_updated (RBDACPPlayer *player,
                     DmapControlShare *share)
{
	dmap_control_share_player_updated (share);
}

static void
dacp_add_guid (DmapControlShare *share,
               gchar *guid,
	       GSettings *settings)
{
	GVariantBuilder *vb;
	GVariantIter iter;
	GVariant *v;
	const char *g;

	v = g_settings_get_value (settings, "known-remotes");

	vb = g_variant_builder_new (G_VARIANT_TYPE ("as"));
	g_variant_iter_init (&iter, v);
	while (g_variant_iter_loop (&iter, "s", &g)) {
		g_variant_builder_add (vb, "s", g);
	}

	g_variant_builder_add (vb, "s", guid);
	g_variant_unref (v);

	g_settings_set_value (settings, "known-remotes", g_variant_builder_end (vb));
	g_variant_builder_unref (vb);
}

static gboolean
dacp_lookup_guid (DmapControlShare *share,
                  gchar *guid,
		  GSettings *settings)
{
	char **guids;
	gboolean found;

	guids = g_settings_get_strv (settings, "known-remotes");
	found = rb_str_in_strv (guid, (const char **)guids);
	g_strfreev (guids);

	return found;
}

typedef struct {
	const char *name;
	RBDACPPairingPage *page;
} FindPage;

static gboolean
find_dacp_page_foreach (GtkTreeModel *model,
                        GtkTreePath  *path,
                        GtkTreeIter  *iter,
                        FindPage     *fp)
{
	gchar *name;
	RBDisplayPage *page;

	gtk_tree_model_get (model, iter,
	                    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
	                    -1);
	if (page && RB_IS_DACP_PAIRING_PAGE (page)) {
		g_object_get (page, "service-name", &name, NULL);
		if (strcmp (name, fp->name) == 0) {
			fp->page = RB_DACP_PAIRING_PAGE (page);
		}
		g_free (name);
	}

	return (fp->page != NULL);
}

static RBDACPPairingPage *
find_dacp_page (RBShell *shell, const gchar *service_name)
{
	RBDisplayPageModel *page_model;
	FindPage find_page;

	find_page.name = service_name;
	find_page.page = NULL;

	g_object_get (shell, "display-page-model", &page_model, NULL);

	gtk_tree_model_foreach (GTK_TREE_MODEL (page_model),
	                        (GtkTreeModelForeachFunc) find_dacp_page_foreach,
	                        &find_page);

	return find_page.page;
}

static void
dacp_remote_added (DmapControlShare *share,
                   gchar            *service_name,
                   gchar            *display_name,
                   RBDaapPlugin     *plugin)
{
	RBDACPPairingPage *page;
	RBShell *shell;

	rb_debug ("Remote %s (%s) found", service_name, display_name);

	g_object_get (plugin, "object", &shell, NULL);

	page = find_dacp_page (shell, service_name);
	if (page == NULL) {
		RBDisplayPageGroup *page_group;

		page_group = rb_display_page_group_get_by_id ("remotes");
		if (page_group == NULL) {
			page_group = rb_display_page_group_new (G_OBJECT (shell),
								"remotes",
								_("Remotes"),
								RB_DISPLAY_PAGE_GROUP_CATEGORY_TRANSIENT);
			rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (page_group), NULL);
		}

		page = rb_dacp_pairing_page_new (G_OBJECT (plugin), shell, share, display_name, service_name);

		rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (page), RB_DISPLAY_PAGE (page_group));
	} else {
		rb_dacp_pairing_page_remote_found (page);
	}

	g_object_unref (shell);
}

static void
dacp_remote_removed (DmapControlShare *share,
                     gchar            *service_name,
                     RBDaapPlugin     *plugin)
{
	RBDACPPairingPage *page;
	RBShell *shell;

	rb_debug ("Remote '%s' went away", service_name);

	g_object_get (plugin, "object", &shell, NULL);

	page = find_dacp_page (shell, service_name);
	if (page != NULL) {
		rb_dacp_pairing_page_remote_lost (page);
	}

	g_object_unref (shell);
}

void
_rb_dacp_pairing_page_register_type (GTypeModule *module)
{
	rb_dacp_pairing_page_register_type (module);
}
