/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileCopyrightText: (C) 2012 Intel Corporation
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Nat Friedman (nat@ximian.com)
 * SPDX-FileContributor: Tristan Van Berkom <tristanvb@openismus.com>
 */

#if !defined (__LIBEDATA_BOOK_H_INSIDE__) && !defined (LIBEDATA_BOOK_COMPILATION)
#error "Only <libedata-book/libedata-book.h> should be included directly."
#endif

#ifndef E_BOOK_BACKEND_PRIVATE_H
#define E_BOOK_BACKEND_PRIVATE_H

#include <gio/gio.h>
#include <libedata-book/e-book-backend.h>

G_BEGIN_DECLS

GTask *		e_book_backend_prepare_for_completion
						(EBookBackend *backend,
						 guint32 opid);

G_END_DECLS

#endif /* E_BOOK_BACKEND_PRIVATE_H */
