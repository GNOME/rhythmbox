/*
 *  Copyright (C) 2002 Colin Walters <walters@gnu.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#ifndef __RB_WINDOWS_INI_FILE_H
#define __RB_WINDOWS_INI_FILE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_WINDOWS_INI_FILE         (rb_windows_ini_file_get_type ())
#define RB_WINDOWS_INI_FILE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_WINDOWS_INI_FILE, RBWindowsINIFile))
#define RB_WINDOWS_INI_FILE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_WINDOWS_INI_FILE, RBWindowsINIFileClass))
#define RB_IS_WINDOWS_INI_FILE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_WINDOWS_INI_FILE))
#define RB_IS_WINDOWS_INI_FILE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_WINDOWS_INI_FILE))
#define RB_WINDOWS_INI_FILE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_WINDOWS_INI_FILE, RBWindowsINIFileClass))

typedef struct _RBWindowsINIFilePrivate RBWindowsINIFilePrivate;

typedef struct {
	GObject parent;

	RBWindowsINIFilePrivate *priv;
} RBWindowsINIFile;

typedef struct {
	GObjectClass parent;
} RBWindowsINIFileClass;

GType     rb_windows_ini_file_get_type    (void);

RBWindowsINIFile *rb_windows_ini_file_new (const char *filename);

GList *rb_windows_ini_file_get_sections (RBWindowsINIFile *inifile);

GList *rb_windows_ini_file_get_keys (RBWindowsINIFile *inifile, const char *section);

const char *rb_windows_ini_file_lookup (RBWindowsINIFile *inifile, const char *section, const char *key);

G_END_DECLS

#endif /* __RB_WINDOWS_INI_FILE_H */
