/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libgnome/gnome-init.h>
#include <bonobo/bonobo-main.h>
#include <stdlib.h>
#include <libebook/e-book.h>

static void
print_contact (EContact *contact)
{
	GList *emails, *e;

	printf ("Contact: %s\n", (char*)e_contact_get_const (contact, E_CONTACT_FILE_AS));
	printf ("UID: %s\n", (char*)e_contact_get_const (contact, E_CONTACT_UID));
	printf ("Email addresses:\n");

	emails = e_contact_get (contact, E_CONTACT_EMAIL);
	for (e = emails; e; e = e->next) {
		printf ("\t%s\n",  (char*)e->data);
	}
	g_list_foreach (emails, (GFunc)g_free, NULL);
	g_list_free (emails);

	printf ("\n");
}

static void
contacts_added (EBookView *book_view, const GList *contacts)
{
  GList *l;

  for (l = (GList*)contacts; l; l = l->next) {
    print_contact (l->data);
  }
}

static void
contacts_removed (EBookView *book_view, const GList *ids)
{
  GList *l;

  for (l = (GList*)ids; l; l = l->next) {
    printf ("Removed contact: %s\n", (char*)l->data);
  }  
}

static void
sequence_complete (EBookView *book_view, EBookViewStatus status)
{
  printf ("sequence_complete (status == %d)\n", status);
}

int
main (int argc, char **argv)
{
	EBook *book;
	gboolean status;
	EBookQuery *query;
	EBookView *view;

	gnome_program_init("test-ebook", "0.0", LIBGNOME_MODULE, argc, argv, NULL);

	if (bonobo_init (&argc, argv) == FALSE)
		g_error ("Could not initialize Bonobo");

	/*
	** the actual ebook foo
	*/

	printf ("loading addressbook\n");
	book = e_book_new_system_addressbook (NULL);
	if (book == NULL) {
		printf ("failed to create local addressbook\n");
		exit(0);
	}

	status = e_book_open (book, FALSE, NULL);
	if (status == FALSE) {
		printf ("failed to open local addressbook\n");
		exit(0);
	}

	printf ("populating view\n");
	query = e_book_query_any_field_contains ("");

	status = e_book_get_book_view (book, query, NULL, -1, &view, NULL);

	e_book_query_unref (query);

	g_signal_connect (view, "contacts_added", G_CALLBACK (contacts_added), NULL);
	g_signal_connect (view, "contacts_removed", G_CALLBACK (contacts_removed), NULL);
	g_signal_connect (view, "sequence_complete", G_CALLBACK (sequence_complete), NULL);

	e_book_view_start (view);

	bonobo_main ();

	return 0;
}
