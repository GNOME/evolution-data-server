/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 2000, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pthread.h>
#include <string.h>

#include <bonobo-activation/bonobo-activation.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-arg.h>
#include "e-data-book-marshal.h"
#include "e-data-book-factory.h"

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
 * @factory:
 * @proto:
 * @backend:
 */
void
e_data_book_factory_register_backend (EDataBookFactory      *factory,
				   const char          *proto,
				   EBookBackendFactoryFn  backend)
{
	g_return_if_fail (factory != NULL);
	g_return_if_fail (E_IS_DATA_BOOK_FACTORY (factory));
	g_return_if_fail (proto != NULL);
	g_return_if_fail (backend != NULL);

	if (g_hash_table_lookup (factory->priv->backends, proto) != NULL) {
		g_warning ("e_data_book_factory_register_backend: "
			   "Proto \"%s\" already registered!\n", proto);
	}

	g_hash_table_insert (factory->priv->backends,
			     g_strdup (proto), backend);
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

	g_return_val_if_fail (factory != NULL, -1);
	g_return_val_if_fail (E_IS_DATA_BOOK_FACTORY (factory), -1);

	g_mutex_lock (factory->priv->map_mutex);
	n_backends = g_hash_table_size (factory->priv->active_server_map);
	g_mutex_unlock (factory->priv->map_mutex);

	return n_backends;
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
		gpointer orig_key;
		gboolean result;
		char *orig_uri;

		g_mutex_lock (factory->priv->map_mutex);

		result = g_hash_table_lookup_extended (factory->priv->active_server_map, uri,
						       &orig_key, NULL);
		g_assert (result != FALSE);

		orig_uri = orig_key;

		g_hash_table_remove (factory->priv->active_server_map, orig_uri);
		g_free (orig_uri);

		g_object_unref (backend);

		g_mutex_unlock (factory->priv->map_mutex);
	}

	if (g_hash_table_size (factory->priv->active_server_map) == 0) {
		/* Notify upstream if there are no more backends */
		g_signal_emit (G_OBJECT (factory), factory_signals[LAST_BOOK_GONE], 0);
	}

	g_free (uri);
}



static EBookBackendFactoryFn
e_data_book_factory_lookup_backend_factory (EDataBookFactory *factory,
					 const char     *uri)
{
	EBookBackendFactoryFn  backend_fn;
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

	backend_fn = g_hash_table_lookup (factory->priv->backends, proto);

	g_free (proto); 
	g_free (canonical_uri);

	return backend_fn;
}

static EBookBackend *
e_data_book_factory_launch_backend (EDataBookFactory      *factory,
				 EBookBackendFactoryFn  backend_factory,
				 GNOME_Evolution_Addressbook_BookListener listener,
				 const char          *uri)
{
	EBookBackend          *backend;

	backend = (* backend_factory) ();
	if (!backend)
		return NULL;

	g_hash_table_insert (factory->priv->active_server_map,
			     g_strdup (uri),
			     backend);

	g_signal_connect (backend, "last_client_gone",
			  G_CALLBACK (backend_last_client_gone_cb),
			  factory);

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
	EDataBook *book;
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

	/* Look up the backend and create one if needed */
	g_mutex_lock (factory->priv->map_mutex);

	backend = g_hash_table_lookup (factory->priv->active_server_map, uri);

	if (!backend) {
		EBookBackendFactoryFn  backend_factory;

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
		GNOME_Evolution_Addressbook_BookListener listener_copy;

		listener_copy = bonobo_object_dup_ref (listener, NULL);

		g_mutex_unlock (factory->priv->map_mutex);

		book = e_data_book_new (backend, source, listener);

		e_book_backend_add_client (backend, book);

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
	return corba_book;
}

static void
e_data_book_factory_construct (EDataBookFactory *factory)
{
	/* nothing to do here.. */
}

/**
 * e_data_book_factory_new:
 */
EDataBookFactory *
e_data_book_factory_new (void)
{
	EDataBookFactory *factory;

	factory = g_object_new (E_TYPE_DATA_BOOK_FACTORY, 
				"poa", bonobo_poa_get_threaded (ORBIT_THREAD_HINT_PER_REQUEST, NULL),
				NULL);
	
	e_data_book_factory_construct (factory);

	return factory;
}

/**
 * e_data_book_factory_activate:
 */
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
e_data_book_factory_init (EDataBookFactory *factory)
{
	factory->priv = g_new0 (EDataBookFactoryPrivate, 1);

	factory->priv->map_mutex         = g_mutex_new();
	factory->priv->active_server_map = g_hash_table_new (g_str_hash, g_str_equal);
	factory->priv->backends          = g_hash_table_new (g_str_hash, g_str_equal);
	factory->priv->registered        = FALSE;
}

static void
free_active_server_map_entry (gpointer key, gpointer value, gpointer data)
{
	char *uri;
	EBookBackend *backend;

	uri = key;
	g_free (uri);

	backend = E_BOOK_BACKEND (value);
	g_object_unref (backend);
}

static void
remove_backends_entry (gpointer key, gpointer value, gpointer data)
{
	char *uri;

	uri = key;
	g_free (uri);
}

static void
e_data_book_factory_dispose (GObject *object)
{
	EDataBookFactory *factory = E_DATA_BOOK_FACTORY (object);

	if (factory->priv) {
		EDataBookFactoryPrivate *priv = factory->priv;

		g_mutex_free (priv->map_mutex);

		g_hash_table_foreach (priv->active_server_map,
				      free_active_server_map_entry,
				      NULL);
		g_hash_table_destroy (priv->active_server_map);
		priv->active_server_map = NULL;

		g_hash_table_foreach (priv->backends,
				      remove_backends_entry,
				      NULL);
		g_hash_table_destroy (priv->backends);
		priv->backends = NULL;

		if (priv->registered) {
			bonobo_activation_active_server_unregister (priv->iid,
							    bonobo_object_corba_objref (BONOBO_OBJECT (factory)));
			priv->registered = FALSE;
		}

		g_free (priv->iid);
	
		g_free (priv);
		factory->priv = NULL;
	}

	if (G_OBJECT_CLASS (e_data_book_factory_parent_class)->dispose)
		G_OBJECT_CLASS (e_data_book_factory_parent_class)->dispose (object);
}

static void
e_data_book_factory_class_init (EDataBookFactoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	POA_GNOME_Evolution_Addressbook_BookFactory__epv *epv;

	e_data_book_factory_parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_data_book_factory_dispose;

	factory_signals[LAST_BOOK_GONE] =
		g_signal_new ("last_book_gone",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EDataBookFactoryClass, last_book_gone),
			      NULL, NULL,
			      e_data_book_marshal_NONE__NONE,
			      G_TYPE_NONE, 0);


	epv = &klass->epv;

	epv->getBook = impl_GNOME_Evolution_Addressbook_BookFactory_getBook;
}

BONOBO_TYPE_FUNC_FULL (
		       EDataBookFactory,
		       GNOME_Evolution_Addressbook_BookFactory,
		       BONOBO_TYPE_OBJECT,
		       e_data_book_factory);
