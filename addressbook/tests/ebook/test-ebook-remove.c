/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

static GMainLoop *loop;

static EBook*
create_test_addressbook (char **uri)
{
        EBook *book;
	GError *error = NULL;
	gchar *file_template;

	file_template = g_build_filename (g_get_tmp_dir (),
					  "change-test-XXXXXX/",
					  NULL);
	g_mkstemp (file_template);

	*uri = g_filename_to_uri (file_template, NULL, &error);
	if (!*uri) {
		printf ("failed to convert %s to an URI: %s\n",
			file_template, error->message);
		exit (1);
	}
	g_free (file_template);

	/* create a temp addressbook in /tmp */
	printf ("loading addressbook\n");
	book = e_book_new_from_uri (*uri, &error);
	if (!book) {
		printf ("failed to create addressbook: `%s': %s\n",
			*uri, error->message);
		exit(1);
	}

        return book;
}

static void
remove_cb (EBook *book, EBookStatus status, gpointer closure)
{
        if (status != E_BOOK_ERROR_OK) {
                printf ("failed to asynchronously remove the book: status %d\n",
                                status);
                exit (1);
        }

        printf ("successfully asynchronously removed the temporary addressbook\n");
        g_main_loop_quit (loop);
}

gint
main (gint argc, gchar **argv)
{
	EBook *book;
        char *uri = NULL;
	GError *error = NULL;

	g_type_init ();

        /* Sync version */
        book = create_test_addressbook (&uri);

	if (!e_book_open (book, FALSE, &error)) {
		printf ("failed to open addressbook: `%s': %s\n",
			uri, error->message);
		exit(1);
	}

	if (!e_book_remove (book, &error)) {
		printf ("failed to remove book; %s\n", error->message);
		exit(1);
	}
	printf ("successfully removed the temporary addressbook\n");

	g_object_unref (book);

        /* Async version */
        book = create_test_addressbook (&uri);

	if (!e_book_open (book, FALSE, &error)) {
		printf ("failed to open addressbook: `%s': %s\n",
			uri, error->message);
		exit(1);
	}

	if (e_book_async_remove (book, remove_cb, NULL)) {
		printf ("failed to set up book removal\n");
		exit(1);
	}

        loop = g_main_loop_new (NULL, TRUE);
        g_main_loop_run (loop);

	return 0;
}
