/* 
 *  arch-tag: Header for object implementing main playback logic
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <gtk/gtkhbox.h>
#include <gtk/gtkuimanager.h>
#include <gtk/gtkactiongroup.h>

#include "rb-source.h"
#include "rhythmdb.h"

#ifndef __RB_SHELL_PLAYER_H
#define __RB_SHELL_PLAYER_H

G_BEGIN_DECLS

#define RB_TYPE_SHELL_PLAYER         (rb_shell_player_get_type ())
#define RB_SHELL_PLAYER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SHELL_PLAYER, RBShellPlayer))
#define RB_SHELL_PLAYER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SHELL_PLAYER, RBShellPlayerClass))
#define RB_IS_SHELL_PLAYER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SHELL_PLAYER))
#define RB_IS_SHELL_PLAYER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SHELL_PLAYER))
#define RB_SHELL_PLAYER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SHELL_PLAYER, RBShellPlayerClass))

typedef enum
{
	RB_SHELL_PLAYER_ERROR_PLAYLIST_PARSE_ERROR,
	RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST,
	RB_SHELL_PLAYER_ERROR_NOT_PLAYING,
        RB_SHELL_PLAYER_ERROR_NOT_SEEKABLE,
} RBShellPlayerError;

GType rb_shell_player_error_get_type (void);
#define RB_TYPE_SHELL_PLAYER_ERROR	(rb_shell_player_error_get_type())

#define RB_SHELL_PLAYER_ERROR rb_shell_player_error_quark ()

GQuark rb_shell_player_error_quark (void);

typedef struct RBShellPlayerPrivate RBShellPlayerPrivate;

typedef struct
{
	GtkHBox parent;

	RBShellPlayerPrivate *priv;
} RBShellPlayer;

typedef struct
{
	GtkHBoxClass parent_class;

	void (*window_title_changed) (RBShellPlayer *player, const char *window_title);
	void (*elapsed_changed) (RBShellPlayer *player, guint elapsed);
	void (*playing_changed) (RBShellPlayer *player, gboolean playing);
	void (*playing_source_changed) (RBShellPlayer *player, RBSource *source);
	void (*playing_uri_changed) (RBShellPlayer *player, const char *uri);
	void (*playing_song_changed) (RBShellPlayer *player, RhythmDBEntry *entry);
} RBShellPlayerClass;

GType			rb_shell_player_get_type   (void);

RBShellPlayer *		rb_shell_player_new		(RhythmDB *db,
							 GtkUIManager *mgr,
							 GtkActionGroup *actiongroup);

void			rb_shell_player_set_selected_source	(RBShellPlayer *shell_player,
								 RBSource *player);
void			rb_shell_player_set_playing_source (RBShellPlayer *player,
							    RBSource *source);

RBSource *		rb_shell_player_get_playing_source (RBShellPlayer *shell_player);
RBSource *		rb_shell_player_get_active_source (RBShellPlayer *shell_player);

void			rb_shell_player_jump_to_current (RBShellPlayer *player);

void			rb_shell_player_play_entry	(RBShellPlayer *player,
							 RhythmDBEntry *entry);
gboolean		rb_shell_player_play		(RBShellPlayer *player, GError **error);
gboolean		rb_shell_player_pause		(RBShellPlayer *player, GError **error);
gboolean                rb_shell_player_playpause	(RBShellPlayer *player, gboolean ignore_stop, GError **error);
void			rb_shell_player_stop		(RBShellPlayer *player);
gboolean                rb_shell_player_do_previous	(RBShellPlayer *player, GError **error);
gboolean		rb_shell_player_do_next		(RBShellPlayer *player, GError **error);

char * 			rb_shell_player_get_playing_time_string	(RBShellPlayer *player);
gboolean		rb_shell_player_get_playing_time(RBShellPlayer *player,
                                                         guint *time,
                                                         GError **error);
gboolean		rb_shell_player_set_playing_time(RBShellPlayer *player,
                                                         guint time,
                                                         GError **error);
void			rb_shell_player_seek		(RBShellPlayer *player, long offset);
long			rb_shell_player_get_playing_song_duration (RBShellPlayer *player);

gboolean		rb_shell_player_get_playing	(RBShellPlayer *shell_player,
							 gboolean *playing,
							 GError **error);

gboolean		rb_shell_player_get_playing_path(RBShellPlayer *shell_player,
							 const gchar **path,
							 GError **error);

void			rb_shell_player_set_playback_state(RBShellPlayer *player,
							   gboolean shuffle, gboolean repeat);

gboolean                rb_shell_player_get_playback_state(RBShellPlayer *player,
							   gboolean *shuffle,
							   gboolean *repeat);

RhythmDBEntry *         rb_shell_player_get_playing_entry (RBShellPlayer *player);

void			rb_shell_player_toggle_mute	(RBShellPlayer *player);

gboolean		rb_shell_player_set_volume	(RBShellPlayer *player,
							 gdouble volume,
							 GError **error);

gboolean		rb_shell_player_get_volume	(RBShellPlayer *player,
							 gdouble *volume,
							 GError **error);

gboolean		rb_shell_player_set_volume_relative (RBShellPlayer *player,
							 gdouble volume,
							 GError **error);

gboolean		rb_shell_player_set_mute	(RBShellPlayer *player,
							 gboolean mute,
							 GError **error);

gboolean		rb_shell_player_get_mute	(RBShellPlayer *player,
							 gboolean *mute,
							 GError **error);


G_END_DECLS

#endif /* __RB_SHELL_PLAYER_H */
