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

#ifndef __gul_toolbar_editor_h
#define __gul_toolbar_editor_h

#include <glib-object.h>
#include <gtk/gtkbutton.h>

#include "gul-toolbar.h"

/* object forward declarations */

typedef struct _GulTbEditor GulTbEditor;
typedef struct _GulTbEditorClass GulTbEditorClass;
typedef struct _GulTbEditorPrivate GulTbEditorPrivate;

/**
 * TbEditor object
 */

#define GUL_TYPE_TB_EDITOR		(gul_tb_editor_get_type())
#define GUL_TB_EDITOR(object)		(G_TYPE_CHECK_INSTANCE_CAST((object), \
					 GUL_TYPE_TB_EDITOR,\
					 GulTbEditor))
#define GUL_TB_EDITOR_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), GUL_TYPE_TB_EDITOR,\
					 GulTbEditorClass))
#define GUL_IS_TB_EDITOR(object)	(G_TYPE_CHECK_INSTANCE_TYPE((object), \
					 GUL_TYPE_TB_EDITOR))
#define GUL_IS_TB_EDITOR_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), GUL_TYPE_TB_EDITOR))
#define GUL_TB_EDITOR_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GUL_TYPE_TB_EDITOR,\
					 GulTbEditorClass))

struct _GulTbEditorClass 
{
	GObjectClass parent_class;
};

/* Remember: fields are public read-only */
struct _GulTbEditor
{
	GObject parent_object;

	GulTbEditorPrivate *priv;
};

/* this class is abstract */

GType			gul_tb_editor_get_type		(void);
GulTbEditor *		gul_tb_editor_new		(void);
void			gul_tb_editor_set_toolbar	(GulTbEditor *tbe, GulToolbar *tb);
GulToolbar *		gul_tb_editor_get_toolbar	(GulTbEditor *tbe);
void			gul_tb_editor_set_available	(GulTbEditor *tbe, GulToolbar *tb);
GulToolbar *		gul_tb_editor_get_available	(GulTbEditor *tbe);
void			gul_tb_editor_set_parent	(GulTbEditor *tbe, GtkWidget *parent);
void			gul_tb_editor_show		(GulTbEditor *tbe);
/* the revert button is hidden initially */
GtkButton *		gul_tb_editor_get_revert_button	(GulTbEditor *tbe);

#endif

