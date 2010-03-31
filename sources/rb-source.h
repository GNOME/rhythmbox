/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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

#ifndef __RB_SOURCE_H
#define __RB_SOURCE_H

#include <gtk/gtk.h>

#include <sources/rb-source-group.h>
#include <sources/rb-source-search.h>
#include <widgets/rb-entry-view.h>
#include <shell/rb-shell-preferences.h>
#include <rhythmdb/rhythmdb-import-job.h>

G_BEGIN_DECLS

typedef enum {
	RB_SOURCE_EOF_ERROR,
	RB_SOURCE_EOF_STOP,
	RB_SOURCE_EOF_RETRY,
	RB_SOURCE_EOF_NEXT,
} RBSourceEOFType;

typedef enum {
	RB_SOURCE_SEARCH_NONE,
	RB_SOURCE_SEARCH_INCREMENTAL,
	RB_SOURCE_SEARCH_EXPLICIT,
} RBSourceSearchType;

typedef struct _RBSource	RBSource;
typedef struct _RBSourceClass	RBSourceClass;
typedef struct _RBSourcePrivate	RBSourcePrivate;

typedef void (*RBSourceActionCallback) (GtkAction *action, RBSource *source);

GType rb_source_eof_type_get_type (void);
#define RB_TYPE_SOURCE_EOF_TYPE	(rb_source_eof_type_get_type())

GType rb_source_search_type_get_type (void);
#define RB_TYPE_SOURCE_SEARCH_TYPE (rb_source_search_type_get_type())

#define RB_TYPE_SOURCE         (rb_source_get_type ())
#define RB_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SOURCE, RBSource))
#define RB_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SOURCE, RBSourceClass))
#define RB_IS_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SOURCE))
#define RB_IS_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SOURCE))
#define RB_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SOURCE, RBSourceClass))

#define RB_SOURCE_ICON_SIZE	GTK_ICON_SIZE_LARGE_TOOLBAR

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
	void		(*impl_get_status)	(RBSource *source, char **text, char **progress_text, float *progress);

	gboolean	(*impl_can_browse)	(RBSource *source);
	char *		(*impl_get_browser_key)	(RBSource *source);
	void		(*impl_browser_toggled)	(RBSource *source, gboolean enabled);

	RBEntryView *	(*impl_get_entry_view)	(RBSource *source);
	GList *		(*impl_get_property_views)	(RBSource *source);

	gboolean	(*impl_can_rename)	(RBSource *source);

	void		(*impl_search)		(RBSource *source, RBSourceSearch *search, const char *cur_text, const char *new_text);
	void		(*impl_reset_filters)	(RBSource *source);
	GtkWidget *	(*impl_get_config_widget)(RBSource *source, RBShellPreferences *prefs);

	gboolean	(*impl_can_cut)		(RBSource *source);
	gboolean	(*impl_can_delete)	(RBSource *source);
	gboolean	(*impl_can_move_to_trash) (RBSource *source);
	gboolean	(*impl_can_copy)	(RBSource *source);
	gboolean	(*impl_can_paste)	(RBSource *source);
	gboolean	(*impl_can_add_to_queue)(RBSource *source);

	GList *		(*impl_cut)		(RBSource *source);
	GList *		(*impl_copy)		(RBSource *source);
	void		(*impl_paste)		(RBSource *source, GList *entries);
	void		(*impl_delete)		(RBSource *source);
	void		(*impl_add_to_queue)	(RBSource *source, RBSource *queue);
	void		(*impl_move_to_trash)	(RBSource *source);

	void		(*impl_song_properties)	(RBSource *source);

	gboolean	(*impl_try_playlist)	(RBSource *source);
	guint		(*impl_want_uri)	(RBSource *source, const char *uri);
	gboolean	(*impl_add_uri)		(RBSource *source, const char *uri, const char *title, const char *genre);
	gboolean	(*impl_uri_is_source)	(RBSource *source, const char *uri);

	gboolean	(*impl_can_pause)	(RBSource *source);
	RBSourceEOFType	(*impl_handle_eos)	(RBSource *source);

	gboolean	(*impl_have_url)	(RBSource *source);
	gboolean	(*impl_receive_drag)	(RBSource *source, GtkSelectionData *data);
	gboolean	(*impl_show_popup)	(RBSource *source);

	void		(*impl_delete_thyself)	(RBSource *source);
	void		(*impl_activate)	(RBSource *source);
	void		(*impl_deactivate)	(RBSource *source);
	GList *		(*impl_get_ui_actions)	(RBSource *source);
	GList *		(*impl_get_search_actions) (RBSource *source);
	char *		(*impl_get_delete_action) (RBSource *source);
};

GType		rb_source_get_type		(void);

void		rb_source_notify_filter_changed	(RBSource *source);

void		rb_source_notify_status_changed (RBSource *source);

void		rb_source_update_play_statistics(RBSource *source, RhythmDB *db,
						 RhythmDBEntry *entry);

/* general interface */
void	        rb_source_set_pixbuf		(RBSource *source, GdkPixbuf *pixbuf);
void	        rb_source_get_status		(RBSource *source, char **text, char **progress_text, float *progress);

gboolean	rb_source_can_browse		(RBSource *source);
char *		rb_source_get_browser_key	(RBSource *source);
void		rb_source_browser_toggled	(RBSource *source, gboolean enabled);

RBEntryView *	rb_source_get_entry_view	(RBSource *source);

GList *		rb_source_get_property_views	(RBSource *source);

gboolean	rb_source_can_rename		(RBSource *source);

void		rb_source_search		(RBSource *source,
						 RBSourceSearch *search,
						 const char *cur_text,
						 const char *new_text);

void		rb_source_reset_filters		(RBSource *source);

GtkWidget *	rb_source_get_config_widget	(RBSource *source, RBShellPreferences *prefs);

gboolean	rb_source_can_cut		(RBSource *source);
gboolean	rb_source_can_delete		(RBSource *source);
gboolean	rb_source_can_move_to_trash	(RBSource *source);
gboolean	rb_source_can_copy		(RBSource *source);
gboolean	rb_source_can_paste		(RBSource *source);
gboolean	rb_source_can_add_to_queue	(RBSource *source);
gboolean	rb_source_can_show_properties	(RBSource *source);

GList *		rb_source_cut			(RBSource *source);
GList *		rb_source_copy			(RBSource *source);
void		rb_source_paste			(RBSource *source, GList *entries);
void		rb_source_delete		(RBSource *source);
void		rb_source_add_to_queue		(RBSource *source, RBSource *queue);
void		rb_source_move_to_trash		(RBSource *source);

void		rb_source_song_properties	(RBSource *source);

gboolean	rb_source_try_playlist		(RBSource *source);
guint		rb_source_want_uri		(RBSource *source, const char *uri);
gboolean	rb_source_uri_is_source		(RBSource *source, const char *uri);
gboolean	rb_source_add_uri		(RBSource *source, const char *uri, const char *title, const char *genre);

gboolean	rb_source_can_pause		(RBSource *source);
RBSourceEOFType	rb_source_handle_eos		(RBSource *source);

gboolean	rb_source_receive_drag		(RBSource *source, GtkSelectionData *data);

gboolean	rb_source_show_popup		(RBSource *source);

void		rb_source_delete_thyself	(RBSource *source);

void		rb_source_activate		(RBSource *source);
void		rb_source_deactivate		(RBSource *source);

GList *		rb_source_get_ui_actions	(RBSource *source);
GList *		rb_source_get_search_actions	(RBSource *source);
char *		rb_source_get_delete_action	(RBSource *source);

GList *		rb_source_gather_selected_properties (RBSource *source, RhythmDBPropType prop);

void            rb_source_set_hidden_when_empty (RBSource *source,
                                                 gboolean  hidden);

/* Protected methods, should only be used by objects inheriting from RBSource */
void            _rb_source_show_popup           (RBSource *source,
						 const char *ui_path);
GtkActionGroup *_rb_source_register_action_group (RBSource *source,
						  const char *group_name,
						  GtkActionEntry *actions,
						  int num_actions,
						  gpointer user_data);
void		_rb_action_group_add_source_actions (GtkActionGroup *group,
						     GObject *shell,
						     GtkActionEntry *actions,
						     int num_actions);

gboolean	_rb_source_check_entry_type	(RBSource *source,
						 RhythmDBEntry *entry);

void		_rb_source_set_import_status	(RBSource *source,
						 RhythmDBImportJob *job,
						 char **progress_text,
						 float *progress);

G_END_DECLS

#endif /* __RB_SOURCE_H */
