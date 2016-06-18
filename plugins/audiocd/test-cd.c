/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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

#include <gst/gst.h>

#include "rb-audiocd-info.h"
#include "rb-musicbrainz-lookup.h"

GMainLoop *mainloop = NULL;

static void
indent (int d)
{
	int i;
	for (i = 0; i < d; i++)
		g_print("  ");
}

static void
dump (RBMusicBrainzData *d, int depth, int recurse)
{
	GList *l, *i;

	indent (depth);
	g_print ("%s {\n", rb_musicbrainz_data_get_data_type (d));
	l = rb_musicbrainz_data_get_attr_names (d);
	for (i = l; i != NULL; i = i->next) {
		GList *v, *vi;

		indent (depth+1);
		g_print ("%s = [", (char *)i->data);
		v = rb_musicbrainz_data_get_attr_values (d, i->data);
		for (vi = v; vi != NULL; vi = vi->next) {
			if (vi != v)
				g_print (", ");
			g_print ("%s", (char *)vi->data);
		}
		g_list_free (v);
		g_print ("]\n");
	}
	g_list_free (l);

	if (recurse) {
		l = rb_musicbrainz_data_get_children (d);
		for (i = l; i != NULL; i = i->next) {
			dump (i->data, depth+1, recurse);
		}
		g_list_free (l);
	}

	indent (depth);
	g_print ("}\n");
}

static void
release_lookup_cb (GObject *object, GAsyncResult *result, RBAudioCDInfo *info)
{
	RBMusicBrainzData *dd;
	GError *error = NULL;
	dd = rb_musicbrainz_lookup_finish (result, &error);
	if (dd != NULL) {
		RBMusicBrainzData *r;
		RBMusicBrainzData *m;

		r = rb_musicbrainz_data_get_children (dd)->data;
		m = rb_musicbrainz_data_find_child (r, "disc-id", info->musicbrainz_disc_id);
		dump (r, 0, 0);
		dump (m, 0, 1);
		rb_musicbrainz_data_free (dd);
	}

	g_main_loop_quit (mainloop);
	rb_audiocd_info_free (info);
}

static void
lookup_cb (GObject *object, GAsyncResult *result, RBAudioCDInfo *info)
{
	RBMusicBrainzData *dd;
	GError *error = NULL;
	GList *l;
	const char *release_includes[] = {
		"discids",
		"media",
		"recordings",
		"artist-credits",
		"work-rels",
		"recording-level-rels",
		"work-level-rels",
		"artist-rels",
		"url-rels",
		NULL
	};

	dd = rb_musicbrainz_lookup_finish (result, &error);
	if (dd != NULL) {
		g_print ("\n\n");
		for (l = rb_musicbrainz_data_get_children (dd); l != NULL; l = l->next) {
			RBMusicBrainzData *r = l->data;

			RBMusicBrainzData *m = rb_musicbrainz_data_find_child (r, "disc-id", info->musicbrainz_disc_id);
			if (m != NULL) {
				dump (r, 0, 0);
				dump (m, 0, 1);

				rb_musicbrainz_lookup ("release",
						       rb_musicbrainz_data_get_attr_value (r, RB_MUSICBRAINZ_ATTR_ALBUM_ID),
						       release_includes,
						       NULL,
						       (GAsyncReadyCallback) release_lookup_cb,
						       info);
				break;
			}
		}
		rb_musicbrainz_data_free (dd);
	} else {
		g_print ("lookup failed: %s\n", error->message);
		g_clear_error (&error);
		g_main_loop_quit (mainloop);
	}

}


static void
audiocd_info_cb (GObject *source, GAsyncResult *result, gpointer data)
{
	RBAudioCDInfo *info;
	GError *error = NULL;
	int i;

	info = rb_audiocd_info_finish (result, &error);

	if (error != NULL) {
		g_print ("err: %s\n", error->message);
		g_clear_error (&error);
	} else {
		g_print ("disc id: %s\n", info->musicbrainz_disc_id);
		g_print ("%d tracks\n", info->num_tracks);

		for (i = 0; i < info->num_tracks; i++) {
			g_print ("%d: %ds\n", info->tracks[i].track_num, info->tracks[i].duration/1000);
		}
	}

	if (info->musicbrainz_disc_id) {
		const char *includes[] = { "artist-credits", NULL };

		rb_musicbrainz_lookup ("discid",
				       info->musicbrainz_disc_id,
				       includes,
				       NULL,
				       (GAsyncReadyCallback) lookup_cb,
				       info);
	} else {
		rb_audiocd_info_free (info);
		g_main_loop_quit (mainloop);
	}
}

static void
release_cb (GObject *source, GAsyncResult *result, gpointer data)
{
	RBMusicBrainzData *dd;
	GError *error = NULL;

	dd = rb_musicbrainz_lookup_finish (result, &error);
	if (dd != NULL) {
		dump (dd, 0, 1);
		rb_musicbrainz_data_free (dd);
	} else {
		g_print ("lookup failed: %s\n", error->message);
		g_clear_error (&error);
	}

	g_main_loop_quit (mainloop);
}

static gboolean
go (const char *thing)
{
	if (thing[0] == '/') {
		rb_audiocd_info_get (thing, NULL, (GAsyncReadyCallback)audiocd_info_cb, NULL);
	} else {
		const char *includes[] = { "artist-credits", NULL };
		rb_musicbrainz_lookup ("release", thing, includes, NULL, (GAsyncReadyCallback) release_cb, NULL);
	}
	return FALSE;
}

int
main (int argc, char **argv)
{
	char *thing;

	gst_init (NULL, NULL);

	if (argv[1] == NULL) {
		thing = "/dev/cdrom";
	} else {
		thing = argv[1];
	}

	mainloop = g_main_loop_new (NULL, FALSE);
	g_idle_add ((GSourceFunc) go, thing);
	g_main_loop_run (mainloop);

	gst_deinit ();

	return 0;
}
