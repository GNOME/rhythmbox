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

#ifndef __RB_VIEW_CMD_H
#define __RB_VIEW_CMD_H

#include <bonobo/bonobo-ui-component.h>

#include "rb-view.h"

G_BEGIN_DECLS

void rb_view_cmd_song_copy 		(BonoboUIComponent *component,
		       		  	 RBView *view,
		       		 	 const char *verbname);

void rb_view_cmd_song_cut  		(BonoboUIComponent *component,
		       		 	 RBView *view,
		       		 	 const char *verbname);

void rb_view_cmd_song_paste  		(BonoboUIComponent *component,
		       		 	 RBView *view,
		       		 	 const char *verbname);

void rb_view_cmd_song_delete 		(BonoboUIComponent *component,
		         	 	 RBView *view,
		         	 	 const char *verbname);

void rb_view_cmd_song_properties 	(BonoboUIComponent *component,
		             		 RBView *view,
		             		 const char *verbname);

G_END_DECLS

#endif /* __RB_VIEW_CMD_H */
