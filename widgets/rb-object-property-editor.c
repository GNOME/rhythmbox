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

#include <widgets/rb-object-property-editor.h>
#include <lib/rb-util.h>
#include <lib/rb-debug.h>

static void rb_object_property_editor_class_init (RBObjectPropertyEditorClass *klass);
static void rb_object_property_editor_init (RBObjectPropertyEditor *editor);

struct _RBObjectPropertyEditorPrivate
{
	GObject *object;
	char **properties;

	gboolean changed;
	gulong notify_id;
};

G_DEFINE_TYPE (RBObjectPropertyEditor, rb_object_property_editor, GTK_TYPE_GRID);

/**
 * SECTION:rbobjectpropertyeditor
 * @short_description: builds widgetry for editing simple GObject properties
 *
 * RBObjectPropertyEditor can be used to provide an interface to edit
 * simple (boolean, integer, enum, float) properties of a GObject.
 */

enum
{
	PROP_0,
	PROP_OBJECT,
	PROP_PROPERTIES
};

enum
{
	CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
impl_finalize (GObject *object)
{
	RBObjectPropertyEditor *editor = RB_OBJECT_PROPERTY_EDITOR (object);

	g_strfreev (editor->priv->properties);

	G_OBJECT_CLASS (rb_object_property_editor_parent_class)->finalize (object);
}

static void
impl_dispose (GObject *object)
{
	RBObjectPropertyEditor *editor = RB_OBJECT_PROPERTY_EDITOR (object);

	if (editor->priv->object != NULL) {
		if (editor->priv->notify_id) {
			g_signal_handler_disconnect (editor->priv->object,
						     editor->priv->notify_id);
			editor->priv->notify_id = 0;
		}
		g_object_unref (editor->priv->object);
		editor->priv->object = NULL;
	}

	G_OBJECT_CLASS (rb_object_property_editor_parent_class)->dispose (object);
}

static void
notify_cb (GObject *object, GParamSpec *pspec, RBObjectPropertyEditor *editor)
{
	editor->priv->changed = TRUE;
}

static void
focus_out_cb (GtkWidget *widget, GdkEvent *event, RBObjectPropertyEditor *editor)
{
	if (editor->priv->changed) {
		rb_debug ("emitting changed");
		g_signal_emit (editor, signals[CHANGED], 0);
		editor->priv->changed = FALSE;
	} else {
		rb_debug ("not emitting changed");
	}
}

static GtkWidget *
create_boolean_editor (RBObjectPropertyEditor *editor, const char *property, GParamSpec *pspec)
{
	GtkWidget *control;

	control = gtk_check_button_new ();

	g_object_bind_property (editor->priv->object, property,
				control, "active",
				G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

	return control;
}

static GtkWidget *
create_enum_editor (RBObjectPropertyEditor *editor, const char *property, GParamSpec *pspec)
{
	GParamSpecEnum *penum;
	GtkListStore *model;
	GtkCellRenderer *renderer;
	GtkWidget *control;
	int p;

	control = gtk_combo_box_new ();
	penum = G_PARAM_SPEC_ENUM (pspec);
	renderer = gtk_cell_renderer_text_new ();

	model = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
	gtk_combo_box_set_model (GTK_COMBO_BOX (control), GTK_TREE_MODEL (model));
	for (p = 0; p < penum->enum_class->n_values; p++) {
		gtk_list_store_insert_with_values (model, NULL, p,
						   0, penum->enum_class->values[p].value_name,
						   1, p,
						   -1);
	}
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (control), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (control), renderer, "text", 0, NULL);

	g_object_bind_property (editor->priv->object, property,
				control, "active",
				G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

	return control;
}

static GtkWidget *
create_int_editor (RBObjectPropertyEditor *editor, const char *property, GParamSpec *pspec, gboolean inverted)
{
	GParamSpecInt *pint;
	GtkWidget *control;
	GtkAdjustment *adjustment;

	pint = G_PARAM_SPEC_INT (pspec);

	adjustment = gtk_adjustment_new (pint->default_value,
					 pint->minimum,
					 pint->maximum + 1,
					 1.0,
					 1.0, 1.0);

	control = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, adjustment);
	gtk_scale_set_digits (GTK_SCALE (control), 0);
	gtk_range_set_inverted (GTK_RANGE (control), inverted);

	g_object_bind_property (editor->priv->object, property,
				adjustment, "value",
				G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

	return control;
}

static GtkWidget *
create_float_editor (RBObjectPropertyEditor *editor, const char *property, GParamSpec *pspec, gboolean inverted)
{
	GParamSpecFloat *pfloat;
	GtkWidget *control;
	GtkAdjustment *adjustment;

	pfloat = G_PARAM_SPEC_FLOAT (pspec);

	adjustment = gtk_adjustment_new (pfloat->default_value,
					 pfloat->minimum,
					 pfloat->maximum + pfloat->epsilon*2,
					 pfloat->epsilon*10,
					 0.1, 0.1);

	control = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, adjustment);
	gtk_range_set_inverted (GTK_RANGE (control), inverted);

	g_object_bind_property (editor->priv->object, property,
				adjustment, "value",
				G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

	return control;
}

static GtkWidget *
create_double_editor (RBObjectPropertyEditor *editor, const char *property, GParamSpec *pspec, gboolean inverted)
{
	GParamSpecDouble *pdouble;
	GtkWidget *control;
	GtkAdjustment *adjustment;

	pdouble = G_PARAM_SPEC_DOUBLE (pspec);

	adjustment = gtk_adjustment_new (pdouble->default_value,
					 pdouble->minimum,
					 pdouble->maximum + pdouble->epsilon*2,
					 pdouble->epsilon*10,
					 0.1, 0.1);

	control = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, adjustment);
	gtk_range_set_inverted (GTK_RANGE (control), inverted);

	g_object_bind_property (editor->priv->object, property,
				adjustment, "value",
				G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

	return control;
}

static void
impl_constructed (GObject *object)
{
	RBObjectPropertyEditor *editor;
	GObjectClass *klass;
	int i;
	int row;

	RB_CHAIN_GOBJECT_METHOD (rb_object_property_editor_parent_class, constructed, object);

	editor = RB_OBJECT_PROPERTY_EDITOR (object);
	klass = G_OBJECT_GET_CLASS (editor->priv->object);

	editor->priv->notify_id = g_signal_connect (editor->priv->object, "notify", G_CALLBACK (notify_cb), editor);

	row = 0;
	for (i = 0; editor->priv->properties[i] != NULL; i++) {
		GParamSpec *pspec;
		GtkWidget *label;
		GtkWidget *control;
		GType prop_type;
		gboolean inverted = FALSE;
		char **bits;

		bits = g_strsplit (editor->priv->properties[i], ":", 2);
		if (g_strcmp0 (bits[1], "inverted") == 0) {
			inverted = TRUE;
		} else if (bits[1] != NULL) {
			g_warning ("unknown property modifier '%s' specified", bits[1]);
			g_strfreev (bits);
			continue;
		}

		pspec = g_object_class_find_property (klass, bits[0]);
		if (pspec == NULL) {
			g_warning ("couldn't find property %s on object %s",
				   bits[0], G_OBJECT_CLASS_NAME (klass));
			g_strfreev (bits);
			continue;
		}

		prop_type = G_PARAM_SPEC_TYPE (pspec);
		if (prop_type == G_TYPE_PARAM_BOOLEAN) {
			control = create_boolean_editor (editor, bits[0], pspec);
		} else if (prop_type == G_TYPE_PARAM_ENUM) {
			control = create_enum_editor (editor, bits[0], pspec);
		} else if (prop_type == G_TYPE_PARAM_INT) {
			control = create_int_editor (editor, bits[0], pspec, inverted);
		} else if (prop_type == G_TYPE_PARAM_FLOAT) {
			control = create_float_editor (editor, bits[0], pspec, inverted);
		} else if (prop_type == G_TYPE_PARAM_DOUBLE) {
			control = create_double_editor (editor, bits[0], pspec, inverted);
		} else {
			/* can't do this */
			g_warning ("don't know how to edit %s", g_type_name (prop_type));
			g_strfreev (bits);
			continue;
		}
		g_signal_connect (control, "focus-out-event", G_CALLBACK (focus_out_cb), editor);
		gtk_widget_set_hexpand (control, TRUE);

		label = gtk_label_new (g_param_spec_get_nick (pspec));
		gtk_widget_set_tooltip_text (label, g_param_spec_get_blurb (pspec));

		gtk_grid_attach (GTK_GRID (editor),
				 label,
				 0, row, 1, 1);
		gtk_grid_attach (GTK_GRID (editor),
				 control,
				 1, row, 1, 1);

		row++;
		g_strfreev (bits);
	}
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBObjectPropertyEditor *editor = RB_OBJECT_PROPERTY_EDITOR (object);
	switch (prop_id) {
	case PROP_OBJECT:
		editor->priv->object = g_value_dup_object (value);
		break;
	case PROP_PROPERTIES:
		editor->priv->properties = g_value_dup_boxed (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBObjectPropertyEditor *editor = RB_OBJECT_PROPERTY_EDITOR (object);
	switch (prop_id) {
	case PROP_OBJECT:
		g_value_set_object (value, editor->priv->object);
		break;
	case PROP_PROPERTIES:
		g_value_set_boxed (value, editor->priv->properties);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void
rb_object_property_editor_init (RBObjectPropertyEditor *editor)
{
	editor->priv = G_TYPE_INSTANCE_GET_PRIVATE (editor,
						    RB_TYPE_OBJECT_PROPERTY_EDITOR,
						    RBObjectPropertyEditorPrivate);
}


static void
rb_object_property_editor_class_init (RBObjectPropertyEditorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = impl_constructed;
	object_class->dispose = impl_dispose;
	object_class->finalize = impl_finalize;
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	/**
	 * RBObjectPropertyEditor::changed:
	 * @editor: the #RBObjectPropertyEditor
	 *
	 * Emitted when a property has been changed.
	 * This won't be emitted on every single change (use 'notify' on the
	 * object being edited for that), but rather when the edit widget
	 * for a property loses focus and the value was changed.
	 */
	signals[CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBObjectPropertyEditorClass, changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);

	/**
	 * RBObjectPropertyEditor:object
	 *
	 * The object to edit.
	 */
	g_object_class_install_property (object_class,
					 PROP_OBJECT,
					 g_param_spec_object ("object",
							      "object",
							      "object to edit",
							      G_TYPE_OBJECT,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBObjectPropertyEditor:properties
	 * 
	 * Array of names of properties to edit.
	 */
	g_object_class_install_property (object_class,
					 PROP_PROPERTIES,
					 g_param_spec_boxed ("properties",
							     "properties",
							     "properties to edit",
							     G_TYPE_STRV,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBObjectPropertyEditorPrivate));
}

/**
 * rb_object_property_editor_new:
 * @object: the object to edit
 * @properties: array of names of properties to edit
 *
 * Creates a property editor for the specified properties of an object.
 *
 * Return value: property editor widget.
 */
GtkWidget *
rb_object_property_editor_new (GObject *object, char **properties)
{
	return GTK_WIDGET (g_object_new (RB_TYPE_OBJECT_PROPERTY_EDITOR,
					 "object", object,
					 "properties", properties,
					 "column-spacing", 6,
					 NULL));
}
