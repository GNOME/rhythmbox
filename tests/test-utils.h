/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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

#include "rhythmdb.h"

#ifndef __TEST_UTILS_H
#define __TEST_UTILS_H

/* yes.  really. */
extern RhythmDB *db;

void init_setup (SRunner *runner, int argc, char **argv);
void init_once (gboolean test);

void start_test_case (void);
void end_step (void);
void end_test_case (void);

void set_waiting_signal (GObject *o, const char *name);
void wait_for_signal (void);

void test_rhythmdb_setup (void);
void test_rhythmdb_shutdown (void);

void set_entry_string (RhythmDB *db, RhythmDBEntry *entry, RhythmDBPropType prop, const char *value);
void set_entry_ulong (RhythmDB *db, RhythmDBEntry *entry, RhythmDBPropType prop, gulong value);
void set_entry_hidden (RhythmDB *db, RhythmDBEntry *entry, gboolean hidden);

gulong set_waiting_signal_with_callback (GObject *o, const char *name, GCallback callback, gpointer data);

#endif /* __TEST_UTILS_H */
