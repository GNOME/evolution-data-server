/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
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

#ifndef __CAMEL_OFFLINE_STORE_H__
#define __CAMEL_OFFLINE_STORE_H__

#include <camel/camel-store.h>

#define CAMEL_TYPE_OFFLINE_STORE            (camel_offline_store_get_type ())
#define CAMEL_OFFLINE_STORE(obj)            (CAMEL_CHECK_CAST ((obj), CAMEL_TYPE_OFFLINE_STORE, CamelOfflineStore))
#define CAMEL_OFFLINE_STORE_CLASS(klass)    (CAMEL_CHECK_CLASS_CAST ((klass), CAMEL_TYPE_OFFLINE_STORE, CamelOfflineStoreClass))
#define CAMEL_IS_OFFLINE_STORE(obj)         (CAMEL_CHECK_TYPE ((obj), CAMEL_TYPE_OFFLINE_STORE))
#define CAMEL_IS_OFFLINE_STORE_CLASS(klass) (CAMEL_CHECK_CLASS_TYPE ((klass), CAMEL_TYPE_OFFLINE_STORE))
#define CAMEL_OFFLINE_STORE_GET_CLASS(obj)  (CAMEL_CHECK_GET_CLASS ((obj), CAMEL_TYPE_OFFLINE_STORE, CamelOfflineStoreClass))

G_BEGIN_DECLS

typedef struct _CamelOfflineStore CamelOfflineStore;
typedef struct _CamelOfflineStoreClass CamelOfflineStoreClass;

enum {
	CAMEL_OFFLINE_STORE_ARG_FIRST  = CAMEL_STORE_ARG_FIRST + 100
};

enum {
	CAMEL_OFFLINE_STORE_NETWORK_AVAIL,
	CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL
};

struct _CamelOfflineStore {
	CamelStore parent_object;

	gint state;
};

struct _CamelOfflineStoreClass {
	CamelStoreClass parent_class;

	void (* set_network_state) (CamelOfflineStore *store, gint state, CamelException *ex);
};

CamelType camel_offline_store_get_type (void);

void camel_offline_store_set_network_state (CamelOfflineStore *store, gint state, CamelException *ex);
gint camel_offline_store_get_network_state (CamelOfflineStore *store, CamelException *ex);

void camel_offline_store_prepare_for_offline (CamelOfflineStore *store, CamelException *ex);

G_END_DECLS

#endif /* __CAMEL_OFFLINE_STORE_H__ */
