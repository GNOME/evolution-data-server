/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
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
 * Author: Tristan Van Berkom <tristanvb@openismus.com>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

/* ICU includes */
#include <unicode/uclean.h>
#include <unicode/ucol.h>
#include <unicode/ustring.h>

#include "e-collator.h"

#define CONVERT_BUFFER_LEN        512
#define COLLATION_KEY_BUFFER_LEN  1024
#define LOCALE_BUFFER_LEN         256

#define ENABLE_DEBUGGING 0

G_DEFINE_QUARK (e-collator-error-quark, e_collator_error)

G_DEFINE_BOXED_TYPE (ECollator,
		     e_collator,
		     e_collator_ref, 
		     e_collator_unref)

struct _ECollator
{
	UCollator *coll;

	gint       ref_count;
};

/*****************************************************
 *                ICU Helper Functions               *
 *****************************************************/
#if ENABLE_DEBUGGING
static void
print_available_locales (void)
{
	UErrorCode status = U_ZERO_ERROR;
	UChar result[100];
	gchar printable[100 * 4];
	gint count, i;

	u_init (&status);

	g_printerr ("List of available locales (default locale is: %s)\n", uloc_getDefault());

	count = uloc_countAvailable();
	for (i = 0; i < count; i++) {
		UEnumeration *keywords;
		const gchar *keyword;

		uloc_getDisplayName(uloc_getAvailable(i), NULL, result, 100, &status);

		u_austrcpy (printable, result);

		/* print result */
		g_printerr ("\t%s - %s", uloc_getAvailable(i), printable);

		keywords = uloc_openKeywords (uloc_getAvailable(i), &status);
		if (keywords) {
			UErrorCode kstatus = U_ZERO_ERROR;

			g_printerr ("[");

			while ((keyword = uenum_next (keywords, NULL, &kstatus)) != NULL)
				g_printerr (" %s ", keyword);

			g_printerr ("]");

			uenum_close (keywords);
		}
		g_printerr ("\n");
	}
}
#endif

static gchar *
canonicalize_locale (const gchar  *posix_locale,
		     GError      **error)
{
	UErrorCode status = U_ZERO_ERROR;
	gchar  locale_buffer[LOCALE_BUFFER_LEN];
	gchar  language_buffer[8];
	gchar *icu_locale;
	gchar *final_locale;
	gint   len;
	const gchar *collation_type = NULL;

	len = uloc_canonicalize (posix_locale, locale_buffer, LOCALE_BUFFER_LEN, &status);

	if (U_FAILURE (status)) {
		g_set_error (error, E_COLLATOR_ERROR,
			     E_COLLATOR_ERROR_INVALID_LOCALE,
			     "Failed to interpret locale '%s' (%s)",
			     posix_locale,
			     u_errorName (status));
		return NULL;
	}

	if (len > LOCALE_BUFFER_LEN) {
		icu_locale = g_malloc (len);

		uloc_canonicalize (posix_locale, icu_locale, len, &status);
	} else {
		icu_locale = g_strndup (locale_buffer, len);
	}

	status = U_ZERO_ERROR;
	len = uloc_getLanguage (icu_locale, language_buffer, 8, &status);

	if (U_FAILURE (status)) {
		g_set_error (error, E_COLLATOR_ERROR,
			     E_COLLATOR_ERROR_INVALID_LOCALE,
			     "Failed to interpret language for locale '%s': %s",
			     icu_locale,
			     u_errorName (status));
		g_free (icu_locale);
		return NULL;
	}

	/* Add 'phonebook' tailoring to certain locales */
	if (len < 8 &&
	    (strcmp (language_buffer, "de") == 0 ||
	     strcmp (language_buffer, "fi") == 0)) {

		collation_type = "phonebook";
	}

	if (collation_type != NULL)
		final_locale = g_strconcat (icu_locale, "@collation=", collation_type, NULL);
	else {
		final_locale = icu_locale;
		icu_locale = NULL;
	}

	g_free (icu_locale);

	return final_locale;
}

/* All purpose character encoding function, encodes text
 * to a UChar from UTF-8 and first ensures that the string
 * is valid UTF-8
 */
static const UChar *
convert_to_ustring (const gchar  *string,
		    UChar        *buffer,
		    gint          buffer_len,
		    gint         *result_len,
		    UChar       **free_me,
		    GError      **error)
{
	UErrorCode status = U_ZERO_ERROR;
	const gchar *source_utf8;
	gchar *alloc_utf8 = NULL;
	gint   converted_len = 0;
	UChar *converted_buffer;

	/* First make sure we're dealing with utf8 */
	if (g_utf8_validate (string, -1, NULL))
		source_utf8 = string;
	else {
		alloc_utf8 = e_util_utf8_make_valid (string);
		source_utf8 = alloc_utf8;
	}

	/* First pass, try converting to UChar in the given buffer */
	converted_buffer = u_strFromUTF8Lenient (buffer,
						 buffer_len,
						 &converted_len,
						 source_utf8,
						 -1,
						 &status);

	/* Set the result length right away... */
	*result_len = converted_len;

	if (U_FAILURE (status)) {
		converted_buffer = NULL;
		goto out;
	}

	/* Second pass, allocate a buffer big enough and then convert */
	if (converted_len > buffer_len) {
		*free_me = g_new (UChar, converted_len);

		converted_buffer = u_strFromUTF8Lenient (*free_me,
							 converted_len,
							 NULL,
							 source_utf8,
							 -1,
							 &status);

		if (U_FAILURE (status)) {
			g_free (*free_me);
			*free_me = NULL;
			converted_buffer = NULL;
			goto out;
		}
	}

 out:
	g_free (alloc_utf8);

	if (U_FAILURE (status))
		g_set_error (error, E_COLLATOR_ERROR,
			     E_COLLATOR_ERROR_CONVERSION,
			     "Error occured while converting character encoding (%s)",
			     u_errorName (status));

	return converted_buffer;
}

/*****************************************************
 *                        API                        *
 *****************************************************/

/**
 * e_collator_new:
 * @locale: The locale under which to sort
 * @error: (allow none): A location to store a #GError from the #E_COLLATOR_ERROR domain
 *
 * Creates a new #ECollator for the given @locale,
 * the returned collator should be freed with e_collator_unref().
 *
 * Returns: (transfer full): A newly created #ECollator.
 *
 * Since: 3.10
 */
ECollator *
e_collator_new (const gchar     *locale,
		GError         **error)
{
	ECollator *collator;
	UCollator *coll;
	UErrorCode status = U_ZERO_ERROR;
	gchar     *icu_locale;

	g_return_val_if_fail (locale && locale[0], NULL);

#if ENABLE_DEBUGGING
	print_available_locales ();
#endif

	icu_locale = canonicalize_locale (locale, error);
	if (!icu_locale)
		return NULL;

	coll = ucol_open (icu_locale, &status);

	if (U_FAILURE (status)) {
		g_set_error (error, E_COLLATOR_ERROR,
			     E_COLLATOR_ERROR_OPEN,
			     "Unable to open collator for locale '%s' (%s)",
			     icu_locale,
			     u_errorName (status));

		g_free (icu_locale);
		ucol_close (coll);
		return NULL;
	}

	g_free (icu_locale);

	ucol_setStrength (coll, UCOL_DEFAULT_STRENGTH);

	collator = g_slice_new0 (ECollator);
	collator->coll = coll;
	collator->ref_count = 1;

	return collator;
}

/**
 * e_collator_ref:
 * @collator: An #ECollator
 *
 * Increases the reference count of @collator.
 *
 * Returns: (transfer full): @collator
 *
 * Since: 3.10
 */
ECollator *
e_collator_ref (ECollator *collator)
{
	g_return_val_if_fail (collator != NULL, NULL);

	collator->ref_count++;

	return collator;
}

/**
 * e_collator_unref:
 * @collator: An #ECollator
 *
 * Decreases the reference count of @collator.
 * If the reference count reaches 0 then the collator is freed
 *
 * Since: 3.10
 */
void
e_collator_unref (ECollator *collator)
{
	g_return_if_fail (collator != NULL);

	collator->ref_count--;

	if (collator->ref_count < 0)
		g_warning ("Unbalanced reference count in ECollator");

	if (collator->ref_count == 0) {

		if (collator->coll)
			ucol_close (collator->coll);

		g_slice_free (ECollator, collator);
	}
}

/**
 * e_collator_generate_key:
 * @collator: An #ECollator
 * @str: The string to generate a collation key for
 * @error: (allow none): A location to store a #GError from the #E_COLLATOR_ERROR domain
 *
 * Generates a collation key for @str, the result of comparing
 * two collation keys with strcmp() will be the same result
 * of calling e_collator_collate() on the same original strings.
 *
 * This function will first ensure that @str is valid UTF-8 encoded.
 *
 * Returns: (transfer full): A collation key for @str, or %NULL on failure with @error set.
 *
 * Since: 3.10
 */
gchar *
e_collator_generate_key (ECollator    *collator,
			 const gchar  *str,
			 GError      **error)
{
	UChar  source_buffer[CONVERT_BUFFER_LEN];
	UChar *free_me = NULL;
	const UChar *source;
	gchar stack_buffer[COLLATION_KEY_BUFFER_LEN];
	gchar *collation_key;
	gint key_len, source_len = 0;

	g_return_val_if_fail (collator != NULL, NULL);
	g_return_val_if_fail (str != NULL, NULL);

	source = convert_to_ustring (str,
				     source_buffer,
				     CONVERT_BUFFER_LEN,
				     &source_len,
				     &free_me,
				     error);

	if (!source)
		return NULL;

	/* First try to generate a key in a predefined buffer size */
	key_len = ucol_getSortKey (collator->coll, source, source_len,
				   (guchar *)stack_buffer, COLLATION_KEY_BUFFER_LEN);

	if (key_len > COLLATION_KEY_BUFFER_LEN) {

		/* Stack buffer wasn't large enough, regenerate into a new buffer
		 * (add a byte for a trailing NULL char)
		 */
		collation_key = g_malloc (key_len + 1);

		ucol_getSortKey (collator->coll, source, source_len,
				 (guchar *)collation_key, key_len);

		/* Just being paranoid, make sure we're null terminated since the API
		 * doesn't specify if the result length is null character inclusive
		 */
		collation_key[key_len] = '\0';
	} else {
		/* Make a duplicate of the generated key on the heap */
		collation_key = g_strndup (stack_buffer, key_len);
	}

	g_free (free_me);

	return (gchar *)collation_key;
}

/**
 * e_collator_collate:
 * @collator: An #ECollator
 * @str_a: A string to compare
 * @str_b: The string to compare with @str_a
 * @result: (out): A location to store the comparison result
 * @error: (allow none): A location to store a #GError from the #E_COLLATOR_ERROR domain
 *
 * Compares @str_a with @str_b, the order of strings is determined by the parameters of @collator.
 *
 * The @result will be set to integer less than, equal to, or greater than zero if @str_a is found,
 * respectively, to be less than, to match, or be greater than @str_b.
 *
 * This function will first ensure that both strings are valid UTF-8.
 *
 * Returns: %TRUE on success, otherwise if %FALSE is returned then @error will be set.
 */
gboolean
e_collator_collate (ECollator    *collator,
		    const gchar  *str_a,
		    const gchar  *str_b,
		    gint         *result,
		    GError      **error)
{
	UCollationResult ucol_result = UCOL_EQUAL;
	UChar  buffer_a[CONVERT_BUFFER_LEN], buffer_b[CONVERT_BUFFER_LEN];
	UChar *free_me_a = NULL, *free_me_b = NULL;
	const UChar *ustr_a, *ustr_b;
	gint len_a = 0, len_b = 0;
	gboolean success = TRUE;

	g_return_val_if_fail (collator != NULL, -1);
	g_return_val_if_fail (str_a != NULL, -1);
	g_return_val_if_fail (str_b != NULL, -1);
	g_return_val_if_fail (result != NULL, -1);

	ustr_a = convert_to_ustring (str_a,
				     buffer_a, CONVERT_BUFFER_LEN,
				     &len_a,
				     &free_me_a,
				     error);

	if (!ustr_a) {
		success = FALSE;
		goto out;
	}

	ustr_b = convert_to_ustring (str_b,
				     buffer_b, CONVERT_BUFFER_LEN,
				     &len_b,
				     &free_me_b,
				     error);

	if (!ustr_b) {
		success = FALSE;
		goto out;
	}

	ucol_result = ucol_strcoll (collator->coll, ustr_a, len_a, ustr_b, len_b);

 out:
	g_free (free_me_a);
	g_free (free_me_b);

	if (success) {

		switch (ucol_result) {
		case UCOL_GREATER: *result = 1;  break;
		case UCOL_LESS:    *result = -1; break;
		case UCOL_EQUAL:   *result = 0;  break;
		}

	}

	return success;
}
