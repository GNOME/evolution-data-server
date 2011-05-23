/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author: Chris Toshok <toshok@ximian.com>
 * Author: Ross Burton <ross@linux.intel.com>
 */

#ifndef __E_DATA_BOOK_TYPES_H__
#define __E_DATA_BOOK_TYPES_H__

G_BEGIN_DECLS

typedef struct _EDataBookView        EDataBookView;
typedef struct _EDataBookViewClass   EDataBookViewClass;

typedef struct _EBookBackendSExp EBookBackendSExp;
typedef struct _EBookBackendSExpClass EBookBackendSExpClass;

typedef struct _EBookBackend        EBookBackend;
typedef struct _EBookBackendClass   EBookBackendClass;

typedef struct _EBookBackendSummary EBookBackendSummary;
typedef struct _EBookBackendSummaryClass EBookBackendSummaryClass;

typedef struct _EBookBackendSync        EBookBackendSync;
typedef struct _EBookBackendSyncClass   EBookBackendSyncClass;

typedef struct _EDataBook        EDataBook;
typedef struct _EDataBookClass   EDataBookClass;

typedef enum {
	E_DATA_BOOK_STATUS_SUCCESS,
	E_DATA_BOOK_STATUS_BUSY,
	E_DATA_BOOK_STATUS_REPOSITORY_OFFLINE,
	E_DATA_BOOK_STATUS_PERMISSION_DENIED,
	E_DATA_BOOK_STATUS_CONTACT_NOT_FOUND,
	E_DATA_BOOK_STATUS_CONTACTID_ALREADY_EXISTS,
	E_DATA_BOOK_STATUS_AUTHENTICATION_FAILED,
	E_DATA_BOOK_STATUS_AUTHENTICATION_REQUIRED,
	E_DATA_BOOK_STATUS_UNSUPPORTED_FIELD,
	E_DATA_BOOK_STATUS_UNSUPPORTED_AUTHENTICATION_METHOD,
	E_DATA_BOOK_STATUS_TLS_NOT_AVAILABLE,
	E_DATA_BOOK_STATUS_NO_SUCH_BOOK,
	E_DATA_BOOK_STATUS_BOOK_REMOVED,
	E_DATA_BOOK_STATUS_OFFLINE_UNAVAILABLE,
	E_DATA_BOOK_STATUS_SEARCH_SIZE_LIMIT_EXCEEDED,
	E_DATA_BOOK_STATUS_SEARCH_TIME_LIMIT_EXCEEDED,
	E_DATA_BOOK_STATUS_INVALID_QUERY,
	E_DATA_BOOK_STATUS_QUERY_REFUSED,
	E_DATA_BOOK_STATUS_COULD_NOT_CANCEL,
	E_DATA_BOOK_STATUS_OTHER_ERROR,
	E_DATA_BOOK_STATUS_INVALID_SERVER_VERSION,
	E_DATA_BOOK_STATUS_NO_SPACE,
	E_DATA_BOOK_STATUS_INVALID_ARG,
	E_DATA_BOOK_STATUS_NOT_SUPPORTED
} EDataBookStatus;

G_END_DECLS

#endif /* __E_DATA_BOOK_TYPES_H__ */
