/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Bertrand Guiheneuf <bertrand@helixcode.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_UID_CACHE_H
#define CAMEL_UID_CACHE_H

#include <glib.h>

#include <stdio.h>
#include <sys/types.h>

G_BEGIN_DECLS

typedef struct {
	gchar *filename;
	GHashTable *uids;
	guint level;
	gsize expired;
	gsize size;
	gint fd;
} CamelUIDCache;

CamelUIDCache *camel_uid_cache_new (const gchar *filename);
gboolean camel_uid_cache_save (CamelUIDCache *cache);
void camel_uid_cache_destroy (CamelUIDCache *cache);

GPtrArray *camel_uid_cache_dup_new_uids (CamelUIDCache *cache, GPtrArray *uids);

void camel_uid_cache_save_uid (CamelUIDCache *cache, const gchar *uid);

G_END_DECLS

#endif /* CAMEL_UID_CACHE_H */
