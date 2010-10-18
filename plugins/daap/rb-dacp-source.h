/*
 *  Header for DACP (iTunes Remote) source object
 *
 *  Copyright (C) 2010 Alexandre Rosenfeld <alexandre.rosenfeld@gmail.com>
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

#ifndef __RB_DACP_SOURCE_H
#define __RB_DACP_SOURCE_H

#include "rb-shell.h"
#include "rb-source.h"
#include "rb-plugin.h"

#include <libdmapsharing/dmap.h>

G_BEGIN_DECLS

/* preferences */
#define CONF_DACP_PREFIX  	CONF_PREFIX "/plugins/daap"
#define CONF_KNOWN_REMOTES 	CONF_DACP_PREFIX "/known_remotes"

#define RB_TYPE_DACP_SOURCE         (rb_dacp_source_get_type ())
#define RB_DACP_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_DACP_SOURCE, RBDACPSource))
#define RB_DACP_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_DACP_SOURCE, RBDACPSourceClass))
#define RB_IS_DACP_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_DACP_SOURCE))
#define RB_IS_DACP_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_DACP_SOURCE))
#define RB_DACP_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_DACP_SOURCE, RBDACPSourceClass))

typedef struct RBDACPSourcePrivate RBDACPSourcePrivate;

typedef struct {
	RBSource parent;

	RBDACPSourcePrivate *priv;
} RBDACPSource;

typedef struct {
	RBSourceClass parent;
} RBDACPSourceClass;

GType 		rb_dacp_source_get_type 	(void);

RBDACPSource* rb_dacp_source_new 		(RBPlugin *plugin,
						 RBShell *shell,
						 DACPShare *dacp_share,
						 const char *display_name,
						 const char *service_name);

void           rb_dacp_source_remote_found     (RBDACPSource *source);
void           rb_dacp_source_remote_lost      (RBDACPSource *source);

DACPShare     *rb_daap_create_dacp_share       (RBPlugin *plugin);

G_END_DECLS

#endif /* __RB_DACP_SOURCE_H */
