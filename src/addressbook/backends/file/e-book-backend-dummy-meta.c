/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

/* The 'dummy-meta' backend is only for testing purposes, to verify
   functionality of the EBookMetaBackend */

#include "libedata-book/libedata-book.h"

#include "e-book-backend-dummy-meta.h"

struct _EBookBackendDummyMetaPrivate {
	GMutex mutex;
	GHashTable *contacts; /* gchar *uid ~> EContact * */
};

G_DEFINE_TYPE_WITH_PRIVATE (EBookBackendDummyMeta, e_book_backend_dummy_meta, E_TYPE_BOOK_META_BACKEND)

static gboolean
book_backend_dummy_meta_connect_sync (EBookMetaBackend *meta_backend,
				      const ENamedParameters *credentials,
				      ESourceAuthenticationResult *out_auth_result,
				      gchar **out_certificate_pem,
				      GTlsCertificateFlags *out_certificate_errors,
				      GCancellable *cancellable,
				      GError **error)
{
	return TRUE;
}

static gboolean
book_backend_dummy_meta_disconnect_sync (EBookMetaBackend *meta_backend,
					 GCancellable *cancellable,
					 GError **error)
{
	return TRUE;
}

static gboolean
book_backend_dummy_meta_list_existing_sync (EBookMetaBackend *meta_backend,
					    gchar **out_new_sync_tag,
					    GSList **out_existing_objects,
					    GCancellable *cancellable,
					    GError **error)
{
	EBookBackendDummyMeta *self;
	GHashTableIter iter;
	gpointer key, value;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_DUMMY_META (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag, FALSE);
	g_return_val_if_fail (out_existing_objects, FALSE);

	self = E_BOOK_BACKEND_DUMMY_META (meta_backend);

	*out_existing_objects = NULL;

	g_mutex_lock (&self->priv->mutex);

	g_hash_table_iter_init (&iter, self->priv->contacts);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar *uid;
		gchar *revision;
		EBookMetaBackendInfo *nfo;

		uid = key;
		revision = e_contact_get (value, E_CONTACT_REV);

		nfo = e_book_meta_backend_info_new (uid, revision, NULL, NULL);
		*out_existing_objects = g_slist_prepend (*out_existing_objects, nfo);

		g_free (revision);
	}

	g_mutex_unlock (&self->priv->mutex);

	return TRUE;
}

static gboolean
book_backend_dummy_meta_save_contact_sync (EBookMetaBackend *meta_backend,
					   gboolean overwrite_existing,
					   EConflictResolution conflict_resolution,
					   EContact *contact,
					   const gchar *extra,
					   guint32 opflags,
					   gchar **out_new_uid,
					   gchar **out_new_extra,
					   GCancellable *cancellable,
					   GError **error)
{
	EBookBackendDummyMeta *self;
	const gchar *uid;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_DUMMY_META (meta_backend), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);
	g_return_val_if_fail (out_new_uid != NULL, FALSE);

	self = E_BOOK_BACKEND_DUMMY_META (meta_backend);

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	g_assert_nonnull (uid);

	g_mutex_lock (&self->priv->mutex);

	if (g_hash_table_contains (self->priv->contacts, uid)) {
		if (!overwrite_existing) {
			g_mutex_unlock (&self->priv->mutex);
			g_propagate_error (error, e_book_client_error_create (E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS, NULL));
			return FALSE;
		}

		g_hash_table_remove (self->priv->contacts, uid);
	}

	/* Intentionally do not add a referenced 'contact', thus any later changes
	   on it are not "propagated" into the test_backend's content. */
	g_hash_table_insert (self->priv->contacts, g_strdup (uid), e_contact_duplicate (contact));

	*out_new_uid = g_strdup (uid);

	g_mutex_unlock (&self->priv->mutex);

	return TRUE;
}

static gboolean
book_backend_dummy_meta_load_contact_sync (EBookMetaBackend *meta_backend,
					   const gchar *uid,
					   const gchar *extra,
					   EContact **out_contact,
					   gchar **out_extra,
					   GCancellable *cancellable,
					   GError **error)
{
	EBookBackendDummyMeta *self;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_DUMMY_META (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_contact != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	self = E_BOOK_BACKEND_DUMMY_META (meta_backend);

	g_mutex_lock (&self->priv->mutex);

	*out_contact = g_hash_table_lookup (self->priv->contacts, uid);

	if (*out_contact) {
		*out_contact = e_contact_duplicate (*out_contact);
		*out_extra = g_strconcat ("extra for ", uid, NULL);

		g_mutex_unlock (&self->priv->mutex);

		return TRUE;
	} else {
		g_propagate_error (error, e_book_client_error_create (E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND, NULL));
	}

	g_mutex_unlock (&self->priv->mutex);

	return FALSE;
}

static gboolean
book_backend_dummy_meta_remove_contact_sync (EBookMetaBackend *meta_backend,
					     EConflictResolution conflict_resolution,
					     const gchar *uid,
					     const gchar *extra,
					     const gchar *object,
					     guint32 opflags,
					     GCancellable *cancellable,
					     GError **error)
{
	EBookBackendDummyMeta *self;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_DUMMY_META (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (extra != NULL, FALSE);

	self = E_BOOK_BACKEND_DUMMY_META (meta_backend);

	g_mutex_lock (&self->priv->mutex);

	success = g_hash_table_remove (self->priv->contacts, uid);
	if (success) {
		gchar *expected_extra;

		expected_extra = g_strconcat ("extra for ", uid, NULL);
		g_assert_cmpstr (expected_extra, ==, extra);
		g_free (expected_extra);
	} else {
		g_propagate_error (error, e_book_client_error_create (E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND, NULL));
	}

	g_mutex_unlock (&self->priv->mutex);

	return success;
}

static void
book_backend_dummy_meta_finalize (GObject *object)
{
	EBookBackendDummyMeta *self = E_BOOK_BACKEND_DUMMY_META (object);

	g_clear_pointer (&self->priv->contacts, g_hash_table_unref);
	g_mutex_clear (&self->priv->mutex);

	/* Chain up to parent's method */
	G_OBJECT_CLASS (e_book_backend_dummy_meta_parent_class)->finalize (object);
}

static void
e_book_backend_dummy_meta_class_init (EBookBackendDummyMetaClass *klass)
{
	GObjectClass *object_class;
	EBookMetaBackendClass *book_meta_backend_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = book_backend_dummy_meta_finalize;

	book_meta_backend_class = E_BOOK_META_BACKEND_CLASS (klass);
	book_meta_backend_class->connect_sync = book_backend_dummy_meta_connect_sync;
	book_meta_backend_class->disconnect_sync = book_backend_dummy_meta_disconnect_sync;
	book_meta_backend_class->list_existing_sync = book_backend_dummy_meta_list_existing_sync;
	book_meta_backend_class->save_contact_sync = book_backend_dummy_meta_save_contact_sync;
	book_meta_backend_class->load_contact_sync = book_backend_dummy_meta_load_contact_sync;
	book_meta_backend_class->remove_contact_sync = book_backend_dummy_meta_remove_contact_sync;
}

static void
e_book_backend_dummy_meta_init (EBookBackendDummyMeta *self)
{
	self->priv = e_book_backend_dummy_meta_get_instance_private (self);
	self->priv->contacts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	g_mutex_init (&self->priv->mutex);

	e_backend_set_online (E_BACKEND (self), TRUE);
	e_book_backend_set_writable (E_BOOK_BACKEND (self), TRUE);
}
