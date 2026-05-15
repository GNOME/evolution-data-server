/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_TRIE_H
#define CAMEL_TRIE_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _CamelTrie CamelTrie;

CamelTrie *	camel_trie_new			(gboolean icase);
void		camel_trie_free			(CamelTrie *trie);
void		camel_trie_add			(CamelTrie *trie,
						 const gchar *pattern,
						 gint pattern_id);
const gchar *	camel_trie_search		(CamelTrie *trie,
						 const gchar *buffer,
						 gsize buflen,
						 gint *matched_id);

G_END_DECLS

#endif /* CAMEL_TRIE_H */
