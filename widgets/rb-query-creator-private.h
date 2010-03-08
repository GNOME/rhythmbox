/*
 *  Copyright (C) 2003, 2004 Colin Walters <walters@gnome.org>
 *  Copyright (C) 2005 James Livingston <walters@gnome.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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

#include <gtk/gtk.h>
#include "rhythmdb.h"

#ifndef __RB_QUERY_CREATOR_PRIVATE_H
#define __RB_QUERY_CREATOR_PRIVATE_H

typedef struct
{
	const char *name;
	gboolean strict;
	RhythmDBQueryType val;
} RBQueryCreatorCriteriaOption;

typedef struct
{
	const char *name;
	const char *sort_key;
	const char *sort_descending_name;
} RBQueryCreatorSortOption;

typedef GtkWidget*	(*CriteriaCreateWidget)		(gboolean *constrain);
typedef void		(*CriteriaSetWidgetData)	(GtkWidget *widget, GValue *val);
typedef void		(*CriteriaGetWidgetData)	(GtkWidget *widget, GValue *val);

typedef struct
{
	int num_criteria_options;
	const RBQueryCreatorCriteriaOption *criteria_options;

	CriteriaCreateWidget	criteria_create_widget;
	CriteriaSetWidgetData	criteria_set_widget_data;
	CriteriaGetWidgetData	criteria_get_widget_data;
} RBQueryCreatorPropertyType;

typedef struct
{
	const char *name;
	RhythmDBPropType strict_val;
	RhythmDBPropType fuzzy_val;
	const RBQueryCreatorPropertyType *property_type;
} RBQueryCreatorPropertyOption;

extern const RBQueryCreatorPropertyOption property_options[];
extern const int num_property_options;
extern const RBQueryCreatorSortOption sort_options[];
extern const int num_sort_options;
extern const int DEFAULT_SORTING_COLUMN;
extern const gint DEFAULT_SORTING_ORDER;

GtkWidget * get_box_widget_at_pos (GtkBox *box, guint pos);

#endif /* __RB_QUERY_CREATOR_H */
