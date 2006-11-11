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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __DAAP_SHARING_H
#define __DAAP_SHARING_H

#include "rb-shell.h"

G_BEGIN_DECLS

void 	rb_daap_sharing_init (RBShell *shell);
void 	rb_daap_sharing_shutdown (RBShell *shell);
char *	rb_daap_sharing_default_share_name (void);

G_END_DECLS

#endif /* __DAAP_SHARING_H */
