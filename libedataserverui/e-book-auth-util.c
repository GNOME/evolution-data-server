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
				      ESource *source, EBookCallback cb, gpointer closure);
static void auth_required_cb (EBook *book, gpointer data);
typedef struct {
	ESource       *source;
	EBook         *book;

	EBookCallback  open_func;
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
load_source_auth_cb (EBook *book, EBookStatus status, gpointer closure)
{
	LoadSourceData *data = closure;

	switch (status) {

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
			status = E_BOOK_ERROR_OK;
			break;

		case E_BOOK_ERROR_AUTHENTICATION_FAILED:
		case E_BOOK_ERROR_AUTHENTICATION_REQUIRED:
		{
			const gchar *uri = e_book_get_uri (book);
			gchar *stripped_uri = remove_parameters_from_uri (uri);
			const gchar *auth_domain = e_source_get_property (data->source, "auth-domain");
			const gchar *component_name;

			component_name = auth_domain ? auth_domain : "Addressbook";

			if (status == E_BOOK_ERROR_AUTHENTICATION_FAILED)
				e_passwords_forget_password (component_name, stripped_uri);

			addressbook_authenticate (book, TRUE, data->source, load_source_auth_cb, closure);

			g_free (stripped_uri);

			return;
		}

		default:
			break;
	}

	if (data->open_func)
		data->open_func (book, status, data->open_func_data);

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
			  EBookCallback cb, gpointer closure)
{
	const gchar *auth;
	const gchar *user;
	const gchar *component_name;
	const gchar *password     = NULL;
	gchar *pass_dup           = NULL;
	const gchar *uri               = e_book_get_uri (book);
        gchar *stripped_uri      = remove_parameters_from_uri (uri);
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
		pass_dup = e_passwords_ask_password (prompt, component_name, uri, prompt,
						     flags, &remember,
						     NULL);
		if (remember != get_remember_password (source))
			set_remember_password (source, remember);

		g_free (prompt);
	}

	if (password || pass_dup) {
		e_book_async_authenticate_user (book, user, password ? password : pass_dup,
						e_source_get_property (source, "auth"),
						cb, closure);
		g_free (pass_dup);
	}
	else {
		/* they hit cancel */
		cb (book, E_BOOK_ERROR_CANCELLED, closure);
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
load_source_cb (EBook *book, EBookStatus status, gpointer closure)
{
	LoadSourceData *load_source_data = closure;

	if (status == E_BOOK_ERROR_OK && book != NULL) {
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

	if (load_source_data->open_func)
		load_source_data->open_func (book, status, load_source_data->open_func_data);

	free_load_source_data (load_source_data);
}

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
	e_book_async_open (book, FALSE, load_source_cb, load_source_data);
	return book;
}
