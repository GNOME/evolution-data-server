/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

static void
print_contact (EContact *contact)
{
	GList *emails, *e;

	printf ("Contact: %s\n", (gchar *) e_contact_get_const (contact, E_CONTACT_FILE_AS));
	printf ("UID: %s\n", (gchar *) e_contact_get_const (contact, E_CONTACT_UID));
	printf ("Email addresses:\n");

	emails = e_contact_get (contact, E_CONTACT_EMAIL);
	for (e = emails; e; e = e->next) {
		printf ("\t%s\n",  (gchar *) e->data);
	}
	g_list_foreach (emails, (GFunc) g_free, NULL);
	g_list_free (emails);

	printf ("\n");
}

static void
contacts_added (EBookView *book_view,
                const GList *contacts)
{
  GList *l;

  for (l = (GList *) contacts; l; l = l->next) {
    print_contact (l->data);
  }
}

static void
contacts_removed (EBookView *book_view,
                  const GList *ids)
{
  GList *l;

  for (l = (GList *) ids; l; l = l->next) {
    printf ("Removed contact: %s\n", (gchar *) l->data);
  }
}

static void
view_complete (EBookView *book_view,
               EBookViewStatus status,
               const gchar *error_msg)
{
  printf ("view_complete (status == %d, error_msg == %s%s%s)\n", status, error_msg ? "'" : "", error_msg ? error_msg : "NULL", error_msg ? "'" : "");
}

gint
main (gint argc,
      gchar **argv)
{
#if 0  /* ACCOUNT_MGMT */
	EBook *book;
	gboolean status;
	EBookQuery *query;
	EBookView *view = NULL;
	EBookView *new_view;
	gint i;

	g_type_init ();

	/*
	** the actual ebook foo
	*/

	printf ("loading addressbook\n");
	book = e_book_new_system_addressbook (NULL);
	if (!book) {
		printf ("failed to create ebook\n");
		exit (0);
	}

	status = e_book_open (book, FALSE, NULL);
	if (status == FALSE) {
		printf ("failed to open local addressbook\n");
		exit (0);
	}

	query = e_book_query_any_field_contains ("");

	for (i = 0; i < 500; i++) {
		status = e_book_get_book_view (book, query, NULL, -1, &new_view, NULL);

		g_signal_connect (new_view, "contacts_added", G_CALLBACK (contacts_added), NULL);
		g_signal_connect (new_view, "contacts_removed", G_CALLBACK (contacts_removed), NULL);
		g_signal_connect (new_view, "view_complete", G_CALLBACK (view_complete), NULL);

		e_book_view_start (new_view);

		if (view) {
			e_book_view_stop (view);
			g_object_unref (view);
		}

		view = new_view;
	}

	e_book_view_stop (view);
	g_object_unref (view);

	e_book_query_unref (query);
	g_object_unref (book);
#endif /* ACCOUNT_MGMT */

	return 0;
}
