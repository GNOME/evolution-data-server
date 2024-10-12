/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_BOOK_BACKEND_DUMMY_META_H
#define E_BOOK_BACKEND_DUMMY_META_H

#include <libedata-book/libedata-book.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_BACKEND_DUMMY_META \
	(e_book_backend_dummy_meta_get_type ())
#define E_BOOK_BACKEND_DUMMY_META(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_BACKEND_DUMMY_META, EBookBackendDummyMeta))
#define E_BOOK_BACKEND_DUMMY_META_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_BACKEND_DUMMY_META, EBookBackendDummyMetaClass))
#define E_IS_BOOK_BACKEND_DUMMY_META(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_BACKEND_DUMMY_META))
#define E_IS_BOOK_BACKEND_DUMMY_META_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_BACKEND_DUMMY_META))
#define E_BOOK_BACKEND_DUMMY_META_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_BACKEND_DUMMY_META, EBookBackendDummyMetaClass))

G_BEGIN_DECLS

typedef struct _EBookBackendDummyMeta EBookBackendDummyMeta;
typedef struct _EBookBackendDummyMetaClass EBookBackendDummyMetaClass;
typedef struct _EBookBackendDummyMetaPrivate EBookBackendDummyMetaPrivate;

/* The 'dummy-meta' backend is only for testing purposes, to verify
   functionality of the EBookMetaBackend */

struct _EBookBackendDummyMeta {
	EBookMetaBackend parent;
	EBookBackendDummyMetaPrivate *priv;
};

struct _EBookBackendDummyMetaClass {
	EBookMetaBackendClass parent_class;
};

GType		e_book_backend_dummy_meta_get_type	(void);

G_END_DECLS

#endif /* E_BOOK_BACKEND_DUMMY_META_H */
