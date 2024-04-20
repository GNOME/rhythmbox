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
#include <widgets/rb-button-bar.h>
#include <lib/rb-util.h>

static void rb_source_toolbar_class_init (RBSourceToolbarClass *klass);
static void rb_source_toolbar_init (RBSourceToolbar *toolbar);

struct _RBSourceToolbarPrivate
{
	GtkAccelGroup *accel_group;
	RBDisplayPage *page;
	RBSearchEntry *search_entry;
	GtkWidget *search_popup;
	GtkWidget *button_bar;
	char *popup_path;

	/* search state */
	int search_value;
	gulong search_change_cb_id;
	RBSourceSearch *active_search;
	char *search_text;

	GAction *search_action;
};

G_DEFINE_TYPE (RBSourceToolbar, rb_source_toolbar, GTK_TYPE_GRID)

/**
 * SECTION:rbsourcetoolbar
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
	PROP_PAGE,
	PROP_ACCEL_GROUP,
};

static void
source_selected_cb (GObject *object, GParamSpec *pspec, RBSourceToolbar *toolbar)
{
	gboolean selected;

	g_object_get (object, "selected", &selected, NULL);

	if (selected) {
		if (toolbar->priv->search_entry != NULL) {
			rb_search_entry_set_mnemonic (toolbar->priv->search_entry, TRUE);

			gtk_widget_add_accelerator (GTK_WIDGET (toolbar->priv->search_entry),
						    "grab-focus",
						    toolbar->priv->accel_group,
						    gdk_unicode_to_keyval ('f'),
						    GDK_CONTROL_MASK,
						    0);
		}

		if (toolbar->priv->button_bar != NULL) {
			rb_button_bar_add_accelerators (RB_BUTTON_BAR (toolbar->priv->button_bar),
							toolbar->priv->accel_group);
		}
	} else {
		if (toolbar->priv->search_entry != NULL) {
			rb_search_entry_set_mnemonic (toolbar->priv->search_entry, FALSE);

			gtk_widget_remove_accelerator (GTK_WIDGET (toolbar->priv->search_entry),
						       toolbar->priv->accel_group,
						       gdk_unicode_to_keyval ('f'),
						       GDK_CONTROL_MASK);
		}

		if (toolbar->priv->button_bar != NULL) {
			rb_button_bar_remove_accelerators (RB_BUTTON_BAR (toolbar->priv->button_bar),
							   toolbar->priv->accel_group);
		}
	}
}

static void
search_cb (RBSearchEntry *search_entry, const char *text, RBSourceToolbar *toolbar)
{
	g_return_if_fail (RB_IS_SOURCE (toolbar->priv->page));

	rb_source_search (RB_SOURCE (toolbar->priv->page), toolbar->priv->active_search, toolbar->priv->search_text, text);

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

	g_clear_object (&toolbar->priv->accel_group);
	g_clear_object (&toolbar->priv->search_popup);

	G_OBJECT_CLASS (rb_source_toolbar_parent_class)->dispose (object);
}

static void
impl_constructed (GObject *object)
{
	RBSourceToolbar *toolbar;
	GtkWidget *blank;
	GMenuModel *toolbar_menu;

	RB_CHAIN_GOBJECT_METHOD (rb_source_toolbar_parent_class, constructed, object);

	toolbar = RB_SOURCE_TOOLBAR (object);

	g_object_get (toolbar->priv->page,
		      "toolbar-menu", &toolbar_menu,
		      NULL);
	if (toolbar_menu != NULL) {
		toolbar->priv->button_bar = rb_button_bar_new (toolbar_menu, G_OBJECT (toolbar->priv->page));
		gtk_widget_show_all (toolbar->priv->button_bar);
		gtk_grid_attach (GTK_GRID (toolbar), toolbar->priv->button_bar, 0, 0, 2 ,1);
		g_object_unref (toolbar_menu);
	} else {
		blank = gtk_toolbar_new ();
		gtk_widget_set_hexpand (blank, TRUE);
		gtk_toolbar_set_style (GTK_TOOLBAR (blank), GTK_TOOLBAR_TEXT);
		gtk_grid_attach (GTK_GRID (toolbar), blank, 0, 0, 2 ,1);
	}

	/* search entry gets created later if required */

	g_signal_connect (toolbar->priv->page, "notify::selected", G_CALLBACK (source_selected_cb), toolbar);
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBSourceToolbar *toolbar = RB_SOURCE_TOOLBAR (object);

	switch (prop_id) {
	case PROP_PAGE:
		g_value_set_object (value, toolbar->priv->page);
		break;
	case PROP_ACCEL_GROUP:
		g_value_set_object (value, toolbar->priv->accel_group);
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
	case PROP_PAGE:
		toolbar->priv->page = g_value_get_object (value);	/* don't take a ref */
		break;
	case PROP_ACCEL_GROUP:
		toolbar->priv->accel_group = g_value_dup_object (value);
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
	 * RBSourceToolbar:page:
	 *
	 * The #RBDisplayPage the toolbar is associated with
	 */
	g_object_class_install_property (object_class,
					 PROP_PAGE,
					 g_param_spec_object ("page",
							      "page",
							      "RBDisplayPage instance",
							      RB_TYPE_DISPLAY_PAGE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBSourceToolbar:accel-group:
	 *
	 * The #GtkAccelGroup to add accelerators to
	 */
	g_object_class_install_property (object_class,
					 PROP_ACCEL_GROUP,
					 g_param_spec_object ("accel-group",
							      "accel group",
							      "GtkAccelGroup instance",
							      GTK_TYPE_ACCEL_GROUP,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_type_class_add_private (klass, sizeof (RBSourceToolbarPrivate));
}

/**
 * rb_source_toolbar_new:
 * @page: a #RBDisplayPage
 * @accel_group: a #GtkAccelGroup to add accelerators to
 *
 * Creates a new source toolbar for @page.  The toolbar does not
 * initially include a search entry.  Call #rb_source_toolbar_add_search_entry
 * to add one.  The toolbar content comes from the @RBSource:toolbar-menu property.
 *
 * Return value: the #RBSourceToolbar
 */
RBSourceToolbar *
rb_source_toolbar_new (RBDisplayPage *page, GtkAccelGroup *accel_group)
{
	GObject *object;
	object = g_object_new (RB_TYPE_SOURCE_TOOLBAR,
			       "page", page,
			       "accel-group", accel_group,
			       "column-spacing", 6,
			       "column-homogeneous", TRUE,
			       "row-spacing", 6,
			       "row-homogeneous", TRUE,
			       "margin-start", 6,
			       "margin-end", 6,
			       NULL);
	return RB_SOURCE_TOOLBAR (object);
}

static void
search_state_notify_cb (GObject *action, GParamSpec *pspec, RBSourceToolbar *toolbar)
{
	GVariant *state;

	/* get search for current state */
	state = g_action_get_state (G_ACTION (action));
	toolbar->priv->active_search = rb_source_search_get_by_name (g_variant_get_string (state, NULL));
	g_variant_unref (state);

	if (toolbar->priv->search_text != NULL) {
		rb_source_search (RB_SOURCE (toolbar->priv->page), toolbar->priv->active_search, NULL, toolbar->priv->search_text);
	}

	if (toolbar->priv->active_search != NULL) {
		rb_search_entry_set_placeholder (toolbar->priv->search_entry, rb_source_search_get_description (toolbar->priv->active_search));
	} else {
		rb_search_entry_set_placeholder (toolbar->priv->search_entry, NULL);	/* ? */
	}
}

static void
add_search_entry (RBSourceToolbar *toolbar, gboolean menu)
{
	g_assert (toolbar->priv->search_entry == NULL);

	toolbar->priv->search_entry = rb_search_entry_new (menu);
	gtk_grid_attach (GTK_GRID (toolbar), GTK_WIDGET (toolbar->priv->search_entry), 2, 0, 1, 1);

	g_signal_connect (toolbar->priv->search_entry, "search", G_CALLBACK (search_cb), toolbar);
}

/**
 * rb_source_toolbar_add_search_entry_menu:
 * @toolbar: a #RBSourceToolbar
 * @search_menu: a #GMenu containing search items
 * @search_action: the #GAction for search state
 *
 * Adds a search entry to the toolbar.
 */
void
rb_source_toolbar_add_search_entry_menu (RBSourceToolbar *toolbar, GMenuModel *search_menu, GAction *search_action)
{
	g_return_if_fail (search_menu != NULL);
	g_return_if_fail (search_action != NULL);

	add_search_entry (toolbar, TRUE);

	toolbar->priv->search_popup = gtk_menu_new_from_model (search_menu);
	gtk_menu_attach_to_widget (GTK_MENU (toolbar->priv->search_popup), GTK_WIDGET (toolbar), NULL);
	g_object_ref_sink (toolbar->priv->search_popup);
	toolbar->priv->search_action = g_object_ref (search_action);

	g_signal_connect (toolbar->priv->search_entry, "show-popup", G_CALLBACK (show_popup_cb), toolbar);
	g_signal_connect (toolbar->priv->search_action, "notify::state", G_CALLBACK (search_state_notify_cb), toolbar);
	search_state_notify_cb (G_OBJECT (toolbar->priv->search_action), NULL, toolbar);
}

/**
 * rb_source_toolbar_add_search_entry:
 * @toolbar: a #RBSourceToolbar
 * @placeholder: the placeholder text for the search entry (or NULL)
 *
 * Adds a search entry with no search type menu.
 */
void
rb_source_toolbar_add_search_entry (RBSourceToolbar *toolbar, const char *placeholder)
{
	add_search_entry (toolbar, FALSE);
	rb_search_entry_set_placeholder (toolbar->priv->search_entry, placeholder);
}

/**
 * rb_source_toolbar_clear_search_entry:
 * @toolbar: a #RBSourceToolbar
 *
 * Clears the search entry text.  Call this from RBSource:reset_filters.
 */
void
rb_source_toolbar_clear_search_entry (RBSourceToolbar *toolbar)
{
	g_assert (toolbar->priv->search_entry != NULL);
	rb_search_entry_clear (toolbar->priv->search_entry);
	g_free (toolbar->priv->search_text);
	toolbar->priv->search_text = NULL;
}
