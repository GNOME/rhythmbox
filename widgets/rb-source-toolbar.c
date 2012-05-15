/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2011 Jonathan Matthew <jonathan@d14n.org>
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

#include <widgets/rb-source-toolbar.h>
#include <lib/rb-util.h>

static void rb_source_toolbar_class_init (RBSourceToolbarClass *klass);
static void rb_source_toolbar_init (RBSourceToolbar *toolbar);

struct _RBSourceToolbarPrivate
{
	GtkUIManager *ui_manager;
	RBSource *source;
	RBSearchEntry *search_entry;
	GtkWidget *search_popup;
	GtkWidget *toolbar;
	GBinding *browse_binding;
	GtkAction *browse_action;
	char *popup_path;

	/* search state */
	int search_value;
	gulong search_change_cb_id;
	RBSourceSearch *active_search;
	char *search_text;
	GtkRadioAction *search_group;
};

G_DEFINE_TYPE (RBSourceToolbar, rb_source_toolbar, GTK_TYPE_GRID)

/**
 * SECTION:rb-source-toolbar
 * @short_description: toolbar+search entry for sources
 *
 * This class combines a toolbar for custom source actions with a
 * search entry.  The toolbar content is specified using a UI path.
 * The #RBSourceToolbar takes care of preserving search state when
 * the selected page changes, and performs searches when the user
 * selects a new search type or changes the search text.
 */

enum
{
	PROP_0,
	PROP_SOURCE,
	PROP_UI_MANAGER,
};

static void
prepare_toolbar (GtkWidget *toolbar)
{
	static GtkCssProvider *provider = NULL;

	if (provider == NULL) {
		const char *style =
			"GtkToolbar {\n"
                        "       -GtkToolbar-shadow-type: none;\n"
                        "       border-style: none;\n"
                        "}";

		provider = gtk_css_provider_new ();
		gtk_css_provider_load_from_data (provider, style, -1, NULL);
	}

	gtk_style_context_add_provider (gtk_widget_get_style_context (toolbar),
					GTK_STYLE_PROVIDER (provider),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	gtk_widget_set_hexpand (toolbar, TRUE);

	gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_TEXT);
}

static void
search_change_cb (GtkRadioAction *group, GtkRadioAction *current, RBSourceToolbar *toolbar)
{
	toolbar->priv->active_search = rb_source_search_get_from_action (G_OBJECT (current));

	if (toolbar->priv->search_text != NULL) {
		rb_source_search (toolbar->priv->source, toolbar->priv->active_search, NULL, toolbar->priv->search_text);
	}

	rb_search_entry_set_placeholder (toolbar->priv->search_entry, gtk_action_get_label (GTK_ACTION (current)));
}

static void
source_selected_cb (GObject *object, GParamSpec *pspec, RBSourceToolbar *toolbar)
{
	gboolean selected;

	g_object_get (object, "selected", &selected, NULL);

	if (selected) {
		char *toolbar_path;
		char *browse_path;

		if (toolbar->priv->toolbar != NULL) {
			gtk_grid_attach (GTK_GRID (toolbar), toolbar->priv->toolbar, 0, 0, 2, 1);
			gtk_widget_show_all (GTK_WIDGET (toolbar->priv->toolbar));
		}

		if (toolbar->priv->search_entry != NULL) {
			rb_search_entry_set_mnemonic (toolbar->priv->search_entry, TRUE);

			gtk_widget_add_accelerator (GTK_WIDGET (toolbar->priv->search_entry),
						    "grab-focus",
						    gtk_ui_manager_get_accel_group (toolbar->priv->ui_manager),
						    gdk_unicode_to_keyval ('f'),
						    GDK_CONTROL_MASK,
						    0);
		}

		if (toolbar->priv->search_group != NULL) {
			if (toolbar->priv->search_value != -1) {
				gtk_radio_action_set_current_value (toolbar->priv->search_group,
								    toolbar->priv->search_value);
			}

			toolbar->priv->search_change_cb_id = g_signal_connect (toolbar->priv->search_group,
									       "changed",
									       G_CALLBACK (search_change_cb),
									       toolbar);
		}

		g_object_get (toolbar->priv->source, "toolbar-path", &toolbar_path, NULL);
		if (toolbar_path != NULL) {

			browse_path = g_strdup_printf ("%s/Browse", toolbar_path);
			toolbar->priv->browse_action = gtk_ui_manager_get_action (toolbar->priv->ui_manager,
										  browse_path);
			g_free (browse_path);

			if (toolbar->priv->browse_action != NULL) {
				toolbar->priv->browse_binding =
					g_object_bind_property (toolbar->priv->source, "show-browser",
								toolbar->priv->browse_action, "active",
								G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
				gtk_action_connect_accelerator (toolbar->priv->browse_action);
			}
			g_free (toolbar_path);
		} else {
			toolbar->priv->browse_action = NULL;
		}
	} else {
		if (toolbar->priv->toolbar != NULL) {
			gtk_container_remove (GTK_CONTAINER (toolbar), toolbar->priv->toolbar);
		}

		if (toolbar->priv->search_entry != NULL) {
			rb_search_entry_set_mnemonic (toolbar->priv->search_entry, FALSE);

			gtk_widget_remove_accelerator (GTK_WIDGET (toolbar->priv->search_entry),
						       gtk_ui_manager_get_accel_group (toolbar->priv->ui_manager),
						       gdk_unicode_to_keyval ('f'),
						       GDK_CONTROL_MASK);
		}

		if (toolbar->priv->search_group != NULL) {
			if (toolbar->priv->search_change_cb_id != 0) {
				g_signal_handler_disconnect (toolbar->priv->search_group,
							     toolbar->priv->search_change_cb_id);
			}

			toolbar->priv->search_value = gtk_radio_action_get_current_value (toolbar->priv->search_group);
		}

		if (toolbar->priv->browse_binding != NULL) {
			g_object_unref (toolbar->priv->browse_binding);
			toolbar->priv->browse_binding = NULL;
		}

		if (toolbar->priv->browse_action != NULL) {
			gtk_action_disconnect_accelerator (toolbar->priv->browse_action);
			toolbar->priv->browse_action = NULL;
		}
	}
}

static void
search_cb (RBSearchEntry *search_entry, const char *text, RBSourceToolbar *toolbar)
{
	rb_source_search (toolbar->priv->source, toolbar->priv->active_search, toolbar->priv->search_text, text);

	g_free (toolbar->priv->search_text);
	toolbar->priv->search_text = NULL;
	if (text != NULL) {
		toolbar->priv->search_text = g_strdup (text);
	}
}

static void
show_popup_cb (RBSearchEntry *search_entry, RBSourceToolbar *toolbar)
{
	gtk_menu_popup (GTK_MENU (toolbar->priv->search_popup),
			NULL, NULL, NULL, NULL, 3,
			gtk_get_current_event_time ());
}


static void
impl_finalize (GObject *object)
{
	RBSourceToolbar *toolbar = RB_SOURCE_TOOLBAR (object);

	g_free (toolbar->priv->search_text);
	g_free (toolbar->priv->popup_path);

	G_OBJECT_CLASS (rb_source_toolbar_parent_class)->finalize (object);
}

static void
impl_dispose (GObject *object)
{
	RBSourceToolbar *toolbar = RB_SOURCE_TOOLBAR (object);

	if (toolbar->priv->ui_manager != NULL) {
		g_object_unref (toolbar->priv->ui_manager);
		toolbar->priv->ui_manager = NULL;
	}
	if (toolbar->priv->search_popup != NULL) {
		g_object_unref (toolbar->priv->search_popup);
		toolbar->priv->search_popup = NULL;
	}
	if (toolbar->priv->toolbar != NULL) {
		g_object_unref (toolbar->priv->toolbar);
		toolbar->priv->toolbar = NULL;
	}
	if (toolbar->priv->browse_binding != NULL) {
		g_object_unref (toolbar->priv->browse_binding);
		toolbar->priv->browse_binding = NULL;
	}

	G_OBJECT_CLASS (rb_source_toolbar_parent_class)->dispose (object);
}

static void
toolbar_add_widget_cb (GtkUIManager *ui_manager, GtkWidget *widget, RBSourceToolbar *toolbar)
{
	char *toolbar_path;
	gboolean selected;

	g_object_get (toolbar->priv->source, "toolbar-path", &toolbar_path, "selected", &selected, NULL);
	toolbar->priv->toolbar = gtk_ui_manager_get_widget (toolbar->priv->ui_manager, toolbar_path);
	g_free (toolbar_path);

	if (toolbar->priv->toolbar) {
		g_object_ref (toolbar->priv->toolbar);
		g_signal_handlers_disconnect_by_func (ui_manager, G_CALLBACK (toolbar_add_widget_cb), toolbar);

		prepare_toolbar (toolbar->priv->toolbar);

		if (selected) {
			gtk_grid_attach (GTK_GRID (toolbar), toolbar->priv->toolbar, 0, 0, 2, 1);
			gtk_widget_show_all (GTK_WIDGET (toolbar->priv->toolbar));
		}
	}
}

static void
impl_constructed (GObject *object)
{
	RBSourceToolbar *toolbar;
	char *toolbar_path;
	GtkWidget *blank;

	RB_CHAIN_GOBJECT_METHOD (rb_source_toolbar_parent_class, constructed, object);

	toolbar = RB_SOURCE_TOOLBAR (object);

	g_object_get (toolbar->priv->source, "toolbar-path", &toolbar_path, NULL);
	if (toolbar_path) {
		toolbar->priv->toolbar = gtk_ui_manager_get_widget (toolbar->priv->ui_manager, toolbar_path);
		if (toolbar->priv->toolbar == NULL) {
			g_signal_connect (toolbar->priv->ui_manager, "add-widget", G_CALLBACK (toolbar_add_widget_cb), toolbar);
		} else {
			g_object_ref (toolbar->priv->toolbar);
			prepare_toolbar (toolbar->priv->toolbar);
		}
	} else {
		blank = gtk_toolbar_new ();
		prepare_toolbar (blank);
		gtk_grid_attach (GTK_GRID (toolbar), blank, 0, 0, 2 ,1);
	}
	g_free (toolbar_path);

	/* search entry gets created later if required */

	g_signal_connect (toolbar->priv->source, "notify::selected", G_CALLBACK (source_selected_cb), toolbar);
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBSourceToolbar *toolbar = RB_SOURCE_TOOLBAR (object);

	switch (prop_id) {
	case PROP_SOURCE:
		g_value_set_object (value, toolbar->priv->source);
		break;
	case PROP_UI_MANAGER:
		g_value_set_object (value, toolbar->priv->ui_manager);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBSourceToolbar *toolbar = RB_SOURCE_TOOLBAR (object);

	switch (prop_id) {
	case PROP_SOURCE:
		toolbar->priv->source = g_value_get_object (value);	/* don't take a ref */
		break;
	case PROP_UI_MANAGER:
		toolbar->priv->ui_manager = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_source_toolbar_init (RBSourceToolbar *toolbar)
{
	toolbar->priv = G_TYPE_INSTANCE_GET_PRIVATE (toolbar, RB_TYPE_SOURCE_TOOLBAR, RBSourceToolbarPrivate);

	toolbar->priv->search_value = -1;
}

static void
rb_source_toolbar_class_init (RBSourceToolbarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = impl_constructed;
	object_class->dispose = impl_dispose;
	object_class->finalize = impl_finalize;
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	/**
	 * RBSourceToolbar:source:
	 *
	 * The #RBSource the toolbar is associated with
	 */
	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "source",
							      "RBSource instance",
							      RB_TYPE_SOURCE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBSourceToolbar:ui-manager:
	 *
	 * The #GtkUIManager instance
	 */
	g_object_class_install_property (object_class,
					 PROP_UI_MANAGER,
					 g_param_spec_object ("ui-manager",
							      "ui manager",
							      "GtkUIManager instance",
							      GTK_TYPE_UI_MANAGER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_type_class_add_private (klass, sizeof (RBSourceToolbarPrivate));
}

/**
 * rb_source_toolbar_new:
 * @source: a #RBSource
 * @ui_manager: the #GtkUIManager
 *
 * Creates a new source toolbar for @source.  The toolbar does not
 * initially include a search entry.  Call #rb_source_toolbar_add_search_entry
 * to add one.  The toolbar content comes from the @RBSource:toolbar-path property.
 *
 * Return value: the #RBSourceToolbar
 */
RBSourceToolbar *
rb_source_toolbar_new (RBSource *source, GtkUIManager *ui_manager)
{
	GObject *object;
	object = g_object_new (RB_TYPE_SOURCE_TOOLBAR,
			       "source", source,
			       "ui-manager", ui_manager,
			       "column-spacing", 6,
			       "column-homogeneous", TRUE,
			       "row-spacing", 6,
			       "row-homogeneous", TRUE,
			       NULL);
	return RB_SOURCE_TOOLBAR (object);
}

static void
setup_search_popup (RBSourceToolbar *toolbar, GtkWidget *popup)
{
	GList *items;
	GSList *l;
	int active_value;

	toolbar->priv->search_popup = g_object_ref (popup);

	items = gtk_container_get_children (GTK_CONTAINER (toolbar->priv->search_popup));
	toolbar->priv->search_group = GTK_RADIO_ACTION (gtk_activatable_get_related_action (GTK_ACTIVATABLE (items->data)));
	g_list_free (items);

	active_value = gtk_radio_action_get_current_value (toolbar->priv->search_group);
	for (l = gtk_radio_action_get_group (toolbar->priv->search_group); l != NULL; l = l->next) {
		int value;
		g_object_get (G_OBJECT (l->data), "value", &value, NULL);
		if (value == active_value) {
			rb_search_entry_set_placeholder (toolbar->priv->search_entry,
							 gtk_action_get_label (GTK_ACTION (l->data)));
		}
	}

	g_signal_connect (toolbar->priv->search_entry, "show-popup", G_CALLBACK (show_popup_cb), toolbar);
}

static void
popup_add_widget_cb (GtkUIManager *ui_manager, GtkWidget *widget, RBSourceToolbar *toolbar)
{
	GtkWidget *popup;
	popup = gtk_ui_manager_get_widget (toolbar->priv->ui_manager, toolbar->priv->popup_path);

	if (popup) {
		setup_search_popup (toolbar, popup);
		g_signal_handlers_disconnect_by_func (ui_manager, G_CALLBACK (popup_add_widget_cb), toolbar);
	}
}


/**
 * rb_source_toolbar_add_search_entry:
 * @toolbar: a #RBSourceToolbar
 * @popup_path: the UI path for the search popup (or NULL)
 * @placeholder: the placeholder text for the search entry (or NULL)
 *
 * Adds a search entry to the toolbar.  If a popup path is specified,
 * clicking on the primary icon will show a menu allowing the user to
 * select a search type, and the placeholder text for the entry will
 * be the selected search description.  Otherwise, the specified placeholder
 * text will be displayed.
 */
void
rb_source_toolbar_add_search_entry (RBSourceToolbar *toolbar, const char *popup_path, const char *placeholder)
{
	g_assert (toolbar->priv->search_entry == NULL);

	toolbar->priv->search_entry = rb_search_entry_new (popup_path != NULL);
	gtk_widget_set_margin_right (GTK_WIDGET (toolbar->priv->search_entry), 6);
	gtk_grid_attach (GTK_GRID (toolbar), GTK_WIDGET (toolbar->priv->search_entry), 2, 0, 1, 1);

	if (placeholder) {
		rb_search_entry_set_placeholder (toolbar->priv->search_entry, placeholder);
	}

	g_signal_connect (toolbar->priv->search_entry, "search", G_CALLBACK (search_cb), toolbar);
	/* activate? */

	if (popup_path != NULL) {
		GtkWidget *popup;
		toolbar->priv->popup_path = g_strdup (popup_path);

		popup = gtk_ui_manager_get_widget (toolbar->priv->ui_manager, popup_path);
		if (popup != NULL) {
			setup_search_popup (toolbar, popup);
		} else {
			g_signal_connect (toolbar->priv->ui_manager, "add-widget", G_CALLBACK (popup_add_widget_cb), toolbar);
		}
	}
}

/**
 * rb_source_toolbar_clear_search_entry:
 * @toolbar: a #RBSourceToolbar
 *
 * Clears the search entry text.  Call this from RBSource:impl_reset_filters.
 */
void
rb_source_toolbar_clear_search_entry (RBSourceToolbar *toolbar)
{
	g_assert (toolbar->priv->search_entry != NULL);
	rb_search_entry_clear (toolbar->priv->search_entry);
}
