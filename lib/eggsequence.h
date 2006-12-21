/* GLIB - Library of useful routines for C programming
 * Copyright (C) 2002, 2003, 2004, 2005  Soeren Sandmann (sandmann@daimi.au.dk)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <glib.h>

#ifndef __GSEQUENCE_H__
#define __GSEQUENCE_H__

typedef struct _EggSequence      EggSequence;
typedef struct _EggSequenceNode  EggSequenceIter;



typedef gint (* EggSequenceIterCompareFunc) (EggSequenceIter *a,
					     EggSequenceIter *b,
					     gpointer	      data);

typedef gpointer (* EggSequenceAggregateFunction) (gconstpointer   before,
						   EggSequenceIter *mid,
						   gconstpointer   after);

/* EggSequence */
EggSequence *   egg_sequence_new                  (GDestroyNotify          data_destroy);
void            egg_sequence_free                 (EggSequence            *seq);
gint            egg_sequence_get_length           (EggSequence            *seq);
void		egg_sequence_foreach		  (EggSequence		  *seq,
						   GFunc		   func,
						   gpointer		   data);
void		egg_sequence_foreach_range	  (EggSequenceIter	  *begin,
						   EggSequenceIter        *end,
						   GFunc		   func,
						   gpointer		   data);
void            egg_sequence_sort                 (EggSequence            *seq,
						   GCompareDataFunc        cmp_func,
						   gpointer                cmp_data);
void		egg_sequence_sort_iter		  (EggSequence		  *seq,
						   EggSequenceIterCompareFunc	   cmp_func,
						   gpointer		   cmp_data);

/* Getting iters */
EggSequenceIter *egg_sequence_get_begin_iter       (EggSequence            *seq);
EggSequenceIter *egg_sequence_get_end_iter         (EggSequence            *seq);
EggSequenceIter *egg_sequence_get_iter_at_pos      (EggSequence            *seq,
						    gint                    pos);
EggSequenceIter *egg_sequence_append               (EggSequence            *seq,
						   gpointer                data);
EggSequenceIter *egg_sequence_prepend              (EggSequence            *seq,
						   gpointer                data);
EggSequenceIter *egg_sequence_insert_before        (EggSequenceIter *        iter,
						   gpointer                data);
void		 egg_sequence_move		  (EggSequenceIter *	   src,
						   EggSequenceIter *	   dest);
void		 egg_sequence_swap                (EggSequenceIter *       a,
						   EggSequenceIter *       b);
EggSequenceIter *egg_sequence_insert_sorted        (EggSequence            *seq,
						   gpointer                data,
						   GCompareDataFunc        cmp_func,
						   gpointer                cmp_data);
EggSequenceIter *egg_sequence_insert_sorted_iter   (EggSequence		  *seq,
						   gpointer                data,
						   EggSequenceIterCompareFunc	   iter_cmp,
						   gpointer		   cmp_data);
void		egg_sequence_sort_changed	  (EggSequenceIter *	   iter,
						   GCompareDataFunc        cmp_func,
						   gpointer                cmp_data);
void		egg_sequence_sort_changed_iter    (EggSequenceIter *	   iter,
						   EggSequenceIterCompareFunc	   iter_cmp,
						   gpointer		   cmp_data);

void            egg_sequence_remove               (EggSequenceIter *        iter);
void            egg_sequence_remove_range         (EggSequenceIter *        begin,
						   EggSequenceIter *        end);
void            egg_sequence_move_range           (EggSequenceIter *        iter,
						   EggSequenceIter *        begin,
						   EggSequenceIter *        end);
EggSequenceIter *egg_sequence_search               (EggSequence            *seq,
						   gpointer		   data,
						   GCompareDataFunc        cmp_func,
						   gpointer                cmp_data);
EggSequenceIter *egg_sequence_search_iter         (EggSequence            *seq,
						   gpointer		   data,
						   EggSequenceIterCompareFunc     cmp_func,
						   gpointer                cmp_data);

/* dereferencing */
gpointer        egg_sequence_get                  (EggSequenceIter *        iter);
void		egg_sequence_set		  (EggSequenceIter *	   iter,
						   gpointer		   data);


/* operations on EggSequenceIter * */
gboolean        egg_sequence_iter_is_begin        (EggSequenceIter *        iter);
gboolean        egg_sequence_iter_is_end          (EggSequenceIter *        iter);
EggSequenceIter *egg_sequence_iter_next            (EggSequenceIter *        iter);
EggSequenceIter *egg_sequence_iter_prev            (EggSequenceIter *        iter);
gint            egg_sequence_iter_get_position    (EggSequenceIter *        iter);
EggSequenceIter *egg_sequence_iter_move            (EggSequenceIter *        iter,
						   gint                    leap);
EggSequence *   egg_sequence_iter_get_sequence    (EggSequenceIter *        iter);


/* search */
gint            egg_sequence_iter_compare         (EggSequenceIter *a,
						   EggSequenceIter *        b);
EggSequenceIter *egg_sequence_range_get_midpoint   (EggSequenceIter *        begin,
						   EggSequenceIter *        end);

/* debug */
gint          egg_sequence_calc_tree_height    (EggSequence                  *seq);
void	      egg_sequence_self_test           (EggSequence                  *seq);

#if 0
/* aggregates */
void          egg_sequence_set_aggregator   (EggSequence                  *seq,
					     EggSequenceAggregateFunction  func,
					     GDestroyNotify                destroy);
gconstpointer egg_sequence_get_aggregate    (EggSequenceIter *              begin,
					     EggSequenceIter *              end);
void          egg_sequence_update_aggregate (EggSequenceIter *              iter);


#endif


#endif /* __GSEQUENCE_H__ */
