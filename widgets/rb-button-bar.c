/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2012 Jonathan Matthew <jonathan@d14n.org>
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

#include <shell/rb-application.h>
#include <widgets/rb-button-bar.h>
#include <lib/rb-util.h>

static void rb_button_bar_class_init (RBButtonBarClass *klass);
static void rb_button_bar_init (RBButtonBar *bar);

static void build_button_bar (RBButtonBar *bar);

struct _RBButtonBarPrivate
{
	GObject *target;
	GtkSizeGroup *size_group;
	GMenuModel *model;
	GHashTable *handlers;

	int position;
};

G_DEFINE_TYPE (RBButtonBar, rb_button_bar, GTK_TYPE_GRID);

enum {
	PROP_0,
	PROP_MODEL,
	PROP_TARGET
};

static void
clear_handlers (RBButtonBar *bar)
{
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init (&iter, bar->priv->handlers);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		gulong id = (gulong)key;
		g_signal_handler_disconnect (value, id);
	}

	g_hash_table_remove_all (bar->priv->handlers);
}

static void
clear_button_bar (RBButtonBar *bar)
{
	GList *c, *l;

	c = gtk_container_get_children (GTK_CONTAINER (bar));
	for (l = c; l != NULL; l = l->next) {
		if (!GTK_IS_LABEL (l->data))
			gtk_size_group_remove_widget (bar->priv->size_group, l->data);
		gtk_container_remove (GTK_CONTAINER (bar), l->data);
	}
	g_list_free (c);

	bar->priv->position = 0;
}

static void
signal_button_clicked_cb (GtkButton *button, RBButtonBar *bar)
{
	guint signal_id;
	signal_id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (button), "rb-signal-bind-id"));
	g_signal_emit (bar->priv->target, signal_id, 0);
}

static void
items_changed_cb (GMenuModel *model, int position, int added, int removed, RBButtonBar *bar)
{
	clear_handlers (bar);
	clear_button_bar (bar);
	build_button_bar (bar);
}

static gboolean
append_menu (RBButtonBar *bar, GMenuModel *menu, gboolean need_separator)
{
	int i;
	gulong id;

	id = g_signal_connect (menu, "items-changed", G_CALLBACK (items_changed_cb), bar);
	g_hash_table_insert (bar->priv->handlers, (gpointer)id, g_object_ref (menu));

	for (i = 0; i < g_menu_model_get_n_items (menu); i++) {
		char *label_text;
		char *accel;
		GtkWidget *button;
		GtkWidget *label;
		GMenuModel *submenu;

		/* recurse into sections */
		submenu = g_menu_model_get_item_link (menu, i, G_MENU_LINK_SECTION);
		if (submenu != NULL) {
			need_separator = append_menu (bar, submenu, TRUE);
			continue;
		}

		/* if this item and the previous item are in different sections, add
		 * a separator between them.  this may not be a good idea.
		 */
		if (need_separator) {
			GtkWidget *sep;

			if (bar->priv->position > 0) {
				sep = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
				gtk_widget_show (sep);
				g_object_set (sep, "margin-start", 6, "margin-end", 6, NULL);
				gtk_grid_attach (GTK_GRID (bar), sep, bar->priv->position++, 0, 1, 1);
			}

			need_separator = FALSE;
		}

		button = NULL;

		/* submenus become menu buttons, normal items become buttons */
		submenu = g_menu_model_get_item_link (menu, i, G_MENU_LINK_SUBMENU);

		if (submenu != NULL) {
			button = gtk_menu_button_new ();
			gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (button), submenu);

			g_object_set_data_full (G_OBJECT (button), "rb-menu-model", g_object_ref (submenu), (GDestroyNotify)g_object_unref);
		} else {
			GMenuAttributeIter *iter;
			const char *name;
			GVariant *value;
			char *str;
			guint signal_id;
		
			/* we can't do more than one of action and rb-property-bind
			 * and rb-signal-bind, so just do whichever turns up first
			 * in the iterator
			 */
			iter = g_menu_model_iterate_item_attributes (menu, i);
			while (g_menu_attribute_iter_get_next (iter, &name, &value)) {
				if (g_str_equal (name, "action")) {
					button = gtk_button_new ();
					g_variant_get (value, "s", &str, NULL);
					gtk_actionable_set_action_name (GTK_ACTIONABLE (button), str);
					/* action target too somehow? */
					g_free (str);
					break;
				} else if (g_str_equal (name, "rb-property-bind")) {
					/* property has to be a boolean, can't do inverts, etc. etc. */
					button = gtk_toggle_button_new ();
					g_variant_get (value, "s", &str, NULL);
					g_object_bind_property (bar->priv->target, str,
								button, "active",
								G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
					g_free (str);
					break;
				} else if (g_str_equal (name, "rb-signal-bind")) {
					button = gtk_button_new ();
					g_variant_get (value, "s", &str, NULL);
					signal_id = g_signal_lookup (str, G_OBJECT_TYPE (bar->priv->target));
					if (signal_id != 0) {
						g_object_set_data (G_OBJECT (button), "rb-signal-bind-id", GUINT_TO_POINTER (signal_id));
						g_signal_connect (button, "clicked", G_CALLBACK (signal_button_clicked_cb), bar);
					}
					g_free (str);
					break;
				}
			}

			g_object_unref (iter);
		}

		if (button == NULL) {
			g_warning ("no idea what's going on here");
			continue;
		}

		gtk_widget_set_hexpand (button, FALSE);
		gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);

		label_text = NULL;
		g_menu_model_get_item_attribute (menu, i, "label", "s", &label_text);
		label = gtk_label_new (g_dgettext (NULL, label_text));
		g_object_set (label, "margin-start", 6, "margin-end", 6, NULL);
		gtk_container_add (GTK_CONTAINER (button), label);

		if (g_menu_model_get_item_attribute (menu, i, "accel", "s", &accel)) {
			g_object_set_data_full (G_OBJECT (button), "rb-accel", accel, (GDestroyNotify) g_free);
		}

		gtk_widget_show_all (button);
		gtk_size_group_add_widget (bar->priv->size_group, button);
		gtk_grid_attach (GTK_GRID (bar), button, bar->priv->position++, 0, 1, 1);

		g_free (label_text);
	}

	return need_separator;
}

static void
build_button_bar (RBButtonBar *bar)
{
	GtkWidget *waste;

	append_menu (bar, bar->priv->model, FALSE);

	waste = gtk_label_new ("");
	gtk_widget_set_hexpand (waste, TRUE);
	gtk_widget_show (waste);
	gtk_grid_attach (GTK_GRID (bar), waste, bar->priv->position++, 0, 1, 1);
}


static void
impl_constructed (GObject *object)
{
	RBButtonBar *bar;

	RB_CHAIN_GOBJECT_METHOD (rb_button_bar_parent_class, constructed, object);

	bar = RB_BUTTON_BAR (object);

	bar->priv->size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	bar->priv->handlers = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);

	build_button_bar (bar);
}

static void
impl_dispose (GObject *object)
{
	RBButtonBar *bar = RB_BUTTON_BAR (object);

	clear_handlers (bar);
	g_clear_object (&bar->priv->model);
	G_OBJECT_CLASS (rb_button_bar_parent_class)->dispose (object);
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBButtonBar *bar = RB_BUTTON_BAR (object);
	
	switch (prop_id) {
	case PROP_MODEL:
		g_value_set_object (value, bar->priv->model);
		break;
	case PROP_TARGET:
		g_value_set_object (value, bar->priv->target);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBButtonBar *bar = RB_BUTTON_BAR (object);
	
	switch (prop_id) {
	case PROP_MODEL:
		bar->priv->model = g_value_dup_object (value);
		break;
	case PROP_TARGET:
		/* we're inside the target object usually, so don't ref it */
		bar->priv->target = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}



static void
rb_button_bar_init (RBButtonBar *bar)
{
	bar->priv = G_TYPE_INSTANCE_GET_PRIVATE (bar, RB_TYPE_BUTTON_BAR, RBButtonBarPrivate);
}

static void
rb_button_bar_class_init (RBButtonBarClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (RBButtonBarPrivate));

	gobject_class->constructed = impl_constructed;
	gobject_class->dispose = impl_dispose;
	gobject_class->set_property = impl_set_property;
	gobject_class->get_property = impl_get_property;

	g_object_class_install_property (gobject_class,
					 PROP_MODEL,
					 g_param_spec_object ("model",
							      "model",
							      "model",
							      G_TYPE_MENU_MODEL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (gobject_class,
					 PROP_TARGET,
					 g_param_spec_object ("target",
							      "target",
							      "binding target",
							      G_TYPE_OBJECT,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

/**
 * rb_button_bar_new:
 * @model: a #GMenuModel
 * @target: property and signal binding target
 *
 * Creates a toolbar-like widget (not actually a #GtkToolbar) containing
 * a row of buttons representing the items in @model.  If an item in the
 * model has an rb-property-bind attribute set, the state of the button
 * is bound to the corresponding property of the source that the toolbar
 * is associated with.  This only works for toggle buttons, so the property
 * must be a boolean.
 *
 * Return value: the button bar
 */
GtkWidget *
rb_button_bar_new (GMenuModel *model, GObject *target)
{
	return GTK_WIDGET (g_object_new (RB_TYPE_BUTTON_BAR,
					 "model", model,
					 "target", target,
					 "column-homogeneous", FALSE,
					 "hexpand", FALSE,
					 "column-spacing", 3,
					 NULL));
}

/**
 * rb_button_bar_add_accelerators:
 * @bar: a #RBButtonBar
 * @group: the #GtkAccelGroup to add accelerators to
 *
 * Adds accelerators for the buttons in @bar to the accelerator
 * group @group.
 */
void
rb_button_bar_add_accelerators (RBButtonBar *bar, GtkAccelGroup *group)
{
	GList *c, *l;

	c = gtk_container_get_children (GTK_CONTAINER (bar));
	for (l = c; l != NULL; l = l->next) {
		GtkWidget *widget = l->data;
		const char *accel_text;
		guint accel_key;
		GdkModifierType accel_mods;

		accel_text = g_object_get_data (G_OBJECT (widget), "rb-accel");
		if (accel_text != NULL) {
			gtk_accelerator_parse (accel_text, &accel_key, &accel_mods);
			if (accel_key != 0) {
				gtk_widget_add_accelerator (widget, "activate", group, accel_key, accel_mods, 0);
			}
		}

		/* handle menus attached to menu buttons */
		if (GTK_IS_MENU_BUTTON (widget)) {
			RBApplication *app = RB_APPLICATION (g_application_get_default ());
			GMenuModel *model;
			model = g_object_get_data (G_OBJECT (widget), "rb-menu-model");
			if (model != NULL)
				rb_application_set_menu_accelerators (app, model, TRUE);
		}
	}
	g_list_free (c);
}

/**
 * rb_button_bar_remove_accelerators:
 * @bar: a #RBButtonBar
 * @group: the #GtkAccelGroup to remove accelerators from
 *
 * Reverses the effects of @rb_button_bar_add_accelerators.
 */
void
rb_button_bar_remove_accelerators (RBButtonBar *bar, GtkAccelGroup *group)
{
	GList *c, *l;

	c = gtk_container_get_children (GTK_CONTAINER (bar));
	for (l = c; l != NULL; l = l->next) {
		GtkWidget *widget = l->data;
		const char *accel_text;
		guint accel_key;
		GdkModifierType accel_mods;

		accel_text = g_object_get_data (G_OBJECT (widget), "rb-accel");
		if (accel_text != NULL) {
			gtk_accelerator_parse (accel_text, &accel_key, &accel_mods);
			if (accel_key != 0) {
				gtk_widget_remove_accelerator (widget, group, accel_key, accel_mods);
			}
		}

		/* handle menus attached to menu buttons */
		if (GTK_IS_MENU_BUTTON (widget)) {
			RBApplication *app = RB_APPLICATION (g_application_get_default ());
			GMenuModel *model;

			model = g_object_get_data (G_OBJECT (widget), "rb-menu-model");
			if (model != NULL)
				rb_application_set_menu_accelerators (app, model, FALSE);
		}
	}
	g_list_free (c);
}
