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

#include <libebook/libebook.h>
#include <string.h>

static gboolean
compare_single_value (EVCard *vcard,
                      const gchar *attrname,
                      const gchar *expected_value)
{
	EVCardAttribute *attr;
	gchar *str;

	g_return_val_if_fail (vcard != NULL, FALSE);
	g_return_val_if_fail (attrname != NULL, FALSE);
	g_return_val_if_fail (expected_value != NULL, FALSE);

	attr = e_vcard_get_attribute (vcard, attrname);
	g_return_val_if_fail (attr != NULL, FALSE);

	str = e_vcard_attribute_get_value (attr);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_strcmp0 (str, expected_value) == 0, FALSE);

	g_free (str);

	return TRUE;
}

static gboolean
has_only_one (EVCard *vcard,
              const gchar *attrname)
{
	gboolean found = FALSE;
	GList *iter;

	for (iter = e_vcard_get_attributes (vcard); iter; iter = iter->next) {
		EVCardAttribute *attr = iter->data;

		if (g_strcmp0 (e_vcard_attribute_get_name (attr), attrname) == 0) {
			if (found)
				return FALSE;
			found = TRUE;
		}
	}

	return found;
}

static gboolean
test_vcard (const gchar *vcard_str)
{
	EVCard *vc1, *vc2;
	gchar *str;

	/* Do not parse */
	vc1 = e_vcard_new_from_string (vcard_str);
	str = e_vcard_to_string (vc1, EVC_FORMAT_VCARD_30);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_ascii_strcasecmp (str, vcard_str) == 0, FALSE);
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == FALSE, FALSE);

	g_free (str);

	/* parse */
	e_vcard_get_attribute (vc1, "UID");
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == TRUE, FALSE);
	str = e_vcard_to_string (vc1, EVC_FORMAT_VCARD_30);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_ascii_strcasecmp (str, vcard_str) == 0, FALSE);

	g_free (str);

	/* parse */
	e_vcard_get_attribute (vc1, "FN");
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == TRUE, FALSE);
	str = e_vcard_to_string (vc1, EVC_FORMAT_VCARD_30);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_ascii_strcasecmp (str, vcard_str) == 0, FALSE);

	g_free (str);
	g_object_unref (vc1);

	/* do not parse */
	vc1 = e_vcard_new_from_string (vcard_str);
	/* Setting the UID does not cause vCard parsing */
	e_vcard_append_attribute_with_value (vc1, e_vcard_attribute_new (NULL, "UID"), "other-uid");
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == FALSE, FALSE);
	/* Retrieving the UID should not cause vCard parsing either */
	g_return_val_if_fail (compare_single_value (vc1, "UID", "other-uid"), FALSE);
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == FALSE, FALSE);
	/* Getting FN attribute WILL cause parsing */
	e_vcard_get_attribute (vc1, "FN");
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == TRUE, FALSE);
	g_object_unref (vc1);

	/* parse */
	vc1 = e_vcard_new_from_string (vcard_str);
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == FALSE, FALSE);
	e_vcard_remove_attributes (vc1, NULL, "UID");
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == TRUE, FALSE);
	e_vcard_append_attribute_with_value (vc1, e_vcard_attribute_new (NULL, "UID"), "other-uid");
	g_return_val_if_fail (compare_single_value (vc1, "UID", "other-uid"), FALSE);
	str = e_vcard_to_string (vc1, EVC_FORMAT_VCARD_30);
	vc2 = e_vcard_new_from_string (str);
	g_free (str);

	g_return_val_if_fail (compare_single_value (vc2, "UID", "other-uid"), FALSE);

	g_object_unref (vc2);

	/* parse */
	e_vcard_get_attribute (vc1, "FN");
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == TRUE, FALSE);
	g_return_val_if_fail (compare_single_value (vc1, "UID", "other-uid"), FALSE);
	str = e_vcard_to_string (vc1, EVC_FORMAT_VCARD_30);
	vc2 = e_vcard_new_from_string (str);
	g_return_val_if_fail (e_vcard_is_parsed (vc2) == FALSE, FALSE);
	g_free (str);

	g_return_val_if_fail (compare_single_value (vc2, "UID", "other-uid"), FALSE);
	g_return_val_if_fail (has_only_one (vc1, "UID"), FALSE);
	g_return_val_if_fail (has_only_one (vc2, "UID"), FALSE);

	g_object_unref (vc2);
	g_object_unref (vc1);

	return TRUE;
}

static gboolean
test_econtact (const gchar *vcard_str)
{
	EContact *c1, *c2;
	gchar *str;

	/* do not parse */
	c1 = e_contact_new_from_vcard (vcard_str);
	str = e_vcard_to_string (E_VCARD (c1), EVC_FORMAT_VCARD_30);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_ascii_strcasecmp (str, vcard_str) == 0, FALSE);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == FALSE, FALSE);

	g_free (str);

	/* parse */
	e_contact_get_const (c1, E_CONTACT_UID);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == TRUE, FALSE);
	str = e_vcard_to_string (E_VCARD (c1), EVC_FORMAT_VCARD_30);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_ascii_strcasecmp (str, vcard_str) == 0, FALSE);

	g_free (str);

	/* parse */
	e_contact_get_const (c1, E_CONTACT_FULL_NAME);
	str = e_vcard_to_string (E_VCARD (c1), EVC_FORMAT_VCARD_30);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_ascii_strcasecmp (str, vcard_str) == 0, FALSE);

	g_free (str);
	g_object_unref (c1);

	/* not parsed again */
	c1 = e_contact_new_from_vcard (vcard_str);
	e_contact_set (c1, E_CONTACT_UID, "other-uid");
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == FALSE, FALSE);
	g_return_val_if_fail (compare_single_value (E_VCARD (c1), "UID", "other-uid"), FALSE);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == FALSE, FALSE);
	str = e_vcard_to_string (E_VCARD (c1), EVC_FORMAT_VCARD_30);
	c2 = e_contact_new_from_vcard (str);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c2)) == FALSE, FALSE);
	g_free (str);

	g_return_val_if_fail (compare_single_value (E_VCARD (c2), "UID", "other-uid"), FALSE);

	g_object_unref (c2);

	/* parse */
	e_contact_get_const (c1, E_CONTACT_FULL_NAME);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == TRUE, FALSE);
	g_return_val_if_fail (compare_single_value (E_VCARD (c1), "UID", "other-uid"), FALSE);
	str = e_vcard_to_string (E_VCARD (c1), EVC_FORMAT_VCARD_30);
	c2 = e_contact_new_from_vcard (str);
	g_free (str);

	g_return_val_if_fail (compare_single_value (E_VCARD (c2), "UID", "other-uid"), FALSE);
	g_return_val_if_fail (has_only_one (E_VCARD (c1), "UID"), FALSE);
	g_return_val_if_fail (has_only_one (E_VCARD (c2), "UID"), FALSE);

	g_object_unref (c2);
	g_object_unref (c1);

	/* do not parse */
	c1 = e_contact_new_from_vcard_with_uid (vcard_str, "other-uid");
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == FALSE, FALSE);
	g_return_val_if_fail (compare_single_value (E_VCARD (c1), "UID", "other-uid"), FALSE);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == FALSE, FALSE);
	str = e_vcard_to_string (E_VCARD (c1), EVC_FORMAT_VCARD_30);
	c2 = e_contact_new_from_vcard (str);
	g_free (str);

	g_return_val_if_fail (compare_single_value (E_VCARD (c2), "UID", "other-uid"), FALSE);

	g_object_unref (c2);

	/* parse */
	e_contact_get_const (c1, E_CONTACT_FULL_NAME);
	g_return_val_if_fail (compare_single_value (E_VCARD (c1), "UID", "other-uid"), FALSE);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == TRUE, FALSE);
	str = e_vcard_to_string (E_VCARD (c1), EVC_FORMAT_VCARD_30);
	c2 = e_contact_new_from_vcard (str);
	g_free (str);

	g_return_val_if_fail (compare_single_value (E_VCARD (c2), "UID", "other-uid"), FALSE);
	g_return_val_if_fail (has_only_one (E_VCARD (c1), "UID"), FALSE);
	g_return_val_if_fail (has_only_one (E_VCARD (c2), "UID"), FALSE);

	g_object_unref (c2);
	g_object_unref (c1);

	return TRUE;
}

static gboolean
test_vcard_qp_2_1_parsing (const gchar *vcard_str,
                           const gchar *expected_text)
{
	EVCard *vcard;
	EVCardAttribute *attr;
	gchar *value;

	vcard = e_vcard_new_from_string (vcard_str);
	g_return_val_if_fail (vcard != NULL, FALSE);

	attr = e_vcard_get_attribute (vcard, "FN");
	g_return_val_if_fail (attr != NULL, FALSE);

	value = e_vcard_attribute_get_value (attr);
	g_return_val_if_fail (value != NULL, FALSE);

	g_return_val_if_fail (g_strcmp0 (value, expected_text) == 0, FALSE);

	g_object_unref (vcard);
	g_free (value);

	return TRUE;
}

static gboolean
test_vcard_qp_2_1_saving (const gchar *expected_text)
{
	EVCard *vcard;
	EVCardAttribute *attr;
	EVCardAttributeParam *param;
	gchar *str, *encoded_value;
	GString *decoded;

	vcard = e_vcard_new ();
	attr = e_vcard_attribute_new (NULL, "FN");
	param = e_vcard_attribute_param_new ("ENCODING");
	e_vcard_attribute_param_add_value (param, "quoted-printable");
	e_vcard_attribute_add_param (attr, param);

	e_vcard_attribute_add_value_decoded (attr, expected_text, strlen (expected_text));

	decoded = e_vcard_attribute_get_value_decoded (attr);
	g_return_val_if_fail (decoded != NULL, FALSE);
	g_return_val_if_fail (g_strcmp0 (decoded->str, expected_text) == 0, FALSE);
	g_string_free (decoded, TRUE);

	encoded_value = e_vcard_attribute_get_value (attr);
	g_return_val_if_fail (encoded_value != NULL, FALSE);
	/* it's the encoded value, thus it cannot match */
	g_return_val_if_fail (g_strcmp0 (encoded_value, expected_text) != 0, FALSE);
	g_free (encoded_value);

	e_vcard_add_attribute (vcard, attr);

	str = e_vcard_to_string (vcard, EVC_FORMAT_VCARD_21);
	g_object_unref (vcard);

	g_return_val_if_fail (str != NULL, FALSE);

	vcard = e_vcard_new_from_string (str);
	g_free (str);

	g_return_val_if_fail (vcard != NULL, FALSE);

	attr = e_vcard_get_attribute (vcard, "FN");
	g_return_val_if_fail (attr != NULL, FALSE);

	decoded = e_vcard_attribute_get_value_decoded (attr);
	g_return_val_if_fail (decoded != NULL, FALSE);
	g_return_val_if_fail (g_strcmp0 (decoded->str, expected_text) == 0, FALSE);
	g_string_free (decoded, TRUE);

	g_object_unref (vcard);

	return TRUE;
}

static gboolean
test_vcard_qp_3_0_saving (const gchar *expected_text)
{
	EVCard *vcard;
	EVCardAttribute *attr;
	EVCardAttributeParam *param;
	gchar *str, *value, *encoded_value;
	GString *decoded;

	vcard = e_vcard_new ();
	attr = e_vcard_attribute_new (NULL, "FN");
	param = e_vcard_attribute_param_new ("ENCODING");
	e_vcard_attribute_param_add_value (param, "quoted-printable");
	e_vcard_attribute_add_param (attr, param);

	e_vcard_attribute_add_value_decoded (attr, expected_text, strlen (expected_text));
	e_vcard_add_attribute (vcard, attr);

	decoded = e_vcard_attribute_get_value_decoded (attr);
	g_return_val_if_fail (decoded != NULL, FALSE);
	g_return_val_if_fail (g_strcmp0 (decoded->str, expected_text) == 0, FALSE);
	g_string_free (decoded, TRUE);

	encoded_value = e_vcard_attribute_get_value (attr);
	g_return_val_if_fail (encoded_value != NULL, FALSE);
	/* it's the encoded value, thus it cannot match */
	g_return_val_if_fail (g_strcmp0 (encoded_value, expected_text) != 0, FALSE);

	str = e_vcard_to_string (vcard, EVC_FORMAT_VCARD_30);

	g_object_unref (vcard);

	g_return_val_if_fail (str != NULL, FALSE);

	vcard = e_vcard_new_from_string (str);
	g_free (str);

	g_return_val_if_fail (vcard != NULL, FALSE);

	attr = e_vcard_get_attribute (vcard, "FN");
	g_return_val_if_fail (attr != NULL, FALSE);

	decoded = e_vcard_attribute_get_value_decoded (attr);
	g_return_val_if_fail (decoded != NULL, FALSE);
	g_return_val_if_fail (g_strcmp0 (decoded->str, expected_text) == 0, FALSE);
	g_string_free (decoded, TRUE);

	value = e_vcard_attribute_get_value (attr);
	g_return_val_if_fail (value != NULL, FALSE);
	/* either base64 or a free form, but not quoted-printable for sure */
	g_return_val_if_fail (g_strcmp0 (value, encoded_value) != 0, FALSE);
	g_free (value);

	g_object_unref (vcard);
	g_free (encoded_value);

	return TRUE;
}

static void
test_vcard_quoted_printable (void)
{
	const gchar *expected_text = "ActualValue ěščřžýáíéúůóöĚŠČŘŽÝÁÍÉÚŮÓÖ§ "
				     "1234567890 1234567890 1234567890 1234567890 1234567890";
	const gchar *vcard_2_1_str =
		"BEGIN:VCARD\r\n"
		"VERSION:2.1\r\n"
		"FN;ENCODING=quoted-printable:ActualValue=20=C4=9B=C5=A1"
		  "=C4=8D=C5=99=C5=BE=C3=BD=C3=A1=C3=AD=C3=A9=C3=BA=C5=AF=C3"
		  "=B3=C3=B6=C4=9A=C5=A0=C4=8C=C5=98=C5=BD=C3=9D=C3=81=C3=8D"
		  "=C3=89=C3=9A=C5=AE=C3=93=C3=96=C2=A7=201234567890=2012345"
		  "67890=201234567890=201234567890=201234567890\r\n"
		"END:VCARD\r\n";

	g_assert_true (test_vcard_qp_2_1_parsing (vcard_2_1_str, expected_text));
	g_assert_true (test_vcard_qp_2_1_saving (expected_text));
	g_assert_true (test_vcard_qp_3_0_saving (expected_text));
}

static const gchar *test_vcard_no_uid_str =
	"BEGIN:VCARD\r\n"
	"VERSION:3.0\r\n"
	"EMAIL;TYPE=OTHER:zyx@no.where\r\n"
	"FN:zyx mix\r\n"
	"N:zyx;mix;;;\r\n"
	"END:VCARD";

static const gchar *test_vcard_with_uid_str =
	"BEGIN:VCARD\r\n"
	"VERSION:3.0\r\n"
	"UID:some-uid\r\n"
	"EMAIL;TYPE=OTHER:zyx@no.where\r\n"
	"FN:zyx mix\r\n"
	"N:zyx;mix;;;\r\n"
	"END:VCARD";

static void
test_vcard_with_uid (void)
{
	g_assert_true (test_vcard (test_vcard_with_uid_str));
}

static void
test_vcard_without_uid (void)
{
	g_assert_true (test_vcard (test_vcard_no_uid_str));
}

static void
test_contact_with_uid (void)
{
	g_assert_true (test_econtact (test_vcard_with_uid_str));
}

static void
test_contact_without_uid (void)
{
	g_assert_true (test_econtact (test_vcard_no_uid_str));
}

static void
test_phone_params_and_value (EContact *contact,
			     EContactField field_id,
			     const gchar *expected_value,
			     const gchar *expected_value_type)
{
	GList *attributes, *params, *link;
	EVCardAttribute *attr = NULL;

	g_assert_true (E_IS_CONTACT (contact));
	g_assert_true (expected_value != NULL);
	g_assert_true (expected_value_type != NULL);

	g_assert_nonnull (e_contact_get_const (contact, field_id));
	g_assert_cmpstr (e_contact_get_const (contact, field_id), ==, expected_value);

	attributes = e_contact_get_attributes (contact, field_id);

	g_assert_true (attributes != NULL);
	g_assert_true (attributes->next != NULL);
	g_assert_true (attributes->next->next != NULL);
	g_assert_true (attributes->next->next->next == NULL);

	for (link = attributes; link; link = g_list_next (link)) {
		gchar *value;

		attr = link->data;

		g_assert_true (attr != NULL);

		value = e_vcard_attribute_get_value (attr);

		g_assert_true (value != NULL);

		if (g_strcmp0 (value, expected_value) == 0) {
			g_free (value);
			break;
		}

		g_free (value);
		attr = NULL;
	}

	g_assert_true (attr != NULL);

	g_assert_true (e_vcard_attribute_get_name (attr) != NULL);

	params = e_vcard_attribute_get_params (attr);
	g_assert_true (params != NULL);
	g_assert_true (params->next != NULL);
	g_assert_true (params->next->next == NULL);

	for (link = params; link; link = g_list_next (link)) {
		EVCardAttributeParam *param = link->data;
		const gchar *name;

		g_assert_true (param != NULL);

		name = e_vcard_attribute_param_get_name (param);

		g_assert_true (name != NULL);
		g_assert_true (g_ascii_strcasecmp (name, EVC_TYPE) == 0 ||
			  g_ascii_strcasecmp (name, EVC_X_E164) == 0);

		if (g_ascii_strcasecmp (name, EVC_X_E164) == 0) {
			GList *values;
			const gchar *value;

			values = e_vcard_attribute_param_get_values (param);

			g_assert_true (values != NULL);
			g_assert_true (values->next == NULL || values->next->next == NULL);

			value = values->data;

			g_assert_true (value != NULL);
			g_assert_cmpstr (value, ==, expected_value);

			if (values->next) {
				value = values->next->data;

				if (value != NULL)
					g_assert_cmpstr (value, ==, "");
			}
		} else {
			GList *values;
			const gchar *value1, *value2;

			values = e_vcard_attribute_param_get_values (param);

			g_assert_true (values != NULL);
			g_assert_true (values->next != NULL);
			g_assert_true (values->next->next == NULL);

			value1 = values->data;
			value2 = values->next->data;

			g_assert_true (value1 != NULL);
			g_assert_true (value2 != NULL);
			g_assert_cmpstr (value1, ==, expected_value_type);
			g_assert_cmpstr (value2, ==, "VOICE");
		}
	}

	g_list_free_full (attributes, (GDestroyNotify) e_vcard_attribute_free);
}

static void
test_contact_empty_value (void)
{
	EContact *contact;

	contact = e_contact_new_from_vcard (
		"BEGIN:VCARD\r\n"
		"VERSION:3.0\r\n"
		"UID:some-uid\r\n"
		"REV:2017-01-12T11:34:36Z(0)\r\n"
		"FN:zyx\r\n"
		"N:zyx;;;;\r\n"
		"EMAIL;TYPE=WORK:work@no.where\r\n"
		"TEL;X-EVOLUTION-E164=00123456789,;TYPE=WORK,VOICE:00123456789\r\n"
		"TEL;TYPE=WORK;TYPE=VOICE;X-EVOLUTION-E164=11123456789,:11123456789\r\n"
		"TEL;X-EVOLUTION-E164=002233445566;TYPE=HOME,VOICE:002233445566\r\n"
		"END:VCARD\r\n");

	g_assert_true (E_IS_CONTACT (contact));

	test_phone_params_and_value (contact, E_CONTACT_PHONE_BUSINESS, "00123456789", "WORK");
	test_phone_params_and_value (contact, E_CONTACT_PHONE_BUSINESS_2, "11123456789", "WORK");
	test_phone_params_and_value (contact, E_CONTACT_PHONE_HOME, "002233445566", "HOME");

	g_clear_object (&contact);
}

static void
test_construction_vcard_attribute_with_group (void)
{
	EVCardAttribute *attr1, *attr2, *attr3;

	attr1 = e_vcard_attribute_new (NULL, "X-TEST");
	attr2 = e_vcard_attribute_new ("", "X-TEST");
	attr3 = e_vcard_attribute_new ("GROUP", "X-TEST");

	g_assert_cmpstr (e_vcard_attribute_get_group (attr1), ==, NULL);
	g_assert_cmpstr (e_vcard_attribute_get_group (attr2), ==, NULL);
	g_assert_cmpstr (e_vcard_attribute_get_group (attr3), ==, "GROUP");

	e_vcard_attribute_free (attr3);
	e_vcard_attribute_free (attr2);
	e_vcard_attribute_free (attr1);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add_func ("/Parsing/VCard/WithUID", test_vcard_with_uid);
	g_test_add_func ("/Parsing/VCard/WithoutUID", test_vcard_without_uid);
	g_test_add_func ("/Parsing/VCard/QuotedPrintable", test_vcard_quoted_printable);
	g_test_add_func ("/Parsing/Contact/WithUID", test_contact_with_uid);
	g_test_add_func ("/Parsing/Contact/WithoutUID", test_contact_without_uid);
	g_test_add_func ("/Parsing/Contact/EmptyValue", test_contact_empty_value);
	g_test_add_func ("/Construction/VCardAttribute/WithGroup",
	                 test_construction_vcard_attribute_with_group);

	return g_test_run ();
}
