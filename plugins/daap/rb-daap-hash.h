/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Header for DAAP (iTunes Music Sharing) hashing, connection
 *
 *  Copyright (C) 2004-2005 Charles Schmidt <cschmidt2@emich.edu>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __RB_DAAP_HASH_H
#define __RB_DAAP_HASH_H

#include <glib.h>

G_BEGIN_DECLS

void rb_daap_hash_generate (short         version_major,
			    const guchar *url,
			    guchar        hash_select,
			    guchar       *out,
			    gint          request_id);

G_END_DECLS

#endif /* __RB_DAAP_HASH_H */
