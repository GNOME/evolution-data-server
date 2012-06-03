/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-vcf.h - VCF contact backend.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 *
 * Authors: Chris Toshok <toshok@ximian.com>
 */

#ifndef E_BOOK_BACKEND_VCF_H
#define E_BOOK_BACKEND_VCF_H

#include <libedata-book/libedata-book.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_BACKEND_VCF \
	(e_book_backend_vcf_get_type ())
#define E_BOOK_BACKEND_VCF(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_BACKEND_VCF, EBookBackendVCF))
#define E_BOOK_BACKEND_VCF_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_BACKEND_VCF, EBookBackendVCFClass))
#define E_IS_BOOK_BACKEND_VCF(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_BACKEND_VCF))
#define E_IS_BOOK_BACKEND_VCF_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_BACKEND_VCF))
#define E_BOOK_BACKEND_VCF_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_BACKEND_VCF, EBookBackendVCFClass))

G_BEGIN_DECLS

typedef struct _EBookBackendVCF EBookBackendVCF;
typedef struct _EBookBackendVCFClass EBookBackendVCFClass;
typedef struct _EBookBackendVCFPrivate EBookBackendVCFPrivate;

struct _EBookBackendVCF {
	EBookBackendSync parent;
	EBookBackendVCFPrivate *priv;
};

struct _EBookBackendVCFClass {
	EBookBackendSyncClass parent_class;
};

GType		e_book_backend_vcf_get_type	(void);

G_END_DECLS

#endif /* E_BOOK_BACKEND_VCF_H */

