/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2008  Jonathan Matthew  <jonathan@d14n.org>
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

#include "rb-iradio-source-search.h"

static void	rb_iradio_source_search_class_init (RBIRadioSourceSearchClass *klass);
static void	rb_iradio_source_search_init (RBIRadioSourceSearch *search);

G_DEFINE_DYNAMIC_TYPE (RBIRadioSourceSearch, rb_iradio_source_search, RB_TYPE_SOURCE_SEARCH)

static RhythmDBQuery *
impl_create_query (RBSourceSearch *bsearch, RhythmDB *db, const char *search_text)
{
	return rhythmdb_query_parse (db,
				     RHYTHMDB_QUERY_PROP_LIKE,
				     RHYTHMDB_PROP_GENRE_FOLDED,
				     search_text,
				     RHYTHMDB_QUERY_DISJUNCTION,
				     RHYTHMDB_QUERY_PROP_LIKE,
				     RHYTHMDB_PROP_TITLE_FOLDED,
				     search_text,
				     RHYTHMDB_QUERY_END);
}

static void
rb_iradio_source_search_class_init (RBIRadioSourceSearchClass *klass)
{
	RBSourceSearchClass *search_class = RB_SOURCE_SEARCH_CLASS (klass);
	search_class->create_query = impl_create_query;
}

static void
rb_iradio_source_search_class_finalize (RBIRadioSourceSearchClass *klass)
{
}

static void
rb_iradio_source_search_init (RBIRadioSourceSearch *search)
{
	/* nothing */
}


RBSourceSearch *
rb_iradio_source_search_new (void)
{
	return g_object_new (RB_TYPE_IRADIO_SOURCE_SEARCH, NULL);
}

void
_rb_iradio_source_search_register_type (GTypeModule *module)
{
	rb_iradio_source_search_register_type (module);
}
