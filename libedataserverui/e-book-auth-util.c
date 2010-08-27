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

#include <config.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>
#include <libebook/e-book.h>
#include <libedataserverui/e-passwords.h>
#include "libedataserver/e-url.h"
#include "e-book-auth-util.h"

static void addressbook_authenticate (EBook *book, gboolean previous_failure,
				      ESource *source, EBookAsyncCallback cb, gpointer closure);
static void auth_required_cb (EBook *book, gpointer data);
typedef struct {
	ESource       *source;
	EBook         *book;

	#ifndef E_BOOK_DISABLE_DEPRECATED
	EBookCallback  open_func;
	#endif
	EBookAsyncCallback  open_func_ex;
	gpointer       open_func_data;
} LoadSourceData;

static void
free_load_source_data (LoadSourceData *data)
{
	if (data->source)
		g_object_unref (data->source);
	if (data->book) {
		g_signal_handlers_disconnect_by_func (data->book, auth_required_cb, NULL);
		g_object_unref (data->book);
	}
	g_free (data);
}

static gchar *
remove_parameters_from_uri (const gchar *uri)
{
	gchar *euri_str;
	EUri *euri;

	euri = e_uri_new (uri);
	euri_str = e_uri_to_string (euri, FALSE);
	e_uri_free (euri);
	return euri_str;
}

static void
load_source_auth_cb (EBook *book, const GError *error, gpointer closure)
{
	LoadSourceData *data = closure;

	switch (error ? error->code : E_BOOK_ERROR_OK) {

		/* the user clicked cancel in the password dialog */
		case E_BOOK_ERROR_CANCELLED:
			if (e_book_check_static_capability (book, "anon-access")) {
				GtkWidget *dialog;

				/* XXX "LDAP" has to be removed from the folowing message
				   so that it wil valid for other servers which provide
				   anonymous access*/

				dialog = gtk_message_dialog_new (NULL,
								 0,
								 GTK_MESSAGE_WARNING,
								 GTK_BUTTONS_OK,
								 _("Accessing LDAP Server anonymously"));
				g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
				gtk_widget_show (dialog);
			}
			break;

		case E_BOOK_ERROR_INVALID_SERVER_VERSION:
			/* aka E_BOOK_ERROR_OK */
			error = NULL;
			break;

		case E_BOOK_ERROR_AUTHENTICATION_FAILED:
		case E_BOOK_ERROR_AUTHENTICATION_REQUIRED:
		{
			const gchar *uri = e_book_get_uri (book);
			gchar *stripped_uri = remove_parameters_from_uri (uri);
			const gchar *auth_domain = e_source_get_property (data->source, "auth-domain");
			const gchar *component_name;

			component_name = auth_domain ? auth_domain : "Addressbook";

			if (error->code == E_BOOK_ERROR_AUTHENTICATION_FAILED)
				e_passwords_forget_password (component_name, stripped_uri);

			addressbook_authenticate (book, TRUE, data->source, load_source_auth_cb, closure);

			g_free (stripped_uri);

			return;
		}

		default:
			break;
	}

	#ifndef E_BOOK_DISABLE_DEPRECATED
	if (data->open_func)
		data->open_func (book, error ? error->code : E_BOOK_ERROR_OK, data->open_func_data);
	#endif
	if (data->open_func_ex)
		data->open_func_ex (book, error, data->open_func_data);

	free_load_source_data (data);
}

static gboolean
get_remember_password (ESource *source)
{
	const gchar *value;

	value = e_source_get_property (source, "remember_password");
	if (value && !g_ascii_strcasecmp (value, "true"))
		return TRUE;

	return FALSE;
}

static void
set_remember_password (ESource *source, gboolean value)
{
	e_source_set_property (source, "remember_password",
			       value ? "true" : "false");
}

static void
addressbook_authenticate (EBook *book, gboolean previous_failure, ESource *source,
			  EBookAsyncCallback cb, gpointer closure)
{
	const gchar *auth;
	const gchar *user;
	const gchar *component_name;
	gchar *password = NULL;
	const gchar *uri = e_book_get_uri (book);
        gchar *stripped_uri = remove_parameters_from_uri (uri);
	const gchar *auth_domain = e_source_get_property (source, "auth-domain");

	component_name = auth_domain ? auth_domain : "Addressbook";
	uri = stripped_uri;

	password = e_passwords_get_password (component_name, uri);

	auth = e_source_get_property (source, "auth");

	if (auth && !strcmp ("ldap/simple-binddn", auth)) {
		user = e_source_get_property (source, "binddn");
	}
	else if (auth && !strcmp ("plain/password", auth)) {
		user = e_source_get_property (source, "user");
		if (!user) {
			user = e_source_get_property (source, "username");
		}
	}
	else {
		user = e_source_get_property (source, "email_addr");
	}
	if (!user)
		user = "";

	if (!password) {
		gchar *prompt;
		gchar *password_prompt;
		gboolean remember;
		const gchar *failed_auth;
		guint32 flags = E_PASSWORDS_REMEMBER_FOREVER|E_PASSWORDS_SECRET|E_PASSWORDS_ONLINE;

		if (previous_failure) {
			failed_auth = _("Failed to authenticate.\n");
			flags |= E_PASSWORDS_REPROMPT;
		}
		else {
			failed_auth = "";
		}

		password_prompt = g_strdup_printf (_("Enter password for %s (user %s)"),
						   e_source_peek_name (source), user);

		prompt = g_strconcat (failed_auth, password_prompt, NULL);
		g_free (password_prompt);

		remember = get_remember_password (source);
		password = e_passwords_ask_password (prompt, component_name, uri, prompt,
						     flags, &remember,
						     NULL);
		if (remember != get_remember_password (source))
			set_remember_password (source, remember);

		g_free (prompt);
	}

	if (password) {
		e_book_authenticate_user_async (book, user, password,
						e_source_get_property (source, "auth"),
						cb, closure);
		g_free (password);
	}
	else {
		GError *error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_CANCELLED, _("Cancelled"));

		/* they hit cancel */
		cb (book, error, closure);

		g_error_free (error);
	}

	g_free (stripped_uri);
}

static void
auth_required_cb (EBook *book, gpointer data)
{
	LoadSourceData *load_source_data = g_new0 (LoadSourceData, 1);

	load_source_data->source = g_object_ref (g_object_ref (e_book_get_source (book)));

	addressbook_authenticate (book, FALSE, load_source_data->source,
				  load_source_auth_cb, load_source_data);
}

static void
load_source_cb (EBook *book, const GError *error, gpointer closure)
{
	LoadSourceData *load_source_data = closure;

	if (!error && book != NULL) {
		const gchar *auth;

		auth = e_source_get_property (load_source_data->source, "auth");
		if (auth && strcmp (auth, "none")) {
			g_signal_connect (book, "auth_required", (GCallback) auth_required_cb, NULL);

			if (e_book_is_online (book)) {
				addressbook_authenticate (book, FALSE, load_source_data->source,
							  load_source_auth_cb, closure);
				return;
			}
		}
	}

	#ifndef E_BOOK_DISABLE_DEPRECATED
	if (load_source_data->open_func)
		load_source_data->open_func (book, error ? error->code : E_BOOK_ERROR_OK, load_source_data->open_func_data);
	#endif
	if (load_source_data->open_func_ex)
		load_source_data->open_func_ex (book, error, load_source_data->open_func_data);

	free_load_source_data (load_source_data);
}

#ifndef E_BOOK_DISABLE_DEPRECATED
/**
 * e_load_book_source:
 * @source: an #ESource
 * @open_func: a function to call when the operation finishes, or %NULL
 * @user_data: data to pass to callback function
 *
 * Creates a new #EBook specified by @source, and starts a non-blocking
 * open operation on it. If the book requires authorization, presents
 * a window asking the user for such.
 *
 * When the operation finishes, calls the callback function indicating
 * if it succeeded or not. If you don't care, you can pass %NULL for
 * @open_func, and no action will be taken on completion.
 *
 * Returns: A new #EBook that is being opened.
 *
 * Deprecated: 3.0: Use e_load_book_source_async() instead.
 **/
EBook *
e_load_book_source (ESource *source, EBookCallback open_func, gpointer user_data)
{
	EBook          *book;
	LoadSourceData *load_source_data = g_new0 (LoadSourceData, 1);

	load_source_data->source = g_object_ref (source);
	load_source_data->open_func = open_func;
	load_source_data->open_func_data = user_data;

	book = e_book_new (source, NULL);
	if (!book)
		return NULL;

	load_source_data->book = book;
	g_object_ref (book);
	e_book_open_async (book, FALSE, load_source_cb, load_source_data);
	return book;
}
#endif

typedef struct {
	EBook *book;
	GtkWindow *parent;
	GCancellable *cancellable;

	gboolean anonymous_alert;

	/* Authentication Details */
	gchar *auth_uri;
	gchar *auth_method;
	gchar *auth_username;
	gchar *auth_component;
	gboolean auth_remember;
} LoadContext;

static void
load_book_source_context_free (LoadContext *context)
{
	if (context->book != NULL)
		g_object_unref (context->book);

	if (context->parent != NULL)
		g_object_unref (context->parent);

	if (context->cancellable != NULL)
		g_object_unref (context->cancellable);

	g_free (context->auth_uri);
	g_free (context->auth_method);
	g_free (context->auth_username);
	g_free (context->auth_component);

	g_slice_free (LoadContext, context);
}

static void
load_book_source_get_auth_details (ESource *source,
                                   LoadContext *context)
{
	const gchar *property;
	gchar *uri;

	/* auth_method */

	property = e_source_get_property (source, "auth");

	if (property == NULL || strcmp (property, "none") == 0)
		return;

	context->auth_method = g_strdup (property);

	/* auth_uri */

	uri = e_source_get_uri (source);
	context->auth_uri = remove_parameters_from_uri (uri);
	g_free (uri);

	/* auth_username */

	if (g_strcmp0 (context->auth_method, "ldap/simple-binddn") == 0) {
		property = e_source_get_property (source, "binddn");

	} else if (g_strcmp0 (context->auth_method, "plain/password") == 0) {
		property = e_source_get_property (source, "user");
		if (property == NULL)
			property = e_source_get_property (source, "username");

	} else
		property = e_source_get_property (source, "email_addr");

	if (property == NULL)
		property = "";

	context->auth_username = g_strdup (property);

	/* auth_component */

	property = e_source_get_property (source, "auth-domain");

	if (property == NULL)
		property = "Addressbook";

	context->auth_component = g_strdup (property);

	/* auth_remember */

	property = e_source_get_property (source, "remember_password");

	context->auth_remember = (g_strcmp0 (property, "true") == 0);
}

static gchar *
load_book_source_password_prompt (EBook *book,
                                  LoadContext *context,
                                  gboolean reprompt)
{
	ESource *source;
	GString *string;
	const gchar *title;
	gchar *password;
	guint32 flags;

	source = e_book_get_source (book);
	string = g_string_sized_new (256);

	flags = E_PASSWORDS_REMEMBER_FOREVER |
		E_PASSWORDS_SECRET | E_PASSWORDS_ONLINE;

	if (reprompt) {
		g_string_assign (string, _("Failed to authenticate.\n"));
		flags |= E_PASSWORDS_REPROMPT;
	}

	g_string_append_printf (
		string, _("Enter password for %s (user %s)"),
		e_source_peek_name (source), context->auth_username);

	/* XXX Dialog windows should not have titles. */
	title = "";

	password = e_passwords_ask_password (
		title, context->auth_component,
		context->auth_uri, string->str, flags,
		&context->auth_remember, context->parent);

	g_string_free (string, TRUE);

	return password;
}

static void
load_book_source_thread (GSimpleAsyncResult *simple,
                         ESource *source,
                         GCancellable *cancellable)
{
	EBook *book;
	LoadContext *context;
	gchar *password;
	gboolean reprompt = FALSE;
	GError *error = NULL;

	context = g_simple_async_result_get_op_res_gpointer (simple);

	book = e_book_new (source, &error);
	if (book == NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
		return;
	}

	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		g_simple_async_result_set_from_error (simple, error);
		g_object_unref (book);
		g_error_free (error);
		return;
	}

	if (!e_book_open (book, FALSE, &error)) {
		g_simple_async_result_set_from_error (simple, error);
		g_object_unref (book);
		g_error_free (error);
		return;
	}

	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		g_simple_async_result_set_from_error (simple, error);
		g_object_unref (book);
		g_error_free (error);
		return;
	}

	/* Do we need to authenticate? */
	if (context->auth_method == NULL)
		goto exit;

	password = e_passwords_get_password (
		context->auth_component, context->auth_uri);

prompt:
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		g_simple_async_result_set_from_error (simple, error);
		g_object_unref (book);
		g_error_free (error);
		g_free (password);
		return;
	}

	if (password == NULL) {
		password = load_book_source_password_prompt (
			book, context, reprompt);
		reprompt = TRUE;
	}

	/* If we have a password, attempt to authenticate with it. */
	if (password != NULL) {
		e_book_authenticate_user (
			book, context->auth_username, password,
			context->auth_method, &error);

		g_free (password);
		password = NULL;

	/* The user did not wish to provide a password.  If the address
	 * book can be accessed anonymously, do that but warn about it. */
	} else if (e_book_check_static_capability (book, "anon-access")) {
		context->anonymous_alert = TRUE;
		goto exit;

	/* Final fallback is to fail. */
	} else {
		g_cancellable_cancel (cancellable);
		goto prompt;
	}

	/* If authentication failed, forget the password and reprompt. */
	if (g_error_matches (
		error, E_BOOK_ERROR, E_BOOK_ERROR_AUTHENTICATION_FAILED)) {
		e_passwords_forget_password (
			context->auth_component, context->auth_uri);
		g_clear_error (&error);
		goto prompt;

	} else if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_object_unref (book);
		g_error_free (error);
		return;
	}

exit:
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

	/* Source must have a group so we can obtain its URI. */
	g_return_if_fail (e_source_peek_group (source) != NULL);

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
	context->parent = parent;
	context->cancellable = cancellable;

	/* Extract authentication details from the ESource before
	 * spawning the thread, since ESource is not thread-safe. */
	load_book_source_get_auth_details (source, context);

	simple = g_simple_async_result_new (
		G_OBJECT (source), callback,
		user_data, e_load_book_source_async);

	g_simple_async_result_set_op_res_gpointer (
		simple, context, (GDestroyNotify)
		load_book_source_context_free);

	g_simple_async_result_run_in_thread (
		simple, (GSimpleAsyncThreadFunc) load_book_source_thread,
		G_PRIORITY_DEFAULT, context->cancellable);

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

	/* Alert the user that an address book is being accessed anonymously.
	 * FIXME Do not mention "LDAP", as this may apply to other backends. */
	if (context->anonymous_alert) {
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new (
			context->parent, 0,
			GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
			_("Accessing LDAP Server anonymously"));
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}

	e_source_set_property (
		source, "remember_password",
		context->auth_remember ? "true" : "false");

	return g_object_ref (context->book);
}
