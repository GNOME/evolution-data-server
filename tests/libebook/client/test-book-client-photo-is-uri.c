/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0 };

static const gchar *photo_data =
"/9j / 4AAQSkZJRgABAQEARwBHAAD//gAXQ3JlYXRlZCB3aXRoIFRoZSBHSU1Q / 9sAQwAIBgYHB\
gUIBwcHCQkICgwUDQwLCwwZEhMPFB0aHx4dGhwcICQuJyAiLCMcHCg3KSwwMTQ0NB8nOT04Mjw\
uMzQy / 9sAQwEJCQkMCwwYDQ0YMiEcITIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyM\
jIyMjIyMjIyMjIyMjIyMjIy / 8AAEQgAMgAyAwEiAAIRAQMRAf / EABsAAQACAwEBAAAAAAAAAAA\
AAAAHCAQFBgID / 8QAMBAAAgEDAQYEBQQDAAAAAAAAAQIDAAQRBQYSEyExQQdhcYEiI0JRkRQVM\
qFiguH / xAAaAQADAQEBAQAAAAAAAAAAAAAABAUCBgED / 8QAIxEAAgICAQQCAwAAAAAAAAAAAAE\
CAwQRQRITITEUYQUiUf / aAAwDAQACEQMRAD8An + sHUtWtNKjVrmQ7754cajLvjrgfbzPIdzWdV\
fds9pJb3XdQkMrcFZGj + HqY0bdVV9Tz / wBia + N9vbjvkaxMb5E9N6SJB1HxLEEjJaWsUjD6QzS\
MPXdGB7E1zV74t63HINy1s4F7CWCTn77wrA0TY86jY3N1qsUk6wxBxBDvYjLHkoUH4j3JP / a0V\
3s1CvF / QM9tKpw0THeU + TLkj8VLnmzT8y0n9FujBx5bioba / rZLWx3iPZ7RzLp95GtnqRGVTez\
HNjruH7 / 4n + 67iqpq7Qi3uYWMMsNynfnE6sM8 / Lr6VamFi0KMepUE1Sx7XZHbI + fjxos1H0z3S\
lKYEjzISI2I64OKqsyu8sck2QYrmPjBvpIYg598Vauoh8VtlY7JW2isoBwpPl6hGByZTyD + o6E\
+h7UtlVOcPHA/+PyI1Wal6Zp7vaC / 06wnTTLtEeUDiKwzu4H8vI9AM9Tiuctkng1Nnk1G5cOoY\
ifB4nI / jB7VjWuoT21qPmwXUCHKlphHKvqG5N6g0 / cLi / Rg88FhbkbxlaUSu3kqpnn6kDzqGqb\
NdPB0XyK4 / svZr9RVntL50GePdcKEDqzhVBx7sKtPpayppNosxzKIlDHzxUFeG2zo2n2kivWhK\
6PpHwwoTnfk65J7kZyT9z5VYADAwKuYtfRA5zPv7tnjgUpSmREV8bq1hvbWW1uY1khlUo6MMhg\
eor7UoAje18FtmLe9eeQT3EXPcglkJRPbv71EWu7Dajp2o3MGmlRCkjKQ30jPUe1WlrlNW0Rpt\
TleNB84DnjkD0P9VlxT4Nqck9pmn8JuFp2zo0cgCWFi2e7555 / NSHXLadso2m3sU0NxlV65HM+\
VdTW3rgwvsUpSvAFKUoAUxSlAClKUAKUpQB//2Q==";

static GMainLoop *loop = NULL;
static gchar     *micheal_jackson_uid = NULL;
static gchar     *james_brown_uid = NULL;

/* Decide what to do with every "view-completed" signal */
enum {
	ITERATION_SWAP_FACE = 0,
	ITERATION_DELETE_JAMES,
	ITERATION_UPDATE_MICHEAL,
	ITERATION_DELETE_MICHEAL,
	ITERATION_FINISH
};
static gint       iteration = ITERATION_SWAP_FACE;

static void
print_contact (EContact *contact)
{
	EContactPhoto *photo = e_contact_get (contact, E_CONTACT_PHOTO);

	g_assert (photo != NULL);
	g_assert (photo->type == E_CONTACT_PHOTO_TYPE_URI);
	g_print ("Test passed with photo uri: %s\n", photo->data.uri);
}

static void
objects_added (EBookView *book_view,
               const GSList *contacts)
{
	const GSList *l;

	for (l = (GSList *) contacts; l; l = l->next) {
		print_contact (l->data);
	}
}

static void
objects_modified (EBookView *book_view,
                  const GSList *contacts)
{
	GSList *l;

	for (l = (GSList *) contacts; l; l = l->next) {
		print_contact (l->data);
	}
}

static void
objects_removed (EBookClientView *book_view,
                 const GSList *ids)
{
	GSList *l;

	for (l = (GSList *) ids; l; l = l->next) {
		g_print ("Removed contact: %s\n", (gchar *) l->data);
	}
}

/* This provokes the backend to handle a cross-referenced photo
 * between contacts, how the backend handles this is it's choice,
 * we should test that when deleting one of the contacts, the other
 * contact does not lose it's photo on disk as a result.
 */
static void
give_james_brown_micheal_jacksons_face (EBookClient *book)
{
	EContact       *micheal = NULL, *james = NULL;
	EContactPhoto  *micheal_face;
	EContactPhoto  *james_face;
	GError         *error = NULL;

	if (!e_book_client_get_contact_sync (book, micheal_jackson_uid, &micheal, NULL, &error))
		g_error ("Unable to get micheal jackson's contact information: %s", error->message);

	if (!e_book_client_get_contact_sync (book, james_brown_uid, &james, NULL, &error))
		g_error ("Unable to get james brown's contact information: %s", error->message);

	g_assert (micheal);
	g_assert (james);

	micheal_face = e_contact_get (micheal, E_CONTACT_PHOTO);
	g_assert_nonnull (micheal_face);
	g_assert (micheal_face->type == E_CONTACT_PHOTO_TYPE_URI);

	james_face = e_contact_photo_new ();
	james_face->type = E_CONTACT_PHOTO_TYPE_URI;
	james_face->data.uri = g_strdup (micheal_face->data.uri);

	e_contact_set (james, E_CONTACT_PHOTO, james_face);

	g_print ("Giving james brown micheal jacksons face: %s\n", micheal_face->data.uri);

	e_contact_photo_free (micheal_face);
	e_contact_photo_free (james_face);

	if (!e_book_client_modify_contact_sync (book, james, E_BOOK_OPERATION_FLAG_NONE, NULL, &error))
		g_error ("Failed to modify contact with cross referenced photo: %s", error->message);
}

static void
update_contact_inline (EBookClient *book,
                       const gchar *uid)
{
	EContact *contact = NULL;
	EContactPhoto *photo;
	guchar *data;
	gsize length = 0;
	GError *error = NULL;

	if (!e_book_client_get_contact_sync (book, uid, &contact, NULL, &error))
		g_error ("Unable to get contact: %s", error->message);

	g_assert (contact);

	data = g_base64_decode (photo_data, &length);

	photo = e_contact_photo_new ();
	photo->type = E_CONTACT_PHOTO_TYPE_INLINED;
	photo->data.inlined.mime_type = NULL;
	photo->data.inlined.data = data;
	photo->data.inlined.length = length;

	/* set the photo */
	e_contact_set (contact, E_CONTACT_PHOTO, photo);

	e_contact_photo_free (photo);

	if (!e_book_client_modify_contact_sync (book, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, &error))
		g_error ("Failed to modify contact with inline photo data: %s", error->message);
}

/* This assertion is made a couple of times in the view-complete
 * handler, we run it to ensure that binary blobs and cross-referenced
 * photo uris exist on disk while they should */
static void
assert_uri_exists (EBookClient *book,
                   const gchar *uid)
{
	EContact      *contact;
	EContactPhoto *photo;
	const gchar   *filename;
	GError        *error = NULL;

	if (!e_book_client_get_contact_sync (book, uid, &contact, NULL, &error))
	  g_error ("Unable to get contact: %s", error->message);

	g_assert (contact);

	photo = e_contact_get (contact, E_CONTACT_PHOTO);
	g_assert (photo);
	g_assert (photo->type == E_CONTACT_PHOTO_TYPE_URI);

	filename = g_filename_from_uri (photo->data.uri, NULL, NULL);
	g_assert (filename);

	/* The file should absolutely exist at this point */
	g_assert (g_file_test (filename, G_FILE_TEST_EXISTS));
}

static void
complete (EBookClientView *view,
          const GError *error)
{
	EBookClient *book = e_book_client_view_ref_client (view);
	GError *local_error = NULL;

	g_print ("View complete, iteration %d\n", iteration);

	/* We get another "complete" notification after removing or modifying a contact */
	switch (iteration++) {
	case ITERATION_SWAP_FACE:
		give_james_brown_micheal_jacksons_face (book);
		break;
	case ITERATION_DELETE_JAMES:
		assert_uri_exists (book, james_brown_uid);

		if (!e_book_client_remove_contact_by_uid_sync (book, james_brown_uid, E_BOOK_OPERATION_FLAG_NONE, NULL, &local_error))
			g_error ("Error removing contact: %s", local_error->message);

		g_free (james_brown_uid);
		james_brown_uid = NULL;
		break;
	case ITERATION_UPDATE_MICHEAL:
		assert_uri_exists (book, micheal_jackson_uid);

		update_contact_inline (book, micheal_jackson_uid);
		break;
	case ITERATION_DELETE_MICHEAL:
		assert_uri_exists (book, micheal_jackson_uid);

		if (!e_book_client_remove_contact_by_uid_sync (book, micheal_jackson_uid, E_BOOK_OPERATION_FLAG_NONE, NULL, &local_error))
			g_error ("Error removing contact: %s", local_error->message);

		g_free (micheal_jackson_uid);
		micheal_jackson_uid = NULL;
		break;
	case ITERATION_FINISH:
		e_book_client_view_stop (view, NULL);
		g_object_unref (view);
		g_main_loop_quit (loop);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	g_object_unref (book);
}

static void
setup_and_start_view (EBookClientView *view)
{
	GError *error = NULL;

	g_signal_connect (view, "objects-added", G_CALLBACK (objects_added), NULL);
	g_signal_connect (view, "objects-removed", G_CALLBACK (objects_removed), NULL);
	g_signal_connect (view, "objects-modified", G_CALLBACK (objects_modified), NULL);
	g_signal_connect (view, "complete", G_CALLBACK (complete), NULL);

	e_book_client_view_set_fields_of_interest (view, NULL, &error);
	if (error)
		g_error ("set fields of interest: %s", error->message);

	e_book_client_view_start (view, &error);
	if (error)
		g_error ("start view: %s", error->message);
}

static void
add_contact_inline (EBookClient *book)
{
	EContact *contact;
	EContactPhoto *photo;
	guchar *data;
	gsize length = 0;

	contact = e_contact_new ();

	data = g_base64_decode (photo_data, &length);

	photo = e_contact_photo_new ();
	photo->type = E_CONTACT_PHOTO_TYPE_INLINED;
	photo->data.inlined.mime_type = NULL;
	photo->data.inlined.data = data;
	photo->data.inlined.length = length;

	/* set the photo */
	e_contact_set (contact, E_CONTACT_PHOTO, photo);
	e_contact_set (contact, E_CONTACT_FULL_NAME, "Micheal Jackson");

	e_contact_photo_free (photo);

	if (!add_contact_verify (book, contact))
		g_error ("Failed to add contact");

	micheal_jackson_uid = e_contact_get (contact, E_CONTACT_UID);
}

static void
add_contact_uri (EBookClient *book)
{
	EContact *contact;
	EContactPhoto *photo;

	contact = e_contact_new ();

	photo = e_contact_photo_new ();
	photo->type = E_CONTACT_PHOTO_TYPE_URI;
	photo->data.uri = g_strdup ("http://en.wikipedia.org/wiki/File:Jamesbrown4.jpg");

	/* set the photo */
	e_contact_set (contact, E_CONTACT_PHOTO, photo);
	e_contact_set (contact, E_CONTACT_FULL_NAME, "James Brown");

	e_contact_photo_free (photo);

	if (!add_contact_verify (book, contact))
		g_error ("Failed to add contact");

	james_brown_uid = e_contact_get (contact, E_CONTACT_UID);
}

static void
test_photo_is_uri (ETestServerFixture *fixture,
                   gconstpointer user_data)
{
	EBookClient *book_client;
	EBookClientView *view;
	EBookQuery *query;
	GError     *error = NULL;
	gchar      *sexp;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	add_contact_inline (book_client);
	add_contact_uri (book_client);

	query = e_book_query_any_field_contains ("");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);
	if (!e_book_client_get_view_sync (book_client, sexp, &view, NULL, &error))
		g_error ("get book view sync: %s", error->message);

	g_free (sexp);

	setup_and_start_view (view);

	loop = fixture->loop;
	g_main_loop_run (loop);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	client_test_utils_read_args (argc, argv);

	g_test_add (
		"/EBookClient/PhotoIsUri",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_photo_is_uri,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}
