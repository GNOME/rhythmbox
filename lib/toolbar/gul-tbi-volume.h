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

#ifndef __gul_tbi_volume_h
#define __gul_tbi_volume_h

#include "gul-toolbar-item.h"

/* object forward declarations */

typedef struct _GulTbiVolume GulTbiVolume;
typedef struct _GulTbiVolumeClass GulTbiVolumeClass;
typedef struct _GulTbiVolumePrivate GulTbiVolumePrivate;

/**
 * TbiVolume object
 */

#define GUL_TYPE_TBI_VOLUME		(gul_tbi_volume_get_type())
#define GUL_TBI_VOLUME(object)		(G_TYPE_CHECK_INSTANCE_CAST((object), GUL_TYPE_TBI_VOLUME,\
					 GulTbiVolume))
#define GUL_TBI_VOLUME_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), GUL_TYPE_TBI_VOLUME,\
					 GulTbiVolumeClass))
#define GUL_IS_TBI_VOLUME(object)		(G_TYPE_CHECK_INSTANCE_TYPE((object), GUL_TYPE_TBI_VOLUME))
#define GUL_IS_TBI_VOLUME_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), GUL_TYPE_TBI_VOLUME))
#define GUL_TBI_VOLUME_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GUL_TYPE_TBI_VOLUME,\
					 GulTbiVolumeClass))

struct _GulTbiVolumeClass 
{
	GulTbItemClass parent_class;
	
};

/* Remember: fields are public read-only */
struct _GulTbiVolume
{
	GulTbItem parent_object;

	GulTbiVolumePrivate *priv;
};

/* this class is abstract */

GType		gul_tbi_volume_get_type		(void);
GulTbiVolume *	gul_tbi_volume_new		(void);

#endif
