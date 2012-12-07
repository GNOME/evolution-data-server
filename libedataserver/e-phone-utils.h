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

/* NOTE: Keeping API documentation in this header file because gtkdoc-mkdb
 * explicitly only scans .h and .c files, but ignores .cpp files. */

/**
 * SECTION: e-phone-utils
 * @include: libedataserver/libedataserver.h
 * @short_description: Phone number support
 *
 * This modules provides utility functions for parsing and formatting
 * phone numbers. Under the hood it uses Google's libphonenumber.
 **/

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_PHONE_UTILS_H
#define E_PHONE_UTILS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define E_TYPE_PHONE_NUMBER (e_phone_number_get_type ())
#define E_PHONE_NUMBER_ERROR (e_phone_number_error_quark ())

/**
 * EPhoneNumberFormat:
 * @E_PHONE_NUMBER_FORMAT_E164: format according E.164: "+493055667788".
 * @E_PHONE_NUMBER_FORMAT_INTERNATIONAL: a formatted phone number always
 * starting with the country calling code: "+49 30 55667788".
 * @E_PHONE_NUMBER_FORMAT_NATIONAL: a formatted phone number in national
 * scope, that is without country code: "(030) 55667788".
 * @E_PHONE_NUMBER_FORMAT_RFC3966: a tel: URL according to RFC 3966:
 * "tel:+49-30-55667788".
 *
 * The supported formatting rules for phone numbers.
 **/
typedef enum {
	E_PHONE_NUMBER_FORMAT_E164,
	E_PHONE_NUMBER_FORMAT_INTERNATIONAL,
	E_PHONE_NUMBER_FORMAT_NATIONAL,
	E_PHONE_NUMBER_FORMAT_RFC3966
} EPhoneNumberFormat;

/**
 * EPhoneNumberMatch:
 * @E_PHONE_NUMBER_MATCH_NONE: The phone numbers did not match.
 * @E_PHONE_NUMBER_MATCH_EXACT: The phone numbers matched exactly.
 * @E_PHONE_NUMBER_MATCH_NATIONAL: There was no country code for at least
 * one of the numbers, but the national parts matched.
 * @E_PHONE_NUMBER_MATCH_SHORT: There was no country code for at least
 * one of the numbers, but one number might be part (suffix) of the other.
 *
 * The quality of a phone number match.

 * Let's consider the phone number "+1-221-5423789", then comparing with
 * "+1.221.542.3789" we have get E_PHONE_NUMBER_MATCH_EXACT because country
 * code, region code and local number are matching. Comparing with "2215423789"
 * will result in E_PHONE_NUMBER_MATCH_NATIONAL because the country code is
 * missing, but the national portion is matching. Finally comparing with
 * "5423789" gives E_PHONE_NUMBER_MATCH_SHORT. For more detail have a look at
 * the following table:
 *
 * <informaltable border="1" align="center">
 *  <colgroup>
 *   <col width="20%" />
 *   <col width="20%" />
 *   <col width="20%" />
 *   <col width="20%" />
 *   <col width="20%" />
 *  </colgroup>
 *  <tbody>
 *   <tr>
 *    <th></th>
 *    <th align="center">+1-617-5423789</th>
 *    <th align="center">+1-221-5423789</th>
 *    <th align="center">221-5423789</th>
 *    <th align="center">5423789</th>
 *   </tr><tr>
 *    <th align="right">+1-617-5423789</th>
 *    <td align="center">exact</td>
 *    <td align="center">none</td>
 *    <td align="center">none</td>
 *    <td align="center">short</td>
 *   </tr><tr>
 *    <th align="right">+1-221-5423789</th>
 *    <td align="center">none</td>
 *    <td align="center">exact</td>
 *    <td align="center">national</td>
 *    <td align="center">short</td>
 *   </tr><tr>
 *    <th align="right">221-5423789</th>
 *    <td align="center">none</td>
 *    <td align="center">national</td>
 *    <td align="center">national</td>
 *    <td align="center">short</td>
 *   </tr><tr>
 *    <th align="right">5423789</th>
 *    <td align="center">short</td>
 *    <td align="center">short</td>
 *    <td align="center">short</td>
 *    <td align="center">short</td>
 *   </tr>
 *  </tbody>
 * </informaltable>
 */
typedef enum {
	E_PHONE_NUMBER_MATCH_NONE,
	E_PHONE_NUMBER_MATCH_EXACT,
	E_PHONE_NUMBER_MATCH_NATIONAL = 1024,
	E_PHONE_NUMBER_MATCH_SHORT = 2048
} EPhoneNumberMatch;

/**
 * EPhoneNumberError:
 * @E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED: the library was built without phone
 * number support
 * @E_PHONE_NUMBER_ERROR_UNKNOWN: the phone number parser reported an yet
 * unkown error code.
 * @E_PHONE_NUMBER_ERROR_INVALID_COUNTRY_CODE: the supplied phone number has an
 * invalid country code.
 * @E_PHONE_NUMBER_ERROR_NOT_A_NUMBER: the supplied text is not a phone number.
 * @E_PHONE_NUMBER_ERROR_TOO_SHORT_AFTER_IDD: the remaining text after the
 * country code is to short for a phone number.
 * @E_PHONE_NUMBER_ERROR_TOO_SHORT: the text is too short for a phone number.
 * @E_PHONE_NUMBER_ERROR_TOO_LONG: the text is too long for a phone number.
 *
 * Numeric description of a phone number related error.
 **/
typedef enum {
	E_PHONE_NUMBER_ERROR_NOT_IMPLEMENTED,
	E_PHONE_NUMBER_ERROR_UNKNOWN,
	E_PHONE_NUMBER_ERROR_NOT_A_NUMBER,
	E_PHONE_NUMBER_ERROR_INVALID_COUNTRY_CODE,
	E_PHONE_NUMBER_ERROR_TOO_SHORT_AFTER_IDD,
	E_PHONE_NUMBER_ERROR_TOO_SHORT,
	E_PHONE_NUMBER_ERROR_TOO_LONG,
} EPhoneNumberError;

/**
 * EPhoneNumber:
 * This opaque type describes a parsed phone number. It can be copied using
 * e_phone_number_copy(). To release it call e_phone_number_free().
 */
typedef struct _EPhoneNumber EPhoneNumber;

GType			e_phone_number_get_type		(void) G_GNUC_CONST;
GQuark			e_phone_number_error_quark	(void) G_GNUC_CONST;

/**
 * e_phone_number_is_supported:
 *
 * Checks if phone number support is available. It is recommended to call this
 * function before using any of the phone-utils functions to ensure that the
 * required functionality is available, and to pick alternative mechnisms if
 * needed.
 *
 * Returns: %TRUE if phone number support is available.
 */
gboolean		e_phone_number_is_supported	(void) G_GNUC_CONST;

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
EPhoneNumber *		e_phone_number_from_string	(const gchar *phone_number,
							 const gchar *country_code,
							 GError **error);

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
gchar *			e_phone_number_to_string	(const EPhoneNumber *phone_number,
							 EPhoneNumberFormat format);

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
 */
EPhoneNumberMatch	e_phone_number_compare		(const EPhoneNumber *first_number,
							 const EPhoneNumber *second_number);

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
 */
EPhoneNumberMatch	e_phone_number_compare_strings	(const gchar *first_number,
							 const gchar *second_number,
							 GError **error);

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
EPhoneNumber *		e_phone_number_copy		(const EPhoneNumber *phone_number);

/**
 * e_phone_number_free:
 * @phone_number: the EPhoneNumber to free
 *
 * Released the memory occupied by @phone_number.
 *
 * Since: 3.8
 **/
void			e_phone_number_free		(EPhoneNumber *phone_number);

G_END_DECLS

#endif /* E_BOOK_BACKEND_FILE_PHONE_UTILS_H */
