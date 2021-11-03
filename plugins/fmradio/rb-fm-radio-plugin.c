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

#include <glib/gi18n-lib.h>

#include "rb-debug.h"

#include "rb-plugin-macros.h"
#include "rb-fm-radio-source.h"
#include "rb-radio-tuner.h"
#include "rb-display-page-group.h"
#include "rb-file-helpers.h"

#define RB_TYPE_FM_RADIO_PLUGIN         (rb_fm_radio_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBFMRadioPlugin, rb_fm_radio_plugin, RB, FM_RADIO_PLUGIN, PeasExtensionBase)

struct _RBFMRadioPlugin {
	PeasExtensionBase parent;
	RBSource *source;
};

struct _RBFMRadioPluginClass {
	PeasExtensionBaseClass parent_class;
};

G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);
GType rb_fm_radio_plugin_get_type (void) G_GNUC_CONST;

RB_DEFINE_PLUGIN (RB_TYPE_FM_RADIO_PLUGIN, RBFMRadioPlugin, rb_fm_radio_plugin,)

static void
rb_fm_radio_plugin_init (RBFMRadioPlugin *plugin)
{
	rb_debug ("RBFMRadioPlugin initialising");
}

static void
impl_activate (PeasActivatable *plugin)
{
	RBFMRadioPlugin *pi = RB_FM_RADIO_PLUGIN (plugin);
	RBRadioTuner *tuner;
	RBShell *shell;

	tuner = rb_radio_tuner_new (NULL, NULL);
	if (tuner == NULL)
		return;

	rb_radio_tuner_set_mute (tuner, TRUE);
	rb_radio_tuner_update (tuner);

	g_object_get (plugin, "object", &shell, NULL);
	pi->source = rb_fm_radio_source_new (G_OBJECT (plugin), shell, tuner);
	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (pi->source), RB_DISPLAY_PAGE_GROUP_LIBRARY);	/* devices? */

	g_object_unref (tuner);

	g_object_unref (shell);
}

static void
impl_deactivate (PeasActivatable *plugin)
{
	RBFMRadioPlugin *pi = RB_FM_RADIO_PLUGIN (plugin);

	if (pi->source) {
		rb_display_page_delete_thyself (RB_DISPLAY_PAGE (pi->source));
		pi->source = NULL;
	}
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_fm_radio_plugin_register_type (G_TYPE_MODULE (module));
	_rb_fm_radio_source_register_type (G_TYPE_MODULE (module));
	_rb_radio_tuner_register_type (G_TYPE_MODULE (module));

	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_FM_RADIO_PLUGIN);
}
