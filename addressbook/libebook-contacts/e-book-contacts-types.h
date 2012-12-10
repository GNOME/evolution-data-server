/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * A client-side GObject which exposes the
 * Evolution:BookListener interface.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#if !defined (__LIBEBOOK_CONTACTS_H_INSIDE__) && !defined (LIBEBOOK_CONTACTS_COMPILATION)
#error "Only <libebook-contacts/libebook-contacts.h> should be included directly."
#endif

#ifndef __E_BOOK_CONTACTS_TYPES_H__
#define __E_BOOK_CONTACTS_TYPES_H__

#include <libebook-contacts/e-contact.h>

G_BEGIN_DECLS


/**
 * EBookClientViewFlags:
 * @E_BOOK_CLIENT_VIEW_FLAGS_NONE:
 *   Symbolic value for no flags
 * @E_BOOK_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL:
 *   If this flag is set then all contacts matching the view's query will
 *   be sent as notifications when starting the view, otherwise only future
 *   changes will be reported.  The default for a #EBookClientView is %TRUE.
 *
 * Flags that control the behaviour of an #EBookClientView.
 *
 * Since: 3.4
 */
typedef enum {
	E_BOOK_CLIENT_VIEW_FLAGS_NONE           = 0,
	E_BOOK_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL = (1 << 0),
} EBookClientViewFlags;

/**
 * EDataBookStatus:
 *
 * XXX Document me!
 *
 * Since: 3.6
 **/
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
	E_DATA_BOOK_STATUS_NOT_SUPPORTED,
	E_DATA_BOOK_STATUS_NOT_OPENED
} EDataBookStatus;

typedef enum {
	E_BOOK_VIEW_STATUS_OK,
	E_BOOK_VIEW_STATUS_TIME_LIMIT_EXCEEDED,
	E_BOOK_VIEW_STATUS_SIZE_LIMIT_EXCEEDED,
	E_BOOK_VIEW_ERROR_INVALID_QUERY,
	E_BOOK_VIEW_ERROR_QUERY_REFUSED,
	E_BOOK_VIEW_ERROR_OTHER_ERROR
} EBookViewStatus;

typedef enum {
	E_BOOK_CHANGE_CARD_ADDED,
	E_BOOK_CHANGE_CARD_DELETED,
	E_BOOK_CHANGE_CARD_MODIFIED
} EBookChangeType;

typedef struct {
	EBookChangeType  change_type;
	EContact        *contact;
} EBookChange;


/**
 * EBookIndexType:
 * @E_BOOK_INDEX_PREFIX: An index suitable for searching contacts with a prefix pattern
 * @E_BOOK_INDEX_SUFFIX: An index suitable for searching contacts with a suffix pattern
 * @E_BOOK_INDEX_PHONE: An index suitable for searching contacts for phone numbers.
 * Note that phone numbers must be convertible into FQTN according to E.164 to be
 * stored in this index. The number "+9999999" for instance won't be stored because
 * the country code "+999" currently is not assigned.
 *
 * The type of index defined by e_source_backend_summary_setup_set_indexed_fields()
 */
typedef enum {
	E_BOOK_INDEX_PREFIX = 0,
	E_BOOK_INDEX_SUFFIX,
	E_BOOK_INDEX_PHONE,
} EBookIndexType;

G_END_DECLS

#endif /* __E_BOOK_CONTACTS_TYPES_H__ */
