/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2007  James Henstridge <james@jamesh.id.au>
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

#include <config.h>

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <lib/rb-debug.h>
#include <lib/rb-util.h>
#include <rhythmdb/rhythmdb-query-model.h>
#include <shell/rb-shell-player.h>
#include <widgets/rb-entry-view.h>
#include <widgets/rb-uri-dialog.h>
#include <widgets/rb-source-toolbar.h>

#include "rb-fm-radio-source.h"
#include "rb-radio-tuner.h"
#include "rb-builder-helpers.h"
#include "rb-application.h"

typedef struct _RhythmDBEntryType RBFMRadioEntryType;
typedef struct _RhythmDBEntryTypeClass RBFMRadioEntryTypeClass;

static void     rb_fm_radio_source_class_init  (RBFMRadioSourceClass *class);
static void     rb_fm_radio_source_init        (RBFMRadioSource *self);
static void	rb_fm_radio_source_constructed (GObject *object);
static void     rb_fm_radio_source_dispose     (GObject *object);
static void     rb_fm_radio_source_do_query    (RBFMRadioSource *self);
static void     rb_fm_radio_source_songs_view_sort_order_changed (GObject *obj,
								  GParamSpec *pspec,
								  RBFMRadioSource *self);
static void     rb_fm_radio_source_songs_view_show_popup (
						RBEntryView *view,
						gboolean over_entry,
						RBFMRadioSource *self);

static void	new_station_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);

static void playing_entry_changed (RBShellPlayer *player, RhythmDBEntry *entry,
				   RBFMRadioSource *self);

static void         impl_delete_selected         (RBSource *source);
static RBEntryView *impl_get_entry_view (RBSource *source);

static void rb_fm_radio_entry_type_class_init (RBFMRadioEntryTypeClass *klass);
static void rb_fm_radio_entry_type_init (RBFMRadioEntryType *etype);
GType rb_fm_radio_entry_type_get_type (void);

struct _RBFMRadioSourcePrivate {
	RhythmDB *db;
	RBShellPlayer *player;
	RhythmDBEntryType *entry_type;
	RhythmDBEntry *playing_entry;

	RBEntryView *stations;

	RBRadioTuner *tuner;

	GMenuModel *popup;
};

G_DEFINE_DYNAMIC_TYPE (RBFMRadioSource, rb_fm_radio_source, RB_TYPE_SOURCE);

G_DEFINE_DYNAMIC_TYPE (RBFMRadioEntryType, rb_fm_radio_entry_type, RHYTHMDB_TYPE_ENTRY_TYPE);

static char *
rb_fm_radio_source_get_playback_uri (RhythmDBEntryType *etype, RhythmDBEntry *entry)
{
	return g_strdup("xrbsilence:///");
}

static void
rb_fm_radio_entry_type_class_init (RBFMRadioEntryTypeClass *klass)
{
	RhythmDBEntryTypeClass *etype_class = RHYTHMDB_ENTRY_TYPE_CLASS (klass);
	etype_class->can_sync_metadata = (RhythmDBEntryTypeBooleanFunc) rb_true_function;
	etype_class->sync_metadata = (RhythmDBEntryTypeSyncFunc) rb_null_function;
	etype_class->get_playback_uri = rb_fm_radio_source_get_playback_uri;
}

static void
rb_fm_radio_entry_type_class_finalize (RBFMRadioEntryTypeClass *klass)
{
}

static void
rb_fm_radio_entry_type_init (RBFMRadioEntryType *etype)
{
}

static void
rb_fm_radio_source_class_init (RBFMRadioSourceClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	RBSourceClass *source_class = RB_SOURCE_CLASS (class);

	object_class->constructed = rb_fm_radio_source_constructed;
	object_class->dispose = rb_fm_radio_source_dispose;

	source_class->can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_pause = (RBSourceFeatureFunc) rb_false_function;
	source_class->delete_selected = impl_delete_selected;
	source_class->get_entry_view = impl_get_entry_view;

	g_type_class_add_private (class, sizeof (RBFMRadioSourcePrivate));
}

static void
rb_fm_radio_source_class_finalize (RBFMRadioSourceClass *class)
{
}

static void
rb_fm_radio_source_init (RBFMRadioSource *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		self, RB_TYPE_FM_RADIO_SOURCE, RBFMRadioSourcePrivate);
}

static void
rb_fm_radio_source_constructed (GObject *object)
{
	RBFMRadioSource *self;
	RBShell *shell;
	RBSourceToolbar *toolbar;
	GtkAccelGroup *accel_group;
	GtkWidget *grid;
	GActionEntry actions[] = {
		{ "fmradio-new-station", new_station_action_cb }
	};

	RB_CHAIN_GOBJECT_METHOD (rb_fm_radio_source_parent_class, constructed, object);
	self = RB_FM_RADIO_SOURCE (object);

	g_object_get (self,
		      "shell", &shell,
		      "entry-type", &self->priv->entry_type,
		      NULL);
	g_object_get (shell,
		      "db", &self->priv->db,
		      "shell-player", &self->priv->player,
		      "accel-group", &accel_group,
		      NULL);
	g_object_unref (shell);

	_rb_add_display_page_actions (G_ACTION_MAP (g_application_get_default ()),
				      G_OBJECT (shell),
				      actions,
				      G_N_ELEMENTS (actions));

	toolbar = rb_source_toolbar_new (RB_DISPLAY_PAGE (self), accel_group);
	g_object_unref (accel_group);

	self->priv->stations = rb_entry_view_new (self->priv->db,
						  G_OBJECT (self->priv->player),
						  FALSE, FALSE);
	rb_entry_view_append_column (self->priv->stations,
				     RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_append_column (self->priv->stations,
				     RB_ENTRY_VIEW_COL_RATING, TRUE);
	rb_entry_view_append_column (self->priv->stations,
				     RB_ENTRY_VIEW_COL_LAST_PLAYED, TRUE);

	g_signal_connect_object (self->priv->stations,
				 "notify::sort-order",
				 G_CALLBACK (rb_fm_radio_source_songs_view_sort_order_changed),
				 self, 0);
	/* sort order */
	
	g_signal_connect_object (self->priv->stations, "show_popup",
		G_CALLBACK (rb_fm_radio_source_songs_view_show_popup),
		self, 0);

	grid = gtk_grid_new ();
	gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (toolbar), 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (self->priv->stations), 0, 1, 1, 1);
	gtk_container_add (GTK_CONTAINER (self), grid);
	gtk_widget_show_all (GTK_WIDGET (self));

	rb_fm_radio_source_do_query (self);

	g_signal_connect_object (G_OBJECT (self->priv->player),
				 "playing-song-changed",
				 G_CALLBACK (playing_entry_changed),
				 self, 0);
}

RBSource *
rb_fm_radio_source_new (GObject *plugin, RBShell *shell, RBRadioTuner *tuner)
{
	RBFMRadioSource *self;
	RhythmDBEntryType *entry_type;
	RhythmDB *db;
	GtkBuilder *builder;
	GMenu *toolbar;

	g_object_get (shell, "db", &db, NULL);

	entry_type = rhythmdb_entry_type_get_by_name (db, "fmradio-station");
	if (entry_type == NULL) {
		entry_type = g_object_new (rb_fm_radio_entry_type_get_type (),
					   "db", db,
					   "name", "fmradio-station",
					   "save-to-disk", TRUE,
					   NULL);
		rhythmdb_register_entry_type (db, entry_type);
	}

	builder = rb_builder_load_plugin_file (plugin, "fmradio-toolbar.ui", NULL);
	toolbar = G_MENU (gtk_builder_get_object (builder, "fmradio-toolbar"));
	rb_application_link_shared_menus (RB_APPLICATION (g_application_get_default ()), toolbar);

	self = g_object_new (RB_TYPE_FM_RADIO_SOURCE,
			     "name", _("FM Radio"),
			     "shell", shell,
			     "entry-type", entry_type,
			     "toolbar-menu", toolbar,
			     NULL);
	self->priv->tuner = g_object_ref (tuner);
	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (self),
						 entry_type);
	g_object_unref (db);
	g_object_unref (builder);
	return RB_SOURCE (self);
}

static void
rb_fm_radio_source_dispose (GObject *object)
{
	RBFMRadioSource *self = (RBFMRadioSource *)object;

	if (self->priv->playing_entry) {
		rhythmdb_entry_unref (self->priv->playing_entry);
		self->priv->playing_entry = NULL;
	}

	if (self->priv->db) {
		g_object_unref (self->priv->db);
		self->priv->db = NULL;
	}

	if (self->priv->tuner) {
		g_object_unref (self->priv->tuner);
		self->priv->tuner = NULL;
	}

	G_OBJECT_CLASS (rb_fm_radio_source_parent_class)->dispose (object);
}

static void
rb_fm_radio_source_do_query (RBFMRadioSource *self)
{
	RhythmDBQueryModel *station_query_model;
	GPtrArray *query;

        query = rhythmdb_query_parse (self->priv->db,
                                      RHYTHMDB_QUERY_PROP_EQUALS,
                                      RHYTHMDB_PROP_TYPE,
                                      self->priv->entry_type,
                                      RHYTHMDB_QUERY_END);
	station_query_model = rhythmdb_query_model_new_empty (self->priv->db);
	rhythmdb_do_full_query_parsed (
		self->priv->db, RHYTHMDB_QUERY_RESULTS (station_query_model),
		query);
	rhythmdb_query_free (query);
	rb_entry_view_set_model (self->priv->stations, station_query_model);
	g_object_set (self, "query-model", station_query_model, NULL);
	g_object_unref (station_query_model);
}

static void
rb_fm_radio_source_songs_view_sort_order_changed (GObject *object, GParamSpec *pspec, RBFMRadioSource *self)
{
	rb_entry_view_resort_model (RB_ENTRY_VIEW (object));
}

static void
rb_fm_radio_source_songs_view_show_popup (RBEntryView *view,
					  gboolean over_entry,
					  RBFMRadioSource *source)
{
	GtkWidget *menu;

	if (over_entry == FALSE)
		return;

	if (source->priv->popup == NULL) {
		GtkBuilder *builder;
		GObject *plugin;
		g_object_get (source, "plugin", &plugin, NULL);
		builder = rb_builder_load_plugin_file (plugin, "fmradio-popup.ui", NULL);
		g_object_unref (plugin);

		source->priv->popup = G_MENU_MODEL (gtk_builder_get_object (builder, "fmradio-popup"));
		g_object_ref (source->priv->popup);
		g_object_unref (builder);
	}

	menu = gtk_menu_new_from_model (source->priv->popup);
	gtk_menu_attach_to_widget (GTK_MENU (menu), GTK_WIDGET (source), NULL);
	gtk_menu_popup (GTK_MENU (menu),
			NULL,
			NULL,
			NULL,
			NULL,
			3,
			gtk_get_current_event_time ());
}

void
rb_fm_radio_source_add_station (RBFMRadioSource *self, const char *frequency,
				const char *title)
{
	RhythmDBEntry *entry;
	gchar *uri, *end = NULL;
	GValue val = { 0, };
	
	/* Check that the location is a double */
	g_ascii_strtod (frequency, &end);
	if (end == NULL || end[0] != '\0') {
		rb_debug ("%s is not a frequency", frequency);
		return;
	}
	uri = g_strconcat ("fmradio:", frequency, NULL);

	entry = rhythmdb_entry_lookup_by_location (self->priv->db, uri);
	if (entry) {
		rb_debug ("uri %s already in db", uri);
		g_free (uri);
		return;
	}

	entry = rhythmdb_entry_new (self->priv->db, self->priv->entry_type,
				    uri);
	g_free (uri);
	if (!entry)
		return;

	g_value_init (&val, G_TYPE_STRING);
	if (title)
		g_value_set_static_string (&val, title);
	else
		g_value_set_static_string (&val, frequency);
	rhythmdb_entry_set (self->priv->db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_DOUBLE);
	g_value_set_double (&val, 0.0);
	rhythmdb_entry_set (self->priv->db, entry, RHYTHMDB_PROP_RATING, &val);
	g_value_unset (&val);

	rhythmdb_commit (self->priv->db);
}

static void
new_station_location_added (RBURIDialog *dialog, const char *frequency,
			    RBFMRadioSource *self)
{
	rb_fm_radio_source_add_station (self, frequency, NULL);
}

static void
new_station_response_cb (GtkDialog *dialog, int response, gpointer meh)
{
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
new_station_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBFMRadioSource *source = RB_FM_RADIO_SOURCE (data);
	GtkWidget *dialog;

	dialog = rb_uri_dialog_new (_("New FM Radio Station"),
				    _("Frequency of radio station"));
	g_signal_connect_object (dialog, "location-added",
				 G_CALLBACK (new_station_location_added),
				 source, 0);
	g_signal_connect (dialog, "response", G_CALLBACK (new_station_response_cb), NULL);
	gtk_widget_show_all (dialog);
}

static void
playing_entry_changed (RBShellPlayer *player, RhythmDBEntry *entry,
		       RBFMRadioSource *self)
{
	const gchar *location;
	gdouble frequency;
	gboolean was_playing = FALSE;

	if (entry == self->priv->playing_entry)
		return;

	if (self->priv->playing_entry != NULL) {
		rb_source_update_play_statistics (RB_SOURCE (self),
						  self->priv->db,
						  self->priv->playing_entry);
		rhythmdb_entry_unref (self->priv->playing_entry);
		self->priv->playing_entry = NULL;
		was_playing = TRUE;
	}

	if (entry != NULL &&
	    rhythmdb_entry_get_entry_type (entry) == self->priv->entry_type) {
		self->priv->playing_entry = rhythmdb_entry_ref (entry);
		location = rhythmdb_entry_get_string (entry,
						      RHYTHMDB_PROP_LOCATION);
		if (g_str_has_prefix (location, "fmradio:")) {
			frequency = g_ascii_strtod(
				&location[strlen("fmradio:")], NULL);
			if (!was_playing)
				rb_radio_tuner_set_mute (self->priv->tuner,
							 FALSE);
			rb_radio_tuner_set_frequency (self->priv->tuner,
						      frequency);
		}
	} else {
		if (was_playing)
			rb_radio_tuner_set_mute (self->priv->tuner, TRUE);
	}
	
}

static void
impl_delete_selected (RBSource *source)
{
	RBFMRadioSource *self = RB_FM_RADIO_SOURCE (source);
	GList *selection, *tmp;

	selection = rb_entry_view_get_selected_entries (self->priv->stations);
	for (tmp = selection; tmp != NULL; tmp = tmp->next) {
		RhythmDBEntry *entry = tmp->data;

		rhythmdb_entry_delete (self->priv->db, entry);
		rhythmdb_commit (self->priv->db);
		rhythmdb_entry_unref (entry);
	}
	g_list_free (selection);
}

static RBEntryView *
impl_get_entry_view (RBSource *source)
{
	RBFMRadioSource *self = (RBFMRadioSource *)source;

	return self->priv->stations;
}

void
_rb_fm_radio_source_register_type (GTypeModule *module)
{
	rb_fm_radio_source_register_type (module);
	rb_fm_radio_entry_type_register_type (module);
}
