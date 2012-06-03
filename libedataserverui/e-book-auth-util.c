/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-auth-util.c - Lame helper to load addressbooks with authentication.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors: Hans Petter Jansson <hpj@novell.com>
 *
 * Mostly taken from Evolution's addressbook/gui/component/addressbook.c
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <libedataserverui/e-passwords.h>

#include "e-book-auth-util.h"

typedef struct {
	EBook *book;
} LoadContext;

static void
load_book_source_context_free (LoadContext *context)
{
	if (context->book != NULL)
		g_object_unref (context->book);

	g_slice_free (LoadContext, context);
}

static void
load_book_source_thread (GSimpleAsyncResult *simple,
                         ESource *source,
                         GCancellable *cancellable)
{
	EBook *book;
	LoadContext *context;
	GError *error = NULL;

	context = g_simple_async_result_get_op_res_gpointer (simple);

	book = e_book_new (source, &error);
	if (book == NULL) {
		g_simple_async_result_take_error (simple, error);
		return;
	}

	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		g_simple_async_result_take_error (simple, error);
		g_object_unref (book);
		return;
	}

	if (!e_book_open (book, FALSE, &error)) {
		g_simple_async_result_take_error (simple, error);
		g_object_unref (book);
		return;
	}

	context->book = book;
}

/**
 * e_load_book_source_async:
 * @source: an #ESource
 * @parent: parent window for password dialogs, or %NULL
 * @cancellable: optional #GCancellable object, %NULL to ignore
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: the data to pass to @callback
 *
 * Creates a new #EBook specified by @source and opens it, prompting the
 * user for authentication if necessary.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call e_load_book_source_finish() to obtain the resulting #EBook.
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_client_utils_open_new(), e_client_utils_open_new_finish() instead.
 **/
void
e_load_book_source_async (ESource *source,
                          GtkWindow *parent,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	GSimpleAsyncResult *simple;
	LoadContext *context;

	g_return_if_fail (E_IS_SOURCE (source));

	if (parent != NULL) {
		g_return_if_fail (GTK_IS_WINDOW (parent));
		g_object_ref (parent);
	}

	if (cancellable != NULL) {
		g_return_if_fail (G_IS_CANCELLABLE (cancellable));
		g_object_ref (cancellable);
	} else {
		/* always provide cancellable, because the code depends on it */
		cancellable = g_cancellable_new ();
	}

	context = g_slice_new0 (LoadContext);

	simple = g_simple_async_result_new (
		G_OBJECT (source), callback,
		user_data, e_load_book_source_async);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, context, (GDestroyNotify)
		load_book_source_context_free);

	g_simple_async_result_run_in_thread (
		simple, (GSimpleAsyncThreadFunc) load_book_source_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_load_book_source_finish:
 * @source: an #ESource
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes an asynchronous #EBook open operation started with
 * e_load_book_source_async().  If an error occurred, or the user
 * declined to authenticate, the function will return %NULL and
 * set @error.
 *
 * Returns: a ready-to-use #EBook, or %NULL or error
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_client_utils_open_new(), e_client_utils_open_new_finish() instead.
 **/
EBook *
e_load_book_source_finish (ESource *source,
                           GAsyncResult *result,
                           GError **error)
{
	GSimpleAsyncResult *simple;
	LoadContext *context;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
			result, G_OBJECT (source),
			e_load_book_source_async), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	context = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_val_if_fail (context != NULL, NULL);

	return g_object_ref (context->book);
}
