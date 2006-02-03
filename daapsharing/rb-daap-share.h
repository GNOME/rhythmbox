/*
 *  Header for DAAP (iTunes Music Sharing) sharing
 *
 *  Copyright (C) 2005 Charles Schmidt <cschmidt2@emich.edu>
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

#ifndef __RB_DAAP_SHARE_H
#define __RB_DAAP_SHARE_H

#include <glib-object.h>
#include "rhythmdb.h"
#include "rb-playlist-manager.h"

G_BEGIN_DECLS

#define RB_TYPE_DAAP_SHARE         (rb_daap_share_get_type ())
#define RB_DAAP_SHARE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_DAAP_SHARE, RBDAAPShare))
#define RB_DAAP_SHARE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_DAAP_SHARE, RBDAAPShareClass))
#define RB_IS_DAAP_SHARE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_DAAP_SHARE))
#define RB_IS_DAAP_SHARE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_DAAP_SHARE))
#define RB_DAAP_SHARE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_DAAP_SHARE, RBDAAPShareClass))

typedef struct RBDAAPSharePrivate RBDAAPSharePrivate;

typedef struct {
	GObject parent;

	RBDAAPSharePrivate *priv;
} RBDAAPShare;

typedef struct {
	GObjectClass parent;
} RBDAAPShareClass;

GType         rb_daap_share_get_type (void);

RBDAAPShare * rb_daap_share_new      (const char *name, 
                                      const char *password,
                                      RhythmDB *db, 
                                      RBPlaylistManager *playlist_manager);

#endif /* __RB_DAAP_SHARE_H */

G_END_DECLS
