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

#include "eggsequence.h"

typedef struct _EggSequenceNode EggSequenceNode;

struct _EggSequence
{
    EggSequenceNode *	 end_node;
    GDestroyNotify	 data_destroy_notify;
    gboolean		 access_prohibited;
};

struct _EggSequenceNode
{
    gint n_nodes;
    EggSequenceNode *parent;    
    EggSequenceNode *left;
    EggSequenceNode *right;
    gpointer data;		/* For the end node, this field points
				 * to the sequence
				 */
};

static EggSequenceNode *node_new           (gpointer           data);
static EggSequenceNode *node_get_first     (EggSequenceNode   *node);
static EggSequenceNode *node_get_last      (EggSequenceNode   *node);
static EggSequenceNode *node_get_prev      (EggSequenceNode   *node);
static EggSequenceNode *node_get_next      (EggSequenceNode   *node);
static gint             node_get_pos       (EggSequenceNode   *node);
static EggSequenceNode *node_get_by_pos    (EggSequenceNode   *node,
					    gint               pos);
static EggSequenceNode *node_find_closest  (EggSequenceNode   *haystack,
					    EggSequenceNode   *needle,
					    EggSequenceNode   *end,
					    EggSequenceIterCompareFunc cmp,
					    gpointer           user_data);
static gint             node_get_length    (EggSequenceNode   *node);
static void             node_free          (EggSequenceNode   *node,
					    EggSequence       *seq);
static void             node_cut           (EggSequenceNode   *split);
static void             node_insert_after  (EggSequenceNode   *node,
					    EggSequenceNode   *second);
static void             node_insert_before (EggSequenceNode   *node,
					    EggSequenceNode   *new);
static void             node_unlink        (EggSequenceNode   *node);
static void             node_insert_sorted (EggSequenceNode   *node,
					    EggSequenceNode   *new,
					    EggSequenceNode   *end,
					    EggSequenceIterCompareFunc cmp_func,
					    gpointer           cmp_data);

static EggSequence *
get_sequence (EggSequenceNode *node)
{
    return (EggSequence *)node_get_last (node)->data;
}

static void
check_seq_access (EggSequence *seq)
{
    if (G_UNLIKELY (seq->access_prohibited))
    {
	g_warning ("Accessing a sequence while it is "
		   "being sorted is not allowed");
    }
}

static void
check_iter_access (EggSequenceIter *iter)
{
    check_seq_access (get_sequence (iter));
}

static gboolean
is_end (EggSequenceIter *iter)
{
    EggSequence *seq = get_sequence (iter);
    
    return seq->end_node == iter;
}

/*
 * Public API
 */

/* EggSequence */
EggSequence *
egg_sequence_new (GDestroyNotify data_destroy)
{
    EggSequence *seq = g_new (EggSequence, 1);
    seq->data_destroy_notify = data_destroy;
    
    seq->end_node = node_new (seq);
    
    seq->access_prohibited = FALSE;
    
    return seq;
}

void
egg_sequence_free (EggSequence *seq)
{
    g_return_if_fail (seq != NULL);
    
    check_seq_access (seq);
    
    node_free (seq->end_node, seq);
    
    g_free (seq);
}

void
egg_sequence_foreach_range (EggSequenceIter *begin,
			    EggSequenceIter *end,
			    GFunc	     func,
			    gpointer	     data)
{
    EggSequence *seq;
    EggSequenceIter *iter;
    
    g_return_if_fail (func != NULL);
    g_return_if_fail (begin != NULL);
    g_return_if_fail (end != NULL);
    
    seq = get_sequence (begin);
    
    seq->access_prohibited = TRUE;
    
    iter = begin;
    while (iter != end)
    {
	EggSequenceIter *next = node_get_next (iter);
	
	func (iter->data, data);
	
	iter = next;
    }
    
    seq->access_prohibited = FALSE;
}

void
egg_sequence_foreach (EggSequence *seq,
		      GFunc        func,
		      gpointer     data)
{
    EggSequenceIter *begin, *end;
    
    check_seq_access (seq);
    
    begin = egg_sequence_get_begin_iter (seq);
    end   = egg_sequence_get_end_iter (seq);
    
    egg_sequence_foreach_range (begin, end, func, data);
}

EggSequenceIter *
egg_sequence_range_get_midpoint (EggSequenceIter *begin,
				 EggSequenceIter *end)
{
    int begin_pos, end_pos, mid_pos;
    
    g_return_val_if_fail (begin != NULL, NULL);
    g_return_val_if_fail (end != NULL, NULL);
    g_return_val_if_fail (get_sequence (begin) == get_sequence (end), NULL);

    begin_pos = node_get_pos (begin);
    end_pos = node_get_pos (end);

    g_return_val_if_fail (end_pos >= begin_pos, NULL);
    
    mid_pos = begin_pos + (end_pos - begin_pos) / 2;

    return node_get_by_pos (begin, mid_pos);
}

gint
egg_sequence_iter_compare (EggSequenceIter *a,
			   EggSequenceIter *b)
{
    gint a_pos, b_pos;
    
    g_return_val_if_fail (a != NULL, 0);
    g_return_val_if_fail (b != NULL, 0);
    g_return_val_if_fail (get_sequence (a) == get_sequence (b), 0);
    
    check_iter_access (a);
    check_iter_access (b);
    
    a_pos = node_get_pos (a);
    b_pos = node_get_pos (b);
    
    if (a_pos == b_pos)
	return 0;
    else if (a_pos > b_pos)
	return 1;
    else
	return -1;
}

EggSequenceIter *
egg_sequence_append (EggSequence *seq,
		     gpointer     data)
{
    EggSequenceNode *node;
    
    g_return_val_if_fail (seq != NULL, NULL);
    
    check_seq_access (seq);
    
    node = node_new (data);
    node_insert_before (seq->end_node, node);
    
    return node;
}

EggSequenceIter *
egg_sequence_prepend (EggSequence *seq,
		      gpointer     data)
{
    EggSequenceNode *node, *first;
    
    g_return_val_if_fail (seq != NULL, NULL);
    
    check_seq_access (seq);
    
    node = node_new (data);
    first = node_get_first (seq->end_node);
    
    node_insert_before (first, node);
    
    return node;
}

EggSequenceIter *
egg_sequence_insert_before (EggSequenceIter *iter,
			    gpointer         data)
{
    EggSequenceNode *node;
    
    g_return_val_if_fail (iter != NULL, NULL);
    
    check_iter_access (iter);
    
    node = node_new (data);
    
    node_insert_before (iter, node);
    
    return node;
}

void
egg_sequence_remove (EggSequenceIter *iter)
{
    EggSequence *seq;
    
    g_return_if_fail (iter != NULL);
    g_return_if_fail (!is_end (iter));
    
    check_iter_access (iter);
    
    seq = get_sequence (iter); 
    
    node_unlink (iter);
    node_free (iter, seq);
}

void
egg_sequence_remove_range (EggSequenceIter *begin,
			   EggSequenceIter *end)
{
    g_return_if_fail (get_sequence (begin) == get_sequence (end));

    check_iter_access (begin);
    check_iter_access (end);
    
    egg_sequence_move_range (NULL, begin, end);
}

#if 0
static void
print_node (EggSequenceNode *node, int level)
{
    int i;

    for (i = 0; i < level; ++i)
	g_print ("  ");

    g_print ("%p\n", node);

    if (!node)
	return;
    
    print_node (node->left, level + 1);
    print_node (node->right, level + 1);
}

static EggSequenceNode *
get_root (EggSequenceNode *node)
{
    EggSequenceNode *root;

    root = node;
    while (root->parent)
	root = root->parent;
    return root;
}

static void
print_tree (EggSequence *seq)
{
    print_node (get_root (seq->end_node), 0);
}
#endif

/**
 * egg_sequence_move_range:
 * @dest: 
 * @begin: 
 * @end: 
 * 
 * Insert a range at the destination pointed to by ptr. The @begin and
 * @end iters must point into the same sequence. It is allowed for @dest to
 * point to a different sequence than the one pointed into by @begin and
 * @end. If @dest is NULL, the range indicated by @begin and @end is
 * removed from the sequence. If @dest iter points to a place within
 * the (@begin, @end) range, the range stays put.
 * 
 * Since: 2.12
 **/
void
egg_sequence_move_range (EggSequenceIter *dest,
			 EggSequenceIter *begin,
			 EggSequenceIter *end)
{
    EggSequence *src_seq;
    EggSequenceNode *first;
    
    g_return_if_fail (begin != NULL);
    g_return_if_fail (end != NULL);
    
    check_iter_access (begin);
    check_iter_access (end);
    if (dest)
	check_iter_access (dest);
    
    src_seq = get_sequence (begin);
    
    g_return_if_fail (src_seq == get_sequence (end));

#if 0
    if (dest && get_sequence (dest) == src_seq)
    {
	g_return_if_fail ((egg_sequence_iter_compare (dest, begin) <= 0)  ||
			  (egg_sequence_iter_compare (end, dest) <= 0));
    }
#endif

    /* Dest points to begin or end? */
    if (dest == begin || dest == end)
	return;

    /* begin comes after end? */
    if (egg_sequence_iter_compare (begin, end) >= 0)
	return;

    /* dest points somewhere in the (begin, end) range? */
    if (dest && get_sequence (dest) == src_seq &&
	egg_sequence_iter_compare (dest, begin) > 0 &&
	egg_sequence_iter_compare (dest, end) < 0)
    {
	return;
    }
    
    src_seq = get_sequence (begin);

    first = node_get_first (begin);

    node_cut (begin);

    node_cut (end);

    if (first != begin)
	node_insert_after (node_get_last (first), end);

    if (dest)
	node_insert_before (dest, begin);
    else
	node_free (begin, src_seq);
}

typedef struct
{
    GCompareDataFunc    cmp_func;
    gpointer		cmp_data;
    EggSequenceNode	*end_node;
} SortInfo;

/* This function compares two iters using a normal compare
 * function and user_data passed in in a SortInfo struct
 */
static gint
iter_compare (EggSequenceIter *node1,
	      EggSequenceIter *node2,
	      gpointer data)
{
    const SortInfo *info = data;
    gint retval;
    
    if (node1 == info->end_node)
	return 1;
    
    if (node2 == info->end_node)
	return -1;
    
    retval = info->cmp_func (node1->data, node2->data, info->cmp_data);
    
    return retval;
}

void
egg_sequence_sort (EggSequence      *seq,
		   GCompareDataFunc  cmp_func,
		   gpointer          cmp_data)
{
    SortInfo info = { cmp_func, cmp_data, seq->end_node };
    
    check_seq_access (seq);
    
    egg_sequence_sort_iter (seq, iter_compare, &info);
}

/**
 * egg_sequence_insert_sorted:
 * @seq: a #EggSequence
 * @data: the data to insert
 * @cmp_func: the #GCompareDataFunc used to compare elements in the queue. It is
 *     called with two elements of the @seq and @user_data. It should
 *     return 0 if the elements are equal, a negative value if the first
 *     element comes before the second, and a positive value if the second
 *     element comes before the first.
 * @cmp_data: user data passed to @cmp_func.
 * 
 * Inserts @data into @queue using @func to determine the new position.
 * 
 * Since: 2.10
 **/
EggSequenceIter *
egg_sequence_insert_sorted (EggSequence       *seq,
			    gpointer           data,
			    GCompareDataFunc   cmp_func,
			    gpointer           cmp_data)
{
    SortInfo info = { cmp_func, cmp_data, NULL };
    
    g_return_val_if_fail (seq != NULL, NULL);
    g_return_val_if_fail (cmp_func != NULL, NULL);
    
    info.end_node = seq->end_node;
    check_seq_access (seq);
    
    return egg_sequence_insert_sorted_iter (seq, data, iter_compare, &info);
}

void
egg_sequence_sort_changed (EggSequenceIter  *iter,
			   GCompareDataFunc  cmp_func,
			   gpointer          cmp_data)
{
    SortInfo info = { cmp_func, cmp_data, NULL };
    
    g_return_if_fail (!is_end (iter));
    
    info.end_node = get_sequence (iter)->end_node;
    check_iter_access (iter);
    
    egg_sequence_sort_changed_iter (iter, iter_compare, &info);
}

void
egg_sequence_sort_iter (EggSequence                *seq,
			EggSequenceIterCompareFunc  cmp_func,
			gpointer		    cmp_data)
{
    EggSequence *tmp;
    EggSequenceNode *begin, *end;
    
    g_return_if_fail (seq != NULL);
    g_return_if_fail (cmp_func != NULL);
    
    check_seq_access (seq);
    
    begin = egg_sequence_get_begin_iter (seq);
    end   = egg_sequence_get_end_iter (seq);
    
    tmp = egg_sequence_new (NULL);
    
    egg_sequence_move_range (egg_sequence_get_begin_iter (tmp), begin, end);
    
    tmp->access_prohibited = TRUE;
    seq->access_prohibited = TRUE;
    
    while (egg_sequence_get_length (tmp) > 0)
    {
	EggSequenceNode *node = egg_sequence_get_begin_iter (tmp);
	
	node_unlink (node);
	
	node_insert_sorted (seq->end_node, node, seq->end_node, cmp_func, cmp_data);
    }
    
    tmp->access_prohibited = FALSE;
    seq->access_prohibited = FALSE;
    
    egg_sequence_free (tmp);
}

void
egg_sequence_sort_changed_iter (EggSequenceIter            *iter,
				EggSequenceIterCompareFunc  iter_cmp,
				gpointer		    cmp_data)
{
    EggSequence *seq;
    EggSequenceIter *next, *prev;
    
    g_return_if_fail (!is_end (iter));
    
    check_iter_access (iter);

    /* If one of the neighbours is equal to iter, then
     * don't move it. This ensures that sort_changed() is
     * a stable operation.
     */

    next = node_get_next (iter);
    prev = node_get_prev (iter);

    if (prev != iter && iter_cmp (prev, iter, cmp_data) == 0)
	return;

    if (!is_end (next) && iter_cmp (next, iter, cmp_data) == 0)
	return;
    
    seq = get_sequence (iter);
    
    seq->access_prohibited = TRUE;
    
    node_unlink (iter);
    node_insert_sorted (seq->end_node, iter, seq->end_node, iter_cmp, cmp_data);
    
    seq->access_prohibited = FALSE;
}

EggSequenceIter *
egg_sequence_insert_sorted_iter   (EggSequence                *seq,
				   gpointer                    data,
				   EggSequenceIterCompareFunc  iter_cmp,
				   gpointer		       cmp_data)
{
    EggSequenceNode *new_node;
    
    check_seq_access (seq);
    
    new_node = node_new (data);
    node_insert_sorted (seq->end_node, new_node,
			seq->end_node, iter_cmp, cmp_data);
    return new_node;
}

EggSequenceIter *
egg_sequence_search_iter (EggSequence                *seq,
			  gpointer                    data,
			  EggSequenceIterCompareFunc  cmp_func,
			  gpointer                    cmp_data)
{
    EggSequenceNode *node;
    EggSequenceNode *dummy;
    
    g_return_val_if_fail (seq != NULL, NULL);
    
    check_seq_access (seq);
    
    seq->access_prohibited = TRUE;

    dummy = node_new (data);
    
    node = node_find_closest (seq->end_node, dummy,
			      seq->end_node, cmp_func, cmp_data);

    node_free (dummy, NULL);
    
    seq->access_prohibited = FALSE;
    
    return node;
}

/**
 * egg_sequence_search:
 * @seq: 
 * @data: 
 * @cmp_func: 
 * @cmp_data: 
 * 
 * Returns an iterator pointing to the position where @data would
 * be inserted according to @cmp_func and @cmp_data.
 * 
 * Return value: 
 * 
 * Since: 2.6
 **/
EggSequenceIter *
egg_sequence_search (EggSequence      *seq,
		     gpointer          data,
		     GCompareDataFunc  cmp_func,
		     gpointer          cmp_data)
{
    SortInfo info = { cmp_func, cmp_data, NULL };
    
    g_return_val_if_fail (seq != NULL, NULL);
    
    info.end_node = seq->end_node;
    check_seq_access (seq);
    
    return egg_sequence_search_iter (seq, data, iter_compare, &info);
}

EggSequence *
egg_sequence_iter_get_sequence (EggSequenceIter *iter)
{
    g_return_val_if_fail (iter != NULL, NULL);
    
    return get_sequence (iter);
}

gpointer
egg_sequence_get (EggSequenceIter *iter)
{
    g_return_val_if_fail (iter != NULL, NULL);
    g_return_val_if_fail (!is_end (iter), NULL);
    
    return iter->data;
}

void
egg_sequence_set (EggSequenceIter *iter,
		  gpointer         data)
{
    EggSequence *seq;
    
    g_return_if_fail (iter != NULL);
    g_return_if_fail (!is_end (iter));
    
    seq = get_sequence (iter);

    /* If @data is identical to iter->data, it is destroyed
     * here. This will work right in case of ref-counted objects. Also
     * it is similar to what ghashtables do.
     *
     * For non-refcounted data it's a little less convenient, but
     * code relying on self-setting not destroying would be
     * pretty dubious anyway ...
     */
    
    if (seq->data_destroy_notify)
	seq->data_destroy_notify (iter->data);
    
    iter->data = data;
}

gint
egg_sequence_get_length (EggSequence *seq)
{
    return node_get_length (seq->end_node) - 1;
}

EggSequenceIter *
egg_sequence_get_end_iter (EggSequence *seq)
{
    g_return_val_if_fail (seq != NULL, NULL);
    
    g_assert (is_end (seq->end_node));
    
    return seq->end_node;
}

EggSequenceIter *
egg_sequence_get_begin_iter (EggSequence *seq)
{
    g_return_val_if_fail (seq != NULL, NULL);
    return node_get_first (seq->end_node);
}

static int
clamp_position (EggSequence *seq,
		int          pos)
{
    gint len = egg_sequence_get_length (seq);
    
    if (pos > len || pos < 0)
	pos = len;
    
    return pos;
}

/*
 * if pos > number of items or -1, will return end pointer
 */
EggSequenceIter *
egg_sequence_get_iter_at_pos (EggSequence *seq,
			      gint         pos)
{
    g_return_val_if_fail (seq != NULL, NULL);
    
    pos = clamp_position (seq, pos);
    
    return node_get_by_pos (seq->end_node, pos);
}

void
egg_sequence_move (EggSequenceIter *src,
		   EggSequenceIter *dest)
{
    g_return_if_fail (src != NULL);
    g_return_if_fail (dest != NULL);
    g_return_if_fail (!is_end (src));
    
    if (src == dest)
	return;
    
    node_unlink (src);
    node_insert_before (dest, src);
}

/* EggSequenceIter * */
gboolean
egg_sequence_iter_is_end (EggSequenceIter *iter)
{
    g_return_val_if_fail (iter != NULL, FALSE);
    
    return is_end (iter);
}

gboolean
egg_sequence_iter_is_begin (EggSequenceIter *iter)
{
    return (node_get_prev (iter) == iter);
}

gint
egg_sequence_iter_get_position (EggSequenceIter *iter)
{
    g_return_val_if_fail (iter != NULL, -1);
    
    return node_get_pos (iter);
}

EggSequenceIter *
egg_sequence_iter_next (EggSequenceIter *iter)
{
    g_return_val_if_fail (iter != NULL, NULL);
    
    return node_get_next (iter);
}

EggSequenceIter *
egg_sequence_iter_prev (EggSequenceIter *iter)
{
    g_return_val_if_fail (iter != NULL, NULL);
    
    return node_get_prev (iter);
}

EggSequenceIter *
egg_sequence_iter_move (EggSequenceIter *iter,
			gint             delta)
{
    gint new_pos;
    
    g_return_val_if_fail (iter != NULL, NULL);
    
    new_pos = node_get_pos (iter) + delta;
    
    new_pos = clamp_position (get_sequence (iter), new_pos);
    
    return node_get_by_pos (iter, new_pos);
}

void
egg_sequence_swap (EggSequenceIter *a,
		   EggSequenceIter *b)
{
    EggSequenceNode *leftmost, *rightmost, *rightmost_next;
    int a_pos, b_pos;
    
    g_return_if_fail (!egg_sequence_iter_is_end (a));
    g_return_if_fail (!egg_sequence_iter_is_end (b));
    
    if (a == b)
	return;
    
    a_pos = egg_sequence_iter_get_position (a);
    b_pos = egg_sequence_iter_get_position (b);
    
    if (a_pos > b_pos)
    {
	leftmost = b;
	rightmost = a;
    }
    else
    {
	leftmost = a;
	rightmost = b;
    }
    
    rightmost_next = node_get_next (rightmost);
    
    /* Situation is now like this:
     *
     *     ..., leftmost, ......., rightmost, rightmost_next, ...
     *
     */
    egg_sequence_move (rightmost, leftmost);
    egg_sequence_move (leftmost, rightmost_next);
}

#if 0
/* aggregates */
void
egg_sequence_set_aggregate (EggSequence               *seq,
			    EggSequenceAggregateFunc   f,
			    gpointer                   data,
			    GDestroyNotify             destroy)
{
    /* FIXME */
}

void
egg_sequence_set_aggregate_data (EggSequenceIter *            iter,
				 const gchar             *aggregate,
				 gpointer                 data)
{
    /* FIXME */
    
}

gpointer
egg_sequence_get_aggregate_data (EggSequenceIter *            begin,
				 EggSequenceIter *            end,
				 const gchar             *aggregate)
{
    g_assert_not_reached();
    return NULL;
}
#endif



/*
 * Implementation of the node_* methods
 */
static void
node_update_fields (EggSequenceNode *node)
{
    g_assert (node != NULL);
    
    node->n_nodes = 1;
    
    if (node->left)
	node->n_nodes += node->left->n_nodes;
    
    if (node->right)
	node->n_nodes += node->right->n_nodes;
    
#if 0
    if (node->left || node->right)
	g_assert (node->n_nodes > 1);
#endif
}

#define NODE_LEFT_CHILD(n)  (((n)->parent) && ((n)->parent->left) == (n))
#define NODE_RIGHT_CHILD(n) (((n)->parent) && ((n)->parent->right) == (n))

static void
node_rotate (EggSequenceNode *node)
{
    EggSequenceNode *tmp, *old;
    
    g_assert (node->parent);
    g_assert (node->parent != node);
    
    if (NODE_LEFT_CHILD (node))
    {
	/* rotate right */
	tmp = node->right;
	
	node->right = node->parent;
	node->parent = node->parent->parent;
	if (node->parent)
	{
	    if (node->parent->left == node->right)
		node->parent->left = node;
	    else
		node->parent->right = node;
	}
	
	g_assert (node->right);
	
	node->right->parent = node;
	node->right->left = tmp;
	
	if (node->right->left)
	    node->right->left->parent = node->right;
	
	old = node->right;
    }
    else
    {
	/* rotate left */
	tmp = node->left;
	
	node->left = node->parent;
	node->parent = node->parent->parent;
	if (node->parent)
	{
	    if (node->parent->right == node->left)
		node->parent->right = node;
	    else
		node->parent->left = node;
	}
	
	g_assert (node->left);
	
	node->left->parent = node;
	node->left->right = tmp;
	
	if (node->left->right)
	    node->left->right->parent = node->left;
	
	old = node->left;
    }
    
    node_update_fields (old);
    node_update_fields (node);
}

static EggSequenceNode *
splay (EggSequenceNode *node)
{
    while (node->parent)
    {
	if (!node->parent->parent)
	{
	    /* zig */
	    node_rotate (node);
	}
	else if ((NODE_LEFT_CHILD (node) && NODE_LEFT_CHILD (node->parent)) ||
		 (NODE_RIGHT_CHILD (node) && NODE_RIGHT_CHILD (node->parent)))
	{
	    /* zig-zig */
	    node_rotate (node->parent);
	    node_rotate (node);
	}
	else
	{
	    /* zig-zag */
	    node_rotate (node);
	    node_rotate (node);
	}
    }
    
    return node;
}

static EggSequenceNode *
node_new (gpointer data)
{
    EggSequenceNode *node = g_new0 (EggSequenceNode, 1);

    node->parent = NULL;
    node->parent = NULL;
    node->left = NULL;
    node->right = NULL;
    
    node->data = data;
    node->n_nodes = 1;
    
    return node;
}

static EggSequenceNode *
find_min (EggSequenceNode *node)
{
    splay (node);
    
    while (node->left)
	node = node->left;
    
    return node;
}

static EggSequenceNode *
find_max (EggSequenceNode *node)
{
    splay (node);
    
    while (node->right)
	node = node->right;
    
    return node;
}

static EggSequenceNode *
node_get_first   (EggSequenceNode    *node)
{
    return splay (find_min (node));
}

static EggSequenceNode *
node_get_last    (EggSequenceNode    *node)
{
    return splay (find_max (node));
}

static gint
get_n_nodes (EggSequenceNode *node)
{
    if (node)
	return node->n_nodes;
    else
	return 0;
}

static EggSequenceNode *
node_get_by_pos  (EggSequenceNode *node,
		  gint             pos)
{
    gint i;
    
    g_assert (node != NULL);
    
    splay (node);
    
    while ((i = get_n_nodes (node->left)) != pos)
    {
	if (i < pos)
	{
	    node = node->right;
	    pos -= (i + 1);
	}
	else
	{
	    node = node->left;
	    g_assert (node->parent != NULL);
	}
    }
    
    return splay (node);
}

static EggSequenceNode *
node_get_prev  (EggSequenceNode    *node)
{
    splay (node);
    
    if (node->left)
    {
	node = node->left;
	while (node->right)
	    node = node->right;
    }
    
    return splay (node);
}

static EggSequenceNode *
node_get_next         (EggSequenceNode    *node)
{
    splay (node);
    
    if (node->right)
    {
	node = node->right;
	while (node->left)
	    node = node->left;
    }
    
    return splay (node);
}

static gint
node_get_pos (EggSequenceNode    *node)
{
    splay (node);
    
    return get_n_nodes (node->left);
}

/* Return closest node _strictly_ bigger than @needle (does always exist because
 * there is an end_node)
 */
static EggSequenceNode *
node_find_closest (EggSequenceNode	      *haystack,
		   EggSequenceNode	      *needle,
		   EggSequenceNode            *end,
		   EggSequenceIterCompareFunc  cmp_func,
		   gpointer		       cmp_data)
{
    EggSequenceNode *best;
    gint c;
    
    g_assert (haystack);
    
    haystack = splay (haystack);
    
    do
    {
	best = haystack;

	/* cmp_func can't be called with the end node (it may be user-supplied) */
	if (haystack == end)
	    c = 1;
	else
	    c = cmp_func (haystack, needle, cmp_data);

	/* In the following we don't break even if c == 0. Instaed we go on searching
	 * along the 'bigger' nodes, so that we find the last one that is equal
	 * to the needle.
	 */
	if (c > 0)
	    haystack = haystack->left;
	else
	    haystack = haystack->right;
    }
    while (haystack != NULL);
    
     /* If the best node is smaller or equal to the data, then move one step
     * to the right to make sure the best one is strictly bigger than the data
     */
    if (best != end && c <= 0)
	best = node_get_next (best);
    
    return best;
}

static void
node_free (EggSequenceNode *node,
	   EggSequence     *seq)
{
    GQueue *stack = g_queue_new ();

    splay (node);
    
    g_queue_push_head (stack, node);
    
    while (!g_queue_is_empty (stack))
    {
	node = g_queue_pop_head (stack);
	
	if (node)
	{
	    g_queue_push_head (stack, node->right);
	    g_queue_push_head (stack, node->left);
	    
	    if (seq && seq->data_destroy_notify && node != seq->end_node)
		seq->data_destroy_notify (node->data);
	    
	    g_free (node);
	}
    }
    
    g_queue_free (stack);
}

/* Splits into two trees, left and right. 
 * @node will be part of the right tree
 */

static void
node_cut (EggSequenceNode *node)
{
    splay (node);

    g_assert (node->parent == NULL);
    
    if (node->left)
	node->left->parent = NULL;
    
    node->left = NULL;
    node_update_fields (node);
}

static void
node_insert_before (EggSequenceNode *node,
		    EggSequenceNode *new)
{
    g_assert (node != NULL);
    g_assert (new != NULL);
    
    splay (node);
    
    new = splay (find_min (new));
    g_assert (new->left == NULL);
    
    if (node->left)
	node->left->parent = new;
    
    new->left = node->left;
    new->parent = node;
    
    node->left = new;
    
    node_update_fields (new);
    node_update_fields (node);
}

static void
node_insert_after (EggSequenceNode *node,
		   EggSequenceNode *new)
{
    g_assert (node != NULL);
    g_assert (new != NULL);
    
    splay (node);
    
    new = splay (find_max (new));
    g_assert (new->right == NULL);
    g_assert (node->parent == NULL);
    
    if (node->right)
	node->right->parent = new;
    
    new->right = node->right;
    new->parent = node;
    
    node->right = new;
    
    node_update_fields (new);
    node_update_fields (node);
}

static gint
node_get_length (EggSequenceNode    *node)
{
    g_assert (node != NULL);
    
    splay (node);
    return node->n_nodes;
}

static void
node_unlink (EggSequenceNode *node)
{
    EggSequenceNode *right, *left;
    
    splay (node);
    
    left = node->left;
    right = node->right;
    
    node->parent = node->left = node->right = NULL;
    node_update_fields (node);
    
    if (right)
    {
	right->parent = NULL;
	
	right = node_get_first (right);
	g_assert (right->left == NULL);
	
	right->left = left;
	if (left)
	{
	    left->parent = right;
	    node_update_fields (right);
	}
    }
    else if (left)
    {
	left->parent = NULL;
    }
}

static void
node_insert_sorted (EggSequenceNode *node,
		    EggSequenceNode *new,
		    EggSequenceNode *end,
		    EggSequenceIterCompareFunc cmp_func,
		    gpointer cmp_data)
{
    EggSequenceNode *closest;
    
    closest = node_find_closest (node, new, end, cmp_func, cmp_data);
    
    node_insert_before (closest, new);
}

static gint
node_calc_height (EggSequenceNode *node)
{
    gint left_height;
    gint right_height;
    
    if (node)
    {
	left_height = 0;
	right_height = 0;
	
	if (node->left)
	    left_height = node_calc_height (node->left);
	
	if (node->right)
	    right_height = node_calc_height (node->right);
	
	return MAX (left_height, right_height) + 1;
    }
    
    return 0;
}

gint
egg_sequence_calc_tree_height   (EggSequence               *seq)
{
    EggSequenceNode *node = seq->end_node;
    gint r, l;
    while (node->parent)
	node = node->parent;
    
    if (node)
    {
	r = node_calc_height (node->right);
	l = node_calc_height (node->left);
	
	return MAX (r, l) + 1;
    }
    else
	return 0;
}

static void
check_node (EggSequenceNode *node)
{
    if (node)
    {
	g_assert (node->parent != node);
	g_assert (node->n_nodes ==
		  1 + get_n_nodes (node->left) + get_n_nodes (node->right));
	check_node (node->left);
	check_node (node->right);
    }
}

void
egg_sequence_self_test (EggSequence *seq)
{
    EggSequenceNode *node = splay (seq->end_node);
    
    check_node (node);
}

#if 0
void
egg_sequence_set_aggregator   (EggSequence                  *seq,
			       EggSequenceAggregateFunction  func,
			       gpointer			     data,
			       GDestroyNotify                destroy)
{
    
}

gconstpointer egg_sequence_get_aggregate    (EggSequenceIter *              begin,
					     EggSequenceIter *              end);
void          egg_sequence_update_aggregate (EggSequenceIter *              iter);
#endif
