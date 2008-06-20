/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <bonobo-activation/bonobo-activation.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-arg.h>
#include "libebackend/e-data-server-module.h"
#include "e-data-book-factory.h"

#include <backends/groupwise/e-book-backend-groupwise.h>

#define DEFAULT_E_DATA_BOOK_FACTORY_OAF_ID "OAFIID:GNOME_Evolution_DataServer_BookFactory:" BASE_VERSION

static BonoboObjectClass          *e_data_book_factory_parent_class;

typedef struct {
	char                                     *uri;
	GNOME_Evolution_Addressbook_BookListener  listener;
} EDataBookFactoryQueuedRequest;

struct _EDataBookFactoryPrivate {
	GMutex *map_mutex;

	GHashTable *backends;
	GHashTable *active_server_map;

	/* OAFIID of the factory */
	char *iid;

	/* Whether the factory has been registered with OAF yet */
	guint       registered : 1;

	int mode;
};

/* Signal IDs */
enum {
	LAST_BOOK_GONE,
	LAST_SIGNAL
};

static guint factory_signals[LAST_SIGNAL];

static char *
e_data_book_factory_canonicalize_uri (const char *uri)
{
	/* FIXME: What do I do here? */

	return g_strdup (uri);
}

static char *
e_data_book_factory_extract_proto_from_uri (const char *uri)
{
	char *proto;
	char *p;

	p = strchr (uri, ':');

	if (p == NULL)
		return NULL;

	proto = g_malloc0 (p - uri + 1);

	strncpy (proto, uri, p - uri);

	return proto;
}

/**
 * e_data_book_factory_register_backend:
 * @factory: an #EDataBookFactory
 * @backend_factory: an #EBookBackendFactory
 *
 * Registers @backend_factory with @factory.
 **/
void
e_data_book_factory_register_backend (EDataBookFactory      *book_factory,
				      EBookBackendFactory   *backend_factory)
{
	const char *proto;

	g_return_if_fail (E_IS_DATA_BOOK_FACTORY (book_factory));
	g_return_if_fail (E_IS_BOOK_BACKEND_FACTORY (backend_factory));

	proto = E_BOOK_BACKEND_FACTORY_GET_CLASS (backend_factory)->get_protocol (backend_factory);

	if (g_hash_table_lookup (book_factory->priv->backends, proto) != NULL) {
		g_warning ("e_data_book_factory_register_backend: "
			   "Proto \"%s\" already registered!\n", proto);
	}

	g_hash_table_insert (book_factory->priv->backends,
			     g_strdup (proto), backend_factory);
}

static void
out_of_proc_check (gpointer key, gpointer value, gpointer data)
{
	gboolean *out_of_proc = data;

	if ((*out_of_proc))
	    return;

	*out_of_proc = e_book_backend_has_out_of_proc_clients (value);
}

/**
 * e_data_book_factory_get_n_backends:
 * @factory: An addressbook factory.
 *
 * Queries the number of running addressbook backends in an addressbook factory.
 *
 * Return value: Number of running backends.
 **/
int
e_data_book_factory_get_n_backends (EDataBookFactory *factory)
{
	int n_backends;
	gboolean out_of_proc = FALSE;

	g_return_val_if_fail (factory != NULL, -1);
	g_return_val_if_fail (E_IS_DATA_BOOK_FACTORY (factory), -1);

	g_mutex_lock (factory->priv->map_mutex);
	g_hash_table_foreach (factory->priv->active_server_map, out_of_proc_check, &out_of_proc);

	if (!out_of_proc)
		n_backends = 0;
	else
		n_backends = g_hash_table_size (factory->priv->active_server_map);
	g_mutex_unlock (factory->priv->map_mutex);

	return n_backends;
}

/**
 * e_data_book_factory_register_backends:
 * @book_factory: an #EDataBookFactory
 *
 * Register the backends supported by the Evolution Data Server,
 * with @book_factory.
 **/
void
e_data_book_factory_register_backends (EDataBookFactory *book_factory)
{
	GList *factories, *f;

	factories = e_data_server_get_extensions_for_type (E_TYPE_BOOK_BACKEND_FACTORY);
	for (f = factories; f; f = f->next) {
		EBookBackendFactory *backend_factory = f->data;

		e_data_book_factory_register_backend (book_factory, g_object_ref (backend_factory));
	}

	e_data_server_extension_list_free (factories);
}

static void
dump_active_server_map_entry (gpointer key, gpointer value, gpointer data)
{
	char *uri;
	EBookBackend *backend;

	uri = key;
	backend = E_BOOK_BACKEND (value);

	g_message ("  %s: %p", uri, backend);
}

/**
 * e_data_book_factory_dump_active_backends:
 * @factory: an #EDataBookFactory
 *
 * Dump the list of active backends registered with @factory
 * to stdout. This is a debugging function.
 **/
void
e_data_book_factory_dump_active_backends (EDataBookFactory *factory)
{
	g_message ("Active PAS backends");

	g_mutex_lock (factory->priv->map_mutex);
	g_hash_table_foreach (factory->priv->active_server_map,
			      dump_active_server_map_entry,
			      NULL);
	g_mutex_unlock (factory->priv->map_mutex);
}

/* Callback used when a backend loses its last connected client */
static void
backend_last_client_gone_cb (EBookBackend *backend, gpointer data)
{
	EDataBookFactory *factory;
	ESource *source;
	gchar *uri;

	factory = E_DATA_BOOK_FACTORY (data);

	/* Remove the backend from the active server map */

	source = e_book_backend_get_source (backend);
	if (source)
		uri = e_source_get_uri (source);
	else
		uri = NULL;

	if (uri) {
		g_mutex_lock (factory->priv->map_mutex);
		g_hash_table_remove (factory->priv->active_server_map, uri);
		g_mutex_unlock (factory->priv->map_mutex);
	}

	if (g_hash_table_size (factory->priv->active_server_map) == 0) {
		/* Notify upstream if there are no more backends */
		g_signal_emit (G_OBJECT (factory), factory_signals[LAST_BOOK_GONE], 0);
	}

	g_free (uri);
}



static EBookBackendFactory*
e_data_book_factory_lookup_backend_factory (EDataBookFactory *factory,
					    const char     *uri)
{
	EBookBackendFactory *backend_factory;
	char                *proto;
	char                *canonical_uri;

	g_assert (factory != NULL);
	g_assert (E_IS_DATA_BOOK_FACTORY (factory));
	g_assert (uri != NULL);

	canonical_uri = e_data_book_factory_canonicalize_uri (uri);
	if (canonical_uri == NULL)
		return NULL;

	proto = e_data_book_factory_extract_proto_from_uri (canonical_uri);
	if (proto == NULL) {
		g_free (canonical_uri);
		return NULL;
	}

	backend_factory = g_hash_table_lookup (factory->priv->backends, proto);

	g_free (proto);
	g_free (canonical_uri);

	return backend_factory;
}

static EBookBackend *
e_data_book_factory_launch_backend (EDataBookFactory      *book_factory,
				    EBookBackendFactory   *backend_factory,
				    GNOME_Evolution_Addressbook_BookListener listener,
				    const char          *uri)
{
	EBookBackend          *backend;

	backend = e_book_backend_factory_new_backend (backend_factory);
	if (!backend)
		return NULL;

	g_hash_table_insert (book_factory->priv->active_server_map,
			     g_strdup (uri),
			     backend);

	g_signal_connect (backend, "last_client_gone",
			  G_CALLBACK (backend_last_client_gone_cb),
			  book_factory);

	return backend;
}

static GNOME_Evolution_Addressbook_Book
impl_GNOME_Evolution_Addressbook_BookFactory_getBook (PortableServer_Servant        servant,
						      const CORBA_char             *source_xml,
						      const GNOME_Evolution_Addressbook_BookListener listener,
						      CORBA_Environment            *ev)
{
	EDataBookFactory      *factory = E_DATA_BOOK_FACTORY (bonobo_object (servant));
	GNOME_Evolution_Addressbook_Book corba_book;
	EBookBackend *backend;
	EDataBook *book = NULL;
	ESource *source;
	gchar *uri;

	printf ("impl_GNOME_Evolution_Addressbook_BookFactory_getBook\n");

	source = e_source_new_from_standalone_xml (source_xml);
	if (!source) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Addressbook_BookFactory_ProtocolNotSupported,
				     NULL);
		return CORBA_OBJECT_NIL;
	}

	uri = e_source_get_uri (source);
	if (!uri) {
		g_object_unref (source);
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Addressbook_BookFactory_ProtocolNotSupported,
				     NULL);
		return CORBA_OBJECT_NIL;
	}
	printf (" + %s\n", uri);

	/* Look up the backend and create one if needed */
	g_mutex_lock (factory->priv->map_mutex);

	backend = g_hash_table_lookup (factory->priv->active_server_map, uri);

	if (!backend) {
		EBookBackendFactory*  backend_factory;

		backend_factory = e_data_book_factory_lookup_backend_factory (factory, uri);

		if (backend_factory == NULL) {
			CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
					     ex_GNOME_Evolution_Addressbook_BookFactory_ProtocolNotSupported,
					     NULL);

			g_mutex_unlock (factory->priv->map_mutex);

			g_free (uri);
			return CORBA_OBJECT_NIL;
		}

		backend = e_data_book_factory_launch_backend (factory, backend_factory, listener, uri);
	}

	g_free (uri);

	if (backend) {
		g_mutex_unlock (factory->priv->map_mutex);

		book = e_data_book_new (backend, source, listener);

		e_book_backend_add_client (backend, book);
		e_book_backend_set_mode (backend, factory->priv->mode);
		corba_book = bonobo_object_corba_objref (BONOBO_OBJECT (book));
	}
	else {
		/* probably need a more descriptive exception here */
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Addressbook_BookFactory_ProtocolNotSupported,
				     NULL);
		g_mutex_unlock (factory->priv->map_mutex);

		corba_book = CORBA_OBJECT_NIL;
	}

	g_object_unref (source);
	if (book)
		printf (" => %p\n", book);
	return corba_book;
}

static void
e_data_book_factory_construct (EDataBookFactory *factory)
{
	/* nothing to do here.. */
}

/**
 * e_data_book_factory_new:
 *
 * Create a new #EDataBookFactory.
 *
 * Return value: A new #EDataBookFactory.
 **/
EDataBookFactory *
e_data_book_factory_new (void)
{
	static GStaticMutex mutex = G_STATIC_MUTEX_INIT;
	static PortableServer_POA poa = NULL;
	EDataBookFactory *factory;

	g_static_mutex_lock (&mutex);
	if (poa == NULL)
		poa = bonobo_poa_get_threaded (ORBIT_THREAD_HINT_PER_REQUEST, NULL);
	g_static_mutex_unlock (&mutex);

	factory = g_object_new (E_TYPE_DATA_BOOK_FACTORY, "poa", poa, NULL);

	e_data_book_factory_construct (factory);

	return factory;
}

/**
 * e_data_book_factory_activate:
 * @factory: an #EDataBookFactory
 * @iid: the OAF ID of the factory to activate
 *
 * Activates the factory specified by @iid, using Bonobo.
 *
 * Return value: %TRUE for success, %FALSE otherwise.
 **/
gboolean
e_data_book_factory_activate (EDataBookFactory *factory, const char *iid)
{
	EDataBookFactoryPrivate *priv;
	Bonobo_RegistrationResult result;
	char *tmp_iid;

	g_return_val_if_fail (factory != NULL, FALSE);
	g_return_val_if_fail (E_IS_DATA_BOOK_FACTORY (factory), FALSE);

	priv = factory->priv;

	g_return_val_if_fail (!priv->registered, FALSE);

	/* if iid is NULL, use the default factory OAFIID */
	if (iid)
		tmp_iid = g_strdup (iid);
	else
		tmp_iid = g_strdup (DEFAULT_E_DATA_BOOK_FACTORY_OAF_ID);

	result = bonobo_activation_active_server_register (tmp_iid, bonobo_object_corba_objref (BONOBO_OBJECT (factory)));

	switch (result) {
	case Bonobo_ACTIVATION_REG_SUCCESS:
		priv->registered = TRUE;
		priv->iid = tmp_iid;
		return TRUE;
	case Bonobo_ACTIVATION_REG_NOT_LISTED:
		g_message ("Error registering the PAS factory: not listed");
		break;
	case Bonobo_ACTIVATION_REG_ALREADY_ACTIVE:
		g_message ("Error registering the PAS factory: already active");
		break;
	case Bonobo_ACTIVATION_REG_ERROR:
	default:
		g_message ("Error registering the PAS factory: generic error");
		break;
	}

	g_free (tmp_iid);
	return FALSE;
}
static void
set_backend_online_status (gpointer key, gpointer value, gpointer data)
{
	EBookBackend *backend;

	backend = E_BOOK_BACKEND (value);
	e_book_backend_set_mode (backend, GPOINTER_TO_INT (data));
}

/**
 * e_data_book_factory_set_backend_mode:
 * @factory: an #EDataBookFactory
 * @mode: a connection status
 *
 * Sets all the backends associated with @factory to be either online
 * or offline. @mode should be passed as 1 for offline, or 2 for
 * online.
 **/
void
e_data_book_factory_set_backend_mode (EDataBookFactory *factory, int mode)
{
	EDataBookFactoryPrivate *priv = factory->priv;


	g_mutex_lock (priv->map_mutex);
	priv->mode = mode;
	g_hash_table_foreach (priv->active_server_map, set_backend_online_status, GINT_TO_POINTER (priv->mode));
	g_mutex_unlock (priv->map_mutex);

}
static void
e_data_book_factory_init (EDataBookFactory *factory)
{
	GHashTable *active_server_map;
	GHashTable *backends;

	active_server_map = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);

	backends = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	factory->priv = g_new0 (EDataBookFactoryPrivate, 1);

	factory->priv->map_mutex         = g_mutex_new();
	factory->priv->active_server_map = active_server_map;
	factory->priv->backends          = backends;
	factory->priv->registered        = FALSE;
}

static void
e_data_book_factory_dispose (GObject *object)
{
	EDataBookFactory *factory = E_DATA_BOOK_FACTORY (object);
	EDataBookFactoryPrivate *priv = factory->priv;

	g_hash_table_remove_all (priv->active_server_map);
	g_hash_table_remove_all (priv->backends);

	if (priv->registered) {
		bonobo_activation_active_server_unregister (
			priv->iid, bonobo_object_corba_objref (
			BONOBO_OBJECT (factory)));
		priv->registered = FALSE;
	}

	if (G_OBJECT_CLASS (e_data_book_factory_parent_class)->dispose)
		G_OBJECT_CLASS (e_data_book_factory_parent_class)->dispose (object);
}

static void
e_data_book_factory_finalize (GObject *object)
{
	EDataBookFactory *factory = E_DATA_BOOK_FACTORY (object);
	EDataBookFactoryPrivate *priv = factory->priv;

	g_mutex_free (priv->map_mutex);
	g_hash_table_destroy (priv->active_server_map);
	g_hash_table_destroy (priv->backends);
	g_free (priv->iid);
	g_free (priv);

	if (G_OBJECT_CLASS (e_data_book_factory_parent_class)->finalize)
		G_OBJECT_CLASS (e_data_book_factory_parent_class)->finalize (object);
}

static void
e_data_book_factory_class_init (EDataBookFactoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	POA_GNOME_Evolution_Addressbook_BookFactory__epv *epv;

	e_data_book_factory_parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_data_book_factory_dispose;
	object_class->finalize = e_data_book_factory_finalize;

	factory_signals[LAST_BOOK_GONE] =
		g_signal_new ("last_book_gone",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EDataBookFactoryClass, last_book_gone),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);


	epv = &klass->epv;

	epv->getBook = impl_GNOME_Evolution_Addressbook_BookFactory_getBook;
}

BONOBO_TYPE_FUNC_FULL (
		       EDataBookFactory,
		       GNOME_Evolution_Addressbook_BookFactory,
		       BONOBO_TYPE_OBJECT,
		       e_data_book_factory);
