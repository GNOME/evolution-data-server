/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 * SPDX-FileContributor: Jacob Berkman <jacob@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MEMCHUNK_H
#define CAMEL_MEMCHUNK_H

#include <glib.h>

G_BEGIN_DECLS

/* memchunks - allocate/free fixed-size blocks of memory */
/* this is like gmemchunk, only faster and less overhead (only 4 bytes for every atomcount allocations) */
typedef struct _CamelMemChunk CamelMemChunk;

CamelMemChunk *	camel_memchunk_new		(gint atomcount,
						 gint atomsize);
gpointer	camel_memchunk_alloc		(CamelMemChunk *memchunk);
gpointer	camel_memchunk_alloc0		(CamelMemChunk *memchunk);
void		camel_memchunk_free		(CamelMemChunk *memchunk,
						 gpointer mem);
void		camel_memchunk_empty		(CamelMemChunk *memchunk);
void		camel_memchunk_clean		(CamelMemChunk *memchunk);
void		camel_memchunk_destroy		(CamelMemChunk *memchunk);

G_END_DECLS

#endif /* CAMEL_MEMCHUNK_H */
