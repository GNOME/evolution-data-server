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

#include <string.h>
#include <libebook-contacts/libebook-contacts.h>

typedef struct {
	EContact *contact;
} TypesFixture;

static void
types_setup (TypesFixture *fixture,
             gconstpointer user_data)
{
	fixture->contact = e_contact_new ();
}

static void
types_teardown (TypesFixture *fixture,
                gconstpointer user_data)
{
	g_object_unref (fixture->contact);
}

/************* UNDEFINED/UNSUPPORTED ***************/

#if 0 /* This cannot be tested, the g_return_val_if_fail() in e_contact_get()
       * will cause the whole test to fail.
       */
static void
test_undefined_field (TypesFixture *fixture,
                      gconstpointer user_data)
{
	gpointer test;

	test = e_contact_get (fixture->contact, 6000 /* something suitably high. */);
	g_assert_true (test == NULL);
}
#endif

/************* STRING *****************/
#define TEST_ID "test-uid"

static void
test_string (TypesFixture *fixture,
             gconstpointer user_data)
{
	e_contact_set (fixture->contact, E_CONTACT_UID, TEST_ID);
	g_assert_cmpstr (e_contact_get_const (fixture->contact, E_CONTACT_UID), ==, TEST_ID);
}

/************* DATE *****************/
static void
test_date (TypesFixture *fixture,
           gconstpointer user_data)
{
	EContactDate date, *dp;

	date.year = 1999;
	date.month = 3;
	date.day = 3;

	e_contact_set (fixture->contact, E_CONTACT_BIRTH_DATE, &date);

	dp = e_contact_get (fixture->contact, E_CONTACT_BIRTH_DATE);

	g_assert_cmpuint (dp->year, ==, date.year);
	g_assert_cmpuint (dp->month, ==, date.month);
	g_assert_cmpuint (dp->day, ==, date.day);

	e_contact_date_free (dp);
}

/************ CERTIFICATES ***************/
static void
test_certificates (TypesFixture *fixture,
		   gconstpointer user_data)
{
	const gchar pgp_blob[] = "fake\tpgp-certificate-blob\n\x1\x2\x3\x4\x5\x6\x7\x8\x9\x0 abc";
	const gchar x509_blob[] = "fake\tX.509-certificate-blob\n\x1\x2\x3\x4\x5\x6\x7\x8\x9\x0 def";
	gsize pgp_blob_length = sizeof (pgp_blob);
	gsize x509_blob_length = sizeof (x509_blob);
	gsize ii;
	EContactCert *cert;

	cert = e_contact_cert_new ();
	cert->data = g_memdup (pgp_blob, pgp_blob_length);
	cert->length = pgp_blob_length;
	e_contact_set (fixture->contact, E_CONTACT_PGP_CERT, cert);
	e_contact_cert_free (cert);

	cert = e_contact_cert_new ();
	cert->data = g_memdup (x509_blob, x509_blob_length);
	cert->length = x509_blob_length;
	e_contact_set (fixture->contact, E_CONTACT_X509_CERT, cert);
	e_contact_cert_free (cert);

	cert = e_contact_get (fixture->contact, E_CONTACT_PGP_CERT);
	g_assert_nonnull (cert);
	g_assert_cmpuint (cert->length, ==, pgp_blob_length);

	for (ii = 0; ii < pgp_blob_length; ii++) {
		if (cert->data[ii] != pgp_blob[ii])
			break;
	}

	g_assert_true (ii == pgp_blob_length);

	e_contact_cert_free (cert);

	cert = e_contact_get (fixture->contact, E_CONTACT_X509_CERT);
	g_assert_nonnull (cert);
	g_assert_cmpuint (cert->length, ==, x509_blob_length);

	for (ii = 0; ii < x509_blob_length; ii++) {
		if (cert->data[ii] != x509_blob[ii])
			break;
	}

	g_assert_true (ii == x509_blob_length);

	e_contact_cert_free (cert);
}

/***************** PHOTO *****************/
static const gchar *photo_data =
	"/9j/4AAQSkZJRgABAQEARwBHAAD//gAXQ3JlYXRlZCB3aXRoIFRoZSBHSU1Q/9sAQwAIBgYHB"
	"gUIBwcHCQkICgwUDQwLCwwZEhMPFB0aHx4dGhwcICQuJyAiLCMcHCg3KSwwMTQ0NB8nOT04Mjw"
	"uMzQy/9sAQwEJCQkMCwwYDQ0YMiEcITIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyM"
	"jIyMjIyMjIyMjIyMjIyMjIy/8AAEQgAMgAyAwEiAAIRAQMRAf/EABsAAQACAwEBAAAAAAAAAAA"
	"AAAAHCAQFBgID/8QAMBAAAgEDAQYEBQQDAAAAAAAAAQIDAAQRBQYSEyExQQdhcYEiI0JRkRQVM"
	"qFiguH/xAAaAQADAQEBAQAAAAAAAAAAAAAABAUCBgED/8QAIxEAAgICAQQCAwAAAAAAAAAAAAE"
	"CAwQRQRITITEUYQUiUf/aAAwDAQACEQMRAD8An+sHUtWtNKjVrmQ7754cajLvjrgfbzPIdzWdV"
	"fds9pJb3XdQkMrcFZGj+HqY0bdVV9Tz/wBia+N9vbjvkaxMb5E9N6SJB1HxLEEjJaWsUjD6QzS"
	"MPXdGB7E1zV74t63HINy1s4F7CWCTn77wrA0TY86jY3N1qsUk6wxBxBDvYjLHkoUH4j3JP/a0V"
	"3s1CvF/QM9tKpw0THeU+TLkj8VLnmzT8y0n9FujBx5bioba/rZLWx3iPZ7RzLp95GtnqRGVTez"
	"HNjruH7/4n+67iqpq7Qi3uYWMMsNynfnE6sM8/Lr6VamFi0KMepUE1Sx7XZHbI+fjxos1H0z3S"
	"lKYEjzISI2I64OKqsyu8sck2QYrmPjBvpIYg598Vauoh8VtlY7JW2isoBwpPl6hGByZTyD+o6E"
	"+h7UtlVOcPHA/+PyI1Wal6Zp7vaC/06wnTTLtEeUDiKwzu4H8vI9AM9Tiuctkng1Nnk1G5cOoY"
	"ifB4nI/jB7VjWuoT21qPmwXUCHKlphHKvqG5N6g0/cLi/Rg88FhbkbxlaUSu3kqpnn6kDzqGqb"
	"NdPB0XyK4/svZr9RVntL50GePdcKEDqzhVBx7sKtPpayppNosxzKIlDHzxUFeG2zo2n2kivWhK"
	"6PpHwwoTnfk65J7kZyT9z5VYADAwKuYtfRA5zPv7tnjgUpSmREV8bq1hvbWW1uY1khlUo6MMhg"
	"eor7UoAje18FtmLe9eeQT3EXPcglkJRPbv71EWu7Dajp2o3MGmlRCkjKQ30jPUe1WlrlNW0Rpt"
	"TleNB84DnjkD0P9VlxT4Nqck9pmn8JuFp2zo0cgCWFi2e7555/NSHXLadso2m3sU0NxlV65HM+"
	"VdTW3rgwvsUpSvAFKUoAUxSlAClKUAKUpQB//2Q==";

static void
test_photo (TypesFixture *fixture,
            gconstpointer user_data)
{
	EContactPhoto *photo, *new_photo;
	guchar *data;
	gsize length = 0;

	data = g_base64_decode (photo_data, &length);

	photo = g_new (EContactPhoto, 1);
	photo->type = E_CONTACT_PHOTO_TYPE_INLINED;
	photo->data.inlined.mime_type = NULL;
	photo->data.inlined.data = data;
	photo->data.inlined.length = length;

	/* set the photo */
	e_contact_set (fixture->contact, E_CONTACT_PHOTO, photo);

	/* then get the photo */
	new_photo = e_contact_get (fixture->contact, E_CONTACT_PHOTO);

	/* and compare */
	g_assert_cmpint (new_photo->data.inlined.length, ==, photo->data.inlined.length);

	if (memcmp (new_photo->data.inlined.data, photo->data.inlined.data, photo->data.inlined.length))
		g_error ("photo data differs");

	e_contact_photo_free (photo);
	e_contact_photo_free (new_photo);
}

/************* CATEGORIES *****************/
static void
test_categories_initially_null_list (TypesFixture *fixture,
                                     gconstpointer user_data)
{
	gpointer test;

	test = e_contact_get (fixture->contact, E_CONTACT_CATEGORY_LIST);
	g_assert_true (test == NULL);
}

static void
test_categories_convert_to_string (TypesFixture *fixture,
                                   gconstpointer user_data)
{
	GList *category_list;
	gchar *categories;

	category_list = NULL;
	category_list = g_list_append (category_list, (gpointer) "Birthday");
	category_list = g_list_append (category_list, (gpointer) "Business");
	category_list = g_list_append (category_list, (gpointer) "Competition");

	e_contact_set (fixture->contact, E_CONTACT_CATEGORY_LIST, category_list);

	categories = e_contact_get (fixture->contact, E_CONTACT_CATEGORIES);

	/* Test conversion of list to string */
	g_assert_cmpstr (categories, ==, "Birthday,Business,Competition");

	g_list_free (category_list);
	g_free (categories);
}

static void
test_categories_convert_to_list (TypesFixture *fixture,
                                 gconstpointer user_data)
{
	GList *category_list;

	e_contact_set (fixture->contact, E_CONTACT_CATEGORIES, "Birthday,Business,Competition");

	category_list = e_contact_get (fixture->contact, E_CONTACT_CATEGORY_LIST);

	/* Test conversion of string to list */
	g_assert_cmpint (g_list_length (category_list), ==, 3);
	g_assert_cmpstr ((gchar *) g_list_nth_data (category_list, 0), ==, "Birthday");
	g_assert_cmpstr ((gchar *) g_list_nth_data (category_list, 1), ==, "Business");
	g_assert_cmpstr ((gchar *) g_list_nth_data (category_list, 2), ==, "Competition");

	g_list_free_full (category_list, g_free);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

#if 0   /* This can't properly be tested, the assertion causes the test to break */
	g_test_add (
		"/Contact/Types/UndefinedField",
		TypesFixture, NULL,
		types_setup,
		test_undefined_field,
		types_teardown);
#endif
	g_test_add (
		"/Contact/Types/String",
		TypesFixture, NULL,
		types_setup,
		test_string,
		types_teardown);
	g_test_add (
		"/Contact/Types/Date",
		TypesFixture, NULL,
		types_setup,
		test_date,
		types_teardown);
	g_test_add (
		"/Contact/Types/Certificates",
		TypesFixture, NULL,
		types_setup,
		test_certificates,
		types_teardown);
	g_test_add (
		"/Contact/Types/Photo",
		TypesFixture, NULL,
		types_setup,
		test_photo,
		types_teardown);
	g_test_add (
		"/Contact/Types/Categories/InitiallyNullList",
		TypesFixture, NULL,
		types_setup,
		test_categories_initially_null_list,
		types_teardown);
	g_test_add (
		"/Contact/Types/Categories/ConvertToString",
		TypesFixture, NULL,
		types_setup,
		test_categories_convert_to_string,
		types_teardown);
	g_test_add (
		"/Contact/Types/Categories/ConvertToList",
		TypesFixture, NULL,
		types_setup,
		test_categories_convert_to_list,
		types_teardown);

	return g_test_run ();
}
