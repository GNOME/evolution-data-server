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

using i18n::phonenumbers::PhoneNumber;
using i18n::phonenumbers::PhoneNumberUtil;

struct _EPhoneNumber {
	PhoneNumber priv;
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

static std::string
e_phone_number_make_region_code (const gchar *region_code)
{
	/* Get country code from current locale's address facet if supported */
#if HAVE__NL_ADDRESS_COUNTRY_AB2
	if (region_code == NULL || region_code[0] == '\0')
		region_code = nl_langinfo (_NL_ADDRESS_COUNTRY_AB2);
#endif /* HAVE__NL_ADDRESS_COUNTRY_AB2 */

	/* Extract country code from current locale id if needed */
	if (region_code == NULL || region_code[0] == '\0') {
		/* From outside this is a C library, so we better consult the
		 * C infrastructure instead of std::locale, which might divert. */
		std::string current_region = setlocale (LC_ADDRESS, NULL);

		const std::string::size_type underscore = current_region.find ('_');

		if (underscore != std::string::npos)
			current_region.resize (underscore);

		return current_region;
	}

	return region_code;
}

gint
_e_phone_number_cxx_get_country_code_for_region (const gchar *region_code)
{
	return e_phone_number_util_get_instance ()->GetCountryCodeForRegion (
		e_phone_number_make_region_code (region_code));
}

gchar *
_e_phone_number_cxx_get_default_region ()
{
	return g_strdup (e_phone_number_make_region_code (NULL).c_str ());
}

EPhoneNumber *
_e_phone_number_cxx_from_string (const gchar *phone_number,
                                 const gchar *region_code,
                                 GError **error)
{
	g_return_val_if_fail (NULL != phone_number, NULL);

	const std::string valid_region = e_phone_number_make_region_code (region_code);
	std::auto_ptr<EPhoneNumber> parsed_number(new EPhoneNumber);

	const PhoneNumberUtil::ErrorType err =
#ifdef PHONENUMBER_RAW_INPUT_NEEDED
		e_phone_number_util_get_instance ()->ParseAndKeepRawInput (
			phone_number, valid_region, &parsed_number->priv);
#else /* PHONENUMBER_RAW_INPUT_NEEDED */
		e_phone_number_util_get_instance ()->Parse (
			phone_number, valid_region, &parsed_number->priv);
#endif /* PHONENUMBER_RAW_INPUT_NEEDED */

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
		(phone_number->priv,
		 static_cast<PhoneNumberUtil::PhoneNumberFormat> (format),
		 &formatted_number);

	if (!formatted_number.empty ())
		return g_strdup (formatted_number.c_str ());

	return NULL;
}

static EPhoneNumberCountrySource
e_phone_number_get_country_source (const EPhoneNumber *phone_number)
{
	g_return_val_if_fail (
		phone_number->priv.has_country_code_source (),
		E_PHONE_NUMBER_COUNTRY_FROM_DEFAULT);

	switch (phone_number->priv.country_code_source ()) {
		case PhoneNumber::FROM_NUMBER_WITH_PLUS_SIGN:
			return E_PHONE_NUMBER_COUNTRY_FROM_FQTN;

		case PhoneNumber::FROM_NUMBER_WITH_IDD:
			return E_PHONE_NUMBER_COUNTRY_FROM_IDD;

		/* FROM_NUMBER_WITHOUT_PLUS_SIGN only is used internally
		 * by libphonenumber to properly(???) reconstruct raw input
		 * from PhoneNumberUtil::ParseAndKeepRawInput(). Let's not
		 * bother our users with that barely understandable and
		 * almost undocumented implementation detail.
		 */
		case PhoneNumber::FROM_NUMBER_WITHOUT_PLUS_SIGN:
		case PhoneNumber::FROM_DEFAULT_COUNTRY:
			break;
	}

	return E_PHONE_NUMBER_COUNTRY_FROM_DEFAULT;
}

gint
_e_phone_number_cxx_get_country_code (const EPhoneNumber *phone_number,
                                      EPhoneNumberCountrySource *source)
{
	g_return_val_if_fail (NULL != phone_number, 0);

	if (phone_number->priv.has_country_code ()) {
		if (source)
			*source = e_phone_number_get_country_source (phone_number);

		return phone_number->priv.country_code ();
	}

	return 0;
}

gchar *
_e_phone_number_cxx_get_national_number (const EPhoneNumber *phone_number)
{
	g_return_val_if_fail (NULL != phone_number, NULL);

	std::string national_number;

	e_phone_number_util_get_instance ()->GetNationalSignificantNumber (
			phone_number->priv, &national_number);

	if (!national_number.empty ())
		return g_strdup (national_number.c_str ());

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
		IsNumberMatch (first_number->priv, second_number->priv);

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
