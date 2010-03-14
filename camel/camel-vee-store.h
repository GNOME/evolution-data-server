/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _CAMEL_VEE_STORE_H
#define _CAMEL_VEE_STORE_H

#include <glib.h>
#include <camel/camel-store.h>

#define CAMEL_VEE_STORE(obj)         CAMEL_CHECK_CAST (obj, camel_vee_store_get_type (), CamelVeeStore)
#define CAMEL_VEE_STORE_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_vee_store_get_type (), CamelVeeStoreClass)
#define CAMEL_IS_VEE_STORE(obj)      CAMEL_CHECK_TYPE (obj, camel_vee_store_get_type ())

G_BEGIN_DECLS

typedef struct _CamelVeeStore      CamelVeeStore;
typedef struct _CamelVeeStoreClass CamelVeeStoreClass;

/* open mode for folder, vee folder auto-update */
#define CAMEL_STORE_VEE_FOLDER_AUTO (1<<16)

/**
 * CAMEL_STORE_VEE_FOLDER_SPECIAL:
 *
 * Since: 2.24
 **/
#define CAMEL_STORE_VEE_FOLDER_SPECIAL (1<<17)

struct _CamelVeeStore {
	CamelStore parent;

	/* Unmatched folder, set up in camel_vee_store_init */
	struct _CamelVeeFolder *folder_unmatched;
	GHashTable *unmatched_uids;
};

struct _CamelVeeStoreClass {
	CamelStoreClass parent_class;
};

CamelType		camel_vee_store_get_type	(void);
CamelVeeStore      *camel_vee_store_new	(void);

G_END_DECLS

#endif /* _CAMEL_VEE_STORE_H */
