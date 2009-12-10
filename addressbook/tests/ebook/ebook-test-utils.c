/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

gboolean
ebook_test_utils_callback_quit (gpointer user_data)
{
        EBookTestClosure *closure = user_data;
        g_main_loop_quit ((GMainLoop*) closure->user_data);

        return FALSE;
}

char*
ebook_test_utils_new_vcard_from_test_case (const char *case_name)
{
        char *filename;
        char *case_filename;
        GFile* file;
        GError *error = NULL;
        char *vcard;

        case_filename = g_strdup_printf ("%s.vcf", case_name);
        filename = g_build_filename (EBOOK_TEST_UTILS_DATA_DIR, EBOOK_TEST_UTILS_VCARDS_DIR, case_filename, NULL);
        file = g_file_new_for_path (filename);
        if (!g_file_load_contents (file, NULL, &vcard, NULL, NULL, &error)) {
                g_warning ("failed to read test contact file '%s': %s",
                                filename, error->message);
                exit(1);
        }

        g_free (case_filename);
        g_free (filename);
        g_object_unref (file);

        return vcard;
}

char*
ebook_test_utils_book_add_contact_from_test_case_verify (EBook       *book,
                                                         const char  *case_name,
                                                         EContact   **contact)
{
        char *vcard;
        EContact *contact_orig;
        EContact *contact_final;
        char *uid;

        vcard = ebook_test_utils_new_vcard_from_test_case (case_name);
        contact_orig = e_contact_new_from_vcard (vcard);
        uid = g_strdup (ebook_test_utils_book_add_contact (book, contact_orig));
        contact_final = ebook_test_utils_book_get_contact (book, uid);

        /* verify the contact was added "successfully" (not thorough) */
        g_assert (ebook_test_utils_contacts_are_equal_shallow (contact_orig, contact_final));

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
        const char *uid_a, *uid_b;

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

const char*
ebook_test_utils_book_add_contact (EBook    *book,
                                   EContact *contact)
{
        GError *error = NULL;

        if (!e_book_add_contact (book, contact, &error)) {
                const char *uri;

                uri = e_book_get_uri (book);
                g_warning ("failed to add contact to addressbook: `%s': %s",
                                uri, error->message);
                exit(1);
        }

        return e_contact_get_const (contact, E_CONTACT_UID);
}

static void
add_contact_cb (EBook            *book,
                EBookStatus       status,
                const char       *uid,
                EBookTestClosure *closure)
{
        if (status != E_BOOK_ERROR_OK) {
                g_warning ("failed to asynchronously add the contact '%s': "
                                "status %d", uid, status);
                exit (1);
        }

        g_print ("successfully asynchronously added the contact "
                        "addressbook\n");
        if (closure) {
                (*closure->cb) (closure);
                g_free (closure);
        }
}

void
ebook_test_utils_book_async_add_contact (EBook       *book,
                                         EContact    *contact,
                                         GSourceFunc  callback,
                                         gpointer     user_data)
{
        EBookTestClosure *closure;

        closure = g_new0 (EBookTestClosure, 1);
        closure->cb = callback;
        closure->user_data = user_data;
        if (e_book_async_add_contact (book, contact,
                                (EBookIdCallback) add_contact_cb, closure)) {
                g_warning ("failed to set up contact add");
                exit(1);
        }
}

void
ebook_test_utils_book_commit_contact (EBook    *book,
                                      EContact *contact)
{
        GError *error = NULL;

        if (!e_book_commit_contact (book, contact, &error)) {
                const char *uid;
                const char *uri;

                uid = (const char*) e_contact_get_const (contact, E_CONTACT_UID);
                uri = e_book_get_uri (book);
                g_warning ("failed to commit changes to contact '%s' to addressbook: `%s': %s",
                                uid, uri, error->message);
                exit(1);
        }
}

static void
commit_contact_cb (EBook            *book,
                   EBookStatus       status,
                   EBookTestClosure *closure)
{
        if (status != E_BOOK_ERROR_OK) {
                g_warning ("failed to asynchronously commit the contact: "
                                "status %d", status);
                exit (1);
        }

        g_print ("successfully asynchronously committed the contact to the "
                        "addressbook\n");
        if (closure) {
                (*closure->cb) (closure);
                g_free (closure);
        }
}

void
ebook_test_utils_book_async_commit_contact (EBook       *book,
                                            EContact    *contact,
                                            GSourceFunc  callback,
                                            gpointer     user_data)
{
        EBookTestClosure *closure;

        closure = g_new0 (EBookTestClosure, 1);
        closure->cb = callback;
        closure->user_data = user_data;
        if (e_book_async_commit_contact (book, contact,
                                (EBookCallback) commit_contact_cb, closure)) {
                g_warning ("failed to set up contact commit");
                exit(1);
        }
}

EContact*
ebook_test_utils_book_get_contact (EBook      *book,
                                   const char *uid)
{
        EContact *contact = NULL;
        GError *error = NULL;

        if (!e_book_get_contact (book, uid, &contact, &error)) {
                const char *uri;

                uri = e_book_get_uri (book);
                g_warning ("failed to get contact '%s' in addressbook: `%s': "
                                "%s", uid, uri, error->message);
                exit(1);
        }

        return contact;
}

static void
get_contact_cb (EBook            *book,
                EBookStatus       status,
                EContact         *contact,
                EBookTestClosure *closure)
{
        const char *uid;

        if (status != E_BOOK_ERROR_OK) {
                g_warning ("failed to asynchronously get the contact: "
                                "status %d", status);
                exit (1);
        }                       

        uid = e_contact_get_const (contact, E_CONTACT_UID);
        g_print ("successfully asynchronously retrieved the contact '%s'\n",
                        uid);

        if (closure) {
                (*closure->cb) (closure);
                g_free (closure);
        }
}

void   
ebook_test_utils_book_async_get_contact (EBook       *book,
                                         const char  *uid,
                                         GSourceFunc  callback,
                                         gpointer     user_data)
{       
        EBookTestClosure *closure;
        
        closure = g_new0 (EBookTestClosure, 1);
        closure->cb = callback;
        closure->user_data = user_data;
        if (e_book_async_get_contact (book, uid,
                                (EBookContactCallback) get_contact_cb,
                                closure)) {
                g_warning ("failed to set up async getContact");
                exit(1);
        }
}

GList*
ebook_test_utils_book_get_required_fields (EBook *book)
{
        GList *fields = NULL;
        GError *error = NULL;

        if (!e_book_get_required_fields (book, &fields, &error)) {
                const char *uri;

                uri = e_book_get_uri (book);
                g_warning ("failed to get required fields for addressbook "
                                "`%s': %s", uri, error->message);
                exit(1);
        }

        return fields;
}

static void
get_required_fields_cb (EBook            *book,
                        EBookStatus       status,
                        EList            *fields,
                        EBookTestClosure *closure)
{
        if (status != E_BOOK_ERROR_OK) {
                g_warning ("failed to asynchronously get the contact: "
                                "status %d", status);
                exit (1);
        }

        closure->list = fields;

        g_print ("successfully asynchronously retrieved the required fields\n");

        if (closure) {
                (*closure->cb) (closure);
                g_free (closure);
        }
}

void
ebook_test_utils_book_async_get_required_fields (EBook       *book,
                                                 GSourceFunc  callback,
                                                 gpointer     user_data)
{
        EBookTestClosure *closure;

        closure = g_new0 (EBookTestClosure, 1);
        closure->cb = callback;
        closure->user_data = user_data;
        if (e_book_async_get_required_fields (book,
                                (EBookEListCallback) get_required_fields_cb,
                                closure)) {
                g_warning ("failed to set up async getRequiredFields");
                exit(1);
        }
}

const char*
ebook_test_utils_book_get_static_capabilities (EBook *book)
{
        GError *error = NULL;
        const char *caps;

        if (!(caps = e_book_get_static_capabilities (book, &error))) {
                const char *uri;

                uri = e_book_get_uri (book);
                g_warning ("failed to get capabilities for addressbook: `%s': "
                                "%s", uri, error->message);
                exit(1);
        }

        return caps;
}

GList*
ebook_test_utils_book_get_supported_auth_methods (EBook *book)
{
        GList *fields = NULL;
        GError *error = NULL;

        if (!e_book_get_supported_auth_methods (book, &fields, &error)) {
                const char *uri;

                uri = e_book_get_uri (book);
                g_warning ("failed to get supported auth methods for "
                                "addressbook `%s': %s", uri, error->message);
                exit(1);
        }

        return fields;
}

static void
get_supported_auth_methods_cb (EBook            *book,
                               EBookStatus       status,
                               EList            *methods,
                               EBookTestClosure *closure)
{
        if (status != E_BOOK_ERROR_OK) {
                g_warning ("failed to asynchronously get the supported auth "
                                "methods: status %d", status);
                exit (1);
        }

        closure->list = methods;

        g_print ("successfully asynchronously retrieved the supported auth "
                        "methods\n");

        if (closure) {
                (*closure->cb) (closure);
                g_free (closure);
        }
}

void
ebook_test_utils_book_async_get_supported_auth_methods (EBook       *book,
                                                        GSourceFunc  callback,
                                                        gpointer     user_data)
{
        EBookTestClosure *closure;

        closure = g_new0 (EBookTestClosure, 1);
        closure->cb = callback;
        closure->user_data = user_data;
        if (e_book_async_get_supported_auth_methods (book,
                                (EBookEListCallback) get_supported_auth_methods_cb,
                                closure)) {
                g_warning ("failed to set up async getSupportedAuthMethods");
                exit(1);
        }
}

GList*
ebook_test_utils_book_get_supported_fields (EBook *book)
{
        GList *fields = NULL;
        GError *error = NULL;

        if (!e_book_get_supported_fields (book, &fields, &error)) {
                const char *uri;

                uri = e_book_get_uri (book);
                g_warning ("failed to get supported fields for addressbook "
                                "`%s': %s", uri, error->message);
                exit(1);
        }

        return fields;
}

static void
get_supported_fields_cb (EBook            *book,
                        EBookStatus       status,
                        EList            *fields,
                        EBookTestClosure *closure)
{
        if (status != E_BOOK_ERROR_OK) {
                g_warning ("failed to asynchronously get the contact: "
                                "status %d", status);
                exit (1);
        }

        closure->list = fields;

        g_print ("successfully asynchronously retrieved the supported fields\n");

        if (closure) {
                (*closure->cb) (closure);
                g_free (closure);
        }
}

void
ebook_test_utils_book_async_get_supported_fields (EBook       *book,
                                                 GSourceFunc  callback,
                                                 gpointer     user_data)
{
        EBookTestClosure *closure;

        closure = g_new0 (EBookTestClosure, 1);
        closure->cb = callback;
        closure->user_data = user_data;
        if (e_book_async_get_supported_fields (book,
                                (EBookEListCallback) get_supported_fields_cb,
                                closure)) {
                g_warning ("failed to set up async getSupportedFields");
                exit(1);
        }
}

void
ebook_test_utils_book_remove_contact (EBook      *book,
                                      const char *uid)
{
        GError *error = NULL;

        if (!e_book_remove_contact (book, uid, &error)) {
                const char *uri;

                uri = e_book_get_uri (book);
                g_warning ("failed to remove contact '%s' from addressbook: `%s': %s",
                                uid, uri, error->message);
                exit(1);
        }
}

static void
remove_contact_cb (EBook            *book,
                   EBookStatus       status,
                   EBookTestClosure *closure)
{
        if (status != E_BOOK_ERROR_OK) {
                g_warning ("failed to asynchronously remove the contact: "
                                "status %d", status);
                exit (1);
        }

        g_print ("successfully asynchronously removed the contact\n");

        if (closure) {
                (*closure->cb) (closure);
                g_free (closure);
        }
}

void
ebook_test_utils_book_async_remove_contact (EBook       *book,
                                            EContact    *contact,
                                            GSourceFunc  callback,
                                            gpointer     user_data)
{
        EBookTestClosure *closure;

        closure = g_new0 (EBookTestClosure, 1);
        closure->cb = callback;
        closure->user_data = user_data;
        if (e_book_async_remove_contact (book, contact,
                                (EBookCallback) remove_contact_cb,
                                closure)) {
                g_warning ("failed to set up async removeContacts (for a single contact)");
                exit(1);
        }
}

static void
remove_contact_by_id_cb (EBook            *book,
                         EBookStatus       status,
                         EBookTestClosure *closure)
{
        if (status != E_BOOK_ERROR_OK) {
                g_warning ("failed to asynchronously remove the contact by id: "
                                "status %d", status);
                exit (1);
        }

        g_print ("successfully asynchronously removed the contact by id\n");

        if (closure) {
                (*closure->cb) (closure);
                g_free (closure);
        }
}

void
ebook_test_utils_book_async_remove_contact_by_id (EBook       *book,
                                                  const char  *uid,
                                                  GSourceFunc  callback,
                                                  gpointer     user_data)
{
        EBookTestClosure *closure;

        closure = g_new0 (EBookTestClosure, 1);
        closure->cb = callback;
        closure->user_data = user_data;
        if (e_book_async_remove_contact_by_id (book, uid,
                                (EBookCallback) remove_contact_by_id_cb,
                                closure)) {
                g_warning ("failed to set up async removeContacts (by id)");
                exit(1);
        }
}

void
ebook_test_utils_book_remove_contacts (EBook *book,
                                       GList *ids)
{
        GError *error = NULL;

        if (!e_book_remove_contacts (book, ids, &error)) {
                const char *uri;

                uri = e_book_get_uri (book);
                g_warning ("failed to remove contacts from addressbook: `%s': %s",
                                uri, error->message);
                exit(1);
        }
}

static void
remove_contacts_cb (EBook            *book,
                    EBookStatus       status,
                    EBookTestClosure *closure)
{
        if (status != E_BOOK_ERROR_OK) {
                g_warning ("failed to asynchronously remove the contacts: "
                                "status %d", status);
                exit (1);
        }

        g_print ("successfully asynchronously removed the contacts\n");

        if (closure) {
                (*closure->cb) (closure);
                g_free (closure);
        }
}

void
ebook_test_utils_book_async_remove_contacts (EBook       *book,
                                             GList       *uids,
                                             GSourceFunc  callback,
                                             gpointer     user_data)
{
        EBookTestClosure *closure;

        closure = g_new0 (EBookTestClosure, 1);
        closure->cb = callback;
        closure->user_data = user_data;
        if (e_book_async_remove_contacts (book, uids,
                                (EBookCallback) remove_contacts_cb,
                                closure)) {
                g_warning ("failed to set up async removeContacts");
                exit(1);
        }
}

EBook*
ebook_test_utils_book_new_temp (char **uri)
{
        EBook *book;
	GError *error = NULL;
	gchar *file_template;
        char *uri_result;

        file_template = g_build_filename (g_get_tmp_dir (),
                        "ebook-test-XXXXXX/", NULL);
	g_mkstemp (file_template);

	uri_result = g_filename_to_uri (file_template, NULL, &error);
	if (!uri_result) {
                g_warning ("failed to convert %s to an URI: %s", file_template,
                                error->message);
		exit (1);
	}
	g_free (file_template);

	/* create a temp addressbook in /tmp */
	g_print ("loading addressbook\n");
	book = e_book_new_from_uri (uri_result, &error);
	if (!book) {
                g_warning ("failed to create addressbook: `%s': %s", *uri,
                                error->message);
		exit(1);
	}

        if (uri)
                *uri = g_strdup (uri_result);

        g_free (uri_result);

        return book;
}

void
ebook_test_utils_book_open (EBook    *book,
                            gboolean  only_if_exists)
{
        GError *error = NULL;

        if (!e_book_open (book, only_if_exists, &error)) {
                const char *uri;

                uri = e_book_get_uri (book);

                g_warning ("failed to open addressbook: `%s': %s", uri,
                                error->message);
                exit(1);
        }
}

void
ebook_test_utils_book_remove (EBook *book)
{
        GError *error = NULL;

        if (!e_book_remove (book, &error)) {
                g_warning ("failed to remove book; %s\n", error->message);
                exit(1);
        }       
        g_print ("successfully removed the temporary addressbook\n");
        
        g_object_unref (book);
}

static void
remove_cb (EBook *book, EBookStatus status, EBookTestClosure *closure)
{
        if (status != E_BOOK_ERROR_OK) {
                g_warning ("failed to asynchronously remove the book: "
                                "status %d", status);
                exit (1);
        }                       
                
        g_print ("successfully asynchronously removed the temporary "
                        "addressbook\n");
        if (closure)
                (*closure->cb) (closure);
}

void   
ebook_test_utils_book_async_remove (EBook       *book,
                                    GSourceFunc  callback,
                                    gpointer     user_data)
{       
        EBookTestClosure *closure;
        
        closure = g_new0 (EBookTestClosure, 1);
        closure->cb = callback;
        closure->user_data = user_data;
        if (e_book_async_remove (book, (EBookCallback) remove_cb, closure)) {
                g_warning ("failed to set up book removal");
                exit(1);
        }
}
