/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

static GMainLoop *loop;

static void
print_email (EContact *contact)
{
	const gchar *file_as = e_contact_get_const (contact, E_CONTACT_FILE_AS);
	const gchar *name_or_org = e_contact_get_const (contact, E_CONTACT_NAME_OR_ORG);
	GList *emails, *e;

	printf ("Contact: %s\n", file_as);
	printf ("Name or org: %s\n", name_or_org);
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
print_all_emails_cb (EBook *book,
                     const GError *error,
                     GList *contacts,
                     gpointer closure)
{
	GList *c;

	if (!error) {
		for (c = contacts; c; c = c->next) {
			EContact *contact = E_CONTACT (c->data);

			print_email (contact);
		}
	} else {
		g_warning ("%s: Got error %d (%s)", G_STRFUNC, error->code, error->message);
	}

	g_main_loop_quit (loop);
}

static void
print_all_emails (EBook *book)
{
	EBookQuery *query;

	query = e_book_query_field_exists (E_CONTACT_FULL_NAME);

	e_book_get_contacts_async (book, query, print_all_emails_cb, NULL);

	e_book_query_unref (query);
}

static void
print_email_cb (EBook *book,
                const GError *error,
                EContact *contact,
                gpointer closure)
{
	if (!error)
		print_email (contact);
	else
		g_warning ("%s: Got error %d (%s)", G_STRFUNC, error->code, error->message);

	printf ("printing all contacts\n");
	print_all_emails (book);
}

static void
print_one_email (EBook *book)
{
	e_book_get_contact_async (book, "pas-id-0002023", print_email_cb, NULL);
}

static void
book_loaded_cb (EBook *book,
                const GError *error,
                gpointer data)
{
	if (error) {
		g_warning ("%s: Got error %d (%s)", G_STRFUNC, error->code, error->message);
		return;
	}

	printf ("printing one contact\n");
	print_one_email (book);
}

gint
main (gint argc,
      gchar **argv)
{
#if 0  /* ACCOUNT_MGMT */
	EBook *book;

	g_type_init ();
	loop = g_main_loop_new (NULL, TRUE);

	/*
	** the actual ebook foo
	*/

	book = e_book_new_system_addressbook (NULL);

	printf ("loading addressbook\n");
	e_book_open_async (book, FALSE, book_loaded_cb, book);

	g_main_loop_run (loop);
#endif /* ACCOUNT_MGMT */

	return 0;
}
