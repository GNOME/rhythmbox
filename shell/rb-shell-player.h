/* 
 *  arch-tag: Header for object implementing main playback logic
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#include <gtk/gtkhbox.h>
#include <bonobo/bonobo-arg.h>
#include <bonobo/bonobo-ui-component.h>
#include <monkey-media.h>

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
} RBShellPlayerError;

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
	void (*duration_changed) (RBShellPlayer *player, const char *duration);
} RBShellPlayerClass;

GType			rb_shell_player_get_type   (void);

RBShellPlayer *		rb_shell_player_new		(BonoboUIComponent *component,
							 BonoboUIComponent *tray_component);

void			rb_shell_player_set_selected_source	(RBShellPlayer *shell_player,
								 RBSource *player);
void			rb_shell_player_set_playing_source (RBShellPlayer *player,
							    RBSource *source);

RBSource *		rb_shell_player_get_playing_source (RBShellPlayer *shell_player);

void			rb_shell_player_jump_to_current (RBShellPlayer *player);

void			rb_shell_player_play_entry	(RBShellPlayer *player,
							 RhythmDBEntry *entry);
void			rb_shell_player_playpause	(RBShellPlayer *player);
void			rb_shell_player_stop		(RBShellPlayer *player);
void			rb_shell_player_do_previous	(RBShellPlayer *player);
void			rb_shell_player_do_next		(RBShellPlayer *player);

long			rb_shell_player_get_playing_time(RBShellPlayer *player);
void			rb_shell_player_set_playing_time(RBShellPlayer *player, long time);
long			rb_shell_player_get_playing_song_duration (RBShellPlayer *player);

MonkeyMediaPlayer *	rb_shell_player_get_mm_player	(RBShellPlayer *shell_player);

gboolean		rb_shell_player_get_playing	(RBShellPlayer *shell_player);

const char *		rb_shell_player_get_playing_path(RBShellPlayer *shell_player);

void			rb_shell_player_sync_buttons	(RBShellPlayer *player);

void                    rb_shell_player_set_repeat      (RBShellPlayer *player,
							 gboolean new_val);
void                    rb_shell_player_set_shuffle     (RBShellPlayer *player,
							 gboolean new_val);

#ifdef HAVE_ACME
gboolean		rb_shell_player_handle_key	(RBShellPlayer *player, guint keyval);
#endif

G_END_DECLS

#endif /* __RB_SHELL_PLAYER_H */
