/* 
 * arch-tag: Implementation of atomic integers for Rhythmbox
 * Copyright (C) 2002, 2003  Red Hat, Inc.
 * Copyright (C) 2003 CodeFactory AB
 *
 * Licensed under the Academic Free License version 1.2
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "rb-atomic.h"

GStaticMutex rb_atomic_mutex = G_STATIC_MUTEX_INIT;

#ifdef RB_USE_ATOMIC_INT_486
/* Taken from CVS version 1.7 of glibc's sysdeps/i386/i486/atomicity.h */
/* Since the asm stuff here is gcc-specific we go ahead and use "inline" also */
static inline gint32
atomic_exchange_and_add (RBAtomic *atomic,
                         volatile gint32 val)
{
  register gint32 result;

  __asm__ __volatile__ ("lock; xaddl %0,%1"
                        : "=r" (result), "=m" (atomic->value)
			: "0" (val), "m" (atomic->value));
  return result;
}
#endif

/**
 * Atomically increments an integer
 *
 * @param atomic pointer to the integer to increment
 * @returns the value before incrementing
 *
 * @todo implement arch-specific faster atomic ops
 */
gint32
rb_atomic_inc (RBAtomic *atomic)
{
#ifdef RB_USE_ATOMIC_INT_486
  return atomic_exchange_and_add (atomic, 1);
#else
  gint32 res;
  g_static_mutex_lock (&rb_atomic_mutex);
  res = atomic->value;
  atomic->value += 1;
  g_static_mutex_unlock (&rb_atomic_mutex);
  return res;
#endif
}

/**
 * Atomically decrement an integer
 *
 * @param atomic pointer to the integer to decrement
 * @returns the value before decrementing
 *
 * @todo implement arch-specific faster atomic ops
 */
gint32
rb_atomic_dec (RBAtomic *atomic)
{
#ifdef RB_USE_ATOMIC_INT_486
  return atomic_exchange_and_add (atomic, -1);
#else
  gint32 res;
  
  g_static_mutex_lock (&rb_atomic_mutex);
  res = atomic->value;
  atomic->value -= 1;
  g_static_mutex_unlock (&rb_atomic_mutex);
  return res;
#endif
}
