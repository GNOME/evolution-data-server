/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

/* The 'dummy' backend is only for test purposes, to verify
   functionality of the EDataBookViewWatcherMemory */

#include "libedata-book/libedata-book.h"

#include "e-book-backend-dummy.h"

struct _EBookBackendDummyPrivate {
	GMutex mutex;
	GHashTable *contacts; /* gchar *uid ~> EContact * */
};

G_DEFINE_TYPE_WITH_PRIVATE (EBookBackendDummy, e_book_backend_dummy, E_TYPE_BOOK_BACKEND)

static void
book_backend_dummy_open (EBookBackend *backend,
			 EDataBook *book,
			 guint32 opid,
			 GCancellable *cancellable)
{
	e_data_book_respond_open (book, opid, NULL);
}

static void
book_backend_dummy_refresh (EBookBackend *backend,
			    EDataBook *book,
			    guint32 opid,
			    GCancellable *cancellable)
{
	e_data_book_respond_refresh (book, opid, NULL);
}

static void
book_backend_dummy_create_contacts (EBookBackend *backend,
				    EDataBook *book,
				    guint32 opid,
				    GCancellable *cancellable,
				    const gchar * const *vcards,
				    guint32 opflags)
{
	EBookBackendDummy *self = E_BOOK_BACKEND_DUMMY (backend);
	GSList *contacts = NULL;
	guint ii;

	g_mutex_lock (&self->priv->mutex);

	for (ii = 0; vcards[ii]; ii++) {
		EContact *contact;

		contact = e_contact_new_from_vcard (vcards[ii]);
		if (contact) {
			contacts = g_slist_prepend (contacts, g_object_ref (contact));

			g_hash_table_insert (self->priv->contacts, e_contact_get (contact, E_CONTACT_UID), contact);
		}
	}

	g_mutex_unlock (&self->priv->mutex);

	contacts = g_slist_reverse (contacts);

	e_data_book_respond_create_contacts (book, opid, NULL, contacts);

	g_slist_free_full (contacts, g_object_unref);
}

static void
book_backend_dummy_modify_contacts (EBookBackend *backend,
				    EDataBook *book,
				    guint32 opid,
				    GCancellable *cancellable,
				    const gchar * const *vcards,
				    guint32 opflags)
{
	EBookBackendDummy *self = E_BOOK_BACKEND_DUMMY (backend);
	GSList *contacts = NULL;
	guint ii;

	g_mutex_lock (&self->priv->mutex);

	for (ii = 0; vcards[ii]; ii++) {
		EContact *contact;

		contact = e_contact_new_from_vcard (vcards[ii]);
		if (contact) {
			contacts = g_slist_prepend (contacts, g_object_ref (contact));

			g_hash_table_insert (self->priv->contacts, e_contact_get (contact, E_CONTACT_UID), contact);
		}
	}

	g_mutex_unlock (&self->priv->mutex);

	contacts = g_slist_reverse (contacts);

	e_data_book_respond_modify_contacts (book, opid, NULL, contacts);

	g_slist_free_full (contacts, g_object_unref);
}

static void
book_backend_dummy_remove_contacts (EBookBackend *backend,
				    EDataBook *book,
				    guint32 opid,
				    GCancellable *cancellable,
				    const gchar * const *uids,
				    guint32 opflags)
{
	EBookBackendDummy *self = E_BOOK_BACKEND_DUMMY (backend);
	GSList *ids = NULL;
	guint ii;

	g_mutex_lock (&self->priv->mutex);

	for (ii = 0; uids[ii]; ii++) {
		if (g_hash_table_remove (self->priv->contacts, uids[ii]))
			ids = g_slist_prepend (ids, (gpointer) uids[ii]);
	}

	g_mutex_unlock (&self->priv->mutex);

	ids = g_slist_reverse (ids);

	e_data_book_respond_remove_contacts (book, opid, NULL, ids);

	g_slist_free (ids);
}

static void
book_backend_dummy_get_contact (EBookBackend *backend,
				EDataBook *book,
				guint32 opid,
				GCancellable *cancellable,
				const gchar *id)
{
	EBookBackendDummy *self = E_BOOK_BACKEND_DUMMY (backend);
	EContact *contact = NULL;
	GError *error = NULL;

	g_mutex_lock (&self->priv->mutex);

	contact = g_hash_table_lookup (self->priv->contacts, id);
	if (contact)
		g_object_ref (contact);
	else
		error = e_book_client_error_create (E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND, NULL);

	g_mutex_unlock (&self->priv->mutex);

	e_data_book_respond_get_contact (book, opid, error, contact);

	g_clear_object (&contact);
	g_clear_error (&error);
}

static GSList *
book_backend_query (EBookBackendDummy *self,
		    const gchar *query,
		    gboolean return_contacts, /* FALSE for UID-s */
		    GError **error)
{
	GSList *items = NULL;
	EBookBackendSExp *sexp;
	GHashTableIter iter;
	gpointer value;

	sexp = e_book_backend_sexp_new (query);

	if (!sexp) {
		g_propagate_error (error, e_client_error_create (E_CLIENT_ERROR_INVALID_QUERY, NULL));
		return NULL;
	}

	g_mutex_lock (&self->priv->mutex);

	g_hash_table_iter_init (&iter, self->priv->contacts);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		EContact *contact = value;

		if (!sexp || e_book_backend_sexp_match_contact (sexp, contact)) {
			if (return_contacts)
				items = g_slist_prepend (items, g_object_ref (contact));
			else
				items = g_slist_prepend (items, e_contact_get (contact, E_CONTACT_UID));
		}
	}

	g_mutex_unlock (&self->priv->mutex);

	items = g_slist_reverse (items);

	g_object_unref (sexp);

	return items;
}

static void
book_backend_dummy_get_contact_list (EBookBackend *backend,
				     EDataBook *book,
				     guint32 opid,
				     GCancellable *cancellable,
				     const gchar *query)
{
	EBookBackendDummy *self = E_BOOK_BACKEND_DUMMY (backend);
	GSList *contacts;
	GError *error = NULL;

	contacts = book_backend_query (self, query, TRUE, &error);

	e_data_book_respond_get_contact_list (book, opid, error, contacts);

	g_clear_error (&error);
	g_slist_free_full (contacts, g_object_unref);
}

static void
book_backend_dummy_get_contact_list_uids (EBookBackend *backend,
					  EDataBook *book,
					  guint32 opid,
					  GCancellable *cancellable,
					  const gchar *query)
{
	EBookBackendDummy *self = E_BOOK_BACKEND_DUMMY (backend);
	GSList *uids;
	GError *error = NULL;

	uids = book_backend_query (self, query, FALSE, &error);

	e_data_book_respond_get_contact_list_uids (book, opid, error, uids);

	g_clear_error (&error);
	g_slist_free_full (uids, g_free);
}

static void
book_backend_dummy_start_view (EBookBackend *backend,
			       EDataBookView *book_view)
{
	if ((e_data_book_view_get_flags (book_view) & E_BOOK_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL) != 0 ||
	    e_data_book_view_get_force_initial_notifications (book_view)) {
		EBookBackendDummy *self = E_BOOK_BACKEND_DUMMY (backend);
		EBookBackendSExp *sexp = e_data_book_view_get_sexp (book_view);
		GHashTableIter iter;
		gpointer value;

		g_hash_table_iter_init (&iter, self->priv->contacts);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			EContact *contact = value;

			if (!sexp || e_book_backend_sexp_match_contact (sexp, contact))
				e_data_book_view_notify_update (book_view, contact);
		}
	}

	e_data_book_view_notify_complete (book_view, NULL);
}

static void
book_backend_dummy_stop_view (EBookBackend *backend,
			      EDataBookView *book_view)
{
}

static void
book_backend_dummy_finalize (GObject *object)
{
	EBookBackendDummy *self = E_BOOK_BACKEND_DUMMY (object);

	g_clear_pointer (&self->priv->contacts, g_hash_table_unref);
	g_mutex_clear (&self->priv->mutex);

	/* Chain up to parent's method */
	G_OBJECT_CLASS (e_book_backend_dummy_parent_class)->finalize (object);
}

static void
e_book_backend_dummy_class_init (EBookBackendDummyClass *klass)
{
	GObjectClass *object_class;
	EBookBackendClass *backend_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = book_backend_dummy_finalize;

	backend_class = E_BOOK_BACKEND_CLASS (klass);
	backend_class->impl_open = book_backend_dummy_open;
	backend_class->impl_refresh = book_backend_dummy_refresh;
	backend_class->impl_create_contacts = book_backend_dummy_create_contacts;
	backend_class->impl_modify_contacts = book_backend_dummy_modify_contacts;
	backend_class->impl_remove_contacts = book_backend_dummy_remove_contacts;
	backend_class->impl_get_contact = book_backend_dummy_get_contact;
	backend_class->impl_get_contact_list = book_backend_dummy_get_contact_list;
	backend_class->impl_get_contact_list_uids = book_backend_dummy_get_contact_list_uids;
	backend_class->impl_start_view = book_backend_dummy_start_view;
	backend_class->impl_stop_view = book_backend_dummy_stop_view;
}

static void
e_book_backend_dummy_init (EBookBackendDummy *self)
{
	self->priv = e_book_backend_dummy_get_instance_private (self);
	self->priv->contacts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	g_mutex_init (&self->priv->mutex);
}
