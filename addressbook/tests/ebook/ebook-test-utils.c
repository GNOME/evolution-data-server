/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

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
                (*closure->cb) (closure->user_data);
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
                (*closure->cb) (closure->user_data);
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
                (*closure->cb) (closure->user_data);
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
