/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_BOOK_BACKEND_DUMMY_H
#define E_BOOK_BACKEND_DUMMY_H

#include <libedata-book/libedata-book.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_BACKEND_DUMMY \
	(e_book_backend_dummy_get_type ())
#define E_BOOK_BACKEND_DUMMY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_BACKEND_DUMMY, EBookBackendDummy))
#define E_BOOK_BACKEND_DUMMY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_BACKEND_DUMMY, EBookBackendDummyClass))
#define E_IS_BOOK_BACKEND_DUMMY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_BACKEND_DUMMY))
#define E_IS_BOOK_BACKEND_DUMMY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_BACKEND_DUMMY))
#define E_BOOK_BACKEND_DUMMY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_BACKEND_DUMMY, EBookBackendDummyClass))

G_BEGIN_DECLS

typedef struct _EBookBackendDummy EBookBackendDummy;
typedef struct _EBookBackendDummyClass EBookBackendDummyClass;
typedef struct _EBookBackendDummyPrivate EBookBackendDummyPrivate;

/* The 'dummy' backend is only for testing purposes, to verify
   functionality of the EDataBookViewWatcherMemory */

struct _EBookBackendDummy {
	EBookBackend parent;
	EBookBackendDummyPrivate *priv;
};

struct _EBookBackendDummyClass {
	EBookBackendClass parent_class;
};

GType		e_book_backend_dummy_get_type	(void);

G_END_DECLS

#endif /* E_BOOK_BACKEND_DUMMY_H */
