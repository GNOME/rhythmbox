/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
 *  $Id$
 */

#include <gtk/gtkhbox.h>
#include <bonobo/bonobo-ui-component.h>
#include <monkey-media.h>

#include "rb-source.h"

#ifndef __RB_SHELL_PLAYER_H
#define __RB_SHELL_PLAYER_H

G_BEGIN_DECLS

#define RB_TYPE_SHELL_PLAYER         (rb_shell_player_get_type ())
#define RB_SHELL_PLAYER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SHELL_PLAYER, RBShellPlayer))
#define RB_SHELL_PLAYER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SHELL_PLAYER, RBShellPlayerClass))
#define RB_IS_SHELL_PLAYER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SHELL_PLAYER))
#define RB_IS_SHELL_PLAYER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SHELL_PLAYER))
#define RB_SHELL_PLAYER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SHELL_PLAYER, RBShellPlayerClass))

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
} RBShellPlayerClass;

GType			rb_shell_player_get_type   (void);

RBShellPlayer *		rb_shell_player_new		(BonoboUIComponent *component,
							 BonoboUIComponent *tray_component);

void			rb_shell_player_set_source	(RBShellPlayer *shell_player,
							 RBSource *player);
void			rb_shell_player_set_playing_source (RBShellPlayer *player,
							    RBSource *source);

RBSource *		rb_shell_player_get_source	(RBShellPlayer *shell_player);

void			rb_shell_player_jump_to_current (RBShellPlayer *player);

void			rb_shell_player_stop		(RBShellPlayer *shell_player);

void			rb_shell_player_set_shuffle	(RBShellPlayer *shell_player,
							 gboolean shuffle);

MonkeyMediaPlayer *	rb_shell_player_get_mm_player	(RBShellPlayer *shell_player);

gboolean		rb_shell_player_get_playing	(RBShellPlayer *shell_player);

const char *		rb_shell_player_get_playing_path(RBShellPlayer *shell_player);

#ifdef HAVE_ACME
gboolean		rb_shell_player_handle_key	(RBShellPlayer *player, guint keyval);
#endif

gboolean		rb_shell_volume_scroll 		(GtkWidget *widget, GdkEvent *event, gpointer data);

G_END_DECLS

#endif /* __RB_SHELL_PLAYER_H */
