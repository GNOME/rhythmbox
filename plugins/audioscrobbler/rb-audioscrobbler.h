/*
 *  arch-tag: Header file for Rhythmbox Audioscrobbler support
 *
 *  Copyright (C) 2005 Alex Revo <xiphoiadappendix@gmail.com>
 *					   Ruben Vermeersch <ruben@Lambda1.be>
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

#ifndef __RB_AUDIOSCROBBLER_H
#define __RB_AUDIOSCROBBLER_H

G_BEGIN_DECLS

#include <glib.h>

#include "rb-shell-player.h"
#include "rb-proxy-config.h"
#include "rb-plugin.h"

#define RB_TYPE_AUDIOSCROBBLER			(rb_audioscrobbler_get_type ())
#define RB_AUDIOSCROBBLER(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOSCROBBLER, RBAudioscrobbler))
#define RB_AUDIOSCROBBLER_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_AUDIOSCROBBLER, RBAudioscrobblerClass))
#define RB_IS_AUDIOSCROBBLER(o)			(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOSCROBBLER))
#define RB_IS_AUDIOSCROBBLER_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOSCROBBLER))
#define RB_AUDIOSCROBBLER_GET_CLASS(o)		(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOSCROBBLER, RBAudioscrobblerClass))


typedef struct _RBAudioscrobblerPrivate RBAudioscrobblerPrivate;

typedef struct
{
	GObject parent;

	RBAudioscrobblerPrivate *priv;
} RBAudioscrobbler;

typedef struct
{
	GObjectClass parent_class;
} RBAudioscrobblerClass;


GType			rb_audioscrobbler_get_type (void);

RBAudioscrobbler *	rb_audioscrobbler_new (RBShellPlayer *shell_player,
					       RBProxyConfig *proxy_config);

GtkWidget *		rb_audioscrobbler_get_config_widget (RBAudioscrobbler *audioscrobbler,
							     RBPlugin *plugin);

void			rb_audioscrobbler_username_entry_focus_out_event_cb (GtkWidget *widget,
                                                                             RBAudioscrobbler *audioscrobbler);
void			rb_audioscrobbler_username_entry_activate_cb (GtkEntry *entry,
								      RBAudioscrobbler *audioscrobbler);

void			rb_audioscrobbler_password_entry_focus_out_event_cb (GtkWidget *widget,
								     RBAudioscrobbler *audioscrobbler);
void			rb_audioscrobbler_password_entry_activate_cb (GtkEntry *entry,
								      RBAudioscrobbler *audioscrobbler);

void			rb_audioscrobbler_enabled_check_changed_cb (GtkCheckButton *button,
								    RBAudioscrobbler *audioscrobbler);


G_END_DECLS

#endif
