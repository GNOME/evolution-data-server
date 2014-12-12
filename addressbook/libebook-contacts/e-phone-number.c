/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2012,2013 Intel Corporation
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Mathias Hasselmann <mathias@openismus.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-phone-number.h"

#include <glib/gi18n-lib.h>

#include "e-phone-number-private.h"

G_DEFINE_BOXED_TYPE (
	EPhoneNumber, e_phone_number,
	e_phone_number_copy, e_phone_number_free)

G_DEFINE_QUARK (e-phone-number-error-quark, e_phone_number_error)

static const gchar *
e_phone_number_error_to_string (EPhoneNumberError code)
{
	switch (code) {
	case E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED:
		return _("The library was built without phone number support.");
	case E_PHONE_NUMBER_ERROR_UNKNOWN:
		return _("The phone number parser reported an yet unkown error code.");
	case E_PHONE_NUMBER_ERROR_NOT_A_NUMBER:
		return _("Not a phone number");
	case E_PHONE_NUMBER_ERROR_INVALID_COUNTRY_CODE:
		return _("Invalid country calling code");
	case E_PHONE_NUMBER_ERROR_TOO_SHORT_AFTER_IDD:
		return _("Remaining text after the country calling code is too short for a phone number");
	case E_PHONE_NUMBER_ERROR_TOO_SHORT:
		return _("Text is too short for a phone number");
	case E_PHONE_NUMBER_ERROR_TOO_LONG:
		return _("Text is too long for a phone number");
	}

	return _("Unknown error");
}

void
_e_phone_number_set_error (GError **error,
                           EPhoneNumberError code)
{
	const gchar *message = e_phone_number_error_to_string (code);
	g_set_error_literal (error, E_PHONE_NUMBER_ERROR, code, message);
}

/**
 * e_phone_number_is_supported:
 *
 * Checks if phone number support is available. It is recommended to call this
 * function before using any of the phone-utils functions to ensure that the
 * required functionality is available, and to pick alternative mechnisms if
 * needed.
 *
 * Returns: %TRUE if phone number support is available.
 *
 * Since: 3.8
 **/
gboolean
e_phone_number_is_supported (void)
{
#ifdef ENABLE_PHONENUMBER

	return TRUE;

#else /* ENABLE_PHONENUMBER */

	return FALSE;

#endif /* ENABLE_PHONENUMBER */
}

/**
 * e_phone_number_get_country_code_for_region:
 * @region_code: (allow-none): a two-letter country code, a locale name, or
 * %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Retrieves the preferred country calling code for @region_code,
 * e.g. 358 for "fi" or 1 for "en_US@UTF-8".
 *
 * If %NULL is passed for @region_code the default region as returned by
 * e_phone_number_get_default_region() is used.
 *
 * Returns: a valid country calling code, or zero if an unknown region
 * code was passed.
 *
 * Since: 3.8
 */
gint
e_phone_number_get_country_code_for_region (const gchar *region_code,
                                            GError **error)
{
#ifdef ENABLE_PHONENUMBER

	return _e_phone_number_cxx_get_country_code_for_region (region_code);

#else /* ENABLE_PHONENUMBER */

	_e_phone_number_set_error (error, E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED);
	return 0;

#endif /* ENABLE_PHONENUMBER */
}

/**
 * e_phone_number_get_default_region:
 * @error: (out): a #GError to set an error, if any
 *
 * Retrieves the current two-letter country code that's used by default for
 * parsing phone numbers in e_phone_number_from_string(). It can be useful
 * to store this number before parsing a bigger number of phone numbers.
 *
 * The result of this functions depends on the current setup of the
 * %LC_ADDRESS category: If that category provides a reasonable value
 * for %_NL_ADDRESS_COUNTRY_AB2 this value is returned. Otherwise the
 * locale name configured for %LC_ADDRESS is parsed.
 *
 * Returns: (transfer full): a newly allocated string containing the
 * current locale's two-letter code for phone number parsing.
 *
 * Since: 3.8
 */
gchar *
e_phone_number_get_default_region (GError **error)
{
#ifdef ENABLE_PHONENUMBER

	return _e_phone_number_cxx_get_default_region ();

#else /* ENABLE_PHONENUMBER */

	_e_phone_number_set_error (error, E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED);
	return NULL;

#endif /* ENABLE_PHONENUMBER */
}

/**
 * e_phone_number_normalize:
 * @phone_number: phone number string
 * @error: (out): a #GError to set an error, if any
 *
 * Normalizes provided phone number string.
 *
 * Returns: (transfer full): a newly allocated string containing the
 * normalized phone number.
 *
 * Since: 3.12
 */
gchar *
e_phone_number_normalize (const char *phone_number, GError **error)
{
#ifdef ENABLE_PHONENUMBER

	return _e_phone_number_cxx_normalize (phone_number);

#else /* ENABLE_PHONENUMBER */

	_e_phone_number_set_error (error, E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED);
	return NULL;

#endif /* ENABLE_PHONENUMBER */
}

/**
 * e_phone_number_from_string:
 * @phone_number: the phone number to parse
 * @region_code: (allow-none): a two-letter country code, or %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Parses the string passed in @phone_number. Note that no validation is
 * performed whether the recognized phone number is valid for a particular
 * region.
 *
 * The two-letter country code passed in @region_code only is used if the
 * @phone_number is not written in international format. The application's
 * default region as returned by e_phone_number_get_default_region() is used
 * if @region_code is %NULL.
 *
 * If the number is guaranteed to start with a '+' followed by the country
 * calling code, then "ZZ" can be passed for @region_code.
 *
 * Returns: (transfer full): a new EPhoneNumber instance on success,
 * or %NULL on error. Call e_phone_number_free() to release this instance.
 *
 * Since: 3.8
 **/
EPhoneNumber *
e_phone_number_from_string (const gchar *phone_number,
                            const gchar *region_code,
                            GError **error)
{
#ifdef ENABLE_PHONENUMBER

	return _e_phone_number_cxx_from_string (phone_number, region_code, error);

#else /* ENABLE_PHONENUMBER */

	_e_phone_number_set_error (error, E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED);
	return NULL;

#endif /* ENABLE_PHONENUMBER */
}

/**
 * e_phone_number_to_string:
 * @phone_number: the phone number to format
 * @format: the phone number format to apply
 *
 * Describes the @phone_number according to the rules applying to @format.
 *
 * Returns: (transfer full): A formatted string for @phone_number.
 *
 * Since: 3.8
 **/
gchar *
e_phone_number_to_string (const EPhoneNumber *phone_number,
                          EPhoneNumberFormat format)
{
#ifdef ENABLE_PHONENUMBER

	return _e_phone_number_cxx_to_string (phone_number, format);

#else /* ENABLE_PHONENUMBER */

	/* The EPhoneNumber instance must be invalid. We'd also bail out with
	 * a warning if phone numbers are supported. Any code triggering this
	 * is broken and should be fixed. */
	g_warning ("%s: The library was built without phone number support.", G_STRFUNC);
	return NULL;

#endif /* ENABLE_PHONENUMBER */
}

/**
 * e_phone_number_get_country_code:
 * @phone_number: the phone number to query
 * @source: an optional location for storing the phone number's origin, or %NULL
 *
 * Queries the @phone_number's country calling code and optionally stores the country
 * calling code's origin in @source. For instance when parsing "+1-617-5423789" this
 * function would return one and assing E_PHONE_NUMBER_COUNTRY_FROM_FQTN to @source.
 *
 * Returns: A valid country calling code, or zero if no code is known.
 *
 * Since: 3.8
 **/
gint
e_phone_number_get_country_code (const EPhoneNumber *phone_number,
                                 EPhoneNumberCountrySource *source)
{
#ifdef ENABLE_PHONENUMBER

	return _e_phone_number_cxx_get_country_code (phone_number, source);

#else /* ENABLE_PHONENUMBER */

	/* The EPhoneNumber instance must be invalid. We'd also bail out with
	 * a warning if phone numbers are supported. Any code triggering this
	 * is broken and should be fixed. */
	g_warning ("%s: The library was built without phone number support.", G_STRFUNC);
	return 0;

#endif /* ENABLE_PHONENUMBER */
}

/**
 * e_phone_number_get_national_number:
 * @phone_number: the phone number to query
 *
 * Queries the national portion of @phone_number without any call-out
 * prefixes. For instance when parsing "+1-617-5423789" this function would
 * return the string "6175423789".
 *
 * Returns: (transfer full): The national portion of @phone_number.
 *
 * Since: 3.8
 **/
gchar *
e_phone_number_get_national_number (const EPhoneNumber *phone_number)
{
#ifdef ENABLE_PHONENUMBER

	return _e_phone_number_cxx_get_national_number (phone_number);

#else /* ENABLE_PHONENUMBER */

	/* The EPhoneNumber instance must be invalid. We'd also bail out with
	 * a warning if phone numbers are supported. Any code triggering this
	 * is broken and should be fixed. */
	g_warning ("%s: The library was built without phone number support.", G_STRFUNC);
	return NULL;

#endif /* ENABLE_PHONENUMBER */
}

/**
 * e_phone_number_compare:
 * @first_number: the first EPhoneNumber to compare
 * @second_number: the second EPhoneNumber to compare
 *
 * Compares two phone numbers.
 *
 * Returns: The quality of matching for the two phone numbers.
 *
 * Since: 3.8
 **/
EPhoneNumberMatch
e_phone_number_compare (const EPhoneNumber *first_number,
                        const EPhoneNumber *second_number)
{
#ifdef ENABLE_PHONENUMBER

	return _e_phone_number_cxx_compare (first_number, second_number);

#else /* ENABLE_PHONENUMBER */

	/* The EPhoneNumber instance must be invalid. We'd also bail out with
	 * a warning if phone numbers are supported. Any code triggering this
	 * is broken and should be fixed. */
	g_warning ("%s: The library was built without phone number support.", G_STRFUNC);
	return E_PHONE_NUMBER_MATCH_NONE;

#endif /* ENABLE_PHONENUMBER */
}

/**
 * e_phone_number_compare_strings:
 * @first_number: the first EPhoneNumber to compare
 * @second_number: the second EPhoneNumber to compare
 * @error: (out): a #GError to set an error, if any
 *
 * Compares two phone numbers.
 *
 * Returns: The quality of matching for the two phone numbers.
 *
 * Since: 3.8
 **/
EPhoneNumberMatch
e_phone_number_compare_strings (const gchar *first_number,
                                const gchar *second_number,
                                GError **error)
{
	return e_phone_number_compare_strings_with_region (
		first_number, second_number, NULL, error);
}

/**
 * e_phone_number_compare_strings_with_region:
 * @first_number: the first EPhoneNumber to compare
 * @second_number: the second EPhoneNumber to compare
 * @region_code: (allow-none): a two-letter country code, or %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Compares two phone numbers within the context of @region_code.
 *
 * Returns: The quality of matching for the two phone numbers.
 *
 * Since: 3.8
 **/
EPhoneNumberMatch
e_phone_number_compare_strings_with_region (const gchar *first_number,
                                            const gchar *second_number,
                                            const gchar *region_code,
                                            GError **error)
{
#ifdef ENABLE_PHONENUMBER

	return _e_phone_number_cxx_compare_strings (
		first_number, second_number, region_code, error);

#else /* ENABLE_PHONENUMBER */

	_e_phone_number_set_error (error, E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED);
	return E_PHONE_NUMBER_MATCH_NONE;

#endif /* ENABLE_PHONENUMBER */
}

/**
 * e_phone_number_copy:
 * @phone_number: the EPhoneNumber to copy
 *
 * Makes a copy of @phone_number.
 *
 * Returns: (transfer full): A newly allocated EPhoneNumber instance.
 * Call e_phone_number_free() to release this instance.
 *
 * Since: 3.8
 **/
EPhoneNumber *
e_phone_number_copy (const EPhoneNumber *phone_number)
{
#ifdef ENABLE_PHONENUMBER

	return _e_phone_number_cxx_copy (phone_number);

#else /* ENABLE_PHONENUMBER */

	/* Without phonenumber support there are no instances.
	 * Any non-NULL value is a programming error in this setup. */
	g_warn_if_fail (phone_number == NULL);
	return NULL;

#endif /* ENABLE_PHONENUMBER */
}

/**
 * e_phone_number_free:
 * @phone_number: the EPhoneNumber to free
 *
 * Released the memory occupied by @phone_number.
 *
 * Since: 3.8
 **/
void
e_phone_number_free (EPhoneNumber *phone_number)
{
#ifdef ENABLE_PHONENUMBER

	_e_phone_number_cxx_free (phone_number);

#else /* ENABLE_PHONENUMBER */

	/* Without phonenumber support there are no instances.
	 * Any non-NULL value is a programming error in this setup. */
	g_warn_if_fail (phone_number == NULL);

#endif /* ENABLE_PHONENUMBER */
}

/**
 * match_exit_code:
 * @phone: phone number string
 * @max: number of characters to be checked (indexed from 0 or -1 if whole string should be checked)
 * @up_to (out): number of matched characters (indexed from 0)
 *
 * Checks if given phone string begins with valid exit code.
 * Tries to match exit codes from longest to shortest ones.
 *
 * @Returns: TRUE in case that string begins with valid exit code, false otherwise.
 *
 * Since: 3.12
 **/
static gboolean
match_exit_code(const char* phone, int max, int* up_to)
{
        if (max == 4 || max == -1)
        {
                *up_to = 4;
                if (strncmp (phone, "00414", 5) == 0) return TRUE;
                if (strncmp (phone, "00468", 5) == 0) return TRUE;
                if (strncmp (phone, "00456", 5) == 0) return TRUE;
                if (strncmp (phone, "00444", 5) == 0) return TRUE;
        }

        if (max == 3 || max == -1)
        {
                *up_to = 3;
                if (strncmp (phone, "0011", 4) == 0) return TRUE;
                if (strncmp (phone, "0014", 4) == 0) return TRUE;
                if (strncmp (phone, "0015", 4) == 0) return TRUE;
                if (strncmp (phone, "0021", 4) == 0) return TRUE;
                if (strncmp (phone, "0023", 4) == 0) return TRUE;
                if (strncmp (phone, "0031", 4) == 0) return TRUE;
                if (strncmp (phone, "1230", 4) == 0) return TRUE;
                if (strncmp (phone, "1200", 4) == 0) return TRUE;
                if (strncmp (phone, "1220", 4) == 0) return TRUE;
                if (strncmp (phone, "1810", 4) == 0) return TRUE;
                if (strncmp (phone, "1690", 4) == 0) return TRUE;
                if (strncmp (phone, "1710", 4) == 0) return TRUE;
        }

	if (max == 2 || max == -1)
        {
                *up_to = 2;
                if (strncmp (phone, "011", 3) == 0) return TRUE;
                if (strncmp (phone, "001", 3) == 0) return TRUE;
                if (strncmp (phone, "006", 3) == 0) return TRUE;
                if (strncmp (phone, "007", 3) == 0) return TRUE;
                if (strncmp (phone, "008", 3) == 0) return TRUE;
                if (strncmp (phone, "119", 3) == 0) return TRUE;
                if (strncmp (phone, "005", 3) == 0) return TRUE;
                if (strncmp (phone, "007", 3) == 0) return TRUE;
                if (strncmp (phone, "009", 3) == 0) return TRUE;
                if (strncmp (phone, "990", 3) == 0) return TRUE;
                if (strncmp (phone, "994", 3) == 0) return TRUE;
                if (strncmp (phone, "999", 3) == 0) return TRUE;
                if (strncmp (phone, "013", 3) == 0) return TRUE;
                if (strncmp (phone, "014", 3) == 0) return TRUE;
                if (strncmp (phone, "018", 3) == 0) return TRUE;
                if (strncmp (phone, "010", 3) == 0) return TRUE;
                if (strncmp (phone, "009", 3) == 0) return TRUE;
                if (strncmp (phone, "002", 3) == 0) return TRUE;
        }

        if (max == 1 || max == -1)
        {
                *up_to = 1;
                if (strncmp (phone, "00", 2) == 0) return TRUE;
                if (strncmp (phone, "99", 2) == 0) return TRUE;
        }

        if (max == 0 || max == -1)
        {
                *up_to = 0;
                if (strncmp (phone, "+", 1) == 0) return TRUE;
        }

        return FALSE;
}

/**
 * match_exit_code_reverse:
 * @phone: phone number string
 * @max: number of characters to be checked (indexed from 0 or -1 if whole string should be checked)
 * @up_to (out): number of matched characters (indexed from 0)
 *
 * Checks if given phone string begins with valid exit code.
 * Tries to match exit codes from shortest to longest ones.
 *
 * @Returns: TRUE in case that string begins with valid exit code, false otherwise.
 *
 * Since: 3.12
 **/
static gboolean
match_exit_code_reverse(const char* phone, int max, int* up_to)
{
        if (max == 0 || max == -1)
        {
                *up_to = 0;
                if (strncmp (phone, "+", 1) == 0) return TRUE;
        }

        if (max == 1 || max == -1)
        {
                *up_to = 1;
                if (strncmp (phone, "00", 2) == 0) return TRUE;
                if (strncmp (phone, "99", 2) == 0) return TRUE;
        }

        if (max == 2 || max == -1)
        {
                *up_to = 2;
                if (strncmp (phone, "011", 3) == 0) return TRUE;
                if (strncmp (phone, "001", 3) == 0) return TRUE;
                if (strncmp (phone, "006", 3) == 0) return TRUE;
                if (strncmp (phone, "007", 3) == 0) return TRUE;
                if (strncmp (phone, "008", 3) == 0) return TRUE;
                if (strncmp (phone, "119", 3) == 0) return TRUE;
                if (strncmp (phone, "005", 3) == 0) return TRUE;
                if (strncmp (phone, "007", 3) == 0) return TRUE;
                if (strncmp (phone, "009", 3) == 0) return TRUE;
                if (strncmp (phone, "990", 3) == 0) return TRUE;
                if (strncmp (phone, "994", 3) == 0) return TRUE;
                if (strncmp (phone, "999", 3) == 0) return TRUE;
                if (strncmp (phone, "013", 3) == 0) return TRUE;
                if (strncmp (phone, "014", 3) == 0) return TRUE;
                if (strncmp (phone, "018", 3) == 0) return TRUE;
                if (strncmp (phone, "010", 3) == 0) return TRUE;
                if (strncmp (phone, "009", 3) == 0) return TRUE;
                if (strncmp (phone, "002", 3) == 0) return TRUE;
        }

	if (max == 3 || max == -1)
        {
                *up_to = 3;
                if (strncmp (phone, "0011", 4) == 0) return TRUE;
                if (strncmp (phone, "0014", 4) == 0) return TRUE;
                if (strncmp (phone, "0015", 4) == 0) return TRUE;
                if (strncmp (phone, "0021", 4) == 0) return TRUE;
                if (strncmp (phone, "0023", 4) == 0) return TRUE;
                if (strncmp (phone, "0031", 4) == 0) return TRUE;
                if (strncmp (phone, "1230", 4) == 0) return TRUE;
                if (strncmp (phone, "1200", 4) == 0) return TRUE;
                if (strncmp (phone, "1220", 4) == 0) return TRUE;
                if (strncmp (phone, "1810", 4) == 0) return TRUE;
                if (strncmp (phone, "1690", 4) == 0) return TRUE;
                if (strncmp (phone, "1710", 4) == 0) return TRUE;
        }

        if (max == 4 || max == -1)
        {
                *up_to = 4;
                if (strncmp (phone, "00414", 5) == 0) return TRUE;
                if (strncmp (phone, "00468", 5) == 0) return TRUE;
                if (strncmp (phone, "00456", 5) == 0) return TRUE;
                if (strncmp (phone, "00444", 5) == 0) return TRUE;
        }

        return FALSE;
}

/**
 * is_exit_code_and_cc:
 * @phone: phone number string
 * @up_to: number of characters to be checked (indexed from 0)
 *
 * Checks if given phone string begins with valid exit code and country code.
 * After sucessfully matching exit code, any further 1-3 digits 
 * will be recognized as valid country code.
 *
 * @Returns: TRUE in case that string begins with valid exit code 
 * and country code, false otherwise.
 *
 * Since: 3.12
 **/
static gboolean
is_exit_code_and_cc(const char* phone, int up_to)
{
        int matched_len = 0;
        if (match_exit_code(phone, -1, &matched_len))
        {
                if (up_to - matched_len -1 >= 0 &&
                    up_to - matched_len - 1 < 3)
                        return TRUE;
        }
        if (match_exit_code_reverse(phone, -1, &matched_len))
        {
                if (up_to - matched_len -1 >= 0 &&
                    up_to - matched_len - 1 < 3)
                        return TRUE;
        }

        return FALSE;
}

/**
 * is_trunk:
 * @phone: phone number string
 * @up_to: number of characters to be checked (indexed from 0)
 *
 * Checks if given phone string begins with trunk code.
 *
 * @Returns: TRUE in case that string begins with trunk code. 
 *
 * Since: 3.12
 **/
static gboolean
is_trunk(const char* phone, int up_to)
{
        if (up_to > 1) return FALSE;
        if (phone[0] == '0' && up_to == 0) return TRUE;
        if (phone[0] == '0' && phone[1] == '1' && up_to == 1) return TRUE;       //Mexico trunk code
        if (phone[0] == '0' && phone[1] == '6' && up_to == 1) return TRUE;       //Hungary trunk code
        if (phone[0] == '8' && phone[1] == '0' && up_to == 1) return TRUE;       //Belarus trunk code


        if (phone[0] == '1' && up_to == 0) return TRUE;                          //Jamaica trunk code
        if (phone[0] == '8' && up_to == 0) return TRUE;                          //Kazakhstan,Lituana trunk code


        return FALSE;
}

/**
 * e_phone_number_equal:
 * @phone1: first phone number string
 * @phone2: second phone number string
 *
 * Compares two phone number strings.
 *
 * @Returns: TRUE in case that phone number matches. false otherwise. 
 *
 * Since: 3.12
 **/
gboolean
e_phone_number_equal (const gchar *phone1,
		      const gchar *phone2)
{
	int a = 0;
	int b = 0;
	int matched = 0;
        if (!phone1 || !phone2) {
		return FALSE;
        }

        a = strlen (phone1) - 1;
        b = strlen (phone2) - 1;

	/*
 	 * Check (in reverse order) number of matching digits
 	 */
        while (a >= 0 && b >= 0) {
                if (phone1[a] != phone2[b]) {
                        break;
                }
                a--;
                b--;
                matched++;
        }

	//if both phone numbers matched completly, they are equal
        if (a < 0 && b < 0) {
		return TRUE;
        }

	//if one of numbers matched completly, 
	//and total number of matched digits is greater than 7,
	//assume also that numbers are equal
        if (matched >= 7 && (a < 0 || b < 0)) {
		return TRUE;
        }

	/*
         * Check if first phone number begins with trunk code
         * and second phone number begins exit code + country code
         */ 
	if (is_trunk(phone1, a) && is_exit_code_and_cc(phone2, b)) {
		return TRUE;
        }

	/*
         * Check opposite condition
         */ 
        if (is_trunk(phone2, b) && is_exit_code_and_cc(phone1, a)) {
		return TRUE;
        }

	return FALSE;
}
