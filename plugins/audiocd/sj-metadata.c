/*
 * sj-metadata.c
 * Copyright (C) 2003 Ross Burton <ross@burtonini.com>
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
 */

#include "config.h"
#include <glib-object.h>
#include <glib/gi18n.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef USE_TOTEM_PL_PARSER
#include <unistd.h>
#include <brasero-medium-selection.h>
#else
#include <totem-disc.h>
#endif /* USE_TOTEM_PL_PARSER */

#include "sj-metadata.h"
#include "sj-metadata-marshal.h"
#include "sj-error.h"

enum {
  METADATA,
  LAST_SIGNAL
};

static void
sj_metadata_base_init (gpointer g_iface)
{
  static gboolean initialized = FALSE;
  if (!initialized) {
    /* TODO: make these constructors */
    /* TODO: add nice nick and blurb strings */
    g_object_interface_install_property (g_iface,
                                         g_param_spec_string ("device", "device", NULL, NULL,
                                                              G_PARAM_READABLE|G_PARAM_WRITABLE|
                                                              G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB));

    g_object_interface_install_property (g_iface,
                                         g_param_spec_string ("proxy-host", "proxy-host", NULL, NULL,
                                                              G_PARAM_READABLE|G_PARAM_WRITABLE|
                                                              G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB));

    g_object_interface_install_property (g_iface,
                                         g_param_spec_int ("proxy-port", "proxy-port", NULL,
                                                           0, G_MAXINT, 0,
                                                           G_PARAM_READABLE|G_PARAM_WRITABLE|
                                                           G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB));

    initialized = TRUE;
  }
}

GType
sj_metadata_get_type (void)
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (SjMetadataClass), /* class_size */
      sj_metadata_base_init,   /* base_init */
      NULL,           /* base_finalize */
      NULL,
      NULL,           /* class_finalize */
      NULL,           /* class_data */
      0,
      0,              /* n_preallocs */
      NULL,
      NULL
    };
    
    type = g_type_register_static (G_TYPE_INTERFACE, "SjMetadata", &info, 0);
    g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);
  }
  return type;
}

void
sj_metadata_set_cdrom (SjMetadata *metadata, const char* device)
{
  g_object_set (metadata, "device", device, NULL);
}

void
sj_metadata_set_proxy (SjMetadata *metadata, const char* proxy)
{
  g_object_set (metadata, "proxy-host", proxy, NULL);
}

void
sj_metadata_set_proxy_port (SjMetadata *metadata, const int proxy_port)
{
  g_object_set (metadata, "proxy-port", proxy_port, NULL);
}

GList *
sj_metadata_list_albums (SjMetadata *metadata, char **url, GError **error)
{
  return SJ_METADATA_GET_CLASS (metadata)->list_albums (metadata, url, error);
}

char *
sj_metadata_helper_scan_disc_number (const char *album_title, int *disc_number)
{
  GRegex *disc_regex;
  GMatchInfo *info;
  char *new_title;
  int num;

  disc_regex = g_regex_new (".+( \\(disc (\\d+).*)", 0, 0, NULL);
  new_title = NULL;
  *disc_number = 0;

  if (g_regex_match (disc_regex, album_title, 0, &info)) {
    int pos = 0;
    char *s;

    g_match_info_fetch_pos (info, 1, &pos, NULL);
    if (pos) {
      new_title = g_strndup (album_title, pos);
    }

    s = g_match_info_fetch (info, 2);
    num = atoi (s);
    *disc_number = num;
    g_free (s);
  }

  g_match_info_free (info);
  g_regex_unref (disc_regex);

  return new_title;
}

GDate *
sj_metadata_helper_scan_date (const char *date)
{
  int matched, year=1, month=1, day=1;

  if (date == NULL)
    return NULL;

  matched = sscanf (date, "%u-%u-%u", &year, &month, &day);
  if (matched >= 1) {
    return g_date_new_dmy ((day == 0) ? 1 : day, (month == 0) ? 1 : month, year);
  }

  return NULL;
}

gboolean
sj_metadata_helper_check_media (const char *cdrom, GError **error)
{
#ifndef USE_TOTEM_PL_PARSER
  BraseroMediumMonitor *monitor;
  BraseroMedium *medium;
  BraseroDrive *drive;


  /* This initialize the library if it isn't done yet */
  monitor = brasero_medium_monitor_get_default ();
  drive = brasero_medium_monitor_get_drive (monitor, cdrom);
  if (drive == NULL) {
    return FALSE;
  }
  medium = brasero_drive_get_medium (drive);
  g_object_unref (drive);

  if (!medium || !BRASERO_MEDIUM_VALID (brasero_medium_get_status (medium))) {
    char *msg;
    SjError err;

    if (access (cdrom, W_OK) == 0) {
      msg = g_strdup_printf (_("Device '%s' does not contain any media"), cdrom);
      err = SJ_ERROR_CD_NO_MEDIA;
    } else {
      msg = g_strdup_printf (_("Device '%s' could not be opened. Check the access permissions on the device."), cdrom);
      err = SJ_ERROR_CD_PERMISSION_ERROR;
    }
    g_set_error (error, SJ_ERROR, err, _("Cannot read CD: %s"), msg);
    g_free (msg);

    return FALSE;
  }
#else
  TotemDiscMediaType type;
  GError *totem_error = NULL;

  type = totem_cd_detect_type (cdrom, &totem_error);

  if (totem_error != NULL) {
    g_set_error (error, SJ_ERROR, SJ_ERROR_CD_NO_MEDIA, _("Cannot read CD: %s"), totem_error->message);
    g_error_free (totem_error);

    return FALSE;
  }
#endif /* !USE_TOTEM_PL_PARSER */

  return TRUE;
}

