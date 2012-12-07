/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright Copyright (C) 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Mathias Hasselmann <mathias@openismus.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libedataserver/libedataserver.h>

static const char *match_candidates[] = {
	"not-a-number",
	"+1-617-4663489", "617-4663489", "4663489",
	"+1.408.845.5246", "4088455246", "8455246",
	"+1-857-4663489"
};

static const EPhoneNumberMatch expected_matches[] = {
	/* not a number */
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,

	/* +1-617-4663489 */
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_EXACT,
	E_PHONE_NUMBER_MATCH_NATIONAL,
	E_PHONE_NUMBER_MATCH_SHORT,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,

	/* 617-4663489 */
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NATIONAL,
	E_PHONE_NUMBER_MATCH_NATIONAL,
	E_PHONE_NUMBER_MATCH_SHORT,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,

	/* 4663489 */
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_SHORT,
	E_PHONE_NUMBER_MATCH_SHORT,
	E_PHONE_NUMBER_MATCH_NATIONAL, /* XXX - Google, really? I'd expect a full match here. */
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_SHORT,

	/* +1.408.845.5246 */
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_EXACT,
	E_PHONE_NUMBER_MATCH_NATIONAL,
	E_PHONE_NUMBER_MATCH_SHORT,
	E_PHONE_NUMBER_MATCH_NONE,

	/* 4088455246 */
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NATIONAL,
	E_PHONE_NUMBER_MATCH_NATIONAL,
	E_PHONE_NUMBER_MATCH_SHORT,
	E_PHONE_NUMBER_MATCH_NONE,

	/* 8455246 */
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_SHORT,
	E_PHONE_NUMBER_MATCH_SHORT,
	E_PHONE_NUMBER_MATCH_NATIONAL, /* XXX - Google, really?  I'd expect a full match here. */
	E_PHONE_NUMBER_MATCH_NONE,

	/* +1-857-4663489 */
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_SHORT,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_EXACT
};

static void
test_parse_and_format (gconstpointer data)
{
	GError *error = NULL;
	EPhoneNumber *parsed;
	gchar **params;

	params = g_strsplit (data, "/", G_MAXINT);
	g_assert_cmpint (g_strv_length (params), ==, 6);

	parsed = e_phone_number_from_string (params[0], params[1], &error);

#ifdef ENABLE_PHONENUMBER

	{
		gchar **test_numbers;
		gint i;

		test_numbers = params + 2;

		g_assert (parsed != NULL);
		g_assert (error == NULL);

		for (i = 0; test_numbers[i]; ++i) {
			gchar *formatted;

			formatted = e_phone_number_to_string (parsed, i);
			g_assert (formatted != NULL);
			g_assert_cmpstr (formatted, ==, test_numbers[i]);
			g_free (formatted);
		}

		e_phone_number_free (parsed);
	}

#else /* ENABLE_PHONENUMBER */

	g_assert (parsed == NULL);
	g_assert (error != NULL);
	g_assert (error->domain == E_PHONE_NUMBER_ERROR);
	g_assert (error->code == E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED);
	g_assert (error->message != NULL);

#endif /* ENABLE_PHONENUMBER */

	g_clear_error (&error);
	g_strfreev (params);
}

static void
test_parse_bad_number (void)
{
	GError *error = NULL;
	EPhoneNumber *parsed;

	parsed = e_phone_number_from_string ("+1-NOT-A-NUMBER", "US", &error);

	g_assert (parsed == NULL);
	g_assert (error != NULL);
	g_assert (error->domain == E_PHONE_NUMBER_ERROR);
#ifdef ENABLE_PHONENUMBER
	g_assert (error->code == E_PHONE_NUMBER_ERROR_NOT_A_NUMBER);
#else /* ENABLE_PHONENUMBER */
	g_assert (error->code == E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED);
#endif /* ENABLE_PHONENUMBER */
	g_assert (error->message != NULL);

	g_clear_error (&error);
}

static void
test_compare_numbers (gconstpointer data)
{
	const size_t n = GPOINTER_TO_UINT (data);
	const size_t i = n % G_N_ELEMENTS (match_candidates);
	const size_t j = n / G_N_ELEMENTS (match_candidates);

#ifdef ENABLE_PHONENUMBER
	const gboolean error_expected = !(i && j) ;
#else /* ENABLE_PHONENUMBER */
	const gboolean error_expected = TRUE;
#endif /* ENABLE_PHONENUMBER */

	EPhoneNumberMatch actual_match;
	GError *error = NULL;

	actual_match = e_phone_number_compare_strings (match_candidates[i],
	                                               match_candidates[j],
	                                               &error);

	g_assert_cmpuint (actual_match, ==, expected_matches[n]);

	if (!error_expected) {
		g_assert (error == NULL);
	} else {
		g_assert (error != NULL);
		g_assert (error->domain == E_PHONE_NUMBER_ERROR);
#ifdef ENABLE_PHONENUMBER
		g_assert (error->code == E_PHONE_NUMBER_ERROR_NOT_A_NUMBER);
#else /* ENABLE_PHONENUMBER */
		g_assert (error->code == E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED);
#endif /* ENABLE_PHONENUMBER */
		g_assert (error->message != NULL);

		g_clear_error (&error);
	}
}

gint
main (gint argc,
      gchar **argv)
{
	size_t i, j;

	g_type_init ();

	g_test_init (&argc, &argv, NULL);

	g_test_add_data_func
		("/e-phone-utils-test/parse-and-format/i164",
		 "+493011223344//+493011223344/+49 30 11223344/030 11223344/tel:+49-30-11223344",
		 test_parse_and_format);
	g_test_add_data_func
		("/e-phone-utils-test/parse-and-format/national",
		 "(030) 22334-455/DE/+493022334455/+49 30 22334455/030 22334455/tel:+49-30-22334455",
		 test_parse_and_format);
	g_test_add_data_func
		("/e-phone-utils-test/parse-and-format/international",
		 "+1 212 33445566//+121233445566/+1 21233445566/21233445566/tel:+1-21233445566",
		 test_parse_and_format);
	g_test_add_data_func
		("/e-phone-utils-test/parse-and-format/rfc3966",
		 "tel:+358-71-44556677//+3587144556677/+358 71 44556677/071 44556677/tel:+358-71-44556677",
		 test_parse_and_format);

	g_test_add_func
		("/e-phone-utils-test/parse-and-format/BadNumber",
		 test_parse_bad_number);

	g_assert_cmpint (G_N_ELEMENTS (match_candidates) * G_N_ELEMENTS (match_candidates),
			 ==, G_N_ELEMENTS (expected_matches));

	for (i = 0; i < G_N_ELEMENTS (match_candidates); ++i) {
		for (j = 0; j < G_N_ELEMENTS (match_candidates); ++j) {
			const size_t n = j + i * G_N_ELEMENTS (match_candidates);
			char *path = g_strdup_printf ("/e-phone-utils-test/compare/%s/%s",
			                              match_candidates[i], match_candidates[j]);

			g_test_add_data_func (path, GUINT_TO_POINTER (n), test_compare_numbers);
			g_free (path);
		}
	}

	return g_test_run ();
}
