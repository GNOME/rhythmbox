/*
 * Copyright (C) 2009 Jonathan Matthew  <jonathan@d14n.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MPID_PRIVATE_H
#define __MPID_PRIVATE_H

void		mpid_debug (const char *format, ...) G_GNUC_PRINTF (1, 2);
void		mpid_debug_strv (const char *what, char **strv);
void		mpid_debug_str (const char *what, const char *str);

void		mpid_device_db_lookup (MPIDDevice *device);
void		mpid_device_read_override_file (MPIDDevice *device);

char *		mpid_device_get_mount_point (MPIDDevice *device);
char *		mpid_device_get_device_path (MPIDDevice *device);

void		mpid_override_string_from_keyfile (char **str, GKeyFile *keyfile, const char *group, const char *key);
void		mpid_override_strv_from_keyfile (char ***strv, GKeyFile *keyfile, const char *group, const char *key);

void		mpid_read_device_file (MPIDDevice *device, const char *device_info_path);
void		mpid_find_and_read_device_file (MPIDDevice *device, const char *device_file);

void		mpid_device_debug (MPIDDevice *device, const char *what);

#endif /* __MPID_PRIVATE_H */

