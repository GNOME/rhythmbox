/*
 *  Copyright (C) 2002,2003 Colin Walters <cwalters@gnome.org>
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


#include "rb-playlist.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnome/gnome-i18n.h>

#include "rb-dialog.h"
#include "rb-iradio-backend.h"
#include "rb-library.h"
#include "rb-string-helpers.h"
#include "rb-windows-ini-file.h"

static void
rb_playlist_handle_entry (RBLibrary *library, RBIRadioBackend *iradio_backend,
			  GList *locations, const char *title)
{
	GnomeVFSURI *vfsuri = gnome_vfs_uri_new ((char *) locations->data);
	const char *scheme;
	if (!vfsuri)
	{
		rb_error_dialog (_("Unable to parse playlist entry \"%s\""),
				 (char *) locations->data);
		return;
	}
	scheme = gnome_vfs_uri_get_scheme (vfsuri);
	if (strncmp ("http", scheme, 4) == 0)
	{
		rb_iradio_backend_add_station_full (iradio_backend,
						    locations, title, NULL);
	}
	else
	{
		rb_library_add_uri (library, (char *) g_list_first(locations)->data);
		g_list_free (locations);
	}
}

char *
rb_playlist_load (RBLibrary *library, RBIRadioBackend *iradio_backend, const char *fname)
{
	RBWindowsINIFile *plsfile = rb_windows_ini_file_new (fname);
	GList *keys, *entry;
	char *firstentry_uri = NULL;
	if (!plsfile)
	{
		fprintf (stderr, "couldn't parse playlist file\n");
		rb_error_dialog (_("Unable to load playlist file \"%s\""));
		return NULL;
	}
	
	keys = rb_windows_ini_file_get_sections (plsfile);
	if ((entry = g_list_find_custom (keys, "playlist", rb_utf8_strncasecmp))
	    || (entry = g_list_find_custom (keys, "default", rb_utf8_strncasecmp)))
	{
		char *section = (char *) entry->data;
		GList *sectionkeys = rb_windows_ini_file_get_keys (plsfile, section);
		GList *sectionentry = g_list_find_custom (sectionkeys, "numberofentries", rb_utf8_strncasecmp);
		long numentries, i;

		GList *locations = NULL;
		char *title = NULL;
		long prevlocation = 0; 

		if (!sectionentry)
		{
			fprintf (stderr, "no numberofentries\n");
			rb_error_dialog (_("Error reading playlist file \"%s\":\n no numberofentries header found"),
					 fname);
			goto section_parse_error;
		}
		numentries = atol (rb_windows_ini_file_lookup (plsfile, section,
							       "numberofentries"));
		for (i = 1; i <= numentries; i++)
		{
			char keystr[60];
			const char *curtitle;
			const char *uri;
			snprintf (keystr, sizeof(keystr), "file%ld", i);
			sectionentry = g_list_find_custom (sectionkeys, keystr, rb_utf8_strncasecmp);
			if (!sectionentry)
				continue;
			uri = rb_windows_ini_file_lookup (plsfile, section, keystr);
			snprintf (keystr, sizeof(keystr), "title%ld", i);
			sectionentry = g_list_find_custom (sectionkeys, keystr, rb_utf8_strncasecmp);
			if (!sectionentry)
				curtitle = NULL;
			else
				curtitle = rb_windows_ini_file_lookup (plsfile, section, keystr);
			if (curtitle && !strncmp ("(#", curtitle, 2))
			{
				char *rest = (char*)curtitle+2;
				long curlocation = strtol (curtitle+2, &rest, 10);
				if (curlocation == prevlocation+1)
				{
					/* Yes, parsing in C sucks. */
					while (isspace (*rest))
						rest++;
					if (*rest != '-')
						goto end_multiple;
					rest++;
					while (isspace (*rest))
						rest++;
					while (isdigit (*rest))
						rest++;
					if (*rest != '/')
						goto end_multiple;
					rest++;
					while (isdigit (*rest))
						rest++;
					if (*rest != ')')
						goto end_multiple;
					rest++;
					while (isspace (*rest))
						rest++;
					if (title == NULL)
						title = rest;
				}
				else
					goto end_multiple;

				/* Ok, if all the above worked, then
				 * this is another entry in a
				 * multiple-entry file */
				locations = g_list_prepend (locations, (char *) uri);
				prevlocation = curlocation;
			}
			else
			{
				rb_playlist_handle_entry (library,
							  iradio_backend,
							  g_list_prepend(NULL, (char *) uri),
							  curtitle);
				if (firstentry_uri == NULL)
					firstentry_uri = g_strdup (uri);
				locations = NULL;
				title = NULL;
				prevlocation = 0;
			}
			continue;
		end_multiple:
			if (locations)
				rb_playlist_handle_entry (library, iradio_backend, locations, title);
			if (firstentry_uri == NULL)
				firstentry_uri = g_strdup (uri);
			locations = g_list_prepend(NULL, (char *) uri);
			rb_playlist_handle_entry (library, iradio_backend, locations, curtitle);
			locations = NULL;
			title = NULL;
			prevlocation = 0;
		}
		if (locations)
		{
			rb_playlist_handle_entry (library, iradio_backend, locations, title);
			if (firstentry_uri == NULL)
				firstentry_uri = g_strdup ((char*) g_list_first (locations)->data);
		}
	section_parse_error:
		g_list_free (sectionkeys);
	}
	else
	{
		fprintf (stderr, "no playlist header\n");
		rb_error_dialog (_("Error reading playlist file \"%s\":\n no playlist header"),
				 fname);
	}

	g_list_free (keys);
	return firstentry_uri;
}
