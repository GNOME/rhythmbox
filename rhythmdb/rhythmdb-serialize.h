/*
 *  arch-tag: Header for RhythmDB support for (de)serialization
 *
 *  Copyright (C) 2003 Colin Walters <walters@rhythmbox.org>
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

#ifndef RHYTHMDB_SERIALIZE_H
#define RHYTHMDB_SERIALIZE_H

#include <libxml/tree.h>
#include "rhythmdb.h"

G_BEGIN_DECLS

void		rhythmdb_query_serialize	(RhythmDB *db, GPtrArray *query,
						 xmlNodePtr node);

GPtrArray *	rhythmdb_query_deserialize	(RhythmDB *db, xmlNodePtr node);

const char *	rhythmdb_elt_name_from_propid	(gint propid);

G_END_DECLS

#endif /* __RHYTHMBDB_LEGACY_H */
