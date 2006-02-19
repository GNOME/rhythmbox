/*
 *  Copyright (C) 2006 Jonathan Matthew <jonathan@kaolin.hn.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

/*
 * Test client for out-of-process metadata reader.
 */

#include <config.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "rb-metadata.h"
#include "rb-metadata-dbus.h"
#include "rb-debug.h"

static RBMetaData *md = NULL;

static void 
print_metadata_string (RBMetaData *md, RBMetaDataField field, const char *name)
{
	GValue v = {0,};
	if (rb_metadata_get (md, field, &v)) {
		printf ("%s: %s\n", name, g_value_get_string (&v));
		g_value_unset (&v);
	} else {
		printf ("%s: unknown\n", name);
	}
}

static gboolean
check_can_save_cb (gpointer mt)
{
	char *mimetype = (char *)mt;

	if (rb_metadata_can_save (md, mimetype)) {
		printf ("Can save %s\n", mimetype);
	} else {
		printf ("Unable to save %s\n", mimetype);
	}

	return FALSE;
}

static gboolean
load_metadata_cb (gpointer file)
{
	char *uri = (char *)file;
	GError *error = NULL;

	if (strncmp (uri, "file://", 7)) {
		if (uri[0] == '/') {
			uri = g_strdup_printf ("file://%s", uri);
		} else {
			char buf[600];
			getcwd (buf, sizeof (buf));
			uri = g_strdup_printf ("file://%s/%s", buf, uri);
		}
	}
	printf ("%s\n", (const char *)uri);
	
	rb_metadata_load (md, (const char *)uri, &error);
	
	if (error) {
		printf ("error: %s\n", error->message);
		g_clear_error (&error);
	} else {
		printf ("type: %s\n", rb_metadata_get_mime (md));
		print_metadata_string (md, RB_METADATA_FIELD_TITLE, "title");
		print_metadata_string (md, RB_METADATA_FIELD_ARTIST, "artist");
		print_metadata_string (md, RB_METADATA_FIELD_ALBUM, "album");
		print_metadata_string (md, RB_METADATA_FIELD_GENRE, "genre");
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

	g_type_init ();
	if (strcmp (argv[1], "--debug") == 0) {
		rb_debug_init (TRUE);
		argv++;
	}

	if (strcmp (argv[1], "--can-save") == 0) {
		g_idle_add (check_can_save_cb, argv[2]);
		argv += 2;
	}
	
	loop = g_main_loop_new (NULL, FALSE);
	md = rb_metadata_new ();
	while (argv[1] != NULL) {
		g_idle_add (load_metadata_cb, argv[1]);
		argv++;
		filecount++;
	}
	g_idle_add (bye, loop);

	g_main_loop_run (loop);
	
	printf ("%d file(s) read\n", filecount);
	g_object_unref (G_OBJECT (md));
	return 0;
}

