/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2010 Jonathan Matthew <jonathan@d14n.org>
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

#include "rb-display-page.h"
#include "rb-shell.h"
#include "rb-debug.h"
#include "rb-util.h"

G_DEFINE_ABSTRACT_TYPE (RBDisplayPage, rb_display_page, GTK_TYPE_BOX)

/**
 * SECTION:rbdisplaypage
 * @short_description: base class for items that appear in the display page tree
 *
 * This is the base class for items that appear in the display page tree and can
 * occupy the main display area.  Sources and source groups are display pages.
 * Other types of display, such as music visualization, could be implemented as
 * display pages too.
 *
 * The display page object itself is the widget shown in the main display area.
 * The icon and name properties control its appearance in the display page
 * tree, and its location is determined by its parent display page, the sorting
 * rules for its source group (if any), and insertion order.  The visibility property
 * controls whether the display page is actually shown in the display page tree at all.
 */

struct _RBDisplayPagePrivate
{
	char *name;
	gboolean visible;
	gboolean selected;
	GIcon *icon;
	RBDisplayPage *parent;

	GObject *plugin;
	RBShell *shell;

	gboolean deleted;

	GList *pending_children;
};

enum
{
	PROP_0,
	PROP_SHELL,
	PROP_NAME,
	PROP_ICON,
	PROP_VISIBLE,
	PROP_PARENT,
	PROP_PLUGIN,
	PROP_SELECTED,
};

enum
{
	STATUS_CHANGED,
	DELETED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

void
_rb_display_page_add_pending_child (RBDisplayPage *page, RBDisplayPage *child)
{
	page->priv->pending_children = g_list_append (page->priv->pending_children, child);
}

GList *
_rb_display_page_get_pending_children (RBDisplayPage *page)
{
	GList *c = page->priv->pending_children;
	page->priv->pending_children = NULL;
	return c;
}

/**
 * rb_display_age_receive_drag:
 * @page: a #RBDisplayPage
 * @data: the selection data
 *
 * This is called when the user drags something to the page.
 * Depending on the drag data type, the data might be a list of
 * #RhythmDBEntry objects, a list of URIs, or a list of album
 * or artist or genre names.
 *
 * Return value: TRUE if the page accepted the drag data
 */
gboolean
rb_display_page_receive_drag (RBDisplayPage *page, GtkSelectionData *data)
{
	RBDisplayPageClass *klass = RB_DISPLAY_PAGE_GET_CLASS (page);

	if (klass->receive_drag)
		return klass->receive_drag (page, data);
	else
		return FALSE;
}

/**
 * rb_display_page_delete_thyself:
 * @page: a #RBDisplayPage
 *
 * This is called when the page should delete itself.
 * The 'deleted' signal will be emitted, which removes the page
 * from the page model.  This will not actually dispose of the
 * page object, so reference counting must still be handled
 * correctly.
 */
void
rb_display_page_delete_thyself (RBDisplayPage *page)
{
	RBDisplayPageClass *klass;

	g_return_if_fail (page != NULL);
	if (page->priv->deleted) {
		rb_debug ("source has already been deleted");
		return;
	}
	page->priv->deleted = TRUE;

	klass = RB_DISPLAY_PAGE_GET_CLASS (page);
	klass->delete_thyself (page);

	g_signal_emit (G_OBJECT (page), signals[DELETED], 0);
}

/**
 * rb_display_page_can_remove:
 * @page: a #RBDisplayPage
 *
 * Called to check whether the user is able to remove the page
 *
 * Return value: %TRUE if the page can be removed
 */
gboolean
rb_display_page_can_remove (RBDisplayPage *page)
{
	RBDisplayPageClass *klass;
	klass = RB_DISPLAY_PAGE_GET_CLASS (page);
	if (klass->can_remove)
		return klass->can_remove (page);

	return FALSE;
}

/**
 * rb_display_page_remove:
 * @page: a #RBDisplayPage
 *
 * Called when the user requests removal of a page.
 */
void
rb_display_page_remove (RBDisplayPage *page)
{
	RBDisplayPageClass *klass;
	klass = RB_DISPLAY_PAGE_GET_CLASS (page);
	g_assert (klass->remove != NULL);
	klass->remove (page);
}

/**
 * rb_display_page_selectable:
 * @page: a #RBDisplayPage
 *
 * Checks if @page can be selected
 */
gboolean
rb_display_page_selectable (RBDisplayPage *page)
{
	RBDisplayPageClass *klass = RB_DISPLAY_PAGE_GET_CLASS (page);
	if (klass->selectable)
		return klass->selectable (page);
	else
		return TRUE;
}

/**
 * rb_display_page_selected:
 * @page: a #RBDisplayPage
 *
 * Called when the page is selected in the page tree.
 */
void
rb_display_page_selected (RBDisplayPage *page)
{
	RBDisplayPageClass *klass = RB_DISPLAY_PAGE_GET_CLASS (page);

	if (klass->selected)
		klass->selected (page);

	page->priv->selected = TRUE;
	g_object_notify (G_OBJECT (page), "selected");
}

/**
 * rb_display_page_deselected:
 * @page: a #RBDisplayPage
 *
 * Called when the page is deselected in the page tree.
 */
void
rb_display_page_deselected (RBDisplayPage *page)
{
	RBDisplayPageClass *klass = RB_DISPLAY_PAGE_GET_CLASS (page);

	if (klass->deselected)
		klass->deselected (page);

	page->priv->selected = FALSE;
	g_object_notify (G_OBJECT (page), "selected");
}

/**
 * rb_display_page_activate:
 * @page: a #RBDisplayPage
 *
 * Called when the page is activated (double clicked, etc.) in the page tree.
 */
void
rb_display_page_activate (RBDisplayPage *page)
{
	RBDisplayPageClass *klass = RB_DISPLAY_PAGE_GET_CLASS (page);

	if (klass->activate)
		klass->activate (page);
}


/**
 * rb_display_page_get_config_widget:
 * @page: a #RBDisplayPage
 * @prefs: the #RBShellPreferences object
 *
 * Source implementations can use this to return an optional
 * configuration widget. The widget will be displayed in a
 * page in the preferences dialog.
 *
 * Return value: (transfer none): configuration widget
 */
GtkWidget *
rb_display_page_get_config_widget (RBDisplayPage *page,
				   RBShellPreferences *prefs)
{
	RBDisplayPageClass *klass = RB_DISPLAY_PAGE_GET_CLASS (page);

	if (klass->get_config_widget) {
		return klass->get_config_widget (page, prefs);
	} else {
		return NULL;
	}
}

/**
 * rb_display_page_get_status:
 * @page: a #RBDisplayPage
 * @text: (inout) (allow-none) (transfer full): holds the returned status text
 * @busy: (inout) (allow-none): holds the busy status
 *
 * Retrieves status details for the page.
 **/
void
rb_display_page_get_status (RBDisplayPage *page,
			    char **text,
			    gboolean *busy)
{
	RBDisplayPageClass *klass = RB_DISPLAY_PAGE_GET_CLASS (page);

	if (klass->get_status)
		klass->get_status (page, text, busy);
}

/**
 * rb_display_page_notify_status_changed:
 * @page: a #RBDisplayPage
 *
 * Page implementations call this when their status bar information
 * changes.
 */
void
rb_display_page_notify_status_changed (RBDisplayPage *page)
{
	g_signal_emit (G_OBJECT (page), signals[STATUS_CHANGED], 0);
}

typedef void (*DisplayPageActionActivateCallback) (GSimpleAction *action, GVariant *parameters, RBDisplayPage *page);
typedef void (*DisplayPageActionChangeStateCallback) (GSimpleAction *action, GVariant *value, RBDisplayPage *page);

typedef struct {
	union {
		DisplayPageActionActivateCallback gaction;
		DisplayPageActionChangeStateCallback gactionstate;
	} u;
	gpointer shell;
} DisplayPageActionData;

static void
display_page_action_data_destroy (DisplayPageActionData *data)
{
	if (data->shell != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (data->shell), &data->shell);
	}
	g_slice_free (DisplayPageActionData, data);
}

static void
display_page_action_activate_cb (GSimpleAction *action, GVariant *parameters, DisplayPageActionData *data)
{
	RBDisplayPage *page;

	if (data->shell == NULL) {
		return;
	}

	g_object_get (data->shell, "selected-page", &page, NULL);
	if (page != NULL) {
		data->u.gaction (action, parameters, page);
		g_object_unref (page);
	}
}

static void
display_page_action_change_state_cb (GSimpleAction *action, GVariant *value, DisplayPageActionData *data)
{
	RBDisplayPage *page;

	if (data->shell == NULL) {
		return;
	}

	g_object_get (data->shell, "selected-page", &page, NULL);
	if (page != NULL) {
		data->u.gactionstate (action, value, page);
		g_object_unref (page);
	}
}

void
_rb_add_display_page_actions (GActionMap *map, GObject *shell, const GActionEntry *actions, gint n_entries)
{
	int i;
	for (i = 0; i < n_entries; i++) {
		GSimpleAction *action;
		const GVariantType *parameter_type;
		DisplayPageActionData *page_action_data;

		if (g_action_map_lookup_action (map, actions[i].name) != NULL) {
			/* action was already added */
			continue;
		}

		if (actions[i].parameter_type) {
			parameter_type = G_VARIANT_TYPE (actions[i].parameter_type);
		} else {
			parameter_type = NULL;
		}

		if (actions[i].state) {
			GVariant *state;
			GError *error = NULL;
			state = g_variant_parse (NULL, actions[i].state, NULL, NULL, &error);
			if (state == NULL) {
				g_critical ("could not parse state value '%s' for action "
					    "%s: %s",
					    actions[i].state, actions[i].name, error->message);
				g_error_free (error);
				continue;
			}
			action = g_simple_action_new_stateful (actions[i].name,
							       parameter_type,
							       state);
		} else {
			action = g_simple_action_new (actions[i].name, parameter_type);
		}

		if (actions[i].activate) {
			GClosure *closure;
			page_action_data = g_slice_new0 (DisplayPageActionData);
			page_action_data->u.gaction = (DisplayPageActionActivateCallback) actions[i].activate;
			page_action_data->shell = shell;
			g_object_add_weak_pointer (shell, &page_action_data->shell);

			closure = g_cclosure_new (G_CALLBACK (display_page_action_activate_cb),
						  page_action_data,
						  (GClosureNotify) display_page_action_data_destroy);
			g_signal_connect_closure (action, "activate", closure, FALSE);
		}

		if (actions[i].change_state) {
			GClosure *closure;
			page_action_data = g_slice_new0 (DisplayPageActionData);
			page_action_data->u.gactionstate = (DisplayPageActionChangeStateCallback) actions[i].change_state;
			page_action_data->shell = shell;
			g_object_add_weak_pointer (shell, &page_action_data->shell);

			closure = g_cclosure_new (G_CALLBACK (display_page_action_change_state_cb),
						  page_action_data,
						  (GClosureNotify) display_page_action_data_destroy);
			g_signal_connect_closure (action, "change-state", closure, FALSE);
		}

		g_action_map_add_action (map, G_ACTION (action));
		g_object_unref (action);
	}
}

/**
 * rb_display_page_set_icon_name:
 * @page: a #RBDisplayPage
 * @icon_name: icon name to use
 *
 * Sets the icon for the page to the specified icon name.
 */
void
rb_display_page_set_icon_name (RBDisplayPage *page, const char *icon_name)
{
	GIcon *icon;

	icon = g_themed_icon_new (icon_name);
	g_object_set (page, "icon", icon, NULL);
	g_object_unref (icon);
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBDisplayPage *page = RB_DISPLAY_PAGE (object);

	switch (prop_id) {
	case PROP_SHELL:
		g_value_set_object (value, page->priv->shell);
		break;
	case PROP_NAME:
		g_value_set_string (value, page->priv->name);
		break;
	case PROP_ICON:
		g_value_set_object (value, page->priv->icon);
		break;
	case PROP_VISIBLE:
		g_value_set_boolean (value, page->priv->visible);
		break;
	case PROP_PARENT:
		g_value_set_object (value, page->priv->parent);
		break;
	case PROP_PLUGIN:
		g_value_set_object (value, page->priv->plugin);
		break;
	case PROP_SELECTED:
		g_value_set_boolean (value, page->priv->selected);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBDisplayPage *page = RB_DISPLAY_PAGE (object);

	switch (prop_id) {
	case PROP_SHELL:
		page->priv->shell = g_value_get_object (value);
		break;
	case PROP_NAME:
		g_free (page->priv->name);
		page->priv->name = g_value_dup_string (value);
		break;
	case PROP_ICON:
		g_clear_object (&page->priv->icon);
		page->priv->icon = g_value_dup_object (value);
		break;
	case PROP_VISIBLE:
		page->priv->visible = g_value_get_boolean (value);
		break;
	case PROP_PARENT:
		page->priv->parent = g_value_get_object (value);
		break;
	case PROP_PLUGIN:
		page->priv->plugin = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_delete_thyself (RBDisplayPage *page)
{
}

static void
impl_selected (RBDisplayPage *page)
{
}

static void
impl_deselected (RBDisplayPage *page)
{
}

static void
impl_dispose (GObject *object)
{
	RBDisplayPage *page;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_DISPLAY_PAGE (object));
	page = RB_DISPLAY_PAGE (object);

	rb_debug ("Disposing page %s", page->priv->name);
	g_clear_object (&page->priv->icon);

	G_OBJECT_CLASS (rb_display_page_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	RBDisplayPage *page;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_DISPLAY_PAGE (object));
	page = RB_DISPLAY_PAGE (object);

	rb_debug ("finalizing page %s", page->priv->name);

	g_free (page->priv->name);

	G_OBJECT_CLASS (rb_display_page_parent_class)->finalize (object);
}

static void
rb_display_page_init (RBDisplayPage *page)
{
	gtk_orientable_set_orientation (GTK_ORIENTABLE (page), GTK_ORIENTATION_HORIZONTAL);
	page->priv = G_TYPE_INSTANCE_GET_PRIVATE (page, RB_TYPE_DISPLAY_PAGE, RBDisplayPagePrivate);

	page->priv->visible = TRUE;
}

static void
rb_display_page_class_init (RBDisplayPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = impl_dispose;
	object_class->finalize = impl_finalize;

	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	klass->selected = impl_selected;
	klass->deselected = impl_deselected;
	klass->delete_thyself = impl_delete_thyself;

	/**
	 * RBDisplayPage:shell:
	 *
	 * The rhythmbox shell object
	 */
	g_object_class_install_property (object_class,
					 PROP_SHELL,
					 g_param_spec_object ("shell",
							      "RBShell",
							      "RBShell object",
							      RB_TYPE_SHELL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBDisplayPage:name:
	 *
	 * Page name as displayed in the tree
	 */
	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "UI name",
							      "Interface name",
							      NULL,
							      G_PARAM_READWRITE));
	/**
	 * RBDisplayPage:icon:
	 *
	 * Icon to display in the page tree
	 */
	g_object_class_install_property (object_class,
					 PROP_ICON,
					 g_param_spec_object ("icon",
							      "Icon",
							      "Page icon",
							      G_TYPE_ICON,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	/**
	 * RBDisplayPage:visibility:
	 *
	 * If FALSE, the page will not be displayed in the tree
	 */
	g_object_class_install_property (object_class,
					 PROP_VISIBLE,
					 g_param_spec_boolean ("visibility",
							       "visibility",
							       "Whether the page should be displayed in the tree",
							       TRUE,
							       G_PARAM_READWRITE));
	/**
	 * RBDisplayPage:parent:
	 *
	 * The parent page in the tree (may be NULL)
	 */
	g_object_class_install_property (object_class,
					 PROP_PARENT,
					 g_param_spec_object ("parent",
							      "Parent",
							      "Parent page",
							      RB_TYPE_DISPLAY_PAGE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBDisplayPage:plugin:
	 *
	 * The plugin that created this page.
	 */
	g_object_class_install_property (object_class,
					 PROP_PLUGIN,
					 g_param_spec_object ("plugin",
							      "plugin instance",
							      "plugin instance that created the page",
							      G_TYPE_OBJECT,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	/**
	 * RBDisplayPage:selected:
	 *
	 * TRUE when the page is selected in the page tree.
	 */
	g_object_class_install_property (object_class,
					 PROP_SELECTED,
					 g_param_spec_boolean ("selected",
							       "selected",
							       "Whether the page is currently selected",
							       FALSE,
							       G_PARAM_READABLE));
	/**
	 * RBDisplayPage::deleted:
	 * @page: the #RBDisplayPage
	 *
	 * Emitted when the page is being deleted.
	 */
	signals[DELETED] =
		g_signal_new ("deleted",
			      RB_TYPE_DISPLAY_PAGE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBDisplayPageClass, deleted),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);
	/**
	 * RBDisplayPage::status-changed:
	 * @page: the #RBDisplayPage
	 *
	 * Emitted when the page's status changes.
	 */
	signals[STATUS_CHANGED] =
		g_signal_new ("status_changed",
			      RB_TYPE_DISPLAY_PAGE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBDisplayPageClass, status_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);

	g_type_class_add_private (object_class, sizeof (RBDisplayPagePrivate));
}
