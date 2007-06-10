/*
 *  arch-tag: Header for mtp source object
 *
 *  Copyright (C) 2006 Peter Grundström  <pete@openfestis.org>
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

#ifndef __RB_MTP_SOURCE_H
#define __RB_MTP_SOURCE_H

#include "rb-shell.h"
#include "rb-removable-media-source.h"
#include "rhythmdb.h"
#include <libmtp.h>

G_BEGIN_DECLS

#define RB_TYPE_MTP_SOURCE         (rb_mtp_source_get_type ())
#define RB_MTP_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_MTP_SOURCE, RBMtpSource))
#define RB_MTP_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_MTP_SOURCE, RBMtpSourceClass))
#define RB_IS_MTP_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_MTP_SOURCE))
#define RB_IS_MTP_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_MTP_SOURCE))
#define RB_MTP_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_MTP_SOURCE, RBMtpSourceClass))

typedef struct
{
	RBRemovableMediaSource parent;
} RBMtpSource;

typedef struct
{
	RBRemovableMediaSourceClass parent;
} RBMtpSourceClass;

RBBrowserSource *	rb_mtp_source_new		(RBShell *shell, LIBMTP_mtpdevice_t *device, const char *udi);
GType			rb_mtp_source_get_type		(void);
GType			rb_mtp_source_register_type	(GTypeModule *module);

gboolean rb_mtp_source_is_udi				(RBMtpSource *source, const char *udi);

G_END_DECLS

#endif /* __RB_MTP_SOURCE_H */
