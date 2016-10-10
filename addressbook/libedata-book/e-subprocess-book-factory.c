/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2014 Red Hat, Inc. (www.redhat.com)
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
 * Authors: Fabiano Fidêncio <fidencio@redhat.com>
 */

/*
 * This class handles and creates #EBackend objects from inside
 * their own subprocesses and also serves as the layer that does
 * the communication between #EDataBookFactory and #EBackend
 */

#include "evolution-data-server-config.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gi18n-lib.h>

#include "e-book-backend.h"
#include "e-book-backend-factory.h"
#include "e-data-book.h"
#include "e-dbus-localed.h"
#include "e-subprocess-book-factory.h"

#include <e-dbus-subprocess-backend.h>

#define E_SUBPROCESS_BOOK_FACTORY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SUBPROCESS_BOOK_FACTORY, ESubprocessBookFactoryPrivate))

struct _ESubprocessBookFactoryPrivate {
	/* Watching "org.freedesktop.locale1" for locale changes */
	guint localed_watch_id;
	guint subprocess_watch_id;
	EDBusLocale1 *localed_proxy;
	GCancellable *localed_cancel;
	gchar *locale;
};

static GInitableIface *initable_parent_interface;

/* Forward Declarations */
static void	e_subprocess_book_factory_initable_init
						(GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (
	ESubprocessBookFactory,
	e_subprocess_book_factory,
	E_TYPE_SUBPROCESS_FACTORY,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_subprocess_book_factory_initable_init))

static gchar *
subprocess_book_factory_open (ESubprocessFactory *subprocess_factory,
			      EBackend *backend,
			      GDBusConnection *connection,
			      gpointer data,
			      GCancellable *cancellable,
			      GError **error)
{
	ESubprocessBookFactory *subprocess_book_factory = E_SUBPROCESS_BOOK_FACTORY (subprocess_factory);
	EDataBook *data_book;
	gchar *object_path;

	/* If the backend already has an EDataBook installed, return its
	 * object path.  Otherwise we need to install a new EDataBook. */
	data_book = e_book_backend_ref_data_book (E_BOOK_BACKEND (backend));

	if (data_book != NULL) {
		object_path = g_strdup (e_data_book_get_object_path (data_book));
	} else {
		object_path = e_subprocess_factory_construct_path ();

		/* The EDataBook will attach itself to EBookBackend,
		 * so no need to call e_book_backend_set_data_book(). */
		data_book = e_data_book_new (
			E_BOOK_BACKEND (backend),
			connection, object_path, error);

		if (data_book != NULL) {
			e_subprocess_factory_set_backend_callbacks (
				subprocess_factory, backend, data);

			/* Don't set the locale on a new book if we have not
			 * yet received a notification of a locale change
			 */
			if (subprocess_book_factory->priv->locale)
				e_data_book_set_locale (
					data_book,
					subprocess_book_factory->priv->locale,
					NULL, NULL);
		} else {
			g_free (object_path);
			object_path = NULL;
		}
	}

	g_clear_object (&data_book);

	return object_path;
}

static EBackend *
subprocess_book_factory_ref_backend (ESourceRegistry *registry,
				     ESource *source,
				     const gchar *backend_factory_type_name)
{
	EBookBackendFactoryClass *backend_factory_class;
	GType backend_factory_type;

	backend_factory_type = g_type_from_name (backend_factory_type_name);
	if (!backend_factory_type)
		return NULL;

	backend_factory_class = g_type_class_ref (backend_factory_type);
	if (!backend_factory_class)
		return NULL;

	return g_object_new (
		backend_factory_class->backend_type,
		"registry", registry,
		"source", source, NULL);
}

static void
subprocess_book_factory_dispose (GObject *object)
{
	ESubprocessBookFactory *subprocess_factory;
	ESubprocessBookFactoryPrivate *priv;

	subprocess_factory = E_SUBPROCESS_BOOK_FACTORY (object);
	priv = subprocess_factory->priv;

	if (priv->localed_cancel)
		g_cancellable_cancel (priv->localed_cancel);

	g_clear_object (&priv->localed_cancel);
	g_clear_object (&priv->localed_proxy);

	if (priv->localed_watch_id > 0)
		g_bus_unwatch_name (priv->localed_watch_id);

	if (priv->subprocess_watch_id > 0)
		g_bus_unwatch_name (priv->subprocess_watch_id);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_subprocess_book_factory_parent_class)->dispose (object);
}

static void
subprocess_book_factory_finalize (GObject *object)
{
	ESubprocessBookFactory *subprocess_factory;

	subprocess_factory = E_SUBPROCESS_BOOK_FACTORY (object);

	g_free (subprocess_factory->priv->locale);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_subprocess_book_factory_parent_class)->finalize (object);
}

static gchar *
subprocess_book_factory_interpret_locale_value (const gchar *value)
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
subprocess_book_factory_interpret_locale (const gchar * const * locale)
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
					subprocess_book_factory_interpret_locale_value (locale[i]);
		}

		for (i = 0; locale[i] != NULL && interpreted_locale == NULL; i++) {
			if (strncmp (locale[i], "LANG", 4) == 0)
				interpreted_locale =
					subprocess_book_factory_interpret_locale_value (locale[i]);
		}
	}

	if (!interpreted_locale) {
		const gchar *system_locale = setlocale (LC_COLLATE, NULL);

		interpreted_locale = g_strdup (system_locale);
	}

	return interpreted_locale;
}

static void
subprocess_book_factory_set_locale (ESubprocessBookFactory *subprocess_factory,
				    const gchar *locale)
{
	ESubprocessBookFactoryPrivate *priv = subprocess_factory->priv;
	GError *error = NULL;

	if (g_strcmp0 (priv->locale, locale) != 0) {
		GList *backends, *l;

		g_free (priv->locale);
		priv->locale = g_strdup (locale);

		backends = e_subprocess_factory_get_backends_list (E_SUBPROCESS_FACTORY (subprocess_factory));

		for (l = backends; l != NULL; l = g_list_next (l)) {
			EBackend *backend = l->data;
			EDataBook *data_book;

			data_book = e_book_backend_ref_data_book (E_BOOK_BACKEND (backend));

			if (!e_data_book_set_locale (data_book, locale, NULL, &error)) {
				g_warning (
					"Failed to set locale on addressbook: %s",
					error->message);
				g_clear_error (&error);
			}

			g_object_unref (data_book);
		}

		g_list_free_full (backends, g_object_unref);
	}
}

static void
subprocess_book_factory_locale_changed (GObject *object,
					GParamSpec *pspec,
					gpointer user_data)
{
	EDBusLocale1 *locale_proxy = E_DBUS_LOCALE1 (object);
	ESubprocessBookFactory *factory = (ESubprocessBookFactory *) user_data;
	const gchar * const *locale;
	gchar *interpreted_locale;

	locale = e_dbus_locale1_get_locale (locale_proxy);
	interpreted_locale = subprocess_book_factory_interpret_locale (locale);

	subprocess_book_factory_set_locale (factory, interpreted_locale);

	g_free (interpreted_locale);
}

static void
subprocess_book_factory_localed_ready (GObject *source_object,
				       GAsyncResult *res,
				       gpointer user_data)
{
	ESubprocessBookFactory *subprocess_factory = (ESubprocessBookFactory *) user_data;
	GError *error = NULL;

	subprocess_factory->priv->localed_proxy = e_dbus_locale1_proxy_new_finish (res, &error);

	if (subprocess_factory->priv->localed_proxy == NULL) {
		g_warning ("Error fetching localed proxy: %s", error->message);
		g_error_free (error);
	}

	g_clear_object (&subprocess_factory->priv->localed_cancel);

	if (subprocess_factory->priv->localed_proxy) {
		g_signal_connect (
			subprocess_factory->priv->localed_proxy, "notify::locale",
			G_CALLBACK (subprocess_book_factory_locale_changed), subprocess_factory);

		/* Initial refresh of the locale */
		subprocess_book_factory_locale_changed (
			G_OBJECT (subprocess_factory->priv->localed_proxy), NULL, subprocess_factory);
	}
}

static void
subprocess_book_factory_localed_appeared (GDBusConnection *connection,
					  const gchar *name,
					  const gchar *name_owner,
					  gpointer user_data)
{
	ESubprocessBookFactory *subprocess_factory = (ESubprocessBookFactory *) user_data;

	subprocess_factory->priv->localed_cancel = g_cancellable_new ();

	e_dbus_locale1_proxy_new (
		connection,
		G_DBUS_PROXY_FLAGS_GET_INVALIDATED_PROPERTIES,
		"org.freedesktop.locale1",
		"/org/freedesktop/locale1",
		subprocess_factory->priv->localed_cancel,
		subprocess_book_factory_localed_ready,
		subprocess_factory);
}

static void
subprocess_book_factory_localed_vanished (GDBusConnection *connection,
					  const gchar *name,
					  gpointer user_data)
{
	ESubprocessBookFactory *subprocess_factory = (ESubprocessBookFactory *) user_data;

	if (subprocess_factory->priv->localed_cancel) {
		g_cancellable_cancel (subprocess_factory->priv->localed_cancel);
		g_clear_object (&subprocess_factory->priv->localed_cancel);
	}

	g_clear_object (&subprocess_factory->priv->localed_proxy);
}

static void
e_subprocess_book_factory_class_init (ESubprocessBookFactoryClass *class)
{
	GObjectClass *object_class;
	ESubprocessFactoryClass *subprocess_factory_class;

	g_type_class_add_private (class, sizeof (ESubprocessBookFactoryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = subprocess_book_factory_dispose;
	object_class->finalize = subprocess_book_factory_finalize;

	subprocess_factory_class = E_SUBPROCESS_FACTORY_CLASS (class);
	subprocess_factory_class->ref_backend = subprocess_book_factory_ref_backend;
	subprocess_factory_class->open_data = subprocess_book_factory_open;
}

static gboolean
subprocess_book_factory_initable_init (GInitable *initable,
				       GCancellable *cancellable,
				       GError **error)
{
	ESubprocessBookFactory *subprocess_factory;
	GBusType bus_type = G_BUS_TYPE_SYSTEM;

	subprocess_factory = E_SUBPROCESS_BOOK_FACTORY (initable);

	/* When running tests, we pretend to be the "org.freedesktop.locale1" service
	 * on the session bus instead of the real location on the system bus.
	 */
	if (g_getenv ("EDS_TESTING") != NULL)
		bus_type = G_BUS_TYPE_SESSION;

	/* Watch system bus for locale change notifications */
	subprocess_factory->priv->localed_watch_id =
		g_bus_watch_name (
			bus_type,
			"org.freedesktop.locale1",
			G_BUS_NAME_WATCHER_FLAGS_NONE,
			subprocess_book_factory_localed_appeared,
			subprocess_book_factory_localed_vanished,
			initable,
			NULL);

	/* Chain up to parent interface's init() method. */
	return initable_parent_interface->init (initable, cancellable, error);
}

static void
e_subprocess_book_factory_initable_init (GInitableIface *iface)
{
	initable_parent_interface = g_type_interface_peek_parent (iface);

	iface->init = subprocess_book_factory_initable_init;
}

static void
e_subprocess_book_factory_init (ESubprocessBookFactory *subprocess_factory)
{
	subprocess_factory->priv = E_SUBPROCESS_BOOK_FACTORY_GET_PRIVATE (subprocess_factory);
}

ESubprocessBookFactory *
e_subprocess_book_factory_new (GCancellable *cancellable,
			       GError **error)
{
	return g_initable_new (
		E_TYPE_SUBPROCESS_BOOK_FACTORY,
		cancellable, error, NULL);
}
