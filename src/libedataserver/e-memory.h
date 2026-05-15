/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 * SPDX-FileContributor: Jacob Berkman <jacob@ximian.com>
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_MEMORY_H
#define E_MEMORY_H

#include <glib.h>

G_BEGIN_DECLS

/* memchunks - allocate/free fixed-size blocks of memory */
/* this is like gmemchunk, only faster and less overhead (only 4 bytes for every atomcount allocations) */
typedef struct _EMemChunk EMemChunk;

EMemChunk *	e_memchunk_new			(gint atomcount,
						 gint atomsize);
gpointer	e_memchunk_alloc		(EMemChunk *memchunk);
gpointer	e_memchunk_alloc0		(EMemChunk *memchunk);
void		e_memchunk_free			(EMemChunk *memchunk,
						 gpointer mem);
void		e_memchunk_empty		(EMemChunk *memchunk);
void		e_memchunk_clean		(EMemChunk *memchunk);
void		e_memchunk_destroy		(EMemChunk *memchunk);

G_END_DECLS

#endif /* E_MEMORY_H */
