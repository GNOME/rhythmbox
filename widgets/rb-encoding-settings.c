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

#include <glib/gi18n.h>

#include <gst/gst.h>
#include <gst/pbutils/install-plugins.h>

#include <backends/rb-encoder.h>
#include <widgets/rb-encoding-settings.h>
#include <widgets/rb-object-property-editor.h>
#include <lib/rb-builder-helpers.h>
#include <lib/rb-util.h>
#include <lib/rb-debug.h>
#include <lib/rb-missing-plugins.h>

#define CUSTOM_SETTINGS_PREFIX "rhythmbox-custom-settings"
#define CBR_SETTINGS_PRESET CUSTOM_SETTINGS_PREFIX "-cbr"
#define CVBR_SETTINGS_PRESET CUSTOM_SETTINGS_PREFIX "-cvbr"
/* this preset name was used in releases where we only had VBR custom settings */
#define VBR_SETTINGS_PRESET CUSTOM_SETTINGS_PREFIX

static struct {
	const char *style;
	const char *label;
	const char *preset;
} encoding_styles[] = {
	{ "cbr", N_("Constant bit rate"), CBR_SETTINGS_PRESET },
	{ "vbr", N_("Variable bit rate"), VBR_SETTINGS_PRESET },
	{ "cvbr", N_("Constrained Variable bit rate"), CVBR_SETTINGS_PRESET },
};

static void rb_encoding_settings_class_init (RBEncodingSettingsClass *klass);
static void rb_encoding_settings_init (RBEncodingSettings *settings);

struct _RBEncodingSettingsPrivate
{
	GSettings *gsettings;
	GstEncodingTarget *target;
	GstElement *encoder_element;

	GtkTreeModel *profile_model;
	GtkTreeModel *preset_model;

	GtkWidget *preferred_format_menu;
	GtkWidget *preset_menu;
	GtkWidget *install_plugins_button;
	GtkWidget *encoder_property_holder;
	GtkWidget *encoder_property_editor;
	GtkWidget *lossless_check;

	gboolean show_lossless;

	gboolean profile_init;
	char *preset_name;

	gulong profile_changed_id;
};

G_DEFINE_TYPE (RBEncodingSettings, rb_encoding_settings, GTK_TYPE_BOX);

/**
 * SECTION:rbencodingsettings
 * @short_description: encapsulates widgets for editing encoding settings
 *
 * RBEncodingSettings provides an interface for selecting and configuring
 * an encoding profile from an encoding target.
 */

enum
{
	PROP_0,
	PROP_SETTINGS,
	PROP_TARGET,
	PROP_SHOW_LOSSLESS
};

static void
profile_changed_cb (RBObjectPropertyEditor *editor, RBEncodingSettings *settings)
{
	if (settings->priv->profile_init)
		return;

	if (settings->priv->encoder_element) {
		rb_debug ("updating preset %s", settings->priv->preset_name);
		gst_preset_save_preset (GST_PRESET (settings->priv->encoder_element),
						    settings->priv->preset_name);
	}
}

static void
update_property_editor_for_preset (RBEncodingSettings *settings, const char *media_type, const char *preset)
{
	int i;
	int style;

	/* figure out if this is a user-settings preset name */
	style = -1;
	for (i = 0; i < G_N_ELEMENTS (encoding_styles); i++) {
		if (g_strcmp0 (preset, encoding_styles[i].preset) == 0) {
			style = i;
			break;
		}
	}

	/* remove old property editor, if there is one */
	if (settings->priv->encoder_property_editor != NULL) {
		g_signal_handler_disconnect (settings->priv->encoder_property_editor,
					     settings->priv->profile_changed_id);

		gtk_container_remove (GTK_CONTAINER (settings->priv->encoder_property_holder),
				      settings->priv->encoder_property_editor);
		settings->priv->profile_changed_id = 0;
		settings->priv->encoder_property_editor = NULL;
		g_free (settings->priv->preset_name);
		settings->priv->preset_name = NULL;
	}

	/* create new property editor, if required */
	if (style != -1 && settings->priv->encoder_element) {
		GstEncodingProfile *profile;
		char **profile_settings;

		/* make sure the preset exists so encoder batches can use it */
		if (gst_preset_load_preset (GST_PRESET (settings->priv->encoder_element), preset) == FALSE) {
			if (rb_gst_encoder_set_encoding_style (settings->priv->encoder_element,
							       encoding_styles[style].style)) {
				gst_preset_save_preset (GST_PRESET (settings->priv->encoder_element),
							preset);
			}
		}

		profile = rb_gst_get_encoding_profile (media_type);
		profile_settings =
			rb_gst_encoding_profile_get_settings (profile,
							      encoding_styles[style].style);
		if (profile_settings != NULL) {
			settings->priv->encoder_property_editor =
				rb_object_property_editor_new (G_OBJECT (settings->priv->encoder_element),
							       profile_settings);
			g_strfreev (profile_settings);
			gst_encoding_profile_unref (profile);

			settings->priv->profile_changed_id =
				g_signal_connect (settings->priv->encoder_property_editor,
						  "changed",
						  G_CALLBACK (profile_changed_cb),
						  settings);

			gtk_grid_attach (GTK_GRID (settings->priv->encoder_property_holder),
					 settings->priv->encoder_property_editor,
					 0, 0, 1, 1);
			gtk_widget_show_all (settings->priv->encoder_property_editor);

			settings->priv->preset_name = g_strdup (preset);
		}
	}
}

static void
format_changed_cb (GtkWidget *widget, RBEncodingSettings *settings)
{
	GtkTreeIter iter;
	char *media_type = NULL;
	GstEncodingProfile *profile;
	RBEncoder *encoder;

	if (settings->priv->profile_init)
		return;

	/* get selected media type */
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter) == FALSE)
		return;
	gtk_tree_model_get (GTK_TREE_MODEL (settings->priv->profile_model),
			    &iter,
			    0, &media_type,
			    2, &profile,
			    -1);

	g_settings_set_string (settings->priv->gsettings, "media-type", media_type);

	/* indicate whether additional plugins are required to encode in this format */
	encoder = rb_encoder_new ();
	if (rb_encoder_get_missing_plugins (encoder, profile, NULL, NULL)) {
		rb_debug ("additional plugins are required to encode %s", media_type);
		gtk_widget_set_visible (settings->priv->install_plugins_button, TRUE);
		/* not a great way to handle this situation; probably should describe
		 * the plugins that are missing when automatic install isn't available.
		 */
		gtk_widget_set_sensitive (settings->priv->install_plugins_button,
					  gst_install_plugins_supported ());
	} else {
		rb_debug ("can encode %s", media_type);
		gtk_widget_set_visible (settings->priv->install_plugins_button, FALSE);
	}

	gtk_widget_set_sensitive (settings->priv->lossless_check, rb_gst_media_type_is_lossless (media_type) == FALSE);

	g_free (media_type);
}

static void
preset_changed_cb (GtkWidget *widget, RBEncodingSettings *settings)
{
	GtkTreeIter iter;
	char *media_type = NULL;
	char *preset = NULL;
	char *stored;
	gboolean have_preset;
	GVariant *presets;

	if (settings->priv->profile_init)
		return;

	/* get selected media type */
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (settings->priv->preferred_format_menu), &iter) == FALSE) {
		rb_debug ("no media type selected?");
		return;
	}
	gtk_tree_model_get (GTK_TREE_MODEL (settings->priv->profile_model),
			    &iter,
			    0, &media_type,
			    -1);

	/* get selected preset */
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (settings->priv->preset_menu), &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (settings->priv->preset_model),
				    &iter,
				    1, &preset,
				    -1);
		rb_debug ("preset %s now selected for media type %s", preset, media_type);
	} else {
		rb_debug ("no preset selected for media type %s?", media_type);
	}

	update_property_editor_for_preset (settings, media_type, preset);

	/* store selected preset */
	presets = g_settings_get_value (settings->priv->gsettings, "media-type-presets");
	stored = NULL;
	have_preset = (preset != NULL && preset[0] != '\0');
	g_variant_lookup (presets, media_type, "s", &stored);
	if (have_preset == FALSE && (stored == NULL || stored[0] == '\0')) {
		/* don't bother */
	} else if (g_strcmp0 (stored, preset) != 0) {
		GVariantBuilder b;
		GVariantIter i;
		char *mt;
		char *p;
		gboolean stored;

		g_variant_builder_init (&b, G_VARIANT_TYPE ("a{ss}"));
		g_variant_iter_init (&i, presets);
		stored = FALSE;
		while (g_variant_iter_loop (&i, "{ss}", &mt, &p)) {
			if (g_strcmp0 (mt, media_type) == 0) {
				if (have_preset) {
					g_variant_builder_add (&b, "{ss}", mt, preset);
				}
				stored = TRUE;
			} else {
				g_variant_builder_add (&b, "{ss}", mt, p);
				rb_debug ("keeping %s => %s", mt, p);
			}
		}

		if (have_preset && stored == FALSE) {
			g_variant_builder_add (&b, "{ss}", media_type, preset);
		}

		g_settings_set_value (settings->priv->gsettings, "media-type-presets", g_variant_builder_end (&b));
	}
	g_variant_unref (presets);

	g_free (stored);
	g_free (preset);
	g_free (media_type);
}

static void
plugin_install_done_cb (gpointer inst, gboolean retry, RBEncodingSettings *settings)
{
	format_changed_cb (settings->priv->preferred_format_menu, settings);
}

static void
install_plugins_cb (GtkWidget *widget, RBEncodingSettings *settings)
{
	char *media_type;
	GstEncodingProfile *profile;
	RBEncoder *encoder;
	char **details;
	GClosure *closure;

	/* get profile */
	media_type = g_settings_get_string (settings->priv->gsettings, "media-type");
	profile = rb_gst_get_encoding_profile (media_type);
	if (profile == NULL) {
		g_warning ("no encoding profile available for %s, so how can we install plugins?",
			   media_type);
		g_free (media_type);
		return;
	}
	g_free (media_type);

	/* get plugin details */
	encoder = rb_encoder_new ();
	if (rb_encoder_get_missing_plugins (encoder, profile, &details, NULL) == FALSE) {
		/* what? */
		g_object_unref (encoder);
		return;
	}

	/* attempt installation */
	closure = g_cclosure_new ((GCallback) plugin_install_done_cb,
				  g_object_ref (settings),
				  (GClosureNotify) g_object_unref);
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__BOOLEAN);

	rb_missing_plugins_install ((const char **)details, TRUE, closure);

	g_closure_sink (closure);
	g_strfreev (details);
}

static void
insert_preset (RBEncodingSettings *settings, const char *display_name, const char *name, gboolean select)
{
	GtkTreeIter iter;

	gtk_list_store_insert_with_values (GTK_LIST_STORE (settings->priv->preset_model),
					   &iter,
					   -1,
					   0, display_name,
					   1, name,
					   -1);
	if (select) {
		rb_debug ("preset %s is selected", display_name);
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (settings->priv->preset_menu), &iter);
	}
}


static void
update_presets (RBEncodingSettings *settings, const char *media_type)
{
	GVariant *preset_settings;
	char *active_preset;
	GstEncodingProfile *profile;
	char **profile_settings;
	char **profile_presets;
	int i;

	settings->priv->profile_init = TRUE;

	gtk_list_store_clear (GTK_LIST_STORE (settings->priv->preset_model));

	if (settings->priv->encoder_element != NULL) {
		gst_object_unref (settings->priv->encoder_element);
		settings->priv->encoder_element = NULL;
	}

	gtk_widget_set_sensitive (settings->priv->preset_menu, FALSE);
	if (media_type == NULL) {
		settings->priv->profile_init = FALSE;
		return;
	}

	/* get preset for the media type from settings */
	preset_settings = g_settings_get_value (settings->priv->gsettings, "media-type-presets");
	active_preset = NULL;
	g_variant_lookup (preset_settings, media_type, "s", &active_preset);

	rb_debug ("active preset for media type %s is %s", media_type, active_preset);

	insert_preset (settings,
		       _("Default settings"),
		       "",
		       (active_preset == NULL || active_preset[0] == '\0'));

	profile = rb_gst_get_encoding_profile (media_type);
	if (profile == NULL) {
		g_warning ("Don't know how to encode to media type %s", media_type);
		settings->priv->profile_init = FALSE;
		return;
	}
	settings->priv->encoder_element = rb_gst_encoding_profile_get_encoder (profile);

	for (i = 0; i < G_N_ELEMENTS (encoding_styles); i++) {
		profile_settings = rb_gst_encoding_profile_get_settings (profile, encoding_styles[i].style);
		if (profile_settings == NULL)
			continue;

		rb_debug ("profile has custom settings for style %s", encoding_styles[i].style);
		insert_preset (settings,
			       gettext (encoding_styles[i].label),
			       encoding_styles[i].preset,
			       g_strcmp0 (active_preset, encoding_styles[i].preset) == 0);
		gtk_widget_set_sensitive (settings->priv->preset_menu, TRUE);
	}

	/* get list of actual presets for the media type */
	profile_presets = rb_gst_encoding_profile_get_presets (profile);
	if (profile_presets) {
		int i;
		for (i = 0; profile_presets[i] != NULL; i++) {
			if (g_str_has_prefix (profile_presets[i], CUSTOM_SETTINGS_PREFIX))
				continue;

			rb_debug ("profile has preset %s", profile_presets[i]);
			insert_preset (settings,
				       profile_presets[i],
				       profile_presets[i],
				       g_strcmp0 (profile_presets[i], active_preset) == 0);
			gtk_widget_set_sensitive (settings->priv->preset_menu, TRUE);
		}
		g_strfreev (profile_presets);
	}

	update_property_editor_for_preset (settings, media_type, active_preset);

	gst_encoding_profile_unref (profile);
	settings->priv->profile_init = FALSE;
}

static void
update_preferred_media_type (RBEncodingSettings *settings)
{
	GtkTreeIter iter;
	gboolean done;
	char *str;

	done = FALSE;
	str = g_settings_get_string (settings->priv->gsettings, "media-type");
	if (gtk_tree_model_get_iter_first (settings->priv->profile_model, &iter)) {
		do {
			char *media_type;

			gtk_tree_model_get (settings->priv->profile_model, &iter,
					    0, &media_type,
					    -1);
			if (g_strcmp0 (media_type, str) == 0) {
				gtk_combo_box_set_active_iter (GTK_COMBO_BOX (settings->priv->preferred_format_menu), &iter);
				update_presets (settings, media_type);
				done = TRUE;
			}
			g_free (media_type);
		} while (done == FALSE && gtk_tree_model_iter_next (settings->priv->profile_model, &iter));
	}

	if (done == FALSE) {
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (settings->priv->preferred_format_menu), NULL);
		update_presets (settings, NULL);
	}

	g_free (str);
}

static void
encoding_settings_changed_cb (GSettings *gsettings, const char *key, RBEncodingSettings *settings)
{
	if (g_strcmp0 (key, "media-type") == 0) {
		rb_debug ("preferred media type changed");
		update_preferred_media_type (settings);
	} else if (g_strcmp0 (key, "media-type-presets") == 0) {
		rb_debug ("media type presets changed");
		/* need to do anything here?  update selection if the
		 * preset for the preferred media type changed?
		 */
	}
}

static void
impl_constructed (GObject *object)
{
	RBEncodingSettings *settings;
	GtkBuilder *builder;
	GtkWidget *grid;
	GtkCellRenderer *renderer;
	const GList *p;

	RB_CHAIN_GOBJECT_METHOD (rb_encoding_settings_parent_class, constructed, object);

	settings = RB_ENCODING_SETTINGS (object);

	g_signal_connect_object (settings->priv->gsettings,
				 "changed",
				 G_CALLBACK (encoding_settings_changed_cb),
				 settings, 0);

	builder = rb_builder_load ("encoding-settings.ui", NULL);
	grid = GTK_WIDGET (gtk_builder_get_object (builder, "encoding-settings-grid"));
	gtk_container_add (GTK_CONTAINER (settings), grid);

	settings->priv->profile_model = GTK_TREE_MODEL (gtk_tree_store_new (3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER));
	for (p = gst_encoding_target_get_profiles (settings->priv->target); p != NULL; p = p->next) {
		GstEncodingProfile *profile = GST_ENCODING_PROFILE (p->data);
		char *media_type;

		media_type = rb_gst_encoding_profile_get_media_type (profile);
		if (media_type == NULL) {
			continue;
		}
		gtk_tree_store_insert_with_values (GTK_TREE_STORE (settings->priv->profile_model),
						   NULL,
						   NULL,
						   -1,
						   0, media_type,
						   1, gst_encoding_profile_get_description (profile),
						   2, profile,
						   -1);
		g_free (media_type);
	}

	settings->priv->preset_model = GTK_TREE_MODEL (gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING));

	settings->priv->preferred_format_menu = GTK_WIDGET (gtk_builder_get_object (builder, "format_select_combo"));
	gtk_combo_box_set_model (GTK_COMBO_BOX (settings->priv->preferred_format_menu), settings->priv->profile_model);
	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (settings->priv->preferred_format_menu), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (settings->priv->preferred_format_menu), renderer, "text", 1, NULL);

	g_signal_connect (G_OBJECT (settings->priv->preferred_format_menu),
			  "changed",
			  G_CALLBACK (format_changed_cb),
			  settings);

	settings->priv->preset_menu = GTK_WIDGET (gtk_builder_get_object (builder, "preset_select_combo"));
	gtk_combo_box_set_model (GTK_COMBO_BOX (settings->priv->preset_menu), settings->priv->preset_model);
	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (settings->priv->preset_menu), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (settings->priv->preset_menu), renderer, "text", 0, NULL);

	g_signal_connect (G_OBJECT (settings->priv->preset_menu),
			  "changed",
			  G_CALLBACK (preset_changed_cb),
			  settings);

	settings->priv->install_plugins_button = GTK_WIDGET (gtk_builder_get_object (builder, "install_plugins_button"));
	gtk_widget_set_no_show_all (settings->priv->install_plugins_button, TRUE);
	g_signal_connect (G_OBJECT (settings->priv->install_plugins_button),
			  "clicked",
			  G_CALLBACK (install_plugins_cb),
			  settings);

	settings->priv->encoder_property_holder = GTK_WIDGET (gtk_builder_get_object (builder, "encoder_property_holder"));

	settings->priv->lossless_check = GTK_WIDGET (gtk_builder_get_object (builder, "transcode_lossless_check"));
	gtk_widget_set_no_show_all (settings->priv->lossless_check, TRUE);
	if (settings->priv->show_lossless) {
		gtk_widget_show (settings->priv->lossless_check);
		g_settings_bind (settings->priv->gsettings,
				 "transcode-lossless",
				 settings->priv->lossless_check,
				 "active",
				 G_SETTINGS_BIND_DEFAULT);
	} else {
		gtk_widget_hide (settings->priv->lossless_check);
	}

	update_preferred_media_type (settings);

	g_object_unref (builder);
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBEncodingSettings *settings = RB_ENCODING_SETTINGS (object);
	switch (prop_id) {
	case PROP_SETTINGS:
		settings->priv->gsettings = g_value_dup_object (value);
		break;
	case PROP_TARGET:
		settings->priv->target = GST_ENCODING_TARGET (g_value_dup_object (value));
		break;
	case PROP_SHOW_LOSSLESS:
		settings->priv->show_lossless = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBEncodingSettings *settings = RB_ENCODING_SETTINGS (object);
	switch (prop_id) {
	case PROP_SETTINGS:
		g_value_set_object (value, settings->priv->gsettings);
		break;
	case PROP_TARGET:
		g_value_set_object (value, GST_MINI_OBJECT (settings->priv->target));
		break;
	case PROP_SHOW_LOSSLESS:
		g_value_set_boolean (value, settings->priv->show_lossless);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void
rb_encoding_settings_init (RBEncodingSettings *settings)
{
	settings->priv = G_TYPE_INSTANCE_GET_PRIVATE (settings,
						      RB_TYPE_ENCODING_SETTINGS,
						      RBEncodingSettingsPrivate);
}

static void
impl_dispose (GObject *object)
{
	RBEncodingSettings *settings = RB_ENCODING_SETTINGS (object);

	if (settings->priv->gsettings != NULL) {
		g_object_unref (settings->priv->gsettings);
		settings->priv->gsettings = NULL;
	}

	if (settings->priv->target != NULL) {
		g_object_unref (settings->priv->target);
		settings->priv->target = NULL;
	}

	/* models and widgets and crap? */

	G_OBJECT_CLASS (rb_encoding_settings_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	RBEncodingSettings *settings = RB_ENCODING_SETTINGS (object);

	g_free (settings->priv->preset_name);

	G_OBJECT_CLASS (rb_encoding_settings_parent_class)->finalize (object);
}


static void
rb_encoding_settings_class_init (RBEncodingSettingsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = impl_constructed;
	object_class->dispose = impl_dispose;
	object_class->finalize = impl_finalize;
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	/**
	 * RBEncodingSettings:settings
	 *
	 * GSettings instance holding the settings to edit.
	 */
	g_object_class_install_property (object_class,
					 PROP_SETTINGS,
					 g_param_spec_object ("settings",
							      "settings",
							      "GSettings instance to edit",
							      G_TYPE_OBJECT,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBEncodingSettings:encoding-target
	 *
	 * The encoding target to select a profile from.
	 */
	g_object_class_install_property (object_class,
					 PROP_TARGET,
					 g_param_spec_object ("encoding-target",
							      "encoding target",
							      "GstEncodingTarget",
							      GST_TYPE_ENCODING_TARGET,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBEncodingSettings:show-lossless
	 *
	 * If %TRUE, show widgets for controlling lossless encodings.
	 */
	g_object_class_install_property (object_class,
					 PROP_SHOW_LOSSLESS,
					 g_param_spec_boolean ("show-lossless",
							       "show-lossless",
							       "whether to show options relating to lossless encodings",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBEncodingSettingsPrivate));
}

/**
 * rb_encoding_settings_new:
 * @settings: the #GSettings instance to edit
 * @target: the #GstEncodingTarget to select a profile from
 * @show_lossless: show widgets for controlling lossless encodings
 *
 * Creates widgets for selecting and configuring an encoding
 * profile from @target.
 *
 * Return value: encoding settings widget.
 */
GtkWidget *
rb_encoding_settings_new (GSettings *settings, GstEncodingTarget *target, gboolean show_lossless)
{
	return GTK_WIDGET (g_object_new (RB_TYPE_ENCODING_SETTINGS,
					 "settings", settings,
					 "encoding-target", target,
					 "show-lossless", show_lossless,
					 NULL));
}
