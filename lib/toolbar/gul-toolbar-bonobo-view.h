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

#ifndef __gul_toolbar_bonobo_view_h
#define __gul_toolbar_bonobo_view_h

#include <glib-object.h>

#include <bonobo/bonobo-ui-component.h>
#include "gul-toolbar.h"

/* object forward declarations */

typedef struct _GulTbBonoboView GulTbBonoboView;
typedef struct _GulTbBonoboViewClass GulTbBonoboViewClass;
typedef struct _GulTbBonoboViewPrivate GulTbBonoboViewPrivate;

/**
 * TbBonoboView object
 */

#define GUL_TYPE_TB_BONOBO_VIEW			(gul_tb_bonobo_view_get_type())
#define GUL_TB_BONOBO_VIEW(object)		(G_TYPE_CHECK_INSTANCE_CAST((object), \
						 GUL_TYPE_TB_BONOBO_VIEW,\
						 GulTbBonoboView))
#define GUL_TB_BONOBO_VIEW_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), GUL_TYPE_TB_BONOBO_VIEW,\
					 	 GulTbBonoboViewClass))
#define GUL_IS_TB_BONOBO_VIEW(object)		(G_TYPE_CHECK_INSTANCE_TYPE((object), \
						 GUL_TYPE_TB_BONOBO_VIEW))
#define GUL_IS_TB_BONOBO_VIEW_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), GUL_TYPE_TB_BONOBO_VIEW))
#define GUL_TB_BONOBO_VIEW_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GUL_TYPE_TB_BONOBO_VIEW,\
						 GulTbBonoboViewClass))

struct _GulTbBonoboViewClass 
{
	GObjectClass parent_class;
	
};

/* Remember: fields are public read-only */
struct _GulTbBonoboView
{
	GObject parent_object;

	GulTbBonoboViewPrivate *priv;
};

/* this class is abstract */

GType			gul_tb_bonobo_view_get_type		(void);
GulTbBonoboView *	gul_tb_bonobo_view_new			(void);
void			gul_tb_bonobo_view_set_toolbar		(GulTbBonoboView *tbv, GulToolbar *tb);
void			gul_tb_bonobo_view_set_path		(GulTbBonoboView *tbv, 
								 BonoboUIComponent *ui,
								 const gchar *path);

#endif

