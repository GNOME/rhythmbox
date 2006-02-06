/*
 *  arch-tag: Header for main Rhythmbox shell
 *
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2004 Colin Walters <walters@redhat.com>
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
 *  
 */

#ifndef __RB_SHELL_H
#define __RB_SHELL_H

#include "rb-source.h"
#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_SHELL         (rb_shell_get_type ())
#define RB_SHELL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SHELL, RBShell))
#define RB_SHELL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SHELL, RBShellClass))
#define RB_IS_SHELL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SHELL))
#define RB_IS_SHELL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SHELL))
#define RB_SHELL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SHELL, RBShellClass))

typedef enum
{
	RB_SHELL_ERROR_NO_SUCH_URI,
	RB_SHELL_ERROR_NO_SUCH_PROPERTY,
	RB_SHELL_ERROR_IMMUTABLE_PROPERTY,
	RB_SHELL_ERROR_INVALID_PROPERTY_TYPE
} RBShellError;

#define RB_SHELL_ERROR rb_shell_error_quark ()

GQuark rb_shell_error_quark (void);

typedef struct RBShellPrivate RBShellPrivate;

typedef struct
{
        GObject parent;

	RBShellPrivate *priv;
} RBShell;

typedef struct
{
        GObjectClass parent_class;
} RBShellClass;

GType		rb_shell_get_type	(void);

RBShell *	rb_shell_new		(int argc, char **argv,
					 gboolean no_registration,
					 gboolean no_update,
					 gboolean dry_run,
					 char *rhythmdb);

gboolean        rb_shell_present        (RBShell *shell, guint32 timestamp, GError **error);

gint            rb_shell_guess_type_for_uri (RBShell *shell, const char *uri);

gboolean        rb_shell_add_uri        (RBShell *shell,
					 gint entry_type,
					 const char *uri,
					 const char *title,
					 const char *genre,
					 GError **error);

gboolean        rb_shell_load_uri       (RBShell *shell, const char *uri, gboolean play, GError **error);

GObject *       rb_shell_get_player     (RBShell *shell);

const char *    rb_shell_get_player_path(RBShell *shell);

GObject *	rb_shell_get_playlist_manager (RBShell *shell);

const char *	rb_shell_get_playlist_manager_path (RBShell *shell);

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

gboolean	rb_shell_add_to_queue (RBShell *shell,
				       const gchar *uri,
				       GError **error);

gboolean	rb_shell_remove_from_queue (RBShell *shell,
					    const gchar *uri,
					    GError **error);

void            rb_shell_hidden_notify  (RBShell *shell,
					 guint timeout,
					 const char *primary,
					 GtkWidget *icon,
					 const char *secondary);

void		rb_shell_construct	(RBShell *shell);

void            rb_shell_register_entry_type_for_source (RBShell *shell,
							 RBSource *source,
							 RhythmDBEntryType type);

void rb_shell_append_source (RBShell *shell, RBSource *source, RBSource *parent);

G_END_DECLS

#endif /* __RB_SHELL_H */
