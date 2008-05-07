/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Camel
 *  Copyright (C) 1999-2007 Novell, Inc. (www.novell.com)
 *
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
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
 *  Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#ifndef __CAMEL_IMAP4_STORE_H__
#define __CAMEL_IMAP4_STORE_H__

#include <camel/camel-offline-store.h>

#define CAMEL_TYPE_IMAP4_STORE            (camel_imap4_store_get_type ())
#define CAMEL_IMAP4_STORE(obj)            (CAMEL_CHECK_CAST ((obj), CAMEL_TYPE_IMAP4_STORE, CamelIMAP4Store))
#define CAMEL_IMAP4_STORE_CLASS(klass)    (CAMEL_CHECK_CLASS_CAST ((klass), CAMEL_TYPE_IMAP4_STORE, CamelIMAP4StoreClass))
#define CAMEL_IS_IMAP4_STORE(obj)         (CAMEL_CHECK_TYPE ((obj), CAMEL_TYPE_IMAP4_STORE))
#define CAMEL_IS_IMAP4_STORE_CLASS(klass) (CAMEL_CHECK_CLASS_TYPE ((klass), CAMEL_TYPE_IMAP4_STORE))
#define CAMEL_IMAP4_STORE_GET_CLASS(obj)  (CAMEL_CHECK_GET_CLASS ((obj), CAMEL_TYPE_IMAP4_STORE, CamelIMAP4StoreClass))

G_BEGIN_DECLS

typedef struct _CamelIMAP4Store CamelIMAP4Store;
typedef struct _CamelIMAP4StoreClass CamelIMAP4StoreClass;

struct _CamelIMAP4Engine;

struct _CamelIMAP4Store {
	CamelOfflineStore parent_object;
	
	struct _CamelIMAP4StoreSummary *summary;
	struct _CamelIMAP4Engine *engine;
	char *storage_path;
};

struct _CamelIMAP4StoreClass {
	CamelOfflineStoreClass parent_class;
	
};


CamelType camel_imap4_store_get_type (void);

G_END_DECLS

#endif /* __CAMEL_IMAP4_STORE_H__ */
