/*
 * SPDX-FileCopyrightText: (C) 2008 Novell, Inc.
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/* Manages bags of weakly-referenced GObjects. */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_OBJECT_BAG_H
#define CAMEL_OBJECT_BAG_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _CamelObjectBag CamelObjectBag;
typedef gpointer (*CamelCopyFunc) (gconstpointer object);

CamelObjectBag *camel_object_bag_new		(GHashFunc key_hash_func,
						 GEqualFunc key_equal_func,
						 CamelCopyFunc key_copy_func,
						 GFreeFunc key_free_func);
gpointer	camel_object_bag_get		(CamelObjectBag *bag,
						 gconstpointer key);
gpointer	camel_object_bag_peek		(CamelObjectBag *bag,
						 gconstpointer key);
gpointer	camel_object_bag_reserve	(CamelObjectBag *bag,
						 gconstpointer key);
void		camel_object_bag_add		(CamelObjectBag *bag,
						 gconstpointer key,
						 gpointer object);
void		camel_object_bag_abort		(CamelObjectBag *bag,
						 gconstpointer key);
void		camel_object_bag_rekey		(CamelObjectBag *bag,
						 gpointer object,
						 gconstpointer new_key);
GPtrArray *	camel_object_bag_list		(CamelObjectBag *bag);
void		camel_object_bag_remove		(CamelObjectBag *bag,
						 gpointer object);
void		camel_object_bag_destroy	(CamelObjectBag *bag);

G_END_DECLS

#endif /* CAMEL_OBJECT_BAG_H */
