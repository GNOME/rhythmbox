/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Implementation of DACP (iTunes Remote) source object
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
#include "rb-source-group.h"
#include "eel-gconf-extensions.h"
#include "rb-stock-icons.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-dialog.h"
#include "rb-preferences.h"
#include "rb-playlist-manager.h"
#include "rb-shell-player.h"
#include "rb-sourcelist-model.h"
#include "rb-rhythmdb-dmap-db-adapter.h"
#include "rb-dmap-container-db-adapter.h"

#include "rb-daap-plugin.h"
#include "rb-daap-sharing.h"
#include "rb-dacp-player.h"

#include <libdmapsharing/dmap.h>

#include "rb-dacp-source.h"

static void rb_dacp_source_dispose (GObject *object);
static void rb_dacp_source_set_property  (GObject *object,
					  guint prop_id,
					  const GValue *value,
					  GParamSpec *pspec);
static void rb_dacp_source_get_property  (GObject *object,
					  guint prop_id,
					  GValue *value,
					  GParamSpec *pspec);

static void rb_dacp_source_connecting (RBDACPSource *source, gboolean connecting);
static gboolean entry_insert_text_cb (GtkWidget *entry, gchar *text, gint len, gint *position, RBDACPSource *rb_dacp_source);
static gboolean entry_backspace_cb (GtkWidget *entry, RBDACPSource *rb_dacp_source);
static void rb_dacp_source_create_ui (RBDACPSource *source, RBPlugin *plugin);
static void remote_paired_cb (DACPShare *share, gchar *service_name, gboolean connected, RBDACPSource *source);

static void dacp_remote_added (DACPShare *share, gchar *service_name, gchar *display_name, RBDaapPlugin *plugin);
static void dacp_remote_removed (DACPShare *share, gchar *service_name, RBDaapPlugin *plugin);

/* DACPShare signals */
static gboolean dacp_lookup_guid (DACPShare *share, gchar *guid);
static void     dacp_add_guid    (DACPShare *share, gchar *guid);

static void dacp_player_updated (RBDACPPlayer *player, DACPShare *share);

struct RBDACPSourcePrivate
{
	char *service_name;

	gboolean done_pairing;

	DACPShare *dacp_share;

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

G_DEFINE_TYPE (RBDACPSource, rb_dacp_source, RB_TYPE_SOURCE)

static gboolean
entry_insert_text_cb (GtkWidget *entry, gchar *text, gint len, gint *position, RBDACPSource *source)
{
	gchar new_char = text[*position];
	gint entry_pos = 0;
	gchar passcode[4];
	int i;

	/* Find out which entry the user just entered text */
	for (entry_pos = 0; entry_pos < 4; entry_pos++) {
		if (entry == source->priv->entries[entry_pos]) {
			break;
		}
	}

	if (!isdigit (new_char)) {
		/* is this a number? If not, don't let it in */
		g_signal_stop_emission_by_name(entry, "insert-text");
		return TRUE;
	}
	if (entry_pos < 3) {
		/* Focus the next entry */
		gtk_widget_grab_focus(source->priv->entries[entry_pos + 1]);
	} else if (entry_pos == 3) {
		/* The user entered all 4 characters of the passcode, so let's pair */
		for (i = 0; i < 3; i++) {
			const gchar *text = gtk_entry_get_text (GTK_ENTRY (source->priv->entries[i]));
			passcode[i] = text[0];
		}
		/* The last character is still not in the entry */
		passcode[3] = new_char;
		rb_dacp_source_connecting (source, TRUE);
		/* Let DACPShare do the heavy-lifting */
		dacp_share_pair(source->priv->dacp_share,
		                source->priv->service_name,
		                passcode);
	}
	/* let the default handler display the number */
	return FALSE;
}

static gboolean
entry_backspace_cb (GtkWidget *entry, RBDACPSource *rb_dacp_source)
{
	gint entry_pos = 0;

	/* Find out which entry the user just entered text */
	for (entry_pos = 0; entry_pos < 4; entry_pos++) {
		if (entry == rb_dacp_source->priv->entries[entry_pos]) {
			break;
		}
	}

	if (entry_pos > 0) {
		gtk_entry_set_text (GTK_ENTRY (rb_dacp_source->priv->entries[entry_pos]), "");
		/* Focus the previous entry */
		gtk_widget_grab_focus (rb_dacp_source->priv->entries[entry_pos - 1]);
	}

	return FALSE;
}

static gboolean
close_pairing_clicked_cb (GtkButton *button, RBDACPSource *source)
{
	rb_source_delete_thyself (RB_SOURCE (source));

	return FALSE;
}

static void
rb_dacp_source_dispose (GObject *object)
{
	G_OBJECT_CLASS (rb_dacp_source_parent_class)->dispose (object);
}

static void
rb_dacp_source_finalize (GObject *object)
{
	RBDACPSource *source = RB_DACP_SOURCE (object);

	g_free (source->priv->service_name);
	g_object_unref (source->priv->builder);
	g_object_unref (source->priv->dacp_share);

	G_OBJECT_CLASS (rb_dacp_source_parent_class)->finalize (object);
}

static void
rb_dacp_source_class_init (RBDACPSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose      = rb_dacp_source_dispose;
	object_class->finalize     = rb_dacp_source_finalize;
	object_class->get_property = rb_dacp_source_get_property;
	object_class->set_property = rb_dacp_source_set_property;

	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;

	g_object_class_install_property (object_class,
					 PROP_SERVICE_NAME,
					 g_param_spec_string ("service-name",
							      "Service name",
							      "mDNS/DNS-SD service name of the share",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBDACPSourcePrivate));
}

static void
rb_dacp_source_init (RBDACPSource *source)
{
	source->priv = G_TYPE_INSTANCE_GET_PRIVATE (source,
						    RB_TYPE_DACP_SOURCE,
						    RBDACPSourcePrivate);
}

static void
rb_dacp_source_set_property (GObject *object,
			    guint prop_id,
			    const GValue *value,
			    GParamSpec *pspec)
{
	RBDACPSource *source = RB_DACP_SOURCE (object);

	switch (prop_id) {
		case PROP_SERVICE_NAME:
			source->priv->service_name = g_value_dup_string (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
rb_dacp_source_get_property (GObject *object,
			    guint prop_id,
			    GValue *value,
			    GParamSpec *pspec)
{
	RBDACPSource *source = RB_DACP_SOURCE (object);

	switch (prop_id) {
		case PROP_SERVICE_NAME:
			g_value_set_string (value, source->priv->service_name);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

RBDACPSource *
rb_dacp_source_new (RBPlugin *plugin,
                    RBShell *shell,
                    DACPShare *dacp_share,
                    const char *display_name,
                    const char *service_name)
{
	RBDACPSource *source;
	RhythmDB *db;
	RhythmDBQueryModel *query_model;
	RBSourceGroup *source_group;

	/* Source icon data */
	gchar *icon_filename;
	gint icon_size;
	GdkPixbuf *icon_pixbuf;

	icon_filename = rb_plugin_find_file (plugin, "remote-icon.png");
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &icon_size, NULL);
	icon_pixbuf = gdk_pixbuf_new_from_file_at_size (icon_filename, icon_size, icon_size, NULL);

	/* Remotes category */
	source_group = rb_source_group_get_by_name ("remotes");
	if (source_group == NULL) {
		source_group = rb_source_group_register ("remotes",
		                                         _("Remotes"),
		                                         RB_SOURCE_GROUP_CATEGORY_TRANSIENT);
	}

	/* This stops some assertions failing due to no query-model */
	g_object_get (shell, "db", &db, NULL);
	query_model = rhythmdb_query_model_new_empty (db);

	source = RB_DACP_SOURCE (g_object_new (RB_TYPE_DACP_SOURCE,
					  "name", display_name,
					  "service-name", service_name,
					  "icon", icon_pixbuf,
					  "shell", shell,
					  "visibility", TRUE,
					  "entry-type", RHYTHMDB_ENTRY_TYPE_IGNORE,
					  "source-group", source_group,
					  "plugin", plugin,
					  "query-model", query_model,
					  NULL));

	g_object_ref (dacp_share);
	source->priv->dacp_share = dacp_share;
	/* Retrieve notifications when the remote is finished pairing */
	g_signal_connect_object (dacp_share, "remote-paired", G_CALLBACK (remote_paired_cb), source, 0);

	g_free (icon_filename);
	g_object_unref (icon_pixbuf);

	g_object_unref (db);
	g_object_unref (query_model);

	rb_dacp_source_create_ui (source, plugin);

	return source;
}

static void
rb_dacp_source_create_ui (RBDACPSource *source, RBPlugin *plugin)
{
	gchar *builder_filename;
	GtkButton *close_pairing_button;
	PangoFontDescription *font;
	int i;

	builder_filename = rb_plugin_find_file (RB_PLUGIN (plugin), "daap-prefs.ui");
	g_return_if_fail (builder_filename != NULL);

	source->priv->builder = rb_builder_load (builder_filename, NULL);
	g_free (builder_filename);

	GtkWidget *passcode_widget = GTK_WIDGET (gtk_builder_get_object (source->priv->builder, "passcode_widget"));
	gtk_container_add(GTK_CONTAINER(source), passcode_widget);

	close_pairing_button = GTK_BUTTON (gtk_builder_get_object (source->priv->builder, "close_pairing_button"));
	g_signal_connect_object (close_pairing_button, "clicked", G_CALLBACK (close_pairing_clicked_cb), source, 0);

	source->priv->finished_widget = GTK_WIDGET (gtk_builder_get_object (source->priv->builder, "finished_widget"));
	source->priv->pairing_widget = GTK_WIDGET (gtk_builder_get_object (source->priv->builder, "pairing_widget"));
	source->priv->pairing_status_widget = GTK_WIDGET (gtk_builder_get_object (source->priv->builder, "pairing_status_widget"));

	font = pango_font_description_from_string ("normal 28");

	for (i = 0; i < 4; i++) {
		gchar *entry_name = g_strdup_printf ("passcode_entry%d", i + 1);
		source->priv->entries[i] = GTK_WIDGET (gtk_builder_get_object (source->priv->builder, entry_name));
		gtk_widget_modify_font (source->priv->entries[i], font);
		g_signal_connect_object (source->priv->entries[i],
		                         "insert-text",
		                         G_CALLBACK (entry_insert_text_cb),
		                         source,
		                         0);
		g_signal_connect_object (source->priv->entries[i],
		                         "backspace",
		                         G_CALLBACK (entry_backspace_cb),
		                         source,
		                         0);
		g_free(entry_name);
	}

	pango_font_description_free (font);

	gtk_widget_show(passcode_widget);
}


static void
rb_dacp_source_reset_passcode (RBDACPSource *source)
{
	int i;

	for (i = 0; i < 4; i++) {
		gtk_entry_set_text (GTK_ENTRY (source->priv->entries[i]), "");
	}
	gtk_widget_grab_focus (source->priv->entries [0]);
}

void
rb_dacp_source_remote_found (RBDACPSource *source)
{
	if (source->priv->done_pairing) {
		rb_dacp_source_reset_passcode (source);
		gtk_widget_show (source->priv->pairing_widget);
		gtk_widget_hide (source->priv->pairing_status_widget);
		gtk_widget_hide (source->priv->finished_widget);
		source->priv->done_pairing = FALSE;
	}
}

void
rb_dacp_source_remote_lost (RBDACPSource *source)
{
	if (!source->priv->done_pairing) {
		rb_source_delete_thyself (RB_SOURCE (source));
	}
}

static void
rb_dacp_source_connecting (RBDACPSource *source, gboolean connecting) {
	int i;

	if (connecting) {
		gtk_widget_show (source->priv->pairing_status_widget);
		gtk_label_set_markup (GTK_LABEL (source->priv->pairing_status_widget), _("Connecting..."));
	} else {
		gtk_label_set_markup (GTK_LABEL (source->priv->pairing_status_widget), _("Could not pair with this Remote."));
	}

	for (i = 0; i < 4; i++) {
		gtk_widget_set_sensitive (source->priv->entries [i], !connecting);
	}
}

static void
remote_paired_cb (DACPShare *share, gchar *service_name, gboolean connected, RBDACPSource *source)
{
	/* Check if this remote is the remote paired */
	if (g_strcmp0(service_name, source->priv->service_name) != 0)
		return;
	rb_dacp_source_connecting (source, FALSE);
	if (connected) {
		gtk_widget_hide (source->priv->pairing_widget);
		gtk_widget_show (source->priv->finished_widget);
		source->priv->done_pairing = TRUE;
	} else {
		gtk_widget_show (source->priv->pairing_status_widget);
		rb_dacp_source_reset_passcode (source);
	}
}

DACPShare *
rb_daap_create_dacp_share (RBPlugin *plugin)
{
	DACPShare *share;
	DACPPlayer *player;
	RhythmDB *rdb;
	DMAPDb *db;
	DMAPContainerDb *container_db;
	RBPlaylistManager *playlist_manager;
	RBShell *shell;
	gchar *name;

	g_object_get (plugin, "shell", &shell, NULL);

	g_object_get (shell,
	              "db", &rdb,
	              "playlist-manager", &playlist_manager,
	              NULL);
	db = DMAP_DB (rb_rhythmdb_dmap_db_adapter_new (rdb, RHYTHMDB_ENTRY_TYPE_SONG));
	container_db = DMAP_CONTAINER_DB (rb_dmap_container_db_adapter_new (playlist_manager));

	player = DACP_PLAYER (rb_dacp_player_new (shell));

	name = eel_gconf_get_string (CONF_DAAP_SHARE_NAME);
	if (name == NULL || *name == '\0') {
		g_free (name);
		name = rb_daap_sharing_default_share_name ();
	}

	share = dacp_share_new (name, player, db, container_db);

	g_signal_connect_object (share,
				 "add-guid",
				 G_CALLBACK (dacp_add_guid),
				 RB_DAAP_PLUGIN (plugin),
				 0);
	g_signal_connect_object (share,
				 "lookup-guid",
				 G_CALLBACK (dacp_lookup_guid),
				 RB_DAAP_PLUGIN (plugin),
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

	return share;
}

static void
dacp_player_updated (RBDACPPlayer *player,
                     DACPShare *share)
{
	dacp_share_player_updated (share);
}

static void
dacp_add_guid (DACPShare *share,
               gchar *guid)
{
	GSList *known_guids;

	known_guids = eel_gconf_get_string_list (CONF_KNOWN_REMOTES);
	if (g_slist_find_custom (known_guids, guid, (GCompareFunc) g_strcmp0)) {
		g_slist_free (known_guids);
		return;
	}
	known_guids = g_slist_insert_sorted (known_guids, guid, (GCompareFunc) g_strcmp0);
	eel_gconf_set_string_list (CONF_KNOWN_REMOTES, known_guids);

	g_slist_free (known_guids);
}

static gboolean
dacp_lookup_guid (DACPShare *share,
                  gchar *guid)
{
	GSList *known_guids;
	int found;

	known_guids = eel_gconf_get_string_list (CONF_KNOWN_REMOTES);
	found = g_slist_find_custom (known_guids, guid, (GCompareFunc) g_strcmp0) != NULL;

	g_slist_free (known_guids);

	return found;
}

/* Remote sources */
typedef struct {
	const gchar *name;
	gboolean    found;
	RBDACPSource *source;
} FindSource;

static gboolean
sourcelist_find_dacp_source_foreach (GtkTreeModel *model,
                                     GtkTreePath  *path,
                                     GtkTreeIter  *iter,
                                     FindSource   *fg)
{
	gchar *name;
	RBSource *source;

	gtk_tree_model_get (model, iter,
	                    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source,
	                    -1);
	if (source && RB_IS_DACP_SOURCE (source)) {
		g_object_get (source, "service-name", &name, NULL);
		if (strcmp (name, fg->name) == 0) {
			fg->found = TRUE;
			fg->source = RB_DACP_SOURCE (source);
		}
		g_free (name);
	}

	return fg->found;
}

static RBDACPSource *
find_dacp_source (RBShell *shell, const gchar *service_name)
{
	RBSourceListModel *source_list;
	FindSource find_group;

	find_group.found = FALSE;
	find_group.name = service_name;

	g_object_get (shell, "sourcelist-model", &source_list, NULL);

	gtk_tree_model_foreach (GTK_TREE_MODEL (source_list),
	                        (GtkTreeModelForeachFunc) sourcelist_find_dacp_source_foreach,
	                        &find_group);

	if (find_group.found) {
		g_assert (find_group.source != NULL);
		return find_group.source;
	} else {
		return NULL;
	}
}

static void
dacp_remote_added (DACPShare    *share,
                   gchar        *service_name,
                   gchar        *display_name,
                   RBDaapPlugin *plugin)
{
	RBDACPSource *source;
	RBShell *shell;

	rb_debug ("Remote %s (%s) found", service_name, display_name);

	g_object_get (plugin, "shell", &shell, NULL);

	GDK_THREADS_ENTER ();

	source = find_dacp_source (shell, service_name);
	if (source == NULL) {
		source = rb_dacp_source_new (RB_PLUGIN (plugin),
		                             shell,
		                             share,
		                             display_name,
		                             service_name);
		rb_shell_append_source (shell, RB_SOURCE (source), NULL);
	} else {
		rb_dacp_source_remote_found (source);
	}

	GDK_THREADS_LEAVE ();
}

static void
dacp_remote_removed (DACPShare       *share,
                     gchar           *service_name,
                     RBDaapPlugin    *plugin)
{
	RBDACPSource *source;
	RBShell *shell;

	rb_debug ("Remote '%s' went away", service_name);

	g_object_get (plugin, "shell", &shell, NULL);

	GDK_THREADS_ENTER ();

	source = find_dacp_source (shell, service_name);

	if (source != NULL) {
		rb_dacp_source_remote_lost (source);
	}

	GDK_THREADS_LEAVE ();
}
