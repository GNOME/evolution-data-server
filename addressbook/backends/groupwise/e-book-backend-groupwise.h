/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Sivaiah Nallagatla (snallagatla@novell.com)
 *
 * Copyright 2004, Novell, Inc.
 */
                                                                                                                          
#ifndef __E_BOOK_BACKEND_GROUPWISE_H__
#define __E_BOOK_BACKEND_GROUPWISE_H__
                                                                                                                             
#include <libedata-book/e-book-backend-sync.h>
                                                                                                                             
#define E_TYPE_BOOK_BACKEND_GROUPWISE        (e_book_backend_groupwise_get_type ())
#define E_BOOK_BACKEND_GROUPWISE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_GROUPWISE, EBookBackendGroupwise))
#define E_BOOK_BACKEND_GROUPWISE_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_GROUPWISE, EBookBackendGroupwiseClass))
#define E_IS_BOOK_BACKEND_GROUPWISE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_GROUPWISE))
#define E_IS_BOOK_BACKEND_GROUPWISE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_GROUPWISE))
#define E_BOOK_BACKEND_GROUPWISE_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_BACKEND_GROUPWISE, EBookBackenGroupwiseClass))                                                                                                                             
typedef struct _EBookBackendGroupwisePrivate EBookBackendGroupwisePrivate;
                                                                                                                             
typedef struct {
	EBookBackend         parent_object;
	EBookBackendGroupwisePrivate *priv;
} EBookBackendGroupwise;
                                                                                                                             
typedef struct {
	EBookBackendClass parent_class;
} EBookBackendGroupwiseClass;
                                                                                                                             
EBookBackend *e_book_backend_groupwise_new      (void);
GType       e_book_backend_groupwise_get_type (void);
                                                                                                                             
#endif /* ! __E_BOOK_BACKEND_GROUPWISE_H__ */
                                                                                                                             


