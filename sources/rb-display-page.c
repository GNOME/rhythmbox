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

G_DEFINE_ABSTRACT_TYPE (RBDisplayPage, rb_display_page, GTK_TYPE_HBOX)

/**
 * SECTION:rb-display-page
 * @short_description: base class for items that appear in the display page tree
 *
 * This is the base class for items that appear in the display page tree and can
 * occupy the main display area.  Sources and source groups are display pages.
 * Other types of display, such as music visualization, could be implemented as
 * display pages too.
 *
 * The display page object itself is the widget shown in the main display area.
 * The pixbuf and name properties control its appearance in the display page
 * tree, and its location is determined by its parent display page, the sorting
 * rules for its source group (if any), and insertion order.  The visibility property
 * controls whether the display page is actually shown in the display page tree at all.
 */

struct _RBDisplayPagePrivate
{
	char *name;
	gboolean visible;
	gboolean selected;
	GdkPixbuf *pixbuf;
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
	PROP_UI_MANAGER,
	PROP_NAME,
	PROP_PIXBUF,
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
 * rb_display_page_show_popup:
 * @page: a #RBDisplayPage
 *
 * Called when the user performs an action (such as right-clicking)
 * that should result in a popup menu being displayed for the page.
 *
 * Return value: TRUE if the page managed to display a popup
 */
gboolean
rb_display_page_show_popup (RBDisplayPage *page)
{
	RBDisplayPageClass *klass = RB_DISPLAY_PAGE_GET_CLASS (page);

	if (klass->show_popup)
		return klass->show_popup (page);
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
 * @progress_text: (inout) (allow-none) (transfer full): holds the returned text for the progress bar
 * @progress: (inout) (allow-none): holds the progress value
 *
 * Retrieves the details to display in the status bar for the page.
 * If the progress value returned is less than zero, the progress bar
 * will pulse.  If the progress value is greater than or equal to 1,
 * the progress bar will be hidden.
 **/
void
rb_display_page_get_status (RBDisplayPage *page,
			    char **text,
			    char **progress_text,
			    float *progress)
{
	RBDisplayPageClass *klass = RB_DISPLAY_PAGE_GET_CLASS (page);

	if (klass->get_status)
		klass->get_status (page, text, progress_text, progress);
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

/**
 * _rb_display_page_show_popup:
 * @page: a #RBDisplayPage
 * @ui_path: UI path to the popup to show
 *
 * Page implementations can use this as a shortcut to
 * display a popup that has been loaded into the UI manager.
 */
void
_rb_display_page_show_popup (RBDisplayPage *page, const char *ui_path)
{
	GtkUIManager *uimanager;

	g_object_get (page->priv->shell, "ui-manager", &uimanager, NULL);
	rb_gtk_action_popup_menu (uimanager, ui_path);
	g_object_unref (uimanager);
}

static GtkActionGroup *
find_action_group (GtkUIManager *uimanager, const char *group_name)
{
	GList *actiongroups;
	GList *i;
	actiongroups = gtk_ui_manager_get_action_groups (uimanager);

	/* Don't create the action group if it's already registered */
	for (i = actiongroups; i != NULL; i = i->next) {
		const char *name;

		name = gtk_action_group_get_name (GTK_ACTION_GROUP (i->data));
		if (g_strcmp0 (name, group_name) == 0) {
			return GTK_ACTION_GROUP (i->data);
		}
	}

	return NULL;
}

/**
 * _rb_display_page_register_action_group:
 * @page: a #RBDisplayPage
 * @group_name: action group name
 * @actions: array of GtkActionEntry structures for the action group
 * @num_actions: number of actions in the @actions array
 * @user_data: user data to use for action signal handlers
 *
 * Creates and registers a GtkActionGroup for the page.
 *
 * Return value: the created action group
 */
GtkActionGroup *
_rb_display_page_register_action_group (RBDisplayPage *page,
					const char *group_name,
					GtkActionEntry *actions,
					int num_actions,
					gpointer user_data)
{
	GtkUIManager *uimanager;
	GtkActionGroup *group;

	g_return_val_if_fail (group_name != NULL, NULL);

	g_object_get (page, "ui-manager", &uimanager, NULL);
	group = find_action_group (uimanager, group_name);
	if (group == NULL) {
		group = gtk_action_group_new (group_name);
		gtk_action_group_set_translation_domain (group,
							 GETTEXT_PACKAGE);
		if (actions != NULL) {
			gtk_action_group_add_actions (group,
						      actions, num_actions,
						      user_data);
		}
		gtk_ui_manager_insert_action_group (uimanager, group, 0);
	} else {
		g_object_ref (group);
	}
	g_object_unref (uimanager);

	return group;
}

typedef void (*DisplayPageActionCallback) (GtkAction *action, RBDisplayPage *page);

typedef struct {
	DisplayPageActionCallback callback;
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
display_page_action_cb (GtkAction *action, DisplayPageActionData *data)
{
	RBDisplayPage *page;

	if (data->shell == NULL) {
		return;
	}

	/* get current page */
	g_object_get (data->shell, "selected-page", &page, NULL);
	if (page != NULL) {
		data->callback (action, page);
		g_object_unref (page);
	}
}

/**
 * _rb_action_group_add_display_page_actions:
 * @group: a #GtkActionGroup
 * @shell: the #RBShell
 * @actions: array of GtkActionEntry structures for the action group
 * @num_actions: number of actions in the @actions array
 *
 * Adds actions to an action group where the action callback is
 * called with the current selected display page.  This can safely be called
 * multiple times on the same action group.
 */
void
_rb_action_group_add_display_page_actions (GtkActionGroup *group,
					   GObject *shell,
					   GtkActionEntry *actions,
					   int num_actions)
{
	int i;
	for (i = 0; i < num_actions; i++) {
		GtkAction *action;
		const char *label;
		const char *tooltip;
		DisplayPageActionData *page_action_data;

		if (gtk_action_group_get_action (group, actions[i].name) != NULL) {
			/* action was already added */
			continue;
		}

		label = gtk_action_group_translate_string (group, actions[i].label);
		tooltip = gtk_action_group_translate_string (group, actions[i].tooltip);

		action = gtk_action_new (actions[i].name, label, tooltip, NULL);
		if (actions[i].stock_id != NULL) {
			g_object_set (action, "stock-id", actions[i].stock_id, NULL);
			if (gtk_icon_theme_has_icon (gtk_icon_theme_get_default (),
						     actions[i].stock_id)) {
				g_object_set (action, "icon-name", actions[i].stock_id, NULL);
			}
		}

		if (actions[i].callback) {
			GClosure *closure;
			page_action_data = g_slice_new0 (DisplayPageActionData);
			page_action_data->callback = (DisplayPageActionCallback) actions[i].callback;
			page_action_data->shell = shell;
			g_object_add_weak_pointer (shell, &page_action_data->shell);

			closure = g_cclosure_new (G_CALLBACK (display_page_action_cb),
						  page_action_data,
						  (GClosureNotify) display_page_action_data_destroy);
			g_signal_connect_closure (action, "activate", closure, FALSE);
		}

		gtk_action_group_add_action_with_accel (group, action, actions[i].accelerator);
		g_object_unref (action);
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBDisplayPage *page = RB_DISPLAY_PAGE (object);

	switch (prop_id) {
	case PROP_SHELL:
		g_value_set_object (value, page->priv->shell);
		break;
	case PROP_UI_MANAGER:
		{
			GtkUIManager *manager;
			g_object_get (page->priv->shell, "ui-manager", &manager, NULL);
			g_value_set_object (value, manager);
			g_object_unref (manager);
			break;
		}
	case PROP_NAME:
		g_value_set_string (value, page->priv->name);
		break;
	case PROP_PIXBUF:
		g_value_set_object (value, page->priv->pixbuf);
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
	case PROP_PIXBUF:
		if (page->priv->pixbuf) {
			g_object_unref (page->priv->pixbuf);
		}
		page->priv->pixbuf = g_value_dup_object (value);
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
	if (page->priv->pixbuf != NULL) {
		g_object_unref (page->priv->pixbuf);
		page->priv->pixbuf = NULL;
	}

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
	 * RBDisplayPage:ui-manager:
	 *
	 * The Gtk UIManager object
	 */
	g_object_class_install_property (object_class,
					 PROP_UI_MANAGER,
					 g_param_spec_object ("ui-manager",
							      "GtkUIManager",
							      "GtkUIManager object",
							      GTK_TYPE_UI_MANAGER,
							      G_PARAM_READABLE));
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
	 * RBDisplayPage:pixbuf:
	 *
	 * Pixbuf to display in the page tree
	 */
	g_object_class_install_property (object_class,
					 PROP_PIXBUF,
					 g_param_spec_object ("pixbuf",
							      "Pixbuf",
							      "Page pixbuf",
							      GDK_TYPE_PIXBUF,
							      G_PARAM_READWRITE));
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
			      g_cclosure_marshal_VOID__VOID,
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
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	g_type_class_add_private (object_class, sizeof (RBDisplayPagePrivate));
}

/* introspection annotations for vmethods */

/**
 * impl_get_status:
 * @source: a #RBSource
 * @text: (inout) (allow-none) (transfer full): holds the returned status text
 * @progress_text: (inout) (allow-none) (transfer full): holds the returned text for the progress bar
 * @progress: (inout): holds the progress value
 */

/**
 * impl_get_config_widget:
 * @source: a #RBSource
 * @prefs: a #RBShellPreferences
 *
 * Return value: (transfer none): configuration widget
 */
