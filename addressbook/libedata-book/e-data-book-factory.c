/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
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
 * Authors: Ross Burton <ross@linux.intel.com>
 */

/**
 * SECTION: e-data-book-factory
 * @include: libedata-book/libedata-book.h
 * @short_description: The main addressbook server object
 *
 * This class handles incomming D-Bus connections and creates
 * the #EDataBook layer for server side addressbooks to communicate
 * with client side #EBookClient objects.
 **/
#include <config.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gi18n.h>

/* Private D-Bus classes. */
#include <e-dbus-address-book-factory.h>

#include "e-book-backend.h"
#include "e-book-backend-factory.h"
#include "e-data-book.h"
#include "e-data-book-factory.h"
#include "e-dbus-localed.h"

#define d(x)

#define E_DATA_BOOK_FACTORY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_BOOK_FACTORY, EDataBookFactoryPrivate))

struct _EDataBookFactoryPrivate {
	EDBusAddressBookFactory *dbus_factory;

	/* Watching "org.freedesktop.locale1" for locale changes */
	guint localed_watch_id;
	EDBusLocale1 *localed_proxy;
	GCancellable *localed_cancel;
	gchar *locale;
};

static GInitableIface *initable_parent_interface;

/* Forward Declarations */
static void	e_data_book_factory_initable_init
						(GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (
	EDataBookFactory,
	e_data_book_factory,
	E_TYPE_DATA_FACTORY,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_data_book_factory_initable_init))

static GDBusInterfaceSkeleton *
data_book_factory_get_dbus_interface_skeleton (EDBusServer *server)
{
	EDataBookFactory *factory;

	factory = E_DATA_BOOK_FACTORY (server);

	return G_DBUS_INTERFACE_SKELETON (factory->priv->dbus_factory);
}

/* This is totally backwards, for some insane reason we holding
 * references to the EBookBackends and then fetching the EDataBook
 * via e_book_backend_ref_data_book(), the whole scheme should
 * be reversed and this function probably removed.
 */
static GList *
data_book_factory_list_books (EDataBookFactory *factory)
{
	GList *books = NULL;
	GSList *backends, *l;

	backends = e_data_factory_list_backends (E_DATA_FACTORY (factory));

	for (l = backends; l != NULL; l = g_slist_next (l)); {
		EBookBackend *backend = l->data;
		EDataBook *book = e_book_backend_ref_data_book (backend);

		if (g_list_find (books, book) == NULL)
			books = g_list_prepend (books, book);
		else
			g_object_unref (book);
	}

	return books;
}

static gchar *
data_book_factory_open (EDataFactory *data_factory,
			EBackend *backend,
			GDBusConnection *connection,
			GError **error)
{
	EDataBookFactory *factory = E_DATA_BOOK_FACTORY (data_factory);
	EDataBook *data_book;
	gchar *object_path;

	/* If the backend already has an EDataBook installed, return its
	 * object path.  Otherwise we need to install a new EDataBook. */

	data_book = e_book_backend_ref_data_book (E_BOOK_BACKEND (backend));

	if (data_book != NULL) {
		object_path = g_strdup (
			e_data_book_get_object_path (data_book));
	} else {
		object_path = e_data_factory_construct_path (data_factory);

		/* The EDataBook will attach itself to EBookBackend,
		 * so no need to call e_book_backend_set_data_book(). */
		data_book = e_data_book_new (
			E_BOOK_BACKEND (backend),
			connection, object_path, error);

		if (data_book != NULL) {
			e_data_factory_set_backend_callbacks (
				data_factory, backend);

			/* Don't set the locale on a new book if we have not
			 * yet received a notification of a locale change
			 */
			if (factory->priv->locale)
				e_data_book_set_locale (
					data_book,
					factory->priv->locale,
					NULL, NULL);
		} else {
			g_free (object_path);
			object_path = NULL;
		}
	}

	g_clear_object (&data_book);

	return object_path;
}

static gboolean
data_book_factory_handle_open_address_book_cb (EDBusAddressBookFactory *iface,
                                               GDBusMethodInvocation *invocation,
                                               const gchar *uid,
                                               EDataBookFactory *factory)
{
	GDBusConnection *connection;
	const gchar *sender;
	gchar *object_path;
	GError *error = NULL;

	connection = g_dbus_method_invocation_get_connection (invocation);
	sender = g_dbus_method_invocation_get_sender (invocation);

	object_path = e_data_factory_open_backend (
		E_DATA_FACTORY (factory), connection, sender,
		uid, E_SOURCE_EXTENSION_ADDRESS_BOOK, &error);

	if (object_path != NULL) {
		e_dbus_address_book_factory_complete_open_address_book (
			iface, invocation, object_path);
		g_free (object_path);
	} else {
		g_return_val_if_fail (error != NULL, FALSE);
		g_dbus_method_invocation_take_error (invocation, error);
	}

	return TRUE;
}

static void
data_book_factory_dispose (GObject *object)
{
	EDataBookFactory *factory;
	EDataBookFactoryPrivate *priv;

	factory = E_DATA_BOOK_FACTORY (object);
	priv = factory->priv;

	g_clear_object (&priv->dbus_factory);

	if (priv->localed_cancel)
		g_cancellable_cancel (priv->localed_cancel);

	g_clear_object (&priv->localed_cancel);
	g_clear_object (&priv->localed_proxy);

	if (priv->localed_watch_id > 0)
		g_bus_unwatch_name (priv->localed_watch_id);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_data_book_factory_parent_class)->dispose (object);
}

static void
data_book_factory_finalize (GObject *object)
{
	EDataBookFactory *factory;

	factory = E_DATA_BOOK_FACTORY (object);

	g_free (factory->priv->locale);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_book_factory_parent_class)->finalize (object);
}

static gchar *
data_book_factory_interpret_locale_value (const gchar *value)
{
	gchar *interpreted_value = NULL;
	gchar **split;

	split = g_strsplit (value, "=", 2);

	if (split && split[0] && split[1])
		interpreted_value = g_strdup (split[1]);

	g_strfreev (split);

	if (!interpreted_value)
		g_warning ("Failed to interpret locale value: %s", value);

	return interpreted_value;
}

static gchar *
data_book_factory_interpret_locale (const gchar * const * locale)
{
	gint i;
	gchar *interpreted_locale = NULL;

	/* Prioritize LC_COLLATE and then LANG values
	 * in the 'locale' specified by localed.
	 *
	 * If localed explicitly specifies no locale, then
	 * default to checking system locale.
	 */
	if (locale) {
		for (i = 0; locale[i] != NULL && interpreted_locale == NULL; i++) {
			if (strncmp (locale[i], "LC_COLLATE", 10) == 0)
				interpreted_locale =
					data_book_factory_interpret_locale_value (locale[i]);
		}

		for (i = 0; locale[i] != NULL && interpreted_locale == NULL; i++) {
			if (strncmp (locale[i], "LANG", 4) == 0)
				interpreted_locale =
					data_book_factory_interpret_locale_value (locale[i]);
		}
	}

	if (!interpreted_locale) {
		const gchar *system_locale = setlocale (LC_COLLATE, NULL);

		interpreted_locale = g_strdup (system_locale);
	}

	return interpreted_locale;
}

static void
data_book_factory_set_locale (EDataBookFactory *factory,
                              const gchar *locale)
{
	EDataBookFactoryPrivate *priv = factory->priv;
	GError *error = NULL;
	GList *books, *l;

	if (g_strcmp0 (priv->locale, locale) != 0) {

		g_free (priv->locale);
		priv->locale = g_strdup (locale);

		books = data_book_factory_list_books (factory);
		for (l = books; l; l = l->next) {
			EDataBook *book = l->data;

			if (!e_data_book_set_locale (book, locale, NULL, &error)) {
				g_warning (
					"Failed to set locale on addressbook: %s",
					error->message);
				g_clear_error (&error);
			}
		}
		g_list_free_full (books, g_object_unref);
	}
}

static void
data_book_factory_locale_changed (GObject *object,
                                  GParamSpec *pspec,
                                  gpointer user_data)
{
	EDBusLocale1 *locale_proxy = E_DBUS_LOCALE1 (object);
	EDataBookFactory *factory = (EDataBookFactory *) user_data;
	const gchar * const *locale;
	gchar *interpreted_locale;

	locale = e_dbus_locale1_get_locale (locale_proxy);
	interpreted_locale = data_book_factory_interpret_locale (locale);

	data_book_factory_set_locale (factory, interpreted_locale);

	g_free (interpreted_locale);
}

static void
data_book_factory_localed_ready (GObject *source_object,
                                 GAsyncResult *res,
                                 gpointer user_data)
{
	EDataBookFactory *factory = (EDataBookFactory *) user_data;
	GError *error = NULL;

	factory->priv->localed_proxy = e_dbus_locale1_proxy_new_finish (res, &error);

	if (factory->priv->localed_proxy == NULL) {
		g_warning ("Error fetching localed proxy: %s", error->message);
		g_error_free (error);
	}

	if (factory->priv->localed_cancel) {
		g_object_unref (factory->priv->localed_cancel);
		factory->priv->localed_cancel = NULL;
	}

	if (factory->priv->localed_proxy) {
		g_signal_connect (
			factory->priv->localed_proxy, "notify::locale",
			G_CALLBACK (data_book_factory_locale_changed), factory);

		/* Initial refresh of the locale */
		data_book_factory_locale_changed (G_OBJECT (factory->priv->localed_proxy), NULL, factory);
	}
}

static void
data_book_factory_localed_appeared (GDBusConnection *connection,
                                    const gchar *name,
                                    const gchar *name_owner,
                                    gpointer user_data)
{
	EDataBookFactory *factory = (EDataBookFactory *) user_data;

	factory->priv->localed_cancel = g_cancellable_new ();

	e_dbus_locale1_proxy_new (
		connection,
		G_DBUS_PROXY_FLAGS_GET_INVALIDATED_PROPERTIES,
		"org.freedesktop.locale1",
		"/org/freedesktop/locale1",
		factory->priv->localed_cancel,
		data_book_factory_localed_ready,
		factory);
}

static void
data_book_factory_localed_vanished (GDBusConnection *connection,
                                    const gchar *name,
                                    gpointer user_data)
{
	EDataBookFactory *factory = (EDataBookFactory *) user_data;

	if (factory->priv->localed_cancel) {
		g_cancellable_cancel (factory->priv->localed_cancel);
		g_object_unref (factory->priv->localed_cancel);
		factory->priv->localed_cancel = NULL;
	}

	if (factory->priv->localed_proxy) {
		g_object_unref (factory->priv->localed_proxy);
		factory->priv->localed_proxy = NULL;
	}
}

static gboolean
data_book_factory_initable_init (GInitable *initable,
                                 GCancellable *cancellable,
                                 GError **error)
{
	EDataBookFactory *factory;
	GBusType bus_type = G_BUS_TYPE_SYSTEM;

	factory = E_DATA_BOOK_FACTORY (initable);

	/* When running tests, we pretend to be the "org.freedesktop.locale1" service
	 * on the session bus instead of the real location on the system bus.
	 */
	if (g_getenv ("EDS_TESTING") != NULL)
		bus_type = G_BUS_TYPE_SESSION;

	/* Watch system bus for locale change notifications */
	factory->priv->localed_watch_id =
		g_bus_watch_name (
			bus_type,
			"org.freedesktop.locale1",
			G_BUS_NAME_WATCHER_FLAGS_NONE,
			data_book_factory_localed_appeared,
			data_book_factory_localed_vanished,
			initable,
			NULL);

	/* Chain up to parent interface's init() method. */
	return initable_parent_interface->init (initable, cancellable, error);
}

static void
e_data_book_factory_class_init (EDataBookFactoryClass *class)
{
	GObjectClass *object_class;
	EDBusServerClass *dbus_server_class;
	EDataFactoryClass *data_factory_class;
	const gchar *modules_directory = BACKENDDIR;
	const gchar *modules_directory_env;

	modules_directory_env = g_getenv (EDS_ADDRESS_BOOK_MODULES);
	if (modules_directory_env &&
	    g_file_test (modules_directory_env, G_FILE_TEST_IS_DIR))
		modules_directory = g_strdup (modules_directory_env);

	g_type_class_add_private (class, sizeof (EDataBookFactoryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = data_book_factory_dispose;
	object_class->finalize = data_book_factory_finalize;

	dbus_server_class = E_DBUS_SERVER_CLASS (class);
	dbus_server_class->bus_name = ADDRESS_BOOK_DBUS_SERVICE_NAME;
	dbus_server_class->module_directory = modules_directory;

	data_factory_class = E_DATA_FACTORY_CLASS (class);
	data_factory_class->backend_factory_type = E_TYPE_BOOK_BACKEND_FACTORY;
	data_factory_class->factory_object_path = "/org/gnome/evolution/dataserver/AddressBookFactory";
	data_factory_class->data_object_path_prefix = "/org/gnome/evolution/dataserver/Addressbook";
	data_factory_class->get_dbus_interface_skeleton = data_book_factory_get_dbus_interface_skeleton;
	data_factory_class->data_open = data_book_factory_open;
}

static void
e_data_book_factory_initable_init (GInitableIface *iface)
{
	initable_parent_interface = g_type_interface_peek_parent (iface);

	iface->init = data_book_factory_initable_init;
}

static void
e_data_book_factory_init (EDataBookFactory *factory)
{
	factory->priv = E_DATA_BOOK_FACTORY_GET_PRIVATE (factory);

	factory->priv->dbus_factory =
		e_dbus_address_book_factory_skeleton_new ();

	g_signal_connect (
		factory->priv->dbus_factory, "handle-open-address-book",
		G_CALLBACK (data_book_factory_handle_open_address_book_cb),
		factory);
}

EDBusServer *
e_data_book_factory_new (GCancellable *cancellable,
                         GError **error)
{
	return g_initable_new (
		E_TYPE_DATA_BOOK_FACTORY,
		cancellable, error, NULL);
}
