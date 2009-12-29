/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

EBook*
ebook_test_utils_book_new_temp (char **uri)
{
        EBook *book;
	GError *error = NULL;
	gchar *file_template;

        file_template = g_build_filename (g_get_tmp_dir (),
                        "ebook-test-XXXXXX/", NULL);
	g_mkstemp (file_template);

	*uri = g_filename_to_uri (file_template, NULL, &error);
	if (!*uri) {
                g_warning ("failed to convert %s to an URI: %s", file_template,
                                error->message);
		exit (1);
	}
	g_free (file_template);

	/* create a temp addressbook in /tmp */
	g_print ("loading addressbook\n");
	book = e_book_new_from_uri (*uri, &error);
	if (!book) {
                g_warning ("failed to create addressbook: `%s': %s", *uri,
                                error->message);
		exit(1);
	}

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
