/* rb-missing-plugins.c

   Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
   Copyright (C) 2007 Jonathan Matthew <jonathan@d14n.org>

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Based on totem-missing-plugins.c, authored by Tim-Philipp Müller <tim centricular net>
 */

#include "config.h"

#include "rb-missing-plugins.h"

#include "rhythmdb.h"
#include "rb-shell-player.h"
#include "rb-debug.h"
#include "rb-podcast-manager.h"
#include "gseal-gtk-compat.h"

#include <gst/pbutils/pbutils.h>
#include <gst/pbutils/install-plugins.h>

#include <gst/gst.h> /* for gst_registry_update */

#include <gtk/gtk.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include <string.h>

/* list of blacklisted detail strings */
static GList *blacklisted_plugins = NULL;

typedef struct
{
	GClosure   *closure;
	gchar     **details;
	RBShell    *shell;
} RBPluginInstallContext;

static gboolean
rb_plugin_install_plugin_is_blacklisted (const gchar * detail)
{
	GList *res;

	res = g_list_find_custom (blacklisted_plugins,
	                          detail,
	                          (GCompareFunc) strcmp);

	return (res != NULL);	
}

static void
rb_plugin_install_blacklist_plugin (const gchar * detail)
{
	if (!rb_plugin_install_plugin_is_blacklisted (detail)) {
		blacklisted_plugins = g_list_prepend (blacklisted_plugins,
		                                      g_strdup (detail));
	}
}

static void
rb_plugin_install_context_free (RBPluginInstallContext *ctx)
{
	rb_debug ("cleaning up plugin install context %p", ctx);
	g_strfreev (ctx->details);
	g_object_unref (ctx->shell);
	g_closure_unref (ctx->closure);
	g_free (ctx);
}

static void
rb_plugin_install_done (RBPluginInstallContext *ctx, gboolean retry)
{
	GValue param[2] = { {0,}, {0,} };

	rb_debug ("invoking plugin install context %p callback: retry %d", ctx, retry);

	g_value_init (&param[0], G_TYPE_POINTER);
	g_value_set_pointer (&param[0], NULL);
	g_value_init (&param[1], G_TYPE_BOOLEAN);
	g_value_set_boolean (&param[1], retry);

	g_closure_invoke (ctx->closure, NULL, 2, param, NULL);
	g_value_unset (&param[0]);
	g_value_unset (&param[1]);
}

static void
on_plugin_installation_done (GstInstallPluginsReturn res, gpointer user_data)
{
	RBPluginInstallContext *ctx = (RBPluginInstallContext *) user_data;
	gchar **p;
	gboolean retry;

	rb_debug ("res = %d (%s)", res, gst_install_plugins_return_get_name (res));

	switch (res)
	{
		/* treat partial success the same as success; in the worst case we'll
		 * just do another round and get NOT_FOUND as result that time */
		case GST_INSTALL_PLUGINS_PARTIAL_SUCCESS:
		case GST_INSTALL_PLUGINS_SUCCESS:
		{
			/* blacklist installed plugins too, so that we don't get
			 * into endless installer loops in case of inconsistencies */
			for (p = ctx->details; p != NULL && *p != NULL; ++p) {
				rb_plugin_install_blacklist_plugin (*p);
			}

			g_message ("Missing plugins installed. Updating plugin registry ...");

			/* force GStreamer to re-read its plugin registry */
			retry = gst_update_registry ();

			rb_plugin_install_done (ctx, retry);
			break;
		}

		case GST_INSTALL_PLUGINS_NOT_FOUND:
		{
			g_message ("No installation candidate for missing plugins found.");

			/* NOT_FOUND should only be returned if not a single one of the
			 * requested plugins was found; if we managed to play something
			 * anyway, we should just continue playing what we have and
			 * blacklist the requested plugins for this session; if we
			 * could not play anything we should blacklist them as well,
			 * so the install wizard isn't called again for nothing */
			for (p = ctx->details; p != NULL && *p != NULL; ++p) {
				rb_plugin_install_blacklist_plugin (*p);
			}

			rb_plugin_install_done (ctx, FALSE);
			break;
		}

		case GST_INSTALL_PLUGINS_USER_ABORT:
		{
			/* blacklist on user abort, so we show an error next time (or
			 * just play what we can) instead of calling the installer */
			for (p = ctx->details; p != NULL && *p != NULL; ++p) {
				rb_plugin_install_blacklist_plugin (*p);
			}

			rb_plugin_install_done (ctx, FALSE);
			break;
		}

		case GST_INSTALL_PLUGINS_ERROR:
		case GST_INSTALL_PLUGINS_CRASHED:
		default:
		{
			g_message ("Missing plugin installation failed: %s",
				   gst_install_plugins_return_get_name (res));

			rb_plugin_install_done (ctx, FALSE);
			break;
		}
	}

	rb_plugin_install_context_free (ctx);
}

static gboolean
missing_plugins_event (RBShell *shell, RBPluginInstallContext *ctx)
{
	GstInstallPluginsContext *install_ctx;
	GstInstallPluginsReturn status;
	int i, num;
	GtkWindow *window;

	num = g_strv_length (ctx->details);
	for (i = 0; i < num; ++i) {
		if (rb_plugin_install_plugin_is_blacklisted (ctx->details[i])) {
			g_message ("Missing plugin: %s (ignoring)", ctx->details[i]);
			g_free (ctx->details[i]);
			ctx->details[i] = ctx->details[num-1];
			ctx->details[num-1] = NULL;
			--num;
			--i;
		} else {
			g_message ("Missing plugin: %s", ctx->details[i]);
		}
	}

	if (num == 0) {
		g_message ("All missing plugins are blacklisted, doing nothing");
		rb_plugin_install_context_free (ctx);
		return FALSE;
	}

	install_ctx = gst_install_plugins_context_new ();

	g_object_get (shell, "window", &window, NULL);
	if (gtk_widget_get_realized (GTK_WIDGET (window))) {
#ifdef GDK_WINDOWING_X11
		gulong xid = 0;
		xid = gdk_x11_drawable_get_xid (gtk_widget_get_window (GTK_WIDGET (window)));
		gst_install_plugins_context_set_xid (install_ctx, xid);
#endif
		g_object_unref (window);
	}

	status = gst_install_plugins_async (ctx->details, install_ctx,
	                                    on_plugin_installation_done,
	                                    ctx);

	gst_install_plugins_context_free (install_ctx);

	rb_debug ("gst_install_plugins_async() result = %d", status);

	if (status != GST_INSTALL_PLUGINS_STARTED_OK) {
		if (status == GST_INSTALL_PLUGINS_HELPER_MISSING) {
			g_message ("Automatic missing codec installation not supported "
			           "(helper script missing)");
		} else {
			g_warning ("Failed to start codec installation: %s",
			           gst_install_plugins_return_get_name (status));
		}
		rb_plugin_install_context_free (ctx);
		return FALSE;
	}

	return TRUE;
}

static gboolean
missing_plugins_cb (gpointer instance,
		    const char **details,
		    GClosure *closure,
		    RBShell *shell)
{
	RBPluginInstallContext *ctx;
	guint num;

	num = g_strv_length ((char **)details);
	g_return_val_if_fail (num > 0, FALSE);

	ctx = g_new0 (RBPluginInstallContext, 1);
	ctx->closure = g_closure_ref (closure);
	ctx->details = g_strdupv ((char **)details);
	ctx->shell = g_object_ref (shell);

	return missing_plugins_event (shell, ctx);
}

void
rb_missing_plugins_init (RBShell *shell)
{
	RBShellPlayer *player;
	RBSource *podcast_source;
	RBPodcastManager *podcast_mgr;

	g_object_get (shell,
		      "shell-player", &player,
		      NULL);
	g_signal_connect (player,
			  "missing-plugins",
			  G_CALLBACK (missing_plugins_cb),
			  shell);

	g_object_unref (player);

	podcast_source = rb_shell_get_source_by_entry_type (shell, RHYTHMDB_ENTRY_TYPE_PODCAST_FEED);
	g_object_get (podcast_source, "podcast-manager", &podcast_mgr, NULL);

	g_signal_connect (podcast_mgr,
			  "missing-plugins",
			  G_CALLBACK (missing_plugins_cb),
			  shell);

	g_object_unref (podcast_mgr);

	gst_pb_utils_init ();

	GST_INFO ("Set up support for automatic missing plugin installation");
}
