/*
 * Copyright (C) 2017 Red Hat, Inc. (www.redhat.com)
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
 */

#include "evolution-data-server-config.h"

#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include "libebook-contacts/libebook-contacts.h"

#include "e-test-server-utils.h"
#include "test-book-cache-utils.h"

#define REMOTE_URL	"https://www.gnome.org/wp-content/themes/gnome-grass/images/gnome-logo.svg"
#define MODIFIED_FN_STR	"Modified FN"

typedef struct _EBookMetaBackendTest {
	EBookMetaBackend parent;

	GHashTable *contacts;

	gint sync_tag_index;
	gboolean can_connect;
	gboolean is_connected;
	gint connect_count;
	gint list_count;
	gint save_count;
	gint load_count;
	gint remove_count;
} EBookMetaBackendTest;

typedef struct _EBookMetaBackendTestClass {
	EBookMetaBackendClass parent_class;
} EBookMetaBackendTestClass;

#define E_TYPE_BOOK_META_BACKEND_TEST (e_book_meta_backend_test_get_type ())
#define E_BOOK_META_BACKEND_TEST(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_META_BACKEND_TEST, EBookMetaBackendTest))
#define E_IS_BOOK_META_BACKEND_TEST(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_META_BACKEND_TEST))

GType e_book_meta_backend_test_get_type (void) G_GNUC_CONST;

G_DEFINE_TYPE (EBookMetaBackendTest, e_book_meta_backend_test, E_TYPE_BOOK_META_BACKEND)

static void
ebmb_test_add_test_case (EBookMetaBackendTest *test_backend,
			 const gchar *case_name)
{
	EContact *contact;

	g_assert_nonnull (test_backend);
	g_assert_nonnull (case_name);

	contact = tcu_new_contact_from_test_case (case_name);
	g_assert_nonnull (contact);

	g_hash_table_insert (test_backend->contacts, e_contact_get (contact, E_CONTACT_UID), contact);
}

static void
ebmb_test_remove_component (EBookMetaBackendTest *test_backend,
			    const gchar *uid)
{
	g_assert_nonnull (test_backend);
	g_assert_nonnull (uid);

	g_hash_table_remove (test_backend->contacts, uid);
}

static GHashTable * /* gchar * ~> NULL */
ebmb_test_gather_uids (va_list args)
{
	GHashTable *expects;
	const gchar *uid;

	expects = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	uid = va_arg (args, const gchar *);
	while (uid) {
		g_hash_table_insert (expects, g_strdup (uid), NULL);
		uid = va_arg (args, const gchar *);
	}

	return expects;
}

static void
ebmb_test_hash_contains (GHashTable *contacts, /* gchar *uid ~> EContact * */
			 gboolean negate,
			 gboolean exact,
			 ...) /* uid-s, ended with NULL */
{
	va_list args;
	GHashTable *expects;
	GHashTableIter iter;
	gpointer uid;
	guint ntotal;

	g_return_if_fail (contacts != NULL);

	va_start (args, exact);
	expects = ebmb_test_gather_uids (args);
	va_end (args);

	ntotal = g_hash_table_size (expects);

	g_hash_table_iter_init (&iter, contacts);
	while (g_hash_table_iter_next (&iter, &uid, NULL)) {
		if (exact) {
			if (negate)
				g_assert_true (!g_hash_table_remove (expects, uid));
			else
				g_assert_true (g_hash_table_remove (expects, uid));
		} else {
			g_hash_table_remove (expects, uid);
		}
	}

	if (negate)
		g_assert_cmpint (g_hash_table_size (expects), ==, ntotal);
	else
		g_assert_cmpint (g_hash_table_size (expects), ==, 0);

	g_hash_table_destroy (expects);
}

static void
ebmb_test_cache_contains (EBookCache *book_cache,
			  gboolean negate,
			  gboolean exact,
			  ...) /* uid-s, ended with NULL */
{
	va_list args;
	GHashTable *expects;
	GHashTableIter iter;
	ECache *cache;
	gpointer key;
	gint found = 0;

	g_return_if_fail (E_IS_BOOK_CACHE (book_cache));

	va_start (args, exact);
	expects = ebmb_test_gather_uids (args);
	va_end (args);

	cache = E_CACHE (book_cache);

	g_hash_table_iter_init (&iter, expects);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		const gchar *uid = key;

		g_assert_nonnull (uid);

		if (e_cache_contains (cache, uid, E_CACHE_EXCLUDE_DELETED))
			found++;
	}

	if (negate)
		g_assert_cmpint (0, ==, found);
	else
		g_assert_cmpint (g_hash_table_size (expects), ==, found);

	g_hash_table_destroy (expects);

	if (exact && !negate)
		g_assert_cmpint (e_cache_get_count (cache, E_CACHE_EXCLUDE_DELETED, NULL, NULL), ==, found);
}

static void
ebmb_test_cache_and_server_equal (EBookCache *book_cache,
				  GHashTable *contacts,
				  ECacheDeletedFlag deleted_flag)
{
	ECache *cache;
	GHashTableIter iter;
	gpointer uid;

	g_return_if_fail (E_IS_BOOK_CACHE (book_cache));
	g_return_if_fail (contacts != NULL);

	cache = E_CACHE (book_cache);

	g_assert_cmpint (e_cache_get_count (cache, deleted_flag, NULL, NULL), ==,
		g_hash_table_size (contacts));

	g_hash_table_iter_init (&iter, contacts);
	while (g_hash_table_iter_next (&iter, &uid, NULL)) {
		g_assert_true (e_cache_contains (cache, uid, deleted_flag));
	}
}

static gchar *
e_book_meta_backend_test_get_backend_property (EBookBackend *book_backend,
					       const gchar *prop_name)
{
	g_return_val_if_fail (E_IS_BOOK_META_BACKEND_TEST (book_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (",",
			e_book_meta_backend_get_capabilities (E_BOOK_META_BACKEND (book_backend)),
			"local",
			"contact-lists",
			NULL);
	}

	/* Chain up to parent's method. */
	return E_BOOK_BACKEND_CLASS (e_book_meta_backend_test_parent_class)->impl_get_backend_property (book_backend, prop_name);
}

static gboolean
e_book_meta_backend_test_get_destination_address (EBackend *backend,
						  gchar **host,
						  guint16 *port)
{
	g_return_val_if_fail (E_IS_BOOK_META_BACKEND_TEST (backend), FALSE);

	if (e_backend_get_online (backend))
		return FALSE;

	/* Provide something unreachable, to not have the meta backend switch the backend to online */
	*host = g_strdup ("server.no.where");
	*port = 65535;

	return TRUE;
}

static gboolean
e_book_meta_backend_test_connect_sync (EBookMetaBackend *meta_backend,
				       const ENamedParameters *credentials,
				       ESourceAuthenticationResult *out_auth_result,
				       gchar **out_certificate_pem,
				       GTlsCertificateFlags *out_certificate_errors,
				       GCancellable *cancellable,
				       GError **error)
{
	EBookMetaBackendTest *test_backend;

	g_return_val_if_fail (E_IS_BOOK_META_BACKEND_TEST (meta_backend), FALSE);

	test_backend = E_BOOK_META_BACKEND_TEST (meta_backend);

	if (test_backend->is_connected)
		return TRUE;

	test_backend->connect_count++;

	if (test_backend->can_connect) {
		test_backend->is_connected = TRUE;
		return TRUE;
	}

	g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_REPOSITORY_OFFLINE,
		e_client_error_to_string (E_CLIENT_ERROR_REPOSITORY_OFFLINE));

	return FALSE;
}

static gboolean
e_book_meta_backend_test_disconnect_sync (EBookMetaBackend *meta_backend,
					  GCancellable *cancellable,
					  GError **error)
{
	EBookMetaBackendTest *test_backend;

	g_return_val_if_fail (E_IS_BOOK_META_BACKEND_TEST (meta_backend), FALSE);

	test_backend = E_BOOK_META_BACKEND_TEST (meta_backend);
	test_backend->is_connected = FALSE;

	return TRUE;
}

static gboolean
e_book_meta_backend_test_get_changes_sync (EBookMetaBackend *meta_backend,
					   const gchar *last_sync_tag,
					   gboolean is_repeat,
					   gchar **out_new_sync_tag,
					   gboolean *out_repeat,
					   GSList **out_created_objects,
					   GSList **out_modified_objects,
					   GSList **out_removed_objects,
					   GCancellable *cancellable,
					   GError **error)
{
	EBookMetaBackendTest *test_backend;

	g_return_val_if_fail (E_IS_BOOK_META_BACKEND_TEST (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_repeat != NULL, FALSE);

	test_backend = E_BOOK_META_BACKEND_TEST (meta_backend);

	if (!test_backend->sync_tag_index) {
		g_assert_null (last_sync_tag);
	} else {
		g_assert_nonnull (last_sync_tag);
		g_assert_cmpint (atoi (last_sync_tag), ==, test_backend->sync_tag_index);
	}

	test_backend->sync_tag_index++;
	*out_new_sync_tag = g_strdup_printf ("%d", test_backend->sync_tag_index);

	if (test_backend->sync_tag_index == 2)
		*out_repeat = TRUE;
	else if (test_backend->sync_tag_index == 3)
		return TRUE;

	/* Nothing to do here at the moment, left the work to the parent class,
	   which calls list_existing_sync() internally. */
	return E_BOOK_META_BACKEND_CLASS (e_book_meta_backend_test_parent_class)->get_changes_sync (meta_backend,
		last_sync_tag, is_repeat, out_new_sync_tag, out_repeat, out_created_objects,
		out_modified_objects, out_removed_objects, cancellable, error);
}

static gboolean
e_book_meta_backend_test_list_existing_sync (EBookMetaBackend *meta_backend,
					     gchar **out_new_sync_tag,
					     GSList **out_existing_objects,
					     GCancellable *cancellable,
					     GError **error)
{
	EBookMetaBackendTest *test_backend;
	GHashTableIter iter;
	gpointer key, value;

	g_return_val_if_fail (E_IS_BOOK_META_BACKEND_TEST (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag, FALSE);
	g_return_val_if_fail (out_existing_objects, FALSE);

	test_backend = E_BOOK_META_BACKEND_TEST (meta_backend);
	test_backend->list_count++;

	g_assert_true (test_backend->is_connected);

	*out_existing_objects = NULL;

	g_hash_table_iter_init (&iter, test_backend->contacts);
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

	return TRUE;
}

static gboolean
e_book_meta_backend_test_save_contact_sync (EBookMetaBackend *meta_backend,
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
	EBookMetaBackendTest *test_backend;
	const gchar *uid;

	g_return_val_if_fail (E_IS_BOOK_META_BACKEND_TEST (meta_backend), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);
	g_return_val_if_fail (out_new_uid != NULL, FALSE);

	test_backend = E_BOOK_META_BACKEND_TEST (meta_backend);
	test_backend->save_count++;

	g_assert_true (test_backend->is_connected);

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	g_assert_nonnull (uid);

	if (g_hash_table_contains (test_backend->contacts, uid)) {
		if (!overwrite_existing) {
			g_propagate_error (error, e_book_client_error_create (E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS, NULL));
			return FALSE;
		}

		g_hash_table_remove (test_backend->contacts, uid);
	}

	/* Intentionally do not add a referenced 'contact', thus any later changes
	   on it are not "propagated" into the test_backend's content. */
	g_hash_table_insert (test_backend->contacts, g_strdup (uid), e_contact_duplicate (contact));

	*out_new_uid = g_strdup (uid);

	return TRUE;
}

static gboolean
e_book_meta_backend_test_load_contact_sync (EBookMetaBackend *meta_backend,
					    const gchar *uid,
					    const gchar *extra,
					    EContact **out_contact,
					    gchar **out_extra,
					    GCancellable *cancellable,
					    GError **error)
{
	EBookMetaBackendTest *test_backend;

	g_return_val_if_fail (E_IS_BOOK_META_BACKEND_TEST (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_contact != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	test_backend = E_BOOK_META_BACKEND_TEST (meta_backend);
	test_backend->load_count++;

	g_assert_true (test_backend->is_connected);

	*out_contact = g_hash_table_lookup (test_backend->contacts, uid);

	if (*out_contact) {
		*out_contact = e_contact_duplicate (*out_contact);
		*out_extra = g_strconcat ("extra for ", uid, NULL);
		return TRUE;
	} else {
		g_propagate_error (error, e_book_client_error_create (E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND, NULL));
	}

	return FALSE;
}

static gboolean
e_book_meta_backend_test_remove_contact_sync (EBookMetaBackend *meta_backend,
					      EConflictResolution conflict_resolution,
					      const gchar *uid,
					      const gchar *extra,
					      const gchar *object,
					      guint32 opflags,
					      GCancellable *cancellable,
					      GError **error)
{
	EBookMetaBackendTest *test_backend;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_META_BACKEND_TEST (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (extra != NULL, FALSE);

	test_backend = E_BOOK_META_BACKEND_TEST (meta_backend);
	test_backend->remove_count++;

	g_assert_true (test_backend->is_connected);

	success = g_hash_table_remove (test_backend->contacts, uid);
	if (success) {
		gchar *expected_extra;

		expected_extra = g_strconcat ("extra for ", uid, NULL);
		g_assert_cmpstr (expected_extra, ==, extra);
		g_free (expected_extra);
	} else {
		g_propagate_error (error, e_book_client_error_create (E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND, NULL));
	}

	return success;
}

static void
e_book_meta_backend_test_reset_counters (EBookMetaBackendTest *test_backend)
{
	g_return_if_fail (E_IS_BOOK_META_BACKEND_TEST (test_backend));

	test_backend->connect_count = 0;
	test_backend->list_count = 0;
	test_backend->save_count = 0;
	test_backend->load_count = 0;
	test_backend->remove_count = 0;
}

static EBookCache *glob_use_cache = NULL;

static void
e_book_meta_backend_test_constructed (GObject *object)
{
	EBookMetaBackendTest *test_backend = E_BOOK_META_BACKEND_TEST (object);

	g_assert_nonnull (glob_use_cache);

	/* Set it before EBookMetaBackend::constucted() creates its own cache */
	e_book_meta_backend_set_cache (E_BOOK_META_BACKEND (test_backend), glob_use_cache);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_meta_backend_test_parent_class)->constructed (object);
}

static void
e_book_meta_backend_test_finalize (GObject *object)
{
	EBookMetaBackendTest *test_backend = E_BOOK_META_BACKEND_TEST (object);

	g_assert_nonnull (test_backend->contacts);

	g_hash_table_destroy (test_backend->contacts);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_meta_backend_test_parent_class)->finalize (object);
}

static void
e_book_meta_backend_test_class_init (EBookMetaBackendTestClass *klass)
{
	EBookMetaBackendClass *book_meta_backend_class;
	EBookBackendClass *book_backend_class;
	EBackendClass *backend_class;
	GObjectClass *object_class;

	book_meta_backend_class = E_BOOK_META_BACKEND_CLASS (klass);
	book_meta_backend_class->connect_sync = e_book_meta_backend_test_connect_sync;
	book_meta_backend_class->disconnect_sync = e_book_meta_backend_test_disconnect_sync;
	book_meta_backend_class->get_changes_sync = e_book_meta_backend_test_get_changes_sync;
	book_meta_backend_class->list_existing_sync = e_book_meta_backend_test_list_existing_sync;
	book_meta_backend_class->save_contact_sync = e_book_meta_backend_test_save_contact_sync;
	book_meta_backend_class->load_contact_sync = e_book_meta_backend_test_load_contact_sync;
	book_meta_backend_class->remove_contact_sync = e_book_meta_backend_test_remove_contact_sync;

	book_backend_class = E_BOOK_BACKEND_CLASS (klass);
	book_backend_class->impl_get_backend_property = e_book_meta_backend_test_get_backend_property;

	backend_class = E_BACKEND_CLASS (klass);
	backend_class->get_destination_address = e_book_meta_backend_test_get_destination_address;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = e_book_meta_backend_test_constructed;
	object_class->finalize = e_book_meta_backend_test_finalize;
}

static void
e_book_meta_backend_test_init (EBookMetaBackendTest *test_backend)
{
	test_backend->sync_tag_index = 0;
	test_backend->is_connected = FALSE;
	test_backend->can_connect = TRUE;
	test_backend->contacts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	ebmb_test_add_test_case (test_backend, "custom-1");
	ebmb_test_add_test_case (test_backend, "custom-2");
	ebmb_test_add_test_case (test_backend, "custom-3");
	ebmb_test_add_test_case (test_backend, "custom-5");
	ebmb_test_add_test_case (test_backend, "custom-6");

	e_book_meta_backend_test_reset_counters (test_backend);

	e_backend_set_online (E_BACKEND (test_backend), TRUE);
	e_book_backend_set_writable (E_BOOK_BACKEND (test_backend), TRUE);
}

static ESourceRegistry *glob_registry = NULL;

static EBookMetaBackend *
e_book_meta_backend_test_new (EBookCache *cache)
{
	EBookMetaBackend *meta_backend;
	GHashTableIter iter;
	ESource *scratch;
	gpointer contact;
	gboolean success;
	GError *error = NULL;

	g_assert_true (E_IS_BOOK_CACHE (cache));

	g_assert_nonnull (glob_registry);
	g_assert_null (glob_use_cache);

	glob_use_cache = cache;

	scratch = e_source_new_with_uid ("test-source", NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (scratch);

	meta_backend = g_object_new (E_TYPE_BOOK_META_BACKEND_TEST,
		"source", scratch,
		"registry", glob_registry,
		NULL);
	g_assert_nonnull (meta_backend);

	g_assert_true (glob_use_cache == cache);
	glob_use_cache = NULL;

	g_object_unref (scratch);

	g_hash_table_iter_init (&iter, E_BOOK_META_BACKEND_TEST (meta_backend)->contacts);
	while (g_hash_table_iter_next (&iter, NULL, &contact)) {
		gchar *extra;

		extra = g_strconcat ("extra for ", e_contact_get_const (contact, E_CONTACT_UID), NULL);
		success = e_book_cache_put_contact (cache, contact, extra, 0, E_CACHE_IS_ONLINE, NULL, &error);
		g_free (extra);

		g_assert_no_error (error);
		g_assert_true (success);
	}

	return meta_backend;
}

static void
e_book_meta_backend_test_change_online (EBookMetaBackend *meta_backend,
					gboolean is_online)
{
	EFlag *flag;
	gulong handler_id;

	if (!is_online) {
		e_backend_set_online (E_BACKEND (meta_backend), FALSE);
		return;
	}

	if (e_backend_get_online (E_BACKEND (meta_backend)))
		return;

	flag = e_flag_new ();

	handler_id = g_signal_connect_swapped (meta_backend, "refresh-completed",
		G_CALLBACK (e_flag_set), flag);

	/* Going online triggers refresh, thus wait for it */
	e_backend_set_online (E_BACKEND (meta_backend), TRUE);

	e_flag_wait (flag);
	e_flag_free (flag);

	g_signal_handler_disconnect (meta_backend, handler_id);
}

static void
e_book_meta_backend_test_call_refresh (EBookMetaBackend *meta_backend)
{
	EBookBackendSyncClass *backend_sync_class;
	EFlag *flag;
	gulong handler_id;
	gboolean success;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_sync_class = E_BOOK_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_sync_class != NULL);
	g_return_if_fail (backend_sync_class->refresh_sync != NULL);

	if (!e_backend_get_online (E_BACKEND (meta_backend)))
		return;

	flag = e_flag_new ();

	handler_id = g_signal_connect_swapped (meta_backend, "refresh-completed",
		G_CALLBACK (e_flag_set), flag);

	success = backend_sync_class->refresh_sync (E_BOOK_BACKEND_SYNC (meta_backend), NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	e_flag_wait (flag);
	e_flag_free (flag);

	g_signal_handler_disconnect (meta_backend, handler_id);
}

static void
test_one_photo (EBookMetaBackend *meta_backend,
		const gchar *test_case,
		EContactField field)
{
	EContact *contact;
	EContactPhoto *photo;
	guchar *orig_content;
	gchar *new_content = NULL;
	gsize orig_len = 0, new_len = 0;
	gchar *filename;
	gchar *mime_type;
	gboolean success;
	GError *error = NULL;

	g_assert_true (E_IS_BOOK_META_BACKEND (meta_backend));
	g_assert_nonnull (test_case);
	g_assert_true (field == E_CONTACT_PHOTO || field == E_CONTACT_LOGO);

	contact = tcu_new_contact_from_test_case (test_case);
	g_assert_nonnull (contact);

	photo = e_contact_get (contact, field);
	g_assert_nonnull (photo);
	g_assert_cmpint (photo->type, ==, E_CONTACT_PHOTO_TYPE_INLINED);

	orig_content = (guchar *) e_contact_photo_get_inlined (photo, &orig_len);
	g_assert_nonnull (orig_content);
	g_assert_cmpint (orig_len, >, 0);

	mime_type = g_strdup (e_contact_photo_get_mime_type (photo));
	g_assert_nonnull (mime_type);

	orig_content = g_memdup2 (orig_content, (guint) orig_len);

	e_contact_photo_free (photo);

	success = e_book_meta_backend_store_inline_photos_sync (meta_backend, contact, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	photo = e_contact_get (contact, field);
	g_assert_nonnull (photo);
	g_assert_cmpint (photo->type, ==, E_CONTACT_PHOTO_TYPE_URI);
	g_assert_nonnull (e_contact_photo_get_uri (photo));

	filename = g_filename_from_uri (e_contact_photo_get_uri (photo), NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (filename);

	success = g_file_get_contents (filename, &new_content, &new_len, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (new_content);
	g_assert_cmpmem (orig_content, orig_len, new_content, new_len);

	g_free (new_content);
	g_free (filename);

	e_contact_photo_free (photo);

	success = e_book_meta_backend_inline_local_photos_sync (meta_backend, contact, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	photo = e_contact_get (contact, field);
	g_assert_nonnull (photo);
	g_assert_cmpint (photo->type, ==, E_CONTACT_PHOTO_TYPE_INLINED);

	new_content = (gchar *) e_contact_photo_get_inlined (photo, &new_len);
	g_assert_nonnull (new_content);
	g_assert_cmpmem (orig_content, orig_len, new_content, new_len);
	g_assert_cmpstr (mime_type, ==, e_contact_photo_get_mime_type (photo));

	e_contact_photo_free (photo);
	g_free (orig_content);
	g_free (mime_type);

	/* Also try with remote URI, which should be left as is */
	photo = e_contact_photo_new ();
	g_assert_nonnull (photo);

	photo->type = E_CONTACT_PHOTO_TYPE_URI;
	e_contact_photo_set_uri (photo, REMOTE_URL);
	e_contact_set (contact, field, photo);
	e_contact_photo_free (photo);

	photo = e_contact_get (contact, field);
	g_assert_nonnull (photo);
	g_assert_cmpint (photo->type, ==, E_CONTACT_PHOTO_TYPE_URI);
	g_assert_cmpstr (e_contact_photo_get_uri (photo), ==, REMOTE_URL);
	e_contact_photo_free (photo);

	success = e_book_meta_backend_store_inline_photos_sync (meta_backend, contact, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	photo = e_contact_get (contact, field);
	g_assert_nonnull (photo);
	g_assert_cmpint (photo->type, ==, E_CONTACT_PHOTO_TYPE_URI);
	g_assert_cmpstr (e_contact_photo_get_uri (photo), ==, REMOTE_URL);
	e_contact_photo_free (photo);

	success = e_book_meta_backend_inline_local_photos_sync (meta_backend, contact, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	photo = e_contact_get (contact, field);
	g_assert_nonnull (photo);
	g_assert_cmpint (photo->type, ==, E_CONTACT_PHOTO_TYPE_URI);
	g_assert_cmpstr (e_contact_photo_get_uri (photo), ==, REMOTE_URL);
	e_contact_photo_free (photo);

	g_object_unref (contact);
}

static void
test_photos (TCUFixture *fixture,
	     gconstpointer user_data)
{
	EBookMetaBackend *meta_backend;

	meta_backend = e_book_meta_backend_test_new (fixture->book_cache);
	g_assert_nonnull (meta_backend);

	test_one_photo (meta_backend, "photo-1", E_CONTACT_PHOTO);
	test_one_photo (meta_backend, "logo-1", E_CONTACT_LOGO);

	g_object_unref (meta_backend);
}

static void
test_empty_cache (TCUFixture *fixture,
		  gconstpointer user_data)
{
	EBookMetaBackend *meta_backend;
	EBookMetaBackendTest *test_backend;
	GSList *uids;
	gboolean success;
	GError *error = NULL;

	meta_backend = e_book_meta_backend_test_new (fixture->book_cache);
	g_assert_nonnull (meta_backend);

	test_backend = E_BOOK_META_BACKEND_TEST (meta_backend);
	g_assert_nonnull (test_backend);

	uids = NULL;
	success = e_book_cache_search_uids (fixture->book_cache, NULL, &uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (g_slist_length (uids), >, 0);
	g_slist_free_full (uids, g_free);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), >, 0);
	g_assert_no_error (error);

	/* Empty the cache */
	success = e_book_meta_backend_empty_cache_sync (meta_backend, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	/* Verify the cache is truly empty */
	uids = NULL;
	success = e_book_cache_search_uids (fixture->book_cache, NULL, &uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (g_slist_length (uids), ==, 0);
	g_slist_free_full (uids, g_free);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 0);
	g_assert_no_error (error);

	g_object_unref (meta_backend);
}

static void
test_create_contacts (EBookMetaBackend *meta_backend)
{
	EBookMetaBackendTest *test_backend;
	EBookBackendSyncClass *backend_sync_class;
	EBookCache *book_cache;
	GSList *offline_changes;
	gchar *vcards[2] = { NULL, NULL }, *tmp;
	GSList *new_contacts = NULL;
	gboolean success;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_sync_class = E_BOOK_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_sync_class != NULL);
	g_return_if_fail (backend_sync_class->create_contacts_sync != NULL);

	test_backend = E_BOOK_META_BACKEND_TEST (meta_backend);
	book_cache = e_book_meta_backend_ref_cache (meta_backend);
	g_assert_nonnull (book_cache);

	ebmb_test_cache_and_server_equal (book_cache, test_backend->contacts, E_CACHE_INCLUDE_DELETED);

	/* Try to add existing contact, it should fail */
	vcards[0] = tcu_new_vcard_from_test_case ("custom-1");

	success = backend_sync_class->create_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) vcards, E_BOOK_OPERATION_FLAG_NONE, &new_contacts, NULL, &error);
	g_assert_error (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS);
	g_assert_true (!success);
	g_assert_cmpint (g_slist_length (new_contacts), ==, 0);
	g_clear_error (&error);
	g_free (vcards[0]);

	e_book_meta_backend_test_reset_counters (test_backend);

	/* Try to add new contact */
	vcards[0] = tcu_new_vcard_from_test_case ("custom-7");

	success = backend_sync_class->create_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) vcards, E_BOOK_OPERATION_FLAG_NONE, &new_contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (g_slist_length (new_contacts), ==, 1);
	g_assert_cmpstr (e_contact_get_const (new_contacts->data, E_CONTACT_UID), ==, "custom-7");
	g_assert_cmpint (test_backend->connect_count, ==, 1);
	g_assert_cmpint (test_backend->list_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 1);
	g_assert_cmpint (test_backend->save_count, ==, 1);

	g_slist_free_full (new_contacts, g_object_unref);
	new_contacts = NULL;
	g_free (vcards[0]);

	ebmb_test_cache_and_server_equal (book_cache, test_backend->contacts, E_CACHE_INCLUDE_DELETED);

	/* Going offline */
	e_book_meta_backend_test_change_online (meta_backend, FALSE);

	e_book_meta_backend_test_reset_counters (test_backend);

	/* Try to add existing contact, it should fail */
	vcards[0] = tcu_new_vcard_from_test_case ("custom-7");

	success = backend_sync_class->create_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) vcards, E_BOOK_OPERATION_FLAG_NONE, &new_contacts, NULL, &error);
	g_assert_error (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS);
	g_assert_true (!success);
	g_assert_cmpint (g_slist_length (new_contacts), ==, 0);
	g_clear_error (&error);
	g_free (vcards[0]);
	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);

	/* Try to add new contact */
	vcards[0] = tcu_new_vcard_from_test_case ("custom-8");

	success = backend_sync_class->create_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) vcards, E_BOOK_OPERATION_FLAG_NONE, &new_contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (g_slist_length (new_contacts), ==, 1);
	g_assert_cmpstr (e_contact_get_const (new_contacts->data, E_CONTACT_UID), ==, "custom-8");
	g_assert_cmpint (test_backend->connect_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);

	g_slist_free_full (new_contacts, g_object_unref);
	new_contacts = NULL;
	g_free (vcards[0]);

	ebmb_test_hash_contains (test_backend->contacts, TRUE, FALSE,
		"custom-8", NULL, NULL);
	ebmb_test_cache_contains (book_cache, FALSE, FALSE,
		"custom-8", NULL, NULL);

	/* Going online */
	e_book_meta_backend_test_change_online (meta_backend, TRUE);

	g_assert_cmpint (test_backend->connect_count, ==, 1);
	g_assert_cmpint (test_backend->load_count, ==, 1);
	g_assert_cmpint (test_backend->save_count, ==, 1);

	ebmb_test_cache_and_server_equal (book_cache, test_backend->contacts, E_CACHE_INCLUDE_DELETED);

	offline_changes = e_cache_get_offline_changes (E_CACHE (book_cache), NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (0, ==, g_slist_length (offline_changes));

	/* Add contact without UID */
	vcards[0] = tcu_new_vcard_from_test_case ("custom-9");
	g_assert_nonnull (vcards[0]);
	tmp = strstr (vcards[0], "UID:custom-9\r\n");
	g_assert_nonnull (tmp);
	memcpy (tmp, "X-TEST:*007*", 12);

	success = backend_sync_class->create_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) vcards, E_BOOK_OPERATION_FLAG_NONE, &new_contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (g_slist_length (new_contacts), ==, 1);
	g_assert_cmpstr (e_contact_get_const (new_contacts->data, E_CONTACT_UID), !=, "custom-9");
	g_assert_cmpint (test_backend->connect_count, ==, 1);
	g_assert_cmpint (test_backend->list_count, ==, 1);
	g_assert_cmpint (test_backend->load_count, ==, 2);
	g_assert_cmpint (test_backend->save_count, ==, 2);

	tmp = e_vcard_to_string (E_VCARD (new_contacts->data));
	g_assert_nonnull (tmp);
	g_assert_nonnull (strstr (tmp, "X-TEST:*007*\r\n"));
	g_assert_nonnull (strstr (tmp, e_contact_get_const (new_contacts->data, E_CONTACT_UID)));
	g_free (tmp);

	g_slist_free_full (new_contacts, g_object_unref);
	new_contacts = NULL;
	g_free (vcards[0]);

	ebmb_test_cache_and_server_equal (book_cache, test_backend->contacts, E_CACHE_INCLUDE_DELETED);

	g_object_unref (book_cache);
}

static gchar *
ebmb_test_modify_case (const gchar *case_name)
{
	gchar *vcard, *tmp;
	const gchar *rev;
	EContact *contact;

	g_assert_nonnull (case_name);

	contact = tcu_new_contact_from_test_case (case_name);
	g_assert_nonnull (contact);

	e_contact_set (contact, E_CONTACT_FULL_NAME, MODIFIED_FN_STR);

	rev = e_contact_get_const (contact, E_CONTACT_REV);
	if (!rev)
		tmp = g_strdup ("0");
	else
		tmp = g_strdup_printf ("%d", atoi (rev) + 1);
	e_contact_set (contact, E_CONTACT_REV, tmp);
	g_free (tmp);

	vcard = e_vcard_to_string (E_VCARD (contact));
	g_object_unref (contact);

	return vcard;
}

static void
test_modify_contacts (EBookMetaBackend *meta_backend)
{
	EBookMetaBackendTest *test_backend;
	EBookBackendSyncClass *backend_sync_class;
	EBookCache *book_cache;
	EContact *contact;
	GSList *offline_changes;
	gchar *vcards[2] = { NULL, NULL }, *tmp;
	GSList *new_contacts = NULL;
	gint old_rev, new_rev;
	gboolean success;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_sync_class = E_BOOK_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_sync_class != NULL);
	g_return_if_fail (backend_sync_class->modify_contacts_sync != NULL);

	test_backend = E_BOOK_META_BACKEND_TEST (meta_backend);
	book_cache = e_book_meta_backend_ref_cache (meta_backend);
	g_assert_nonnull (book_cache);

	/* Modify non-existing contact */
	vcards[0] = tcu_new_vcard_from_test_case ("custom-1");
	g_assert_nonnull (vcards[0]);
	tmp = strstr (vcards[0], "UID:custom-1");
	g_assert_nonnull (tmp);
	memcpy (tmp + 4, "unknown", 7);

	success = backend_sync_class->modify_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) vcards, E_BOOK_OPERATION_FLAG_NONE, &new_contacts, NULL, &error);
	g_assert_error (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND);
	g_assert_true (!success);
	g_assert_cmpint (g_slist_length (new_contacts), ==, 0);
	g_clear_error (&error);
	g_free (vcards[0]);

	/* Modify existing contact */
	vcards[0] = ebmb_test_modify_case ("custom-1");

	success = backend_sync_class->modify_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) vcards, E_BOOK_OPERATION_FLAG_NONE, &new_contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (g_slist_length (new_contacts), ==, 1);
	g_assert_cmpint (test_backend->load_count, ==, 1);
	g_assert_cmpint (test_backend->save_count, ==, 1);

	contact = tcu_new_contact_from_test_case ("custom-1");
	g_assert_nonnull (contact);
	g_assert_nonnull (e_contact_get_const (contact, E_CONTACT_REV));
	g_assert_nonnull (e_contact_get_const (contact, E_CONTACT_FULL_NAME));

	old_rev = atoi (e_contact_get_const (contact, E_CONTACT_REV));
	g_assert_cmpstr (e_contact_get_const (contact, E_CONTACT_FULL_NAME), !=, MODIFIED_FN_STR);
	g_assert_cmpstr (e_contact_get_const (contact, E_CONTACT_UID), ==, "custom-1");

	g_object_unref (contact);

	contact = new_contacts->data;
	g_assert_nonnull (contact);
	g_assert_nonnull (e_contact_get_const (contact, E_CONTACT_REV));
	g_assert_nonnull (e_contact_get_const (contact, E_CONTACT_FULL_NAME));

	new_rev = atoi (e_contact_get_const (contact, E_CONTACT_REV));
	g_assert_cmpint (old_rev + 1, ==, new_rev);
	g_assert_cmpstr (e_contact_get_const (contact, E_CONTACT_FULL_NAME), ==, MODIFIED_FN_STR);
	g_assert_cmpstr (e_contact_get_const (contact, E_CONTACT_UID), ==, "custom-1");

	g_slist_free_full (new_contacts, g_object_unref);
	new_contacts = NULL;
	g_free (vcards[0]);

	/* Going offline */
	e_book_meta_backend_test_change_online (meta_backend, FALSE);

	e_book_meta_backend_test_reset_counters (test_backend);

	/* Modify custom-2 */
	vcards[0] = ebmb_test_modify_case ("custom-2");

	success = backend_sync_class->modify_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) vcards, E_BOOK_OPERATION_FLAG_NONE, &new_contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (g_slist_length (new_contacts), ==, 1);
	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);

	contact = tcu_new_contact_from_test_case ("custom-2");
	g_assert_nonnull (contact);
	g_assert_nonnull (e_contact_get_const (contact, E_CONTACT_REV));
	g_assert_nonnull (e_contact_get_const (contact, E_CONTACT_FULL_NAME));

	old_rev = atoi (e_contact_get_const (contact, E_CONTACT_REV));
	g_assert_cmpstr (e_contact_get_const (contact, E_CONTACT_FULL_NAME), !=, MODIFIED_FN_STR);
	g_assert_cmpstr (e_contact_get_const (contact, E_CONTACT_UID), ==, "custom-2");

	g_object_unref (contact);

	contact = new_contacts->data;
	g_assert_nonnull (contact);
	g_assert_nonnull (e_contact_get_const (contact, E_CONTACT_REV));
	g_assert_nonnull (e_contact_get_const (contact, E_CONTACT_FULL_NAME));

	new_rev = atoi (e_contact_get_const (contact, E_CONTACT_REV));
	g_assert_cmpint (old_rev + 1, ==, new_rev);
	g_assert_cmpstr (e_contact_get_const (contact, E_CONTACT_FULL_NAME), ==, MODIFIED_FN_STR);
	g_assert_cmpstr (e_contact_get_const (contact, E_CONTACT_UID), ==, "custom-2");

	g_slist_free_full (new_contacts, g_object_unref);
	new_contacts = NULL;
	g_free (vcards[0]);

	/* Going online */
	e_book_meta_backend_test_change_online (meta_backend, TRUE);

	g_assert_cmpint (test_backend->load_count, ==, 1);
	g_assert_cmpint (test_backend->save_count, ==, 1);

	ebmb_test_cache_and_server_equal (book_cache, test_backend->contacts, E_CACHE_INCLUDE_DELETED);

	offline_changes = e_cache_get_offline_changes (E_CACHE (book_cache), NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (0, ==, g_slist_length (offline_changes));

	g_object_unref (book_cache);
}

static void
test_remove_contacts (EBookMetaBackend *meta_backend)
{
	EBookMetaBackendTest *test_backend;
	EBookBackendSyncClass *backend_sync_class;
	EBookCache *book_cache;
	EOfflineState state;
	const gchar *uids[2] = { NULL, NULL };
	GSList *offline_changes;
	GSList *removed_uids = NULL;
	gboolean success;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_sync_class = E_BOOK_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_sync_class != NULL);
	g_return_if_fail (backend_sync_class->remove_contacts_sync != NULL);

	test_backend = E_BOOK_META_BACKEND_TEST (meta_backend);
	book_cache = e_book_meta_backend_ref_cache (meta_backend);
	g_assert_nonnull (book_cache);

	/* Remove non-existing contact */
	uids[0] = "unknown-contact";

	success = backend_sync_class->remove_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) uids, E_BOOK_OPERATION_FLAG_NONE, &removed_uids, NULL, &error);
	g_assert_error (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND);
	g_assert_true (!success);
	g_assert_null (removed_uids);
	g_clear_error (&error);

	/* Remove existing contact */
	uids[0] = "custom-1";

	success = backend_sync_class->remove_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) uids, E_BOOK_OPERATION_FLAG_NONE, &removed_uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->remove_count, ==, 1);
	g_assert_cmpint (g_slist_length (removed_uids), ==, 1);
	g_assert_cmpstr (removed_uids->data, ==, uids[0]);
	g_slist_free_full (removed_uids, g_free);
	removed_uids = NULL;

	ebmb_test_hash_contains (test_backend->contacts, TRUE, FALSE,
		"custom-1", NULL,
		NULL);

	/* Going offline */
	e_book_meta_backend_test_change_online (meta_backend, FALSE);

	e_book_meta_backend_test_reset_counters (test_backend);

	/* Remove existing contact */
	uids[0] = "custom-3";

	success = backend_sync_class->remove_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) uids, E_BOOK_OPERATION_FLAG_NONE, &removed_uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->remove_count, ==, 0);
	g_assert_cmpint (g_slist_length (removed_uids), ==, 1);
	g_assert_cmpstr (removed_uids->data, ==, uids[0]);
	g_slist_free_full (removed_uids, g_free);
	removed_uids = NULL;

	ebmb_test_hash_contains (test_backend->contacts, FALSE, FALSE,
		"custom-3", NULL,
		NULL);
	ebmb_test_cache_contains (book_cache, TRUE, FALSE,
		"custom-3", NULL,
		NULL);

	/* Going online */
	e_book_meta_backend_test_change_online (meta_backend, TRUE);

	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->remove_count, ==, 1);

	ebmb_test_hash_contains (test_backend->contacts, TRUE, FALSE,
		"custom-3", NULL,
		NULL);
	ebmb_test_cache_contains (book_cache, TRUE, FALSE,
		"custom-3", NULL,
		NULL);

	ebmb_test_cache_and_server_equal (book_cache, test_backend->contacts, E_CACHE_INCLUDE_DELETED);

	offline_changes = e_cache_get_offline_changes (E_CACHE (book_cache), NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (0, ==, g_slist_length (offline_changes));

	/* Set a contact as being created in offline */
	uids[0] = "custom-2";

	success = e_cache_set_offline_state (E_CACHE (book_cache), uids[0], E_OFFLINE_STATE_LOCALLY_CREATED, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	state = e_cache_get_offline_state (E_CACHE (book_cache), uids[0], NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (state, ==, E_OFFLINE_STATE_LOCALLY_CREATED);

	success = backend_sync_class->remove_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) uids, E_BOOK_OPERATION_FLAG_NONE, &removed_uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->remove_count, ==, 1);
	g_assert_cmpint (g_slist_length (removed_uids), ==, 1);
	g_assert_cmpstr (removed_uids->data, ==, uids[0]);
	g_slist_free_full (removed_uids, g_free);
	removed_uids = NULL;

	ebmb_test_hash_contains (test_backend->contacts, FALSE, FALSE,
		uids[0], NULL,
		NULL);
	ebmb_test_cache_contains (book_cache, TRUE, FALSE,
		uids[0], NULL,
		NULL);

	/* Set a contact as being modified in offline */
	uids[0] = "custom-5";

	success = e_cache_set_offline_state (E_CACHE (book_cache), uids[0], E_OFFLINE_STATE_LOCALLY_MODIFIED, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	state = e_cache_get_offline_state (E_CACHE (book_cache), uids[0], NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (state, ==, E_OFFLINE_STATE_LOCALLY_MODIFIED);

	success = backend_sync_class->remove_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) uids, E_BOOK_OPERATION_FLAG_NONE, &removed_uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->remove_count, ==, 2);
	g_assert_cmpint (g_slist_length (removed_uids), ==, 1);
	g_assert_cmpstr (removed_uids->data, ==, uids[0]);
	g_slist_free_full (removed_uids, g_free);
	removed_uids = NULL;

	ebmb_test_hash_contains (test_backend->contacts, TRUE, FALSE,
		uids[0], NULL,
		NULL);
	ebmb_test_cache_contains (book_cache, TRUE, FALSE,
		uids[0], NULL,
		NULL);

	/* Set a contact as being deleted in offline */
	uids[0] = "custom-6";

	success = e_cache_set_offline_state (E_CACHE (book_cache), uids[0], E_OFFLINE_STATE_LOCALLY_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	state = e_cache_get_offline_state (E_CACHE (book_cache), uids[0], NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (state, ==, E_OFFLINE_STATE_LOCALLY_DELETED);

	success = backend_sync_class->remove_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) uids, E_BOOK_OPERATION_FLAG_NONE, &removed_uids, NULL, &error);
	g_assert_error (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND);
	g_assert_true (!success);
	g_assert_null (removed_uids);
	g_clear_error (&error);

	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->remove_count, ==, 2);

	ebmb_test_hash_contains (test_backend->contacts, FALSE, FALSE,
		uids[0], NULL,
		NULL);
	ebmb_test_cache_contains (book_cache, TRUE, FALSE,
		uids[0], NULL,
		NULL);

	g_object_unref (book_cache);
}

static void
test_get_contact (EBookMetaBackend *meta_backend)
{
	EBookMetaBackendTest *test_backend;
	EBookBackendSyncClass *backend_sync_class;
	EBookCache *book_cache;
	EContact *contact;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_sync_class = E_BOOK_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_sync_class != NULL);
	g_return_if_fail (backend_sync_class->get_contact_sync != NULL);

	test_backend = E_BOOK_META_BACKEND_TEST (meta_backend);
	book_cache = e_book_meta_backend_ref_cache (meta_backend);
	g_assert_nonnull (book_cache);

	e_book_cache_remove_contact (book_cache, "custom-5", 0, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);
	e_book_cache_remove_contact (book_cache, "custom-6", 0, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);

	/* Non-existing */
	contact = backend_sync_class->get_contact_sync (E_BOOK_BACKEND_SYNC (meta_backend), "unknown-contact", NULL, &error);
	g_assert_error (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND);
	g_assert_null (contact);
	g_clear_error (&error);

	/* Existing */
	contact = backend_sync_class->get_contact_sync (E_BOOK_BACKEND_SYNC (meta_backend), "custom-1", NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (contact);
	g_assert_cmpstr (e_contact_get_const (contact, E_CONTACT_UID), ==, "custom-1");
	g_object_unref (contact);

	/* Going offline */
	e_book_meta_backend_test_change_online (meta_backend, FALSE);

	g_assert_true (!e_cache_contains (E_CACHE (book_cache), "custom-5", E_CACHE_EXCLUDE_DELETED));

	e_book_meta_backend_test_reset_counters (test_backend);

	contact = backend_sync_class->get_contact_sync (E_BOOK_BACKEND_SYNC (meta_backend), "custom-5", NULL, &error);
	g_assert_error (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND);
	g_assert_null (contact);
	g_clear_error (&error);
	g_assert_cmpint (test_backend->connect_count, ==, 0);
	g_assert_cmpint (test_backend->list_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 0);

	/* Going online */
	e_book_meta_backend_test_change_online (meta_backend, TRUE);

	g_assert_true (e_cache_contains (E_CACHE (book_cache), "custom-5", E_CACHE_EXCLUDE_DELETED));

	/* Remove it from the cache, thus it's loaded from the "server" on demand */
	e_book_cache_remove_contact (book_cache, "custom-5", 0, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);

	g_assert_cmpint (test_backend->connect_count, ==, 1);
	e_book_meta_backend_test_reset_counters (test_backend);
	g_assert_cmpint (test_backend->connect_count, ==, 0);

	contact = backend_sync_class->get_contact_sync (E_BOOK_BACKEND_SYNC (meta_backend), "custom-5", NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (contact);
	g_assert_cmpint (test_backend->connect_count, ==, 0);
	g_assert_cmpint (test_backend->list_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 1);
	g_assert_cmpstr (e_contact_get_const (contact, E_CONTACT_UID), ==, "custom-5");
	g_object_unref (contact);

	g_assert_true (e_cache_contains (E_CACHE (book_cache), "custom-5", E_CACHE_EXCLUDE_DELETED));

	g_object_unref (book_cache);
}

static void
test_get_contact_list (EBookMetaBackend *meta_backend)
{
	EBookBackendSyncClass *backend_sync_class;
	GSList *contacts = NULL;
	EContact *contact;
	gboolean success;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_sync_class = E_BOOK_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_sync_class != NULL);
	g_return_if_fail (backend_sync_class->get_contact_list_sync != NULL);

	success = backend_sync_class->get_contact_list_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		"(is \"uid\" \"unknown-contact\")", &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (g_slist_length (contacts), ==, 0);

	success = backend_sync_class->get_contact_list_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		"(is \"uid\" \"custom-3\")", &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (g_slist_length (contacts), ==, 1);
	contact = contacts->data;
	g_assert_nonnull (contact);
	g_assert_cmpstr (e_contact_get_const (contact, E_CONTACT_UID), ==, "custom-3");
	g_slist_free_full (contacts, g_object_unref);
}

static void
test_get_contact_list_uids (EBookMetaBackend *meta_backend)
{
	EBookBackendSyncClass *backend_sync_class;
	GSList *uids = NULL;
	gboolean success;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_sync_class = E_BOOK_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_sync_class != NULL);
	g_return_if_fail (backend_sync_class->get_contact_list_uids_sync != NULL);

	success = backend_sync_class->get_contact_list_uids_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		"(is \"uid\" \"unknown-contact\")", &uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (g_slist_length (uids), ==, 0);

	success = backend_sync_class->get_contact_list_uids_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		"(is \"uid\" \"custom-3\")", &uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (g_slist_length (uids), ==, 1);
	g_assert_nonnull (uids->data);
	g_assert_cmpstr (uids->data, ==, "custom-3");
	g_slist_free_full (uids, g_free);
}

static void
test_refresh (EBookMetaBackend *meta_backend)
{
	EBookMetaBackendTest *test_backend;
	EBookCache *book_cache;
	ECache *cache;
	guint count;
	EContact *contact;
	gchar *sync_tag;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	test_backend = E_BOOK_META_BACKEND_TEST (meta_backend);
	book_cache = e_book_meta_backend_ref_cache (meta_backend);
	g_assert_nonnull (book_cache);

	cache = E_CACHE (book_cache);

	/* Empty local cache */
	e_cache_remove_all (cache, NULL, &error);
	g_assert_no_error (error);

	count = e_cache_get_count (cache, E_CACHE_INCLUDE_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (count, ==, 0);

	e_book_meta_backend_test_reset_counters (test_backend);

	ebmb_test_remove_component (test_backend, "custom-5");
	ebmb_test_remove_component (test_backend, "custom-6");

	/* Sync with server content */
	e_book_meta_backend_test_call_refresh (meta_backend);

	g_assert_cmpint (test_backend->list_count, ==, 1);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 3);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	count = e_cache_get_count (cache, E_CACHE_INCLUDE_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (count, ==, 3);

	ebmb_test_cache_and_server_equal (book_cache, test_backend->contacts, E_CACHE_INCLUDE_DELETED);

	sync_tag = e_book_meta_backend_dup_sync_tag (meta_backend);
	g_assert_nonnull (sync_tag);
	g_assert_cmpstr (sync_tag, ==, "1");
	g_free (sync_tag);

	/* Add new contact */
	ebmb_test_add_test_case (test_backend, "custom-5");

	/* Sync with server content */
	e_book_meta_backend_test_call_refresh (meta_backend);

	g_assert_cmpint (test_backend->list_count, ==, 2);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 4);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	count = e_cache_get_count (cache, E_CACHE_INCLUDE_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (count, ==, 4);

	ebmb_test_hash_contains (test_backend->contacts, FALSE, TRUE,
		"custom-1",
		"custom-2",
		"custom-3",
		"custom-5",
		NULL);

	ebmb_test_cache_contains (book_cache, FALSE, TRUE,
		"custom-1",
		"custom-2",
		"custom-3",
		"custom-5",
		NULL);

	/* Sync with server content */
	e_book_meta_backend_test_call_refresh (meta_backend);

	g_assert_cmpint (test_backend->list_count, ==, 3);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 4);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	count = e_cache_get_count (cache, E_CACHE_INCLUDE_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (count, ==, 4);

	ebmb_test_cache_and_server_equal (book_cache, test_backend->contacts, E_CACHE_INCLUDE_DELETED);

	/* Add some more contacts */
	ebmb_test_add_test_case (test_backend, "custom-6");
	ebmb_test_add_test_case (test_backend, "custom-7");

	/* Sync with server content */
	e_book_meta_backend_test_call_refresh (meta_backend);

	g_assert_cmpint (test_backend->list_count, ==, 4);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 6);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	count = e_cache_get_count (cache, E_CACHE_INCLUDE_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (count, ==, 6);

	ebmb_test_cache_and_server_equal (book_cache, test_backend->contacts, E_CACHE_INCLUDE_DELETED);

	/* Remove two contacts */
	ebmb_test_remove_component (test_backend, "custom-2");
	ebmb_test_remove_component (test_backend, "custom-5");

	/* Sync with server content */
	e_book_meta_backend_test_call_refresh (meta_backend);

	g_assert_cmpint (test_backend->list_count, ==, 5);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 6);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	count = e_cache_get_count (cache, E_CACHE_INCLUDE_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (count, ==, 4);

	ebmb_test_cache_and_server_equal (book_cache, test_backend->contacts, E_CACHE_INCLUDE_DELETED);

	/* Mix add/remove/modify */
	ebmb_test_add_test_case (test_backend, "custom-8");

	ebmb_test_remove_component (test_backend, "custom-3");
	ebmb_test_remove_component (test_backend, "custom-6");

	contact = g_hash_table_lookup (test_backend->contacts, "custom-1");
	g_assert_nonnull (contact);
	e_contact_set (contact, E_CONTACT_REV, "changed");

	contact = g_hash_table_lookup (test_backend->contacts, "custom-7");
	g_assert_nonnull (contact);
	e_contact_set (contact, E_CONTACT_REV, "changed");

	/* Sync with server content */
	e_book_meta_backend_test_call_refresh (meta_backend);

	g_assert_cmpint (test_backend->list_count, ==, 6);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 9);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	count = e_cache_get_count (cache, E_CACHE_INCLUDE_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (count, ==, 3);

	ebmb_test_cache_and_server_equal (book_cache, test_backend->contacts, E_CACHE_INCLUDE_DELETED);

	sync_tag = e_book_meta_backend_dup_sync_tag (meta_backend);
	g_assert_nonnull (sync_tag);
	g_assert_cmpstr (sync_tag, ==, "7");
	g_free (sync_tag);

	g_object_unref (book_cache);
}

static void
test_cursor (EBookMetaBackend *meta_backend)
{
	EBookBackendClass *backend_class;
	EBookBackendSyncClass *backend_sync_class;
	EDataBookCursor *cursor;
	EContactField sort_fields[] = { E_CONTACT_FULL_NAME };
	EBookCursorSortType sort_types[] = { E_BOOK_CURSOR_SORT_ASCENDING };
	GSList *contacts = NULL;
	GSList *removed_uids = NULL;
	gchar *vcards[2] = { NULL, NULL };
	const gchar *uids[2] = { NULL, NULL };
	gint traversed;
	gboolean success;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_class = E_BOOK_BACKEND_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->impl_create_cursor != NULL);
	g_return_if_fail (backend_class->impl_delete_cursor != NULL);

	backend_sync_class = E_BOOK_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_sync_class != NULL);
	g_return_if_fail (backend_sync_class->create_contacts_sync != NULL);
	g_return_if_fail (backend_sync_class->modify_contacts_sync != NULL);
	g_return_if_fail (backend_sync_class->remove_contacts_sync != NULL);

	/* Create the cursor */
	cursor = backend_class->impl_create_cursor (E_BOOK_BACKEND (meta_backend),
		sort_fields, sort_types, 1, &error);
	g_assert_no_error (error);
	g_assert_nonnull (cursor);
	g_assert_cmpint (e_data_book_cursor_get_total (cursor), ==, 5);
	g_assert_cmpint (e_data_book_cursor_get_position (cursor), ==, 0);

	traversed = e_data_book_cursor_step (cursor, NULL, E_BOOK_CURSOR_STEP_MOVE, E_BOOK_CURSOR_ORIGIN_CURRENT, 3, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (traversed, ==, 3);
	g_assert_cmpint (e_data_book_cursor_get_total (cursor), ==, 5);
	g_assert_cmpint (e_data_book_cursor_get_position (cursor), ==, 3);

	/* Create */
	vcards[0] = tcu_new_vcard_from_test_case ("custom-7");
	success = backend_sync_class->create_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) vcards, E_BOOK_OPERATION_FLAG_NONE, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (g_slist_length (contacts), ==, 1);
	g_slist_free_full (contacts, g_object_unref);
	contacts = NULL;
	g_free (vcards[0]);

	g_assert_cmpint (e_data_book_cursor_get_total (cursor), ==, 6);
	g_assert_cmpint (e_data_book_cursor_get_position (cursor), ==, 3);

	/* Modify */
	vcards[0] = ebmb_test_modify_case ("custom-2");
	success = backend_sync_class->modify_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) vcards, E_BOOK_OPERATION_FLAG_NONE, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (g_slist_length (contacts), ==, 1);
	g_slist_free_full (contacts, g_object_unref);
	contacts = NULL;
	g_free (vcards[0]);

	g_assert_cmpint (e_data_book_cursor_get_total (cursor), ==, 6);
	g_assert_cmpint (e_data_book_cursor_get_position (cursor), ==, 3);

	/* Remove */
	uids[0] = "custom-3";
	success = backend_sync_class->remove_contacts_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		(const gchar * const *) uids, E_BOOK_OPERATION_FLAG_NONE, &removed_uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (g_slist_length (removed_uids), ==, 1);
	g_assert_cmpstr (removed_uids->data, ==, uids[0]);
	g_slist_free_full (removed_uids, g_free);
	removed_uids = NULL;

	g_assert_cmpint (e_data_book_cursor_get_total (cursor), ==, 5);
	g_assert_cmpint (e_data_book_cursor_get_position (cursor), ==, 2);

	/* Free the cursor */
	success = backend_class->impl_delete_cursor (E_BOOK_BACKEND (meta_backend), cursor, &error);
	g_assert_no_error (error);
	g_assert_true (success);
}

static void
test_contains_email (EBookMetaBackend *meta_backend)
{
	EBookBackendSyncClass *backend_sync_class;
	gboolean success;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_sync_class = E_BOOK_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_sync_class != NULL);
	g_return_if_fail (backend_sync_class->contains_email_sync != NULL);

	success = backend_sync_class->contains_email_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		"bobby@brown.com", NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	success = backend_sync_class->contains_email_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		"\"Bobby\" <bobby@brown.org>", NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	success = backend_sync_class->contains_email_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		"\"Unknown\" <unknown@no.where>", NULL, &error);
	g_assert_no_error (error);
	g_assert_true (!success);

	success = backend_sync_class->contains_email_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		"\"Unknown\" <unknown@no.where>, \"Bobby\" <bobby@brown.org>", NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	success = backend_sync_class->contains_email_sync (E_BOOK_BACKEND_SYNC (meta_backend),
		"", NULL, &error);
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_CONSTRAINT);
	g_assert_true (!success);
	g_clear_error (&error);
}

typedef void (* TestWithMainLoopFunc) (EBookMetaBackend *meta_backend);

typedef struct _MainLoopThreadData {
	TestWithMainLoopFunc func;
	EBookMetaBackend *meta_backend;
	GMainLoop *main_loop;
} MainLoopThreadData;

static gpointer
test_with_main_loop_thread (gpointer user_data)
{
	MainLoopThreadData *mlt = user_data;

	g_assert_nonnull (mlt);
	g_assert_nonnull (mlt->func);
	g_assert_nonnull (mlt->meta_backend);

	mlt->func (mlt->meta_backend);

	g_main_loop_quit (mlt->main_loop);

	return NULL;
}

static gboolean
quit_test_with_mainloop_cb (gpointer user_data)
{
	GMainLoop *main_loop = user_data;

	g_assert_nonnull (main_loop);

	g_main_loop_quit (main_loop);

	return FALSE;
}

static gboolean
test_with_mainloop_run_thread_idle (gpointer user_data)
{
	GThread *thread;

	g_assert_nonnull (user_data);

	thread = g_thread_new (NULL, test_with_main_loop_thread, user_data);
	g_thread_unref (thread);

	return FALSE;
}

static void
test_with_main_loop (EBookCache *book_cache,
		     TestWithMainLoopFunc func)
{
	MainLoopThreadData mlt;
	EBookMetaBackend *meta_backend;
	guint timeout_id;

	g_assert_nonnull (book_cache);
	g_assert_nonnull (func);

	meta_backend = e_book_meta_backend_test_new (book_cache);
	g_assert_nonnull (meta_backend);

	mlt.func = func;
	mlt.meta_backend = meta_backend;
	mlt.main_loop = g_main_loop_new (NULL, FALSE);

	g_idle_add (test_with_mainloop_run_thread_idle, &mlt);
	timeout_id = g_timeout_add_seconds (10, quit_test_with_mainloop_cb, mlt.main_loop);

	g_main_loop_run (mlt.main_loop);

	g_source_remove (timeout_id);
	g_main_loop_unref (mlt.main_loop);
	g_clear_object (&mlt.meta_backend);
}

#define main_loop_wrapper(_func) \
static void \
_func ## _tcu (TCUFixture *fixture, \
	       gconstpointer user_data) \
{ \
	test_with_main_loop (fixture->book_cache, _func); \
}

main_loop_wrapper (test_create_contacts)
main_loop_wrapper (test_modify_contacts)
main_loop_wrapper (test_remove_contacts)
main_loop_wrapper (test_get_contact)
main_loop_wrapper (test_get_contact_list)
main_loop_wrapper (test_get_contact_list_uids)
main_loop_wrapper (test_refresh)
main_loop_wrapper (test_cursor)
main_loop_wrapper (test_contains_email)

#undef main_loop_wrapper

gint
main (gint argc,
      gchar **argv)
{
	ETestServerClosure tsclosure = {
		E_TEST_SERVER_NONE,
		NULL, /* Source customization function */
		0,    /* Calendar Type */
		TRUE, /* Keep the working sandbox after the test, don't remove it */
		NULL, /* Destroy Notify function */
	};
	ETestServerFixture tsfixture = { 0 };
	TCUClosure closure = { 0 };
	gint res;

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	tcu_read_args (argc, argv);

	/* Ensure that the client and server get the same locale */
	g_assert_true (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

	e_test_server_utils_prepare_run (argc, argv, 0);
	e_test_server_utils_setup (&tsfixture, &tsclosure);

	glob_registry = tsfixture.registry;
	g_assert_nonnull (glob_registry);

	g_test_add ("/EBookMetaBackend/Photos", TCUFixture, &closure,
		tcu_fixture_setup, test_photos, tcu_fixture_teardown);
	g_test_add ("/EBookMetaBackend/EmptyCache", TCUFixture, &closure,
		tcu_fixture_setup, test_empty_cache, tcu_fixture_teardown);
	g_test_add ("/EBookMetaBackend/CreateContacts", TCUFixture, &closure,
		tcu_fixture_setup, test_create_contacts_tcu, tcu_fixture_teardown);
	g_test_add ("/EBookMetaBackend/ModifyContacts", TCUFixture, &closure,
		tcu_fixture_setup, test_modify_contacts_tcu, tcu_fixture_teardown);
	g_test_add ("/EBookMetaBackend/RemoveContacts", TCUFixture, &closure,
		tcu_fixture_setup, test_remove_contacts_tcu, tcu_fixture_teardown);
	g_test_add ("/EBookMetaBackend/GetContact", TCUFixture, &closure,
		tcu_fixture_setup, test_get_contact_tcu, tcu_fixture_teardown);
	g_test_add ("/EBookMetaBackend/GetContactList", TCUFixture, &closure,
		tcu_fixture_setup, test_get_contact_list_tcu, tcu_fixture_teardown);
	g_test_add ("/EBookMetaBackend/GetContactListUids", TCUFixture, &closure,
		tcu_fixture_setup, test_get_contact_list_uids_tcu, tcu_fixture_teardown);
	g_test_add ("/EBookMetaBackend/Refresh", TCUFixture, &closure,
		tcu_fixture_setup, test_refresh_tcu, tcu_fixture_teardown);
	g_test_add ("/EBookMetaBackend/Cursor", TCUFixture, &closure,
		tcu_fixture_setup, test_cursor_tcu, tcu_fixture_teardown);
	g_test_add ("/EBookMetaBackend/ContainsEmail", TCUFixture, &closure,
		tcu_fixture_setup, test_contains_email_tcu, tcu_fixture_teardown);

	res = g_test_run ();

	e_test_server_utils_teardown (&tsfixture, &tsclosure);
	e_test_server_utils_finish_run ();

	return res;
}
