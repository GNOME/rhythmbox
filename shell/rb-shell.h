/*
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2004 Colin Walters <walters@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#ifndef __RB_SHELL_H
#define __RB_SHELL_H

#include <sources/rb-source.h>
#include <rhythmdb/rhythmdb.h>
#include <widgets/rb-song-info.h>

G_BEGIN_DECLS

#define RB_TYPE_SHELL         (rb_shell_get_type ())
#define RB_SHELL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SHELL, RBShell))
#define RB_SHELL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SHELL, RBShellClass))
#define RB_IS_SHELL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SHELL))
#define RB_IS_SHELL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SHELL))
#define RB_SHELL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SHELL, RBShellClass))

typedef enum
{
	RB_SHELL_ACTIVATION_SELECT = 0,
	RB_SHELL_ACTIVATION_PLAY = 1,
	RB_SHELL_ACTIVATION_ALWAYS_PLAY = 2
} RBShellActivationType;

GType rb_shell_activation_type_get_type (void);
#define RB_TYPE_SHELL_ACTIVATION_TYPE (rb_shell_activation_type_get_type ())

typedef enum
{
	RB_SHELL_ERROR_NO_SUCH_URI,
	RB_SHELL_ERROR_NO_SUCH_PROPERTY,
	RB_SHELL_ERROR_IMMUTABLE_PROPERTY,
	RB_SHELL_ERROR_INVALID_PROPERTY_TYPE,
	RB_SHELL_ERROR_NO_SOURCE_FOR_URI
} RBShellError;

#define RB_SHELL_ERROR rb_shell_error_quark ()

GQuark rb_shell_error_quark (void);
GType rb_shell_error_get_type (void);
#define RB_TYPE_SHELL_ERROR (rb_shell_error_get_type ())

typedef enum
{
	RB_SHELL_UI_LOCATION_SIDEBAR,
	RB_SHELL_UI_LOCATION_RIGHT_SIDEBAR,
	RB_SHELL_UI_LOCATION_MAIN_TOP,
	RB_SHELL_UI_LOCATION_MAIN_BOTTOM
} RBShellUILocation;

GType rb_shell_ui_location_get_type (void);
#define RB_TYPE_SHELL_UI_LOCATION	(rb_shell_ui_location_get_type())

typedef struct _RBShell RBShell;
typedef struct _RBShellClass RBShellClass;
typedef struct _RBShellPrivate RBShellPrivate;

struct _RBShell
{
        GtkApplication parent;

	RBShellPrivate *priv;
};

struct _RBShellClass
{
        GtkApplicationClass parent_class;

	/* signals */
	gboolean (*visibility_changing)	(RBShell *shell, gboolean initial, gboolean visible);
	void	 (*visibility_changed)	(RBShell *shell, gboolean visible);
	void	 (*create_song_info)	(RBShell *shell, RBSongInfo *song_info, gboolean multi);
	void	 (*removable_media_scan_finished) (RBShell *shell);
	void     (*database_load_complete) (RBShell *shell);
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC(RBShell, g_object_unref)

GType		rb_shell_get_type	(void);

gboolean        rb_shell_present        (RBShell *shell, guint32 timestamp, GError **error);

RBSource *	rb_shell_guess_source_for_uri (RBShell *shell, const char *uri);

gboolean        rb_shell_add_uri        (RBShell *shell,
					 const char *uri,
					 const char *title,
					 const char *genre,
					 GError **error);

gboolean        rb_shell_load_uri       (RBShell *shell, const char *uri, gboolean play, GError **error);

void            rb_shell_toggle_visibility (RBShell *shell);

gboolean        rb_shell_get_song_properties (RBShell *shell,
					      const char *uri,
					      GHashTable **properties,
					      GError **error);

gboolean        rb_shell_set_song_property (RBShell *shell,
					    const char *uri,
					    const char *propname,
					    const GValue *value,
					    GError **error);

gboolean	rb_shell_quit (RBShell *shell,
			       GError **error);

gboolean	rb_shell_activate_source_by_uri (RBShell *shell,
						 const char *source_uri,
						 guint play,
						 GError **error);
gboolean	rb_shell_activate_source (RBShell *shell,
					  RBSource *source,
					  guint play,
					  GError **error);

void            rb_shell_notify_custom  (RBShell *shell,
					 guint timeout,
					 const char *primary,
					 const char *secondary,
					 const char *image_uri,
					 gboolean requested);
gboolean	rb_shell_do_notify (RBShell *shell,
				    gboolean requested,
				    GError **error);

void            rb_shell_register_entry_type_for_source (RBShell *shell,
							 RBSource *source,
							 RhythmDBEntryType *type);
RBSource * rb_shell_get_source_by_entry_type (RBShell *shell,
					      RhythmDBEntryType *type);

void 		rb_shell_append_display_page (RBShell *shell, RBDisplayPage *page, RBDisplayPage *parent);

void 		rb_shell_add_widget (RBShell *shell, GtkWidget *widget, RBShellUILocation location, gboolean expand, gboolean fill);
void 		rb_shell_remove_widget (RBShell *shell, GtkWidget *widget, RBShellUILocation location);

G_END_DECLS

#endif /* __RB_SHELL_H */
