/*
 * SPDX-FileCopyrightText: (C) 2024 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <libebook-contacts/libebook-contacts.h>

static void
test_contact_convert (void)
{
	EVCardVersion versions[] = { E_VCARD_VERSION_21, E_VCARD_VERSION_30, E_VCARD_VERSION_40 };
	guint ii, jj;

	for (ii = 0; ii < G_N_ELEMENTS (versions); ii++) {
		EVCardVersion from_version = versions[ii];
		EContact *source;
		EVCardAttribute *attr;
		GList *values;
		gchar *str;

		str = g_strdup_printf (
			"BEGIN:VCARD\r\n"
			"VERSION:%s\r\n"
			"FN:test\r\n"
			"END:VCARD\r\n",
			e_vcard_version_to_string (from_version));
		source = e_contact_new_from_vcard (str);
		g_free (str);

		attr = e_vcard_get_attribute (E_VCARD (source), EVC_VERSION);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, e_vcard_version_to_string (from_version));

		g_assert_cmpint (e_vcard_get_version (E_VCARD (source)), ==, from_version);

		for (jj = 0; jj < G_N_ELEMENTS (versions); jj++) {
			EVCardVersion to_version = versions[jj];
			EContact *converted;

			converted = e_contact_convert (source, to_version);
			if (from_version == to_version) {
				g_assert_null (converted);
			} else {
				g_assert_nonnull (converted);
				g_assert_true (E_IS_CONTACT (converted));

				attr = e_vcard_get_attribute (E_VCARD (converted), EVC_VERSION);
				values = e_vcard_attribute_get_values (attr);
				g_assert_cmpuint (g_list_length (values), ==, 1);
				g_assert_cmpstr (values->data, ==, e_vcard_version_to_string (to_version));

				g_assert_cmpint (e_vcard_get_version (E_VCARD (converted)), ==, to_version);

				g_object_unref (converted);
			}
		}

		g_object_unref (source);
	}
}

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
		g_object_set (contact, e_contact_field_name (start_index + ii - jj - 1), NULL, NULL);
		e_vcard_remove_attributes (E_VCARD (contact), NULL, e_contact_vcard_attribute (jj));
	}
}

static void
test_contact_gobject_props (EContact *contact)
{
	guint ii;

	for (ii = E_CONTACT_UID; ii < E_CONTACT_FIELD_LAST; ii++) {
		if (ii <= E_CONTACT_LAST_SIMPLE_STRING ||
		    ii == E_CONTACT_KIND ||
		    ii == E_CONTACT_SOURCE ||
		    ii == E_CONTACT_XML ||
		    ii == E_CONTACT_BIRTHPLACE ||
		    ii == E_CONTACT_DEATHPLACE ||
		    ii == E_CONTACT_CONTACT_URI ||
		    ii == E_CONTACT_SOCIALPROFILE) {
			gchar *stored = NULL;
			gchar *expected = g_strdup_printf ("some-value-%u", ii);
			gint temporares_from = -1, n_temporares = 0;

			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);

			if (ii == E_CONTACT_EMAIL_2 || ii == E_CONTACT_EMAIL_3 || ii == E_CONTACT_EMAIL_4) {
				guint jj;

				/* cannot set EMAIL 2, without having set also EMAIL 1 */
				for (jj = E_CONTACT_EMAIL_1; jj < ii; jj++) {
					g_object_set (contact, e_contact_field_name (jj), "tmp-value", NULL);
				}

				temporares_from = E_CONTACT_EMAIL_1;
				n_temporares = jj - temporares_from;
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
				temporares_from = ii - 1;
				n_temporares = 1;
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
				temporares_from = ii - 2;
				n_temporares = 2;
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

			while (n_temporares > 0) {
				e_contact_set (contact, temporares_from + n_temporares - 1, NULL);
				n_temporares--;
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
			   ii == E_CONTACT_IM_MATRIX ||
			   ii == E_CONTACT_IMPP ||
			   ii == E_CONTACT_LANG ||
			   ii == E_CONTACT_MEMBER ||
			   ii == E_CONTACT_RELATED ||
			   ii == E_CONTACT_EXPERTISE ||
			   ii == E_CONTACT_HOBBY ||
			   ii == E_CONTACT_INTEREST ||
			   ii == E_CONTACT_ORG_DIRECTORY) {
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
		} else if (ii == E_CONTACT_GENDER) {
			EContactGenderSex expected_sex[] = {
				E_CONTACT_GENDER_SEX_UNKNOWN,
				E_CONTACT_GENDER_SEX_NOT_SET,
				E_CONTACT_GENDER_SEX_MALE,
				E_CONTACT_GENDER_SEX_FEMALE,
				E_CONTACT_GENDER_SEX_OTHER,
				E_CONTACT_GENDER_SEX_NOT_APPLICABLE
			};
			const gchar *expected_identity[] = {
				NULL,
				"",
				"identity",
				"Something Special"
			};
			guint es, ei;

			for (es = 0; es < G_N_ELEMENTS (expected_sex); es++) {
				for (ei = 0; ei < G_N_ELEMENTS (expected_identity); ei++) {
					EContactGender *stored = NULL, expected;

					expected.sex = expected_sex[es];
					expected.identity = (gchar *) expected_identity[ei];

					g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
					g_assert_null (stored);
					g_object_set (contact, e_contact_field_name (ii), &expected, NULL);
					g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
					g_assert_nonnull (stored);
					g_assert_true (stored != &expected);
					g_assert_cmpint (stored->sex, ==, expected.sex);
					if (expected.identity && !*expected.identity)
						g_assert_cmpstr (stored->identity, ==, NULL);
					else
						g_assert_cmpstr (stored->identity, ==, expected.identity);
					e_contact_gender_free (stored);

					g_object_set (contact, e_contact_field_name (ii), NULL, NULL);
					g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
					g_assert_null (stored);
				}
			}
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

			/* the vCard 4.0 adds also 'kind', thus drop it */
			if (ii == E_CONTACT_IS_LIST)
				e_vcard_remove_attributes (E_VCARD (contact), NULL, EVC_KIND);
		} else if (ii == E_CONTACT_BIRTH_DATE ||
			   ii == E_CONTACT_ANNIVERSARY ||
			   ii == E_CONTACT_DEATHDATE) {
			EContactDate *stored = NULL, *expected;

			expected = e_contact_date_new ();
			expected->year = 2000 + ii;
			if (ii == E_CONTACT_DEATHDATE) {
				expected->month = 1 + 3;
				expected->day = 3 + 3;
			} else {
				expected->month = 1 + ii - E_CONTACT_BIRTH_DATE;
				expected->day = 3 + ii - E_CONTACT_BIRTH_DATE;
			}

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
		} else if (ii == E_CONTACT_CREATED) {
			EContactDateTime *stored = NULL, *expected;

			expected = e_contact_date_time_new ();
			expected->year = 2000 + ii;
			expected->month = 8;
			expected->day = 4;
			expected->hour = 15;
			expected->minute = 26;
			expected->second = 37;
			expected->utc_offset = 1230;

			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);
			g_object_set (contact, e_contact_field_name (ii), expected, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_nonnull (stored);
			g_assert_true (stored != expected);
			g_assert_cmpuint (stored->year, ==, expected->year);
			g_assert_cmpuint (stored->month, ==, expected->month);
			g_assert_cmpuint (stored->day, ==, expected->day);
			g_assert_cmpuint (stored->hour, ==, expected->hour);
			g_assert_cmpuint (stored->minute, ==, expected->minute);
			g_assert_cmpuint (stored->second, ==, expected->second);
			e_contact_date_time_free (stored);

			g_object_set (contact, e_contact_field_name (ii), NULL, NULL);
			g_object_get (contact, e_contact_field_name (ii), &stored, NULL);
			g_assert_null (stored);

			e_contact_date_time_free (expected);
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
}

static void
test_contact_gobject_props_30 (void)
{
	EContact *contact;

	contact = e_contact_new ();
	test_contact_gobject_props (contact);
	g_clear_object (&contact);

	contact = e_contact_new ();
	e_vcard_add_attribute_with_value (E_VCARD (contact), e_vcard_attribute_new (NULL, EVC_VERSION), "3.0");
	test_contact_gobject_props (contact);
	g_clear_object (&contact);
}

static void
test_contact_gobject_props_40 (void)
{
	EContact *contact;

	contact = e_contact_new ();
	e_vcard_add_attribute_with_value (E_VCARD (contact), e_vcard_attribute_new (NULL, EVC_VERSION), "4.0");
	test_contact_gobject_props (contact);
	g_clear_object (&contact);
}

static EVCardAttribute *
test_contact_find_typed_attribute (EVCard *vcard,
				   const gchar *name,
				   const gchar *type,
				   guint nth)
{
	GList *link;

	for (link = e_vcard_get_attributes (vcard); link; link = g_list_next (link)) {
		EVCardAttribute *attr = link->data;

		if (g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), name) == 0 &&
		    e_vcard_attribute_has_type (attr, type)) {
			if (nth == 0)
				return attr;
			nth--;
		}
	}

	return NULL;
}

static void
test_contact_quirks_adr (void)
{
	struct _parts {
		EContactField addr_field;
		EContactField label_field;
		const gchar *vcard_type;
	} parts[] = {
		{ E_CONTACT_ADDRESS_HOME, E_CONTACT_ADDRESS_LABEL_HOME, "HOME" },
		{ E_CONTACT_ADDRESS_WORK, E_CONTACT_ADDRESS_LABEL_WORK, "WORK" },
		{ E_CONTACT_ADDRESS_OTHER, E_CONTACT_ADDRESS_LABEL_OTHER, "OTHER" }
	};
	EContact *contact;
	EVCard *vcard;
	EVCardAttribute *attr;
	EContactAddress expected_addr = { 0, }, *addr;
	GList *values;
	gchar *label;
	guint ii, jj;

	/* vCard 3.0 */
	contact = e_contact_new ();
	vcard = E_VCARD (contact);
	e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_VERSION), "3.0");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
	g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
	g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));
	g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_HOME));
	g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_WORK));
	g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_OTHER));

	expected_addr.po = (gchar *) "po1";
	expected_addr.ext = (gchar *) "";
	expected_addr.street = (gchar *) "street1";
	expected_addr.locality = (gchar *) "locality1";
	expected_addr.region = (gchar *) "region1";
	expected_addr.code = (gchar *) "code1";
	expected_addr.country = (gchar *) "country1";

	for (ii = 0; ii < G_N_ELEMENTS (parts); ii++) {
		EContactField addr_field = parts[ii].addr_field;
		EContactField label_field = parts[ii].label_field;
		const gchar *vcard_type = parts[ii].vcard_type;

		e_contact_set (contact, addr_field, &expected_addr);
		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
		g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

		attr = test_contact_find_typed_attribute (vcard, EVC_ADR, vcard_type, 0);
		g_assert_nonnull (attr);

		addr = e_contact_get (contact, addr_field);
		g_assert_nonnull (addr);
		g_assert_cmpstr (addr->po, ==, expected_addr.po);
		g_assert_cmpstr (addr->ext, ==, expected_addr.ext);
		g_assert_cmpstr (addr->street, ==, expected_addr.street);
		g_assert_cmpstr (addr->locality, ==, expected_addr.locality);
		g_assert_cmpstr (addr->region, ==, expected_addr.region);
		g_assert_cmpstr (addr->code, ==, expected_addr.code);
		g_assert_cmpstr (addr->country, ==, expected_addr.country);
		g_clear_pointer (&addr, e_contact_address_free);

		e_contact_set (contact, label_field, "label1");
		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 3);
		g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
		if (label_field != E_CONTACT_ADDRESS_LABEL_HOME)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
		if (label_field != E_CONTACT_ADDRESS_LABEL_WORK)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
		if (label_field != E_CONTACT_ADDRESS_LABEL_OTHER)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

		attr = test_contact_find_typed_attribute (vcard, EVC_ADR, vcard_type, 0);
		g_assert_nonnull (attr);

		attr = test_contact_find_typed_attribute (vcard, EVC_LABEL, vcard_type, 0);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, "label1");

		label = e_contact_get (contact, label_field);
		g_assert_cmpstr (label, ==, "label1");
		g_clear_pointer (&label, g_free);

		/* remove label first */
		e_contact_set (contact, label_field, NULL);
		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
		g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

		attr = test_contact_find_typed_attribute (vcard, EVC_ADR, vcard_type, 0);
		g_assert_nonnull (attr);

		/* add label and remove address (in vCard 3.0 the label will stay) */
		e_contact_set (contact, label_field, "label1");
		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 3);
		e_contact_set (contact, addr_field, NULL);
		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
		g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
		if (label_field != E_CONTACT_ADDRESS_LABEL_HOME)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
		if (label_field != E_CONTACT_ADDRESS_LABEL_WORK)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
		if (label_field != E_CONTACT_ADDRESS_LABEL_OTHER)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

		attr = test_contact_find_typed_attribute (vcard, EVC_LABEL, vcard_type, 0);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, "label1");

		label = e_contact_get (contact, label_field);
		g_assert_cmpstr (label, ==, "label1");
		g_clear_pointer (&label, g_free);

		e_contact_set (contact, label_field, NULL);
		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
		g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	}

	expected_addr.po = (gchar *) "po2";
	expected_addr.ext = (gchar *) "";
	expected_addr.street = (gchar *) "street2";
	expected_addr.locality = (gchar *) "locality2";
	expected_addr.region = (gchar *) "region2";
	expected_addr.code = (gchar *) "code2";
	expected_addr.country = (gchar *) "country2";

	/* try when one of them (home/work/other) is set */
	for (jj = 0; jj < G_N_ELEMENTS (parts); jj++) {
		EContactField preset_addr_field = parts[jj].addr_field;
		EContactField preset_label_field = parts[jj].label_field;
		EContactAddress preset_addr = { 0, };

		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_HOME));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_WORK));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_OTHER));

		preset_addr.po = (gchar *) "preset-po";
		preset_addr.ext = (gchar *) "";
		preset_addr.street = (gchar *) "preset-street";
		preset_addr.locality = (gchar *) "preset-locality";
		preset_addr.region = (gchar *) "preset-region";
		preset_addr.code = (gchar *) "preset-code";
		preset_addr.country = (gchar *) "preset-country";

		e_contact_set (contact, preset_addr_field, &preset_addr);
		e_contact_set (contact, preset_label_field, "preset-label");
		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 3);
		g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

		addr = e_contact_get (contact, preset_addr_field);
		g_assert_nonnull (addr);
		g_assert_cmpstr (addr->po, ==, preset_addr.po);
		g_assert_cmpstr (addr->ext, ==, preset_addr.ext);
		g_assert_cmpstr (addr->street, ==, preset_addr.street);
		g_assert_cmpstr (addr->locality, ==, preset_addr.locality);
		g_assert_cmpstr (addr->region, ==, preset_addr.region);
		g_assert_cmpstr (addr->code, ==, preset_addr.code);
		g_assert_cmpstr (addr->country, ==, preset_addr.country);
		g_clear_pointer (&addr, e_contact_address_free);

		if (preset_label_field != E_CONTACT_ADDRESS_LABEL_HOME)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
		if (preset_label_field != E_CONTACT_ADDRESS_LABEL_WORK)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
		if (preset_label_field != E_CONTACT_ADDRESS_LABEL_OTHER)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

		label = e_contact_get (contact, preset_label_field);
		g_assert_cmpstr (label, ==, "preset-label");
		g_clear_pointer (&label, g_free);

		for (ii = 0; ii < G_N_ELEMENTS (parts); ii++) {
			EContactField addr_field = parts[ii].addr_field;
			EContactField label_field = parts[ii].label_field;
			const gchar *vcard_type = parts[ii].vcard_type;

			if (ii == jj)
				continue;

			e_contact_set (contact, addr_field, &expected_addr);
			g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 4);
			g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

			attr = test_contact_find_typed_attribute (vcard, EVC_ADR, vcard_type, 0);
			g_assert_nonnull (attr);

			addr = e_contact_get (contact, addr_field);
			g_assert_nonnull (addr);
			g_assert_cmpstr (addr->po, ==, expected_addr.po);
			g_assert_cmpstr (addr->ext, ==, expected_addr.ext);
			g_assert_cmpstr (addr->street, ==, expected_addr.street);
			g_assert_cmpstr (addr->locality, ==, expected_addr.locality);
			g_assert_cmpstr (addr->region, ==, expected_addr.region);
			g_assert_cmpstr (addr->code, ==, expected_addr.code);
			g_assert_cmpstr (addr->country, ==, expected_addr.country);
			g_clear_pointer (&addr, e_contact_address_free);

			e_contact_set (contact, label_field, "label2");
			g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 5);
			g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
			if (label_field != E_CONTACT_ADDRESS_LABEL_HOME && preset_label_field != E_CONTACT_ADDRESS_LABEL_HOME)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
			if (label_field != E_CONTACT_ADDRESS_LABEL_WORK && preset_label_field != E_CONTACT_ADDRESS_LABEL_WORK)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
			if (label_field != E_CONTACT_ADDRESS_LABEL_OTHER && preset_label_field != E_CONTACT_ADDRESS_LABEL_OTHER)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

			attr = test_contact_find_typed_attribute (vcard, EVC_ADR, vcard_type, 0);
			g_assert_nonnull (attr);

			attr = test_contact_find_typed_attribute (vcard, EVC_LABEL, vcard_type, 0);
			g_assert_nonnull (attr);
			values = e_vcard_attribute_get_values (attr);
			g_assert_cmpuint (g_list_length (values), ==, 1);
			g_assert_cmpstr (values->data, ==, "label2");

			label = e_contact_get (contact, label_field);
			g_assert_cmpstr (label, ==, "label2");
			g_clear_pointer (&label, g_free);

			/* remove label first */
			e_contact_set (contact, label_field, NULL);
			g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 4);
			g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
			if (preset_label_field != E_CONTACT_ADDRESS_LABEL_HOME)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
			if (preset_label_field != E_CONTACT_ADDRESS_LABEL_WORK)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
			if (preset_label_field != E_CONTACT_ADDRESS_LABEL_OTHER)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

			attr = test_contact_find_typed_attribute (vcard, EVC_ADR, vcard_type, 0);
			g_assert_nonnull (attr);

			/* add label and remove address (in vCard 3.0 the label will stay) */
			e_contact_set (contact, label_field, "label3");
			g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 5);
			e_contact_set (contact, addr_field, NULL);
			g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 4);
			g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
			if (label_field != E_CONTACT_ADDRESS_LABEL_HOME && preset_label_field != E_CONTACT_ADDRESS_LABEL_HOME)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
			if (label_field != E_CONTACT_ADDRESS_LABEL_WORK && preset_label_field != E_CONTACT_ADDRESS_LABEL_WORK)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
			if (label_field != E_CONTACT_ADDRESS_LABEL_OTHER && preset_label_field != E_CONTACT_ADDRESS_LABEL_OTHER)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

			attr = test_contact_find_typed_attribute (vcard, EVC_LABEL, vcard_type, 0);
			g_assert_nonnull (attr);
			values = e_vcard_attribute_get_values (attr);
			g_assert_cmpuint (g_list_length (values), ==, 1);
			g_assert_cmpstr (values->data, ==, "label3");

			label = e_contact_get (contact, label_field);
			g_assert_cmpstr (label, ==, "label3");
			g_clear_pointer (&label, g_free);

			e_contact_set (contact, label_field, NULL);
			g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 3);
			g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
			g_assert_null (e_contact_get (contact, addr_field));
			g_assert_null (e_contact_get (contact, label_field));
		}

		addr = e_contact_get (contact, preset_addr_field);
		g_assert_nonnull (addr);
		g_assert_cmpstr (addr->po, ==, preset_addr.po);
		g_assert_cmpstr (addr->ext, ==, preset_addr.ext);
		g_assert_cmpstr (addr->street, ==, preset_addr.street);
		g_assert_cmpstr (addr->locality, ==, preset_addr.locality);
		g_assert_cmpstr (addr->region, ==, preset_addr.region);
		g_assert_cmpstr (addr->code, ==, preset_addr.code);
		g_assert_cmpstr (addr->country, ==, preset_addr.country);
		g_clear_pointer (&addr, e_contact_address_free);

		if (preset_label_field != E_CONTACT_ADDRESS_LABEL_HOME)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
		if (preset_label_field != E_CONTACT_ADDRESS_LABEL_WORK)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
		if (preset_label_field != E_CONTACT_ADDRESS_LABEL_OTHER)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

		label = e_contact_get (contact, preset_label_field);
		g_assert_cmpstr (label, ==, "preset-label");
		g_clear_pointer (&label, g_free);

		e_contact_set (contact, preset_addr_field, NULL);
		e_contact_set (contact, preset_label_field, NULL);
		g_assert_null (e_contact_get (contact, preset_addr_field));
		g_assert_null (e_contact_get (contact, preset_label_field));
	}

	g_clear_object (&contact);

	/* vCard 4.0 */
	contact = e_contact_new ();
	vcard = E_VCARD (contact);
	e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_VERSION), "4.0");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
	g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
	g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));
	g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_HOME));
	g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_WORK));
	g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_OTHER));

	expected_addr.po = (gchar *) "po1";
	expected_addr.ext = (gchar *) "";
	expected_addr.street = (gchar *) "street1";
	expected_addr.locality = (gchar *) "locality1";
	expected_addr.region = (gchar *) "region1";
	expected_addr.code = (gchar *) "code1";
	expected_addr.country = (gchar *) "country1";

	for (ii = 0; ii < G_N_ELEMENTS (parts); ii++) {
		EContactField addr_field = parts[ii].addr_field;
		EContactField label_field = parts[ii].label_field;
		const gchar *vcard_type = parts[ii].vcard_type;

		e_contact_set (contact, addr_field, &expected_addr);
		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
		g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

		attr = test_contact_find_typed_attribute (vcard, EVC_ADR, vcard_type, 0);
		g_assert_nonnull (attr);

		addr = e_contact_get (contact, addr_field);
		g_assert_nonnull (addr);
		g_assert_cmpstr (addr->po, ==, expected_addr.po);
		g_assert_cmpstr (addr->ext, ==, expected_addr.ext);
		g_assert_cmpstr (addr->street, ==, expected_addr.street);
		g_assert_cmpstr (addr->locality, ==, expected_addr.locality);
		g_assert_cmpstr (addr->region, ==, expected_addr.region);
		g_assert_cmpstr (addr->code, ==, expected_addr.code);
		g_assert_cmpstr (addr->country, ==, expected_addr.country);
		g_clear_pointer (&addr, e_contact_address_free);

		e_contact_set (contact, label_field, "label1");
		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
		g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
		if (label_field != E_CONTACT_ADDRESS_LABEL_HOME)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
		if (label_field != E_CONTACT_ADDRESS_LABEL_WORK)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
		if (label_field != E_CONTACT_ADDRESS_LABEL_OTHER)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

		attr = test_contact_find_typed_attribute (vcard, EVC_ADR, vcard_type, 0);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_param (attr, EVC_LABEL);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, "label1");

		attr = test_contact_find_typed_attribute (vcard, EVC_LABEL, vcard_type, 0);
		g_assert_null (attr);

		label = e_contact_get (contact, label_field);
		g_assert_cmpstr (label, ==, "label1");
		g_clear_pointer (&label, g_free);

		/* remove label first */
		e_contact_set (contact, label_field, NULL);
		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
		g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

		attr = test_contact_find_typed_attribute (vcard, EVC_ADR, vcard_type, 0);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_param (attr, EVC_LABEL);
		g_assert_cmpuint (g_list_length (values), ==, 0);

		/* add label and remove address (in vCard 4.0 the label will be removed) */
		e_contact_set (contact, label_field, "label1");
		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
		e_contact_set (contact, addr_field, NULL);
		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
		g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

		attr = test_contact_find_typed_attribute (vcard, EVC_LABEL, vcard_type, 0);
		g_assert_null (attr);

		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
		g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	}

	expected_addr.po = (gchar *) "po2";
	expected_addr.ext = (gchar *) "";
	expected_addr.street = (gchar *) "street2";
	expected_addr.locality = (gchar *) "locality2";
	expected_addr.region = (gchar *) "region2";
	expected_addr.code = (gchar *) "code2";
	expected_addr.country = (gchar *) "country2";

	/* try when one of them (home/work/other) is set */
	for (jj = 0; jj < G_N_ELEMENTS (parts); jj++) {
		EContactField preset_addr_field = parts[jj].addr_field;
		EContactField preset_label_field = parts[jj].label_field;
		EContactAddress preset_addr = { 0, };

		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_HOME));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_WORK));
		g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_OTHER));

		preset_addr.po = (gchar *) "preset-po";
		preset_addr.ext = (gchar *) "";
		preset_addr.street = (gchar *) "preset-street";
		preset_addr.locality = (gchar *) "preset-locality";
		preset_addr.region = (gchar *) "preset-region";
		preset_addr.code = (gchar *) "preset-code";
		preset_addr.country = (gchar *) "preset-country";

		e_contact_set (contact, preset_addr_field, &preset_addr);
		e_contact_set (contact, preset_label_field, "preset-label");
		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
		g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

		addr = e_contact_get (contact, preset_addr_field);
		g_assert_nonnull (addr);
		g_assert_cmpstr (addr->po, ==, preset_addr.po);
		g_assert_cmpstr (addr->ext, ==, preset_addr.ext);
		g_assert_cmpstr (addr->street, ==, preset_addr.street);
		g_assert_cmpstr (addr->locality, ==, preset_addr.locality);
		g_assert_cmpstr (addr->region, ==, preset_addr.region);
		g_assert_cmpstr (addr->code, ==, preset_addr.code);
		g_assert_cmpstr (addr->country, ==, preset_addr.country);
		g_clear_pointer (&addr, e_contact_address_free);

		if (preset_label_field != E_CONTACT_ADDRESS_LABEL_HOME)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
		if (preset_label_field != E_CONTACT_ADDRESS_LABEL_WORK)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
		if (preset_label_field != E_CONTACT_ADDRESS_LABEL_OTHER)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

		label = e_contact_get (contact, preset_label_field);
		g_assert_cmpstr (label, ==, "preset-label");
		g_clear_pointer (&label, g_free);

		for (ii = 0; ii < G_N_ELEMENTS (parts); ii++) {
			EContactField addr_field = parts[ii].addr_field;
			EContactField label_field = parts[ii].label_field;
			const gchar *vcard_type = parts[ii].vcard_type;

			if (ii == jj)
				continue;

			e_contact_set (contact, addr_field, &expected_addr);
			g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 3);
			g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

			attr = test_contact_find_typed_attribute (vcard, EVC_ADR, vcard_type, 0);
			g_assert_nonnull (attr);

			addr = e_contact_get (contact, addr_field);
			g_assert_nonnull (addr);
			g_assert_cmpstr (addr->po, ==, expected_addr.po);
			g_assert_cmpstr (addr->ext, ==, expected_addr.ext);
			g_assert_cmpstr (addr->street, ==, expected_addr.street);
			g_assert_cmpstr (addr->locality, ==, expected_addr.locality);
			g_assert_cmpstr (addr->region, ==, expected_addr.region);
			g_assert_cmpstr (addr->code, ==, expected_addr.code);
			g_assert_cmpstr (addr->country, ==, expected_addr.country);
			g_clear_pointer (&addr, e_contact_address_free);

			e_contact_set (contact, label_field, "label2");
			g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 3);
			g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
			if (label_field != E_CONTACT_ADDRESS_LABEL_HOME && preset_label_field != E_CONTACT_ADDRESS_LABEL_HOME)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
			if (label_field != E_CONTACT_ADDRESS_LABEL_WORK && preset_label_field != E_CONTACT_ADDRESS_LABEL_WORK)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
			if (label_field != E_CONTACT_ADDRESS_LABEL_OTHER && preset_label_field != E_CONTACT_ADDRESS_LABEL_OTHER)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

			attr = test_contact_find_typed_attribute (vcard, EVC_ADR, vcard_type, 0);
			g_assert_nonnull (attr);
			values = e_vcard_attribute_get_param (attr, EVC_LABEL);
			g_assert_cmpuint (g_list_length (values), ==, 1);
			g_assert_cmpstr (values->data, ==, "label2");

			attr = test_contact_find_typed_attribute (vcard, EVC_LABEL, vcard_type, 0);
			g_assert_null (attr);

			label = e_contact_get (contact, label_field);
			g_assert_cmpstr (label, ==, "label2");
			g_clear_pointer (&label, g_free);

			/* remove label first */
			e_contact_set (contact, label_field, NULL);
			g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 3);
			g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
			if (preset_label_field != E_CONTACT_ADDRESS_LABEL_HOME)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
			if (preset_label_field != E_CONTACT_ADDRESS_LABEL_WORK)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
			if (preset_label_field != E_CONTACT_ADDRESS_LABEL_OTHER)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

			attr = test_contact_find_typed_attribute (vcard, EVC_ADR, vcard_type, 0);
			g_assert_nonnull (attr);
			values = e_vcard_attribute_get_param (attr, EVC_LABEL);
			g_assert_cmpuint (g_list_length (values), ==, 0);

			/* add label and remove address (in vCard 4.0 the label will be removed) */
			e_contact_set (contact, label_field, "label3");
			g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 3);
			e_contact_set (contact, addr_field, NULL);
			g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
			g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
			if (label_field != E_CONTACT_ADDRESS_LABEL_HOME && preset_label_field != E_CONTACT_ADDRESS_LABEL_HOME)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
			if (label_field != E_CONTACT_ADDRESS_LABEL_WORK && preset_label_field != E_CONTACT_ADDRESS_LABEL_WORK)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
			if (label_field != E_CONTACT_ADDRESS_LABEL_OTHER && preset_label_field != E_CONTACT_ADDRESS_LABEL_OTHER)
				g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

			attr = test_contact_find_typed_attribute (vcard, EVC_LABEL, vcard_type, 0);
			g_assert_null (attr);

			label = e_contact_get (contact, label_field);
			g_assert_cmpstr (label, ==, NULL);
			g_clear_pointer (&label, g_free);

			e_contact_set (contact, label_field, NULL);
			g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
			g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
			g_assert_null (e_contact_get (contact, addr_field));
			g_assert_null (e_contact_get (contact, label_field));
		}

		addr = e_contact_get (contact, preset_addr_field);
		g_assert_nonnull (addr);
		g_assert_cmpstr (addr->po, ==, preset_addr.po);
		g_assert_cmpstr (addr->ext, ==, preset_addr.ext);
		g_assert_cmpstr (addr->street, ==, preset_addr.street);
		g_assert_cmpstr (addr->locality, ==, preset_addr.locality);
		g_assert_cmpstr (addr->region, ==, preset_addr.region);
		g_assert_cmpstr (addr->code, ==, preset_addr.code);
		g_assert_cmpstr (addr->country, ==, preset_addr.country);
		g_clear_pointer (&addr, e_contact_address_free);

		if (preset_label_field != E_CONTACT_ADDRESS_LABEL_HOME)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_HOME));
		if (preset_label_field != E_CONTACT_ADDRESS_LABEL_WORK)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_WORK));
		if (preset_label_field != E_CONTACT_ADDRESS_LABEL_OTHER)
			g_assert_null (e_contact_get (contact, E_CONTACT_ADDRESS_LABEL_OTHER));

		label = e_contact_get (contact, preset_label_field);
		g_assert_cmpstr (label, ==, "preset-label");
		g_clear_pointer (&label, g_free);

		e_contact_set (contact, preset_addr_field, NULL);
		e_contact_set (contact, preset_label_field, NULL);
		g_assert_null (e_contact_get (contact, preset_addr_field));
		g_assert_null (e_contact_get (contact, preset_label_field));
	}

	g_clear_object (&contact);
}

static void
test_contact_quirks_anniversary (void)
{
	EContact *contact;
	EVCard *vcard;
	EVCardAttribute *attr;
	EContactDate expected_dt = { 0, }, *dt = NULL;
	GList *values;

	/* vCard 3.0 */
	contact = e_contact_new ();
	vcard = E_VCARD (contact);
	e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_VERSION), "3.0");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);

	expected_dt.year = 1987;
	expected_dt.month = 6;
	expected_dt.day = 25;

	e_contact_set (contact, E_CONTACT_ANNIVERSARY, &expected_dt);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_ANNIVERSARY));

	attr = e_vcard_get_attribute (vcard, EVC_X_ANNIVERSARY);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "1987-06-25");

	dt = e_contact_get (contact, E_CONTACT_ANNIVERSARY);
	g_assert_nonnull (dt);
	g_assert_cmpint (dt->year, ==, expected_dt.year);
	g_assert_cmpint (dt->month, ==, expected_dt.month);
	g_assert_cmpint (dt->day, ==, expected_dt.day);
	g_clear_pointer (&dt, e_contact_date_free);

	expected_dt.year = 1749;
	expected_dt.month = 5;
	expected_dt.day = 23;

	e_contact_set (contact, E_CONTACT_ANNIVERSARY, &expected_dt);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_ANNIVERSARY));

	attr = e_vcard_get_attribute (vcard, EVC_X_ANNIVERSARY);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "1749-05-23");

	dt = e_contact_get (contact, E_CONTACT_ANNIVERSARY);
	g_assert_nonnull (dt);
	g_assert_cmpint (dt->year, ==, expected_dt.year);
	g_assert_cmpint (dt->month, ==, expected_dt.month);
	g_assert_cmpint (dt->day, ==, expected_dt.day);
	g_clear_pointer (&dt, e_contact_date_free);

	e_contact_set (contact, E_CONTACT_ANNIVERSARY, NULL);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_ANNIVERSARY));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_X_ANNIVERSARY));

	g_clear_object (&contact);

	/* vCard 4.0 */
	contact = e_contact_new ();
	vcard = E_VCARD (contact);
	e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_VERSION), "4.0");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);

	expected_dt.year = 1987;
	expected_dt.month = 6;
	expected_dt.day = 25;

	e_contact_set (contact, E_CONTACT_ANNIVERSARY, &expected_dt);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_X_ANNIVERSARY));

	attr = e_vcard_get_attribute (vcard, EVC_ANNIVERSARY);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "19870625");

	dt = e_contact_get (contact, E_CONTACT_ANNIVERSARY);
	g_assert_nonnull (dt);
	g_assert_cmpint (dt->year, ==, expected_dt.year);
	g_assert_cmpint (dt->month, ==, expected_dt.month);
	g_assert_cmpint (dt->day, ==, expected_dt.day);
	g_clear_pointer (&dt, e_contact_date_free);

	expected_dt.year = 1749;
	expected_dt.month = 5;
	expected_dt.day = 23;

	e_contact_set (contact, E_CONTACT_ANNIVERSARY, &expected_dt);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_X_ANNIVERSARY));

	attr = e_vcard_get_attribute (vcard, EVC_ANNIVERSARY);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "17490523");

	dt = e_contact_get (contact, E_CONTACT_ANNIVERSARY);
	g_assert_nonnull (dt);
	g_assert_cmpint (dt->year, ==, expected_dt.year);
	g_assert_cmpint (dt->month, ==, expected_dt.month);
	g_assert_cmpint (dt->day, ==, expected_dt.day);
	g_clear_pointer (&dt, e_contact_date_free);

	e_contact_set (contact, E_CONTACT_ANNIVERSARY, NULL);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_ANNIVERSARY));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_X_ANNIVERSARY));

	expected_dt.year = 0; /* out of bounds */
	expected_dt.month = 5;
	expected_dt.day = 23;

	e_contact_set (contact, E_CONTACT_ANNIVERSARY, &expected_dt);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_X_ANNIVERSARY));

	attr = e_vcard_get_attribute (vcard, EVC_ANNIVERSARY);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "--0523");

	dt = e_contact_get (contact, E_CONTACT_ANNIVERSARY);
	g_assert_nonnull (dt);
	g_assert_cmpint (dt->year, ==, expected_dt.year);
	g_assert_cmpint (dt->month, ==, expected_dt.month);
	g_assert_cmpint (dt->day, ==, expected_dt.day);
	g_clear_pointer (&dt, e_contact_date_free);

	expected_dt.year = 1987;
	expected_dt.month = 0; /* out of bounds */
	expected_dt.day = 0; /* out of bounds */

	e_contact_set (contact, E_CONTACT_ANNIVERSARY, &expected_dt);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_X_ANNIVERSARY));

	attr = e_vcard_get_attribute (vcard, EVC_ANNIVERSARY);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "1987");

	dt = e_contact_get (contact, E_CONTACT_ANNIVERSARY);
	g_assert_nonnull (dt);
	g_assert_cmpint (dt->year, ==, expected_dt.year);
	g_assert_cmpint (dt->month, ==, expected_dt.month);
	g_assert_cmpint (dt->day, ==, expected_dt.day);
	g_clear_pointer (&dt, e_contact_date_free);

	g_clear_object (&contact);
}

static void
test_contact_quirks_kind (void)
{
	EContact *contact;
	EVCard *vcard;
	EVCardAttribute *attr;
	gchar *value = NULL;
	GList *values;

	/* vCard 3.0 */
	contact = e_contact_new ();
	vcard = E_VCARD (contact);
	e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_VERSION), "3.0");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);

	e_contact_set (contact, E_CONTACT_KIND, "other");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_KIND));

	attr = e_vcard_get_attribute (vcard, EVC_X_EVOLUTION_KIND);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "other");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "other");
	g_clear_pointer (&value, g_free);
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	e_contact_set (contact, E_CONTACT_KIND, NULL);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	/* add as new */
	e_contact_set (contact, E_CONTACT_KIND, "group");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_KIND));

	attr = e_vcard_get_attribute (vcard, EVC_X_LIST);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "TRUE");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "group");
	g_clear_pointer (&value, g_free);
	g_assert_nonnull (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* change to other */
	e_contact_set (contact, E_CONTACT_KIND, "other");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_KIND));

	attr = e_vcard_get_attribute (vcard, EVC_X_EVOLUTION_KIND);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "other");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "other");
	g_clear_pointer (&value, g_free);
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* change to group */
	e_contact_set (contact, E_CONTACT_KIND, "group");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 3);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_KIND));

	attr = e_vcard_get_attribute (vcard, EVC_X_EVOLUTION_KIND);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "group");

	attr = e_vcard_get_attribute (vcard, EVC_X_LIST);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "TRUE");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "group");
	g_clear_pointer (&value, g_free);
	g_assert_nonnull (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* set with is-list from 'group' */
	e_contact_set (contact, E_CONTACT_IS_LIST, NULL);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, NULL);
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* set with is-list from none to none */
	e_contact_set (contact, E_CONTACT_IS_LIST, NULL);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* set with is-list from none to true */
	e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (TRUE));
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = e_vcard_get_attribute (vcard, EVC_X_LIST);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "TRUE");
	g_assert_nonnull (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* set with is-list from true to none */
	e_contact_set (contact, E_CONTACT_IS_LIST, NULL);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* set kind to other when is-list is true */
	e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (TRUE));
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_X_LIST));

	e_contact_set (contact, E_CONTACT_KIND, "other");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_X_LIST));

	attr = e_vcard_get_attribute (vcard, EVC_X_EVOLUTION_KIND);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "other");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "other");
	g_clear_pointer (&value, g_free);
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* set kind to group when is-list is true */
	e_contact_set (contact, E_CONTACT_KIND, NULL);
	e_contact_set (contact, E_CONTACT_IS_LIST, NULL);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (TRUE));
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_X_LIST));
	g_assert_nonnull (e_contact_get (contact, E_CONTACT_IS_LIST));

	e_contact_set (contact, E_CONTACT_KIND, "group");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_X_EVOLUTION_KIND));

	attr = e_vcard_get_attribute (vcard, EVC_X_LIST);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "TRUE");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "group");
	g_clear_pointer (&value, g_free);
	g_assert_nonnull (e_contact_get (contact, E_CONTACT_IS_LIST));

	g_clear_object (&contact);

	/* vCard 4.0 */
	contact = e_contact_new ();
	vcard = E_VCARD (contact);
	e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_VERSION), "4.0");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);

	e_contact_set (contact, E_CONTACT_KIND, "other");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_X_EVOLUTION_KIND));

	attr = e_vcard_get_attribute (vcard, EVC_KIND);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "other");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "other");
	g_clear_pointer (&value, g_free);
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	e_contact_set (contact, E_CONTACT_KIND, NULL);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, NULL);
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* add as new */
	e_contact_set (contact, E_CONTACT_KIND, "group");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = e_vcard_get_attribute (vcard, EVC_KIND);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "group");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "group");
	g_clear_pointer (&value, g_free);
	g_assert_nonnull (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* change to other */
	e_contact_set (contact, E_CONTACT_KIND, "other");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = e_vcard_get_attribute (vcard, EVC_KIND);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "other");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "other");
	g_clear_pointer (&value, g_free);
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* change to group */
	e_contact_set (contact, E_CONTACT_KIND, "group");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = e_vcard_get_attribute (vcard, EVC_KIND);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "group");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "group");
	g_clear_pointer (&value, g_free);
	g_assert_nonnull (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* set with is-list from 'group' */
	e_contact_set (contact, E_CONTACT_IS_LIST, NULL);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	attr = e_vcard_get_attribute (vcard, EVC_KIND);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "individual");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "individual");
	g_clear_pointer (&value, g_free);
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* set with is-list from none to none */
	e_contact_set (contact, E_CONTACT_KIND, NULL);
	e_contact_set (contact, E_CONTACT_IS_LIST, NULL);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, NULL);

	/* set with is-list from none to true */
	e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (TRUE));
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = e_vcard_get_attribute (vcard, EVC_KIND);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "group");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "group");
	g_clear_pointer (&value, g_free);
	g_assert_nonnull (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* set with is-list from true to none */
	e_contact_set (contact, E_CONTACT_IS_LIST, NULL);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	attr = e_vcard_get_attribute (vcard, EVC_KIND);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "individual");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "individual");
	g_clear_pointer (&value, g_free);
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* set kind to other when is-list is true */
	e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (TRUE));
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_KIND));

	e_contact_set (contact, E_CONTACT_KIND, "other");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = e_vcard_get_attribute (vcard, EVC_KIND);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "other");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "other");
	g_clear_pointer (&value, g_free);
	g_assert_null (e_contact_get (contact, E_CONTACT_IS_LIST));

	/* set kind to group when is-list is true */
	e_contact_set (contact, E_CONTACT_KIND, NULL);
	e_contact_set (contact, E_CONTACT_IS_LIST, NULL);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (TRUE));
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_KIND));
	g_assert_nonnull (e_contact_get (contact, E_CONTACT_IS_LIST));

	e_contact_set (contact, E_CONTACT_KIND, "group");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = e_vcard_get_attribute (vcard, EVC_KIND);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "group");

	value = e_contact_get (contact, E_CONTACT_KIND);
	g_assert_cmpstr (value, ==, "group");
	g_clear_pointer (&value, g_free);
	g_assert_nonnull (e_contact_get (contact, E_CONTACT_IS_LIST));

	g_clear_object (&contact);
}

/* test only some of them, they should work the same otherwise */
static void
test_contact_quirks_impp (void)
{
	EContact *contact;
	EVCard *vcard;
	EVCardAttribute *attr;
	gchar *value = NULL;
	GList *values;

	/* vCard 3.0 */
	contact = e_contact_new ();
	vcard = E_VCARD (contact);
	e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_VERSION), "3.0");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	e_contact_set (contact, E_CONTACT_IM_GROUPWISE_HOME_1, "gw1");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = test_contact_find_typed_attribute (vcard, EVC_X_GROUPWISE, "HOME", 0);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "gw1");

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_1);
	g_assert_cmpstr (value, ==, "gw1");
	g_clear_pointer (&value, g_free);

	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_3));

	e_contact_set (contact, E_CONTACT_IM_GROUPWISE_WORK_1, "gw2");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 3);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = test_contact_find_typed_attribute (vcard, EVC_X_GROUPWISE, "HOME", 0);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "gw1");

	attr = test_contact_find_typed_attribute (vcard, EVC_X_GROUPWISE, "WORK", 0);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "gw2");

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_1);
	g_assert_cmpstr (value, ==, "gw1");
	g_clear_pointer (&value, g_free);

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_1);
	g_assert_cmpstr (value, ==, "gw2");
	g_clear_pointer (&value, g_free);

	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_3));

	e_contact_set (contact, E_CONTACT_IM_ICQ_WORK_1, "icq5");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 4);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = test_contact_find_typed_attribute (vcard, EVC_X_ICQ, "WORK", 0);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "icq5");

	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_3));

	value = e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_1);
	g_assert_cmpstr (value, ==, "icq5");
	g_clear_pointer (&value, g_free);

	e_contact_set (contact, E_CONTACT_IM_GROUPWISE_HOME_2, "gw3");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 5);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = test_contact_find_typed_attribute (vcard, EVC_X_GROUPWISE, "HOME", 0);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "gw1");

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_1);
	g_assert_cmpstr (value, ==, "gw1");
	g_clear_pointer (&value, g_free);

	attr = test_contact_find_typed_attribute (vcard, EVC_X_GROUPWISE, "HOME", 1);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "gw3");

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_2);
	g_assert_cmpstr (value, ==, "gw3");
	g_clear_pointer (&value, g_free);

	attr = test_contact_find_typed_attribute (vcard, EVC_X_GROUPWISE, "WORK", 0);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "gw2");

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_1);
	g_assert_cmpstr (value, ==, "gw2");
	g_clear_pointer (&value, g_free);

	attr = test_contact_find_typed_attribute (vcard, EVC_X_ICQ, "WORK", 0);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "icq5");

	value = e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_1);
	g_assert_cmpstr (value, ==, "icq5");
	g_clear_pointer (&value, g_free);

	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_3));

	values = e_contact_get (contact, E_CONTACT_IM_GROUPWISE);
	g_assert_cmpuint (g_list_length (values), ==, 3);
	g_assert_cmpstr (values->data, ==, "gw1");
	g_assert_cmpstr (values->next->data, ==, "gw2");
	g_assert_cmpstr (values->next->next->data, ==, "gw3");
	g_list_free_full (values, g_free);

	values = e_contact_get (contact, E_CONTACT_IM_ICQ);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "icq5");
	g_list_free_full (values, g_free);

	e_contact_set (contact, E_CONTACT_IM_GROUPWISE_WORK_1, "gw2-changed");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 5);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_1);
	g_assert_cmpstr (value, ==, "gw1");
	g_clear_pointer (&value, g_free);

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_1);
	g_assert_cmpstr (value, ==, "gw2-changed");
	g_clear_pointer (&value, g_free);

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_2);
	g_assert_cmpstr (value, ==, "gw3");
	g_clear_pointer (&value, g_free);

	value = e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_1);
	g_assert_cmpstr (value, ==, "icq5");
	g_clear_pointer (&value, g_free);

	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_3));

	values = e_contact_get (contact, E_CONTACT_IM_GROUPWISE);
	g_assert_cmpuint (g_list_length (values), ==, 3);
	g_assert_cmpstr (values->data, ==, "gw1");
	g_assert_cmpstr (values->next->data, ==, "gw2-changed");
	g_assert_cmpstr (values->next->next->data, ==, "gw3");
	g_list_free_full (values, g_free);

	values = e_contact_get (contact, E_CONTACT_IM_ICQ);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "icq5");
	g_list_free_full (values, g_free);

	/* removing HOME_1 turns HOME_2 into HOME_1 */
	e_contact_set (contact, E_CONTACT_IM_GROUPWISE_HOME_1, NULL);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 4);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_1);
	g_assert_cmpstr (value, ==, "gw3");
	g_clear_pointer (&value, g_free);

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_1);
	g_assert_cmpstr (value, ==, "gw2-changed");
	g_clear_pointer (&value, g_free);

	value = e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_1);
	g_assert_cmpstr (value, ==, "icq5");
	g_clear_pointer (&value, g_free);

	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_3));

	values = e_contact_get (contact, E_CONTACT_IM_GROUPWISE);
	g_assert_cmpuint (g_list_length (values), ==, 2);
	g_assert_cmpstr (values->data, ==, "gw2-changed");
	g_assert_cmpstr (values->next->data, ==, "gw3");
	g_list_free_full (values, g_free);

	values = e_contact_get (contact, E_CONTACT_IM_ICQ);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "icq5");
	g_list_free_full (values, g_free);

	values = e_contact_get (contact, E_CONTACT_IMPP);
	g_assert_cmpuint (g_list_length (values), ==, 3);
	g_assert_cmpstr (values->data, ==, "groupwise:gw2-changed");
	g_assert_cmpstr (values->next->data, ==, "icq:icq5");
	g_assert_cmpstr (values->next->next->data, ==, "groupwise:gw3");
	g_list_free_full (values, g_free);

	values = g_list_prepend (NULL, (gpointer) "groupwise:impp4");
	values = g_list_prepend (values, (gpointer) "icq:impp3");
	values = g_list_prepend (values, (gpointer) "groupwise:impp2");
	values = g_list_prepend (values, (gpointer) "other:impp-other");
	values = g_list_prepend (values, (gpointer) "groupwise:impp1");

	e_contact_set (contact, E_CONTACT_IMPP, values);
	g_list_free (values);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 6);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	/* they are Groupwise and ICQ, but without type */
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_3));
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_X_GROUPWISE));
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_X_ICQ));
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_X_EVOLUTION_IMPP));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_IMPP));

	values = e_contact_get (contact, E_CONTACT_IM_GROUPWISE);
	g_assert_cmpuint (g_list_length (values), ==, 3);
	g_assert_cmpstr (values->data, ==, "impp1");
	g_assert_cmpstr (values->next->data, ==, "impp2");
	g_assert_cmpstr (values->next->next->data, ==, "impp4");
	g_list_free_full (values, g_free);

	values = e_contact_get (contact, E_CONTACT_IM_ICQ);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "impp3");
	g_list_free_full (values, g_free);

	values = e_contact_get (contact, E_CONTACT_IMPP);
	g_assert_cmpuint (g_list_length (values), ==, 5);
	g_assert_cmpstr (values->data, ==, "groupwise:impp1");
	g_assert_cmpstr (values->next->data, ==, "other:impp-other");
	g_assert_cmpstr (values->next->next->data, ==, "groupwise:impp2");
	g_assert_cmpstr (values->next->next->next->data, ==, "icq:impp3");
	g_assert_cmpstr (values->next->next->next->next->data, ==, "groupwise:impp4");
	g_list_free_full (values, g_free);

	g_clear_object (&contact);

	/* vCard 4.0 */
	contact = e_contact_new ();
	vcard = E_VCARD (contact);
	e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_VERSION), "4.0");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	e_contact_set (contact, E_CONTACT_IM_GROUPWISE_HOME_1, "gw1");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 2);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = test_contact_find_typed_attribute (vcard, EVC_IMPP, "HOME", 0);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "groupwise:gw1");

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_1);
	g_assert_cmpstr (value, ==, "gw1");
	g_clear_pointer (&value, g_free);

	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_3));

	e_contact_set (contact, E_CONTACT_IM_GROUPWISE_WORK_1, "gw2");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 3);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = test_contact_find_typed_attribute (vcard, EVC_IMPP, "HOME", 0);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "groupwise:gw1");

	attr = test_contact_find_typed_attribute (vcard, EVC_IMPP, "WORK", 0);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "groupwise:gw2");

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_1);
	g_assert_cmpstr (value, ==, "gw1");
	g_clear_pointer (&value, g_free);

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_1);
	g_assert_cmpstr (value, ==, "gw2");
	g_clear_pointer (&value, g_free);

	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_3));

	e_contact_set (contact, E_CONTACT_IM_ICQ_WORK_1, "icq5");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 4);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = test_contact_find_typed_attribute (vcard, EVC_IMPP, "WORK", 1);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "icq:icq5");

	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_3));

	value = e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_1);
	g_assert_cmpstr (value, ==, "icq5");
	g_clear_pointer (&value, g_free);

	e_contact_set (contact, E_CONTACT_IM_GROUPWISE_HOME_2, "gw3");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 5);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	attr = test_contact_find_typed_attribute (vcard, EVC_IMPP, "HOME", 0);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "groupwise:gw1");

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_1);
	g_assert_cmpstr (value, ==, "gw1");
	g_clear_pointer (&value, g_free);

	attr = test_contact_find_typed_attribute (vcard, EVC_IMPP, "HOME", 1);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "groupwise:gw3");

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_2);
	g_assert_cmpstr (value, ==, "gw3");
	g_clear_pointer (&value, g_free);

	attr = test_contact_find_typed_attribute (vcard, EVC_IMPP, "WORK", 0);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "groupwise:gw2");

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_1);
	g_assert_cmpstr (value, ==, "gw2");
	g_clear_pointer (&value, g_free);

	attr = test_contact_find_typed_attribute (vcard, EVC_IMPP, "WORK", 1);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "icq:icq5");

	value = e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_1);
	g_assert_cmpstr (value, ==, "icq5");
	g_clear_pointer (&value, g_free);

	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_3));

	values = e_contact_get (contact, E_CONTACT_IM_GROUPWISE);
	g_assert_cmpuint (g_list_length (values), ==, 3);
	g_assert_cmpstr (values->data, ==, "gw1");
	g_assert_cmpstr (values->next->data, ==, "gw2");
	g_assert_cmpstr (values->next->next->data, ==, "gw3");
	g_list_free_full (values, g_free);

	values = e_contact_get (contact, E_CONTACT_IM_ICQ);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "icq5");
	g_list_free_full (values, g_free);

	e_contact_set (contact, E_CONTACT_IM_GROUPWISE_WORK_1, "gw2-changed");
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 5);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_1);
	g_assert_cmpstr (value, ==, "gw1");
	g_clear_pointer (&value, g_free);

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_1);
	g_assert_cmpstr (value, ==, "gw2-changed");
	g_clear_pointer (&value, g_free);

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_2);
	g_assert_cmpstr (value, ==, "gw3");
	g_clear_pointer (&value, g_free);

	value = e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_1);
	g_assert_cmpstr (value, ==, "icq5");
	g_clear_pointer (&value, g_free);

	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_3));

	values = e_contact_get (contact, E_CONTACT_IM_GROUPWISE);
	g_assert_cmpuint (g_list_length (values), ==, 3);
	g_assert_cmpstr (values->data, ==, "gw1");
	g_assert_cmpstr (values->next->data, ==, "gw2-changed");
	g_assert_cmpstr (values->next->next->data, ==, "gw3");
	g_list_free_full (values, g_free);

	values = e_contact_get (contact, E_CONTACT_IM_ICQ);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "icq5");
	g_list_free_full (values, g_free);

	/* removing HOME_1 turns HOME_2 into HOME_1 */
	e_contact_set (contact, E_CONTACT_IM_GROUPWISE_HOME_1, NULL);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 4);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_1);
	g_assert_cmpstr (value, ==, "gw3");
	g_clear_pointer (&value, g_free);

	value = e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_1);
	g_assert_cmpstr (value, ==, "gw2-changed");
	g_clear_pointer (&value, g_free);

	value = e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_1);
	g_assert_cmpstr (value, ==, "icq5");
	g_clear_pointer (&value, g_free);

	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_3));

	values = e_contact_get (contact, E_CONTACT_IM_GROUPWISE);
	g_assert_cmpuint (g_list_length (values), ==, 2);
	g_assert_cmpstr (values->data, ==, "gw2-changed");
	g_assert_cmpstr (values->next->data, ==, "gw3");
	g_list_free_full (values, g_free);

	values = e_contact_get (contact, E_CONTACT_IM_ICQ);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "icq5");
	g_list_free_full (values, g_free);

	values = e_contact_get (contact, E_CONTACT_IMPP);
	g_assert_cmpuint (g_list_length (values), ==, 3);
	g_assert_cmpstr (values->data, ==, "groupwise:gw2-changed");
	g_assert_cmpstr (values->next->data, ==, "icq:icq5");
	g_assert_cmpstr (values->next->next->data, ==, "groupwise:gw3");
	g_list_free_full (values, g_free);

	values = g_list_prepend (NULL, (gpointer) "groupwise:impp4");
	values = g_list_prepend (values, (gpointer) "icq:impp3");
	values = g_list_prepend (values, (gpointer) "groupwise:impp2");
	values = g_list_prepend (values, (gpointer) "other:impp-other");
	values = g_list_prepend (values, (gpointer) "groupwise:impp1");

	e_contact_set (contact, E_CONTACT_IMPP, values);
	g_list_free (values);
	g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 6);
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));
	/* they are Groupwise and ICQ, but without type */
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_GROUPWISE_WORK_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_HOME_3));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_1));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_2));
	g_assert_null (e_contact_get (contact, E_CONTACT_IM_ICQ_WORK_3));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_X_GROUPWISE));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_X_ICQ));
	g_assert_null (e_vcard_get_attribute (vcard, EVC_X_EVOLUTION_IMPP));
	g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_IMPP));

	values = e_contact_get (contact, E_CONTACT_IM_GROUPWISE);
	g_assert_cmpuint (g_list_length (values), ==, 3);
	g_assert_cmpstr (values->data, ==, "impp1");
	g_assert_cmpstr (values->next->data, ==, "impp2");
	g_assert_cmpstr (values->next->next->data, ==, "impp4");
	g_list_free_full (values, g_free);

	values = e_contact_get (contact, E_CONTACT_IM_ICQ);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "impp3");
	g_list_free_full (values, g_free);

	values = e_contact_get (contact, E_CONTACT_IMPP);
	g_assert_cmpuint (g_list_length (values), ==, 5);
	g_assert_cmpstr (values->data, ==, "groupwise:impp1");
	g_assert_cmpstr (values->next->data, ==, "other:impp-other");
	g_assert_cmpstr (values->next->next->data, ==, "groupwise:impp2");
	g_assert_cmpstr (values->next->next->next->data, ==, "icq:impp3");
	g_assert_cmpstr (values->next->next->next->next->data, ==, "groupwise:impp4");
	g_list_free_full (values, g_free);

	g_clear_object (&contact);
}

static void
test_contact_quirks_photo_logo (void)
{
	EContactField fields[] = { E_CONTACT_PHOTO, E_CONTACT_LOGO };
	guint step;

	for (step = 0; step < 2; step++) {
		EContact *contact;
		EVCard *vcard;
		EContactPhoto *photo, photo_stack;
		guint ii;

		if (step == 0) {
			/* vCard 3.0 */
			contact = e_contact_new ();
			vcard = E_VCARD (contact);
			e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_VERSION), "3.0");
		} else {
			/* vCard 4.0 */
			contact = e_contact_new ();
			vcard = E_VCARD (contact);
			e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_VERSION), "4.0");
		}

		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (vcard)), ==, 1);
		g_assert_nonnull (e_vcard_get_attribute (vcard, EVC_VERSION));

		for (ii = 0; ii < G_N_ELEMENTS (fields); ii++) {
			EContactField field = fields[ii];
			const gchar *evc_field = e_contact_field_name (field);
			EVCardAttribute *attr;
			GList *values;

			g_assert_nonnull (evc_field);

			photo = e_contact_get (contact, field);
			g_assert_null (photo);

			memset (&photo_stack, 0, sizeof (EContactPhoto));
			photo_stack.type = E_CONTACT_PHOTO_TYPE_URI;
			photo_stack.data.uri = (gchar *) "https://example.comp/image.png";

			e_contact_set (contact, field, &photo_stack);

			photo = e_contact_get (contact, field);
			g_assert_nonnull (photo);
			g_assert_cmpint (photo->type, ==, photo_stack.type);
			g_assert_cmpstr (photo->data.uri, ==, photo_stack.data.uri);
			e_contact_photo_free (photo);

			attr = e_vcard_get_attribute (vcard, evc_field);
			g_assert_nonnull (attr);

			if (e_vcard_get_version (vcard) == E_VCARD_VERSION_30) {
				values = e_vcard_attribute_get_param (attr, EVC_VALUE);
				g_assert_nonnull (values);
				g_assert_cmpuint (g_list_length (values), ==, 1);
				g_assert_cmpstr (values->data, ==, "uri");

				values = e_vcard_attribute_get_values (attr);
				g_assert_cmpuint (g_list_length (values), ==, 1);
				g_assert_cmpstr (values->data, ==, photo_stack.data.uri);
			} else {
				values = e_vcard_attribute_get_param (attr, EVC_VALUE);
				g_assert_null (values);

				values = e_vcard_attribute_get_values (attr);
				g_assert_cmpuint (g_list_length (values), ==, 1);
				g_assert_cmpstr (values->data, ==, photo_stack.data.uri);
			}

			e_contact_set (contact, field, NULL);

			photo = e_contact_get (contact, field);
			g_assert_null (photo);

			memset (&photo_stack, 0, sizeof (EContactPhoto));
			photo_stack.type = E_CONTACT_PHOTO_TYPE_INLINED;
			photo_stack.data.inlined.mime_type = (gchar *) "image/png";
			photo_stack.data.inlined.length = 6;
			photo_stack.data.inlined.data = (guchar *) "hello\n";

			e_contact_set (contact, field, &photo_stack);

			photo = e_contact_get (contact, field);
			g_assert_nonnull (photo);
			g_assert_cmpint (photo->type, ==, photo_stack.type);
			g_assert_cmpstr (photo->data.inlined.mime_type, ==, photo_stack.data.inlined.mime_type);
			g_assert_cmpuint (photo->data.inlined.length, ==, photo_stack.data.inlined.length);
			g_assert_cmpmem (photo->data.inlined.data, photo->data.inlined.length, photo_stack.data.inlined.data, photo_stack.data.inlined.length);
			e_contact_photo_free (photo);

			attr = e_vcard_get_attribute (vcard, evc_field);
			g_assert_nonnull (attr);

			values = e_vcard_attribute_get_param (attr, EVC_VALUE);
			g_assert_null (values);

			if (e_vcard_get_version (vcard) == E_VCARD_VERSION_30) {
				values = e_vcard_attribute_get_param (attr, EVC_TYPE);
				g_assert_nonnull (values);
				g_assert_cmpuint (g_list_length (values), ==, 1);
				g_assert_cmpstr (values->data, ==, "png");

				values = e_vcard_attribute_get_param (attr, EVC_ENCODING);
				g_assert_nonnull (values);
				g_assert_cmpuint (g_list_length (values), ==, 1);
				g_assert_cmpstr (values->data, ==, "b");

				values = e_vcard_attribute_get_values (attr);
				g_assert_cmpuint (g_list_length (values), ==, 1);
				g_assert_cmpstr (values->data, ==, "aGVsbG8K");
			} else {
				values = e_vcard_attribute_get_param (attr, EVC_TYPE);
				g_assert_null (values);

				values = e_vcard_attribute_get_param (attr, EVC_ENCODING);
				g_assert_null (values);

				values = e_vcard_attribute_get_values (attr);
				g_assert_cmpuint (g_list_length (values), ==, 1);
				g_assert_cmpstr (values->data, ==, "data:image/png;base64,aGVsbG8K");
			}

			e_contact_set (contact, field, NULL);

			photo = e_contact_get (contact, field);
			g_assert_null (photo);
		}

		g_clear_object (&contact);
	}
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	g_test_add_func ("/EContact/Convert", test_contact_convert);
	g_test_add_func ("/EContact/GObjectProps30", test_contact_gobject_props_30);
	g_test_add_func ("/EContact/GObjectProps40", test_contact_gobject_props_40);
	g_test_add_func ("/EContact/QuirksADR", test_contact_quirks_adr);
	g_test_add_func ("/EContact/QuirksANNIVERSARY", test_contact_quirks_anniversary);
	g_test_add_func ("/EContact/QuirksKIND", test_contact_quirks_kind);
	g_test_add_func ("/EContact/QuirksIMPP", test_contact_quirks_impp);
	g_test_add_func ("/EContact/QuirksPHOTOLOGO", test_contact_quirks_photo_logo);

	return g_test_run ();
}
