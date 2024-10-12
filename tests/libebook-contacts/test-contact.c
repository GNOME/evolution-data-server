/*
 * SPDX-FileCopyrightText: (C) 2024 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <libebook-contacts/libebook-contacts.h>

static EContactAddress *
test_util_new_address (guint index)
{
	EContactAddress *addr;

	addr = e_contact_address_new ();
	addr->address_format = g_strdup (""); /* g_strdup_printf ("%02u", index); */ /* unused in the e-contact.c */
	addr->po = g_strdup_printf ("%02u-po", index);
	addr->ext = g_strdup_printf ("%02u-ext", index);
	addr->street = g_strdup_printf ("%02u-street", index);
	addr->locality = g_strdup_printf ("%02u-locality", index);
	addr->region = g_strdup_printf ("%02u-region", index);
	addr->code = g_strdup_printf ("%02u-code", index);
	addr->country = g_strdup_printf ("%02u-country", index);

	return addr;
}

static void
test_util_verify_address (const EContactAddress *addr1,
			  const EContactAddress *addr2)
{
	g_assert_nonnull (addr1);
	g_assert_nonnull (addr2);
	g_assert_true (addr1 != addr2);
	g_assert_cmpstr (addr1->address_format, ==, addr2->address_format);
	g_assert_cmpstr (addr1->po, ==, addr2->po);
	g_assert_cmpstr (addr1->ext, ==, addr2->ext);
	g_assert_cmpstr (addr1->street, ==, addr2->street);
	g_assert_cmpstr (addr1->locality, ==, addr2->locality);
	g_assert_cmpstr (addr1->region, ==, addr2->region);
	g_assert_cmpstr (addr1->code, ==, addr2->code);
	g_assert_cmpstr (addr1->country, ==, addr2->country);
}

static void
test_util_check_im_field (EContact *contact,
			  guint ii,
			  guint start_index_1,
			  guint start_index_2)
{
	gchar *stored = NULL, expected[128];
	guint start_index = ii < start_index_2 ? start_index_1 : start_index_2;
	guint jj;

	for (jj = start_index; jj < ii; jj++) {
		gchar tmp[128];

		g_snprintf (tmp, sizeof (tmp), "im-tmp-%u-%u", ii, jj);
		g_object_set (contact, e_contact_field_name (jj), tmp, NULL);
	}

	g_snprintf (expected, sizeof (expected), "im-%03u", ii);

	g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
	g_assert_null (stored);
	g_object_set (contact, e_contact_field_name (ii), expected, NULL);
	g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
	g_assert_nonnull (stored);
	g_assert_true (stored != expected);
	g_assert_cmpstr (stored, ==, expected);
	g_free (stored);

	g_object_set (contact, e_contact_field_name (ii), NULL, NULL);
	g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
	g_assert_null (stored);

	for (jj = start_index; jj < ii; jj++) {
		g_object_set (contact, e_contact_field_name (jj), NULL, NULL);
		e_vcard_remove_attributes (E_VCARD (contact), NULL, e_contact_vcard_attribute (jj));
	}
}

static void
test_contact_gobject_props (void)
{
	EContact *contact;
	guint ii;

	contact = e_contact_new ();

	for (ii = E_CONTACT_UID; ii < E_CONTACT_FIELD_LAST; ii++) {
		if (ii <= E_CONTACT_LAST_SIMPLE_STRING) {
			gchar *stored = NULL;
			gchar *expected = g_strdup_printf ("some-value-%u", ii);

			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);

			if (ii == E_CONTACT_EMAIL_2 || ii == E_CONTACT_EMAIL_3 || ii == E_CONTACT_EMAIL_4) {
				guint jj;

				/* cannot set EMAIL 2, without having set also EMAIL 1 */
				for (jj = E_CONTACT_EMAIL_1; jj < ii; jj++) {
					g_object_set (contact, e_contact_field_name (jj), "tmp-value", NULL);
				}
			} else if (ii == E_CONTACT_PHONE_BUSINESS_2 ||
				   ii == E_CONTACT_PHONE_HOME_2 ||
				   ii == E_CONTACT_IM_AIM_HOME_2 ||
				   ii == E_CONTACT_IM_AIM_WORK_2 ||
				   ii == E_CONTACT_IM_GROUPWISE_HOME_2 ||
				   ii == E_CONTACT_IM_GROUPWISE_WORK_2 ||
				   ii == E_CONTACT_IM_JABBER_HOME_2 ||
				   ii == E_CONTACT_IM_JABBER_WORK_2 ||
				   ii == E_CONTACT_IM_YAHOO_HOME_2 ||
				   ii == E_CONTACT_IM_YAHOO_WORK_2 ||
				   ii == E_CONTACT_IM_MSN_HOME_2 ||
				   ii == E_CONTACT_IM_MSN_WORK_2 ||
				   ii == E_CONTACT_IM_ICQ_HOME_2 ||
				   ii == E_CONTACT_IM_ICQ_WORK_2 ) {
				g_object_set (contact, e_contact_field_name (ii - 1), "tmp-value", NULL);
			} else if (ii == E_CONTACT_IM_AIM_HOME_3 ||
				   ii == E_CONTACT_IM_AIM_WORK_3 ||
				   ii == E_CONTACT_IM_GROUPWISE_HOME_3 ||
				   ii == E_CONTACT_IM_GROUPWISE_WORK_3 ||
				   ii == E_CONTACT_IM_JABBER_HOME_3 ||
				   ii == E_CONTACT_IM_JABBER_WORK_3 ||
				   ii == E_CONTACT_IM_YAHOO_HOME_3 ||
				   ii == E_CONTACT_IM_YAHOO_WORK_3 ||
				   ii == E_CONTACT_IM_MSN_HOME_3 ||
				   ii == E_CONTACT_IM_MSN_WORK_3 ||
				   ii == E_CONTACT_IM_ICQ_HOME_3 ||
				   ii == E_CONTACT_IM_ICQ_WORK_3 ) {
				g_object_set (contact, e_contact_field_name (ii - 2), "tmp-value-1", NULL);
				g_object_set (contact, e_contact_field_name (ii - 1), "tmp-value-2", NULL);
			}

			if (ii == E_CONTACT_NAME_OR_ORG) {
				g_object_set (contact, e_contact_field_name (E_CONTACT_FULL_NAME), "full-name", NULL);
				g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
				g_assert_nonnull (stored);
				g_assert_cmpstr (stored, ==, "full-name");
				g_free (stored);
				g_object_set (contact, e_contact_field_name (E_CONTACT_NAME), NULL, NULL);
				g_object_set (contact, e_contact_field_name (E_CONTACT_FULL_NAME), NULL, NULL);
				g_object_set (contact, e_contact_field_name (E_CONTACT_FILE_AS), NULL, NULL);
				e_contact_set (contact, E_CONTACT_FILE_AS, NULL);
				e_vcard_remove_attributes (E_VCARD (contact), NULL, e_contact_vcard_attribute (E_CONTACT_NAME));
				e_vcard_remove_attributes (E_VCARD (contact), NULL, e_contact_vcard_attribute (E_CONTACT_FULL_NAME));
				e_vcard_remove_attributes (E_VCARD (contact), NULL, e_contact_vcard_attribute (E_CONTACT_FILE_AS));
				g_object_set (contact, e_contact_field_name (E_CONTACT_ORG), "org", NULL);
				g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
				g_assert_nonnull (stored);
				g_assert_cmpstr (stored, ==, "org");
				g_free (stored);
			} else {
				g_object_set (contact, e_contact_field_name (ii), expected, NULL);
				g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
				g_assert_nonnull (stored);
				g_assert_cmpstr (stored, ==, expected);
				g_free (stored);
			}

			g_free (expected);

			if (ii != E_CONTACT_NAME_OR_ORG)
				e_contact_set (contact, ii, NULL);

			if (ii == E_CONTACT_CATEGORIES) {
				e_vcard_remove_attributes (E_VCARD (contact), NULL, "CATEGORIES");
			} else if (ii != E_CONTACT_NAME_OR_ORG) {
				e_vcard_remove_attributes (E_VCARD (contact), NULL, e_contact_vcard_attribute (ii));
			}
			if (ii == E_CONTACT_FULL_NAME) {
				e_vcard_remove_attributes (E_VCARD (contact), NULL, e_contact_vcard_attribute (E_CONTACT_NAME));
				e_vcard_remove_attributes (E_VCARD (contact), NULL, e_contact_vcard_attribute (E_CONTACT_FILE_AS));
			}
			/* it's not a writable attribute and the EContact caches its value; it cannot
			   know the attributes had been removed through the vCard API */
			if (ii != E_CONTACT_NAME_OR_ORG) {
				g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
				g_assert_null (stored);
			}
		} else if (ii == E_CONTACT_ADDRESS) {
			/* This does not work right, and hopefully nobody uses it;
			   the getter returns only a list of strings, while it should be
			   the EContactAddress data; similarly the setter. */
			/*EContactAddress *addr1, *addr2;
			GList *stored = NULL, *expected = NULL;

			addr1 = test_util_new_address (1);
			addr2 = test_util_new_address (2);

			expected = g_list_prepend (expected, addr2);
			expected = g_list_prepend (expected, addr1);

			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);
			g_object_set (contact, e_contact_field_name (ii), expected, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_nonnull (stored);
			g_assert_true (stored != expected);
			g_assert_cmpint (g_list_length (stored), ==, g_list_length (expected));
			test_util_verify_address (stored->data, expected->data);
			test_util_verify_address (stored->next->data, expected->next->data);
			g_list_free_full (stored, (GDestroyNotify) e_contact_address_free);

			g_object_set (contact, e_contact_field_name (ii), NULL, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);

			g_list_free_full (expected, (GDestroyNotify) e_contact_address_free);*/
		} else if (ii == E_CONTACT_ADDRESS_HOME ||
			   ii == E_CONTACT_ADDRESS_WORK ||
			   ii == E_CONTACT_ADDRESS_OTHER) {
			EContactAddress *stored = NULL, *expected;

			expected = test_util_new_address (ii);

			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);
			g_object_set (contact, e_contact_field_name (ii), expected, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_nonnull (stored);
			g_assert_true (stored != expected);
			test_util_verify_address (stored, expected);
			e_contact_address_free (stored);

			g_object_set (contact, e_contact_field_name (ii), NULL, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);

			e_contact_address_free (expected);
		} else if (ii == E_CONTACT_CATEGORY_LIST ||
			   ii == E_CONTACT_EMAIL ||
			   ii == E_CONTACT_IM_AIM ||
			   ii == E_CONTACT_IM_GROUPWISE ||
			   ii == E_CONTACT_IM_JABBER ||
			   ii == E_CONTACT_IM_YAHOO ||
			   ii == E_CONTACT_IM_MSN ||
			   ii == E_CONTACT_IM_ICQ ||
			   ii == E_CONTACT_IM_GADUGADU ||
			   ii == E_CONTACT_TEL ||
			   ii == E_CONTACT_IM_SKYPE ||
			   ii == E_CONTACT_SIP ||
			   ii == E_CONTACT_IM_GOOGLE_TALK ||
			   ii == E_CONTACT_IM_TWITTER ||
			   ii == E_CONTACT_IM_MATRIX) {
			GList *stored = NULL, *expected = NULL;

			expected = g_list_prepend (expected, g_strdup_printf ("%02u-a", ii));
			expected = g_list_prepend (expected, g_strdup_printf ("%02u-b", ii));

			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);
			g_object_set (contact, e_contact_field_name (ii), expected, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_nonnull (stored);
			g_assert_true (stored != expected);
			g_assert_cmpint (g_list_length (stored), ==, g_list_length (expected));
			g_assert_cmpstr (stored->data, ==, expected->data);
			g_assert_cmpstr (stored->next->data, ==, expected->next->data);
			g_list_free_full (stored, g_free);

			g_object_set (contact, e_contact_field_name (ii), NULL, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);

			g_list_free_full (expected, g_free);
		} else if (ii == E_CONTACT_PHOTO ||
			   ii == E_CONTACT_LOGO) {
			EContactPhoto *stored = NULL, *expected;

			expected = e_contact_photo_new ();
			expected->type = E_CONTACT_PHOTO_TYPE_URI;
			expected->data.uri = g_strdup ("https://www.gnome.org/");

			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);
			g_object_set (contact, e_contact_field_name (ii), expected, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_nonnull (stored);
			g_assert_true (stored != expected);
			g_assert_cmpint (stored->type, ==, expected->type);
			g_assert_cmpstr (stored->data.uri, ==, expected->data.uri);
			e_contact_photo_free (stored);

			g_object_set (contact, e_contact_field_name (ii), NULL, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);

			e_contact_photo_free (expected);
		} else if (ii == E_CONTACT_NAME) {
			EContactName *stored = NULL, *expected;

			expected = e_contact_name_new ();
			expected->family = g_strdup ("family");
			expected->given = g_strdup ("given");
			expected->additional = g_strdup ("additional");
			expected->prefixes = g_strdup ("prefixes");
			expected->suffixes = g_strdup ("suffixes");

			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);
			g_object_set (contact, e_contact_field_name (ii), expected, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_nonnull (stored);
			g_assert_true (stored != expected);
			g_assert_cmpstr (stored->family, ==, expected->family);
			g_assert_cmpstr (stored->given, ==, expected->given);
			g_assert_cmpstr (stored->additional, ==, expected->additional);
			g_assert_cmpstr (stored->prefixes, ==, expected->prefixes);
			g_assert_cmpstr (stored->suffixes, ==, expected->suffixes);
			e_contact_name_free (stored);

			g_object_set (contact, e_contact_field_name (ii), NULL, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);

			e_contact_name_free (expected);
		} else if (ii == E_CONTACT_WANTS_HTML ||
			   ii == E_CONTACT_IS_LIST ||
			   ii == E_CONTACT_LIST_SHOW_ADDRESSES) {
			gboolean stored = FALSE, expected = TRUE;

			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_false (stored);
			g_object_set (contact, e_contact_field_name (ii), GINT_TO_POINTER (expected), NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_true (stored);

			g_object_set (contact, e_contact_field_name (ii), NULL, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_false (stored);
		} else if (ii == E_CONTACT_BIRTH_DATE ||
			   ii == E_CONTACT_ANNIVERSARY) {
			EContactDate *stored = NULL, *expected;

			expected = e_contact_date_new ();
			expected->year = 2000 + ii;
			expected->month = 1 + ii - E_CONTACT_BIRTH_DATE;
			expected->day = 3 + ii - E_CONTACT_BIRTH_DATE;

			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);
			g_object_set (contact, e_contact_field_name (ii), expected, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_nonnull (stored);
			g_assert_true (stored != expected);
			g_assert_cmpint (stored->year, ==, expected->year);
			g_assert_cmpint (stored->month, ==, expected->month);
			g_assert_cmpint (stored->day, ==, expected->day);
			e_contact_date_free (stored);

			g_object_set (contact, e_contact_field_name (ii), NULL, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);

			e_contact_date_free (expected);
		} else if (ii == E_CONTACT_X509_CERT ||
			   ii == E_CONTACT_PGP_CERT) {
			EContactCert *stored = NULL, *expected;

			expected = e_contact_cert_new ();
			expected->data = g_strdup ("cert-data");
			expected->length = strlen (expected->data) + 1;

			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);
			g_object_set (contact, e_contact_field_name (ii), expected, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_nonnull (stored);
			g_assert_true (stored != expected);
			g_assert_cmpmem (stored->data, stored->length, expected->data, expected->length);
			e_contact_cert_free (stored);

			g_object_set (contact, e_contact_field_name (ii), NULL, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);

			e_contact_cert_free (expected);
		} else if (ii == E_CONTACT_GEO) {
			EContactGeo *stored = NULL, *expected;

			expected = e_contact_geo_new ();
			expected->latitude = 12.3;
			expected->longitude = 45.6;

			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);
			g_object_set (contact, e_contact_field_name (ii), expected, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_nonnull (stored);
			g_assert_true (stored != expected);
			g_assert_cmpfloat_with_epsilon (stored->latitude, expected->latitude, 1e-5);
			g_assert_cmpfloat_with_epsilon (stored->longitude, expected->longitude, 1e-5);
			e_contact_geo_free (stored);

			g_object_set (contact, e_contact_field_name (ii), NULL, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);

			e_contact_geo_free (expected);
		} else if (ii >= E_CONTACT_IM_GADUGADU_HOME_1 && ii <= E_CONTACT_IM_GADUGADU_WORK_3) {
			test_util_check_im_field (contact, ii, E_CONTACT_IM_GADUGADU_HOME_1, E_CONTACT_IM_GADUGADU_WORK_1);
		} else if (ii >= E_CONTACT_IM_SKYPE_HOME_1 && ii <= E_CONTACT_IM_SKYPE_WORK_3) {
			test_util_check_im_field (contact, ii, E_CONTACT_IM_SKYPE_HOME_1, E_CONTACT_IM_SKYPE_WORK_1);
		} else if (ii >= E_CONTACT_IM_GOOGLE_TALK_HOME_1 && ii <= E_CONTACT_IM_GOOGLE_TALK_WORK_3) {
			test_util_check_im_field (contact, ii, E_CONTACT_IM_GOOGLE_TALK_HOME_1, E_CONTACT_IM_GOOGLE_TALK_WORK_1);
		} else if (ii >= E_CONTACT_IM_MATRIX_HOME_1 && ii <= E_CONTACT_IM_MATRIX_WORK_3) {
			test_util_check_im_field (contact, ii, E_CONTACT_IM_MATRIX_HOME_1, E_CONTACT_IM_MATRIX_WORK_1);
		} else {
			g_error ("Unhandled field index %u, named '%s'", ii, e_contact_field_name (ii));
		}
	}

	g_clear_object (&contact);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	g_test_add_func ("/EContact/GObjectProps", test_contact_gobject_props);

	return g_test_run ();
}
