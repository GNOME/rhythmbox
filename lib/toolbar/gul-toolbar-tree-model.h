/* 
 *  Copyright (C) 2002  Ricardo Fernándezs Pascual <ric@users.sourceforge.net>
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

#ifndef __GUL_TOOLBAR_TREE_MODEL_H__
#define __GUL_TOOLBAR_TREE_MODEL_H__

#include <gtk/gtktreemodel.h>
#include "gul-toolbar.h"

/* object forward declarations */

typedef struct _GulTbTreeModel GulTbTreeModel;
typedef struct _GulTbTreeModelClass GulTbTreeModelClass;
typedef struct _GulTbTreeModelPrivate GulTbTreeModelPrivate;

typedef enum {
	GUL_TB_TREE_MODEL_COL_ICON,
	GUL_TB_TREE_MODEL_COL_NAME,
	GUL_TB_TREE_MODEL_NUM_COLUMS
} GulTbTreeModelColumn;

/**
 * Tb tree model object
 */

#define GUL_TYPE_TB_TREE_MODEL		(gul_tb_tree_model_get_type())
#define GUL_TB_TREE_MODEL(object)	(G_TYPE_CHECK_INSTANCE_CAST((object), GUL_TYPE_TB_TREE_MODEL,\
					 GulTbTreeModel))
#define GUL_TB_TREE_MODEL_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), GUL_TYPE_TB_TREE_MODEL,\
					 GulTbTreeModelClass))
#define GUL_IS_TB_TREE_MODEL(object)	(G_TYPE_CHECK_INSTANCE_TYPE((object), GUL_TYPE_TB_TREE_MODEL))
#define GUL_IS_TB_TREE_MODEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GUL_TYPE_TB_TREE_MODEL))
#define GUL_TB_TREE_MODEL_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GUL_TYPE_TB_TREE_MODEL,\
					  GulTbTreeModelClass))

struct _GulTbTreeModel
{
	GObject parent;
	
	GulTbTreeModelPrivate *priv;
	gint stamp;
};

struct _GulTbTreeModelClass
{
	GObjectClass parent_class;
};


GtkType			gul_tb_tree_model_get_type		(void);
GulTbTreeModel *	gul_tb_tree_model_new			(void);
void			gul_tb_tree_model_set_toolbar		(GulTbTreeModel *tm, GulToolbar *tb);
GulTbItem *		gul_tb_tree_model_item_from_iter	(GulTbTreeModel *tm, GtkTreeIter *iter);

#endif 
