/* 
 * Copyright (C) 2003 Ross Burton <ross@burtonini.com>
 *
 * Sound Juicer - sj-error.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The Rhythmbox authors hereby grants permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: Ross Burton <ross@burtonini.com>
 */

#ifndef SJ_ERROR_H
#define SJ_ERROR_H

#include <glib.h>

#define SJ_ERROR sj_error_quark ()

typedef enum {
  SJ_ERROR_INTERNAL_ERROR,
  SJ_ERROR_CD_PERMISSION_ERROR,
  SJ_ERROR_CD_NO_MEDIA,
  SJ_ERROR_CD_LOOKUP_ERROR
} SjError;

GQuark sj_error_quark (void) G_GNUC_CONST;

#endif
