/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *	    Jacob Berkman <jacob@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef _E_MEMORY_H
#define _E_MEMORY_H

#include <glib.h>

G_BEGIN_DECLS

/* memchunks - allocate/free fixed-size blocks of memory */
/* this is like gmemchunk, only faster and less overhead (only 4 bytes for every atomcount allocations) */
typedef struct _EMemChunk EMemChunk;

EMemChunk *e_memchunk_new(gint atomcount, gint atomsize);
gpointer e_memchunk_alloc(EMemChunk *m);
gpointer e_memchunk_alloc0(EMemChunk *m);
void e_memchunk_free(EMemChunk *m, gpointer mem);
void e_memchunk_empty(EMemChunk *m);
void e_memchunk_clean(EMemChunk *m);
void e_memchunk_destroy(EMemChunk *m);

/* mempools - allocate variable sized blocks of memory, and free as one */
/* allocation is very fast, but cannot be freed individually */
typedef struct _EMemPool EMemPool;
typedef enum {
	E_MEMPOOL_ALIGN_STRUCT = 0,	/* allocate to native structure alignment */
	E_MEMPOOL_ALIGN_WORD = 1,	/* allocate to words - 16 bit alignment */
	E_MEMPOOL_ALIGN_BYTE = 2,	/* allocate to bytes - 8 bit alignment */
	E_MEMPOOL_ALIGN_MASK = 3	/* which bits determine the alignment information */
} EMemPoolFlags;

EMemPool *e_mempool_new(gint blocksize, gint threshold, EMemPoolFlags flags);
gpointer e_mempool_alloc(EMemPool *pool, gint size);
gchar *e_mempool_strdup(EMemPool *pool, const gchar *str);
void e_mempool_flush(EMemPool *pool, gint freeall);
void e_mempool_destroy(EMemPool *pool);

/* strv's string arrays that can be efficiently modified and then compressed mainly for retrival */
/* building is relatively fast, once compressed it takes the minimum amount of memory possible to store */
typedef struct _EStrv EStrv;

EStrv *e_strv_new(gint size);
EStrv *e_strv_set_ref(EStrv *strv, gint index, gchar *str);
EStrv *e_strv_set_ref_free(EStrv *strv, gint index, gchar *str);
EStrv *e_strv_set(EStrv *strv, gint index, const gchar *str);
EStrv *e_strv_pack(EStrv *strv);
const gchar *e_strv_get(EStrv *strv, gint index);
void e_strv_destroy(EStrv *strv);

/* poolv's are similar to strv's, but they store common strings */
typedef struct _EPoolv EPoolv;

EPoolv *e_poolv_new(guint size);
EPoolv *e_poolv_cpy(EPoolv *dest, const EPoolv *src);
EPoolv *e_poolv_set(EPoolv *poolv, gint index, gchar *str, gint freeit);
const gchar *e_poolv_get(EPoolv *poolv, gint index);
void e_poolv_destroy(EPoolv *poolv);

G_END_DECLS

#endif /* _E_MEMORY_H */
