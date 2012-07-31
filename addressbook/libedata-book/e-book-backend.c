/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#include <config.h>

#include <glib/gi18n-lib.h>

#include "e-data-book-view.h"
#include "e-data-book.h"
#include "e-book-backend.h"

#define EDB_OPENING_ERROR	e_data_book_create_error (E_DATA_BOOK_STATUS_BUSY, _("Cannot process, book backend is opening"))
#define EDB_NOT_OPENED_ERROR	e_data_book_create_error (E_DATA_BOOK_STATUS_NOT_OPENED, NULL)

#define E_BOOK_BACKEND_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BOOK_BACKEND, EBookBackendPrivate))

struct _EBookBackendPrivate {
	ESourceRegistry *registry;

	GMutex *clients_mutex;
	GSList *clients;

	gboolean opening, opened, readonly, removed, online;

	GMutex *views_mutex;
	GSList *views;

	gchar *cache_dir;
};

/* Property IDs */
enum {
	PROP_0,
	PROP_CACHE_DIR,
	PROP_REGISTRY
};

G_DEFINE_TYPE (EBookBackend, e_book_backend, E_TYPE_BACKEND)

static void
book_backend_set_default_cache_dir (EBookBackend *backend)
{
	ESource *source;
	const gchar *user_cache_dir;
	const gchar *uid;
	gchar *filename;

	user_cache_dir = e_get_user_cache_dir ();
	source = e_backend_get_source (E_BACKEND (backend));

	uid = e_source_get_uid (source);
	g_return_if_fail (uid != NULL);

	filename = g_build_filename (
		user_cache_dir, "addressbook", uid, NULL);
	e_book_backend_set_cache_dir (backend, filename);
	g_free (filename);
}

static void
book_backend_get_backend_property (EBookBackend *backend,
                                   EDataBook *book,
                                   guint32 opid,
                                   GCancellable *cancellable,
                                   const gchar *prop_name)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (book != NULL);
	g_return_if_fail (prop_name != NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_OPENED)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, e_book_backend_is_opened (backend) ? "TRUE" : "FALSE");
	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_OPENING)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, e_book_backend_is_opening (backend) ? "TRUE" : "FALSE");
	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_ONLINE)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, e_backend_get_online (E_BACKEND (backend)) ? "TRUE" : "FALSE");
	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_READONLY)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, e_book_backend_is_readonly (backend) ? "TRUE" : "FALSE");
	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CACHE_DIR)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, e_book_backend_get_cache_dir (backend));
	} else {
		e_data_book_respond_get_backend_property (book, opid, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_NOT_SUPPORTED, _("Unknown book property '%s'"), prop_name), NULL);
	}
}

static void
book_backend_set_backend_property (EBookBackend *backend,
                                   EDataBook *book,
                                   guint32 opid,
                                   GCancellable *cancellable,
                                   const gchar *prop_name,
                                   const gchar *prop_value)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (book != NULL);
	g_return_if_fail (prop_name != NULL);

	e_data_book_respond_set_backend_property (book, opid, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_NOT_SUPPORTED, _("Cannot change value of book property '%s'"), prop_name));
}

static void
book_backend_set_registry (EBookBackend *backend,
                           ESourceRegistry *registry)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (backend->priv->registry == NULL);

	backend->priv->registry = g_object_ref (registry);
}

static void
book_backend_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CACHE_DIR:
			e_book_backend_set_cache_dir (
				E_BOOK_BACKEND (object),
				g_value_get_string (value));
			return;

		case PROP_REGISTRY:
			book_backend_set_registry (
				E_BOOK_BACKEND (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
book_backend_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CACHE_DIR:
			g_value_set_string (
				value, e_book_backend_get_cache_dir (
				E_BOOK_BACKEND (object)));
			return;

		case PROP_REGISTRY:
			g_value_set_object (
				value, e_book_backend_get_registry (
				E_BOOK_BACKEND (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
book_backend_dispose (GObject *object)
{
	EBookBackendPrivate *priv;

	priv = E_BOOK_BACKEND_GET_PRIVATE (object);

	if (priv->registry != NULL) {
		g_object_unref (priv->registry);
		priv->registry = NULL;
	}

	if (priv->views != NULL) {
		g_slist_free (priv->views);
		priv->views = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_book_backend_parent_class)->dispose (object);
}

static void
book_backend_finalize (GObject *object)
{
	EBookBackendPrivate *priv;

	priv = E_BOOK_BACKEND_GET_PRIVATE (object);

	g_slist_free (priv->clients);

	g_mutex_free (priv->clients_mutex);
	g_mutex_free (priv->views_mutex);

	g_free (priv->cache_dir);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_backend_parent_class)->finalize (object);
}

static gboolean
book_backend_authenticate_sync (EBackend *backend,
                                ESourceAuthenticator *auth,
                                GCancellable *cancellable,
                                GError **error)
{
	EBookBackend *book_backend;
	ESourceRegistry *registry;
	ESource *source;

	book_backend = E_BOOK_BACKEND (backend);
	registry = e_book_backend_get_registry (book_backend);
	source = e_backend_get_source (backend);

	return e_source_registry_authenticate_sync (
		registry, source, auth, cancellable, error);
}

static gboolean
view_notify_update (EDataBookView *view,
                    gpointer contact)
{
	e_data_book_view_notify_update (view, contact);

	return TRUE;
}

static void
book_backend_notify_update (EBookBackend *backend,
                            const EContact *contact)
{
	e_book_backend_foreach_view (backend, view_notify_update, (gpointer) contact);
}

static void
e_book_backend_class_init (EBookBackendClass *class)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;

	g_type_class_add_private (class, sizeof (EBookBackendPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = book_backend_set_property;
	object_class->get_property = book_backend_get_property;
	object_class->dispose = book_backend_dispose;
	object_class->finalize = book_backend_finalize;

	backend_class = E_BACKEND_CLASS (class);
	backend_class->authenticate_sync = book_backend_authenticate_sync;

	class->get_backend_property = book_backend_get_backend_property;
	class->set_backend_property = book_backend_set_backend_property;
	class->notify_update        = book_backend_notify_update;

	g_object_class_install_property (
		object_class,
		PROP_CACHE_DIR,
		g_param_spec_string (
			"cache-dir",
			"Cache Dir",
			"The backend's cache directory",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_REGISTRY,
		g_param_spec_object (
			"registry",
			"Registry",
			"Data source registry",
			E_TYPE_SOURCE_REGISTRY,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_book_backend_init (EBookBackend *backend)
{
	backend->priv = E_BOOK_BACKEND_GET_PRIVATE (backend);

	backend->priv->clients = NULL;
	backend->priv->clients_mutex = g_mutex_new ();

	backend->priv->views = NULL;
	backend->priv->views_mutex = g_mutex_new ();
}

/**
 * e_book_backend_get_cache_dir:
 * @backend: an #EBookBackend
 *
 * Returns the cache directory for the given backend.
 *
 * Returns: the cache directory for the backend
 *
 * Since: 2.32
 **/
const gchar *
e_book_backend_get_cache_dir (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	return backend->priv->cache_dir;
}

/**
 * e_book_backend_set_cache_dir:
 * @backend: an #EBookBackend
 * @cache_dir: a local cache directory
 *
 * Sets the cache directory for the given backend.
 *
 * Note that #EBookBackend is initialized with a usable default based on
 * the #ESource given to e_book_backend_open().  Backends should
 * not override the default without good reason.
 *
 * Since: 2.32
 **/
void
e_book_backend_set_cache_dir (EBookBackend *backend,
                              const gchar *cache_dir)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (cache_dir != NULL);

	if (g_strcmp0 (backend->priv->cache_dir, cache_dir) == 0)
		return;

	g_free (backend->priv->cache_dir);
	backend->priv->cache_dir = g_strdup (cache_dir);

	g_object_notify (G_OBJECT (backend), "cache-dir");
}

/**
 * e_book_backend_get_registry:
 * @backend: an #EBookBackend
 *
 * Returns the data source registry to which #EBackend:source belongs.
 *
 * Returns: an #ESourceRegistry
 *
 * Since: 3.6
 **/
ESourceRegistry *
e_book_backend_get_registry (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	return backend->priv->registry;
}

/**
 * e_book_backend_open:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @only_if_exists: %TRUE to prevent the creation of a new book
 *
 * Executes an 'open' request specified by @opid on @book
 * using @backend. This call might be finished
 * with e_data_book_respond_open() or e_book_backend_respond_opened(),
 * though the overall opening phase finishes only after call
 * of e_book_backend_notify_opened() after which call the backend
 * is either fully opened (including authentication against (remote)
 * server/storage) or an error was encountered during this opening phase.
 * 'opened' and 'opening' properties are updated automatically.
 * The backend refuses all other operations until the opening phase is finished.
 *
 * The e_book_backend_notify_opened() is called either from this function
 * or from e_book_backend_authenticate_user(), or after necessary steps
 * initiated by these two functions.
 *
 * The opening phase usually works like this:
 * 1) client requests open for the backend
 * 2) server receives this request and calls e_book_backend_open() - the opening phase begun
 * 3) either the backend is opened during this call, and notifies client
 *    with e_book_backend_notify_opened() about that. This is usually
 *    for local backends; their opening phase is finished
 * 4) or the backend requires authentication, thus it notifies client
 *    about that with e_book_backend_notify_auth_required() and is
 *    waiting for credentials, which will be received from client
 *    by e_book_backend_authenticate_user() call. Backend's opening
 *    phase is still running in this case, thus it doesn't call
 *    e_book_backend_notify_opened() within e_book_backend_open() call.
 * 5) when backend receives credentials in e_book_backend_authenticate_user()
 *    then it tries to authenticate against a server/storage with them
 *    and only after it knows result of the authentication, whether user
 *    was or wasn't authenticated, it notifies client with the result
 *    by e_book_backend_notify_opened() and it's opening phase is
 *    finished now. If there was no error returned then the backend is
 *    considered opened, otherwise it's considered closed. Use AuthenticationFailed
 *    error when the given credentials were rejected by the server/store, which
 *    will result in a re-prompt on the client side, otherwise use AuthenticationRequired
 *    if there was anything wrong with the given credentials. Set error's
 *    message to a reason for a re-prompt, it'll be shown to a user.
 * 6) client checks error returned from e_book_backend_notify_opened() and
 *    reprompts for a password if it was AuthenticationFailed. Otherwise
 *    considers backend opened based on the error presence (no error means success).
 *
 * In any case, the call of e_book_backend_open() should be always finished
 * with e_data_book_respond_open(), which has no influence on the opening phase,
 * or alternatively with e_book_backend_respond_opened(). Never use authentication
 * errors in e_data_book_respond_open() to notify the client the authentication is
 * required, there is e_book_backend_notify_auth_required() for this.
 **/
void
e_book_backend_open (EBookBackend *backend,
                     EDataBook *book,
                     guint32 opid,
                     GCancellable *cancellable,
                     gboolean only_if_exists)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	g_mutex_lock (backend->priv->clients_mutex);

	if (e_book_backend_is_opened (backend)) {
		g_mutex_unlock (backend->priv->clients_mutex);

		e_data_book_report_readonly (book, backend->priv->readonly);
		e_data_book_report_online (book, backend->priv->online);

		e_book_backend_respond_opened (backend, book, opid, NULL);
	} else if (e_book_backend_is_opening (backend)) {
		g_mutex_unlock (backend->priv->clients_mutex);

		e_data_book_respond_open (book, opid, EDB_OPENING_ERROR);
	} else {
		backend->priv->opening = TRUE;
		g_mutex_unlock (backend->priv->clients_mutex);

		/* Subclasses may need to call e_book_backend_get_cache_dir() in
		 * their open() methods, so get the "cache-dir" property
		 * initialized before we call the method. */
		book_backend_set_default_cache_dir (backend);

		g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->open != NULL);

		(* E_BOOK_BACKEND_GET_CLASS (backend)->open) (backend, book, opid, cancellable, only_if_exists);
	}
}

/**
 * e_book_backend_remove:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @opid: the ID to use for this operation
 *
 * Executes a 'remove' request to remove all of @backend's data,
 * specified by @opid on @book.
 * This might be finished with e_data_book_respond_remove().
 **/
void
e_book_backend_remove (EBookBackend *backend,
                       EDataBook *book,
                       guint32 opid,
                       GCancellable *cancellable)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->remove);

	if (e_book_backend_is_opening (backend))
		e_data_book_respond_remove (book, opid, EDB_OPENING_ERROR);
	else
		(* E_BOOK_BACKEND_GET_CLASS (backend)->remove) (backend, book, opid, cancellable);
}

/**
 * e_book_backend_refresh:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 *
 * Refreshes the address book being accessed by the given backend.
 * This might be finished with e_data_book_respond_refresh(),
 * and it might be called as soon as possible; it doesn't mean
 * that the refreshing is done after calling that, the backend
 * is only notifying client whether it started the refresh process
 * or not.
 *
 * Since: 3.2
 **/
void
e_book_backend_refresh (EBookBackend *backend,
                        EDataBook *book,
                        guint32 opid,
                        GCancellable *cancellable)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	if (e_book_backend_is_opening (backend))
		e_data_book_respond_refresh (book, opid, EDB_OPENING_ERROR);
	else if (!E_BOOK_BACKEND_GET_CLASS (backend)->refresh)
		e_data_book_respond_refresh (book, opid, e_data_book_create_error (E_DATA_BOOK_STATUS_NOT_SUPPORTED, NULL));
	else if (!e_book_backend_is_opened (backend))
		e_data_book_respond_refresh (book, opid, EDB_NOT_OPENED_ERROR);
	else
		(* E_BOOK_BACKEND_GET_CLASS (backend)->refresh) (backend, book, opid, cancellable);
}

/**
 * e_book_backend_create_contacts
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @vcards: a #GSList of vCards to add
 *
 * Executes a 'create contacts' request specified by @opid on @book
 * using @backend.
 * This might be finished with e_data_book_respond_create_contacts().
 *
 * Since: 3.4
 **/
void
e_book_backend_create_contacts (EBookBackend *backend,
                               EDataBook *book,
                               guint32 opid,
                               GCancellable *cancellable,
                               const GSList *vcards)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (vcards);
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->create_contacts);

	if (e_book_backend_is_opening (backend))
		e_data_book_respond_create_contacts (book, opid, EDB_OPENING_ERROR, NULL);
	else if (!e_book_backend_is_opened (backend))
		e_data_book_respond_create_contacts (book, opid, EDB_NOT_OPENED_ERROR, NULL);
	else
		(* E_BOOK_BACKEND_GET_CLASS (backend)->create_contacts) (backend, book, opid, cancellable, vcards);
}

/**
 * e_book_backend_remove_contacts:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @id_list: list of string IDs to remove
 *
 * Executes a 'remove contacts' request specified by @opid on @book
 * using @backend.
 * This might be finished with e_data_book_respond_remove_contacts().
 **/
void
e_book_backend_remove_contacts (EBookBackend *backend,
                                EDataBook *book,
                                guint32 opid,
                                GCancellable *cancellable,
                                const GSList *id_list)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (id_list);
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->remove_contacts);

	if (e_book_backend_is_opening (backend))
		e_data_book_respond_remove_contacts (book, opid, EDB_OPENING_ERROR, NULL);
	else if (!e_book_backend_is_opened (backend))
		e_data_book_respond_remove_contacts (book, opid, EDB_NOT_OPENED_ERROR, NULL);
	else
		(* E_BOOK_BACKEND_GET_CLASS (backend)->remove_contacts) (backend, book, opid, cancellable, id_list);
}

/**
 * e_book_backend_modify_contacts:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @vcards: the VCards to update
 *
 * Executes a 'modify contacts' request specified by @opid on @book
 * using @backend.
 * This might be finished with e_data_book_respond_modify_contacts().
 *
 * Since: 3.4
 **/
void
e_book_backend_modify_contacts (EBookBackend *backend,
                               EDataBook *book,
                               guint32 opid,
                               GCancellable *cancellable,
                               const GSList *vcards)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (vcards);
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->modify_contacts);

	if (e_book_backend_is_opening (backend))
		e_data_book_respond_modify_contacts (book, opid, EDB_OPENING_ERROR, NULL);
	else if (!e_book_backend_is_opened (backend))
		e_data_book_respond_modify_contacts (book, opid, EDB_NOT_OPENED_ERROR, NULL);
	else
		(* E_BOOK_BACKEND_GET_CLASS (backend)->modify_contacts) (backend, book, opid, cancellable, vcards);
}

/**
 * e_book_backend_get_contact:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @id: the ID of the contact to get
 *
 * Executes a 'get contact' request specified by @opid on @book
 * using @backend.
 * This might be finished with e_data_book_respond_get_contact().
 **/
void
e_book_backend_get_contact (EBookBackend *backend,
                            EDataBook *book,
                            guint32 opid,
                            GCancellable *cancellable,
                            const gchar *id)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (id);
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->get_contact);

	if (e_book_backend_is_opening (backend))
		e_data_book_respond_get_contact (book, opid, EDB_OPENING_ERROR, NULL);
	else if (!e_book_backend_is_opened (backend))
		e_data_book_respond_get_contact (book, opid, EDB_NOT_OPENED_ERROR, NULL);
	else
		(* E_BOOK_BACKEND_GET_CLASS (backend)->get_contact) (backend, book, opid, cancellable, id);
}

/**
 * e_book_backend_get_contact_list:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @query: the s-expression to match
 *
 * Executes a 'get contact list' request specified by @opid on @book
 * using @backend.
 * This might be finished with e_data_book_respond_get_contact_list().
 **/
void
e_book_backend_get_contact_list (EBookBackend *backend,
                                 EDataBook *book,
                                 guint32 opid,
                                 GCancellable *cancellable,
                                 const gchar *query)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (query);
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->get_contact_list);

	if (e_book_backend_is_opening (backend))
		e_data_book_respond_get_contact_list (book, opid, EDB_OPENING_ERROR, NULL);
	else if (!e_book_backend_is_opened (backend))
		e_data_book_respond_get_contact_list (book, opid, EDB_NOT_OPENED_ERROR, NULL);
	else
		(* E_BOOK_BACKEND_GET_CLASS (backend)->get_contact_list) (backend, book, opid, cancellable, query);
}

/**
 * e_book_backend_get_contact_list_uids:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @query: the s-expression to match
 *
 * Executes a 'get contact list uids' request specified by @opid on @book
 * using @backend.
 * This might be finished with e_data_book_respond_get_contact_list_uids().
 *
 * Since: 3.2
 **/
void
e_book_backend_get_contact_list_uids (EBookBackend *backend,
                                      EDataBook *book,
                                      guint32 opid,
                                      GCancellable *cancellable,
                                      const gchar *query)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (query);
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->get_contact_list_uids);

	if (e_book_backend_is_opening (backend))
		e_data_book_respond_get_contact_list_uids (book, opid, EDB_OPENING_ERROR, NULL);
	else if (!e_book_backend_is_opened (backend))
		e_data_book_respond_get_contact_list_uids (book, opid, EDB_NOT_OPENED_ERROR, NULL);
	else
		(* E_BOOK_BACKEND_GET_CLASS (backend)->get_contact_list_uids) (backend, book, opid, cancellable, query);
}

/**
 * e_book_backend_start_book_view:
 * @backend: an #EBookBackend
 * @view: the #EDataBookView to start
 *
 * Starts running the query specified by @view, emitting signals for
 * matching contacts.
 **/
void
e_book_backend_start_book_view (EBookBackend *backend,
                                EDataBookView *view)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (view));
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->start_book_view);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->start_book_view) (backend, view);
}

/**
 * e_book_backend_stop_book_view:
 * @backend: an #EBookBackend
 * @view: the #EDataBookView to stop
 *
 * Stops running the query specified by @view, emitting no more signals.
 **/
void
e_book_backend_stop_book_view (EBookBackend *backend,
                               EDataBookView *view)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (view));
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->stop_book_view);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->stop_book_view) (backend, view);
}

/**
 * e_book_backend_add_book_view:
 * @backend: an #EBookBackend
 * @view: an #EDataBookView
 *
 * Adds @view to @backend for querying.
 **/
void
e_book_backend_add_book_view (EBookBackend *backend,
                              EDataBookView *view)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_mutex_lock (backend->priv->views_mutex);

	e_data_book_view_ref (view);
	backend->priv->views = g_slist_append (backend->priv->views, view);

	g_mutex_unlock (backend->priv->views_mutex);
}

/**
 * e_book_backend_remove_book_view:
 * @backend: an #EBookBackend
 * @view: an #EDataBookView
 *
 * Removes @view from @backend.
 **/
void
e_book_backend_remove_book_view (EBookBackend *backend,
                                 EDataBookView *view)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_mutex_lock (backend->priv->views_mutex);

	backend->priv->views = g_slist_remove (backend->priv->views, view);
	e_data_book_view_unref (view);

	g_mutex_unlock (backend->priv->views_mutex);
}

/**
 * e_book_backend_add_client:
 * @backend: An addressbook backend.
 * @book: the corba object representing the client connection.
 *
 * Adds a client to an addressbook backend.
 *
 * Returns: TRUE on success, FALSE on failure to add the client.
 */
gboolean
e_book_backend_add_client (EBookBackend *backend,
                           EDataBook *book)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), FALSE);

	g_mutex_lock (backend->priv->clients_mutex);
	backend->priv->clients = g_slist_prepend (backend->priv->clients, book);
	g_mutex_unlock (backend->priv->clients_mutex);

	return TRUE;
}

/**
 * e_book_backend_remove_client:
 * @backend: an #EBookBackend
 * @book: an #EDataBook to remove
 *
 * Removes @book from the list of @backend's clients.
 **/
void
e_book_backend_remove_client (EBookBackend *backend,
                              EDataBook *book)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	/* Make sure the backend stays alive while holding the mutex. */
	g_object_ref (backend);

	/* Disconnect */
	g_mutex_lock (backend->priv->clients_mutex);
	backend->priv->clients = g_slist_remove (backend->priv->clients, book);

	if (backend->priv->clients == NULL)
		backend->priv->opening = FALSE;

	g_mutex_unlock (backend->priv->clients_mutex);

	g_object_unref (backend);
}

/**
 * e_book_backend_foreach_view:
 * @backend: an #EBookBackend
 * @callback: callback to call
 * @user_data: user_data passed into the @callback
 *
 * Calls @callback for each known book view of this @backend.
 * @callback returns %FALSE to stop further processing.
 *
 * Since: 3.2
 **/
void
e_book_backend_foreach_view (EBookBackend *backend,
                             gboolean (*callback) (EDataBookView *view,
                                                   gpointer user_data),
                             gpointer user_data)
{
	const GSList *views;
	EDataBookView *view;
	gboolean stop = FALSE;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (callback != NULL);

	g_mutex_lock (backend->priv->views_mutex);

	for (views = backend->priv->views; views && !stop; views = views->next) {
		view = E_DATA_BOOK_VIEW (views->data);
		stop = !callback (view, user_data);
	}

	g_mutex_unlock (backend->priv->views_mutex);
}

/**
 * e_book_backend_get_book_backend_property:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @prop_name: property name to get value of; cannot be NULL
 *
 * Calls the get_backend_property method on the given backend.
 * This might be finished with e_data_book_respond_get_backend_property().
 * Default implementation takes care of common properties and returns
 * an 'unsupported' error for any unknown properties. The subclass may
 * always call this default implementation for properties which fetching
 * it doesn't overwrite.
 *
 * Since: 3.2
 **/
void
e_book_backend_get_backend_property (EBookBackend *backend,
                                     EDataBook *book,
                                     guint32 opid,
                                     GCancellable *cancellable,
                                     const gchar *prop_name)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->get_backend_property);

	E_BOOK_BACKEND_GET_CLASS (backend)->get_backend_property (backend, book, opid, cancellable, prop_name);
}

/**
 * e_book_backend_set_backend_property:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @prop_name: property name to change; cannot be NULL
 * @prop_value: value to set to @prop_name; cannot be NULL
 *
 * Calls the set_backend_property method on the given backend.
 * This might be finished with e_data_book_respond_set_backend_property().
 * Default implementation simply returns an 'unsupported' error.
 * The subclass may always call this default implementation for properties
 * which fetching it doesn't overwrite.
 *
 * Since: 3.2
 **/
void
e_book_backend_set_backend_property (EBookBackend *backend,
                                     EDataBook *book,
                                     guint32 opid,
                                     GCancellable *cancellable,
                                     const gchar *prop_name,
                                     const gchar *prop_value)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (prop_value != NULL);
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->set_backend_property != NULL);

	E_BOOK_BACKEND_GET_CLASS (backend)->set_backend_property (backend, book, opid, cancellable, prop_name, prop_value);
}

/**
 * e_book_backend_is_opened:
 * @backend: an #EBookBackend
 *
 * Checks if @backend's storage has been opened (and
 * authenticated, if necessary) and the backend itself
 * is ready for accessing. This property is changed automatically
 * within call of e_book_backend_notify_opened().
 *
 * Returns: %TRUE if fully opened, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_is_opened (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	return backend->priv->opened;
}

/**
 * e_book_backend_is_opening:
 * @backend: an #EBookBackend
 *
 * Checks if @backend is processing its opening phase, which
 * includes everything since the e_book_backend_open() call,
 * through authentication, up to e_book_backend_notify_opened().
 * This property is managed automatically and the backend deny
 * every operation except of cancel and authenticate_user while
 * it is being opening.
 *
 * Returns: %TRUE if opening phase is in the effect, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_is_opening (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	return backend->priv->opening;
}

/**
 * e_book_backend_is_readonly:
 * @backend: an #EBookBackend
 *
 * Checks if we can write to @backend.
 *
 * Returns: %TRUE if writeable, %FALSE if not.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_is_readonly (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	return backend->priv->readonly;
}

/**
 * e_book_backend_is_removed:
 * @backend: an #EBookBackend
 *
 * Checks if @backend has been removed from its physical storage.
 *
 * Returns: %TRUE if @backend has been removed, %FALSE otherwise.
 **/
gboolean
e_book_backend_is_removed (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	return backend->priv->removed;
}

/**
 * e_book_backend_set_is_removed:
 * @backend: an #EBookBackend
 * @is_removed: A flag indicating whether the backend's storage was removed
 *
 * Sets the flag indicating whether @backend was removed to @is_removed.
 * Meant to be used by backend implementations.
 **/
void
e_book_backend_set_is_removed (EBookBackend *backend,
                               gboolean is_removed)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	backend->priv->removed = is_removed;
}

/**
 * e_book_backend_sync:
 * @backend: an #EBookbackend
 *
 * Write all pending data to disk.  This is only required under special
 * circumstances (for example before a live backup) and should not be used in
 * normal use.
 *
 * Since: 1.12
 */
void
e_book_backend_sync (EBookBackend *backend)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_object_ref (backend);

	if (E_BOOK_BACKEND_GET_CLASS (backend)->sync)
		(* E_BOOK_BACKEND_GET_CLASS (backend)->sync) (backend);

	g_object_unref (backend);
}

/**
 * e_book_backend_notify_update:
 * @backend: an #EBookBackend
 * @contact: a new or modified contact
 *
 * Notifies all of @backend's book views about the new or modified
 * contacts @contact.
 *
 * e_data_book_respond_create_contacts() and e_data_book_respond_modify_contacts() call this
 * function for you. You only need to call this from your backend if
 * contacts are created or modified by another (non-PAS-using) client.
 **/
void
e_book_backend_notify_update (EBookBackend *backend,
                              const EContact *contact)
{
	E_BOOK_BACKEND_GET_CLASS (backend)->notify_update (backend, contact);
}

static gboolean
view_notify_remove (EDataBookView *view,
                    gpointer id)
{
	e_data_book_view_notify_remove (view, id);

	return TRUE;
}

/**
 * e_book_backend_notify_remove:
 * @backend: an #EBookBackend
 * @id: a contact id
 *
 * Notifies all of @backend's book views that the contact with UID
 * @id has been removed.
 *
 * e_data_book_respond_remove_contacts() calls this function for you. You
 * only need to call this from your backend if contacts are removed by
 * another (non-PAS-using) client.
 **/
void
e_book_backend_notify_remove (EBookBackend *backend,
                              const gchar *id)
{
	e_book_backend_foreach_view (backend, view_notify_remove, (gpointer) id);
}

static gboolean
view_notify_complete (EDataBookView *view,
                      gpointer unused)
{
	e_data_book_view_notify_complete (view, NULL /* SUCCESS */);

	return TRUE;
}

/**
 * e_book_backend_notify_complete:
 * @backend: an #EBookbackend
 *
 * Notifies all of @backend's book views that the current set of
 * notifications is complete; use this after a series of
 * e_book_backend_notify_update() and e_book_backend_notify_remove() calls.
 **/
void
e_book_backend_notify_complete (EBookBackend *backend)
{
	e_book_backend_foreach_view (backend, view_notify_complete, NULL);
}


/**
 * e_book_backend_notify_error:
 * @backend: an #EBookBackend
 * @message: an error message
 *
 * Notifies each backend listener about an error. This is meant to be used
 * for cases where is no GError return possibility, to notify user about
 * an issue.
 *
 * Since: 3.2
 **/
void
e_book_backend_notify_error (EBookBackend *backend,
                             const gchar *message)
{
	EBookBackendPrivate *priv;
	GSList *clients;

	priv = backend->priv;

	g_mutex_lock (priv->clients_mutex);

	for (clients = priv->clients; clients != NULL; clients = g_slist_next (clients))
		e_data_book_report_error (E_DATA_BOOK (clients->data), message);

	g_mutex_unlock (priv->clients_mutex);
}

/**
 * e_book_backend_notify_readonly:
 * @backend: an #EBookBackend
 * @is_readonly: flag indicating readonly status
 *
 * Notifies all backend's clients about the current readonly state.
 *
 * Since: 3.2
 **/
void
e_book_backend_notify_readonly (EBookBackend *backend,
                                gboolean is_readonly)
{
	EBookBackendPrivate *priv;
	GSList *clients;

	priv = backend->priv;
	priv->readonly = is_readonly;
	g_mutex_lock (priv->clients_mutex);

	for (clients = priv->clients; clients != NULL; clients = g_slist_next (clients))
		e_data_book_report_readonly (E_DATA_BOOK (clients->data), is_readonly);

	g_mutex_unlock (priv->clients_mutex);

}

/**
 * e_book_backend_notify_online:
 * @backend: an #EBookBackend
 * @is_online: flag indicating whether @backend is connected and online
 *
 * Notifies clients of @backend's connection status indicated by @is_online.
 * Meant to be used by backend implementations.
 *
 * Since: 3.2
 **/
void
e_book_backend_notify_online (EBookBackend *backend,
                              gboolean is_online)
{
	EBookBackendPrivate *priv;
	GSList *clients;

	priv = backend->priv;
	priv->online = is_online;
	g_mutex_lock (priv->clients_mutex);

	for (clients = priv->clients; clients != NULL; clients = g_slist_next (clients))
		e_data_book_report_online (E_DATA_BOOK (clients->data), is_online);

	g_mutex_unlock (priv->clients_mutex);
}

/**
 * e_book_backend_notify_opened:
 * @backend: an #EBookBackend
 * @error: a #GError corresponding to the error encountered during
 *    the opening phase. Use %NULL for success. The @error is freed
 *    automatically if not %NULL.
 *
 * Notifies clients that @backend finished its opening phase.
 * See e_book_backend_open() for more information how the opening
 * phase works. Calling this function changes 'opening' property,
 * same as 'opened'. 'opening' is set to %FALSE and the backend
 * is considered 'opened' only if the @error is %NULL.
 *
 * See also: e_book_backend_respond_opened()
 *
 * Note: The @error is freed automatically if not %NULL.
 *
 * Meant to be used by backend implementations.
 *
 * Since: 3.2
 **/
void
e_book_backend_notify_opened (EBookBackend *backend,
                              GError *error)
{
	EBookBackendPrivate *priv;
	GSList *clients;

	priv = backend->priv;
	g_mutex_lock (priv->clients_mutex);

	priv->opening = FALSE;
	priv->opened = error == NULL;

	for (clients = priv->clients; clients != NULL; clients = g_slist_next (clients))
		e_data_book_report_opened (E_DATA_BOOK (clients->data), error);

	g_mutex_unlock (priv->clients_mutex);

	if (error)
		g_error_free (error);
}

/**
 * e_book_backend_notify_property_changed:
 * @backend: an #EBookBackend
 * @prop_name: property name, which changed
 * @prop_value: new property value
 *
 * Notifies clients about property value change.
 *
 * Since: 3.2
 **/
void
e_book_backend_notify_property_changed (EBookBackend *backend,
                                        const gchar *prop_name,
                                        const gchar *prop_value)
{
	EBookBackendPrivate *priv;
	GSList *clients;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (*prop_name != '\0');
	g_return_if_fail (prop_value != NULL);

	priv = backend->priv;
	g_mutex_lock (priv->clients_mutex);

	for (clients = priv->clients; clients != NULL; clients = g_slist_next (clients))
		e_data_book_report_backend_property_changed (E_DATA_BOOK (clients->data), prop_name, prop_value);

	g_mutex_unlock (priv->clients_mutex);
}

/**
 * e_book_backend_respond_opened:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: an operation ID
 * @error: result error; can be %NULL, if it isn't then it's automatically freed
 *
 * This is a replacement for e_data_book_respond_open() for cases where
 * the finish of 'open' method call also finishes backend opening phase.
 * This function covers calling of both e_book_backend_notify_opened()
 * and e_data_book_respond_open() with the same @error.
 *
 * See e_book_backend_open() for more details how the opening phase works.
 *
 * Since: 3.2
 **/
void
e_book_backend_respond_opened (EBookBackend *backend,
                               EDataBook *book,
                               guint32 opid,
                               GError *error)
{
	GError *copy = NULL;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (book != NULL);
	g_return_if_fail (opid != 0);

	if (error)
		copy = g_error_copy (error);

	e_book_backend_notify_opened (backend, copy);
	e_data_book_respond_open (book, opid, error);
}
