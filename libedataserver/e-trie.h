/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

/* This API has been moved to Camel. */

#ifndef __E_TRIE_H__
#define __E_TRIE_H__

#ifndef EDS_DISABLE_DEPRECATED

#include <glib.h>

G_BEGIN_DECLS

typedef struct _ETrie ETrie;

ETrie *e_trie_new (gboolean icase);
void e_trie_free (ETrie *trie);

void e_trie_add (ETrie *trie, const gchar *pattern, gint pattern_id);

const gchar *e_trie_search (ETrie *trie, const gchar *buffer, gsize buflen, gint *matched_id);

G_END_DECLS

#endif /* EDS_DISABLE_DEPRECATED */

#endif /* __E_TRIE_H__ */
