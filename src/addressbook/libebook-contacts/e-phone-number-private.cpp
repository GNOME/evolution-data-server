/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2012,2013 Intel Corporation
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Mathias Hasselmann <mathias@openismus.com>
 */

#include "evolution-data-server-config.h"

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
_e_phone_number_cxx_region_code_from_locale (const gchar *locale)
{
	std::string current_region = locale;
	const std::string::size_type uscore = current_region.find ('_');

	if (uscore != std::string::npos) {
		const std::string::size_type n = std::min (uscore + 3, current_region.length ());

		if (n == current_region.length() || not ::isalpha(current_region.at(n)))
			current_region = current_region.substr (uscore + 1, 2);
	}

	if (current_region.length() != 2)
		return "US";

	return current_region;
}

static std::string
_e_phone_number_cxx_make_region_code (const gchar *region_code)
{
	if (region_code && strlen (region_code) > 2)
		return _e_phone_number_cxx_region_code_from_locale (region_code);

	/* Get two-letter country code from current locale's address facet if supported */
#if HAVE__NL_ADDRESS_COUNTRY_AB2
	if (region_code == NULL || region_code[0] == '\0')
		region_code = nl_langinfo (_NL_ADDRESS_COUNTRY_AB2);
#endif /* HAVE__NL_ADDRESS_COUNTRY_AB2 */

	/* Extract two-letter country code from current locale id if needed.
	 * From outside this is a C library, so we better consult the
         * C infrastructure instead of std::locale, which might divert. */
	if (region_code == NULL || region_code[0] == '\0')
		return _e_phone_number_cxx_region_code_from_locale (setlocale (LC_ADDRESS, NULL));

	return region_code;
}

gint
_e_phone_number_cxx_get_country_code_for_region (const gchar *region_code)
{
	return e_phone_number_util_get_instance ()->GetCountryCodeForRegion (
		_e_phone_number_cxx_make_region_code (region_code));
}

gchar *
_e_phone_number_cxx_get_default_region ()
{
	return g_strdup (_e_phone_number_cxx_make_region_code (NULL).c_str ());
}

static bool
_e_phone_number_cxx_parse (const std::string &phone_number,
                           const std::string &region,
                           PhoneNumber *parsed_number,
                           GError **error)
{
	const PhoneNumberUtil::ErrorType err =
#ifdef PHONENUMBER_RAW_INPUT_NEEDED
		e_phone_number_util_get_instance ()->ParseAndKeepRawInput (
			phone_number, region, parsed_number);
#else /* PHONENUMBER_RAW_INPUT_NEEDED */
		e_phone_number_util_get_instance ()->Parse (
			phone_number, region, parsed_number);
#endif /* PHONENUMBER_RAW_INPUT_NEEDED */

	if (err != PhoneNumberUtil::NO_PARSING_ERROR) {
		_e_phone_number_set_error (error, e_phone_number_error_code (err));
		return false;
	}

	return true;
}

EPhoneNumber *
_e_phone_number_cxx_from_string (const gchar *phone_number,
                                 const gchar *region_code,
                                 GError **error)
{
	g_return_val_if_fail (NULL != phone_number, NULL);

	const std::string valid_region = _e_phone_number_cxx_make_region_code (region_code);
	std::auto_ptr<EPhoneNumber> parsed_number(new EPhoneNumber);

	if (!_e_phone_number_cxx_parse (
		phone_number, valid_region, &parsed_number->priv, error))
		return NULL;

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
_e_phone_number_cxx_get_country_source (const PhoneNumber &phone_number)
{
	g_return_val_if_fail (
		phone_number.has_country_code_source (),
		E_PHONE_NUMBER_COUNTRY_FROM_DEFAULT);

	switch (phone_number.country_code_source ()) {
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

static EPhoneNumberCountrySource
_e_phone_number_cxx_get_country_source (const EPhoneNumber *phone_number)
{
	return _e_phone_number_cxx_get_country_source (phone_number->priv);
}

gint
_e_phone_number_cxx_get_country_code (const EPhoneNumber *phone_number,
                                      EPhoneNumberCountrySource *source)
{
	g_return_val_if_fail (NULL != phone_number, 0);

	if (phone_number->priv.has_country_code ()) {
		if (source)
			*source = _e_phone_number_cxx_get_country_source (phone_number);

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

static EPhoneNumberMatch
_e_phone_number_cxx_compare (const PhoneNumber &first_number_in,
                             const PhoneNumber &second_number_in)
{
	PhoneNumber first_number(first_number_in);
	PhoneNumber second_number(second_number_in);
	const EPhoneNumberCountrySource cs1 =
		_e_phone_number_cxx_get_country_source (first_number);
	const EPhoneNumberCountrySource cs2 =
		_e_phone_number_cxx_get_country_source (second_number);

	/* Must clear guessed country codes, otherwise libphonenumber
	 * includes them in the comparison, leading to false
	 * negatives. */
	if (cs1 == E_PHONE_NUMBER_COUNTRY_FROM_DEFAULT)
		first_number.clear_country_code();
	if (cs2 == E_PHONE_NUMBER_COUNTRY_FROM_DEFAULT)
		second_number.clear_country_code();

	PhoneNumberUtil::MatchType match_type =
		e_phone_number_util_get_instance ()->IsNumberMatch (
			first_number, second_number);

	/* XXX Work around a bug in libphonenumber's C++ implementation
	 *
	 * If we got a short match and either of the numbers is missing
	 * the country code, then make sure both of them have an arbitrary
	 * country code specified... if that matches exactly, then we promote
	 * the short number match to a national match.
	 */
	if (match_type == PhoneNumberUtil::SHORT_NSN_MATCH &&
	    (first_number.country_code() == 0 ||
	     second_number.country_code() == 0)) {

		second_number.set_country_code (1);
		first_number.set_country_code (1);

		PhoneNumberUtil::MatchType second_match =
			e_phone_number_util_get_instance ()->IsNumberMatch (
				first_number, second_number);

		if (second_match == PhoneNumberUtil::EXACT_MATCH)
			match_type = PhoneNumberUtil::NSN_MATCH;
	}

	g_warn_if_fail (match_type != PhoneNumberUtil::INVALID_NUMBER);
	return e_phone_number_match (match_type);
}

EPhoneNumberMatch
_e_phone_number_cxx_compare (const EPhoneNumber *first_number,
                             const EPhoneNumber *second_number)
{
	g_return_val_if_fail (NULL != first_number, E_PHONE_NUMBER_MATCH_NONE);
	g_return_val_if_fail (NULL != second_number, E_PHONE_NUMBER_MATCH_NONE);

	return _e_phone_number_cxx_compare (first_number->priv, second_number->priv);
}

EPhoneNumberMatch
_e_phone_number_cxx_compare_strings (const gchar *first_number,
                                     const gchar *second_number,
                                     const gchar *region_code,
                                     GError **error)
{
	g_return_val_if_fail (NULL != first_number, E_PHONE_NUMBER_MATCH_NONE);
	g_return_val_if_fail (NULL != second_number, E_PHONE_NUMBER_MATCH_NONE);

	const std::string region = _e_phone_number_cxx_make_region_code (region_code);
	PhoneNumber pn1, pn2;

	if (!_e_phone_number_cxx_parse (first_number, region, &pn1, error))
		return E_PHONE_NUMBER_MATCH_NONE;
	if (!_e_phone_number_cxx_parse (second_number, region, &pn2, error))
		return E_PHONE_NUMBER_MATCH_NONE;

	return _e_phone_number_cxx_compare (pn1, pn2);
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
