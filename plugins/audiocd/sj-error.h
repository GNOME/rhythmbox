/* 
 * Copyright (C) 2003 Ross Burton <ross@burtonini.com>
 *
 * Sound Juicer - sj-error.h
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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
