/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2007  James Henstridge <james@jamesh.id.au>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grants permission for non-GPL compatible
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

#include <glib/gi18n-lib.h>

#include "rb-debug.h"
#include "rb-plugin.h"

#include "rb-fm-radio-source.h"
#include "rb-radio-tuner.h"

#define RB_TYPE_FM_RADIO_PLUGIN         (rb_fm_radio_plugin_get_type ())
#define RB_FM_RADIO_PLUGIN(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_FM_RADIO_PLUGIN, RBFMRadioPlugin))
#define RB_FM_RADIO_PLUGIN_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_FM_RADIO_PLUGIN, RBFMRadioPluginClass))
#define RB_IS_FM_RADIO_PLUGIN(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_FM_RADIO_PLUGIN))
#define RB_IS_FM_RADIO_PLUGIN_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_FM_RADIO_PLUGIN))
#define RB_FM_RADIO_PLUGIN_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_FM_RADIO_PLUGIN, RBFMRadioPluginClass))

typedef struct _RBFMRadioPlugin RBFMRadioPlugin;
typedef struct _RBFMRadioPluginClass RBFMRadioPluginClass;

struct _RBFMRadioPlugin {
	RBPlugin parent;
	RBSource *source;
	guint ui_merge_id;
};

struct _RBFMRadioPluginClass {
	RBPluginClass parent_class;
};

G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType rb_fm_radio_plugin_get_type (void) G_GNUC_CONST;

static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);

RB_PLUGIN_REGISTER (RBFMRadioPlugin, rb_fm_radio_plugin);

static void
rb_fm_radio_plugin_init (RBFMRadioPlugin *plugin)
{
	rb_debug ("RBFMRadioPlugin initialising");
}

static void
rb_fm_radio_plugin_finalize (GObject *object)
{
	rb_debug ("RBIRadioPlugin finalising");

	G_OBJECT_CLASS (rb_fm_radio_plugin_parent_class)->finalize (object);
}

static void
impl_activate (RBPlugin *plugin, RBShell *shell)
{
	RBFMRadioPlugin *pi = RB_FM_RADIO_PLUGIN (plugin);
	RBRadioTuner *tuner;
	GtkUIManager *uimanager;
	char *filename;

	tuner = rb_radio_tuner_new (NULL, NULL);
	if (tuner == NULL)
		return;

	rb_radio_tuner_set_mute (tuner, TRUE);
	rb_radio_tuner_update (tuner);
	pi->source = rb_fm_radio_source_new (shell, tuner);
	rb_shell_append_source (shell, pi->source, NULL);
	g_object_unref (tuner);

	filename = rb_plugin_find_file (plugin, "fmradio-ui.xml");
	if (filename != NULL) {
		g_object_get (shell, "ui-manager", &uimanager, NULL);
		pi->ui_merge_id = gtk_ui_manager_add_ui_from_file (uimanager,
								   filename,
								   NULL);
		g_object_unref (uimanager);
		g_free(filename);
	} else {
		g_warning ("Unable to find file: fmradio-ui.xml");
	}
	
}

static void
impl_deactivate (RBPlugin *plugin, RBShell *shell)
{
	RBFMRadioPlugin *pi = RB_FM_RADIO_PLUGIN (plugin);
	GtkUIManager *uimanager;

	if (pi->source) {
		rb_source_delete_thyself (pi->source);
		pi->source = NULL;
	}

	if (pi->ui_merge_id) {
		g_object_get (shell, "ui-manager", &uimanager, NULL);
		gtk_ui_manager_remove_ui (uimanager, pi->ui_merge_id);
		g_object_unref (uimanager);
		pi->ui_merge_id = 0;
	}
}

static void
rb_fm_radio_plugin_class_init (RBFMRadioPluginClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

        object_class->finalize = rb_fm_radio_plugin_finalize;

        plugin_class->activate = impl_activate;
        plugin_class->deactivate = impl_deactivate;

	RB_PLUGIN_REGISTER_TYPE (rb_radio_tuner);
	RB_PLUGIN_REGISTER_TYPE (rb_fm_radio_source);
}
