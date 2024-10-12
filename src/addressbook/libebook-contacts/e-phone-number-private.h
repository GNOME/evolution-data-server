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

/* NOTE: Keeping API documentation in this header file because gtkdoc-mkdb
 * explicitly only scans .h and .c files, but ignores .cpp files. */

/*
 * This modules provides utility functions for parsing and formatting
 * phone numbers. Under the hood it uses Google's libphonenumber.
 */
#if !defined (__LIBEBOOK_CONTACTS_H_INSIDE__) && !defined (LIBEBOOK_CONTACTS_COMPILATION)
#error "Only <libebook-contacts/libebook-contacts.h> should be included directly."
#endif

#ifndef E_PHONE_NUMBER_PRIVATE_H
#define E_PHONE_NUMBER_PRIVATE_H

#include "e-phone-number.h"

G_BEGIN_DECLS

#if __GNUC__ >= 4
#define E_PHONE_NUMBER_LOCAL __attribute__ ((visibility ("hidden")))
#else
#define E_PHONE_NUMBER_LOCAL
#endif

/* defined and used in e-phone-number.c, but also used by e-phone-number-private.cpp */

E_PHONE_NUMBER_LOCAL void		_e_phone_number_set_error		(GError **error,
										 EPhoneNumberError code);

#ifdef ENABLE_PHONENUMBER

/* defined in e-phone-number-private.cpp, and used by by e-phone-number.c */

E_PHONE_NUMBER_LOCAL gint		_e_phone_number_cxx_get_country_code_for_region
										(const gchar *region_code);
E_PHONE_NUMBER_LOCAL gchar *		_e_phone_number_cxx_get_default_region	(void);

E_PHONE_NUMBER_LOCAL EPhoneNumber *	_e_phone_number_cxx_from_string		(const gchar *phone_number,
										 const gchar *region_code,
										 GError **error);
E_PHONE_NUMBER_LOCAL gchar *		_e_phone_number_cxx_to_string		(const EPhoneNumber *phone_number,
										 EPhoneNumberFormat format);
E_PHONE_NUMBER_LOCAL gint		_e_phone_number_cxx_get_country_code	(const EPhoneNumber *phone_number,
										 EPhoneNumberCountrySource *source);
E_PHONE_NUMBER_LOCAL gchar *		_e_phone_number_cxx_get_national_number	(const EPhoneNumber *phone_number);

E_PHONE_NUMBER_LOCAL EPhoneNumberMatch	_e_phone_number_cxx_compare		(const EPhoneNumber *first_number,
										 const EPhoneNumber *second_number);
E_PHONE_NUMBER_LOCAL EPhoneNumberMatch	_e_phone_number_cxx_compare_strings	(const gchar *first_number,
										 const gchar *second_number,
										 const gchar *region_code,
										 GError **error);
E_PHONE_NUMBER_LOCAL EPhoneNumber *	_e_phone_number_cxx_copy		(const EPhoneNumber *phone_number);
E_PHONE_NUMBER_LOCAL void		_e_phone_number_cxx_free		(EPhoneNumber *phone_number);

#endif /* ENABLE_PHONENUMBER */

G_END_DECLS

#endif /* E_PHONE_NUMBER_PRIVATE_H */
