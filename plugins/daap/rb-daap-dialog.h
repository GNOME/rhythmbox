/*
 *  Header for DAAP (iTunes Music Sharing) dialogs (password & name collision)
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __RB_DAAP_DIALOG_H
#define __RB_DAAP_DIALOG_H

#include <glib.h>

G_BEGIN_DECLS

char * rb_daap_collision_dialog_new_run (GtkWindow  *parent,
                                         const char *old_name);

G_END_DECLS

#endif /* __RB_DAAP_DIALOG_H */
