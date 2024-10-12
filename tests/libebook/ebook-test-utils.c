/*
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
 */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "ebook-test-utils.h"

void
test_print (const gchar *format,
            ...)
{
	va_list args;
	const gchar *debug_string;
	static gboolean debug_set = FALSE;
	static gboolean debug = FALSE;

	if (!debug_set) {
		debug_string = g_getenv ("EDS_TEST_DEBUG");
		if (debug_string) {
			debug = (g_ascii_strtoll (debug_string, NULL, 10) >= 1);
		}
		debug_set = TRUE;
	}

	if (debug) {
		va_start (args, format);
		vprintf (format, args);
		va_end (args);
	}
}

gboolean
ebook_test_utils_callback_quit (gpointer user_data)
{
	EBookTestClosure *closure = user_data;
	g_main_loop_quit ((GMainLoop *) closure->user_data);

	return FALSE;
}

static const gchar *args_data_dir = NULL;

void
ebook_test_utils_read_args (gint argc,
			    gchar **argv)
{
	gint ii;

	for (ii = 0; ii < argc; ii++) {
		if (g_strcmp0 (argv[ii], "--data-dir") == 0) {
			if (ii + 1 < argc)
				args_data_dir = argv[ii + 1];
			break;
		}
	}
}

gchar *
ebook_test_utils_new_vcard_from_test_case (const gchar *case_name)
{
	gchar *filename;
	gchar *case_filename;
	GFile * file;
	GError *error = NULL;
	gchar *vcard;

	case_filename = g_strdup_printf ("%s.vcf", case_name);

	/* In the case of installed tests, they run in ${pkglibexecdir}/installed-tests
	 * and the vcards are installed in ${pkglibexecdir}/installed-tests/vcards
	 */
	if (g_getenv ("TEST_INSTALLED_SERVICES") != NULL) {
		filename = g_build_filename (INSTALLED_TEST_DIR, "vcards", case_filename, NULL);
	} else {
		if (!args_data_dir) {
			g_warning ("Data directory not set, pass it with `--data-dir PATH`");
			exit(1);
		}

		filename = g_build_filename (args_data_dir, case_filename, NULL);
	}

	file = g_file_new_for_path (filename);
	if (!g_file_load_contents (file, NULL, &vcard, NULL, NULL, &error)) {
		g_warning (
			"failed to read test contact file '%s': %s",
			filename, error->message);
		exit (1);
	}

	g_free (case_filename);
	g_free (filename);
	g_object_unref (file);

	return vcard;
}

gchar *
ebook_test_utils_book_add_contact_from_test_case_verify (EBook *book,
                                                         const gchar *case_name,
                                                         EContact **contact)
{
	gchar *vcard;
	EContact *contact_orig;
	EContact *contact_final;
	gchar *uid;

	vcard = ebook_test_utils_new_vcard_from_test_case (case_name);
	contact_orig = e_contact_new_from_vcard (vcard);
	uid = g_strdup (ebook_test_utils_book_add_contact (book, contact_orig));
	contact_final = ebook_test_utils_book_get_contact (book, uid);

	/* verify the contact was added "successfully" (not thorough) */
	g_assert_true (ebook_test_utils_contacts_are_equal_shallow (contact_orig, contact_final));

	if (contact)
		*contact = g_object_ref (contact_final);

	return uid;
}

/* This is not a thorough comparison (which is difficult, assuming we give the
 * back-ends leniency in implementation) and is best suited for simple tests */
gboolean
ebook_test_utils_contacts_are_equal_shallow (EContact *a,
                                             EContact *b)

{
	const gchar *uid_a, *uid_b;

	/* Avoid warnings if one or more are NULL, to make this function
	 * "NULL-friendly" */
	if (!a && !b)
		return TRUE;
	if (!E_IS_CONTACT (a) || !E_IS_CONTACT (b))
		return FALSE;

	uid_a = e_contact_get_const (a, E_CONTACT_UID);
	uid_b = e_contact_get_const (b, E_CONTACT_UID);

	return g_strcmp0 (uid_a, uid_b) == 0;
}

const gchar *
ebook_test_utils_book_add_contact (EBook *book,
                                   EContact *contact)
{
	GError *error = NULL;

	if (!e_book_add_contact (book, contact, &error)) {
		ESource *source;
		const gchar *name;

		source = e_book_get_source (book);
		name = e_source_get_display_name (source);
		g_warning (
			"failed to add contact to addressbook: `%s': %s",
			name, error->message);
		exit (1);
	}

	return e_contact_get_const (contact, E_CONTACT_UID);
}

static void
add_contact_cb (EBook *book,
                const GError *error,
                const gchar *uid,
                EBookTestClosure *closure)
{
	if (error) {
		g_warning (
			"failed to asynchronously add the contact '%s': "
			"status %d (%s)", uid, error->code, error->message);
		exit (1);
	}

	test_print ("successfully asynchronously added the contact "
			"addressbook\n");
	if (closure->cb)
		(*closure->cb) (closure);

	g_free (closure);
}

void
ebook_test_utils_book_async_add_contact (EBook *book,
                                         EContact *contact,
                                         GSourceFunc callback,
                                         gpointer user_data)
{
	EBookTestClosure *closure;

	closure = g_new0 (EBookTestClosure, 1);
	closure->cb = callback;
	closure->user_data = user_data;
	if (!e_book_add_contact_async (book, contact,
				(EBookIdAsyncCallback) add_contact_cb, closure)) {
		g_warning ("failed to set up contact add");
		exit (1);
	}
}

void
ebook_test_utils_book_commit_contact (EBook *book,
                                      EContact *contact)
{
	GError *error = NULL;

	if (!e_book_commit_contact (book, contact, &error)) {
		ESource *source;
		const gchar *name;
		const gchar *uid;

		uid = (const gchar *) e_contact_get_const (contact, E_CONTACT_UID);

		source = e_book_get_source (book);
		name = e_source_get_display_name (source);
		g_warning (
			"failed to commit changes to contact '%s' to "
			"addressbook: `%s': %s", uid, name, error->message);
		exit (1);
	}
}

static void
commit_contact_cb (EBook *book,
                   const GError *error,
                   EBookTestClosure *closure)
{
	if (error) {
		g_warning (
			"failed to asynchronously commit the contact: "
			"status %d (%s)", error->code, error->message);
		exit (1);
	}

	test_print ("successfully asynchronously committed the contact to the "
			"addressbook\n");
	if (closure->cb)
		(*closure->cb) (closure);

	g_free (closure);
}

void
ebook_test_utils_book_async_commit_contact (EBook *book,
                                            EContact *contact,
                                            GSourceFunc callback,
                                            gpointer user_data)
{
	EBookTestClosure *closure;

	closure = g_new0 (EBookTestClosure, 1);
	closure->cb = callback;
	closure->user_data = user_data;
	if (!e_book_commit_contact_async (book, contact,
				(EBookAsyncCallback) commit_contact_cb, closure)) {
		g_warning ("failed to set up contact commit");
		exit (1);
	}
}

EContact *
ebook_test_utils_book_get_contact (EBook *book,
                                   const gchar *uid)
{
	EContact *contact = NULL;
	GError *error = NULL;

	if (!e_book_get_contact (book, uid, &contact, &error)) {
		ESource *source;
		const gchar *name;

		source = e_book_get_source (book);
		name = e_source_get_display_name (source);
		g_warning (
			"failed to get contact '%s' in addressbook: `%s': "
			"%s", uid, name, error->message);
		exit (1);
	}

	return contact;
}

static void
get_contact_cb (EBook *book,
                const GError *error,
                EContact *contact,
                EBookTestClosure *closure)
{
	const gchar *uid;

	if (error) {
		g_warning (
			"failed to asynchronously get the contact: "
			"status %d (%s)", error->code, error->message);
		exit (1);
	}

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	test_print (
		"successfully asynchronously retrieved the contact '%s'\n",
			uid);

	if (closure->cb)
		(*closure->cb) (closure);

	g_free (closure);
}

void
ebook_test_utils_book_async_get_contact (EBook *book,
                                         const gchar *uid,
                                         GSourceFunc callback,
                                         gpointer user_data)
{
	EBookTestClosure *closure;

	closure = g_new0 (EBookTestClosure, 1);
	closure->cb = callback;
	closure->user_data = user_data;
	if (!e_book_get_contact_async (book, uid,
				(EBookContactAsyncCallback) get_contact_cb,
				closure)) {
		g_warning ("failed to set up async getContact");
		exit (1);
	}
}

GList *
ebook_test_utils_book_get_required_fields (EBook *book)
{
	GList *fields = NULL;
	GError *error = NULL;

	if (!e_book_get_required_fields (book, &fields, &error)) {
		ESource *source;
		const gchar *name;

		source = e_book_get_source (book);
		name = e_source_get_display_name (source);
		g_warning (
			"failed to get required fields for addressbook "
			"`%s': %s", name, error->message);
		exit (1);
	}

	return fields;
}

static void
get_required_fields_cb (EBook *book,
                        const GError *error,
                        EList *fields,
                        EBookTestClosure *closure)
{
	if (error) {
		g_warning (
			"failed to asynchronously get the required fields: "
			"status %d (%s)", error->code, error->message);
		exit (1);
	}

	closure->list = fields;

	test_print ("successfully asynchronously retrieved the required fields\n");

	if (closure->cb)
		(*closure->cb) (closure);

	g_free (closure);
}

void
ebook_test_utils_book_async_get_required_fields (EBook *book,
                                                 GSourceFunc callback,
                                                 gpointer user_data)
{
	EBookTestClosure *closure;

	closure = g_new0 (EBookTestClosure, 1);
	closure->cb = callback;
	closure->user_data = user_data;
	if (!e_book_get_required_fields_async (book,
				(EBookEListAsyncCallback) get_required_fields_cb,
				closure)) {
		g_warning ("failed to set up async getRequiredFields");
		exit (1);
	}
}

const gchar *
ebook_test_utils_book_get_static_capabilities (EBook *book)
{
	GError *error = NULL;
	const gchar *caps;

	if (!(caps = e_book_get_static_capabilities (book, &error))) {
		ESource *source;
		const gchar *name;

		source = e_book_get_source (book);
		name = e_source_get_display_name (source);
		g_warning (
			"failed to get capabilities for addressbook: `%s': "
			"%s", name, error->message);
		exit (1);
	}

	return caps;
}

GList *
ebook_test_utils_book_get_supported_auth_methods (EBook *book)
{
	GList *fields = NULL;
	GError *error = NULL;

	if (!e_book_get_supported_auth_methods (book, &fields, &error)) {
		ESource *source;
		const gchar *name;

		source = e_book_get_source (book);
		name = e_source_get_display_name (source);
		g_warning (
			"failed to get supported auth methods for "
			"addressbook `%s': %s", name, error ? error->message : "Unknown error");
		exit (1);
	}

	return fields;
}

static void
get_supported_auth_methods_cb (EBook *book,
                               const GError *error,
                               EList *methods,
                               EBookTestClosure *closure)
{
	if (error) {
		g_warning (
			"failed to asynchronously get the supported auth "
			"methods: status %d (%s)", error->code, error->message);
		exit (1);
	}

	closure->list = methods;

	test_print ("successfully asynchronously retrieved the supported auth "
			"methods\n");

	if (closure->cb)
		(*closure->cb) (closure);
	g_free (closure);
}

void
ebook_test_utils_book_async_get_supported_auth_methods (EBook *book,
                                                        GSourceFunc callback,
                                                        gpointer user_data)
{
	EBookTestClosure *closure;

	closure = g_new0 (EBookTestClosure, 1);
	closure->cb = callback;
	closure->user_data = user_data;
	if (!e_book_get_supported_auth_methods_async (book,
				(EBookEListAsyncCallback) get_supported_auth_methods_cb,
				closure)) {
		g_warning ("failed to set up async getSupportedAuthMethods");
		exit (1);
	}
}

GList *
ebook_test_utils_book_get_supported_fields (EBook *book)
{
	GList *fields = NULL;
	GError *error = NULL;

	if (!e_book_get_supported_fields (book, &fields, &error)) {
		ESource *source;
		const gchar *name;

		source = e_book_get_source (book);
		name = e_source_get_display_name (source);
		g_warning (
			"failed to get supported fields for addressbook "
			"`%s': %s", name, error->message);
		exit (1);
	}

	return fields;
}

static void
get_supported_fields_cb (EBook *book,
                        const GError *error,
                        EList *fields,
                        EBookTestClosure *closure)
{
	if (error) {
		g_warning (
			"failed to asynchronously get the supported fields: "
			"status %d (%s)", error->code, error->message);
		exit (1);
	}

	closure->list = fields;

	test_print ("successfully asynchronously retrieved the supported fields\n");

	if (closure->cb)
		(*closure->cb) (closure);
	g_free (closure);
}

void
ebook_test_utils_book_async_get_supported_fields (EBook *book,
                                                 GSourceFunc callback,
                                                 gpointer user_data)
{
	EBookTestClosure *closure;

	closure = g_new0 (EBookTestClosure, 1);
	closure->cb = callback;
	closure->user_data = user_data;
	if (!e_book_get_supported_fields_async (book,
				(EBookEListAsyncCallback) get_supported_fields_cb,
				closure)) {
		g_warning ("failed to set up async getSupportedFields");
		exit (1);
	}
}

void
ebook_test_utils_book_remove_contact (EBook *book,
                                      const gchar *uid)
{
	GError *error = NULL;

	if (!e_book_remove_contact (book, uid, &error)) {
		ESource *source;
		const gchar *name;

		source = e_book_get_source (book);
		name = e_source_get_display_name (source);
		g_warning (
			"failed to remove contact '%s' from addressbook: "
			"`%s': %s", uid, name, error->message);
		exit (1);
	}
}

static void
remove_contact_cb (EBook *book,
                   const GError *error,
                   EBookTestClosure *closure)
{
	if (error) {
		g_warning (
			"failed to asynchronously remove the contact: "
			"status %d (%s)", error->code, error->message);
		exit (1);
	}

	test_print ("successfully asynchronously removed the contact\n");

	if (closure->cb)
		(*closure->cb) (closure);

	g_free (closure);
}

void
ebook_test_utils_book_async_remove_contact (EBook *book,
                                            EContact *contact,
                                            GSourceFunc callback,
                                            gpointer user_data)
{
	EBookTestClosure *closure;

	closure = g_new0 (EBookTestClosure, 1);
	closure->cb = callback;
	closure->user_data = user_data;
	if (!e_book_remove_contact_async (book, contact,
				(EBookAsyncCallback) remove_contact_cb,
				closure)) {
		g_warning (
			"failed to set up async removeContacts "
			"(for a single contact)");
		exit (1);
	}
}

static void
remove_contact_by_id_cb (EBook *book,
                         const GError *error,
                         EBookTestClosure *closure)
{
	if (error) {
		g_warning (
			"failed to asynchronously remove the contact by id: "
			"status %d (%s)", error->code, error->message);
		exit (1);
	}

	test_print ("successfully asynchronously removed the contact by id\n");

	if (closure->cb)
		(*closure->cb) (closure);

	g_free (closure);
}

void
ebook_test_utils_book_async_remove_contact_by_id (EBook *book,
                                                  const gchar *uid,
                                                  GSourceFunc callback,
                                                  gpointer user_data)
{
	EBookTestClosure *closure;

	closure = g_new0 (EBookTestClosure, 1);
	closure->cb = callback;
	closure->user_data = user_data;
	if (!e_book_remove_contact_by_id_async (book, uid,
				(EBookAsyncCallback) remove_contact_by_id_cb,
				closure)) {
		g_warning ("failed to set up async removeContacts (by id)");
		exit (1);
	}
}

void
ebook_test_utils_book_remove_contacts (EBook *book,
                                       GList *ids)
{
	GError *error = NULL;

	if (!e_book_remove_contacts (book, ids, &error)) {
		ESource *source;
		const gchar *name;

		source = e_book_get_source (book);
		name = e_source_get_display_name (source);
		g_warning (
			"failed to remove contacts from addressbook: `%s': %s",
			name, error->message);
		exit (1);
	}
}

static void
remove_contacts_cb (EBook *book,
                    const GError *error,
                    EBookTestClosure *closure)
{
	if (error) {
		g_warning (
			"failed to asynchronously remove the contacts: "
			"status %d (%s)", error->code, error->message);
		exit (1);
	}

	test_print ("successfully asynchronously removed the contacts\n");

	if (closure->cb)
		(*closure->cb) (closure);

	g_free (closure);
}

void
ebook_test_utils_book_async_remove_contacts (EBook *book,
                                             GList *uids,
                                             GSourceFunc callback,
                                             gpointer user_data)
{
	EBookTestClosure *closure;

	closure = g_new0 (EBookTestClosure, 1);
	closure->cb = callback;
	closure->user_data = user_data;
	if (!e_book_remove_contacts_async (book, uids,
				(EBookAsyncCallback) remove_contacts_cb,
				closure)) {
		g_warning ("failed to set up async removeContacts");
		exit (1);
	}
}

void
ebook_test_utils_book_get_book_view (EBook *book,
                                     EBookQuery *query,
                                     EBookView **view)
{
	GError *error = NULL;

	if (!e_book_get_book_view (book, query, NULL, -1, view, &error)) {
		ESource *source;
		const gchar *name;

		source = e_book_get_source (book);
		name = e_source_get_display_name (source);

		g_warning (
			"failed to get view for addressbook: `%s': %s",
			name, error->message);
		exit (1);
	}
}

static void
get_book_view_cb (EBook *book,
                  const GError *error,
                  EBookView *view,
                  EBookTestClosure *closure)
{
	if (error) {
		g_warning (
			"failed to asynchronously get book view for the "
			"book: status %d (%s)", error->code, error->message);
		exit (1);
	}

	closure->view = view;

	test_print ("successfully asynchronously retrieved the book view\n");
	if (closure->cb)
		(*closure->cb) (closure);
}

void
ebook_test_utils_book_async_get_book_view (EBook *book,
                                           EBookQuery *query,
                                           GSourceFunc callback,
                                           gpointer user_data)
{
	EBookTestClosure *closure;

	closure = g_new0 (EBookTestClosure, 1);
	closure->cb = callback;
	closure->user_data = user_data;
	if (!e_book_get_book_view_async (book, query, NULL, -1, (EBookBookViewAsyncCallback) get_book_view_cb, closure)) {
		g_warning ("failed to set up book view retrieval");
		exit (1);
	}
}
