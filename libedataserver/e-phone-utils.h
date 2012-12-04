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
 * EPhoneNumberError:
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
	E_PHONE_NUMBER_ERROR_INVALID_COUNTRY_CODE = 1,
	E_PHONE_NUMBER_ERROR_NOT_A_NUMBER,
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
 * @phone_number is not written in international format. If the number is
 * guaranteed to start with a '+' followed by the country calling code,
 * then "ZZ" can be passed here.
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
