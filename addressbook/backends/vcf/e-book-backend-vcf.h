/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 * Copyright (C) 2003, Ximian, Inc.
 */

#ifndef __E_BOOK_BACKEND_VCF_H__
#define __E_BOOK_BACKEND_VCF_H__

#include <libedata-book/e-book-backend-sync.h>

#define E_TYPE_BOOK_BACKEND_VCF         (e_book_backend_vcf_get_type ())
#define E_BOOK_BACKEND_VCF(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_VCF, EBookBackendVCF))
#define E_BOOK_BACKEND_VCF_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_VCF, EBookBackendVCFClass))
#define E_IS_BOOK_BACKEND_VCF(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_VCF))
#define E_IS_BOOK_BACKEND_VCF_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_VCF))
#define E_BOOK_BACKEND_VCF_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_BACKEND_VCF, EBookBackendVCFClass))

typedef struct _EBookBackendVCFPrivate EBookBackendVCFPrivate;

typedef struct {
	EBookBackendSync         parent_object;
	EBookBackendVCFPrivate *priv;
} EBookBackendVCF;

typedef struct {
	EBookBackendSyncClass parent_class;
} EBookBackendVCFClass;

EBookBackend *e_book_backend_vcf_new      (void);
GType       e_book_backend_vcf_get_type (void);

#endif /* ! __E_BOOK_BACKEND_VCF_H__ */

