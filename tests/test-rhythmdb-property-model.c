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


#include "config.h"

#include <check.h>
#include <gtk/gtk.h>
#include <locale.h>
#include "test-utils.h"
#include "rhythmdb-query-model.h"
#include "rhythmdb-property-model.h"

#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

static gulong pre_delete_sig_handler;
static gulong post_delete_sig_handler;

static int
_get_property_count (RhythmDBPropertyModel *model, const char *artist)
{
	GtkTreeIter iter;
	int count;

	if (rhythmdb_property_model_iter_from_string (model, artist, &iter) == FALSE) {
		return 0;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_NUMBER, &count, -1);
	return count;
}

static void
verify_pre_row_deletion (RhythmDBPropertyModel *propmodel)
{
	char *artist;
	gint nrows;
	GtkTreeIter iter;

	nrows = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (propmodel), NULL);
	ck_assert (nrows == 3);

	/* skip 'All' */
	ck_assert (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (propmodel), &iter));

	/* artist 1 */
	ck_assert (gtk_tree_model_iter_next (GTK_TREE_MODEL (propmodel), &iter));
	gtk_tree_model_get (GTK_TREE_MODEL (propmodel), &iter,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &artist, -1);
	ck_assert (g_strcmp0 (artist, "x") == 0);
	g_free (artist);

	/* artist 2 */
	ck_assert (gtk_tree_model_iter_next (GTK_TREE_MODEL (propmodel), &iter));
	gtk_tree_model_get (GTK_TREE_MODEL (propmodel), &iter,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &artist, -1);
	ck_assert (g_strcmp0 (artist, "y") == 0);
	g_free (artist);

	/* end of model */
	ck_assert (gtk_tree_model_iter_next (GTK_TREE_MODEL (propmodel), &iter) == FALSE);

	g_signal_handler_disconnect (G_OBJECT (propmodel), pre_delete_sig_handler);
}

static void
verify_post_row_deletion (RhythmDBPropertyModel *propmodel)
{
	char *artist;
	gint nrows;
	GtkTreeIter iter;

	nrows = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (propmodel), NULL);
	ck_assert (nrows == 2);

	/* skip 'All' */
	ck_assert (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (propmodel), &iter));

	/* artist 1 */
	ck_assert (gtk_tree_model_iter_next (GTK_TREE_MODEL (propmodel), &iter));
	gtk_tree_model_get (GTK_TREE_MODEL (propmodel), &iter,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &artist, -1);
	ck_assert (g_strcmp0 (artist, "y") == 0);
	g_free (artist);

	/* end of model */
	ck_assert (gtk_tree_model_iter_next (GTK_TREE_MODEL (propmodel), &iter) == FALSE);

	g_signal_handler_disconnect (G_OBJECT (propmodel), post_delete_sig_handler);
}

/* tests property models attached to static query models */
START_TEST (test_rhythmdb_property_model_static)
{
	RhythmDBQueryModel *model;
	RhythmDBQueryModel *model2;
	RhythmDBPropertyModel *propmodel;
	RhythmDBEntry *a, *b;
	GtkTreeIter iter;

	start_test_case ();

	/* setup */
	model = rhythmdb_query_model_new_empty (db);
	g_object_set (model, "show-hidden", FALSE, NULL);
	propmodel = rhythmdb_property_model_new (db, RHYTHMDB_PROP_ARTIST);
	g_object_set (propmodel, "query-model", model, NULL);

	/* create test entries */
	a = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///a.ogg");
	b = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///b.ogg");
	rhythmdb_commit (db);

	/* set artist values */
	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "x");
	set_entry_string (db, b, RHYTHMDB_PROP_ARTIST, "y");
	rhythmdb_commit (db);

	end_step ();

	/* add to model */
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	rhythmdb_query_model_add_entry (model, a, -1);
	wait_for_signal ();
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	rhythmdb_query_model_add_entry (model, b, -1);
	wait_for_signal ();
	ck_assert (rhythmdb_query_model_entry_to_iter (model, a, &iter));
	ck_assert (rhythmdb_query_model_entry_to_iter (model, b, &iter));
	/*ck_assert (_get_property_count (propmodel, _("All")) == 2);*/
	ck_assert (_get_property_count (propmodel, "x") == 1);
	ck_assert (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* change one */
	pre_delete_sig_handler = set_waiting_signal_with_callback (G_OBJECT (propmodel),
								   "pre-row-deletion",
								   G_CALLBACK (verify_pre_row_deletion),
								   NULL);
	post_delete_sig_handler = set_waiting_signal_with_callback (G_OBJECT (propmodel),
								    "row-deleted",
								    G_CALLBACK (verify_post_row_deletion),
								    NULL);

	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "y");
	rhythmdb_commit (db);
	wait_for_signal ();
	ck_assert (_get_property_count (propmodel, "x") == 0);
	ck_assert (_get_property_count (propmodel, "y") == 2);

	end_step ();

	/* hide it */
	set_waiting_signal (G_OBJECT (model), "entry-prop-changed");
	set_entry_hidden (db, a, TRUE);
	rhythmdb_commit (db);
	wait_for_signal ();
	/*ck_assert (_get_property_count (propmodel, _("All")) == 1);*/
	ck_assert (_get_property_count (propmodel, "x") == 0);
	ck_assert (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* change back */
	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "x");
	rhythmdb_commit (db);
	ck_assert (_get_property_count (propmodel, "x") == 0);
	ck_assert (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* unhide */
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	set_entry_hidden (db, a, FALSE);
	rhythmdb_commit (db);
	wait_for_signal ();

	/*ck_assert (_get_property_count (propmodel, _("All")) == 2);*/
	ck_assert (_get_property_count (propmodel, "x") == 1);
	ck_assert (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* remove one */
	set_waiting_signal (G_OBJECT (propmodel), "pre-row-deletion");
	rhythmdb_query_model_remove_entry (model, a);
	wait_for_signal ();
	/*ck_assert (_get_property_count (propmodel, _("All")) == 1);*/
	ck_assert (_get_property_count (propmodel, "x") == 0);
	ck_assert (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* switch model */
	model2 = rhythmdb_query_model_new_empty (db);
	g_object_set (model2, "show-hidden", FALSE, NULL);
	rhythmdb_query_model_add_entry (model2, a, -1);
	rhythmdb_query_model_add_entry (model2, b, -1);
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	g_object_set (propmodel, "query-model", model2, NULL);
	wait_for_signal ();

	ck_assert (_get_property_count (propmodel, "x") == 1);
	ck_assert (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* delete an entry */
	set_waiting_signal (G_OBJECT (db), "entry_deleted");
	rhythmdb_entry_delete (db, a);
	rhythmdb_commit (db);
	wait_for_signal ();
	ck_assert (_get_property_count (propmodel, "x") == 0);
	ck_assert (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* and the other */
	set_waiting_signal (G_OBJECT (propmodel), "row-deleted");
	rhythmdb_entry_delete (db, b);
	rhythmdb_commit (db);
	wait_for_signal ();
	ck_assert (_get_property_count (propmodel, "x") == 0);
	ck_assert (_get_property_count (propmodel, "y") == 0);

	end_test_case ();

	g_object_unref (model);
	g_object_unref (model2);
	g_object_unref (propmodel);
}
END_TEST

/* tests property models attached to query models with an actual query */
START_TEST (test_rhythmdb_property_model_query)
{
	RhythmDBQueryModel *model;
	RhythmDBQueryModel *model2;
	RhythmDBPropertyModel *propmodel;
	RhythmDBEntry *a, *b;
	GPtrArray *query;

	start_test_case ();

	/* setup */
	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				        RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_IGNORE,
				      RHYTHMDB_QUERY_PROP_LIKE,
				        RHYTHMDB_PROP_ARTIST, "x",
				      RHYTHMDB_QUERY_END);

	model = rhythmdb_query_model_new (db, query, (GCompareDataFunc)rhythmdb_query_model_location_sort_func, NULL, NULL, FALSE);
	rhythmdb_query_free (query);

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				        RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_IGNORE,
				      RHYTHMDB_QUERY_PROP_LIKE,
				        RHYTHMDB_PROP_ARTIST, "y",
				      RHYTHMDB_QUERY_END);
	model2 = rhythmdb_query_model_new (db, query, (GCompareDataFunc)rhythmdb_query_model_location_sort_func, NULL, NULL, FALSE);
	rhythmdb_query_free (query);


	propmodel = rhythmdb_property_model_new (db, RHYTHMDB_PROP_ARTIST);
	g_object_set (propmodel, "query-model", model, NULL);

	end_step ();

	/* create test entries */
	set_waiting_signal (G_OBJECT (db), "entry_added");
	a = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///a.ogg");
	b = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///b.ogg");
	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "x");
	set_entry_string (db, b, RHYTHMDB_PROP_ARTIST, "y");
	rhythmdb_commit (db);
	wait_for_signal ();

	ck_assert (_get_property_count (propmodel, "x") == 1);
	ck_assert (_get_property_count (propmodel, "y") == 0);

	end_step ();

	/* change b so it matches the query */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_string (db, b, RHYTHMDB_PROP_ARTIST, "x");
	rhythmdb_commit (db);
	wait_for_signal ();
	ck_assert (_get_property_count (propmodel, "x") == 2);
	ck_assert (_get_property_count (propmodel, "y") == 0);

	end_step ();

	/* change b again */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_string (db, b, RHYTHMDB_PROP_ARTIST, "xx");
	rhythmdb_commit (db);
	wait_for_signal ();
	ck_assert (_get_property_count (propmodel, "x") == 1);
	ck_assert (_get_property_count (propmodel, "xx") == 1);
	ck_assert (_get_property_count (propmodel, "y") == 0);

	end_step ();

	/* hide a */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_hidden (db, a, TRUE);
	rhythmdb_commit (db);
	wait_for_signal ();
	ck_assert (_get_property_count (propmodel, "x") == 0);
	ck_assert (_get_property_count (propmodel, "xx") == 1);

	end_step ();

	/* change a */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "xx");
	rhythmdb_commit (db);
	wait_for_signal ();
	ck_assert (_get_property_count (propmodel, "x") == 0);
	ck_assert (_get_property_count (propmodel, "xx") == 1);

	end_step ();

	/* unhide a */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_hidden (db, a, FALSE);
	rhythmdb_commit (db);
	wait_for_signal ();
	ck_assert (_get_property_count (propmodel, "x") == 0);
	ck_assert (_get_property_count (propmodel, "xx") == 2);

	end_step ();

	/* change a -> y */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "y");
	rhythmdb_commit (db);
	wait_for_signal ();
	ck_assert (_get_property_count (propmodel, "x") == 0);
	ck_assert (_get_property_count (propmodel, "xx") == 1);
	ck_assert (_get_property_count (propmodel, "y") == 0);

	end_step ();

	/* switch to model2 */
	g_object_set (propmodel, "query-model", model2, NULL);
	ck_assert (_get_property_count (propmodel, "x") == 0);
	ck_assert (_get_property_count (propmodel, "y") == 1);
	ck_assert (_get_property_count (propmodel, "xx") == 0);

	end_step ();

	/* change a -> x */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "x");
	rhythmdb_commit (db);
	wait_for_signal ();
	ck_assert (_get_property_count (propmodel, "x") == 0);
	ck_assert (_get_property_count (propmodel, "xx") == 0);
	ck_assert (_get_property_count (propmodel, "y") == 0);

	end_step ();

	rhythmdb_entry_delete (db, a);
	rhythmdb_entry_delete (db, b);
	rhythmdb_commit (db);

	end_test_case ();

	g_object_unref (model);
	g_object_unref (model2);
	g_object_unref (propmodel);
}
END_TEST

/* tests property models attached to chained query models */
START_TEST (test_rhythmdb_property_model_query_chain)
{
	RhythmDBQueryModel *base_model;
	RhythmDBQueryModel *model;
	RhythmDBPropertyModel *propmodel;
	RhythmDBQuery *query;
	RhythmDBEntry *a, *b;

	start_test_case ();

	/* setup */
	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				        RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_IGNORE,
				      RHYTHMDB_QUERY_PROP_LIKE,
				        RHYTHMDB_PROP_SEARCH_MATCH, "y",
				      RHYTHMDB_QUERY_END);

	base_model = rhythmdb_query_model_new (db, query, (GCompareDataFunc)rhythmdb_query_model_location_sort_func, NULL, NULL, FALSE);
	rhythmdb_query_free (query);

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				        RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_IGNORE,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				        RHYTHMDB_PROP_TRACK_NUMBER, 1,
				      RHYTHMDB_QUERY_END);
	model = rhythmdb_query_model_new (db, query, (GCompareDataFunc)rhythmdb_query_model_location_sort_func, NULL, NULL, FALSE);
	rhythmdb_query_free (query);

	rhythmdb_query_model_chain (model, base_model, TRUE);

	propmodel = rhythmdb_property_model_new (db, RHYTHMDB_PROP_ALBUM);
	g_object_set (propmodel, "query-model", model, NULL);

	/* create test entries */
	set_waiting_signal (G_OBJECT (db), "entry_added");
	a = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///a.ogg");
	set_entry_string (db, a, RHYTHMDB_PROP_ALBUM, "x");
	set_entry_ulong (db, a, RHYTHMDB_PROP_TRACK_NUMBER, 1);
	rhythmdb_commit (db);
	wait_for_signal ();

	set_waiting_signal (G_OBJECT (db), "entry_added");
	b = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///b.ogg");
	set_entry_string (db, b, RHYTHMDB_PROP_ALBUM, "y");
	set_entry_ulong (db, b, RHYTHMDB_PROP_TRACK_NUMBER, 1);
	rhythmdb_commit (db);
	wait_for_signal ();

	ck_assert (_get_property_count (propmodel, "x") == 0);
	ck_assert (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* change entry a so it matches the child query */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_string (db, a, RHYTHMDB_PROP_ALBUM, "yy");
	rhythmdb_commit (db);
	wait_for_signal ();

	ck_assert (_get_property_count (propmodel, "x") == 0);
	ck_assert (_get_property_count (propmodel, "y") == 1);
	ck_assert (_get_property_count (propmodel, "yy") == 1);

	end_step ();

	/* change entry a again */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_string (db, a, RHYTHMDB_PROP_ALBUM, "y");
	rhythmdb_commit (db);
	wait_for_signal ();

	ck_assert (_get_property_count (propmodel, "y") == 2);
	ck_assert (_get_property_count (propmodel, "yy") == 0);

	end_step ();

	/* change entry b again */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_string (db, b, RHYTHMDB_PROP_ALBUM, "z");
	rhythmdb_commit (db);
	wait_for_signal ();

	ck_assert (_get_property_count (propmodel, "y") == 1);
	ck_assert (_get_property_count (propmodel, "z") == 0);

	end_step ();

	rhythmdb_entry_delete (db, a);
	rhythmdb_entry_delete (db, b);
	rhythmdb_commit (db);

	end_test_case ();

	g_object_unref (model);
	g_object_unref (base_model);
	g_object_unref (propmodel);
}
END_TEST

/* tests sort order of entries in a property model */
START_TEST (test_rhythmdb_property_model_sorting)
{
	RhythmDBQueryModel *model;
	RhythmDBPropertyModel *propmodel;
	RhythmDBEntry *a, *the_b, *c;
	GtkTreeIter iter1, iter2;

	start_test_case ();

	/* setup */
	model = rhythmdb_query_model_new_empty (db);
	propmodel = rhythmdb_property_model_new (db, RHYTHMDB_PROP_ARTIST);
	g_object_set (propmodel, "query-model", model, NULL);

	/* create test entries */
	set_waiting_signal (G_OBJECT (db), "entry_added");
	a = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///a.ogg");
	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "a");
	rhythmdb_commit (db);
	wait_for_signal ();

	set_waiting_signal (G_OBJECT (db), "entry_added");
	the_b = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///the-b.ogg");
	set_entry_string (db, the_b, RHYTHMDB_PROP_ARTIST, "the b");
	rhythmdb_commit (db);
	wait_for_signal ();

	set_waiting_signal (G_OBJECT (db), "entry_added");
	c = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///c.ogg");
	set_entry_string (db, c, RHYTHMDB_PROP_ARTIST, "c");
	rhythmdb_commit (db);
	wait_for_signal ();

	/* add to model */
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	rhythmdb_query_model_add_entry (model, a, -1);
	wait_for_signal ();
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	rhythmdb_query_model_add_entry (model, the_b, -1);
	wait_for_signal ();
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	rhythmdb_query_model_add_entry (model, c, -1);
	wait_for_signal ();

	/* test a comes immediately before c */
	rhythmdb_property_model_iter_from_string (propmodel, "a", &iter1);
	rhythmdb_property_model_iter_from_string (propmodel, "c", &iter2);
	ck_assert (iter1.user_data == g_sequence_iter_prev (iter2.user_data));

	/* test c comes immediately before the_b */
	rhythmdb_property_model_iter_from_string (propmodel, "c", &iter1);
	rhythmdb_property_model_iter_from_string (propmodel, "the b", &iter2);
	ck_assert (iter1.user_data == g_sequence_iter_prev (iter2.user_data));

	end_step ();

	/* change "the b" to sort under "b, the" */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_string (db, the_b, RHYTHMDB_PROP_ARTIST_SORTNAME, "b, the");
	rhythmdb_commit (db);
	wait_for_signal ();

	/* test a comes immediately before the_b */
	rhythmdb_property_model_iter_from_string (propmodel, "a", &iter1);
	rhythmdb_property_model_iter_from_string (propmodel, "the b", &iter2);
	ck_assert (iter1.user_data == g_sequence_iter_prev (iter2.user_data));

	/* test the_b comes immediately before c */
	rhythmdb_property_model_iter_from_string (propmodel, "the b", &iter1);
	rhythmdb_property_model_iter_from_string (propmodel, "c", &iter2);
	ck_assert (iter1.user_data == g_sequence_iter_prev (iter2.user_data));

	end_step();

	/* change display name for b */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_string (db, the_b, RHYTHMDB_PROP_ARTIST, "THE B");
	rhythmdb_commit (db);
	wait_for_signal ();

	/* property model order shouldn't have changed */
	rhythmdb_property_model_iter_from_string (propmodel, "a", &iter1);
	rhythmdb_property_model_iter_from_string (propmodel, "THE B", &iter2);
	ck_assert (iter1.user_data == g_sequence_iter_prev (iter2.user_data));

	rhythmdb_property_model_iter_from_string (propmodel, "THE B", &iter1);
	rhythmdb_property_model_iter_from_string (propmodel, "c", &iter2);
	ck_assert (iter1.user_data == g_sequence_iter_prev (iter2.user_data));

	end_step();

	/* change sortname for b */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_string (db, the_b, RHYTHMDB_PROP_ARTIST_SORTNAME, "B, THE");
	rhythmdb_commit (db);
	wait_for_signal ();

	/* property model order shouldn't have changed */
	rhythmdb_property_model_iter_from_string (propmodel, "a", &iter1);
	rhythmdb_property_model_iter_from_string (propmodel, "THE B", &iter2);
	ck_assert (iter1.user_data == g_sequence_iter_prev (iter2.user_data));

	rhythmdb_property_model_iter_from_string (propmodel, "THE B", &iter1);
	rhythmdb_property_model_iter_from_string (propmodel, "c", &iter2);
	ck_assert (iter1.user_data == g_sequence_iter_prev (iter2.user_data));

	end_step();

	/* change sort order for b */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_string (db, the_b, RHYTHMDB_PROP_ARTIST_SORTNAME, "zzz");
	rhythmdb_commit (db);
	wait_for_signal ();

	/* property model order should have changed to match */
	rhythmdb_property_model_iter_from_string (propmodel, "a", &iter1);
	rhythmdb_property_model_iter_from_string (propmodel, "c", &iter2);
	ck_assert (iter1.user_data == g_sequence_iter_prev (iter2.user_data));

	rhythmdb_property_model_iter_from_string (propmodel, "c", &iter1);
	rhythmdb_property_model_iter_from_string (propmodel, "THE B", &iter2);
	ck_assert (iter1.user_data == g_sequence_iter_prev (iter2.user_data));

	end_step();

	/* remove sort order for b */
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	set_entry_string (db, the_b, RHYTHMDB_PROP_ARTIST_SORTNAME, "");
	rhythmdb_commit (db);
	wait_for_signal ();

	/* property model order should have changed to match */
	rhythmdb_property_model_iter_from_string (propmodel, "a", &iter1);
	rhythmdb_property_model_iter_from_string (propmodel, "c", &iter2);
	ck_assert (iter1.user_data == g_sequence_iter_prev (iter2.user_data));

	rhythmdb_property_model_iter_from_string (propmodel, "c", &iter1);
	rhythmdb_property_model_iter_from_string (propmodel, "THE B", &iter2);
	ck_assert (iter1.user_data == g_sequence_iter_prev (iter2.user_data));

	end_step();


	rhythmdb_entry_delete (db, a);
	rhythmdb_entry_delete (db, the_b);
	rhythmdb_entry_delete (db, c);
	rhythmdb_commit (db);

	end_test_case ();

	g_object_unref (model);
	g_object_unref (propmodel);
}
END_TEST

/* tests handling of empty strings */
START_TEST (test_rhythmdb_property_model_empty_strings)
{
	RhythmDBQueryModel *model;
	RhythmDBPropertyModel *propmodel;
	RhythmDBEntry *a, *b;

	start_test_case ();

	/* setup */
	model = rhythmdb_query_model_new_empty (db);
	propmodel = rhythmdb_property_model_new (db, RHYTHMDB_PROP_GENRE);
	g_object_set (propmodel, "query-model", model, NULL);

	/* create test entries */
	set_waiting_signal (G_OBJECT (db), "entry_added");
	a = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///a.ogg");
	set_entry_string (db, a, RHYTHMDB_PROP_GENRE, "unknown");
	rhythmdb_commit (db);
	wait_for_signal ();

	set_waiting_signal (G_OBJECT (db), "entry_added");
	b = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///b.ogg");
	set_entry_string (db, b, RHYTHMDB_PROP_GENRE, "something");
	rhythmdb_commit (db);
	wait_for_signal ();

	end_step ();

	/* add to model */
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	rhythmdb_query_model_add_entry (model, a, -1);
	wait_for_signal ();

	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	rhythmdb_query_model_add_entry (model, b, -1);
	wait_for_signal ();

	end_step ();

	/* set to empty string */
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	set_entry_string (db, a, RHYTHMDB_PROP_GENRE, "");
	rhythmdb_commit (db);
	wait_for_signal ();

	end_step ();

	/* set to non-empty string */
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	set_entry_string (db, a, RHYTHMDB_PROP_GENRE, "junk");
	rhythmdb_commit (db);
	wait_for_signal ();

	end_step ();

	/* set to empty string again */
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	set_entry_string (db, a, RHYTHMDB_PROP_GENRE, "");
	rhythmdb_commit (db);
	wait_for_signal ();

	end_step ();

	rhythmdb_entry_delete (db, a);
	rhythmdb_entry_delete (db, b);
	rhythmdb_commit (db);

	end_test_case ();
	g_object_unref (model);
	g_object_unref (propmodel);
}
END_TEST

static Suite *
rhythmdb_property_model_suite (void)
{
	Suite *s = suite_create ("rhythmdb-property-model");
	TCase *tc_chain = tcase_create ("rhythmdb-property-model-core");
	TCase *tc_bugs = tcase_create ("rhythmdb-property-model-bugs");

	suite_add_tcase (s, tc_chain);
	tcase_add_checked_fixture (tc_chain, test_rhythmdb_setup, test_rhythmdb_shutdown);
	suite_add_tcase (s, tc_bugs);
	tcase_add_checked_fixture (tc_bugs, test_rhythmdb_setup, test_rhythmdb_shutdown);

	/* test core functionality */
	tcase_add_test (tc_chain, test_rhythmdb_property_model_static);
	tcase_add_test (tc_chain, test_rhythmdb_property_model_query);
	tcase_add_test (tc_chain, test_rhythmdb_property_model_query_chain);
	tcase_add_test (tc_chain, test_rhythmdb_property_model_sorting);

	/* tests for breakable bug fixes */
/*	tcase_add_test (tc_bugs, test_hidden_chain_filter);*/
	tcase_add_test (tc_chain, test_rhythmdb_property_model_empty_strings);

	return s;
}

int
main (int argc, char **argv)
{
	int ret;
	SRunner *sr;
	Suite *s;

	/* init stuff */
	rb_profile_start ("rhythmdb-property-model test suite");

	rb_threads_init ();
	setlocale (LC_ALL, "");
	rb_debug_init (TRUE);
	rb_refstring_system_init ();
	rb_file_helpers_init ();

	/* setup tests */
	s = rhythmdb_property_model_suite ();
	sr = srunner_create (s);
	
	init_setup (sr, argc, argv);
	init_once (FALSE);
	
	srunner_run_all (sr, CK_NORMAL);
	ret = srunner_ntests_failed (sr);
	srunner_free (sr);

	rb_file_helpers_shutdown ();
	rb_refstring_system_shutdown ();

	rb_profile_end ("rhythmdb-property-model test suite");
	return ret;
}

