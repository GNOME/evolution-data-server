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

#include "e-phone-utils.h"

/* GLib headers */
#include <glib/gi18n-lib.h>

#ifdef ENABLE_PHONENUMBER

/* C++ standard library */
#include <string>

/* system headers */
#include <langinfo.h>

/* libphonenumber */
#include <phonenumbers/logger.h>
#include <phonenumbers/phonenumberutil.h>

using i18n::phonenumbers::PhoneNumberUtil;

#endif /* ENABLE_PHONENUMBER */

struct _EPhoneNumber {
#ifdef ENABLE_PHONENUMBER
	i18n::phonenumbers::PhoneNumber phone_number;
#endif /* ENABLE_PHONENUMBER */
};

G_DEFINE_BOXED_TYPE (EPhoneNumber,
                     e_phone_number,
                     e_phone_number_copy,
                     e_phone_number_free)

GQuark
e_phone_number_error_quark (void)
{
	static GQuark q = 0;

	if (q == 0)
		q = g_quark_from_static_string ("e-phone-number-error-quark");

	return q;
}

#ifdef ENABLE_PHONENUMBER

static PhoneNumberUtil *
e_phone_number_util_get_instance (void)
{
	static PhoneNumberUtil *instance = NULL;

	if (g_once_init_enter (&instance)) {
		/* FIXME: Ideally PhoneNumberUtil would not be a singleton,
		 * so that we could safely tweak it's attributes without
		 * influencing other users of the library. */
		PhoneNumberUtil *new_instance = PhoneNumberUtil::GetInstance ();

		/* Disable all logging: libphonenumber is pretty verbose. */
		new_instance->SetLogger (new i18n::phonenumbers::NullLogger);
		g_once_init_leave (&instance, new_instance);
	}

	return instance;
}

static EPhoneNumberError
e_phone_number_error_code (PhoneNumberUtil::ErrorType error)
{
	switch (error) {
	case PhoneNumberUtil::NO_PARSING_ERROR:
		g_return_val_if_reached (E_PHONE_NUMBER_ERROR_UNKNOWN);
	case PhoneNumberUtil::INVALID_COUNTRY_CODE_ERROR:
		return E_PHONE_NUMBER_ERROR_INVALID_COUNTRY_CODE;
	case PhoneNumberUtil::NOT_A_NUMBER:
		return E_PHONE_NUMBER_ERROR_NOT_A_NUMBER;
	case PhoneNumberUtil::TOO_SHORT_AFTER_IDD:
		return E_PHONE_NUMBER_ERROR_TOO_SHORT_AFTER_IDD;
	case PhoneNumberUtil::TOO_SHORT_NSN:
		return E_PHONE_NUMBER_ERROR_TOO_SHORT;
	case PhoneNumberUtil::TOO_LONG_NSN:
		return E_PHONE_NUMBER_ERROR_TOO_LONG;
	}

	/* Please file a bug that we can add a proper error code. */
	g_return_val_if_reached (E_PHONE_NUMBER_ERROR_UNKNOWN);
}

#endif /* ENABLE_PHONENUMBER */

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

static void
e_phone_number_set_error (GError **error,
                          EPhoneNumberError code)
{
	const gchar *message = e_phone_number_error_to_string (code);
	g_set_error_literal (error, E_PHONE_NUMBER_ERROR, code, message);
}

gboolean
e_phone_number_is_supported (void)
{
#ifdef ENABLE_PHONENUMBER
	return TRUE;
#else /* ENABLE_PHONENUMBER */
	return FALSE;
#endif /* ENABLE_PHONENUMBER */
}

EPhoneNumber *
e_phone_number_from_string (const gchar *phone_number,
                            const gchar *country_code,
                            GError **error)
{
	EPhoneNumber *parsed_number = NULL;

	g_return_val_if_fail (NULL != phone_number, NULL);

#ifdef ENABLE_PHONENUMBER

	if (country_code == NULL) {
#if HAVE__NL_ADDRESS_COUNTRY_AB2
		country_code = nl_langinfo (_NL_ADDRESS_COUNTRY_AB2);
#else /* HAVE__NL_ADDRESS_COUNTRY_AB2 */
#error Cannot resolve default 2-letter country code. Find a replacement for _NL_ADDRESS_COUNTRY_AB2 or implement code to parse the locale name.
#endif /* HAVE__NL_ADDRESS_COUNTRY_AB2 */
	}

	parsed_number = new EPhoneNumber;

	const PhoneNumberUtil::ErrorType err =
		e_phone_number_util_get_instance ()->Parse (phone_number, country_code,
		                                            &parsed_number->phone_number);

	if (err != PhoneNumberUtil::NO_PARSING_ERROR) {
		e_phone_number_set_error (error, e_phone_number_error_code (err));

		delete parsed_number;
		parsed_number = NULL;
	}

#else /* ENABLE_PHONENUMBER */

	e_phone_number_set_error (error, E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED);

#endif /* ENABLE_PHONENUMBER */

	return parsed_number;
}

gchar *
e_phone_number_to_string (const EPhoneNumber *phone_number,
                          EPhoneNumberFormat format)
{
	g_return_val_if_fail (NULL != phone_number, NULL);

#ifdef ENABLE_PHONENUMBER

	std::string formatted_number;

	e_phone_number_util_get_instance ()->Format
		(phone_number->phone_number,
		 static_cast<PhoneNumberUtil::PhoneNumberFormat> (format),
		 &formatted_number);

	if (!formatted_number.empty ())
		return g_strdup (formatted_number.c_str ());

#else /* ENABLE_PHONENUMBER */

	g_warning ("%s: The library was built without phone number support.", G_STRFUNC);

#endif /* ENABLE_PHONENUMBER */

	return NULL;
}

#ifdef ENABLE_PHONENUMBER

static EPhoneNumberMatch
e_phone_number_match (PhoneNumberUtil::MatchType match_type)
{
	switch (match_type) {
	case PhoneNumberUtil::NO_MATCH:
	case PhoneNumberUtil::INVALID_NUMBER:
		return E_PHONE_NUMBER_MATCH_NONE;
	case PhoneNumberUtil::SHORT_NSN_MATCH:
		return E_PHONE_NUMBER_MATCH_SHORT;
	case PhoneNumberUtil::NSN_MATCH:
		return E_PHONE_NUMBER_MATCH_NATIONAL;
	case PhoneNumberUtil::EXACT_MATCH:
		return E_PHONE_NUMBER_MATCH_EXACT;
	}

	g_return_val_if_reached (E_PHONE_NUMBER_MATCH_NONE);
}

#endif /* ENABLE_PHONENUMBER */

EPhoneNumberMatch
e_phone_number_compare	(const EPhoneNumber *first_number,
			 const EPhoneNumber *second_number)
{
	g_return_val_if_fail (NULL != first_number, E_PHONE_NUMBER_MATCH_NONE);
	g_return_val_if_fail (NULL != second_number, E_PHONE_NUMBER_MATCH_NONE);

#ifdef ENABLE_PHONENUMBER

	const PhoneNumberUtil::MatchType match_type =
		e_phone_number_util_get_instance ()->
		IsNumberMatch (first_number->phone_number,
		               second_number->phone_number);

	g_warn_if_fail (match_type != PhoneNumberUtil::INVALID_NUMBER);
	return e_phone_number_match (match_type);

#else /* ENABLE_PHONENUMBER */

	g_warn_if_reached ();
	return E_PHONE_NUMBER_MATCH_NONE;

#endif /* ENABLE_PHONENUMBER */
}

EPhoneNumberMatch
e_phone_number_compare_strings	(const gchar *first_number,
				 const gchar *second_number,
				 GError **error)
{
	g_return_val_if_fail (NULL != first_number, E_PHONE_NUMBER_MATCH_NONE);
	g_return_val_if_fail (NULL != second_number, E_PHONE_NUMBER_MATCH_NONE);

	EPhoneNumberMatch result = E_PHONE_NUMBER_MATCH_NONE;

#ifdef ENABLE_PHONENUMBER

	const PhoneNumberUtil::MatchType match_type =
		e_phone_number_util_get_instance ()->
		IsNumberMatchWithTwoStrings (first_number, second_number);

	if (match_type == PhoneNumberUtil::INVALID_NUMBER) {
		e_phone_number_set_error (error, E_PHONE_NUMBER_ERROR_NOT_A_NUMBER);
	} else {
		result = e_phone_number_match (match_type);
	}

#else /* ENABLE_PHONENUMBER */

	e_phone_number_set_error (error, E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED);

#endif /* ENABLE_PHONENUMBER */

	return result;
}

EPhoneNumber *
e_phone_number_copy (const EPhoneNumber *phone_number)
{
	if (phone_number)
		return new EPhoneNumber (*phone_number);

	return NULL;
}

void
e_phone_number_free (EPhoneNumber *phone_number)
{
	delete phone_number;
}
