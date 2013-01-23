/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2012,2013 Intel Corporation
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

#include "e-phone-number.h"

#include <glib/gi18n-lib.h>

#include "e-phone-number-private.h"

#ifndef ENABLE_PHONENUMBER

/* With phonenumber support enabled the boxed type must be defined in
 * the C++ code because we cannot compute the size of C++ types here. */
G_DEFINE_BOXED_TYPE (EPhoneNumber,
                     e_phone_number,
                     e_phone_number_copy,
                     e_phone_number_free)

#endif /* ENABLE_PHONENUMBER */

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
		return _("Invalid country code");
	case E_PHONE_NUMBER_ERROR_TOO_SHORT_AFTER_IDD:
		return _("Remaining text after the country code is to short for a phone number");
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
 * e_phone_number_from_string:
 * @phone_number: the phone number to parse
 * @country_code: (allow-none): a 2-letter country code, or %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Parses the string passed in @phone_number. Note that no validation is
 * performed whether the recognized phone number is valid for a particular
 * region.
 *
 * The 2-letter country code passed in @country_code only is used if the
 * @phone_number is not written in international format. The applications's
 * currently locale is consulted if %NULL gets passed for @country_code.
 * If the number is guaranteed to start with a '+' followed by the country
 * calling code, then "ZZ" can be passed here.
 *
 * Returns: (transfer full): a new EPhoneNumber instance on success,
 * or %NULL on error. Call e_phone_number_free() to release this instance.
 *
 * Since: 3.8
 **/
EPhoneNumber *
e_phone_number_from_string (const gchar *phone_number,
                            const gchar *country_code,
                            GError **error)
{
#ifdef ENABLE_PHONENUMBER

	return _e_phone_number_cxx_from_string (phone_number, country_code, error);

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

	/* NOTE: This calls for a dedicated return value, but I sense broken
	 * client code that only checks for E_PHONE_NUMBER_MATCH_NONE and then
	 * treats the "not-implemented" return value as a match */
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
#ifdef ENABLE_PHONENUMBER

	return _e_phone_number_cxx_compare_strings (first_number, second_number, error);

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
