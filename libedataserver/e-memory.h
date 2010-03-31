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

#ifndef E_MEMORY_H
#define E_MEMORY_H

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

G_END_DECLS

#endif /* E_MEMORY_H */
