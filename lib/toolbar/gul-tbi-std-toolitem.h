/*
 *  Copyright (C) 2002  Ricardo Fernández Pascual
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __gul_tbi_std_toolitem_h
#define __gul_tbi_std_toolitem_h

#include "gul-toolbar-item.h"

/* object forward declarations */

typedef struct _GulTbiStdToolitem GulTbiStdToolitem;
typedef struct _GulTbiStdToolitemClass GulTbiStdToolitemClass;
typedef struct _GulTbiStdToolitemPrivate GulTbiStdToolitemPrivate;

/**
 * TbiStdToolitem object
 */

#define GUL_TYPE_TBI_STD_TOOLITEM		(gul_tbi_std_toolitem_get_type())
#define GUL_TBI_STD_TOOLITEM(object)		(G_TYPE_CHECK_INSTANCE_CAST((object), \
						 GUL_TYPE_TBI_STD_TOOLITEM,\
						 GulTbiStdToolitem))
#define GUL_TBI_STD_TOOLITEM_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), \
						 GUL_TYPE_TBI_STD_TOOLITEM,\
						 GulTbiStdToolitemClass))
#define GUL_IS_TBI_STD_TOOLITEM(object)		(G_TYPE_CHECK_INSTANCE_TYPE((object), \
						 GUL_TYPE_TBI_STD_TOOLITEM))
#define GUL_IS_TBI_STD_TOOLITEM_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), \
						 GUL_TYPE_TBI_STD_TOOLITEM))
#define GUL_TBI_STD_TOOLITEM_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), \
						 GUL_TYPE_TBI_STD_TOOLITEM,\
						 GulTbiStdToolitemClass))
typedef enum
{
	GUL_TBI_STD_TOOLITEM_PREVIOUS,
	GUL_TBI_STD_TOOLITEM_PLAY,
	GUL_TBI_STD_TOOLITEM_NEXT,
	GUL_TBI_STD_TOOLITEM_SHUFFLE,
	GUL_TBI_STD_TOOLITEM_RESTART,
	GUL_TBI_STD_TOOLITEM_REPEAT,
	GUL_TBI_STD_TOOLITEM_CUT,
	GUL_TBI_STD_TOOLITEM_COPY,
	GUL_TBI_STD_TOOLITEM_PASTE,
	GUL_TBI_STD_TOOLITEM_PROPERTIES,
	GUL_TBI_STD_TOOLITEM_ADD_TO_LIBRARY
} GulTbiStdToolitemItem;


struct _GulTbiStdToolitemClass 
{
	GulTbItemClass parent_class;
	
};

/* Remember: fields are public read-only */
struct _GulTbiStdToolitem
{
	GulTbItem parent_object;

	GulTbiStdToolitemPrivate *priv;
};

/* this class is abstract */

GType			gul_tbi_std_toolitem_get_type		(void);
GulTbiStdToolitem *	gul_tbi_std_toolitem_new		(void);
void			gul_tbi_std_toolitem_set_item 		(GulTbiStdToolitem *sit,
								 GulTbiStdToolitemItem it);

#endif

