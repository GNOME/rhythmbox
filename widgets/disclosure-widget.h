/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  arch-tag: Header file for the "disclosure" widget
 *
 *  Authors: Iain Holmes <iain@ximian.com>
 *
 *  Copyright 2002 Iain Holmes
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
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef __DISCLOSURE_WIDGET_H__
#define __DISCLOSURE_WIDGET_H__

#include <gtk/gtkcheckbutton.h>

G_BEGIN_DECLS

#define CDDB_DISCLOSURE_TYPE (cddb_disclosure_get_type ())
#define CDDB_DISCLOSURE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), CDDB_DISCLOSURE_TYPE, CDDBDisclosure))
#define CDDB_DISCLOSURE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), CDDB_DISCLOSURE_TYPE, CDDBDisclosureClass))
#define IS_CDDB_DISCLOSURE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CDDB_DISCLOSURE_TYPE))
#define IS_CDDB_DISCLOSURE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CDDB_DISCLOSURE_TYPE))

typedef struct _CDDBDisclosure CDDBDisclosure;
typedef struct _CDDBDisclosureClass CDDBDisclosureClass;
typedef struct _CDDBDisclosurePrivate CDDBDisclosurePrivate;

struct _CDDBDisclosure {
	GtkCheckButton parent;

	CDDBDisclosurePrivate *priv;
};

struct _CDDBDisclosureClass {
	GtkCheckButtonClass parent_class;

	void	(*changed) (CDDBDisclosure *cddb, gboolean disclosed);
};

GType 		 cddb_disclosure_get_type (void);

GtkWidget	*cddb_disclosure_new (const char *shown,
				      const char *hidden);

void	   	 cddb_disclosure_set_container	(CDDBDisclosure *cddb,
						 GtkWidget *widget);

void	   	 cddb_disclosure_set_labels (CDDBDisclosure *cddb,
					     const char *label_when_shown,
					     const char *label_when_hidden);

GtkWidget 	*cddb_disclosure_new_from_glade	(gchar *widget_name,
						 gchar *string1, gchar *string2,
						 gint int1, gint int2);

G_END_DECLS

#endif
