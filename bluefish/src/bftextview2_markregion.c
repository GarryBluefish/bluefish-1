/* Bluefish HTML Editor
 * bftextview2_markregion.c
 *
 * Copyright (C) 2013 Olivier Sessink
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "bluefish.h"
#include "bf_lib.h"
#include "bftextview2_private.h"
#include "bftextview2_markregion.h"

#ifdef MARKREGION

typedef struct {
	BF_ELIST_HEAD;
	guint32 pos;
	guint32 is_start; /* a guint8 would have been good enough, but
								both on 32bit or 64 bit systems that doesn't affect
								the size of Tchange, and this is faster on 32bit
								systems */
} Tchange;
#define CHANGE(var) ((Tchange *)var)

#ifdef DEVELOPMENT

void
bftextview2_dump_needscanning(BluefishTextView *btv) {
	gboolean cont=TRUE;
	GtkTextIter start,end;
	gtk_text_buffer_get_start_iter(btv->buffer, &start);
	while (cont) {
		if (!gtk_text_iter_begins_tag(&start, btv->needscanning)) {
			if (!gtk_text_iter_forward_to_tag_toggle(&start, btv->needscanning)) {
				cont=FALSE;
				break;
			}
		}
		end = start;
		gtk_text_iter_forward_char(&end);
		if (!gtk_text_iter_ends_tag(&end, btv->needscanning)) {
			if (!gtk_text_iter_forward_to_tag_toggle(&end, btv->needscanning)) {
				g_print("huh2?\n");
			}
		}
		g_print("needscanning %u:%u\n",gtk_text_iter_get_offset(&start),gtk_text_iter_get_offset(&end));
		start = end;
		cont = gtk_text_iter_forward_char(&start);
	}
}


static void
markregion_verify_integrity(Tregions *rg)
{
	Tchange *end, *start = rg->head;
	g_print("\n*****\n");
	while (start) {
		if (start->is_start ==FALSE) {
			g_print("ERROR: markregion_verify_integrity, entry(%u)->is_start==FALSE but should start a region?!?\n",start->pos);
			g_assert_not_reached();
		}
		end = start->next;
		if (!end) {
			g_print("ERROR: markregion_verify_integrity, entry(%u) does not have an end?!?\n",start->pos);
			g_assert_not_reached();
		}
		g_print("region %u:%u\n",start->pos,end->pos);
	
		if (end->is_start == TRUE) {
			g_print("ERROR: markregion_verify_integrity, entry(%u)->is_start==TRUE but should end a region?!?\n",end->pos);
			g_assert_not_reached();
		}
		if (start->pos >= end->pos) {
			g_print("ERROR: markregion_verify_integrity, start at %u but end at %u ?!?\n",start->pos,end->pos);
			g_assert_not_reached();
		}
		start = end->next;
		if (start) {
			if (end->pos >= start->pos) {
				g_print("ERROR: markregion_verify_integrity, next start at %u but last end at %u ?!?\n",start->pos,end->pos);
				g_assert_not_reached();
			}
		}
	}
	g_print("*****\n");
}
#endif

static Tchange *
new_change(guint pos, gboolean is_start)
{
	Tchange *change = g_slice_new(Tchange);
	change->next = change->prev = NULL;
	change->pos = pos;
	change->is_start = is_start;
	return change;
}
/*
static void
insert_start_and_end(Tregions *rg, guint start, guint end)
{
	Tchange *tmp = rg->head;
	/ * insert start * /
	if (tmp->pos > start) {
		rg->head = bf_elist_prepend(rg->head, new_change(start, TRUE));
		tmp = rg->head;
	} else {
		while (tmp && tmp->pos < start) {
			tmp = tmp->next;
		}
		/ * if tmp->is_start == FALSE we don't need to insert a new start, because
		the previous start is valid
		if there is no tmp we append to the tail * /
		if (!tmp) {
			rg->tail = tmp = CHANGE(bf_elist_append(BF_ELIST(rg->tail), BF_ELIST(new_change(start, TRUE))));
		} else if (tmp->is_start == TRUE) {
			tmp = (Tchange *)bf_elist_prepend(BF_ELIST(tmp), new_change(start, TRUE));
		} else {
			tmp = tmp->prev;
		}
	}
	/ * tmp now points to the start position, continue to the end position * /
	while (tmp && tmp->pos < end) {
		Tchange *toremove = tmp;
		tmp = CHANGE(bf_elist_remove(BF_ELIST(toremove))); / * returns the previous entry if there is one * /
		g_slice_free(Tchange, toremove);
		tmp = tmp->next;
	}
	if (!tmp) {
		rg->tail = bf_elist_append(BF_ELIST(rg->tail), BF_ELIST(new_change(end, FALSE)));
		return;
	}

	/ * if tmp->is_start == FALSE we do not have to do anything:
		the already existing region ends on or beyond the region we are ending now, so use
		the existing end and merge them together * /
	if (tmp->is_start == TRUE) {
		if (tmp->pos == end) {
			Tchange *toremove = tmp;
			/ * the end of the current region starts the next region, simply remove
			the start of the next region so they form a continuous region * /
			bf_elist_remove(BF_ELIST(toremove));
			g_slice_free(Tchange, toremove);
		} else {
			bf_elist_prepend(BF_ELIST(tmp), BF_ELIST(new_change(end, FALSE)));
		}
	}
}

void
mark_region_changed_real(Tregions *rg, guint start, guint end)
{
	g_print("mark_region_changed, %u:%u\n",start,end);
	if (!rg->head) {
		rg->head = new_change(start, TRUE);
		rg->tail = bf_elist_append(BF_ELIST(rg->head), BF_ELIST(new_change(end, FALSE)));
		return;
	}

	if (CHANGE(rg->tail)->pos < start) {
		rg->tail = bf_elist_append(BF_ELIST(rg->tail), BF_ELIST(new_change(start, TRUE)));
		rg->tail = bf_elist_append(BF_ELIST(rg->tail), BF_ELIST(new_change(end, FALSE)));
		return;
	} else if (CHANGE(rg->head)->pos > end){
		rg->head = bf_elist_prepend(BF_ELIST(rg->head), BF_ELIST(new_change(end, FALSE)));
		rg->head = bf_elist_prepend(BF_ELIST(rg->head), BF_ELIST(new_change(start, TRUE)));
		return;
	}

	insert_start_and_end(rg, start, end);
}*/

void markregion_region_done(Tregions *rg, guint end)
{
	Tchange *tmp;
	g_print("mark_region_done, end=%u\n",end);
	if (!rg->head) {
		return;
	}
	tmp = rg->head;
	while (tmp && tmp->pos <= end) {
		g_print("mark_region_done, remove change with pos=%u\n",tmp->pos);
		tmp = CHANGE(bf_elist_remove(BF_ELIST(tmp))); /* returns the previous entry if that exists, but
										it does not exist in this case because we remove all entries */
	}
	if (tmp && tmp->is_start == FALSE) {
		g_print("next change at %u is a end!\n",tmp->pos);
		if (tmp->pos == end) {
			Tchange *toremove = tmp;
			g_print("mark_region_done, remove change with pos=%u\n",toremove->pos);
			tmp = CHANGE(bf_elist_remove(BF_ELIST(toremove)));
			g_slice_free(Tchange, toremove);
		} else {
			g_print("mark_region_done, prepend change with end=%u\n",end);
			tmp = CHANGE(bf_elist_prepend(BF_ELIST(tmp), BF_ELIST(new_change(end, TRUE))));
		}
	} else {
#ifdef DEVELOPMENT
		if (tmp) {
			g_print("mark_region_done, keep %u, is_start=%d\n",tmp->pos,tmp->is_start);
		}
#endif
	}
	rg->head = tmp;
	rg->last = NULL;
	if (tmp == NULL) {
		rg->tail = NULL;
	}
}
/*
gpointer
update_offset_real(Tregions *rg, gpointer cur, guint start , gint offset, guint nextpos)
{
	g_print("update_offset, start=%u, offset=%d, nextpos=%u\n",start,offset,nextpos);
	if (cur == NULL) {
		cur = rg->head;
		while (cur && CHANGE(cur)->pos <= start) {
			cur = CHANGE(cur)->next;
		}
	}
	g_print("update_offset, start at cur->pos=%u\n",cur ? CHANGE(cur)->pos : -1);
	while (cur && CHANGE(cur)->pos+offset < nextpos) {
		g_print("update_offset, update cur->pos=%u to %u\n",CHANGE(cur)->pos, CHANGE(cur)->pos + offset);
		CHANGE(cur)->pos += offset;
		cur = CHANGE(cur)->next;
	}
	return cur;
}

typedef enum {
	cache_changed,
	cache_offset
} Tcachetype;

typedef struct {
	BF_ELIST_HEAD;
	Tcachetype type;
	gint val1;
	gint val2;
} Tcache;

static guint
cache_get_nextpos(Tcache *startch)
{
	Tcache *ch = startch->next;
	while (ch) {
		if (ch->type == cache_offset) {
			return ch->val1;
		}
		ch = ch->next;
	}
	return BF2_OFFSET_UNDEFINED;
}

static gboolean
process_cache(Tregions *rg)
{
	Tcache *ch, *nextch;
	gpointer cur=NULL;
	gint handleoffset=0;
	if (!rg->cachehead)
		return FALSE;

	ch = rg->cachehead;
	while (ch) {
		if (ch->type == cache_changed) {
			mark_region_changed_real(rg, ch->val1, ch->val2);
		} else if (ch->type == cache_offset) {
			guint nextpos = cache_get_nextpos(ch);
			handleoffset += ch->val2;
			cur = update_offset_real(rg, cur, ch->val1 , handleoffset, nextpos);
		} else {
#ifdef DEVELOPMENT
			g_assert_not_reached();
#endif
		}
		nextch = ch->next;
		g_slice_free(Tcache, ch);
		ch = nextch;
	}
	rg->cachehead = rg->cachetail = NULL;
	return TRUE;
}

static void
add_to_cache(Tregions *rg, Tcachetype type, gint val1, gint val2)
{
	Tcache *ch = g_slice_new(Tcache);
	ch->prev = ch->next = NULL;
	ch->val1 = val1;
	ch->val2 = val2;
	ch->type = type;
	rg->cachetail = bf_elist_append(rg->cachetail, ch);
	if (!rg->cachehead)
		rg->cachehead = rg->cachetail;
}

void
mark_region_changed(Tregions *rg, guint start, guint end)
{
	add_to_cache(rg, cache_changed, start, end);
}

void
update_offset(Tregions *rg, guint start , gint offset)
{
	add_to_cache(rg, cache_offset, start, offset);
}*/

static void
update_offset(Tchange *start, gint offset)
{
	if (offset == 0)
		return;
	while (start) {
		start->pos += offset;
		start = start->next;
	}
}

static Tchange *
find_prev_or_equal_position(Tregions *rg, guint position)
{
	if (!rg->last) {
		if (position - CHANGE(rg->head)->pos < CHANGE(rg->tail)->pos - position) {
			rg->last = rg->head;
		} else {
			rg->last = rg->tail;
		}
	}
#ifdef DEVELOPMENT
	if (!rg->last) {
		g_print("ABORT: find_prev_or_equal_position, rg->last==NULL ?!?!?!\n");
		g_assert_not_reached();
	}
#endif
	if (CHANGE(rg->last)->pos == position)
		return CHANGE(rg->last);
	
	if (CHANGE(rg->last)->pos > position) {
		g_print("find_prev_or_equal_position, current position %u is beyond the requested position %u\n",CHANGE(rg->last)->pos,position);
		while (rg->last && CHANGE(rg->last)->pos > position) {
			g_print("backward start %p(%u) to %p\n",CHANGE(rg->last),CHANGE(rg->last)->pos,CHANGE(rg->last)->prev);
			rg->last = CHANGE(rg->last)->prev;
		}
	} else {
		Tchange *next = CHANGE(rg->last)->next;
		g_print("find_prev_or_equal_position, current position %u is before the requested position %u\n",CHANGE(rg->last)->pos,position);
		while (next && next->pos <= position) {
			g_print("forward start %p(%u) to %p(%u), next->next=%p\n",CHANGE(rg->last),CHANGE(rg->last)->pos,next,next->pos,next->next);
			rg->last = next;
			next = next->next;
		}
	}
	g_print("find_prev_or_equal_position, requested position %u, returning change->pos=%d\n",position,rg->last?CHANGE(rg->last)->pos:-1);
	return CHANGE(rg->last);
}

static gboolean
markregion_handle_generic(Tregions *rg, guint markstart, guint markend, guint comparepos, gint offset)
{
	if (!rg->head) {
		/* empty region, just append the start and end */
		g_print("markregion_handle_generic, empty, just add the first entries\n");
		rg->head = new_change(markstart, TRUE);
		rg->last = rg->tail = bf_elist_append(BF_ELIST(rg->head), BF_ELIST(new_change(markend, FALSE)));
		return TRUE;
	}
	if (CHANGE(rg->tail)->pos < markstart) {
		g_print("markregion_handle_generic, beyond the end, append new entries\n");
		/* the new region is beyond the end */
		rg->last = rg->tail = bf_elist_append(BF_ELIST(rg->tail), BF_ELIST(new_change(markstart, TRUE)));
		rg->tail = bf_elist_append(BF_ELIST(rg->tail), BF_ELIST(new_change(markend, FALSE)));
		return TRUE;
	}
	if (CHANGE(rg->head)->pos > comparepos) {
		Tchange *oldhead = rg->head;
		g_print("markregion_handle_generic, before the start, prepend new entries and update offsets\n");
		/* the first region is before the current start */
		rg->last = rg->head = bf_elist_prepend(BF_ELIST(rg->head), BF_ELIST(new_change(markend, FALSE)));
		rg->head = bf_elist_prepend(BF_ELIST(rg->head), BF_ELIST(new_change(markstart, TRUE)));
		update_offset(oldhead, offset);
		return TRUE;
	}
	return FALSE;
}

void
markregion_insert(Tregions *rg, guint markstart, guint markend)
{
	gint offset = markend-markstart;
#ifdef DEVELOPMENT
	markregion_verify_integrity(rg);
#endif
	g_print("markregion_insert, markstart=%u, markend=%u, offset=%d\n",markstart,markend,offset);
	
	if (markregion_handle_generic(rg, markstart, markend, markstart, offset))
		return;
	
	/* insert somewhere within the existing regions */ 
	rg->last = find_prev_or_equal_position(rg, markstart);
	if (CHANGE(rg->last)->is_start == TRUE) {
		/* only update the offset */
		update_offset(CHANGE(rg->last)->next, offset);
		return;
	}
	if (CHANGE(rg->last)->pos == markstart) {
		/* out current start previously ended a region, merge the old and new regions together */
 		rg->last = bf_elist_remove(rg->last); /* returns 'change->prev' if there is one */
		rg->last = bf_elist_append(BF_ELIST(rg->last), BF_ELIST(new_change(markend, FALSE)));
		update_offset(CHANGE(rg->last)->next, offset);
		return;
	}
	if (CHANGE(rg->last)->pos < markstart) {
		rg->last = bf_elist_append(BF_ELIST(rg->last), BF_ELIST(new_change(markstart, TRUE)));
		rg->last = bf_elist_append(BF_ELIST(rg->last), BF_ELIST(new_change(markend, FALSE)));
		update_offset(CHANGE(rg->last)->next, offset);
		return;
	}
#ifdef DEVELOPMENT
	g_print("ABORT: markregion_insert, we should never get to the end of this function\n");
	g_assert_not_reached();
#endif
}

void
markregion_delete(Tregions *rg, guint markstart, guint markend, gint offset)
{
	gint comparepos = markend-offset;

#ifdef DEVELOPMENT
	markregion_verify_integrity(rg);
#endif
	g_print("markregion_delete, %u:%u, update offset with %d, comparepos=%d. head(%d)|tail(%d)\n",markstart,markend,offset,comparepos
						,rg->head?CHANGE(rg->head)->pos:-1
						,rg->tail?CHANGE(rg->tail)->pos:-1);
	
	if (markregion_handle_generic(rg, markstart, markend, comparepos, offset))
		return;

	rg->last = find_prev_or_equal_position(rg, markstart);
	if (rg->last == NULL) {
		/* starts before head */
		rg->last = rg->head = bf_elist_prepend(BF_ELIST(rg->head), BF_ELIST(new_change(markstart, TRUE)));
	}
	
	if (CHANGE(rg->last)->is_start == FALSE) {
		Tchange *oldlast = rg->last;
		rg->last = CHANGE(rg->last)->next;
		/* first remove anything in the deleted area */
		while(rg->last && CHANGE(rg->last)->pos < comparepos) {
			rg->last = bf_elist_remove(BF_ELIST(rg->last)); /* returns 'change->prev' if there is one */
			rg->last = CHANGE(rg->last)->next;
		}
		if (oldlast->pos == markstart) {
			/* existing region ends at our start, merge the previous and this region together */
			oldlast = CHANGE(bf_elist_remove(BF_ELIST(oldlast))); /* returns 'change->prev' if there is one */
		} else {
			/* existing region ended before our start, start a new region */
			oldlast = CHANGE(bf_elist_append(BF_ELIST(oldlast), BF_ELIST(new_change(markstart, TRUE))));
		}
		oldlast = CHANGE(bf_elist_append(BF_ELIST(oldlast), BF_ELIST(new_change(markend, FALSE))));
		if (!rg->last) { /* we deleted up to the end, so tail is a dangling pointer */
			rg->last = rg->tail = oldlast;
			return;
		}
		update_offset(rg->last, offset);
		return;
	}
	if (CHANGE(rg->last)->is_start == TRUE) {
		Tchange *oldlast = rg->last;
		rg->last = CHANGE(rg->last)->next;
		while(rg->last && CHANGE(rg->last)->pos < comparepos) {
			rg->last = bf_elist_remove(BF_ELIST(rg->last)); /* returns 'change->prev' if there is one */
			rg->last = CHANGE(rg->last)->next;
		}
		if (!rg->last) {
			rg->tail = bf_elist_append(BF_ELIST(oldlast), BF_ELIST(new_change(markend, FALSE)));
			rg->last = oldlast;
			return;
		}
		if (CHANGE(rg->last)->is_start == TRUE) {
			if (CHANGE(rg->last)->pos == comparepos) { /* merge the regions */
				rg->last = bf_elist_remove(rg->last); /* returns 'change->prev' if there is one */
			} else /*if (CHANGE(rg->last)->pos > comparepos) */{
				rg->last = bf_elist_prepend(BF_ELIST(rg->last), BF_ELIST(new_change(markend, FALSE)));
			}
			update_offset(CHANGE(rg->last)->next, offset);
		} else {
			update_offset(rg->last, offset);
		}
		return;
	}
#ifdef DEVELOPMENT
	g_print("ABORT: markregion_delete, we should never get to the end of this function\n");
	g_assert_not_reached();
#endif
}

void
markregion_nochange(Tregions *rg, guint markstart, guint markend)
{
	g_print("markregion_nochange, markstart=%u, markend=%u\n",markstart,markend);
	markregion_delete(rg, markstart, markend, 0);
}

gpointer
markregion_get_region(Tregions *rg, gpointer cur, guint *start, guint *end)
{
#ifdef DEVELOPMENT
	markregion_verify_integrity(rg);
#endif

	if (cur == NULL) {
		if (rg->head==NULL) {
			*start = BF2_OFFSET_UNDEFINED;
			*end = BF2_OFFSET_UNDEFINED;
			return NULL;
		}
		cur = rg->head;
	}

#ifdef DEVELOPMENT
	if (!CHANGE(cur)->is_start) {
		g_print("ABORT: get_region is called, and cur is not a start of region\n");
		g_assert_not_reached();
	}
#endif
	*start = CHANGE(cur)->pos;
	cur = CHANGE(cur)->next;
#ifdef DEVELOPMENT
	if (!cur) {
		g_print("ABORT: get_region is called, and next cur does not exist\n");
		g_assert_not_reached();
	}
	if (CHANGE(cur)->is_start) {
		g_print("ABORT: get_region is called, and next cur is a start of region\n");
		g_assert_not_reached();
	}
#endif
	*end = CHANGE(cur)->pos;
	cur = CHANGE(cur)->next;
	g_print("markregion_get_region, start=%u,end=%u,cur->pos(%d)\n",*start,*end,cur?CHANGE(cur)->pos:-1);
	return cur;
}


#endif
