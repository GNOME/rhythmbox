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
#include <bonobo/bonobo-ui-component.h>

#include "rb-entry-view.h"

G_BEGIN_DECLS

typedef enum {
	RB_SOURCE_EOF_ERROR,
	RB_SOURCE_EOF_NEXT,
} RBSourceEOFType;


#define RB_TYPE_SOURCE         (rb_source_get_type ())
#define RB_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SOURCE, RBSource))
#define RB_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SOURCE, RBSourceClass))
#define RB_IS_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SOURCE))
#define RB_IS_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SOURCE))
#define RB_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SOURCE, RBSourceClass))

typedef struct RBSourcePrivate RBSourcePrivate;

typedef struct
{
	GtkHBox parent;

	RBSourcePrivate *priv;
} RBSource;

typedef struct
{
	GtkHBoxClass parent;
	
	/* signals */
	void (*status_changed)	(RBSource *source);
	void (*filter_changed)	(RBSource *source);
	void (*deleted)		(RBSource *source);

	/* methods */
	const char *	(*impl_get_status)	(RBSource *source);

	const char *	(*impl_get_browser_key)	(RBSource *source);

	RBEntryView *	(*impl_get_entry_view)	(RBSource *source);
	GList *		(*impl_get_extra_views)	(RBSource *source);

	GdkPixbuf *	(*impl_get_pixbuf)	(RBSource *source);
	gboolean	(*impl_can_rename)	(RBSource *source);

	gboolean	(*impl_can_search)	(RBSource *source);

	void		(*impl_search)		(RBSource *source, const char *text);
	void		(*impl_reset_filters)	(RBSource *source);
	GtkWidget *	(*impl_get_config_widget)(RBSource *source);

	gboolean	(*impl_can_cut)		(RBSource *source);
	gboolean	(*impl_can_delete)	(RBSource *source);
	gboolean	(*impl_can_copy)	(RBSource *source);
	
	GList *		(*impl_cut)		(RBSource *source);
	GList *		(*impl_copy)		(RBSource *source);
	void		(*impl_paste)		(RBSource *source, GList *entries);
	void		(*impl_delete)		(RBSource *source);

	void		(*impl_song_properties)	(RBSource *source);

	gboolean	(*impl_can_pause)	(RBSource *source);
	RBSourceEOFType	(*impl_handle_eos)	(RBSource *source);
	
	gboolean	(*impl_have_artist_album)(RBSource *source);
	const char *	(*impl_get_artist)	(RBSource *source);
	const char *	(*impl_get_album)	(RBSource *source);
	gboolean	(*impl_have_url)	(RBSource *source);
	void		(*impl_buffering_done)	(RBSource *source);

	gboolean	(*impl_receive_drag)	(RBSource *source, GtkSelectionData *data);
	gboolean	(*impl_show_popup)	(RBSource *source);
				   
	void		(*impl_delete_thyself)	(RBSource *source);
} RBSourceClass;

typedef gboolean (*RBSourceFeatureFunc) (RBSource *source);

GType		rb_source_get_type		(void);

void		rb_source_notify_filter_changed	(RBSource *source);

void		rb_source_notify_status_changed (RBSource *status);

void		rb_source_update_play_statistics(RBSource *source, RhythmDB *db,
						 RhythmDBEntry *entry);

/* general interface */
const char *	rb_source_get_status		(RBSource *source);

const char *	rb_source_get_browser_key	(RBSource *source);

RBEntryView *	rb_source_get_entry_view	(RBSource *source);

GList *		rb_source_get_extra_views	(RBSource *source);

GdkPixbuf *	rb_source_get_pixbuf		(RBSource *source);
gboolean	rb_source_can_rename		(RBSource *source);

gboolean	rb_source_can_search		(RBSource *source);

void		rb_source_search		(RBSource *source,
						 const char *text);

void		rb_source_reset_filters		(RBSource *source);

GtkWidget *	rb_source_get_config_widget	(RBSource *source);

gboolean	rb_source_can_cut		(RBSource *source);
gboolean	rb_source_can_delete		(RBSource *source);
gboolean	rb_source_can_copy		(RBSource *source);

GList *		rb_source_cut			(RBSource *source);
GList *		rb_source_copy			(RBSource *source);
void		rb_source_paste			(RBSource *source, GList *entries);
void		rb_source_delete		(RBSource *source);

void		rb_source_song_properties	(RBSource *source);

gboolean	rb_source_can_pause		(RBSource *source);
RBSourceEOFType	rb_source_handle_eos		(RBSource *source);

gboolean	rb_source_have_artist_album	(RBSource *source);
const char *	rb_source_get_artist		(RBSource *source);
const char *	rb_source_get_album		(RBSource *source);
gboolean	rb_source_have_url		(RBSource *source);
void		rb_source_buffering_done	(RBSource *source);

gboolean	rb_source_receive_drag		(RBSource *source, GtkSelectionData *data);

gboolean	rb_source_show_popup		(RBSource *source);

void		rb_source_delete_thyself	(RBSource *source);

G_END_DECLS

#endif /* __RB_SOURCE_H */
