/*
 *  arch-tag: Header for simple Rhythmbox debugging interface
 *
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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
 */

#ifndef __RB_DEBUG_H
#define __RB_DEBUG_H

#include <stdarg.h>
#include <glib.h>

G_BEGIN_DECLS

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define rb_debug(...) rb_debug_real (__func__, __FILE__, __LINE__, __VA_ARGS__)
#elif defined(__GNUC__) && __GNUC__ >= 2
#define rb_debug(...) rb_debug_real (__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#else
#define rb_debug(...) rb_debug_real ("", __FILE__, __LINE__, __VA_ARGS__)
#endif

void rb_debug_init             (gboolean debug);

void rb_debug_real             (const char *func,
				const char *file,
				int line,
				const char *format, ...);

void rb_debug_stop_in_debugger (void);

typedef struct RBProfiler RBProfiler;

RBProfiler *rb_profiler_new   (const char *name);
void        rb_profiler_dump  (RBProfiler *profiler);
void        rb_profiler_reset (RBProfiler *profiler);
void        rb_profiler_free  (RBProfiler *profiler);

G_END_DECLS

#endif /* __RB_DEBUG_H */
