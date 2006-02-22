/*
 *  arch-tag: Header for the abstract base class of all sources
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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

#ifndef __RB_SOURCE_H
#define __RB_SOURCE_H

#include <gtk/gtkhbox.h>
#include <gtk/gtkactiongroup.h>

#include "rb-entry-view.h"
#include "rb-shell-preferences.h"

G_BEGIN_DECLS

typedef enum {
	RB_SOURCE_EOF_ERROR,
	RB_SOURCE_EOF_STOP,
	RB_SOURCE_EOF_RETRY,
	RB_SOURCE_EOF_NEXT,
} RBSourceEOFType;


#define RB_TYPE_SOURCE         (rb_source_get_type ())
#define RB_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SOURCE, RBSource))
#define RB_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SOURCE, RBSourceClass))
#define RB_IS_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SOURCE))
#define RB_IS_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SOURCE))
#define RB_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SOURCE, RBSourceClass))

typedef struct _RBSourceClass	RBSourceClass;
typedef struct _RBSource		RBSource;
typedef struct _RBSourcePrivate	RBSourcePrivate;

typedef gboolean (*RBSourceFeatureFunc) (RBSource *source);
typedef const char * (*RBSourceStringFunc) (RBSource *source);

struct _RBSource
{
	GtkHBox parent;
};

struct _RBSourceClass
{
	GtkHBoxClass parent;
	
	/* signals */
	void (*status_changed)	(RBSource *source);
	void (*filter_changed)	(RBSource *source);
	void (*deleted)		(RBSource *source);
	void (*artistalbum_changed)	(RBSource *source);

	/* methods */
	char *	        (*impl_get_status)	(RBSource *source);

	gboolean	(*impl_can_browse)	(RBSource *source);
	const char *	(*impl_get_browser_key)	(RBSource *source);
	void		(*impl_browser_toggled)	(RBSource *source, gboolean enabled);

	RBEntryView *	(*impl_get_entry_view)	(RBSource *source);
	GList *		(*impl_get_property_views)	(RBSource *source);

	gboolean	(*impl_can_rename)	(RBSource *source);

	gboolean	(*impl_can_search)	(RBSource *source);

	void		(*impl_search)		(RBSource *source, const char *text);
	void		(*impl_reset_filters)	(RBSource *source);
	GtkWidget *	(*impl_get_config_widget)(RBSource *source, RBShellPreferences *prefs);

	gboolean	(*impl_can_cut)		(RBSource *source);
	gboolean	(*impl_can_delete)	(RBSource *source);
	gboolean	(*impl_can_move_to_trash) (RBSource *source);
	gboolean	(*impl_can_copy)	(RBSource *source);
	gboolean	(*impl_can_add_to_queue)(RBSource *source);
	
	GList *		(*impl_cut)		(RBSource *source);
	GList *		(*impl_copy)		(RBSource *source);
	void		(*impl_paste)		(RBSource *source, GList *entries);
	void		(*impl_delete)		(RBSource *source);
	void		(*impl_add_to_queue)	(RBSource *source, RBSource *queue);
	void		(*impl_move_to_trash)	(RBSource *source);

	void		(*impl_song_properties)	(RBSource *source);

	gboolean	(*impl_try_playlist)	(RBSource *source);

	gboolean	(*impl_can_pause)	(RBSource *source);
	RBSourceEOFType	(*impl_handle_eos)	(RBSource *source);
	
	gboolean	(*impl_have_url)	(RBSource *source);
	gboolean	(*impl_receive_drag)	(RBSource *source, GtkSelectionData *data);
	gboolean	(*impl_show_popup)	(RBSource *source);
				   
	void		(*impl_delete_thyself)	(RBSource *source);
	void		(*impl_activate)	(RBSource *source);
	void		(*impl_deactivate)	(RBSource *source);
	gboolean	(*impl_disconnect)	(RBSource *source);
	GList*		(*impl_get_ui_actions)	(RBSource *source);
};

GType		rb_source_get_type		(void);

void		rb_source_notify_filter_changed	(RBSource *source);

void		rb_source_notify_status_changed (RBSource *status);

void		rb_source_update_play_statistics(RBSource *source, RhythmDB *db,
						 RhythmDBEntry *entry);

/* general interface */
void	        rb_source_set_pixbuf		(RBSource *source, GdkPixbuf *pixbuf);
char *	        rb_source_get_status		(RBSource *source);

gboolean	rb_source_can_browse		(RBSource *source);
const char *	rb_source_get_browser_key	(RBSource *source);
void		rb_source_browser_toggled	(RBSource *source, gboolean enabled);

RBEntryView *	rb_source_get_entry_view	(RBSource *source);

GList *		rb_source_get_property_views	(RBSource *source);

gboolean	rb_source_can_rename		(RBSource *source);

gboolean	rb_source_can_search		(RBSource *source);
void		rb_source_search		(RBSource *source,
						 const char *text);

void		rb_source_reset_filters		(RBSource *source);

GtkWidget *	rb_source_get_config_widget	(RBSource *source, RBShellPreferences *prefs);

gboolean	rb_source_can_cut		(RBSource *source);
gboolean	rb_source_can_delete		(RBSource *source);
gboolean	rb_source_can_move_to_trash	(RBSource *source);
gboolean	rb_source_can_copy		(RBSource *source);
gboolean	rb_source_can_add_to_queue	(RBSource *source);

GList *		rb_source_cut			(RBSource *source);
GList *		rb_source_copy			(RBSource *source);
void		rb_source_paste			(RBSource *source, GList *entries);
void		rb_source_delete		(RBSource *source);
void		rb_source_add_to_queue		(RBSource *source, RBSource *queue);
void		rb_source_move_to_trash		(RBSource *source);

void		rb_source_song_properties	(RBSource *source);

gboolean	rb_source_try_playlist		(RBSource *source);

gboolean	rb_source_can_pause		(RBSource *source);
RBSourceEOFType	rb_source_handle_eos		(RBSource *source);

gboolean	rb_source_have_url		(RBSource *source);

gboolean	rb_source_receive_drag		(RBSource *source, GtkSelectionData *data);

gboolean	rb_source_show_popup		(RBSource *source);

void		rb_source_delete_thyself	(RBSource *source);

void		rb_source_activate		(RBSource *source);
void		rb_source_deactivate		(RBSource *source);
gboolean	rb_source_disconnect		(RBSource *source);

GList*		rb_source_get_ui_actions	(RBSource *source);

GList *		rb_source_gather_selected_properties (RBSource *source, RhythmDBPropType prop);

/* Protected methods, should only be used by objects inheriting from RBSource */
void            _rb_source_show_popup           (RBSource *source, 
						 const char *ui_path);
GtkActionGroup *_rb_source_register_action_group (RBSource *source,
						  const char *group_name,
						  GtkActionEntry *actions,
						  int num_actions,
						  gpointer user_data);
void		_rb_source_hide_when_empty	(RBSource *source,
						 RhythmDBQueryModel *model);

G_END_DECLS

#endif /* __RB_SOURCE_H */
