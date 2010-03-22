/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2003 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@redhat.com>
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-source-header.h"
#include "rb-stock-icons.h"
#include "rb-preferences.h"
#include "rb-search-entry.h"
#include "rb-debug.h"
#include "rb-entry-view.h"
#include "eel-gconf-extensions.h"
#include "rb-util.h"
#include "rb-marshal.h"

/**
 * SECTION:rb-source-header
 * @short_description: container for search box and toolbar
 *
 * The source header contains the search box and the search
 * action toolbar.  It remembers the search text and browser
 * visibility state for each source, applying the stored state
 * for the newly selected source when the source selection changes.
 *
 * The search bar contains a set of actions determined by the
 * selected source.  The source is responsible for tracking
 * which search action is active.
 */

static void rb_source_header_class_init (RBSourceHeaderClass *klass);
static void rb_source_header_init (RBSourceHeader *shell_player);
static void rb_source_header_finalize (GObject *object);
static void rb_source_header_dispose (GObject *object);
static void rb_source_header_set_property (GObject *object,
					  guint prop_id,
					  const GValue *value,
					  GParamSpec *pspec);
static void rb_source_header_get_property (GObject *object,
					  guint prop_id,
					  GValue *value,
					  GParamSpec *pspec);
static void rb_source_header_filter_changed_cb (RBSource *source,
						RBSourceHeader *header);
static void rb_source_header_search_cb (RBSearchEntry *search,
					const char *text,
					RBSourceHeader *header);
static void rb_source_header_search_activate_cb (RBSearchEntry *search,
						 const char *text,
						 RBSourceHeader *header);
static void rb_source_header_view_browser_changed_cb (GtkAction *action,
						      RBSourceHeader *header);
static void rb_source_header_source_weak_destroy_cb (RBSourceHeader *header, RBSource *source);
static void search_action_changed_cb (GtkRadioAction *action,
				      GtkRadioAction *current,
				      RBSourceHeader *header);
static void rb_source_header_refresh_search_bar (RBSourceHeader *header);

typedef struct {
	gboolean 	disclosed;
	char     	*search_text;
	GtkRadioAction  *search_action;
} SourceState;

static void
sourcestate_free (SourceState *state)
{
	if (state->search_action != NULL) {
		g_object_unref (state->search_action);
	}
        g_free (state->search_text);
        g_free (state);
}

struct RBSourceHeaderPrivate
{
	RBSource *selected_source;

	GtkUIManager *ui_manager;
	GtkActionGroup *actiongroup;
	guint source_ui_merge_id;

	GtkWidget *search;
	GtkWidget *search_bar;
	GtkRadioAction *search_group_head;

	guint browser_notify_id;
	guint search_notify_id;
	RBSourceSearchType search_type;
	gboolean have_browser;
	gboolean disclosed;
	char *browser_key;

	GHashTable *source_states;
};

#define RB_SOURCE_HEADER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SOURCE_HEADER, RBSourceHeaderPrivate))

enum
{
	PROP_0,
	PROP_ACTION_GROUP,
	PROP_UI_MANAGER,
	PROP_SOURCE,
};

static GtkToggleActionEntry rb_source_header_toggle_entries [] =
{
	{ "ViewBrowser", RB_STOCK_BROWSER, N_("_Browse"), "<control>B",
	  N_("Change the visibility of the browser"),
	  G_CALLBACK (rb_source_header_view_browser_changed_cb), FALSE }
};

enum
{
	GET_SEARCH_ACTIONS,
	REFRESH_SEARCH_BAR,
	LAST_SIGNAL
};

static guint rb_source_header_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBSourceHeader, rb_source_header, GTK_TYPE_TABLE)

static inline void
force_no_shadow (GtkWidget *widget)
{
        gboolean first_time = TRUE;

        if (first_time) {
                gtk_rc_parse_string ("\n"
                                     "   style \"search-toolbar-style\"\n"
                                     "   {\n"
                                     "      GtkToolbar::shadow-type=GTK_SHADOW_NONE\n"
                                     "   }\n"
                                     "\n"
                                     "    widget \"*.search-toolbar\" style \"search-toolbar-style\"\n"
                                     "\n");
                first_time = FALSE;
        }

        gtk_widget_set_name (widget, "search-toolbar");
}

static void
ui_manager_add_widget_cb (GtkUIManager *ui_manager,
			  GtkWidget *widget,
			  RBSourceHeader *header)
{
	if (header->priv->search_bar != NULL) {
		return;
	}

	if (GTK_IS_TOOLBAR (widget)) {
		header->priv->search_bar = gtk_ui_manager_get_widget (header->priv->ui_manager, "/SearchBar");
		if (header->priv->search_bar != NULL) {
			gtk_toolbar_set_style (GTK_TOOLBAR (header->priv->search_bar), GTK_TOOLBAR_TEXT);
			force_no_shadow (header->priv->search_bar);
			gtk_widget_show (header->priv->search_bar);
			gtk_table_attach (GTK_TABLE (header),
					  header->priv->search_bar,
					  1, 3, 0, 1,
					  GTK_EXPAND | GTK_FILL,
					  GTK_EXPAND | GTK_FILL,
					  5, 0);
		}
	}
}

static void
rb_source_header_class_init (RBSourceHeaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_source_header_finalize;
	object_class->dispose = rb_source_header_dispose;

	object_class->set_property = rb_source_header_set_property;
	object_class->get_property = rb_source_header_get_property;

	klass->refresh_search_bar = rb_source_header_refresh_search_bar;

	/**
	 * RBSourceHeader:source:
	 *
	 * The currently selected #RBSource.
	 */
	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "RBSource",
							      "RBSource object",
							      RB_TYPE_SOURCE,
							      G_PARAM_READWRITE));
	/**
	 * RBSourceHeader:action-group:
	 *
	 * The #GtkActionGroup to add actions to.
	 */
	g_object_class_install_property (object_class,
					 PROP_ACTION_GROUP,
					 g_param_spec_object ("action-group",
							      "GtkActionGroup",
							      "GtkActionGroup object",
							      GTK_TYPE_ACTION_GROUP,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBSourceHeader:ui-manager:
	 *
	 * The #GtkUIManager instance.
	 */
	g_object_class_install_property (object_class,
					 PROP_UI_MANAGER,
					 g_param_spec_object ("ui-manager",
							      "GtkUIManager",
							      "GtkUIManager object",
							      GTK_TYPE_UI_MANAGER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBSourceHeader::get-search-actions:
	 * @header: the #RBSourceHeader
	 *
	 * Allows signal handlers to add search actions to the search
	 * bar by returning an array of search action names.
	 */
	rb_source_header_signals[GET_SEARCH_ACTIONS] =
		g_signal_new ("get-search-actions",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,		/* no need to handle this ourselves */
			      rb_signal_accumulator_value_array, NULL,
			      rb_marshal_BOXED__OBJECT,
			      G_TYPE_VALUE_ARRAY,
			      1,
			      RB_TYPE_SOURCE);

	/**
	 * RBSourceHeader::refresh-search-bar:
	 * @header: the #RBSourceHeader
	 *
	 * An action signal used to repopulate the search bar.
	 * This should be called after a signal handler for
	 * #::get-search-actions has been connected
	 * or disconnected.
	 */
	rb_source_header_signals[REFRESH_SEARCH_BAR] =
		g_signal_new ("refresh-search-bar",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceHeaderClass, refresh_search_bar),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	g_type_class_add_private (klass, sizeof (RBSourceHeaderPrivate));
}

static void
rb_source_header_init (RBSourceHeader *header)
{
	GtkWidget *align;
	GtkEventBox *ebox;

	header->priv = RB_SOURCE_HEADER_GET_PRIVATE (header);

	gtk_table_set_col_spacings (GTK_TABLE (header), 5);
	gtk_table_resize (GTK_TABLE (header), 1, 3);

	ebox = GTK_EVENT_BOX (gtk_event_box_new ());
	header->priv->search = GTK_WIDGET (rb_search_entry_new ());

	gtk_widget_set_tooltip_text (GTK_WIDGET (ebox),
				     _("Filter music display by genre, artist, album, or title"));

	gtk_container_add (GTK_CONTAINER (ebox), GTK_WIDGET (header->priv->search));

	g_signal_connect_object (G_OBJECT (header->priv->search), "search",
				 G_CALLBACK (rb_source_header_search_cb), header, 0);
	g_signal_connect_object (G_OBJECT (header->priv->search), "activate",
				 G_CALLBACK (rb_source_header_search_activate_cb), header, 0);

	align = gtk_alignment_new (1.0, 0.5, 1.0, 1.0);
	gtk_container_add (GTK_CONTAINER (align), GTK_WIDGET (ebox));
	gtk_table_attach (GTK_TABLE (header),
			  align,
			  0, 1, 0, 1,
			  GTK_EXPAND | GTK_FILL,
			  GTK_EXPAND | GTK_FILL,
			  5, 0);

	header->priv->source_states = g_hash_table_new_full (g_direct_hash, g_direct_equal,
							    NULL, (GDestroyNotify)sourcestate_free);

	/* invisible action at the start of the search bar, used to
	 * simplify the radio action group handling.
	 */
	header->priv->search_group_head = gtk_radio_action_new ("InvisibleSearchHead", NULL, NULL, NULL, 0);
	gtk_action_set_visible (GTK_ACTION (header->priv->search_group_head), FALSE);

	g_signal_connect_object (header->priv->search_group_head,
				 "changed",
				 G_CALLBACK (search_action_changed_cb), header, 0);
}

static void
rb_source_header_source_weak_unref (RBSource *source, char *text, RBSourceHeader *header)
{
	g_object_weak_unref (G_OBJECT (source),
			     (GWeakNotify)rb_source_header_source_weak_destroy_cb,
			     header);
}

static void
rb_source_header_dispose (GObject *object)
{
	RBSourceHeader *header;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SOURCE_HEADER (object));

	header = RB_SOURCE_HEADER (object);

	g_return_if_fail (header->priv != NULL);

	if (header->priv->search_group_head != NULL) {
		g_object_unref (header->priv->search_group_head);
		header->priv->search_group_head = NULL;
	}

	G_OBJECT_CLASS (rb_source_header_parent_class)->dispose (object);
}

static void
rb_source_header_finalize (GObject *object)
{
	RBSourceHeader *header;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SOURCE_HEADER (object));

	header = RB_SOURCE_HEADER (object);

	g_return_if_fail (header->priv != NULL);

	g_hash_table_foreach (header->priv->source_states,
			      (GHFunc) rb_source_header_source_weak_unref,
			      header);
	g_hash_table_destroy (header->priv->source_states);

	g_free (header->priv->browser_key);

	G_OBJECT_CLASS (rb_source_header_parent_class)->finalize (object);
}

static void
merge_source_ui_cb (const char *action,
		    RBSourceHeader *header)
{
	GSList *group;
	GtkAction *radio_action;
	char *path;

	gtk_ui_manager_add_ui (header->priv->ui_manager,
			       header->priv->source_ui_merge_id,
			       "/SearchBar",
			       action,
			       action,
			       GTK_UI_MANAGER_AUTO,
			       FALSE);

	/* find the action */
	path = g_strdup_printf ("/SearchBar/%s", action);
	radio_action = gtk_ui_manager_get_action (header->priv->ui_manager, path);
	g_free (path);
	g_assert (radio_action);
	
	/* add it to the group */
	group = gtk_radio_action_get_group (header->priv->search_group_head);
	gtk_radio_action_set_group (GTK_RADIO_ACTION (radio_action), group);

	/* ensure it isn't active; we'll activate the right one later */
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (radio_action), FALSE);
}

static void
rb_source_header_refresh_search_bar (RBSourceHeader *header)
{
	GSList *group;
	GSList *t;

	if (header->priv->source_ui_merge_id != 0) {
		gtk_ui_manager_remove_ui (header->priv->ui_manager, header->priv->source_ui_merge_id);
	}

	gtk_ui_manager_ensure_update (header->priv->ui_manager);

	/* dismember the search action group */
	group = gtk_radio_action_get_group (header->priv->search_group_head);
	group = g_slist_copy (group);

	for (t = group; t != NULL; t = t->next) {
		GtkRadioAction *a = (GtkRadioAction *)t->data;
		if (a != header->priv->search_group_head) {
			gtk_radio_action_set_group (a, NULL);
		}
	}

	g_slist_free (group);

	gtk_ui_manager_add_ui (header->priv->ui_manager,
			       header->priv->source_ui_merge_id,
			       "/SearchBar",
			       "InvisibleSearchHead",
			       "InvisibleSearchHead",
			       GTK_UI_MANAGER_AUTO,
			       FALSE);

	if (header->priv->selected_source != NULL) {
		SourceState *source_state;
		GList *actions;
		GValueArray *plugin_actions;

		source_state = g_hash_table_lookup (header->priv->source_states,
						    header->priv->selected_source);

		/* merge the source-specific UI */
		actions = rb_source_get_search_actions (header->priv->selected_source);
		g_list_foreach (actions, (GFunc)merge_source_ui_cb, header);
		rb_list_deep_free (actions);

		/* merge in plugin-supplied search actions */
		g_signal_emit (header,
			       rb_source_header_signals[GET_SEARCH_ACTIONS], 0,
			       header->priv->selected_source,
			       &plugin_actions);
		if (plugin_actions != NULL) {
			int i;
			for (i = 0; i < plugin_actions->n_values; i++) {
				GValue *v = g_value_array_get_nth (plugin_actions, i);
				const char *action;

				action = g_value_get_string (v);
				merge_source_ui_cb (action, header);
			}

			g_value_array_free (plugin_actions);
		}

		/* select the active search for the source */
		if (source_state != NULL && source_state->search_action != NULL) {
			gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (source_state->search_action),
						      TRUE);
		}
	}
}

static void
rb_source_header_set_source_internal (RBSourceHeader *header,
				      RBSource *source)
{
	if (header->priv->selected_source != NULL) {
		g_signal_handlers_disconnect_by_func (G_OBJECT (header->priv->selected_source),
						      G_CALLBACK (rb_source_header_filter_changed_cb),
						      header);
	}

	header->priv->selected_source = source;
	rb_debug ("selected source %p", source);

	if (header->priv->selected_source != NULL) {
		SourceState *source_state;
		char        *text;
		gboolean    disclosed;

		source_state = g_hash_table_lookup (header->priv->source_states,
						    header->priv->selected_source);

		if (source_state) {
			text = g_strdup (source_state->search_text);
			disclosed = source_state->disclosed;
		} else {
			text = NULL;
			disclosed = FALSE;
		}

		g_free (header->priv->browser_key);
		header->priv->browser_key = rb_source_get_browser_key (header->priv->selected_source);

		rb_search_entry_set_text (RB_SEARCH_ENTRY (header->priv->search), text);
		g_signal_connect_object (G_OBJECT (header->priv->selected_source),
					 "filter_changed",
					 G_CALLBACK (rb_source_header_filter_changed_cb),
					 header, 0);

		g_object_get (header->priv->selected_source, "search-type", &header->priv->search_type, NULL);
		gtk_widget_set_sensitive (GTK_WIDGET (header->priv->search),
					  (header->priv->search_type != RB_SOURCE_SEARCH_NONE));
		header->priv->have_browser = rb_source_can_browse (header->priv->selected_source);

		if (!header->priv->have_browser) {
			header->priv->disclosed = FALSE;
		} else if (header->priv->browser_key) {
			header->priv->disclosed = eel_gconf_get_boolean (header->priv->browser_key);
		} else {
			/* restore the previous state of the source*/
			header->priv->disclosed = disclosed;
		}

		if (!header->priv->have_browser && (header->priv->search_type == RB_SOURCE_SEARCH_NONE)) {
			gtk_widget_hide (GTK_WIDGET (header));
		} else {
			gtk_widget_show (GTK_WIDGET (header));
		}

	} else {
		/* no selected source -> hide source header */
		gtk_widget_hide (GTK_WIDGET (header));
		header->priv->have_browser = FALSE;
		header->priv->disclosed = FALSE;

		g_free (header->priv->browser_key);
		header->priv->browser_key = NULL;
	}

	rb_source_header_refresh_search_bar (header);

	rb_source_header_sync_control_state (header);
}

static void
rb_source_header_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBSourceHeader *header = RB_SOURCE_HEADER (object);

	switch (prop_id) {
	case PROP_SOURCE:
		rb_source_header_set_source_internal (header, g_value_get_object (value));

		break;
	case PROP_ACTION_GROUP:
		header->priv->actiongroup = g_value_get_object (value);
		gtk_action_group_add_action (header->priv->actiongroup,
					     GTK_ACTION (header->priv->search_group_head));
		gtk_action_group_add_toggle_actions (header->priv->actiongroup,
						     rb_source_header_toggle_entries,
						     G_N_ELEMENTS (rb_source_header_toggle_entries),
						     header);
		break;
	case PROP_UI_MANAGER:
		header->priv->ui_manager = g_value_get_object (value);
		g_signal_connect (G_OBJECT (header->priv->ui_manager), "add_widget",
				  G_CALLBACK (ui_manager_add_widget_cb), header);

		header->priv->source_ui_merge_id = gtk_ui_manager_new_merge_id (header->priv->ui_manager);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_source_header_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBSourceHeader *header = RB_SOURCE_HEADER (object);

	switch (prop_id) {
	case PROP_SOURCE:
		g_value_set_object (value, header->priv->selected_source);
		break;
	case PROP_ACTION_GROUP:
		g_value_set_object (value, header->priv->actiongroup);
		break;
	case PROP_UI_MANAGER:
		g_value_set_object (value, header->priv->ui_manager);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_source_header_set_source:
 * @header: the #RBSourceHeader
 * @source: the new selected #RBSource
 *
 * Updates the source header when a new source is selected.
 */
void
rb_source_header_set_source (RBSourceHeader *header,
			     RBSource *source)
{
	g_return_if_fail (RB_IS_SOURCE_HEADER (header));
	g_return_if_fail (source == NULL || RB_IS_SOURCE (source));

	g_object_set (G_OBJECT (header),
		      "source", source,
		      NULL);
}

/**
 * rb_source_header_new:
 * @mgr: the #GtkUIManager
 * @actiongroup: the #GtkActionGroup to add actions to
 *
 * Creates the #RBSourceHeader.
 *
 * Return value: the #RBSourceHeader instance
 */
RBSourceHeader *
rb_source_header_new (GtkUIManager   *mgr,
		      GtkActionGroup *actiongroup)
{
	RBSourceHeader *header = g_object_new (RB_TYPE_SOURCE_HEADER,
					       "action-group", actiongroup,
					       "ui-manager", mgr,
					       NULL);

	g_return_val_if_fail (header->priv != NULL, NULL);

	return header;
}

static void
rb_source_header_filter_changed_cb (RBSource *source,
				    RBSourceHeader *header)
{
	rb_debug  ("filter changed for %p", source);
	/* To add this line back in, you need to add a search_changed signal,
	 * make RBShufflePlayOrder at least listen to it, and change the
	 * filter_changed notifies in all of the search functions to
	 * search_change notifies.
	 */
	/* rb_search_entry_clear (RB_SEARCH_ENTRY (header->priv->search)); */
}

static void
rb_source_header_source_weak_destroy_cb (RBSourceHeader *header, RBSource *source)
{
	g_hash_table_remove (header->priv->source_states, source);
}

static void
rb_source_state_sync (RBSourceHeader *header,
		      gboolean set_text,
		      const char *text,
		      gboolean set_search,
		      GtkRadioAction *action,
		      gboolean set_disclosure,
		      gboolean disclosed)
{
	SourceState *state;
	gboolean do_search = FALSE;
	char *old_text = NULL;

	if (header->priv->selected_source == NULL) {
		return;
	}

	state = g_hash_table_lookup (header->priv->source_states,
				     header->priv->selected_source);
	if (state == NULL) {
		/* if we haven't seen the source before, monitor it for deletion */
		g_object_weak_ref (G_OBJECT (header->priv->selected_source),
				   (GWeakNotify)rb_source_header_source_weak_destroy_cb,
				   header);

		state = g_new0 (SourceState, 1);
		state->search_text = g_strdup ("");
		g_hash_table_insert (header->priv->source_states,
				     header->priv->selected_source,
				     state);
		do_search = TRUE;
	}

	if (set_text) {
		if (rb_safe_strcmp (state->search_text, text) != 0) {
			old_text = state->search_text;
			do_search = TRUE;
		} else {
			old_text = NULL;
			g_free (state->search_text);
		}
		state->search_text = g_strdup (text ? text : "");
	}

	if (set_disclosure) {
		state->disclosed = disclosed;
	}

	if (set_search) {
		if (state->search_action != action) {
			/* if the search action changes, there's no way we can use the current
			 * search text to optimise the query.
			 */
			g_free (old_text);
			old_text = NULL;

			do_search = TRUE;
		}
		if (state->search_action != NULL) {
			g_object_unref (state->search_action);
			state->search_action = NULL;
		}
		if (action != NULL) {
			state->search_action = g_object_ref (action);
		}
	}

	if (do_search) {
		RBSourceSearch *search = NULL;

		if (state->search_action != NULL) {
			search = rb_source_search_get_from_action (G_OBJECT (state->search_action));
		}

		rb_source_search (header->priv->selected_source,
				  search,
				  old_text,
				  state->search_text);
	}

	g_free (old_text);
}

static void
search_action_changed_cb (GtkRadioAction *action,
			  GtkRadioAction *current,
			  RBSourceHeader *header)
{
	if (header->priv->selected_source == NULL) {
		return;
	}

	if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (current)) == FALSE) {
		return;
	}

	rb_debug ("search action %s activated", gtk_action_get_name (GTK_ACTION (current)));
	rb_source_state_sync (header,
			      FALSE, NULL,
			      TRUE, current,
			      FALSE, FALSE);
	rb_source_header_sync_control_state (header);
}

static void
rb_source_header_search_cb (RBSearchEntry *search,
			    const char *text,
			    RBSourceHeader *header)
{
	if (header->priv->search_type != RB_SOURCE_SEARCH_INCREMENTAL) {
		return;
	}

	rb_debug ("searching for \"%s\"", text);

	rb_source_state_sync (header,
			      TRUE, text,
			      FALSE, NULL,
			      FALSE, FALSE);

	rb_source_header_sync_control_state (header);
}

/**
 * rb_source_header_clear_search:
 * @header: the #RBSourceHeader
 *
 * Clears the search box and resets all search state for the
 * current selected source.
 */
void
rb_source_header_clear_search (RBSourceHeader *header)
{
	rb_debug ("clearing search");

	if (!rb_search_entry_searching (RB_SEARCH_ENTRY (header->priv->search)))
		return;

	if (header->priv->selected_source) {
		rb_source_state_sync (header,
				      TRUE, NULL,
				      FALSE, NULL,
				      FALSE, FALSE);
	}

	rb_search_entry_clear (RB_SEARCH_ENTRY (header->priv->search));
	rb_source_header_sync_control_state (header);
}

static void
rb_source_header_view_browser_changed_cb (GtkAction *action,
					  RBSourceHeader *header)
{
	rb_debug ("got view browser toggle");
	header->priv->disclosed = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));

	if (header->priv->browser_key) {
		eel_gconf_set_boolean (header->priv->browser_key,
				       header->priv->disclosed);
	} else {
		rb_source_state_sync (header,
				      FALSE, NULL,
				      FALSE, NULL,
				      TRUE, header->priv->disclosed);
	}

	rb_debug ("got view browser toggle");

	rb_source_header_sync_control_state (header);
}

/**
 * rb_source_header_sync_control_state:
 * @header: the #RBSourceHeader
 *
 * Updates the sensitivity and active state of various actions
 * related to the source header.
 */
void
rb_source_header_sync_control_state (RBSourceHeader *header)
{
	GtkAction *viewbrowser_action;
	GtkAction *viewstatusbar_action;
	GtkAction *viewall_action;
	gboolean not_small = !eel_gconf_get_boolean (CONF_UI_SMALL_DISPLAY);

	viewbrowser_action = gtk_action_group_get_action (header->priv->actiongroup,
							  "ViewBrowser");
	g_object_set (G_OBJECT (viewbrowser_action), "sensitive",
		      header->priv->have_browser && not_small, NULL);
	viewstatusbar_action = gtk_action_group_get_action (header->priv->actiongroup,
							    "ViewStatusbar");
	g_object_set (G_OBJECT (viewstatusbar_action), "sensitive",
		      not_small, NULL);
	viewall_action = gtk_action_group_get_action (header->priv->actiongroup,
						      "ViewAll");
	g_object_set (G_OBJECT (viewall_action), "sensitive",
		      (header->priv->have_browser || (header->priv->search_type != RB_SOURCE_SEARCH_NONE)) && not_small, NULL);

	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (viewbrowser_action),
				      header->priv->disclosed);

	if (header->priv->selected_source)
		rb_source_browser_toggled (header->priv->selected_source, header->priv->disclosed);
}

static void
rb_source_header_search_activate_cb (RBSearchEntry *search,
				     const char *text,
				     RBSourceHeader *header)
{
	if (header->priv->search_type == RB_SOURCE_SEARCH_EXPLICIT) {
		rb_source_state_sync (header,
				      TRUE, text,
				      FALSE, NULL,
				      FALSE, FALSE);
	} else {
		/* whatever this is supposed to do, I don't think it's doing it */
		gtk_widget_grab_focus (GTK_WIDGET (header->priv->selected_source));
	}
}

/**
 * rb_source_header_focus_search_box:
 * @header: the #RBSourceHeader
 *
 * Grabs input focus for the search box.
 */
void
rb_source_header_focus_search_box (RBSourceHeader *header)
{
	rb_search_entry_grab_focus (RB_SEARCH_ENTRY (header->priv->search));
}
