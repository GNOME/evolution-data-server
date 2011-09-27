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
 * Author: Ross Burton <ross@linux.intel.com>
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gi18n.h>

#ifdef ENABLE_MAINTAINER_MODE
#include <gtk/gtk.h>
#endif

#ifdef G_OS_UNIX
#if GLIB_CHECK_VERSION(2,29,5)
#include <glib-unix.h>
#endif
#endif

#ifdef HAVE_GOA
#define GOA_API_IS_SUBJECT_TO_CHANGE
#include <goa/goa.h>

/* This is the property name or URL parameter under which we
 * embed the GoaAccount ID into an EAccount or ESource object. */
#define GOA_KEY "goa-account-id"
#endif

#include "e-book-backend-factory.h"
#include "e-data-book-factory.h"
#include "e-data-book.h"
#include "e-book-backend.h"

#include "e-gdbus-book-factory.h"

#ifdef G_OS_WIN32
#include <windows.h>
#include <conio.h>
#ifndef PROCESS_DEP_ENABLE
#define PROCESS_DEP_ENABLE 0x00000001
#endif
#ifndef PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION
#define PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION 0x00000002
#endif
#endif

#define d(x)

#define E_DATA_BOOK_FACTORY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_BOOK_FACTORY, EDataBookFactoryPrivate))

/* Keep running after the last client is closed. */
static gboolean opt_keep_running = FALSE;

/* Convenience macro to test and set a GError/return on failure */
#define g_set_error_val_if_fail(test, returnval, error, domain, code) \
	G_STMT_START {							\
	if G_LIKELY (test) {						\
	} else {							\
		g_set_error_literal (error, domain, code, #test);	\
		g_warning (#test " failed");				\
		return (returnval);					\
	}								\
	} G_STMT_END

G_DEFINE_TYPE (EDataBookFactory, e_data_book_factory, E_TYPE_DATA_FACTORY);

struct _EDataBookFactoryPrivate {
	EGdbusBookFactory *gdbus_object;

	GMutex *books_lock;
	/* A hash of object paths for book URIs to EDataBooks */
	GHashTable *books;

	GMutex *connections_lock;
	/* This is a hash of client addresses to GList* of EDataBooks */
	GHashTable *connections;

	guint exit_timeout;

#ifdef HAVE_GOA
	GoaClient *goa_client;
	GHashTable *goa_accounts;
#endif
};

/* Forward Declarations */
void e_data_book_migrate_basedir (void);

static gchar *
e_data_book_factory_extract_proto_from_uri (const gchar *uri)
{
	gchar *proto, *cp;

	cp = strchr (uri, ':');
	if (cp == NULL)
		return NULL;

	proto = g_malloc0 (cp - uri + 1);
	strncpy (proto, uri, cp - uri);

	return proto;
}

static EBackend *
e_data_book_factory_get_backend (EDataBookFactory *factory,
                                 ESource *source,
                                 const gchar *uri)
{
	EBackend *backend;
	gchar *hash_key;

	hash_key = e_data_book_factory_extract_proto_from_uri (uri);
	if (hash_key == NULL) {
		g_warning ("Cannot extract protocol from URI %s", uri);
		return NULL;
	}

	backend = e_data_factory_get_backend (
		E_DATA_FACTORY (factory), hash_key, source);

	g_free (hash_key);

	return backend;
}

static gchar *
construct_book_factory_path (void)
{
	static volatile gint counter = 1;

	g_atomic_int_inc (&counter);

	return g_strdup_printf (
		"/org/gnome/evolution/dataserver/AddressBook/%d/%u",
		getpid (), counter);
}

static gboolean
remove_dead_pointer_cb (gpointer path,
                        gpointer live,
                        gpointer dead)
{
	return live == dead;
}

static void
book_freed_cb (EDataBookFactory *factory,
               GObject *dead)
{
	EDataBookFactoryPrivate *priv = factory->priv;
	GHashTableIter iter;
	gpointer hkey, hvalue;

	d (g_debug ("in factory %p (%p) is dead", factory, dead));

	g_hash_table_foreach_remove (
		priv->books, remove_dead_pointer_cb, dead);

	g_hash_table_iter_init (&iter, priv->connections);
	while (g_hash_table_iter_next (&iter, &hkey, &hvalue)) {
		GList *books = hvalue;

		if (g_list_find (books, dead)) {
			books = g_list_remove (books, dead);
			if (books != NULL)
				g_hash_table_insert (
					priv->connections,
					g_strdup (hkey), books);
			else
				g_hash_table_remove (priv->connections, hkey);

			break;
		}
	}

	if (g_hash_table_size (priv->books) > 0)
		return;

	/* If there are no open books, start a timer to quit */
	if (!opt_keep_running && priv->exit_timeout == 0)
		priv->exit_timeout = g_timeout_add_seconds (
			10, (GSourceFunc) e_dbus_server_quit, factory);
}

static gboolean
impl_BookFactory_get_book (EGdbusBookFactory *object,
                           GDBusMethodInvocation *invocation,
                           const gchar *in_source,
                           EDataBookFactory *factory)
{
	EDataBook *book;
	EBackend *backend;
	EDataBookFactoryPrivate *priv = factory->priv;
	GDBusConnection *connection;
	ESource *source;
	gchar *path;
	gchar *uri;
	const gchar *sender;
	GList *list;
	GError *error = NULL;

	sender = g_dbus_method_invocation_get_sender (invocation);
	connection = g_dbus_method_invocation_get_connection (invocation);

	if (in_source == NULL || in_source[0] == '\0') {
		error = g_error_new (
			E_DATA_BOOK_ERROR,
			E_DATA_BOOK_STATUS_NO_SUCH_BOOK,
			_("Empty URI"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	source = e_source_new_from_standalone_xml (in_source);
	if (!source) {
		error = g_error_new (
			E_DATA_BOOK_ERROR,
			E_DATA_BOOK_STATUS_NO_SUCH_BOOK,
			_("Invalid source"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	uri = e_source_get_uri (source);

	if (uri == NULL || *uri == '\0') {
		g_object_unref (source);
		g_free (uri);

		error = g_error_new (
			E_DATA_BOOK_ERROR,
			E_DATA_BOOK_STATUS_NO_SUCH_BOOK,
			_("Empty URI"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	backend = e_data_book_factory_get_backend (factory, source, uri);

	if (backend == NULL) {
		g_free (uri);
		g_object_unref (source);

		error = g_error_new (
			E_DATA_BOOK_ERROR,
			E_DATA_BOOK_STATUS_NO_SUCH_BOOK,
			_("Invalid source"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	g_mutex_lock (priv->books_lock);

	/* Remove a pending exit */
	if (priv->exit_timeout) {
		g_source_remove (priv->exit_timeout);
		priv->exit_timeout = 0;
	}

#ifdef HAVE_GOA
	{
		GoaObject *goa_object = NULL;
		const gchar *goa_account_id;

		/* Embed the corresponding GoaObject in the EBookBackend
		 * so the backend can retrieve it.  We're not ready to add
		 * formal API for this to EBookBackend just yet. */
		goa_account_id = e_source_get_property (source, GOA_KEY);
		if (goa_account_id != NULL)
			goa_object = g_hash_table_lookup (
				factory->priv->goa_accounts, goa_account_id);
		if (GOA_IS_OBJECT (goa_object))
			g_object_set_data_full (
				G_OBJECT (backend),
				"GNOME Online Account",
				g_object_ref (goa_object),
				(GDestroyNotify) g_object_unref);
	}
#endif

	path = construct_book_factory_path ();
	book = e_data_book_new (E_BOOK_BACKEND (backend));
	g_hash_table_insert (priv->books, g_strdup (path), book);
	e_book_backend_add_client (E_BOOK_BACKEND (backend), book);
	e_data_book_register_gdbus_object (book, connection, path, &error);
	g_object_weak_ref (
		G_OBJECT (book), (GWeakNotify)
		book_freed_cb, factory);

	/* Update the hash of open connections. */
	g_mutex_lock (priv->connections_lock);
	list = g_hash_table_lookup (priv->connections, sender);
	list = g_list_prepend (list, book);
	g_hash_table_insert (priv->connections, g_strdup (sender), list);
	g_mutex_unlock (priv->connections_lock);

	g_mutex_unlock (priv->books_lock);

	g_object_unref (source);
	g_free (uri);

	e_gdbus_book_factory_complete_get_book (
		object, invocation, path, error);

	if (error)
		g_error_free (error);

	g_free (path);

	return TRUE;
}

static void
remove_data_book_cb (EDataBook *data_book)
{
	EBookBackend *backend;

	g_return_if_fail (data_book != NULL);

	backend = e_data_book_get_backend (data_book);
	e_book_backend_remove_client (backend, data_book);

	g_object_unref (data_book);
}

static void
e_data_book_factory_dispose (GObject *object)
{
	EDataBookFactoryPrivate *priv;

	priv = E_DATA_BOOK_FACTORY_GET_PRIVATE (object);

	if (priv->gdbus_object != NULL) {
		g_object_unref (priv->gdbus_object);
		priv->gdbus_object = NULL;
	}

#ifdef HAVE_GOA
	if (priv->goa_client != NULL) {
		g_object_unref (priv->goa_client);
		priv->goa_client = NULL;
	}

	g_hash_table_remove_all (priv->goa_accounts);
#endif

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_book_factory_parent_class)->dispose (object);
}

static void
e_data_book_factory_finalize (GObject *object)
{
	EDataBookFactoryPrivate *priv;

	priv = E_DATA_BOOK_FACTORY_GET_PRIVATE (object);

	g_hash_table_destroy (priv->books);
	g_hash_table_destroy (priv->connections);

	g_mutex_free (priv->books_lock);
	g_mutex_free (priv->connections_lock);

#ifdef HAVE_GOA
	g_hash_table_destroy (priv->goa_accounts);
#endif

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_book_factory_parent_class)->finalize (object);
}

static void
data_book_factory_bus_acquired (EDBusServer *server,
                                GDBusConnection *connection)
{
	EDataBookFactoryPrivate *priv;
	guint registration_id;
	GError *error = NULL;

	priv = E_DATA_BOOK_FACTORY_GET_PRIVATE (server);

	registration_id = e_gdbus_book_factory_register_object (
		priv->gdbus_object,
		connection,
		"/org/gnome/evolution/dataserver/AddressBookFactory",
		&error);

	if (error != NULL) {
		g_error (
			"Failed to register a BookFactory object: %s",
			error->message);
		g_assert_not_reached ();
	}

	g_assert (registration_id > 0);

	/* Chain up to parent's bus_acquired() method. */
	E_DBUS_SERVER_CLASS (e_data_book_factory_parent_class)->
		bus_acquired (server, connection);
}

static void
data_book_factory_bus_name_lost (EDBusServer *server,
                                 GDBusConnection *connection)
{
	EDataBookFactoryPrivate *priv;
	GList *list = NULL;
	gchar *key;

	priv = E_DATA_BOOK_FACTORY_GET_PRIVATE (server);

	g_mutex_lock (priv->connections_lock);

	while (g_hash_table_lookup_extended (
		priv->connections,
		ADDRESS_BOOK_DBUS_SERVICE_NAME,
		(gpointer) &key, (gpointer) &list)) {
		GList *copy;

		/* this should trigger the book's weak ref notify
		 * function, which will remove it from the list before
		 * it's freed, and will remove the connection from
		 * priv->connections once they're all gone */
		copy = g_list_copy (list);
		g_list_foreach (copy, (GFunc) remove_data_book_cb, NULL);
		g_list_free (copy);
	}

	g_mutex_unlock (priv->connections_lock);

	/* Chain up to parent's bus_name_lost() method. */
	E_DBUS_SERVER_CLASS (e_data_book_factory_parent_class)->
		bus_name_lost (server, connection);
}

static void
e_data_book_factory_class_init (EDataBookFactoryClass *class)
{
	GObjectClass *object_class;
	EDBusServerClass *dbus_server_class;

	g_type_class_add_private (class, sizeof (EDataBookFactoryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = e_data_book_factory_dispose;
	object_class->finalize = e_data_book_factory_finalize;

	dbus_server_class = E_DBUS_SERVER_CLASS (class);
	dbus_server_class->bus_name = ADDRESS_BOOK_DBUS_SERVICE_NAME;
	dbus_server_class->module_directory = BACKENDDIR;
	dbus_server_class->bus_acquired = data_book_factory_bus_acquired;
	dbus_server_class->bus_name_lost = data_book_factory_bus_name_lost;
}

#ifdef HAVE_GOA
static void
e_data_book_factory_update_goa_accounts (EDataBookFactory *factory)
{
	GList *list, *iter;

	g_hash_table_remove_all (factory->priv->goa_accounts);

	list = goa_client_get_accounts (factory->priv->goa_client);

	for (iter = list; iter != NULL; iter = g_list_next (iter)) {
		GoaObject *goa_object;
		GoaAccount *goa_account;
		const gchar *goa_account_id;

		goa_object = GOA_OBJECT (iter->data);
		goa_account = goa_object_peek_account (goa_object);
		goa_account_id = goa_account_get_id (goa_account);

		/* Takes ownership of the GoaObject. */
		g_hash_table_insert (
				     factory->priv->goa_accounts,
				     g_strdup (goa_account_id), goa_object);
	}

	g_list_free (list);
}

static void
e_data_book_factory_accounts_changed_cb (GoaClient *client, GDBusObject *object, EDataBookFactory *factory)
{
	e_data_book_factory_update_goa_accounts (factory);
}

#endif



static void
e_data_book_factory_init (EDataBookFactory *factory)
{
#ifdef HAVE_GOA
	GError *error = NULL;
#endif

	factory->priv = E_DATA_BOOK_FACTORY_GET_PRIVATE (factory);

	factory->priv->gdbus_object = e_gdbus_book_factory_stub_new ();
	g_signal_connect (
		factory->priv->gdbus_object, "handle-get-book",
		G_CALLBACK (impl_BookFactory_get_book), factory);

	factory->priv->books_lock = g_mutex_new ();
	factory->priv->books = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	factory->priv->connections_lock = g_mutex_new ();
	factory->priv->connections = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

#ifdef HAVE_GOA
	factory->priv->goa_accounts = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);

	/* Failure here is non-fatal, just emit a terminal warning. */
	factory->priv->goa_client = goa_client_new_sync (NULL, &error);

	if (factory->priv->goa_client != NULL) {
		e_data_book_factory_update_goa_accounts (factory);

		g_signal_connect (factory->priv->goa_client,
				  "account_added", e_data_book_factory_accounts_changed_cb, factory);
		g_signal_connect (factory->priv->goa_client,
				  "account_removed", e_data_book_factory_accounts_changed_cb, factory);
		g_signal_connect (factory->priv->goa_client,
				  "account_changed", e_data_book_factory_accounts_changed_cb, factory);
	} else if (error != NULL) {
		g_warning ("%s", error->message);
		g_error_free (error);
	}
#endif
}

static GOptionEntry entries[] = {

	/* FIXME Have the description translated for 3.2, but this
	 *       option is to aid in testing and development so it
	 *       doesn't really matter. */
	{ "keep-running", 'r', 0, G_OPTION_ARG_NONE, &opt_keep_running,
	  "Keep running after the last client is closed", NULL },
	{ NULL }
};

gint
main (gint argc,
      gchar **argv)
{
	GOptionContext *context;
	EDBusServer *server;
	GError *error = NULL;

#ifdef G_OS_WIN32
	/* Reduce risks */
	{
		typedef BOOL (WINAPI *t_SetDllDirectoryA) (LPCSTR lpPathName);
		t_SetDllDirectoryA p_SetDllDirectoryA;

		p_SetDllDirectoryA = GetProcAddress (GetModuleHandle ("kernel32.dll"), "SetDllDirectoryA");
		if (p_SetDllDirectoryA)
			(*p_SetDllDirectoryA) ("");
	}
#ifndef _WIN64
	{
		typedef BOOL (WINAPI *t_SetProcessDEPPolicy) (DWORD dwFlags);
		t_SetProcessDEPPolicy p_SetProcessDEPPolicy;

		p_SetProcessDEPPolicy = GetProcAddress (GetModuleHandle ("kernel32.dll"), "SetProcessDEPPolicy");
		if (p_SetProcessDEPPolicy)
			(*p_SetProcessDEPPolicy) (PROCESS_DEP_ENABLE | PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION);
	}
#endif
#endif

	g_type_init ();
	g_set_prgname (E_PRGNAME);
	if (!g_thread_supported ()) g_thread_init (NULL);

	#ifdef ENABLE_MAINTAINER_MODE
	/* only to load gtk-modules, like bug-buddy's gnomesegvhandler, if possible */
	gtk_init_check (&argc, &argv);
	#endif

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
	g_option_context_parse (context, &argc, &argv, &error);
	g_option_context_free (context);

	if (error != NULL) {
		g_printerr ("%s\n", error->message);
		exit (EXIT_FAILURE);
	}

	/* Migrate user data from ~/.evolution to XDG base directories. */
	e_data_book_migrate_basedir ();

	server = g_initable_new (
		E_TYPE_DATA_BOOK_FACTORY, NULL, &error, NULL);

	if (error != NULL) {
		g_printerr ("%s\n", error->message);
		exit (EXIT_FAILURE);
	}

	g_print ("Server is up and running...\n");

	e_dbus_server_run (server);

	g_object_unref (server);

	g_print ("Bye.\n");

	return 0;
}
