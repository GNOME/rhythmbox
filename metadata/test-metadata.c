/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006 Jonathan Matthew <jonathan@kaolin.hn.org>
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

/*
 * Test client for out-of-process metadata reader.
 */

#include <config.h>
#include <locale.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "rb-metadata.h"
#include "rb-metadata-dbus.h"
#include "rb-debug.h"

static RBMetaData *md = NULL;

static gboolean debug = FALSE;
static gboolean can_save = FALSE;

static GOptionEntry entries [] = {
	{ "debug", 0, 0, G_OPTION_ARG_NONE, &debug, NULL, NULL },
	{ "can-save", 0, 0, G_OPTION_ARG_NONE, &can_save, NULL, NULL },
	{ NULL }
};

static void
print_metadata_string (RBMetaData *md, RBMetaDataField field, const char *name)
{
	GValue v = {0,};
	if (rb_metadata_get (md, field, &v)) {
		char *s;

		s = g_strdup_value_contents (&v);
		printf ("%s: %s\n", name, s);
		g_free (s);
		g_value_unset (&v);
	}
}

static gboolean
check_can_save_cb (gpointer mt)
{
	char *media_type = (char *)mt;

	if (rb_metadata_can_save (md, media_type)) {
		printf ("Can save %s\n", media_type);
	} else {
		printf ("Unable to save %s\n", media_type);
	}

	return FALSE;
}

static gboolean
load_metadata_cb (gpointer file)
{
	char *uri = (char *)file;
	char **missing_plugins;
	char **plugin_descriptions;
	GError *error = NULL;
	RBMetaDataField f;

	if (strncmp (uri, "file://", 7)) {
		if (uri[0] == '/') {
			uri = g_filename_to_uri (uri, NULL, NULL);
		} else {
			char buf[1024];
			if (getcwd (buf, sizeof (buf)) != NULL) {
				char *filename;

				filename = g_build_filename (buf, uri, NULL);
				uri = g_filename_to_uri (filename, NULL, NULL);
				g_free (filename);
			}
		}
	}
	printf ("%s\n", (const char *)uri);

	rb_metadata_load (md, (const char *)uri, &error);

	if (error) {
		printf ("error: %s\n", error->message);
		g_clear_error (&error);
	}

	printf ("type: %s\n", rb_metadata_get_media_type (md));
	for (f =(RBMetaDataField)0; f < RB_METADATA_FIELD_LAST; f++)
		print_metadata_string (md, f, rb_metadata_get_field_name (f));

	printf ("has audio: %d\n", rb_metadata_has_audio (md));
	printf ("has video: %d\n", rb_metadata_has_video (md));
	printf ("has other data: %d\n", rb_metadata_has_other_data (md));

	if (rb_metadata_get_missing_plugins (md, &missing_plugins, &plugin_descriptions)) {
		int i = 0;
		g_print ("missing plugins:\n");
		while (missing_plugins[i] != NULL) {
			g_print ("\t%s (%s)\n", missing_plugins[i], plugin_descriptions[i]);
			i++;
		}
		g_strfreev (missing_plugins);
	}
	printf ("---\n");
	return FALSE;
}

static gboolean
bye (gpointer nah)
{
	g_main_loop_quit ((GMainLoop *)nah);
	return FALSE;
}

int main(int argc, char **argv)
{
	GMainLoop *loop;
	int filecount = 0;
	GOptionContext *context;
	gboolean retval;
	GError *error = NULL;

	setlocale (LC_ALL, "");

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);
	retval = g_option_context_parse (context, &argc, &argv, &error);

	g_option_context_free (context);

	if (! retval) {
		g_warning ("%s", error->message);
		g_error_free (error);
		exit (1);
	}

	if (debug) {
		rb_debug_init (TRUE);
	}

	loop = g_main_loop_new (NULL, FALSE);
	md = rb_metadata_new ();
	while (argv[1] != NULL) {
		if (can_save) {
			g_idle_add (check_can_save_cb, argv[1]);
		} else {
			g_idle_add (load_metadata_cb, argv[1]);
		}
		argv++;
		filecount++;
	}
	g_idle_add (bye, loop);

	g_main_loop_run (loop);

	if (can_save) {
		printf ("%d file type(s) checked\n", filecount);
	} else {
		printf ("%d file(s) read\n", filecount);
	}
	g_object_unref (G_OBJECT (md));
	return 0;
}
