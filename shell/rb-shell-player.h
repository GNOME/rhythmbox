/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
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

#include <glib-object.h>

#include <sources/rb-source.h>
#include <rhythmdb/rhythmdb.h>

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
	RB_SHELL_PLAYER_ERROR_POSITION_NOT_AVAILABLE,
	RB_SHELL_PLAYER_ERROR_NOT_PLAYABLE,
} RBShellPlayerError;

GType rb_shell_player_error_get_type (void);
#define RB_TYPE_SHELL_PLAYER_ERROR	(rb_shell_player_error_get_type())

#define RB_SHELL_PLAYER_ERROR rb_shell_player_error_quark ()

GQuark rb_shell_player_error_quark (void);
typedef struct _RBShellPlayer RBShellPlayer;
typedef struct _RBShellPlayerClass RBShellPlayerClass;
typedef struct RBShellPlayerPrivate RBShellPlayerPrivate;

struct _RBShellPlayer
{
	GObject parent;

	RBShellPlayerPrivate *priv;
};

struct _RBShellPlayerClass
{
	GObjectClass parent_class;

	void (*window_title_changed) (RBShellPlayer *player, const char *window_title);
	void (*elapsed_changed) (RBShellPlayer *player, guint elapsed);
	void (*elapsed_nano_changed) (RBShellPlayer *player, gint64 elapsed);
	void (*playing_changed) (RBShellPlayer *player, gboolean playing);
	void (*playing_source_changed) (RBShellPlayer *player, RBSource *source);
	void (*playing_uri_changed) (RBShellPlayer *player, const char *uri);
	void (*playing_song_changed) (RBShellPlayer *player, RhythmDBEntry *entry);
	void (*playing_song_property_changed) (RBShellPlayer *player,
					       const char *uri,
					       const char *property,
					       GValue *old,
					       GValue *newValue);
};

GType			rb_shell_player_get_type   (void);

RBShellPlayer *		rb_shell_player_new		(RhythmDB *db);

void			rb_shell_player_set_selected_source	(RBShellPlayer *player,
								 RBSource *source);
void			rb_shell_player_set_playing_source (RBShellPlayer *player,
							    RBSource *source);

RBSource *		rb_shell_player_get_playing_source (RBShellPlayer *player);
RBSource *		rb_shell_player_get_active_source (RBShellPlayer *player);

void			rb_shell_player_jump_to_current (RBShellPlayer *player);

void			rb_shell_player_play_entry	(RBShellPlayer *player,
							 RhythmDBEntry *entry,
							 RBSource *source);
gboolean		rb_shell_player_play		(RBShellPlayer *player, GError **error);
gboolean		rb_shell_player_pause		(RBShellPlayer *player, GError **error);
gboolean                rb_shell_player_playpause	(RBShellPlayer *player, GError **error);
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
gboolean		rb_shell_player_seek		(RBShellPlayer *player,
							 gint32 offset,
							 GError **error);
long			rb_shell_player_get_playing_song_duration (RBShellPlayer *player);

gboolean		rb_shell_player_get_playing	(RBShellPlayer *player,
							 gboolean *playing,
							 GError **error);

gboolean		rb_shell_player_get_playing_path(RBShellPlayer *player,
							 const gchar **path,
							 GError **error);

void			rb_shell_player_set_playback_state(RBShellPlayer *player,
							   gboolean shuffle, gboolean repeat);

gboolean                rb_shell_player_get_playback_state(RBShellPlayer *player,
							   gboolean *shuffle,
							   gboolean *repeat);

RhythmDBEntry *         rb_shell_player_get_playing_entry (RBShellPlayer *player);

gboolean		rb_shell_player_set_volume	(RBShellPlayer *player,
							 gdouble volume,
							 GError **error);

gboolean		rb_shell_player_get_volume	(RBShellPlayer *player,
							 gdouble *volume,
							 GError **error);

gboolean		rb_shell_player_set_volume_relative (RBShellPlayer *player,
							 gdouble delta,
							 GError **error);

gboolean		rb_shell_player_set_mute	(RBShellPlayer *player,
							 gboolean mute,
							 GError **error);

gboolean		rb_shell_player_get_mute	(RBShellPlayer *player,
							 gboolean *mute,
							 GError **error);
void			rb_shell_player_add_play_order (RBShellPlayer *player,
							const char *name,
							const char *description,
							GType order_type,
							gboolean hidden);
void			rb_shell_player_remove_play_order (RBShellPlayer *player,
							   const char *name);

G_END_DECLS

#endif /* __RB_SHELL_PLAYER_H */
