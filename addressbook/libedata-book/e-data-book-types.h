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
} EDataBookStatus;

/* Some hacks so the backends compile without change */
#define GNOME_Evolution_Addressbook_CallStatus EDataBookStatus
#define GNOME_Evolution_Addressbook_BookMode EDataBookMode

#define GNOME_Evolution_Addressbook_Success E_DATA_BOOK_STATUS_SUCCESS
#define GNOME_Evolution_Addressbook_RepositoryOffline E_DATA_BOOK_STATUS_REPOSITORY_OFFLINE
#define GNOME_Evolution_Addressbook_PermissionDenied E_DATA_BOOK_STATUS_PERMISSION_DENIED
#define GNOME_Evolution_Addressbook_ContactNotFound E_DATA_BOOK_STATUS_CONTACT_NOT_FOUND
#define GNOME_Evolution_Addressbook_ContactIdAlreadyExists E_DATA_BOOK_STATUS_CONTACTID_ALREADY_EXISTS
#define GNOME_Evolution_Addressbook_AuthenticationFailed E_DATA_BOOK_STATUS_AUTHENTICATION_FAILED
#define GNOME_Evolution_Addressbook_AuthenticationRequired E_DATA_BOOK_STATUS_AUTHENTICATION_REQUIRED
#define GNOME_Evolution_Addressbook_UnsupportedField E_DATA_BOOK_STATUS_UNSUPPORTED_FIELD
#define GNOME_Evolution_Addressbook_UnsupportedAuthenticationMethod E_DATA_BOOK_STATUS_UNSUPPORTED_AUTHENTICATION_METHOD
#define GNOME_Evolution_Addressbook_TLSNotAvailable E_DATA_BOOK_STATUS_TLS_NOT_AVAILABLE
#define GNOME_Evolution_Addressbook_NoSuchBook E_DATA_BOOK_STATUS_NO_SUCH_BOOK
#define GNOME_Evolution_Addressbook_BookRemoved E_DATA_BOOK_STATUS_BOOK_REMOVED
#define GNOME_Evolution_Addressbook_OfflineUnavailable E_DATA_BOOK_STATUS_BOOK_REMOVED
#define GNOME_Evolution_Addressbook_SearchSizeLimitExceeded E_DATA_BOOK_STATUS_SEARCH_SIZE_LIMIT_EXCEEDED
#define GNOME_Evolution_Addressbook_SearchTimeLimitExceeded E_DATA_BOOK_STATUS_SEARCH_TIME_LIMIT_EXCEEDED
#define GNOME_Evolution_Addressbook_InvalidQuery E_DATA_BOOK_STATUS_INVALID_QUERY
#define GNOME_Evolution_Addressbook_QueryRefused E_DATA_BOOK_STATUS_QUERY_REFUSED
#define GNOME_Evolution_Addressbook_CouldNotCancel E_DATA_BOOK_STATUS_COULD_NOT_CANCEL
#define GNOME_Evolution_Addressbook_OtherError E_DATA_BOOK_STATUS_OTHER_ERROR
#define GNOME_Evolution_Addressbook_InvalidServerVersion E_DATA_BOOK_STATUS_INVALID_SERVER_VERSION
#define GNOME_Evolution_Addressbook_NoSpace E_DATA_BOOK_STATUS_NO_SPACE

typedef enum {
	E_DATA_BOOK_MODE_LOCAL,
	E_DATA_BOOK_MODE_REMOTE,
	E_DATA_BOOK_MODE_ANY,
} EDataBookMode;

#define GNOME_Evolution_Addressbook_MODE_LOCAL E_DATA_BOOK_MODE_LOCAL
#define GNOME_Evolution_Addressbook_MODE_REMOTE E_DATA_BOOK_MODE_REMOTE
#define GNOME_Evolution_Addressbook_MODE_ANY E_DATA_BOOK_MODE_ANY

typedef enum {
	E_DATA_BOOK_BACKEND_CHANGE_ADDED,
	E_DATA_BOOK_BACKEND_CHANGE_DELETED,
	E_DATA_BOOK_BACKEND_CHANGE_MODIFIED
} EDataBookChangeType;

typedef struct {
	EDataBookChangeType change_type;
	gchar *vcard;
} EDataBookChange;

/* Transition typedef */
typedef EDataBookChange GNOME_Evolution_Addressbook_BookChangeItem;

#define GNOME_Evolution_Addressbook_ContactAdded E_DATA_BOOK_BACKEND_CHANGE_ADDED
#define GNOME_Evolution_Addressbook_ContactDeleted E_DATA_BOOK_BACKEND_CHANGE_DELETED
#define GNOME_Evolution_Addressbook_ContactModified E_DATA_BOOK_BACKEND_CHANGE_MODIFIED

G_END_DECLS

#endif /* __E_DATA_BOOK_TYPES_H__ */
