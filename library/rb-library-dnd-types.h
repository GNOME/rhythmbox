/*
 *  arch-tag: Header with various type declarations for DND
 *
 *  Copyright (C) 2002 Olivier Martin <omartin@ifrance.com>
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

#ifndef __RB_LIBRARY_DND_TYPES_H
#define __RB_LIBRARY_DND_TYPES_H

G_BEGIN_DECLS

#define RB_LIBRARY_DND_URI_LIST_TYPE	"text/uri-list"
#define RB_LIBRARY_DND_NODE_ID_TYPE	"rb-node-id"

typedef enum
{
	RB_LIBRARY_DND_URI_LIST,
	RB_LIBRARY_DND_NODE_ID
} RBLibraryDndType;

G_END_DECLS

#endif /* __RB_LIBRARY_DND_TYPES_H */
