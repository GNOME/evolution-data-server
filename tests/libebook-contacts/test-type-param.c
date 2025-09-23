/*
 * Copyright (C) 2019 Red Hat, Inc. (www.redhat.com)
 *
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
#include <string.h>
#include <libebook-contacts/libebook-contacts.h>

typedef struct _TestData {
	const gchar *vcard_str;
	EContactField field_id;
	const gchar *expected;
} TestData;

static void
test_type_param (const TestData datas[],
		 guint n_datas,
		 void (* check_value_func) (gconstpointer value,
					    const gchar *expected),
		 GDestroyNotify free_value_func)
{
	const gchar *vcard_str = NULL;
	guint ii;

	for (ii = 0; ii < n_datas; ii++) {
		EContact *contact;
		gpointer value;

		/* Items can inherit the vCard definition */
		if (datas[ii].vcard_str)
			vcard_str = datas[ii].vcard_str;

		g_assert_nonnull (vcard_str);

		contact = e_contact_new_from_vcard (vcard_str);
		g_assert_nonnull (contact);

		value = e_contact_get (contact, datas[ii].field_id);

		if (datas[ii].expected) {
			g_assert_nonnull (value);

			if (check_value_func)
				check_value_func (value, datas[ii].expected);
			else
				g_assert_cmpstr (value, ==, datas[ii].expected);
		} else {
			g_assert_null (value);
		}

		if (free_value_func)
			free_value_func (value);
		else
			g_free (value);

		g_object_unref (contact);
	}
}

static void
test_type_param_email (void)
{
	TestData datas[] = {
		{ "BEGIN:VCARD\r\n"
		  "EMAIL;TYPE=home:V1\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_EMAIL_1, "V1" },
		{ NULL, E_CONTACT_EMAIL_2, NULL },
		{ NULL, E_CONTACT_EMAIL_3, NULL },
		{ NULL, E_CONTACT_EMAIL_4, NULL },
		{ "BEGIN:VCARD\r\n"
		  "EMAIL;TYPE=home:V1\r\n"
		  "EMAIL;TYPE=home:V2\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_EMAIL_1, "V1" },
		{ NULL, E_CONTACT_EMAIL_2, "V2" },
		{ NULL, E_CONTACT_EMAIL_3, NULL },
		{ NULL, E_CONTACT_EMAIL_4, NULL },
		{ "BEGIN:VCARD\r\n"
		  "EMAIL;TYPE=home:V1\r\n"
		  "EMAIL;TYPE=home:V2\r\n"
		  "EMAIL;TYPE=home:V3\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_EMAIL_1, "V1" },
		{ NULL, E_CONTACT_EMAIL_2, "V2" },
		{ NULL, E_CONTACT_EMAIL_3, "V3" },
		{ NULL, E_CONTACT_EMAIL_4, NULL },
		{ "BEGIN:VCARD\r\n"
		  "EMAIL;TYPE=home:V1\r\n"
		  "EMAIL;TYPE=home:V2\r\n"
		  "EMAIL;TYPE=home:V3\r\n"
		  "EMAIL;TYPE=home:V4\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_EMAIL_1, "V1" },
		{ NULL, E_CONTACT_EMAIL_2, "V2" },
		{ NULL, E_CONTACT_EMAIL_3, "V3" },
		{ NULL, E_CONTACT_EMAIL_4, "V4" },
		{ "BEGIN:VCARD\r\n"
		  "EMAIL;TYPE=home:V1\r\n"
		  "EMAIL;TYPE=home:V2\r\n"
		  "EMAIL;TYPE=home:V3\r\n"
		  "EMAIL;TYPE=home:V4\r\n"
		  "EMAIL;TYPE=home:V5\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_EMAIL_1, "V1" },
		{ NULL, E_CONTACT_EMAIL_2, "V2" },
		{ NULL, E_CONTACT_EMAIL_3, "V3" },
		{ NULL, E_CONTACT_EMAIL_4, "V4" }
	};

	test_type_param (datas, G_N_ELEMENTS (datas), NULL, NULL);
}

static void
check_value_adr (gconstpointer value,
		 const gchar *expected)
{
	const EContactAddress *addr = value;

	g_assert_nonnull (addr);
	g_assert_nonnull (expected);

	g_assert_cmpstr (addr->street, ==, expected);
}

static void
test_type_param_adr (void)
{
	TestData datas[] = {
		{ "BEGIN:VCARD\r\n"
		  "ADR;TYPE=home:;;V1;;;;\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_ADDRESS_HOME, "V1" },
		{ NULL, E_CONTACT_ADDRESS_WORK, NULL },
		{ NULL, E_CONTACT_ADDRESS_OTHER, NULL },
		{ "BEGIN:VCARD\r\n"
		  "ADR:;;V1;;;;\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_ADDRESS_HOME, NULL },
		{ NULL, E_CONTACT_ADDRESS_WORK, "V1" },
		{ NULL, E_CONTACT_ADDRESS_OTHER, NULL },
		{ "BEGIN:VCARD\r\n"
		  "ADR;TYPE=work:;;V1;;;;\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_ADDRESS_HOME, NULL },
		{ NULL, E_CONTACT_ADDRESS_WORK, "V1" },
		{ NULL, E_CONTACT_ADDRESS_OTHER, NULL },
		{ "BEGIN:VCARD\r\n"
		  "ADR;TYPE=other:;;V1;;;;\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_ADDRESS_HOME, NULL },
		{ NULL, E_CONTACT_ADDRESS_WORK, NULL },
		{ NULL, E_CONTACT_ADDRESS_OTHER, "V1" },
		{ "BEGIN:VCARD\r\n"
		  "ADR;TYPE=dom;TYPE=home:;;V1;;;;\r\n"
		  "ADR;TYPE=postal,work:;;V2;;;;\r\n"
		  "ADR;TYPE=postal,intl;TYPE=parcel,other:;;V3;;;;\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_ADDRESS_HOME, "V1" },
		{ NULL, E_CONTACT_ADDRESS_WORK, "V2" },
		{ NULL, E_CONTACT_ADDRESS_OTHER, "V3" },
		{ "BEGIN:VCARD\r\n"
		  "ADR:;;V1;;;;\r\n"
		  "ADR;TYPE=dom;TYPE=home:;;V2;;;;\r\n"
		  "ADR;TYPE=postal,intl;TYPE=parcel,other:;;V3;;;;\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_ADDRESS_HOME, "V2" },
		{ NULL, E_CONTACT_ADDRESS_WORK, "V1" },
		{ NULL, E_CONTACT_ADDRESS_OTHER, "V3" },
		{ "BEGIN:VCARD\r\n"
		  "ADR;TYPE=dom:;;V1;;;;\r\n"
		  "ADR:;;V2;;;;\r\n"
		  "ADR;TYPE=postal,intl;TYPE=parcel,other:;;V3;;;;\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_ADDRESS_HOME, NULL },
		{ NULL, E_CONTACT_ADDRESS_WORK, "V2" },
		{ NULL, E_CONTACT_ADDRESS_OTHER, "V3" }
	};

	test_type_param (datas, G_N_ELEMENTS (datas), check_value_adr, (GDestroyNotify) e_contact_address_free);
}

static void
test_type_param_label (void)
{
	TestData datas[] = {
		{ "BEGIN:VCARD\r\n"
		  "VERSION:3.0\r\n"
		  "LABEL;TYPE=home:V1\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_ADDRESS_LABEL_HOME, "V1" },
		{ NULL, E_CONTACT_ADDRESS_LABEL_WORK, NULL },
		{ NULL, E_CONTACT_ADDRESS_LABEL_OTHER, NULL },
		{ "BEGIN:VCARD\r\n"
		  "VERSION:3.0\r\n"
		  "LABEL:V1\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_ADDRESS_LABEL_HOME, NULL },
		{ NULL, E_CONTACT_ADDRESS_LABEL_WORK, "V1" },
		{ NULL, E_CONTACT_ADDRESS_LABEL_OTHER, NULL },
		{ "BEGIN:VCARD\r\n"
		  "VERSION:3.0\r\n"
		  "LABEL;TYPE=work:V1\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_ADDRESS_LABEL_HOME, NULL },
		{ NULL, E_CONTACT_ADDRESS_LABEL_WORK, "V1" },
		{ NULL, E_CONTACT_ADDRESS_LABEL_OTHER, NULL },
		{ "BEGIN:VCARD\r\n"
		  "VERSION:3.0\r\n"
		  "LABEL;TYPE=other:V1\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_ADDRESS_LABEL_HOME, NULL },
		{ NULL, E_CONTACT_ADDRESS_LABEL_WORK, NULL },
		{ NULL, E_CONTACT_ADDRESS_LABEL_OTHER, "V1" },
		{ "BEGIN:VCARD\r\n"
		  "VERSION:3.0\r\n"
		  "LABEL;TYPE=dom;TYPE=home:V1\r\n"
		  "LABEL;TYPE=postal,work:V2\r\n"
		  "LABEL;TYPE=postal,intl;TYPE=parcel,other:V3\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_ADDRESS_LABEL_HOME, "V1" },
		{ NULL, E_CONTACT_ADDRESS_LABEL_WORK, "V2" },
		{ NULL, E_CONTACT_ADDRESS_LABEL_OTHER, "V3" },
		{ "BEGIN:VCARD\r\n"
		  "VERSION:3.0\r\n"
		  "LABEL:V1\r\n"
		  "LABEL;TYPE=dom;TYPE=home:V2\r\n"
		  "LABEL;TYPE=postal,intl;TYPE=parcel,other:V3\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_ADDRESS_LABEL_HOME, "V2" },
		{ NULL, E_CONTACT_ADDRESS_LABEL_WORK, "V1" },
		{ NULL, E_CONTACT_ADDRESS_LABEL_OTHER, "V3" },
		{ "BEGIN:VCARD\r\n"
		  "VERSION:3.0\r\n"
		  "LABEL;TYPE=dom:V1\r\n"
		  "LABEL:V2\r\n"
		  "LABEL;TYPE=postal,intl;TYPE=parcel,other:V3\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_ADDRESS_LABEL_HOME, NULL },
		{ NULL, E_CONTACT_ADDRESS_LABEL_WORK, "V2" },
		{ NULL, E_CONTACT_ADDRESS_LABEL_OTHER, "V3" }
	};

	test_type_param (datas, G_N_ELEMENTS (datas), NULL, NULL);
}

static void
check_value_key (gconstpointer value,
		 const gchar *expected)
{
	const EContactCert *cert = value;
	gchar *str_cert;

	g_assert_nonnull (cert);
	g_assert_nonnull (expected);

	str_cert = g_strndup (cert->data, cert->length);

	g_assert_cmpstr (str_cert, ==, expected);

	g_free (str_cert);
}

static void
test_type_param_key (void)
{
	TestData datas[] = {
		{ "BEGIN:VCARD\r\n"
		  "KEY;TYPE=x509:V1\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_X509_CERT, "V1" },
		{ NULL, E_CONTACT_PGP_CERT, NULL },
		{ "BEGIN:VCARD\r\n"
		  "KEY;TYPE=pgp:V1\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_X509_CERT, NULL },
		{ NULL, E_CONTACT_PGP_CERT, "V1" },
		{ "BEGIN:VCARD\r\n"
		  "KEY;TYPE=X509;TYPE=x-test:V1\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_X509_CERT, "V1" },
		{ NULL, E_CONTACT_PGP_CERT, NULL },
		{ "BEGIN:VCARD\r\n"
		  "KEY;TYPE=PGP;TYPE=x-test:V1\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_X509_CERT, NULL },
		{ NULL, E_CONTACT_PGP_CERT, "V1" },
		{ "BEGIN:VCARD\r\n"
		  "KEY;TYPE=x-test,x509:V1\r\n"
		  "KEY;TYPE=x-test,pgp:V2\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_X509_CERT, "V1" },
		{ NULL, E_CONTACT_PGP_CERT, "V2" },
		{ "BEGIN:VCARD\r\n"
		  "KEY:V1\r\n"
		  "KEY;TYPE=x-test:V2\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_X509_CERT, NULL },
		{ NULL, E_CONTACT_PGP_CERT, NULL }
	};

	test_type_param (datas, G_N_ELEMENTS (datas), check_value_key, (GDestroyNotify) e_contact_cert_free);
}

static void
test_type_param_tel (void)
{
	TestData datas[] = {
		{ "BEGIN:VCARD\r\n"
		  "TEL;TYPE=" EVC_X_ASSISTANT ":V1\r\n"
		  "TEL;TYPE=work:V2\r\n"
		  "TEL;TYPE=work,voice:V3\r\n"
		  "TEL;TYPE=work;TYPE=fax:V4\r\n"
		  "TEL;TYPE=" EVC_X_CALLBACK ":V5\r\n"
		  "TEL;TYPE=car:V6\r\n"
		  "TEL;TYPE=" EVC_X_COMPANY ":V7\r\n"
		  "TEL;TYPE=voice,home:V8\r\n"
		  "TEL;TYPE=home:V9\r\n"
		  "TEL;TYPE=fax;Type=home:V10\r\n"
		  "TEL;TYPE=ISDN:V11\r\n"
		  "TEL;TYPE=cell:V12\r\n"
		  "TEL;TYPE=voice:V13\r\n"
		  "TEL;TYPE=fax:V14\r\n"
		  "TEL;TYPE=pager:V15\r\n"
		  "TEL;TYPE=pref:V16\r\n"
		  "TEL;TYPE=" EVC_X_RADIO ":V17\r\n"
		  "TEL;TYPE=" EVC_X_TELEX ":V18\r\n"
		  "TEL;TYPE=" EVC_X_TTYTDD ":V19\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_PHONE_ASSISTANT, "V1" },
		{ NULL, E_CONTACT_PHONE_BUSINESS, "V2" },
		{ NULL, E_CONTACT_PHONE_BUSINESS_2, "V3" },
		{ NULL, E_CONTACT_PHONE_BUSINESS_FAX, "V4" },
		{ NULL, E_CONTACT_PHONE_CALLBACK, "V5" },
		{ NULL, E_CONTACT_PHONE_CAR, "V6" },
		{ NULL, E_CONTACT_PHONE_COMPANY, "V7" },
		{ NULL, E_CONTACT_PHONE_HOME, "V8" },
		{ NULL, E_CONTACT_PHONE_HOME_2, "V9" },
		{ NULL, E_CONTACT_PHONE_HOME_FAX, "V10" },
		{ NULL, E_CONTACT_PHONE_ISDN, "V11" },
		{ NULL, E_CONTACT_PHONE_MOBILE, "V12" },
		{ NULL, E_CONTACT_PHONE_OTHER, "V13" },
		{ NULL, E_CONTACT_PHONE_OTHER_FAX, "V14" },
		{ NULL, E_CONTACT_PHONE_PAGER, "V15" },
		{ NULL, E_CONTACT_PHONE_PRIMARY, "V16" },
		{ NULL, E_CONTACT_PHONE_RADIO, "V17" },
		{ NULL, E_CONTACT_PHONE_TELEX, "V18" },
		{ NULL, E_CONTACT_PHONE_TTYTDD, "V19" },
		{ "BEGIN:VCARD\r\n"
		  "TEL;TYPE=" EVC_X_ASSISTANT ";Type=msg:V1\r\n"
		  "TEL;Type=msg;TYPE=work:V2\r\n"
		  "TEL;TYPE=work,msg;type=Voice:V3\r\n"
		  "TEL;TYPE=work;Type=msg;TYPE=fax:V4\r\n"
		  "TEL;TYPE=msg," EVC_X_CALLBACK ":V5\r\n"
		  "TEL;TYPE=msg,car:V6\r\n"
		  "TEL;TYPE=" EVC_X_COMPANY ",msg:V7\r\n"
		  "TEL;TYPE=voice,msg,home:V8\r\n"
		  "TEL;TYPE=home,msg:V9\r\n"
		  "TEL;TYPE=fax,msg;Type=home:V10\r\n"
		  "TEL;TYPE=msg,Isdn:V11\r\n"
		  "TEL;TYPE=cELL,msg:V12\r\n"
		  "TEL:V13\r\n"
		  "TEL;TYPE=fax,msg:V14\r\n"
		  "TEL;TYPE=pager,msg:V15\r\n"
		  "TEL;TYPE=pref,msg:V16\r\n"
		  "TEL;TYPE=msg," EVC_X_RADIO ":V17\r\n"
		  "TEL;Type=msg;TYPE=" EVC_X_TELEX ":V18\r\n"
		  "TEL;TYPE=" EVC_X_TTYTDD ";Type=msg:V19\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_PHONE_ASSISTANT, "V1" },
		{ NULL, E_CONTACT_PHONE_BUSINESS, "V2" },
		{ NULL, E_CONTACT_PHONE_BUSINESS_2, "V3" },
		{ NULL, E_CONTACT_PHONE_BUSINESS_FAX, "V4" },
		{ NULL, E_CONTACT_PHONE_CALLBACK, "V5" },
		{ NULL, E_CONTACT_PHONE_CAR, "V6" },
		{ NULL, E_CONTACT_PHONE_COMPANY, "V7" },
		{ NULL, E_CONTACT_PHONE_HOME, "V8" },
		{ NULL, E_CONTACT_PHONE_HOME_2, "V9" },
		{ NULL, E_CONTACT_PHONE_HOME_FAX, "V10" },
		{ NULL, E_CONTACT_PHONE_ISDN, "V11" },
		{ NULL, E_CONTACT_PHONE_MOBILE, "V12" },
		{ NULL, E_CONTACT_PHONE_OTHER, "V13" },
		{ NULL, E_CONTACT_PHONE_OTHER_FAX, "V14" },
		{ NULL, E_CONTACT_PHONE_PAGER, "V15" },
		{ NULL, E_CONTACT_PHONE_PRIMARY, "V16" },
		{ NULL, E_CONTACT_PHONE_RADIO, "V17" },
		{ NULL, E_CONTACT_PHONE_TELEX, "V18" },
		{ NULL, E_CONTACT_PHONE_TTYTDD, "V19" },
		{ "BEGIN:VCARD\r\n"
		  "TEL;Type=msg:V1\r\n"
		  "TEL;TYPE=msg;type=Voice:V2\r\n"
		  "TEL:V3\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_PHONE_ASSISTANT, NULL },
		{ NULL, E_CONTACT_PHONE_BUSINESS, NULL },
		{ NULL, E_CONTACT_PHONE_BUSINESS_2, NULL },
		{ NULL, E_CONTACT_PHONE_BUSINESS_FAX, NULL },
		{ NULL, E_CONTACT_PHONE_CALLBACK, NULL },
		{ NULL, E_CONTACT_PHONE_CAR, NULL },
		{ NULL, E_CONTACT_PHONE_COMPANY, NULL },
		{ NULL, E_CONTACT_PHONE_HOME, NULL },
		{ NULL, E_CONTACT_PHONE_HOME_2, NULL },
		{ NULL, E_CONTACT_PHONE_HOME_FAX, NULL },
		{ NULL, E_CONTACT_PHONE_ISDN, NULL },
		{ NULL, E_CONTACT_PHONE_MOBILE, NULL },
		{ NULL, E_CONTACT_PHONE_OTHER, "V2" },
		{ NULL, E_CONTACT_PHONE_OTHER_FAX, NULL },
		{ NULL, E_CONTACT_PHONE_PAGER, NULL },
		{ NULL, E_CONTACT_PHONE_PRIMARY, NULL },
		{ NULL, E_CONTACT_PHONE_RADIO, NULL },
		{ NULL, E_CONTACT_PHONE_TELEX, NULL },
		{ NULL, E_CONTACT_PHONE_TTYTDD, NULL },
		{ "BEGIN:VCARD\r\n"
		  "TEL;Type=msg:V1\r\n"
		  "TEL:V2\r\n"
		  "TEL;TYPE=msg;type=Voice:V3\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_PHONE_ASSISTANT, NULL },
		{ NULL, E_CONTACT_PHONE_BUSINESS, NULL },
		{ NULL, E_CONTACT_PHONE_BUSINESS_2, NULL },
		{ NULL, E_CONTACT_PHONE_BUSINESS_FAX, NULL },
		{ NULL, E_CONTACT_PHONE_CALLBACK, NULL },
		{ NULL, E_CONTACT_PHONE_CAR, NULL },
		{ NULL, E_CONTACT_PHONE_COMPANY, NULL },
		{ NULL, E_CONTACT_PHONE_HOME, NULL },
		{ NULL, E_CONTACT_PHONE_HOME_2, NULL },
		{ NULL, E_CONTACT_PHONE_HOME_FAX, NULL },
		{ NULL, E_CONTACT_PHONE_ISDN, NULL },
		{ NULL, E_CONTACT_PHONE_MOBILE, NULL },
		{ NULL, E_CONTACT_PHONE_OTHER, "V2" },
		{ NULL, E_CONTACT_PHONE_OTHER_FAX, NULL },
		{ NULL, E_CONTACT_PHONE_PAGER, NULL },
		{ NULL, E_CONTACT_PHONE_PRIMARY, NULL },
		{ NULL, E_CONTACT_PHONE_RADIO, NULL },
		{ NULL, E_CONTACT_PHONE_TELEX, NULL },
		{ NULL, E_CONTACT_PHONE_TTYTDD, NULL },
		{ "BEGIN:VCARD\r\n"
		  "TEL;Type=msg:V1\r\n"
		  "TEL;TYPE=msg;type=Fax:V2\r\n"
		  "TEL:V3\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_PHONE_ASSISTANT, NULL },
		{ NULL, E_CONTACT_PHONE_BUSINESS, NULL },
		{ NULL, E_CONTACT_PHONE_BUSINESS_2, NULL },
		{ NULL, E_CONTACT_PHONE_BUSINESS_FAX, NULL },
		{ NULL, E_CONTACT_PHONE_CALLBACK, NULL },
		{ NULL, E_CONTACT_PHONE_CAR, NULL },
		{ NULL, E_CONTACT_PHONE_COMPANY, NULL },
		{ NULL, E_CONTACT_PHONE_HOME, NULL },
		{ NULL, E_CONTACT_PHONE_HOME_2, NULL },
		{ NULL, E_CONTACT_PHONE_HOME_FAX, NULL },
		{ NULL, E_CONTACT_PHONE_ISDN, NULL },
		{ NULL, E_CONTACT_PHONE_MOBILE, NULL },
		{ NULL, E_CONTACT_PHONE_OTHER, "V3" },
		{ NULL, E_CONTACT_PHONE_OTHER_FAX, "V2" },
		{ NULL, E_CONTACT_PHONE_PAGER, NULL },
		{ NULL, E_CONTACT_PHONE_PRIMARY, NULL },
		{ NULL, E_CONTACT_PHONE_RADIO, NULL },
		{ NULL, E_CONTACT_PHONE_TELEX, NULL },
		{ NULL, E_CONTACT_PHONE_TTYTDD, NULL },
		{ "BEGIN:VCARD\r\n"
		  "TEL;Type=msg:V1\r\n"
		  "TEL:V2\r\n"
		  "TEL;TYPE=msg;type=Fax:V3\r\n"
		  "END:VCARD\r\n",
		  E_CONTACT_PHONE_ASSISTANT, NULL },
		{ NULL, E_CONTACT_PHONE_BUSINESS, NULL },
		{ NULL, E_CONTACT_PHONE_BUSINESS_2, NULL },
		{ NULL, E_CONTACT_PHONE_BUSINESS_FAX, NULL },
		{ NULL, E_CONTACT_PHONE_CALLBACK, NULL },
		{ NULL, E_CONTACT_PHONE_CAR, NULL },
		{ NULL, E_CONTACT_PHONE_COMPANY, NULL },
		{ NULL, E_CONTACT_PHONE_HOME, NULL },
		{ NULL, E_CONTACT_PHONE_HOME_2, NULL },
		{ NULL, E_CONTACT_PHONE_HOME_FAX, NULL },
		{ NULL, E_CONTACT_PHONE_ISDN, NULL },
		{ NULL, E_CONTACT_PHONE_MOBILE, NULL },
		{ NULL, E_CONTACT_PHONE_OTHER, "V2" },
		{ NULL, E_CONTACT_PHONE_OTHER_FAX, "V3" },
		{ NULL, E_CONTACT_PHONE_PAGER, NULL },
		{ NULL, E_CONTACT_PHONE_PRIMARY, NULL },
		{ NULL, E_CONTACT_PHONE_RADIO, NULL },
		{ NULL, E_CONTACT_PHONE_TELEX, NULL },
		{ NULL, E_CONTACT_PHONE_TTYTDD, NULL }
	};

	test_type_param (datas, G_N_ELEMENTS (datas), NULL, NULL);
}

static void
test_type_param_im (const gchar *im_attr,
		    gint first_im_field_id)
{
	TestData datas[] = {
		{ NULL, -1, "V1" }, /* vcard[0] */
		{ NULL, -1, NULL },
		{ NULL, -1, NULL },
		{ NULL, -1, NULL },
		{ NULL, -1, NULL },
		{ NULL, -1, NULL },
		{ NULL, -1, NULL }, /* vcard[1] */
		{ NULL, -1, NULL },
		{ NULL, -1, NULL },
		{ NULL, -1, "V1" },
		{ NULL, -1, NULL },
		{ NULL, -1, NULL },
		{ NULL, -1, "V2" }, /* vcard[2] */
		{ NULL, -1, NULL },
		{ NULL, -1, NULL },
		{ NULL, -1, "V1" },
		{ NULL, -1, NULL },
		{ NULL, -1, NULL },
		{ NULL, -1, "V6" }, /* vcard[3] */
		{ NULL, -1, "V7" },
		{ NULL, -1, "V8" },
		{ NULL, -1, "V1" },
		{ NULL, -1, "V2" },
		{ NULL, -1, "V4" }
	};
	gchar *vcards[4];
	gint ii;

	vcards[0] = g_strdup_printf (
		"BEGIN:VCARD\r\n"
		"%s;Type=home:V1\r\n"
		"END:VCARD\r\n",
		im_attr);

	vcards[1] = g_strdup_printf (
		"BEGIN:VCARD\r\n"
		"%s;TYPE=WORK:V1\r\n"
		"END:VCARD\r\n",
		im_attr);

	vcards[2] = g_strdup_printf (
		"BEGIN:VCARD\r\n"
		"%s;TYPE=x-test,WORK:V1\r\n"
		"%s;type=X-Test;tYPE=Home:V2\r\n"
		"END:VCARD\r\n",
		im_attr, im_attr);

	vcards[3] = g_strdup_printf (
		"BEGIN:VCARD\r\n"
		"%s;type=WORK:V1\r\n"
		"%s;TYPE=x-test,work:V2\r\n"
		"%s:V3\r\n"
		"%s;TYPE=WORK,x-test:V4\r\n"
		"%s:V5\r\n"
		"%s;type=X-Test;tYPE=Home:V6\r\n"
		"%s;tYPE=Home;type=X-Test:V7\r\n"
		"%s;type=Home:V8\r\n"
		"END:VCARD\r\n",
		im_attr, im_attr, im_attr, im_attr, im_attr, im_attr, im_attr, im_attr);

	for (ii = 0; ii < G_N_ELEMENTS (datas); ii++) {
		if (!(ii % 6)) {
			g_assert_cmpint (ii / 6, <, G_N_ELEMENTS (vcards));

			datas[ii].vcard_str = vcards[ii / 6];
		}

		datas[ii].field_id = first_im_field_id + (ii % 6);
	}

	test_type_param (datas, G_N_ELEMENTS (datas), NULL, NULL);

	for (ii = 0; ii < G_N_ELEMENTS (vcards); ii++) {
		g_free (vcards[ii]);
	}
}

static void
test_type_param_xaim (void)
{
	test_type_param_im (EVC_X_AIM, E_CONTACT_IM_AIM_HOME_1);
}

static void
test_type_param_xgadugadu (void)
{
	test_type_param_im (EVC_X_GADUGADU, E_CONTACT_IM_GADUGADU_HOME_1);
}

static void
test_type_param_xgoogletalk (void)
{
	test_type_param_im (EVC_X_GOOGLE_TALK, E_CONTACT_IM_GOOGLE_TALK_HOME_1);
}

static void
test_type_param_xgroupwise (void)
{
	test_type_param_im (EVC_X_GROUPWISE, E_CONTACT_IM_GROUPWISE_HOME_1);
}

static void
test_type_param_xicq (void)
{
	test_type_param_im (EVC_X_ICQ, E_CONTACT_IM_ICQ_HOME_1);
}

static void
test_type_param_xjabber (void)
{
	test_type_param_im (EVC_X_JABBER, E_CONTACT_IM_JABBER_HOME_1);
}

static void
test_type_param_xmsn (void)
{
	test_type_param_im (EVC_X_MSN, E_CONTACT_IM_MSN_HOME_1);
}

static void
test_type_param_xskype (void)
{
	test_type_param_im (EVC_X_SKYPE, E_CONTACT_IM_SKYPE_HOME_1);
}

static void
test_type_param_xyahoo (void)
{
	test_type_param_im (EVC_X_YAHOO, E_CONTACT_IM_YAHOO_HOME_1);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/issues/");

	g_test_add_func ("/Contact/TypeParam/Email", test_type_param_email);
	g_test_add_func ("/Contact/TypeParam/Adr", test_type_param_adr);
	g_test_add_func ("/Contact/TypeParam/Label", test_type_param_label);
	g_test_add_func ("/Contact/TypeParam/Key", test_type_param_key);
	g_test_add_func ("/Contact/TypeParam/Tel", test_type_param_tel);
	g_test_add_func ("/Contact/TypeParam/XAim", test_type_param_xaim);
	g_test_add_func ("/Contact/TypeParam/XGadugadu", test_type_param_xgadugadu);
	g_test_add_func ("/Contact/TypeParam/XGoogletalk", test_type_param_xgoogletalk);
	g_test_add_func ("/Contact/TypeParam/XGroupwise", test_type_param_xgroupwise);
	g_test_add_func ("/Contact/TypeParam/XIcq", test_type_param_xicq);
	g_test_add_func ("/Contact/TypeParam/XJabber", test_type_param_xjabber);
	g_test_add_func ("/Contact/TypeParam/XMsn", test_type_param_xmsn);
	g_test_add_func ("/Contact/TypeParam/XSkype", test_type_param_xskype);
	g_test_add_func ("/Contact/TypeParam/XYahoo", test_type_param_xyahoo);

	return g_test_run ();
}
