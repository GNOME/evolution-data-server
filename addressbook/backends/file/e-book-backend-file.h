/*
 * Copyright 2000, Ximian, Inc.
 */

#ifndef __E_BOOK_BACKEND_FILE_H__
#define __E_BOOK_BACKEND_FILE_H__

#include <libedata-book/e-book-backend-sync.h>

#define E_TYPE_BOOK_BACKEND_FILE        (e_book_backend_file_get_type ())
#define E_BOOK_BACKEND_FILE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_FILE, EBookBackendFile))
#define E_BOOK_BACKEND_FILE_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_FILE, EBookBackendFileClass))
#define E_IS_BOOK_BACKEND_FILE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_FILE))
#define E_IS_BOOK_BACKEND_FILE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_FILE))
#define E_BOOK_BACKEND_FILE_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_BACKEND_FILE, EBookBackendFileClass))

typedef struct _EBookBackendFilePrivate EBookBackendFilePrivate;

typedef struct {
	EBookBackendSync         parent_object;
	EBookBackendFilePrivate *priv;
} EBookBackendFile;

typedef struct {
	EBookBackendSyncClass parent_class;
} EBookBackendFileClass;

EBookBackend *e_book_backend_file_new      (void);
GType       e_book_backend_file_get_type (void);

#endif /* ! __E_BOOK_BACKEND_FILE_H__ */

