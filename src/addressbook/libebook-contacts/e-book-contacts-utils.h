/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2012 Intel Corporation
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Nat Friedman (nat@ximian.com)
 *          Tristan Van Berkom <tristanvb@openismus.com>
 */

#if !defined (__LIBEBOOK_CONTACTS_H_INSIDE__) && !defined (LIBEBOOK_CONTACTS_COMPILATION)
#error "Only <libebook-contacts/libebook-contacts.h> should be included directly."
#endif

#ifndef E_BOOK_CONTACTS_UTILS_H
#define E_BOOK_CONTACTS_UTILS_H

#include <libedataserver/libedataserver.h>
#include <libebook-contacts/e-book-contacts-enums.h>
#include <libebook-contacts/e-contact.h>

/**
 * E_BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS: (value "required-fields")
 *
 * Provides comma-separated list of required fields by the book backend.
 * All of these attributes should be set, otherwise the backend will reject
 * saving the contact.
 *
 * The e_contact_field_id() can be used to transform the field name
 * into an #EContactField.
 *
 * Since: 3.2
 **/
#define E_BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS		"required-fields"

/**
 * E_BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS: (value "supported-fields")
 *
 * Provides comma-separated list of supported fields by the book backend.
 * Attributes other than those listed here can be discarded. This can be
 * used to enable/show only supported elements in GUI.
 *
 * The e_contact_field_id() can be used to transform the field name
 * into an #EContactField.
 *
 * Since: 3.2
 **/
#define E_BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS	"supported-fields"

/**
 * E_BOOK_BACKEND_PROPERTY_REVISION: (value "revision")
 *
 * The current overall revision string, this can be used as
 * a quick check to see if data has changed at all since the
 * last time the addressbook revision was observed.
 *
 * Since: 3.4
 **/
#define E_BOOK_BACKEND_PROPERTY_REVISION		"revision"

/**
 * E_BOOK_CLIENT_ERROR:
 *
 * Error domain for #EBookClient errors
 *
 * Since: 3.2
 **/
#define E_BOOK_CLIENT_ERROR e_book_client_error_quark ()

G_BEGIN_DECLS

/**
 * EBookClientError:
 * @E_BOOK_CLIENT_ERROR_NO_SUCH_BOOK: Requested book did not exist
 * @E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND: Contact referred to was not found
 * @E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS: Tried to add a contact which already exists
 * @E_BOOK_CLIENT_ERROR_NO_SUCH_SOURCE: Referred #ESource does not exist
 * @E_BOOK_CLIENT_ERROR_NO_SPACE: Out of disk space
 *
 * Error codes returned by #EBookClient APIs, if an #EClientError was not available.
 *
 * Since: 3.2
 **/
typedef enum {
	E_BOOK_CLIENT_ERROR_NO_SUCH_BOOK,
	E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND,
	E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS,
	E_BOOK_CLIENT_ERROR_NO_SUCH_SOURCE,
	E_BOOK_CLIENT_ERROR_NO_SPACE
} EBookClientError;

GQuark		e_book_client_error_quark	(void) G_GNUC_CONST;
const gchar *	e_book_client_error_to_string	(EBookClientError code);
GError *	e_book_client_error_create	(EBookClientError code,
						 const gchar *custom_msg);
GError *	e_book_client_error_create_fmt	(EBookClientError code,
						 const gchar *format,
						 ...) G_GNUC_PRINTF (2, 3);

EConflictResolution
		e_book_util_operation_flags_to_conflict_resolution
						(guint32 flags); /* bit-or of EBookOperationFlags */
guint32		e_book_util_conflict_resolution_to_operation_flags /* bit-or of EBookOperationFlags */
						(EConflictResolution conflict_resolution);

void		e_book_util_foreach_address	(const gchar *email_address,
						 GHRFunc func,
						 gpointer user_data);

#ifndef EDS_DISABLE_DEPRECATED

/**
 * EBookViewStatus:
 * @E_BOOK_VIEW_STATUS_OK: Ok
 * @E_BOOK_VIEW_STATUS_TIME_LIMIT_EXCEEDED: Time limit exceeded
 * @E_BOOK_VIEW_STATUS_SIZE_LIMIT_EXCEEDED: Size limit exceeded
 * @E_BOOK_VIEW_ERROR_INVALID_QUERY: Invalid search expression
 * @E_BOOK_VIEW_ERROR_QUERY_REFUSED: Search expression refused
 * @E_BOOK_VIEW_ERROR_OTHER_ERROR: Another error occurred
 *
 * Status messages used in notifications in the deprecated #EBookView class
 *
 * Deprecated: 3.2: Use #EBookClientView instead.
 */
typedef enum {
	E_BOOK_VIEW_STATUS_OK,
	E_BOOK_VIEW_STATUS_TIME_LIMIT_EXCEEDED,
	E_BOOK_VIEW_STATUS_SIZE_LIMIT_EXCEEDED,
	E_BOOK_VIEW_ERROR_INVALID_QUERY,
	E_BOOK_VIEW_ERROR_QUERY_REFUSED,
	E_BOOK_VIEW_ERROR_OTHER_ERROR
} EBookViewStatus;

/**
 * EBookChangeType:
 * @E_BOOK_CHANGE_CARD_ADDED: A vCard was added
 * @E_BOOK_CHANGE_CARD_DELETED: A vCard was deleted
 * @E_BOOK_CHANGE_CARD_MODIFIED: A vCard was modified
 *
 * The type of change in an #EBookChange
 *
 * Deprecated: 3.2
 */
typedef enum {
	E_BOOK_CHANGE_CARD_ADDED,
	E_BOOK_CHANGE_CARD_DELETED,
	E_BOOK_CHANGE_CARD_MODIFIED
} EBookChangeType;

/**
 * EBookChange:
 * @change_type: The #EBookChangeType
 * @contact: The #EContact which changed
 *
 * This is a part of the deprecated #EBook API.
 *
 * Deprecated: 3.2
 */
typedef struct {
	EBookChangeType  change_type;
	EContact        *contact;
} EBookChange;

#endif /* EDS_DISABLE_DEPRECATED */

G_END_DECLS

#endif /* E_BOOK_CONTACTS_UTILS_H */
