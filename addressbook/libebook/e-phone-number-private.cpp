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

#ifndef ENABLE_PHONENUMBER
#error Phone number support must be enabled for this file
#endif /* ENABLE_PHONENUMBER */

#include "e-phone-number-private.h"

/* C++ standard library */
#include <string>

/* system headers */
#include <langinfo.h>
#include <locale.h>

/* libphonenumber */
#include <phonenumbers/logger.h>
#include <phonenumbers/phonenumberutil.h>

using i18n::phonenumbers::PhoneNumberUtil;

struct _EPhoneNumber {
	i18n::phonenumbers::PhoneNumber phone_number;
};

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

EPhoneNumber *
_e_phone_number_cxx_from_string (const gchar *phone_number,
                                 const gchar *region_code,
                                 GError **error)
{
	g_return_val_if_fail (NULL != phone_number, NULL);

	std::string valid_region;

	/* Get country code from current locale's address facet if supported */
#if HAVE__NL_ADDRESS_COUNTRY_AB2
	if (region_code == NULL || region_code[0] == '\0')
		region_code = nl_langinfo (_NL_ADDRESS_COUNTRY_AB2);
#endif /* HAVE__NL_ADDRESS_COUNTRY_AB2 */

	/* Extract country code from current locale id if needed */
	if (region_code == NULL || region_code[0] == '\0') {
		/* From outside this is a C library, so we better consult the
		 * C infrastructure instead of std::locale, which might divert. */
		valid_region = setlocale (LC_ADDRESS, NULL);

		const std::string::size_type underscore = valid_region.find ('_');

		if (underscore != std::string::npos)
			valid_region.resize (underscore);
	} else {
		valid_region = region_code;
	}

	/* Now finally parse the phone number */
	std::auto_ptr<EPhoneNumber> parsed_number(new EPhoneNumber);

	const PhoneNumberUtil::ErrorType err =
		e_phone_number_util_get_instance ()->Parse (phone_number, valid_region,
		                                            &parsed_number->phone_number);

	if (err != PhoneNumberUtil::NO_PARSING_ERROR) {
		_e_phone_number_set_error (error, e_phone_number_error_code (err));
		return NULL;
	}

	return parsed_number.release ();
}

gchar *
_e_phone_number_cxx_to_string (const EPhoneNumber *phone_number,
                               EPhoneNumberFormat format)
{
	g_return_val_if_fail (NULL != phone_number, NULL);

	std::string formatted_number;

	e_phone_number_util_get_instance ()->Format
		(phone_number->phone_number,
		 static_cast<PhoneNumberUtil::PhoneNumberFormat> (format),
		 &formatted_number);

	if (!formatted_number.empty ())
		return g_strdup (formatted_number.c_str ());

	return NULL;
}

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

EPhoneNumberMatch
_e_phone_number_cxx_compare (const EPhoneNumber *first_number,
                             const EPhoneNumber *second_number)
{
	g_return_val_if_fail (NULL != first_number, E_PHONE_NUMBER_MATCH_NONE);
	g_return_val_if_fail (NULL != second_number, E_PHONE_NUMBER_MATCH_NONE);

	const PhoneNumberUtil::MatchType match_type =
		e_phone_number_util_get_instance ()->
		IsNumberMatch (first_number->phone_number,
		               second_number->phone_number);

	g_warn_if_fail (match_type != PhoneNumberUtil::INVALID_NUMBER);
	return e_phone_number_match (match_type);
}

EPhoneNumberMatch
_e_phone_number_cxx_compare_strings (const gchar *first_number,
                                     const gchar *second_number,
                                     GError **error)
{
	EPhoneNumberMatch result = E_PHONE_NUMBER_MATCH_NONE;

	g_return_val_if_fail (NULL != first_number, E_PHONE_NUMBER_MATCH_NONE);
	g_return_val_if_fail (NULL != second_number, E_PHONE_NUMBER_MATCH_NONE);

	const PhoneNumberUtil::MatchType match_type =
		e_phone_number_util_get_instance ()->
		IsNumberMatchWithTwoStrings (first_number, second_number);

	if (match_type == PhoneNumberUtil::INVALID_NUMBER) {
		_e_phone_number_set_error (error, E_PHONE_NUMBER_ERROR_NOT_A_NUMBER);
	} else {
		result = e_phone_number_match (match_type);
	}

	return result;
}

EPhoneNumber *
_e_phone_number_cxx_copy (const EPhoneNumber *phone_number)
{
	if (phone_number)
		return new EPhoneNumber (*phone_number);

	return NULL;
}

void
_e_phone_number_cxx_free (EPhoneNumber *phone_number)
{
	delete phone_number;
}
