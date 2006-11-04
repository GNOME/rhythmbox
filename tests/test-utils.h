
#include "rhythmdb.h"

#ifndef __TEST_UTILS_H
#define __TEST_UTILS_H

#ifndef fail_if
#define fail_if(expr, ...) fail_unless(!(expr), "Failure '"#expr"' occured")
#endif

/* yes.  really. */
extern RhythmDB *db;

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

#endif /* __TEST_UTILS_H */
