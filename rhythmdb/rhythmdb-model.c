/* 
 *  arch-tag: Implementation of RhythmDB model interface
 *
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#include "rhythmdb-model.h"

GType
rhythmdb_model_get_type (void)
{
	static GType rhythmdb_model_type = 0;

	if (rhythmdb_model_type == 0) {
		static const GTypeInfo our_info = {
			sizeof (RhythmDBModelIface),
			NULL,
			NULL,
			NULL,
			NULL,
			NULL,
			0,
			0,
			NULL
		};

		rhythmdb_model_type = g_type_register_static (G_TYPE_INTERFACE,
							      "RhythmDBModel",
							      &our_info, 0);
		g_type_interface_add_prerequisite (rhythmdb_model_type, G_TYPE_OBJECT);
	}

	return rhythmdb_model_type;
}

gboolean
rhythmdb_model_entry_to_iter (RhythmDBModel *model, RhythmDBEntry *entry, GtkTreeIter *iter)
{
	RhythmDBModelIface *iface = RHYTHMDB_MODEL_GET_IFACE (model);

	return iface->entry_to_iter (model, entry, iter);
}

gboolean
rhythmdb_model_poll (RhythmDBModel *model, GTimeVal *timeout)
{
	RhythmDBModelIface *iface = RHYTHMDB_MODEL_GET_IFACE (model);

	return iface->poll (model, timeout);
}

void
rhythmdb_model_cancel (RhythmDBModel *model)
{
	RhythmDBModelIface *iface = RHYTHMDB_MODEL_GET_IFACE (model);

	iface->cancel (model);
}

gboolean
rhythmdb_model_sortable (RhythmDBModel *model)
{
	RhythmDBModelIface *iface = RHYTHMDB_MODEL_GET_IFACE (model);

	return iface->sortable (model);
}
