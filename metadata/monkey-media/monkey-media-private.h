/*  monkey-sound
 *
 *  arch-tag: Header for internal monkey-media utility functions
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *                     Marco Pesenti Gritti <marco@it.gnome.org>
 *                     Bastien Nocera <hadess@hadess.net>
 *                     Seth Nickell <snickell@stanford.edu>
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

#ifndef __MONKEY_MEDIA_PRIVATE_H
#define __MONKEY_MEDIA_PRIVATE_H

#include <libintl.h>

#include "config.h"

G_BEGIN_DECLS

#define _(String) dgettext (GETTEXT_PACKAGE, String)
#define N_(String) (String)

GType       monkey_media_get_stream_info_impl_for (const char *uri,
						   char **mimetype);

gboolean    monkey_media_is_alive                 (void);

void        monkey_media_mkdir                    (const char *path);

const char *monkey_media_get_dir                  (void);

G_END_DECLS

#endif /* __MONKEY_MEDIA_PRIVATE_H */
