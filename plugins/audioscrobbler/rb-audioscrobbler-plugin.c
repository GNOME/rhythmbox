/*
 * rb-audioscrobbler-plugin.c
 *
 * Copyright (C) 2006 James Livingston <doclivingston@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h> /* For strlen */
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include "rb-audioscrobbler-service.h"
#include "rb-audioscrobbler-profile-source.h"
#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-dialog.h"


#define RB_TYPE_AUDIOSCROBBLER_PLUGIN		(rb_audioscrobbler_plugin_get_type ())
#define RB_AUDIOSCROBBLER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPlugin))
#define RB_AUDIOSCROBBLER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPluginClass))
#define RB_IS_AUDIOSCROBBLER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN))
#define RB_IS_AUDIOSCROBBLER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOSCROBBLER_PLUGIN))
#define RB_AUDIOSCROBBLER_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPluginClass))

typedef struct
{
	RBPlugin parent;

	RBSource *lastfm_source;
	RBSource *librefm_source;
} RBAudioscrobblerPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBAudioscrobblerPluginClass;

G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_audioscrobbler_plugin_get_type		(void) G_GNUC_CONST;



static void rb_audioscrobbler_plugin_init (RBAudioscrobblerPlugin *plugin);
static void rb_audioscrobbler_plugin_finalize (GObject *object);
static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);

RB_PLUGIN_REGISTER(RBAudioscrobblerPlugin, rb_audioscrobbler_plugin)

static void
rb_audioscrobbler_plugin_class_init (RBAudioscrobblerPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	object_class->finalize = rb_audioscrobbler_plugin_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
}

static void
rb_audioscrobbler_plugin_init (RBAudioscrobblerPlugin *plugin)
{
	rb_debug ("RBAudioscrobblerPlugin initialising");
}

static void
rb_audioscrobbler_plugin_finalize (GObject *object)
{
	rb_debug ("RBAudioscrobblerPlugin finalising");

	G_OBJECT_CLASS (rb_audioscrobbler_plugin_parent_class)->finalize (object);
}



static void
impl_activate (RBPlugin *bplugin,
	       RBShell *shell)
{
	RBAudioscrobblerPlugin *plugin;
	RBAudioscrobblerService *lastfm;
	RBAudioscrobblerService *librefm;

	plugin = RB_AUDIOSCROBBLER_PLUGIN (bplugin);

	lastfm = rb_audioscrobbler_service_new_lastfm ();
	plugin->lastfm_source = rb_audioscrobbler_profile_source_new (shell, bplugin, lastfm);

	librefm = rb_audioscrobbler_service_new_librefm ();
	plugin->librefm_source = rb_audioscrobbler_profile_source_new (shell, bplugin, librefm);

	g_object_unref (lastfm);
	g_object_unref (librefm);
}

static void
impl_deactivate	(RBPlugin *bplugin,
		 RBShell *shell)
{
	RBAudioscrobblerPlugin *plugin = RB_AUDIOSCROBBLER_PLUGIN (bplugin);

	rb_source_delete_thyself (plugin->lastfm_source);
	plugin->lastfm_source = NULL;

	rb_source_delete_thyself (plugin->librefm_source);
	plugin->librefm_source = NULL;
}
