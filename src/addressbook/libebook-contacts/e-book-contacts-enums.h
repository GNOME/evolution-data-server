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

#ifndef E_BOOK_CONTACTS_ENUMS_H
#define E_BOOK_CONTACTS_ENUMS_H

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
typedef enum { /*< flags >*/
	E_BOOK_CLIENT_VIEW_FLAGS_NONE = 0,
	E_BOOK_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL = (1 << 0),
} EBookClientViewFlags;

/**
 * EBookIndexType:
 * @E_BOOK_INDEX_PREFIX: An index suitable for searching contacts with a prefix pattern
 * @E_BOOK_INDEX_SUFFIX: An index suitable for searching contacts with a suffix pattern
 * @E_BOOK_INDEX_PHONE: An index suitable for searching contacts for phone numbers.
 * <note><para>Phone numbers must be convertible into FQTN according to E.164 to be
 * stored in this index. The number "+9999999" for instance won't be stored because
 * the country calling code "+999" currently is not assigned.</para></note>
 * @E_BOOK_INDEX_SORT_KEY: Indicates that a given #EContactField should be usable as a sort key.
 *
 * The type of index defined by e_source_backend_summary_setup_set_indexed_fields()
 */
typedef enum {
	E_BOOK_INDEX_PREFIX = 0,
	E_BOOK_INDEX_SUFFIX,
	E_BOOK_INDEX_PHONE,
	E_BOOK_INDEX_SORT_KEY
} EBookIndexType;

/**
 * EBookCursorSortType:
 * @E_BOOK_CURSOR_SORT_ASCENDING: Sort results in ascending order
 * @E_BOOK_CURSOR_SORT_DESCENDING: Sort results in descending order
 *
 * Specifies the sort order of an ordered query
 *
 * Since: 3.12
 */
typedef enum {
	E_BOOK_CURSOR_SORT_ASCENDING = 0,
	E_BOOK_CURSOR_SORT_DESCENDING
} EBookCursorSortType;

/**
 * EBookCursorOrigin:
 * @E_BOOK_CURSOR_ORIGIN_CURRENT:  The current cursor position
 * @E_BOOK_CURSOR_ORIGIN_BEGIN:    The beginning of the cursor results.
 * @E_BOOK_CURSOR_ORIGIN_END:      The ending of the cursor results.
 *
 * Specifies the start position to in the list of traversed contacts
 * in calls to e_book_client_cursor_step().
 *
 * When an #EBookClientCursor is created, the current position implied by %E_BOOK_CURSOR_ORIGIN_CURRENT
 * is the same as %E_BOOK_CURSOR_ORIGIN_BEGIN.
 *
 * Since: 3.12
 */
typedef enum {
	E_BOOK_CURSOR_ORIGIN_CURRENT,
	E_BOOK_CURSOR_ORIGIN_BEGIN,
	E_BOOK_CURSOR_ORIGIN_END
} EBookCursorOrigin;

/**
 * EBookCursorStepFlags:
 * @E_BOOK_CURSOR_STEP_MOVE:  The cursor position should be modified while stepping
 * @E_BOOK_CURSOR_STEP_FETCH: Traversed contacts should be listed and returned while stepping.
 *
 * Defines the behaviour of e_book_client_cursor_step().
 *
 * Since: 3.12
 */
typedef enum { /*< flags >*/
	E_BOOK_CURSOR_STEP_MOVE = (1 << 0),
	E_BOOK_CURSOR_STEP_FETCH = (1 << 1)
} EBookCursorStepFlags;

/**
 * EBookOperationFlags:
 * @E_BOOK_OPERATION_FLAG_NONE: no operation flags defined
 * @E_BOOK_OPERATION_FLAG_CONFLICT_FAIL: conflict resolution mode, to fail and do not
 *    do any changes, when a conflict is detected
 * @E_BOOK_OPERATION_FLAG_CONFLICT_USE_NEWER: conflict resolution mode, to use newer
 *    of the local and the server side data, when a conflict is detected
 * @E_BOOK_OPERATION_FLAG_CONFLICT_KEEP_SERVER: conflict resolution mode, to use
 *    the server data (and local changed), when a conflict is detected
 * @E_BOOK_OPERATION_FLAG_CONFLICT_KEEP_LOCAL: conflict resolution mode, to use
 *    local data (and always overwrite server data), when a conflict is detected
 * @E_BOOK_OPERATION_FLAG_CONFLICT_WRITE_COPY: conflict resolution mode, to create
 *    a copy of the data, when a conflict is detected
 *
 * Book operation flags, to specify behavior in certain situations. The conflict
 * resolution mode flags cannot be combined together, where the @E_BOOK_OPERATION_FLAG_CONFLICT_KEEP_LOCAL
 * is the default behavior (and it is used when no other conflict resolution flag is set).
 * The flags can be ignored when the operation or the backend don't support it.
 *
 * Since: 3.34
 **/
typedef enum { /*< flags >*/
	E_BOOK_OPERATION_FLAG_NONE			= 0,
	E_BOOK_OPERATION_FLAG_CONFLICT_FAIL		= (1 << 0),
	E_BOOK_OPERATION_FLAG_CONFLICT_USE_NEWER	= (1 << 1),
	E_BOOK_OPERATION_FLAG_CONFLICT_KEEP_SERVER	= (1 << 2),
	E_BOOK_OPERATION_FLAG_CONFLICT_KEEP_LOCAL	= 0,
	E_BOOK_OPERATION_FLAG_CONFLICT_WRITE_COPY	= (1 << 3)
} EBookOperationFlags;

G_END_DECLS

#endif /* E_BOOK_CONTACTS_ENUMS_H */
