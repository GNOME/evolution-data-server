/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2012,2013 Intel Corporation
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
 * Authors: Mathias Hasselmann <mathias@openismus.com>
 */

#include "evolution-data-server-config.h"

#include <libebook-contacts/libebook-contacts.h>
#include <locale.h>

/* Pick a locale category to set and test. */
#ifdef LC_ADDRESS
/* LC_ADDRESS is a GNU extension. */
#define CATEGORY LC_ADDRESS
#else
/* Mimic the fallback branch in EBookQuery. */
#ifdef G_OS_WIN32
#ifndef LC_MESSAGES
#define LC_MESSAGES LC_CTYPE
#endif
#endif
#define CATEGORY LC_MESSAGES
#endif /* LC_ADDRESS */

static const gchar *match_candidates[] = {
	"not-a-number",
	"+1-617-4663489", "617-4663489", "4663489",
	"+1.408.845.5246", "4088455246", "8455246",
	"+1-857-4663489"
};

#ifdef ENABLE_PHONENUMBER
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
#endif /* ENABLE_PHONENUMBER */

typedef struct {
	gchar				*phone_number;
	gchar				*region_code;
	EPhoneNumberCountrySource	 country_source;
	gint				 country_code;
	gchar				*national_number;
	gchar				*formatted_numbers[4];
} ParseAndFormatData;

static ParseAndFormatData *
parse_and_format_data_new (const gchar *phone_number,
                           const gchar *region_code,
                           EPhoneNumberCountrySource country_source,
                           gint country_code,
                           const gchar *national_number,
                           const gchar *formatted_e164,
                           const gchar *formatted_intl,
                           const gchar *formatted_natl,
                           const gchar *formatted_uri)
{
	ParseAndFormatData *test_data = g_slice_new0 (ParseAndFormatData);

	test_data->phone_number = g_strdup (phone_number);
	test_data->region_code = g_strdup (region_code);
	test_data->country_source = country_source;
	test_data->country_code = country_code;
	test_data->national_number = g_strdup (national_number);
	test_data->formatted_numbers[0] = g_strdup (formatted_e164);
	test_data->formatted_numbers[1] = g_strdup (formatted_intl);
	test_data->formatted_numbers[2] = g_strdup (formatted_natl);
	test_data->formatted_numbers[3] = g_strdup (formatted_uri);

	return test_data;
}

static void
parse_and_format_data_free (gpointer data)
{
	ParseAndFormatData *const test_data = data;

	g_free (test_data->phone_number);
	g_free (test_data->region_code);
	g_free (test_data->national_number);
	g_free (test_data->formatted_numbers[0]);
	g_free (test_data->formatted_numbers[1]);
	g_free (test_data->formatted_numbers[2]);
	g_free (test_data->formatted_numbers[3]);

	g_slice_free (ParseAndFormatData, test_data);
}

static void
test_parse_and_format (gconstpointer data)
{
	const ParseAndFormatData *const test_data = data;
	GError *error = NULL;
	EPhoneNumber *parsed;

	parsed = e_phone_number_from_string (
		test_data->phone_number, test_data->region_code, &error);

#ifdef ENABLE_PHONENUMBER

	{
		EPhoneNumberCountrySource source;
		gchar *national;
		gint i;

		g_assert_cmpint (
			e_phone_number_get_country_code (parsed, &source), ==,
			test_data->country_code);
		g_assert_cmpuint (source, ==, test_data->country_source);

		national = e_phone_number_get_national_number (parsed);
		g_assert_cmpstr (national, ==, test_data->national_number);
		g_free (national);

		g_assert (parsed != NULL);
		g_assert (error == NULL);

		for (i = 0; i < G_N_ELEMENTS (test_data->formatted_numbers); ++i) {
			gchar *formatted = e_phone_number_to_string (parsed, i);
			g_assert (formatted != NULL);
			g_assert_cmpstr (formatted, ==, test_data->formatted_numbers[i]);
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
test_parse_auto_region (void)
{
	GError *error = NULL;
	EPhoneNumber *parsed;

	parsed = e_phone_number_from_string ("212-5423789", NULL, &error);

#ifdef ENABLE_PHONENUMBER

	{
		EPhoneNumberCountrySource source;
		gchar *national;
		gchar *formatted;

		g_assert (parsed != NULL);
		g_assert (error == NULL);

		g_assert_cmpint (e_phone_number_get_country_code (parsed, &source), ==, 1);
		g_assert_cmpuint (source, ==, E_PHONE_NUMBER_COUNTRY_FROM_DEFAULT);

		national = e_phone_number_get_national_number (parsed);
		g_assert_cmpstr (national, ==, "2125423789");
		g_free (national);

		formatted = e_phone_number_to_string (parsed, E_PHONE_NUMBER_FORMAT_E164);
		g_assert_cmpstr (formatted, ==, "+12125423789");
		g_free (formatted);

		e_phone_number_free (parsed);
	}

#else /* ENABLE_PHONENUMBER */

	g_assert (parsed == NULL);
	g_assert (error != NULL);
	g_assert (error->domain == E_PHONE_NUMBER_ERROR);
	g_assert (error->code == E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED);
	g_assert (error->message != NULL);
	g_clear_error (&error);

#endif /* ENABLE_PHONENUMBER */
}

static void
test_compare_numbers (gconstpointer data)
{
	const size_t n = GPOINTER_TO_UINT (data);
	const size_t i = n % G_N_ELEMENTS (match_candidates);
	const size_t j = n / G_N_ELEMENTS (match_candidates);

#ifdef ENABLE_PHONENUMBER
	const gboolean error_expected = !(i && j);
#else /* ENABLE_PHONENUMBER */
	const gboolean error_expected = TRUE;
#endif /* ENABLE_PHONENUMBER */

	EPhoneNumberMatch actual_match;
	GError *error = NULL;

	actual_match = e_phone_number_compare_strings (
		match_candidates[i],
		match_candidates[j],
		&error);

#ifdef ENABLE_PHONENUMBER
	g_assert_cmpuint (actual_match, ==, expected_matches[n]);
#else /* ENABLE_PHONENUMBER */
	g_assert_cmpuint (actual_match, ==, E_PHONE_NUMBER_MATCH_NONE);
#endif /* ENABLE_PHONENUMBER */

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

static void
test_supported (void)
{
#ifdef ENABLE_PHONENUMBER
	g_assert (e_phone_number_is_supported ());
#else /* ENABLE_PHONENUMBER */
	g_assert (!e_phone_number_is_supported ());
#endif /* ENABLE_PHONENUMBER */
}

static void
test_country_code_for_region (void)
{
	GError *error = NULL;
	gint code;

	g_assert_cmpstr (setlocale (CATEGORY, NULL), ==, "en_US.UTF-8");

#ifdef ENABLE_PHONENUMBER

	code = e_phone_number_get_country_code_for_region ("CH", &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert_cmpint (code, ==, 41);

	code = e_phone_number_get_country_code_for_region (NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert_cmpint (code, ==, 1);

	code = e_phone_number_get_country_code_for_region ("C", &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert_cmpint (code, ==, 0);

	code = e_phone_number_get_country_code_for_region ("", &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert_cmpint (code, ==, 1);

#else /* ENABLE_PHONENUMBER */

	code = e_phone_number_get_country_code_for_region ("CH", &error);

	g_assert (error != NULL);
	g_assert (error->domain == E_PHONE_NUMBER_ERROR);
	g_assert (error->code == E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED);
	g_assert (error->message != NULL);
	g_assert_cmpint (code, ==, 0);

#endif /* ENABLE_PHONENUMBER */
}

static void
test_default_region (void)
{
	GError *error = NULL;
	gchar *country;

	g_assert_cmpstr (setlocale (CATEGORY, NULL), ==, "en_US.UTF-8");
	country = e_phone_number_get_default_region (&error);

#ifdef ENABLE_PHONENUMBER

	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert_cmpstr (country, ==, "US");

#else /* ENABLE_PHONENUMBER */

	g_assert (error != NULL);
	g_assert (error->domain == E_PHONE_NUMBER_ERROR);
	g_assert (error->code == E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED);
	g_assert (error->message != NULL);
	g_assert_cmpstr (country, ==, NULL);

#endif /* ENABLE_PHONENUMBER */

	g_free (country);
}

gint
main (gint argc,
      gchar **argv)
{
	size_t i, j;

	if (setlocale (LC_ALL, "en_US.UTF-8") == NULL) {
		g_message ("Failed to set locale to en_US.UTF-8");
		g_message ("Skipping all /ebook-phone-number/* tests");
		return 0;
	}

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add_func (
		"/ebook-phone-number/supported",
		test_supported);

	g_test_add_data_func_full (
		"/ebook-phone-number/parse-and-format/i164",
		parse_and_format_data_new (
			"+493011223344", NULL,
			E_PHONE_NUMBER_COUNTRY_FROM_FQTN,
			49, "3011223344",
			"+493011223344",
			"+49 30 11223344",
			"030 11223344",
			"tel:+49-30-11223344"),
		test_parse_and_format,
		parse_and_format_data_free);
	g_test_add_data_func_full (
		"/ebook-phone-number/parse-and-format/national",
		parse_and_format_data_new (
			"(030) 22334-455", "DE",
			E_PHONE_NUMBER_COUNTRY_FROM_DEFAULT,
			49, "3022334455",
			"+493022334455",
			"+49 30 22334455",
			"030 22334455",
			"tel:+49-30-22334455"),
		test_parse_and_format,
		parse_and_format_data_free);
	g_test_add_data_func_full (
		"/ebook-phone-number/parse-and-format/national2",
		parse_and_format_data_new (
			"0049 (30) 22334-455", "DE",
			E_PHONE_NUMBER_COUNTRY_FROM_IDD,
			49, "3022334455",
			"+493022334455",
			"+49 30 22334455",
			"030 22334455",
			"tel:+49-30-22334455"),
		test_parse_and_format,
		parse_and_format_data_free);
	g_test_add_data_func_full (
		"/ebook-phone-number/parse-and-format/international",
		parse_and_format_data_new (
			"+1 212 33445566", NULL,
			E_PHONE_NUMBER_COUNTRY_FROM_FQTN,
			1, "21233445566",
			"+121233445566",
			"+1 21233445566",
			"21233445566",
			"tel:+1-21233445566"),
		test_parse_and_format,
		parse_and_format_data_free);
	g_test_add_data_func_full (
		"/ebook-phone-number/parse-and-format/rfc3966",
		parse_and_format_data_new (
			"tel:+358-71-44556677", NULL,
			E_PHONE_NUMBER_COUNTRY_FROM_FQTN,
			358, "7144556677",
			"+3587144556677",
			"+358 71 44556677",
			"071 44556677",
			"tel:+358-71-44556677"),
		test_parse_and_format,
		parse_and_format_data_free);

	g_test_add_func (
		"/ebook-phone-number/parse-and-format/bad-number",
		test_parse_bad_number);

	g_test_add_func (
		"/ebook-phone-number/parse-and-format/auto-region",
		test_parse_auto_region);

	#ifdef ENABLE_PHONENUMBER
	g_assert_cmpint (
		G_N_ELEMENTS (match_candidates) * G_N_ELEMENTS (match_candidates),
		==, G_N_ELEMENTS (expected_matches));
	#endif

	for (i = 0; i < G_N_ELEMENTS (match_candidates); ++i) {
		for (j = 0; j < G_N_ELEMENTS (match_candidates); ++j) {
			const size_t n = j + i * G_N_ELEMENTS (match_candidates);
			gchar *path = g_strdup_printf (
				"/ebook-phone-number/compare/%s/%s",
				match_candidates[i], match_candidates[j]);

			g_test_add_data_func (
				path,
				GUINT_TO_POINTER (n),
				test_compare_numbers);
			g_free (path);
		}
	}

	g_test_add_func (
		"/ebook-phone-number/country-code/for-region",
		test_country_code_for_region);
	g_test_add_func (
		"/ebook-phone-number/country-code/default-region",
		test_default_region);

	return g_test_run ();
}
